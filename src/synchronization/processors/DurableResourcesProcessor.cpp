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

#include "DurableResourcesProcessor.h"

#include <synchronization/processors/IResourcesProcessor.h>
#include <synchronization/processors/Utils.h>
#include <synchronization/sync_chunks/Utils.h>
#include <synchronization/types/DownloadResourcesStatus.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/threading/Future.h>
#include <quentier/threading/QtFutureContinuations.h>
#include <quentier/threading/TrackedTask.h>

#include <qevercloud/types/builders/SyncChunkBuilder.h>

#include <QThread>

#include <algorithm>

namespace quentier::synchronization {

namespace {

class Callback final : public IResourcesProcessor::ICallback
{
public:
    explicit Callback(
        IDurableResourcesProcessor::ICallbackWeakPtr callbackWeak,
        std::weak_ptr<IDurableResourcesProcessor> durableProcessorWeak,
        const QDir & syncResourcesDir) :
        m_callbackWeak{std::move(callbackWeak)},
        m_durableProcessorWeak{std::move(durableProcessorWeak)},
        m_syncResourcesDir{syncResourcesDir}
    {}

    // IResourcesProcessor::ICallback
    void onProcessedResource(
        const qevercloud::Guid & resourceGuid,
        qint32 resourceUpdateSequenceNum) noexcept override
    {
        const auto durableProcessor = m_durableProcessorWeak.lock();
        if (!durableProcessor) {
            return;
        }

        try {
            utils::writeProcessedResourceInfo(
                resourceGuid, resourceUpdateSequenceNum, m_syncResourcesDir);
        }
        catch (const std::exception & e) {
            QNWARNING(
                "synchronization::DurableResourcesProcessor",
                "Failed to write processed resource info: "
                    << e.what() << ", resource guid = " << resourceGuid
                    << ", resource usn = " << resourceUpdateSequenceNum);
        }
        catch (...) {
            QNWARNING(
                "synchronization::DurableResourcesProcessor",
                "Failed to write processed resource info: unknown exception, "
                    << "resource guid = " << resourceGuid
                    << ", resource usn = " << resourceUpdateSequenceNum);
        }

        if (const auto callback = m_callbackWeak.lock()) {
            callback->onProcessedResource(
                resourceGuid, resourceUpdateSequenceNum);
        }
    }

    void onResourceFailedToDownload(
        const qevercloud::Resource & resource,
        const QException & e) noexcept override
    {
        const auto durableProcessor = m_durableProcessorWeak.lock();
        if (!durableProcessor) {
            return;
        }

        try {
            utils::writeFailedToDownloadResource(resource, m_syncResourcesDir);
        }
        catch (const std::exception & e) {
            QNWARNING(
                "synchronization::IDurableResourcesProcessor",
                "Failed to write failed to download resource: "
                    << e.what() << ", resource: " << resource);
        }
        catch (...) {
            QNWARNING(
                "synchronization::DurableResourcesProcessor",
                "Failed to write failed to download resource: unknown "
                "exception, "
                    << "resource: " << resource);
        }

        if (const auto callback = m_callbackWeak.lock()) {
            callback->onResourceFailedToDownload(resource, e);
        }
    }

    void onResourceFailedToProcess(
        const qevercloud::Resource & resource,
        const QException & e) noexcept override
    {
        const auto durableProcessor = m_durableProcessorWeak.lock();
        if (!durableProcessor) {
            return;
        }

        try {
            utils::writeFailedToProcessResource(resource, m_syncResourcesDir);
        }
        catch (const std::exception & e) {
            QNWARNING(
                "synchronization::IDurableResourcesProcessor",
                "Failed to write failed to process resource: "
                    << e.what() << ", resource: " << resource);
        }
        catch (...) {
            QNWARNING(
                "synchronization::DurableResourcesProcessor",
                "Failed to write failed to process resource: unknown "
                "exception, "
                    << "resource: " << resource);
        }

        if (const auto callback = m_callbackWeak.lock()) {
            callback->onResourceFailedToProcess(resource, e);
        }
    }

    void onResourceProcessingCancelled(
        const qevercloud::Resource & resource) noexcept override
    {
        const auto durableProcessor = m_durableProcessorWeak.lock();
        if (!durableProcessor) {
            return;
        }

        try {
            utils::writeCancelledResource(resource, m_syncResourcesDir);
        }
        catch (const std::exception & e) {
            QNWARNING(
                "synchronization::DurableResourcesProcessor",
                "Failed to write cancelled resource: "
                    << e.what() << ", resource: " << resource);
        }
        catch (...) {
            QNWARNING(
                "synchronization::DurableResourcesProcessor",
                "Failed to write cancelled resource: unknown exception, "
                "resource: "
                    << resource);
        }

        if (const auto callback = m_callbackWeak.lock()) {
            callback->onResourceProcessingCancelled(resource);
        }
    }

private:
    const IDurableResourcesProcessor::ICallbackWeakPtr m_callbackWeak;
    const std::weak_ptr<IDurableResourcesProcessor> m_durableProcessorWeak;
    const QDir m_syncResourcesDir;
};

} // namespace

DurableResourcesProcessor::DurableResourcesProcessor(
    IResourcesProcessorPtr resourcesProcessor,
    const QDir & syncPersistentStorageDir) :
    m_resourcesProcessor{std::move(resourcesProcessor)},
    m_syncResourcesDir{[&] {
        QDir lastSyncDataDir{syncPersistentStorageDir.absoluteFilePath(
            QStringLiteral("lastSyncData"))};
        return QDir{
            lastSyncDataDir.absoluteFilePath(QStringLiteral("resources"))};
    }()}
{
    if (Q_UNLIKELY(!m_resourcesProcessor)) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "DurableResourcesProcessor ctor: resources processor is null")}};
    }
}

