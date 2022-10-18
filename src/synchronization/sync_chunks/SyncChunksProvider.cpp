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

#include "SyncChunksProvider.h"
#include "Utils.h"

#include <synchronization/sync_chunks/ISyncChunksDownloader.h>
#include <synchronization/sync_chunks/ISyncChunksStorage.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/threading/Future.h>
#include <quentier/threading/QtFutureContinuations.h>
#include <quentier/utility/cancelers/ICanceler.h>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include <quentier/threading/Qt5Promise.h>
#endif

#include <algorithm>
#include <functional>
#include <optional>
#include <utility>

namespace quentier::synchronization {

namespace {

class SyncChunksDownloaderCallback : public ISyncChunksDownloader::ICallback
{
public:
    SyncChunksDownloaderCallback(
        ISyncChunksProvider::ICallbackWeakPtr callbackWeak,
        std::optional<qint32> actualLastPreviousUsn = std::nullopt) :
        m_callbackWeak{std::move(callbackWeak)},
        m_actualLastPreviousUsn{actualLastPreviousUsn}
    {}

    void onUserOwnSyncChunksDownloadProgress(
        qint32 highestDownloadedUsn, qint32 highestServerUsn,
        qint32 lastPreviousUsn) override
    {
        if (const auto callback = m_callbackWeak.lock()) {
            callback->onUserOwnSyncChunksDownloadProgress(
                highestDownloadedUsn, highestServerUsn,
                m_actualLastPreviousUsn.value_or(lastPreviousUsn));
        }
    }

