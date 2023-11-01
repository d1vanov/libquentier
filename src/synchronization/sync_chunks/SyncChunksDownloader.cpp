/*
 * Copyright 2022-2023 Dmitry Ivanov
 *
 * This file is part of libquentier
 *
 * libquentier is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, version 3 of the License.
 *
 * libquentier is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with libquentier. If not, see <http://www.gnu.org/licenses/>.
 */

#include "SyncChunksDownloader.h"
#include "../Utils.h"
#include "Utils.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/RuntimeError.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/threading/Future.h>
#include <quentier/threading/QtFutureContinuations.h>
#include <quentier/utility/cancelers/ICanceler.h>

#include <synchronization/INoteStoreProvider.h>

#include <qevercloud/services/INoteStore.h>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include <quentier/threading/Qt5Promise.h>
#endif

#include <QTextStream>
#include <QThread>

#include <functional>

namespace quentier::synchronization {

namespace {

[[nodiscard]] QFuture<qevercloud::SyncChunk> downloadSingleUserOwnSyncChunk(
    const qint32 afterUsn, const SynchronizationMode synchronizationMode,
    qevercloud::INoteStore & noteStore, QThread * currentThread,
    qevercloud::IRequestContextPtr ctx)
{
    QNDEBUG(
        "synchronization::SyncChunksDownloader",
        "downloadSingleUserOwnSyncChunk: afterUsn = " << afterUsn
            << ", synchronization mode = " << synchronizationMode);

    qevercloud::SyncChunkFilter filter;
    filter.setIncludeNotebooks(true);
    filter.setIncludeNotes(true);
    filter.setIncludeTags(true);
    filter.setIncludeSearches(true);
    filter.setIncludeNoteResources(true);
    filter.setIncludeNoteAttributes(true);
    filter.setIncludeNoteApplicationDataFullMap(true);
    filter.setIncludeNoteResourceApplicationDataFullMap(true);
    filter.setIncludeLinkedNotebooks(true);

    if (synchronizationMode == SynchronizationMode::Incremental) {
        filter.setIncludeExpunged(true);
        filter.setIncludeResources(true);
    }

    constexpr qint32 maxEntries = 50;

    auto promise = std::make_shared<QPromise<qevercloud::SyncChunk>>();
    auto future = promise->future();

    promise->start();

    auto thenFuture = threading::then(
        noteStore.getFilteredSyncChunkAsync(
            afterUsn, maxEntries, filter, std::move(ctx)),
        currentThread,
        [promise](qevercloud::SyncChunk syncChunk) mutable // NOLINT
        {
            QNDEBUG(
                "synchronization::SyncChunksDownloader",
                "downloadSingleUserOwnSyncChunk: received sync chunk, "
                    << "high USN = "
                    << (syncChunk.chunkHighUSN()
                        ? QString::number(*syncChunk.chunkHighUSN())
                        : QStringLiteral("<none>")));
            promise->addResult(std::move(syncChunk));
            promise->finish();
        });

    threading::onFailed(
        std::move(thenFuture), currentThread, [promise](const QException & e) {
            QNWARNING(
                "synchronization::SyncChunksDownloader",
                "Failed to download sync chunk: " << e.what());
            promise->setException(e);
            promise->finish();
        });

    return future;
}

[[nodiscard]] QFuture<qevercloud::SyncChunk>
    downloadSingleLinkedNotebookSyncChunk(
        const qevercloud::LinkedNotebook & linkedNotebook,
        const qint32 afterUsn, const SynchronizationMode synchronizationMode,
        qevercloud::INoteStore & noteStore, QThread * currentThread,
        qevercloud::IRequestContextPtr ctx)
{
    QNDEBUG(
        "synchronization::SyncChunksDownloader",
        "downloadSingleLinkedNotebookSyncChunk: "
            << linkedNotebookInfo(linkedNotebook) << ", after usn = "
            << afterUsn << ", sync mode = " << synchronizationMode);

    Q_ASSERT(linkedNotebook.guid());

    constexpr qint32 maxEntries = 50;

    auto promise = std::make_shared<QPromise<qevercloud::SyncChunk>>();
    auto future = promise->future();

    promise->start();

    auto thenFuture = threading::then(
        noteStore.getLinkedNotebookSyncChunkAsync(
            linkedNotebook, afterUsn, maxEntries,
            (synchronizationMode == SynchronizationMode::Full), std::move(ctx)),
        currentThread,
        [promise, linkedNotebookGuid = *linkedNotebook.guid()](
            qevercloud::SyncChunk syncChunk) mutable // NOLINT
        {
            utils::setLinkedNotebookGuidToSyncChunkEntries(
                linkedNotebookGuid, syncChunk);

            promise->addResult(std::move(syncChunk));
            promise->finish();
        });

    threading::onFailed(
        std::move(thenFuture), currentThread, [promise](const QException & e) {
            promise->setException(e);
            promise->finish();
        });

    return future;
}

using SingleSyncChunkDownloader = std::function<QFuture<qevercloud::SyncChunk>(
    qint32, SynchronizationMode, qevercloud::INoteStore &, QThread *,
    qevercloud::IRequestContextPtr)>;

void processSingleDownloadedSyncChunk(
    qint32 lastPreviousUsn, SynchronizationMode synchronizationMode,
    qevercloud::INoteStorePtr noteStore, qevercloud::IRequestContextPtr ctx,
    utility::cancelers::ICancelerPtr canceler,
    ISyncChunksDownloader::ICallbackWeakPtr callbackWeak,
    std::optional<qevercloud::LinkedNotebook> linkedNotebook,
    QThread * currentThread,
    SingleSyncChunkDownloader singleSyncChunkDownloader,
    std::shared_ptr<QPromise<ISyncChunksDownloader::SyncChunksResult>> promise,
    QList<qevercloud::SyncChunk> runningResult,
    qevercloud::SyncChunk syncChunk);

void downloadSyncChunksList(
    const qint32 lastPreviousUsn, const qint32 afterUsn, // NOLINT
    const SynchronizationMode synchronizationMode,
    qevercloud::INoteStorePtr noteStore, qevercloud::IRequestContextPtr ctx,
    utility::cancelers::ICancelerPtr canceler,
    ISyncChunksDownloader::ICallbackWeakPtr callbackWeak,
    std::optional<qevercloud::LinkedNotebook> linkedNotebook,
    QThread * currentThread,
    SingleSyncChunkDownloader singleSyncChunkDownloader,
    std::shared_ptr<QPromise<ISyncChunksDownloader::SyncChunksResult>> promise,
    QList<qevercloud::SyncChunk> runningResult = {})
{
    QNDEBUG(
        "synchronization::SyncChunksDownloader",
        "downloadSyncChunksList: last previous usn = "
            << lastPreviousUsn << ", after usn = " << afterUsn
            << ", sync mode = " << synchronizationMode << ", "
            << (linkedNotebook ? linkedNotebookInfo(*linkedNotebook)
                               : "user own")
            << " sync chunks");

    Q_ASSERT(noteStore);
    Q_ASSERT(singleSyncChunkDownloader);
    Q_ASSERT(canceler);

    if (canceler->isCanceled()) {
        QNDEBUG(
            "synchronization::SyncChunksDownloader",
            "Sync chunks downloading was canceled");
        promise->addResult(ISyncChunksDownloader::SyncChunksResult{
            std::move(runningResult),
            std::make_shared<RuntimeError>(ErrorString{
                QStringLiteral("Sync chunks downloading was canceled")})});
        promise->finish();
        return;
    }

    auto singleSyncChunkFuture = singleSyncChunkDownloader(
        afterUsn, synchronizationMode, *noteStore, currentThread, ctx);

    auto thenFuture = threading::then(
        std::move(singleSyncChunkFuture), currentThread,
        [synchronizationMode, promise, runningResult, lastPreviousUsn,
         currentThread, ctx = std::move(ctx), canceler = std::move(canceler),
         callbackWeak = std::move(callbackWeak),
         linkedNotebook = std::move(linkedNotebook),
         singleSyncChunkDownloader = std::move(singleSyncChunkDownloader),
         noteStore =
             std::move(noteStore)](qevercloud::SyncChunk syncChunk) mutable {
            QNDEBUG(
                "synchronization::SyncChunksDownloader",
                "Downloaded single sync chunk: lastPreviousUsn = "
                    << lastPreviousUsn << ", chunk high usn = "
                    << (syncChunk.chunkHighUSN()
                        ? QString::number(*syncChunk.chunkHighUSN())
                        : QStringLiteral("<none>")));

            processSingleDownloadedSyncChunk(
                lastPreviousUsn, synchronizationMode, std::move(noteStore),
                std::move(ctx), std::move(canceler), std::move(callbackWeak),
                std::move(linkedNotebook), currentThread,
                std::move(singleSyncChunkDownloader), std::move(promise),
                std::move(runningResult), std::move(syncChunk));
        });

    threading::onFailed(
        std::move(thenFuture), currentThread,
        [promise, runningResult](const QException & e) mutable {
            QNWARNING(
                "synchronization::SyncChunksDownloader",
                "Failed to download sync chunk: " << e.what());
            promise->addResult(ISyncChunksDownloader::SyncChunksResult{
                std::move(runningResult),
                std::shared_ptr<QException>(e.clone())});
            promise->finish();
        });
}

void processSingleDownloadedSyncChunk(
    const qint32 lastPreviousUsn, const SynchronizationMode synchronizationMode,
    qevercloud::INoteStorePtr noteStore, qevercloud::IRequestContextPtr ctx,
    utility::cancelers::ICancelerPtr canceler,
    ISyncChunksDownloader::ICallbackWeakPtr callbackWeak,
    std::optional<qevercloud::LinkedNotebook> linkedNotebook,
    QThread * currentThread,
    SingleSyncChunkDownloader singleSyncChunkDownloader,
    std::shared_ptr<QPromise<ISyncChunksDownloader::SyncChunksResult>> promise,
    QList<qevercloud::SyncChunk> runningResult,
    qevercloud::SyncChunk syncChunk) // NOLINT
{
    QNDEBUG(
        "synchronization::SyncChunksDownloader",
        "processSingleDownloadedSyncChunk: "
            << (linkedNotebook ? linkedNotebookInfo(*linkedNotebook)
                               : QStringLiteral("user own"))
            << " sync chunks, last previous usn = " << lastPreviousUsn
            << ", sync mode = " << synchronizationMode);

    if (Q_UNLIKELY(!syncChunk.chunkHighUSN())) {
        QNWARNING(
            "synchronization::SyncChunksDownloader",
            "Downloaded sync chunk without chunkHighUsn: " << syncChunk);

        promise->addResult(ISyncChunksDownloader::SyncChunksResult{
            std::move(runningResult),
            std::make_shared<RuntimeError>(ErrorString{
                QStringLiteral("Got sync chunk without chunkHighUSN")})});
        promise->finish();
        return;
    }

    runningResult << syncChunk;

    if (const auto callback = callbackWeak.lock()) {
        if (linkedNotebook) {
            callback->onLinkedNotebookSyncChunksDownloadProgress(
                *syncChunk.chunkHighUSN(), syncChunk.updateCount(),
                lastPreviousUsn, *linkedNotebook);
        }
        else {
            callback->onUserOwnSyncChunksDownloadProgress(
                *syncChunk.chunkHighUSN(), syncChunk.updateCount(),
                lastPreviousUsn);
        }
    }

    if (*syncChunk.chunkHighUSN() >= syncChunk.updateCount()) {
        QNDEBUG(
            "synchronization::SyncChunksDownloader",
            "Downloaded all sync chunks");
        promise->addResult(ISyncChunksDownloader::SyncChunksResult{
            std::move(runningResult), nullptr});
        promise->finish();
        return;
    }

    downloadSyncChunksList(
        lastPreviousUsn, *syncChunk.chunkHighUSN(), synchronizationMode,
        std::move(noteStore), std::move(ctx), std::move(canceler),
        std::move(callbackWeak), std::move(linkedNotebook), currentThread,
        std::move(singleSyncChunkDownloader), std::move(promise),
        std::move(runningResult));
}

} // namespace

SyncChunksDownloader::SyncChunksDownloader(
    INoteStoreProviderPtr noteStoreProvider,
    qevercloud::IRetryPolicyPtr retryPolicy) :
    m_noteStoreProvider{std::move(noteStoreProvider)},
    m_retryPolicy{std::move(retryPolicy)}
{
    if (Q_UNLIKELY(!m_noteStoreProvider)) {
        throw InvalidArgument{ErrorString{
            QStringLiteral("SyncChunksDownloader ctor: note store is null")}};
    }
}

QFuture<ISyncChunksDownloader::SyncChunksResult>
    SyncChunksDownloader::downloadSyncChunks(
        qint32 afterUsn, SynchronizationMode syncMode,
        qevercloud::IRequestContextPtr ctx,
        utility::cancelers::ICancelerPtr canceler,
        ICallbackWeakPtr callbackWeak)
{
    QNDEBUG(
        "synchronization::SyncChunksDownloader",
        "SyncChunksDownloader::downloadSyncChunks: after usn = "
            << afterUsn << ", sync mode = " << syncMode);

    auto promise =
        std::make_shared<QPromise<ISyncChunksDownloader::SyncChunksResult>>();

    auto future = promise->future();
    promise->start();

    auto noteStoreFuture =
        m_noteStoreProvider->userOwnNoteStore(ctx, m_retryPolicy);

    auto * currentThread = QThread::currentThread();

    threading::thenOrFailed(
        std::move(noteStoreFuture), currentThread, promise,
        [promise, afterUsn, syncMode, currentThread,
         canceler = std::move(canceler), ctx = std::move(ctx),
         callbackWeak = std::move(callbackWeak)](
            qevercloud::INoteStorePtr noteStore) mutable {
            Q_ASSERT(noteStore);

            QNDEBUG(
                "synchronization::SyncChunksDownloader",
                "Received user own note store, starting the download");

            downloadSyncChunksList(
                afterUsn, afterUsn, syncMode, std::move(noteStore),
                std::move(ctx), std::move(canceler), std::move(callbackWeak),
                std::nullopt, // linkedNotebook
                currentThread,
                [](const qint32 afterUsn,
                   const SynchronizationMode synchronizationMode,
                   qevercloud::INoteStore & noteStore, QThread * currentThread,
                   qevercloud::IRequestContextPtr ctx) {
                    return downloadSingleUserOwnSyncChunk(
                        afterUsn, synchronizationMode, noteStore, currentThread,
                        std::move(ctx));
                },
                promise);
        });

    return future;
}

QFuture<ISyncChunksDownloader::SyncChunksResult>
    SyncChunksDownloader::downloadLinkedNotebookSyncChunks(
        qevercloud::LinkedNotebook linkedNotebook, qint32 afterUsn,
        SynchronizationMode syncMode, qevercloud::IRequestContextPtr ctx,
        utility::cancelers::ICancelerPtr canceler,
        ICallbackWeakPtr callbackWeak)
{
    QNDEBUG(
        "synchronization::SyncChunksDownloader",
        "SyncChunksDownloader::downloadLinkedNotebookSyncChunks: "
            << linkedNotebookInfo(linkedNotebook)
            << ", after usn = " << afterUsn << ", sync mode = " << syncMode);

    if (Q_UNLIKELY(!linkedNotebook.guid())) {
        return threading::makeExceptionalFuture<
            ISyncChunksDownloader::SyncChunksResult>(
            InvalidArgument{ErrorString{QStringLiteral(
                "Cannot download linked notebook sync chunks: linked notebook "
                "has no guid")}});
    }

    auto promise =
        std::make_shared<QPromise<ISyncChunksDownloader::SyncChunksResult>>();

    auto future = promise->future();
    promise->start();

    auto noteStoreFuture = m_noteStoreProvider->linkedNotebookNoteStore(
        *linkedNotebook.guid(), ctx, m_retryPolicy);

    QThread * currentThread = QThread::currentThread();

    threading::thenOrFailed(
        std::move(noteStoreFuture), currentThread, promise,
        [promise, afterUsn, syncMode, currentThread,
         canceler = std::move(canceler), ctx = std::move(ctx),
         callbackWeak = std::move(callbackWeak),
         linkedNotebook = std::move(linkedNotebook)](
            qevercloud::INoteStorePtr noteStore) mutable {
            Q_ASSERT(noteStore);

            downloadSyncChunksList(
                afterUsn, afterUsn, syncMode, std::move(noteStore),
                std::move(ctx), std::move(canceler), std::move(callbackWeak),
                linkedNotebook, currentThread,
                [linkedNotebook](
                    const qint32 afterUsn,
                    const SynchronizationMode synchronizationMode,
                    qevercloud::INoteStore & noteStore, QThread * currentThread,
                    qevercloud::IRequestContextPtr ctx) {
                    return downloadSingleLinkedNotebookSyncChunk(
                        linkedNotebook, afterUsn, synchronizationMode,
                        noteStore, currentThread, std::move(ctx));
                },
                promise);
        });

    return future;
}

} // namespace quentier::synchronization