QFuture<DownloadResourcesStatusPtr> DurableResourcesProcessor::processResources(
    const QList<qevercloud::SyncChunk> & syncChunks,
    utility::cancelers::ICancelerPtr canceler, ICallbackWeakPtr callbackWeak)
{
    Q_ASSERT(canceler);

    // First need to check whether there are resources which failed to be
    // processed or which processing was cancelled. If such resources exist,
    // they need to be processed first.
    auto previousResources = resourcesFromPreviousSync();

    // Also need to check whether there are resources which were fully processed
    // during the last sync within the sync chunks. If so, such resources should
    // not be processed again
    const auto alreadyProcessedResourcesInfo =
        utils::processedResourcesInfoFromLastSync(m_syncResourcesDir);

    if (alreadyProcessedResourcesInfo.isEmpty()) {
        return processResourcesImpl(
            syncChunks, std::move(canceler), std::move(previousResources),
            std::move(callbackWeak));
    }

    auto filteredSyncChunks = syncChunks;
    for (auto & syncChunk: filteredSyncChunks) {
        if (!syncChunk.resources()) {
            continue;
        }

        auto & resources = *syncChunk.mutableResources();
        for (auto it = resources.begin(); it != resources.end();) {
            if (Q_UNLIKELY(!it->guid())) {
                QNWARNING(
                    "synchronization::DurableResourcesProcessor",
                    "Detected resource within sync chunks without guid: "
                        << *it);
                it = resources.erase(it);
                continue;
            }

            const auto processedResourceIt =
                alreadyProcessedResourcesInfo.find(*it->guid());

            if (processedResourceIt != alreadyProcessedResourcesInfo.end()) {
                it = resources.erase(it);
                continue;
            }

            ++it;
        }
    }

    return processResourcesImpl(
        filteredSyncChunks, std::move(canceler), std::move(previousResources),
        std::move(callbackWeak));
}

QList<qevercloud::Resource>
    DurableResourcesProcessor::resourcesFromPreviousSync() const
{
    if (!m_syncResourcesDir.exists()) {
        return {};
    }

    QList<qevercloud::Resource> result;

    result << utils::resourcesWhichFailedToDownloadDuringLastSync(
        m_syncResourcesDir);

    result << utils::resourcesWhichFailedToProcessDuringLastSync(
        m_syncResourcesDir);

    result << utils::resourcesCancelledDuringLastSync(m_syncResourcesDir);
    return result;
}

QFuture<DownloadResourcesStatusPtr>
    DurableResourcesProcessor::processResourcesImpl(
        const QList<qevercloud::SyncChunk> & syncChunks,
        utility::cancelers::ICancelerPtr canceler,
        QList<qevercloud::Resource> previousResources,
        ICallbackWeakPtr callbackWeak)
{
    const auto selfWeak = weak_from_this();
    auto * currentThread = QThread::currentThread();

    auto promise = std::make_shared<QPromise<DownloadResourcesStatusPtr>>();
    auto future = promise->future();
    promise->start();

    if (previousResources.isEmpty()) {
        auto callback = std::make_shared<Callback>(
            std::move(callbackWeak), weak_from_this(), m_syncResourcesDir);

        auto processSyncChunksFuture = m_resourcesProcessor->processResources(
            syncChunks, std::move(canceler), callback);

        threading::thenOrFailed(
            std::move(processSyncChunksFuture), currentThread, promise,
            [promise, callback = std::move(callback)](
                DownloadResourcesStatusPtr status) {
                promise->addResult(std::move(status));
                promise->finish();
            });

        return future;
    }

    const auto pseudoSyncChunks = QList<qevercloud::SyncChunk>{}
        << qevercloud::SyncChunkBuilder{}
               .setResources(std::move(previousResources))
               .build();

    auto callback =
        std::make_shared<Callback>(callbackWeak, selfWeak, m_syncResourcesDir);

    auto resourcesFuture = m_resourcesProcessor->processResources(
        pseudoSyncChunks, canceler, callback);

    threading::thenOrFailed(
        std::move(resourcesFuture), currentThread, promise,
        threading::TrackedTask{
            selfWeak,
            [this, selfWeak, promise, canceler = std::move(canceler),
             syncChunks, callbackWeak = std::move(callbackWeak),
             currentThread](
                DownloadResourcesStatusPtr status) mutable {
                auto processResourcesFuture = processResourcesImpl(
                    syncChunks, std::move(canceler), {},
                    std::move(callbackWeak));

                threading::thenOrFailed(
                    std::move(processResourcesFuture), currentThread, promise,
                    threading::TrackedTask{
                        selfWeak,
                        [selfWeak, promise,
                         processResourcesStatus = std::move(status)](
                            DownloadResourcesStatusPtr status) mutable {
                            *status = utils::mergeDownloadResourcesStatuses(
                                std::move(*status), *processResourcesStatus);

                            promise->addResult(std::move(status));
                            promise->finish();
                        }});
            }});

    return future;
}

} // namespace quentier::synchronization
