/*
 * Copyright 2022 Dmitry Ivanov
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

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/RuntimeError.h>

#include <quentier/threading/Future.h>
#include <quentier/threading/QtFutureContinuations.h>

#include <qevercloud/services/INoteStore.h>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include <quentier/threading/Qt5Promise.h>
#endif

#include <functional>

namespace quentier::synchronization {

namespace {

[[nodiscard]] QFuture<qevercloud::SyncChunk> downloadSingleUserOwnSyncChunk(
    const qint32 afterUsn, const SynchronizationMode synchronizationMode,
    qevercloud::INoteStore & noteStore, qevercloud::IRequestContextPtr ctx)
{
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
        [promise](qevercloud::SyncChunk syncChunk) mutable // NOLINT
        {
            promise->addResult(std::move(syncChunk));
            promise->finish();
        });

    threading::onFailed(std::move(thenFuture), [promise](const QException & e) {
        promise->setException(e);
        promise->finish();
    });

    return future;
}

[[nodiscard]] QFuture<qevercloud::SyncChunk>
    downloadSingleLinkedNotebookSyncChunk(
        const qevercloud::LinkedNotebook & linkedNotebook,
        const qint32 afterUsn, const SynchronizationMode synchronizationMode,
        qevercloud::INoteStore & noteStore, qevercloud::IRequestContextPtr ctx)
{
    constexpr qint32 maxEntries = 50;

    auto promise = std::make_shared<QPromise<qevercloud::SyncChunk>>();
    auto future = promise->future();

    promise->start();

    auto thenFuture = threading::then(
        noteStore.getLinkedNotebookSyncChunkAsync(
            linkedNotebook, afterUsn, maxEntries,
            (synchronizationMode == SynchronizationMode::Full), std::move(ctx)),
        [promise = std::move(promise)](
            qevercloud::SyncChunk syncChunk) mutable // NOLINT
        {
            promise->addResult(std::move(syncChunk));
            promise->finish();
        });

    threading::onFailed(std::move(thenFuture), [promise](const QException & e) {
        promise->setException(e);
        promise->finish();
    });

    return future;
}

using SingleSyncChunkDownloader = std::function<QFuture<qevercloud::SyncChunk>(
    qint32, SynchronizationMode, qevercloud::INoteStore &,
    qevercloud::IRequestContextPtr)>;

void processSingleDownloadedSyncChunk(
    SynchronizationMode synchronizationMode,
    qevercloud::INoteStorePtr noteStore, qevercloud::IRequestContextPtr ctx,
    SingleSyncChunkDownloader singleSyncChunkDownloader,
    std::shared_ptr<QPromise<ISyncChunksDownloader::SyncChunksResult>> promise,
    QList<qevercloud::SyncChunk> runningResult,
    qevercloud::SyncChunk syncChunk);

void downloadSyncChunksList(
    const qint32 afterUsn, const SynchronizationMode synchronizationMode,
    qevercloud::INoteStorePtr noteStore, qevercloud::IRequestContextPtr ctx,
    SingleSyncChunkDownloader singleSyncChunkDownloader,
    std::shared_ptr<QPromise<ISyncChunksDownloader::SyncChunksResult>> promise,
    QList<qevercloud::SyncChunk> runningResult = {})
{
    Q_ASSERT(noteStore);
    Q_ASSERT(singleSyncChunkDownloader);

    if (promise->isCanceled()) {
        return;
    }

    auto singleSyncChunkFuture = singleSyncChunkDownloader(
        afterUsn, synchronizationMode, *noteStore, ctx);

    auto thenFuture = threading::then(
        std::move(singleSyncChunkFuture),
        [synchronizationMode, promise, runningResult, ctx = std::move(ctx),
         singleSyncChunkDownloader = std::move(singleSyncChunkDownloader),
         noteStore =
             std::move(noteStore)](qevercloud::SyncChunk syncChunk) mutable {
            processSingleDownloadedSyncChunk(
                synchronizationMode, std::move(noteStore), std::move(ctx),
                std::move(singleSyncChunkDownloader), std::move(promise),
                std::move(runningResult), std::move(syncChunk));
        });

    threading::onFailed(
        std::move(thenFuture),
        [promise, runningResult](const QException & e) mutable {
            promise->addResult(ISyncChunksDownloader::SyncChunksResult{
                std::move(runningResult),
                std::shared_ptr<QException>(
                    e.clone())});

            promise->finish();
        });
}

void processSingleDownloadedSyncChunk(
    const SynchronizationMode synchronizationMode,
    qevercloud::INoteStorePtr noteStore, qevercloud::IRequestContextPtr ctx,
    SingleSyncChunkDownloader singleSyncChunkDownloader,
    std::shared_ptr<QPromise<ISyncChunksDownloader::SyncChunksResult>> promise,
    QList<qevercloud::SyncChunk> runningResult,
    qevercloud::SyncChunk syncChunk) // NOLINT
{
    if (Q_UNLIKELY(!syncChunk.chunkHighUSN())) {
        promise->addResult(ISyncChunksDownloader::SyncChunksResult{
            std::move(runningResult),
            std::make_shared<RuntimeError>(ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::SyncChunksDownloader",
            "Got sync chunk without chunkHighUSN")})});

        promise->finish();
        return;
    }

    runningResult << syncChunk;

    if (*syncChunk.chunkHighUSN() >= syncChunk.updateCount()) {
        promise->addResult(ISyncChunksDownloader::SyncChunksResult{
            std::move(runningResult), nullptr});

        promise->finish();
        return;
    }

    downloadSyncChunksList(
        *syncChunk.chunkHighUSN(), synchronizationMode, std::move(noteStore),
        std::move(ctx), std::move(singleSyncChunkDownloader),
        std::move(promise), std::move(runningResult));
}

} // namespace

SyncChunksDownloader::SyncChunksDownloader(
    const SynchronizationMode synchronizationMode,
    qevercloud::INoteStorePtr noteStore) :
    m_synchronizationMode{synchronizationMode},
    m_noteStore{std::move(noteStore)}
{
    if (Q_UNLIKELY(!m_noteStore)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::SyncChunksDownloader",
            "SyncChunksDownloader ctor: note store is null")}};
    }
}

QFuture<ISyncChunksDownloader::SyncChunksResult>
    SyncChunksDownloader::downloadSyncChunks(
        qint32 afterUsn, qevercloud::IRequestContextPtr ctx)
{
    auto promise =
        std::make_shared<QPromise<ISyncChunksDownloader::SyncChunksResult>>();

    auto future = promise->future();
    promise->start();

    downloadSyncChunksList(
        afterUsn, m_synchronizationMode, m_noteStore, std::move(ctx),
        [](const qint32 afterUsn, const SynchronizationMode synchronizationMode,
           qevercloud::INoteStore & noteStore,
           qevercloud::IRequestContextPtr ctx) {
            return downloadSingleUserOwnSyncChunk(
                afterUsn, synchronizationMode, noteStore, std::move(ctx));
        },
        promise);

    return future;
}

QFuture<ISyncChunksDownloader::SyncChunksResult>
    SyncChunksDownloader::downloadLinkedNotebookSyncChunks(
        qevercloud::LinkedNotebook linkedNotebook, qint32 afterUsn,
        qevercloud::IRequestContextPtr ctx)
{
    auto promise =
        std::make_shared<QPromise<ISyncChunksDownloader::SyncChunksResult>>();

    auto future = promise->future();
    promise->start();

    downloadSyncChunksList(
        afterUsn, m_synchronizationMode, m_noteStore, std::move(ctx),
        [linkedNotebook = std::move(linkedNotebook)](
            const qint32 afterUsn,
            const SynchronizationMode synchronizationMode,
            qevercloud::INoteStore & noteStore,
            qevercloud::IRequestContextPtr ctx) {
            return downloadSingleLinkedNotebookSyncChunk(
                linkedNotebook, afterUsn, synchronizationMode, noteStore,
                std::move(ctx));
        },
        promise);

    return future;
}

} // namespace quentier::synchronization