    void onLinkedNotebookSyncChunksDownloadProgress(
        qint32 highestDownloadedUsn, qint32 highestServerUsn,
        qint32 lastPreviousUsn,
        qevercloud::LinkedNotebook linkedNotebook) override
    {
        if (const auto callback = m_callbackWeak.lock()) {
            callback->onLinkedNotebookSyncChunksDownloadProgress(
                highestDownloadedUsn, highestServerUsn,
                m_actualLastPreviousUsn.value_or(lastPreviousUsn),
                std::move(linkedNotebook));
        }
    }

private:
    const ISyncChunksProvider::ICallbackWeakPtr m_callbackWeak;
    const std::optional<qint32> m_actualLastPreviousUsn;
};

using StoredSyncChunksUsnRangeFetcher =
    std::function<QList<std::pair<qint32, qint32>>()>;

using SyncChunksDownloader =
    std::function<QFuture<ISyncChunksDownloader::SyncChunksResult>(
        qint32, qevercloud::IRequestContextPtr,
        utility::cancelers::ICancelerPtr,
        ISyncChunksProvider::ICallbackWeakPtr callbackWeak)>;

using StoredSyncChunksFetcher =
    std::function<QList<qevercloud::SyncChunk>(qint32)>;

using SyncChunksStorer = std::function<void(QList<qevercloud::SyncChunk>)>;

[[nodiscard]] QFuture<QList<qevercloud::SyncChunk>> fetchSyncChunksImpl(
    qint32 afterUsn, qevercloud::IRequestContextPtr ctx,
    utility::cancelers::ICancelerPtr canceler,
    ISyncChunksProvider::ICallbackWeakPtr callbackWeak,
    const StoredSyncChunksUsnRangeFetcher & storedSyncChunksUsnRangeFetcher,
    SyncChunksDownloader syncChunksDownloader,
    const StoredSyncChunksFetcher & storedSyncChunksFetcher,
    SyncChunksStorer syncChunksStorer)
{
    Q_ASSERT(storedSyncChunksUsnRangeFetcher);
    Q_ASSERT(syncChunksDownloader);
    Q_ASSERT(storedSyncChunksFetcher);
    Q_ASSERT(syncChunksStorer);
    Q_ASSERT(canceler);

    auto downloadSyncChunks =
        [syncChunksDownloader = std::move(syncChunksDownloader),
         syncChunksStorer = std::move(syncChunksStorer)](
            qint32 afterUsn, qevercloud::IRequestContextPtr ctx,
            utility::cancelers::ICancelerPtr canceler,
            ISyncChunksProvider::ICallbackWeakPtr callbackWeak) mutable {
            auto promise =
                std::make_shared<QPromise<QList<qevercloud::SyncChunk>>>();

            auto future = promise->future();
            promise->start();

            auto syncChunksDownloaderFuture = syncChunksDownloader(
                afterUsn, std::move(ctx), std::move(canceler),
                std::move(callbackWeak));

            auto thenFuture = threading::then(
                std::move(syncChunksDownloaderFuture),
                [promise, syncChunksStorer = std::move(syncChunksStorer)](
                    ISyncChunksDownloader::SyncChunksResult result) mutable {
                    if (!result.m_exception) {
                        promise->addResult(std::move(result.m_syncChunks));
                        promise->finish();
                        return;
                    }

                    if (!result.m_syncChunks.isEmpty()) {
                        syncChunksStorer(std::move(result.m_syncChunks));
                    }

                    promise->setException(*result.m_exception);
                    promise->finish();
                });

            threading::onFailed(
                std::move(thenFuture), [promise](const QException & e) {
                    promise->setException(e);
                    promise->finish();
                });

            return future;
        };

    const QList<std::pair<qint32, qint32>> storedSyncChunksUsnRange =
        storedSyncChunksUsnRangeFetcher();

    const auto nextSyncChunkLowUsnIt = std::upper_bound(
        storedSyncChunksUsnRange.begin(), storedSyncChunksUsnRange.end(),
        std::pair<qint32, qint32>{afterUsn, 0});
    if ((nextSyncChunkLowUsnIt == storedSyncChunksUsnRange.end()) ||
        (afterUsn != 0 && nextSyncChunkLowUsnIt->first != (afterUsn + 1)))
    {
        // There are no stored sync chunks corresponding to the range
        // we are looking for, will download the sync chunks right away
        return downloadSyncChunks(
            afterUsn, std::move(ctx), std::move(canceler),
            std::move(callbackWeak));
    }

    auto storedSyncChunks = storedSyncChunksFetcher(afterUsn);

    // The set of cached sync chunks might be incomplete, even despite the fact
    // that we checked the usns range previously - the storage could fail to
    // read or deserialize some of the stored sync chunks from files, need to
    // check for that
    std::optional<qint32> chunksLowUsn;
    std::optional<qint32> chunksHighUsn;
    for (const auto & syncChunk: qAsConst(storedSyncChunks)) {
        const auto highUsn = syncChunk.chunkHighUSN();
        if (Q_UNLIKELY(!highUsn)) {
            QNWARNING(
                "synchronization::SyncChunksProvider",
                "Detected stored sync chunk without high USN: " << syncChunk);

            // Something is wrong with the stored sync chunks, falling back to
            // downloading the sync chunks right away
            return downloadSyncChunks(
                afterUsn, std::move(ctx), std::move(canceler),
                std::move(callbackWeak));
        }

        const auto lowUsn = utils::syncChunkLowUsn(syncChunk);
        if (Q_UNLIKELY(!lowUsn)) {
            QNWARNING(
                "synchronization::SyncChunksProvider",
                "Failed to find low USN for stored sync chunk: " << syncChunk);

            // Something is wrong with the stored sync chunks, falling back to
            // downloading the sync chunks right away
            return downloadSyncChunks(
                afterUsn, std::move(ctx), std::move(canceler),
                std::move(callbackWeak));
        }

        if (!chunksLowUsn || (*chunksLowUsn > *lowUsn)) {
            chunksLowUsn = *lowUsn;
        }

        if (!chunksHighUsn || (*chunksHighUsn < *highUsn)) {
            chunksHighUsn = *highUsn;
        }
    }

    if (!chunksLowUsn || !chunksHighUsn ||
        (afterUsn != 0 && *chunksLowUsn != afterUsn + 1))
    {
        if (Q_UNLIKELY(!chunksLowUsn || !chunksHighUsn)) {
            QNWARNING(
                "synchronization::SyncChunksProvider",
                "Failed to determine overall low or high USN for a set "
                "of stored sync chunks");
        }

        return downloadSyncChunks(
            afterUsn, std::move(ctx), std::move(canceler),
            std::move(callbackWeak));
    }

    // At this point we can be sure that stored sync chunks indeed start
    // from afterUsn + 1. But instead of just returning them, will still
    // request sync chunks after chunksHighUsn from the downloader, then unite
    // its result with the cached sync chunks and return the overall result.
    auto promise = std::make_shared<QPromise<QList<qevercloud::SyncChunk>>>();
    auto future = promise->future();

    auto downloaderFuture = downloadSyncChunks(
        *chunksHighUsn, std::move(ctx), std::move(canceler),
        std::move(callbackWeak));

    promise->start();

    auto thenFuture = threading::then(
        std::move(downloaderFuture),
        [promise, storedSyncChunks = std::move(storedSyncChunks)](
            QList<qevercloud::SyncChunk>
                downloadedSyncChunks) mutable { // NOLINT
            storedSyncChunks << downloadedSyncChunks;
            promise->addResult(storedSyncChunks);
            promise->finish();
        });

    threading::onFailed(std::move(thenFuture), [promise](const QException & e) {
        promise->setException(e);
        promise->finish();
    });

    return future;
}

} // namespace

SyncChunksProvider::SyncChunksProvider(
    ISyncChunksDownloaderPtr syncChunksDownloader,
    ISyncChunksStoragePtr syncChunksStorage) :
    m_syncChunksDownloader{std::move(syncChunksDownloader)},
    m_syncChunksStorage{std::move(syncChunksStorage)}
{
    if (Q_UNLIKELY(!m_syncChunksDownloader)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::SyncChunksProvider",
            "SyncChunksProvider ctor: sync chunks downloader is null")}};
    }

    if (Q_UNLIKELY(!m_syncChunksStorage)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::SyncChunksProvider",
            "SyncChunksProvider ctor: sync chunks storage is null")}};
    }
}

