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
#include <quentier/threading/QtFutureContinuations.h>
#include <quentier/threading/Future.h>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include <quentier/threading/Qt5Promise.h>
#endif

#include <algorithm>
#include <functional>

namespace quentier::synchronization {

namespace {

using StoredSyncChunksUsnRangeFetcher =
    std::function<QList<std::pair<qint32, qint32>>()>;

using SyncChunksDownloader =
    std::function<QFuture<ISyncChunksDownloader::SyncChunksResult>(
        qint32, qevercloud::IRequestContextPtr)>;

using StoredSyncChunksFetcher =
    std::function<QList<qevercloud::SyncChunk>(qint32)>;

using SyncChunksStorer =
    std::function<void(QList<qevercloud::SyncChunk>)>;

[[nodiscard]] QFuture<QList<qevercloud::SyncChunk>> fetchSyncChunksImpl(
    qint32 afterUsn, qevercloud::IRequestContextPtr ctx,
    const StoredSyncChunksUsnRangeFetcher & storedSyncChunksUsnRangeFetcher,
    SyncChunksDownloader syncChunksDownloader,
    const StoredSyncChunksFetcher & storedSyncChunksFetcher,
    SyncChunksStorer syncChunksStorer)
{
    Q_ASSERT(storedSyncChunksUsnRangeFetcher);
    Q_ASSERT(syncChunksDownloader);
    Q_ASSERT(storedSyncChunksFetcher);
    Q_ASSERT(syncChunksStorer);

    auto downloadSyncChunks =
        [syncChunksDownloader = std::move(syncChunksDownloader),
         syncChunksStorer = std::move(syncChunksStorer)](
            qint32 afterUsn, qevercloud::IRequestContextPtr ctx) mutable
        {
            auto promise =
                std::make_shared<QPromise<QList<qevercloud::SyncChunk>>>();

            auto future = promise->future();
            promise->start();

            auto syncChunksDownloaderFuture =
                syncChunksDownloader(afterUsn, std::move(ctx));

            threading::bindCancellation(future, syncChunksDownloaderFuture);

            auto thenFuture = threading::then(
                std::move(syncChunksDownloaderFuture),
                [promise, syncChunksStorer = std::move(syncChunksStorer)](
                    ISyncChunksDownloader::SyncChunksResult result) mutable
                {
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
                std::move(thenFuture),
                [promise](const QException & e)
                {
                    promise->setException(e);
                    promise->finish();
                });

            return future;
        };

    const QList<std::pair<qint32, qint32>> storedSyncChunksUsnRange =
        storedSyncChunksUsnRangeFetcher();

    const auto nextSyncChunkLowUsnIt = std::upper_bound(
        storedSyncChunksUsnRange.begin(),
        storedSyncChunksUsnRange.end(),
        std::pair<qint32, qint32>{afterUsn, 0});
    if ((nextSyncChunkLowUsnIt == storedSyncChunksUsnRange.end()) ||
        (afterUsn != 0 && nextSyncChunkLowUsnIt->first != (afterUsn + 1)))
    {
        // There are no stored sync chunks corresponding to the range
        // we are looking for, will download the sync chunks right away
        return downloadSyncChunks(afterUsn, std::move(ctx));
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
                "Detected stored sync chunk without high USN: "
                    << syncChunk);

            // Something is wrong with the stored sync chunks, falling back to
            // downloading the sync chunks right away
            return downloadSyncChunks(afterUsn, std::move(ctx));
        }

        const auto lowUsn = utils::syncChunkLowUsn(syncChunk);
        if (Q_UNLIKELY(!lowUsn)) {
            QNWARNING(
                "synchronization::SyncChunksProvider",
                "Failed to find low USN for stored sync chunk: "
                    << syncChunk);

            // Something is wrong with the stored sync chunks, falling back to
            // downloading the sync chunks right away
            return downloadSyncChunks(afterUsn, std::move(ctx));
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

        return downloadSyncChunks(afterUsn, std::move(ctx));
    }

    // At this point we can be sure that stored sync chunks indeed start
    // from afterUsn + 1. But instead of just returning them, will still
    // request sync chunks after chunksHighUsn from the downloader, then unite
    // its result with the cached sync chunks and return the overall result.
    auto promise = std::make_shared<QPromise<QList<qevercloud::SyncChunk>>>();
    auto future = promise->future();

    auto downloaderFuture =
        downloadSyncChunks(*chunksHighUsn, std::move(ctx));

    promise->start();

    threading::bindCancellation(future, downloaderFuture);

    auto thenFuture = threading::then(
        std::move(downloaderFuture),
        [promise, storedSyncChunks = std::move(storedSyncChunks)](
            QList<qevercloud::SyncChunk> downloadedSyncChunks) mutable { // NOLINT
            storedSyncChunks << downloadedSyncChunks;
            promise->addResult(storedSyncChunks);
            promise->finish();
        });

    threading::onFailed(
        std::move(thenFuture),
        [promise](const QException & e)
        {
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
        throw InvalidArgument{ErrorString{
            QT_TRANSLATE_NOOP(
                "synchronization::SyncChunksProvider",
                "SyncChunksProvider ctor: sync chunks downloader is null")}};
    }

    if (Q_UNLIKELY(!m_syncChunksStorage)) {
        throw InvalidArgument{ErrorString{
            QT_TRANSLATE_NOOP(
                "synchronization::SyncChunksProvider",
                "SyncChunksProvider ctor: sync chunks storage is null")}};
    }
}

QFuture<QList<qevercloud::SyncChunk>> SyncChunksProvider::fetchSyncChunks(
    qint32 afterUsn, qevercloud::IRequestContextPtr ctx)
{
    return fetchSyncChunksImpl(
        afterUsn, std::move(ctx),
        [this]
        {
            return m_syncChunksStorage->fetchUserOwnSyncChunksLowAndHighUsns();
        },
        [this](qint32 afterUsn, qevercloud::IRequestContextPtr ctx)
        {
            return m_syncChunksDownloader->downloadSyncChunks(
                afterUsn, std::move(ctx));
        },
        [this](qint32 afterUsn)
        {
            return m_syncChunksStorage->fetchRelevantUserOwnSyncChunks(
                afterUsn);
        },
        [this](const QList<qevercloud::SyncChunk> & syncChunks)
        {
            m_syncChunksStorage->putUserOwnSyncChunks(syncChunks);
        });
}

QFuture<QList<qevercloud::SyncChunk>>
    SyncChunksProvider::fetchLinkedNotebookSyncChunks(
        qevercloud::LinkedNotebook linkedNotebook, qint32 afterUsn,
        qevercloud::IRequestContextPtr ctx)
{
    const auto linkedNotebookGuid = linkedNotebook.guid(); // NOLINT
    if (!linkedNotebookGuid) {
        return threading::makeExceptionalFuture<QList<qevercloud::SyncChunk>>(
            InvalidArgument{ErrorString{
                QT_TRANSLATE_NOOP(
                    "synchronization::SyncChunksProvider",
                    "Can't fetch linked notebook sync chunks: linked notebook "
                    "guid is empty")}});
    }

    return fetchSyncChunksImpl(
        afterUsn, std::move(ctx),
        [this, linkedNotebookGuid = *linkedNotebookGuid]
        {
            return m_syncChunksStorage->fetchLinkedNotebookSyncChunksLowAndHighUsns(
                linkedNotebookGuid);
        },
        [this, linkedNotebook = std::move(linkedNotebook)](
            qint32 afterUsn, qevercloud::IRequestContextPtr ctx) mutable
        {
            return m_syncChunksDownloader->downloadLinkedNotebookSyncChunks(
                std::move(linkedNotebook), afterUsn, std::move(ctx));
        },
        [this, linkedNotebookGuid = *linkedNotebookGuid](qint32 afterUsn)
        {
            return m_syncChunksStorage->fetchRelevantLinkedNotebookSyncChunks(
                linkedNotebookGuid, afterUsn);
        },
        [this, linkedNotebookGuid = *linkedNotebookGuid](
            const QList<qevercloud::SyncChunk> & syncChunks)
        {
            m_syncChunksStorage->putLinkedNotebookSyncChunks(
                linkedNotebookGuid, syncChunks);
        });
}

} // namespace quentier::synchronization