QFuture<QList<qevercloud::SyncChunk>> SyncChunksProvider::fetchSyncChunks(
    qint32 afterUsn, qevercloud::IRequestContextPtr ctx,
    utility::cancelers::ICancelerPtr canceler, ICallbackWeakPtr callbackWeak)
{
    return fetchSyncChunksImpl(
        afterUsn, std::move(ctx), std::move(canceler), std::move(callbackWeak),
        [this] {
            return m_syncChunksStorage->fetchUserOwnSyncChunksLowAndHighUsns();
        },
        [this, actualAfterUsn = afterUsn](
            qint32 afterUsn, qevercloud::IRequestContextPtr ctx,
            utility::cancelers::ICancelerPtr canceler,
            ICallbackWeakPtr callbackWeak) {
            // Artificially prolonging the lifetime of
            // SyncChunksDownloaderCallback until the result of
            // m_syncChunksDownloader->downloadSyncChunks is ready
            auto callback = std::make_shared<SyncChunksDownloaderCallback>(
                std::move(callbackWeak), actualAfterUsn);
            auto downloadFuture = m_syncChunksDownloader->downloadSyncChunks(
                afterUsn, std::move(ctx), std::move(canceler), callback);
            auto resultFuture = threading::then(
                std::move(downloadFuture),
                [callback = std::move(callback)](
                    ISyncChunksDownloader::SyncChunksResult result) {
                    return result;
                });
            return resultFuture;
        },
        [this](qint32 afterUsn) {
            return m_syncChunksStorage->fetchRelevantUserOwnSyncChunks(
                afterUsn);
        },
        [this](const QList<qevercloud::SyncChunk> & syncChunks) {
            m_syncChunksStorage->putUserOwnSyncChunks(syncChunks);
        });
}

QFuture<QList<qevercloud::SyncChunk>>
    SyncChunksProvider::fetchLinkedNotebookSyncChunks(
        qevercloud::LinkedNotebook linkedNotebook, qint32 afterUsn,
        qevercloud::IRequestContextPtr ctx,
        utility::cancelers::ICancelerPtr canceler,
        ICallbackWeakPtr callbackWeak)
{
    const auto linkedNotebookGuid = linkedNotebook.guid(); // NOLINT
    if (!linkedNotebookGuid) {
        return threading::makeExceptionalFuture<QList<qevercloud::SyncChunk>>(
            InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
                "synchronization::SyncChunksProvider",
                "Can't fetch linked notebook sync chunks: linked notebook "
                "guid is empty")}});
    }

    return fetchSyncChunksImpl(
        afterUsn, std::move(ctx), std::move(canceler), std::move(callbackWeak),
        [this, linkedNotebookGuid = *linkedNotebookGuid] {
            return m_syncChunksStorage
                ->fetchLinkedNotebookSyncChunksLowAndHighUsns(
                    linkedNotebookGuid);
        },
        [this, linkedNotebook = std::move(linkedNotebook),
         actualAfterUsn = afterUsn](
            qint32 afterUsn, qevercloud::IRequestContextPtr ctx,
            utility::cancelers::ICancelerPtr canceler,
            ICallbackWeakPtr callbackWeak) mutable {
            // Artificially prolonging the lifetime of
            // SyncChunksDownloaderCallback until the result of
            // m_syncChunksDownloader->downloadLinkedNotebookSyncChunks is ready
            auto callback = std::make_shared<SyncChunksDownloaderCallback>(
                std::move(callbackWeak), actualAfterUsn);
            auto downloadFuture =
                m_syncChunksDownloader->downloadLinkedNotebookSyncChunks(
                    std::move(linkedNotebook), afterUsn, std::move(ctx),
                    std::move(canceler), callback);
            auto resultFuture = threading::then(
                std::move(downloadFuture),
                [callback = std::move(callback)](
                    ISyncChunksDownloader::SyncChunksResult result) {
                    return result;
                });
            return resultFuture;
        },
        [this, linkedNotebookGuid = *linkedNotebookGuid](qint32 afterUsn) {
            return m_syncChunksStorage->fetchRelevantLinkedNotebookSyncChunks(
                linkedNotebookGuid, afterUsn);
        },
        [this, linkedNotebookGuid = *linkedNotebookGuid](
            const QList<qevercloud::SyncChunk> & syncChunks) {
            m_syncChunksStorage->putLinkedNotebookSyncChunks(
                linkedNotebookGuid, syncChunks);
        });
}

} // namespace quentier::synchronization
