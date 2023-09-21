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

#include "ResourcesProcessor.h"

#include <synchronization/INoteStoreProvider.h>
#include <synchronization/conflict_resolvers/Utils.h>
#include <synchronization/processors/IResourceFullDataDownloader.h>
#include <synchronization/sync_chunks/Utils.h>
#include <synchronization/types/DownloadResourcesStatus.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/RuntimeError.h>
#include <quentier/local_storage/ILocalStorage.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/synchronization/ISyncConflictResolver.h>
#include <quentier/threading/Factory.h>
#include <quentier/threading/Future.h>
#include <quentier/threading/TrackedTask.h>
#include <quentier/utility/UidGenerator.h>
#include <quentier/utility/cancelers/AnyOfCanceler.h>
#include <quentier/utility/cancelers/ManualCanceler.h>

#include <qevercloud/exceptions/EDAMSystemException.h>
#include <qevercloud/types/SyncChunk.h>

#include <QFutureWatcher>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>

#include <algorithm>
#include <type_traits>

namespace quentier::synchronization {

ResourcesProcessor::ResourcesProcessor(
    local_storage::ILocalStoragePtr localStorage,
    IResourceFullDataDownloaderPtr resourceFullDataDownloader,
    INoteStoreProviderPtr noteStoreProvider, qevercloud::IRequestContextPtr ctx,
    qevercloud::IRetryPolicyPtr retryPolicy,
    threading::QThreadPoolPtr threadPool) :
    m_localStorage{std::move(localStorage)},
    m_resourceFullDataDownloader{std::move(resourceFullDataDownloader)},
    m_noteStoreProvider{std::move(noteStoreProvider)}, m_ctx{std::move(ctx)},
    m_retryPolicy{std::move(retryPolicy)},
    m_threadPool{
        threadPool ? std::move(threadPool) : threading::globalThreadPool()}
{
    if (Q_UNLIKELY(!m_localStorage)) {
        throw InvalidArgument{ErrorString{
            QStringLiteral("ResourcesProcessor ctor: local storage is null")}};
    }

    if (Q_UNLIKELY(!m_resourceFullDataDownloader)) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "ResourcesProcessor ctor: resource full data downloader is null")}};
    }

    if (Q_UNLIKELY(!m_noteStoreProvider)) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "ResourcesProcessor ctor: note store provider is null")}};
    }

    Q_ASSERT(m_threadPool);
}

QFuture<DownloadResourcesStatusPtr> ResourcesProcessor::processResources(
    const QList<qevercloud::SyncChunk> & syncChunks,
    utility::cancelers::ICancelerPtr canceler, ICallbackWeakPtr callbackWeak)
{
    QNDEBUG(
        "synchronization::ResourcesProcessor",
        "ResourcesProcessor::processResources");

    Q_ASSERT(canceler);

    QList<qevercloud::Resource> resources;
    for (const auto & syncChunk: qAsConst(syncChunks)) {
        resources << utils::collectResourcesFromSyncChunk(syncChunk);
    }

    if (resources.isEmpty()) {
        QNDEBUG(
            "synchronization::ResourcesProcessor", "No new/updated resources");

        return threading::makeReadyFuture<DownloadResourcesStatusPtr>(
            std::make_shared<DownloadResourcesStatus>());
    }

    const int resourceCount = resources.size();

    const auto selfWeak = weak_from_this();

    QList<QFuture<ProcessResourceStatus>> resourceFutures;
    resourceFutures.reserve(resourceCount);

    using FetchResourceOptions =
        local_storage::ILocalStorage::FetchResourceOptions;

    auto context = std::make_shared<Context>();

    context->status = std::make_shared<DownloadResourcesStatus>();
    context->statusMutex = std::make_shared<QMutex>();

    // Processing of all resources might need to be globally canceled if certain
    // kind of exceptional situation occurs, for example:
    // 1. Evernote API rate limit gets exceeded - once this happens, all further
    //    immediate attempts to download full resource data would fail with the
    //    same exception so it doesn't make sense to continue processing
    // 2. Authentication token expires during the attempt to download full
    //    resource data - it's pretty unlikely as the first step of sync should
    //    ensure the auth token isn't close to expiration and re-acquire the
    //    token if it is close to expiration. But still need to be able to
    //    handle this situation.
    context->manualCanceler =
        std::make_shared<utility::cancelers::ManualCanceler>();

    auto promise = std::make_shared<QPromise<DownloadResourcesStatusPtr>>();
    auto future = promise->future();

    context->canceler = std::make_shared<utility::cancelers::AnyOfCanceler>(
        QList<utility::cancelers::ICancelerPtr>{
            context->manualCanceler, std::move(canceler)});

    context->callbackWeak = std::move(callbackWeak);

    for (const auto & resource: qAsConst(resources)) {
        auto resourcePromise =
            std::make_shared<QPromise<ProcessResourceStatus>>();

        resourceFutures << resourcePromise->future();
        resourcePromise->start();

        Q_ASSERT(resource.guid());
        Q_ASSERT(resource.updateSequenceNum());

        auto findResourceByGuidFuture = m_localStorage->findResourceByGuid(
            *resource.guid(), FetchResourceOptions{});

        auto thenFuture = threading::then(
            std::move(findResourceByGuidFuture),
            threading::TrackedTask{
                selfWeak,
                [this, updatedResource = resource,
                 resourcePromise = resourcePromise, selfWeak,
                 context = context](const std::optional<qevercloud::Resource> &
                                        resource) mutable {
                    if (context->canceler->isCanceled()) {
                        const auto & guid = *updatedResource.guid();

                        if (const auto callback = context->callbackWeak.lock())
                        {
                            callback->onResourceProcessingCancelled(
                                updatedResource);
                        }

                        {
                            const QMutexLocker locker{
                                context->statusMutex.get()};
                            context->status
                                ->m_cancelledResourceGuidsAndUsns[guid] =
                                updatedResource.updateSequenceNum().value();
                        }

                        resourcePromise->addResult(
                            ProcessResourceStatus::Canceled);

                        resourcePromise->finish();
                        return;
                    }

                    if (resource) {
                        {
                            const QMutexLocker locker{
                                context->statusMutex.get()};
                            ++context->status->m_totalUpdatedResources;
                        }

                        onFoundDuplicate(
                            std::move(context), std::move(resourcePromise),
                            std::move(updatedResource), *resource);
                        return;
                    }

                    {
                        const QMutexLocker locker{context->statusMutex.get()};
                        ++context->status->m_totalNewResources;
                    }

                    // No duplicate by guid was found, will download full
                    // resource data and then put it into the local storage
                    downloadFullResourceData(
                        std::move(context), std::move(resourcePromise),
                        std::move(updatedResource), ResourceKind::NewResource);
                }});

        threading::onFailed(
            std::move(thenFuture),
            [resourcePromise, context, resource](const QException & e) {
                if (const auto callback = context->callbackWeak.lock()) {
                    callback->onResourceFailedToProcess(resource, e);
                }

                {
                    const QMutexLocker locker{context->statusMutex.get()};
                    context->status->m_resourcesWhichFailedToProcess
                        << DownloadResourcesStatus::ResourceWithException{
                               resource,
                               std::shared_ptr<QException>(e.clone())};
                }

                resourcePromise->addResult(
                    ProcessResourceStatus::FailedToPutResourceToLocalStorage);

                resourcePromise->finish();
            });
    }

    Q_ASSERT(resourceCount == resourceFutures.size());

    auto allResourcesFuture =
        threading::whenAll<ProcessResourceStatus>(std::move(resourceFutures));

    promise->setProgressRange(0, 100);
    promise->setProgressValue(0);
    threading::mapFutureProgress(allResourcesFuture, promise);

    promise->start();

    threading::thenOrFailed(
        std::move(allResourcesFuture), promise,
        [promise, context,
         resourceCount](const QList<ProcessResourceStatus> & statuses) {
            Q_ASSERT(statuses.size() == resourceCount);
            Q_UNUSED(statuses)

            promise->addResult(context->status);
            promise->finish();
        });

    return future;
}

void ResourcesProcessor::onFoundDuplicate(
    ContextPtr context,
    std::shared_ptr<QPromise<ProcessResourceStatus>> promise,
    qevercloud::Resource updatedResource, qevercloud::Resource localResource)
{
    Q_ASSERT(context);
    Q_ASSERT(updatedResource.noteGuid());

    bool shouldMakeLocalConflictNewResource = false;

    if (Q_UNLIKELY(!localResource.noteGuid())) {
        // Although it is unlikely, the resource might have been moved to a note
        // which has not yet been synchronized with Evernote and hence has no
        // guid
        QNDEBUG(
            "synchronization::ResourcesProcessor",
            "ResourcesProcessor::onFoundDuplicate: local resource has no "
                << "note guid: " << localResource);
        shouldMakeLocalConflictNewResource = true;
    }
    else if (*localResource.noteGuid() != *updatedResource.noteGuid()) {
        QNDEBUG(
            "synchronization::ResourcesProcessor",
            "ResourcesProcessor::onFoundDuplicate: local resource belongs "
                << "to a different note than updated resource; local resource: "
                << localResource << "\nUpdated resource: " << updatedResource);
        shouldMakeLocalConflictNewResource = true;
    }
    else if (localResource.isLocallyModified()) {
        QNDEBUG(
            "synchronization::ResourcesProcessor",
            "ResourcesProcessor::onFoundDuplicate: local resource with local "
                << "id " << localResource.localId() << " is marked as locally "
                << "modified, will make it a local conflicting resource");
        shouldMakeLocalConflictNewResource = true;
    }

    if (shouldMakeLocalConflictNewResource) {
        handleResourceConflict(
            context, promise, std::move(updatedResource),
            std::move(localResource));
        return;
    }

    downloadFullResourceData(
        std::move(context), std::move(promise), std::move(updatedResource),
        ResourceKind::UpdatedResource);
}

void ResourcesProcessor::onFoundNoteOwningConflictingResource(
    const ContextPtr & context,
    const std::shared_ptr<QPromise<ProcessResourceStatus>> & promise,
    qevercloud::Resource updatedResource,
    const qevercloud::Resource & localResource, qevercloud::Note localNote)
{
    Q_ASSERT(context);
    Q_ASSERT(updatedResource.guid());

    // Local note would be turned into the conflicting local one with local
    // duplicates of all resources.
    if (localNote.resources()) {
        const auto it = std::find_if(
            localNote.mutableResources()->begin(),
            localNote.mutableResources()->end(),
            [localResourceLocalId = localResource.localId()](
                const qevercloud::Resource & resource) {
                return resource.localId() == localResourceLocalId;
            });
        if (it == localNote.mutableResources()->end()) {
            localNote.setResources(
                QList<qevercloud::Resource>{} << *localNote.resources()
                                              << localResource);
        }
        else {
            *it = localResource;
        }
    }
    else {
        localNote.setResources(QList<qevercloud::Resource>{} << localResource);
    }

    Q_ASSERT(localNote.guid());
    const auto noteGuid = *localNote.guid();

    localNote.setLocalId(UidGenerator::Generate());
    localNote.setGuid(std::nullopt);
    localNote.setUpdateSequenceNum(std::nullopt);
    localNote.setLocallyModified(true);

    if (localNote.resources()) {
        for (auto & resource: *localNote.mutableResources()) {
            resource.setLocalId(UidGenerator::Generate());
            resource.setGuid(std::nullopt);
            resource.setUpdateSequenceNum(std::nullopt);
            resource.setNoteGuid(std::nullopt);
            resource.setLocalId(localNote.localId());
            resource.setLocallyModified(true);
        }
    }

    if (!localNote.attributes()) {
        localNote.setAttributes(qevercloud::NoteAttributes{});
    }
    localNote.mutableAttributes()->setConflictSourceNoteGuid(noteGuid);

    localNote.setTitle(utils::makeLocalConflictingNoteTitle(localNote));

    auto putLocalNoteFuture = m_localStorage->putNote(std::move(localNote));

    const auto selfWeak = weak_from_this();

    auto thenFuture = threading::then(
        std::move(putLocalNoteFuture), m_threadPool.get(),
        threading::TrackedTask{
            selfWeak,
            [this, selfWeak, context = context, promise = promise,
             updatedResource = updatedResource]() mutable {
                if (context->canceler->isCanceled()) {
                    const auto & guid = *updatedResource.guid();
                    if (const auto callback = context->callbackWeak.lock()) {
                        callback->onResourceProcessingCancelled(
                            updatedResource);
                    }

                    {
                        const QMutexLocker locker{context->statusMutex.get()};
                        context->status->m_cancelledResourceGuidsAndUsns[guid] =
                            updatedResource.updateSequenceNum().value();
                    }

                    promise->addResult(ProcessResourceStatus::Canceled);
                    promise->finish();
                    return;
                }

                downloadFullResourceData(
                    std::move(context), std::move(promise),
                    std::move(updatedResource), ResourceKind::UpdatedResource);
            }});

    threading::onFailed(
        std::move(thenFuture),
        [context, promise, updatedResource = std::move(updatedResource)](
            const QException & e) mutable {
            if (const auto callback = context->callbackWeak.lock()) {
                callback->onResourceFailedToProcess(updatedResource, e);
            }

            {
                const QMutexLocker locker{context->statusMutex.get()};
                context->status->m_resourcesWhichFailedToProcess
                    << DownloadResourcesStatus::ResourceWithException{
                           std::move(updatedResource),
                           std::shared_ptr<QException>(e.clone())};
            }

            promise->addResult(
                ProcessResourceStatus::FailedToPutResourceToLocalStorage);

            promise->finish();
        });
}

void ResourcesProcessor::handleResourceConflict(
    const ContextPtr & context,
    const std::shared_ptr<QPromise<ProcessResourceStatus>> & promise,
    qevercloud::Resource updatedResource, qevercloud::Resource localResource)
{
    Q_ASSERT(context);

    localResource.setLocalId(UidGenerator::Generate());
    localResource.setGuid(std::nullopt);
    localResource.setNoteGuid(std::nullopt);
    localResource.setUpdateSequenceNum(std::nullopt);
    localResource.setLocallyModified(true);

    auto findNoteByGuidFuture = m_localStorage->findNoteByGuid(
        *updatedResource.noteGuid(),
        local_storage::ILocalStorage::FetchNoteOptions{} |
            local_storage::ILocalStorage::FetchNoteOption::
                WithResourceMetadata);

    const auto selfWeak = weak_from_this();

    auto thenFuture = threading::then(
        std::move(findNoteByGuidFuture),
        threading::TrackedTask{
            selfWeak,
            [this, context, promise, localResource = std::move(localResource),
             updatedResource](std::optional<qevercloud::Note> note) mutable {
                if (Q_UNLIKELY(!note)) {
                    ErrorString error{QStringLiteral(
                        "Failed to resolve resources conflict: note "
                        "owning the conflicting resource was not found by "
                        "guid")};
                    error.details() = *updatedResource.noteGuid();

                    auto resourceWithException =
                        DownloadResourcesStatus::ResourceWithException{
                            std::move(updatedResource),
                            std::make_shared<RuntimeError>(std::move(error))};

                    if (const auto callback = context->callbackWeak.lock()) {
                        callback->onResourceFailedToProcess(
                            resourceWithException.first,
                            *resourceWithException.second);
                    }

                    {
                        const QMutexLocker locker{context->statusMutex.get()};
                        context->status->m_resourcesWhichFailedToProcess
                            << resourceWithException;
                    }

                    promise->addResult(
                        ProcessResourceStatus::FailedToResolveResourceConflict);

                    promise->finish();
                    return;
                }

                onFoundNoteOwningConflictingResource(
                    context, promise, std::move(updatedResource), localResource,
                    std::move(*note));
            }});

    threading::onFailed(
        std::move(thenFuture),
        [context, promise, updatedResource = std::move(updatedResource)](
            const QException & e) mutable {
            if (const auto callback = context->callbackWeak.lock()) {
                callback->onResourceFailedToProcess(updatedResource, e);
            }

            {
                const QMutexLocker locker{context->statusMutex.get()};
                context->status->m_resourcesWhichFailedToProcess
                    << DownloadResourcesStatus::ResourceWithException{
                           std::move(updatedResource),
                           std::shared_ptr<QException>(e.clone())};
            }

            promise->addResult(
                ProcessResourceStatus::FailedToResolveResourceConflict);

            promise->finish();
        });
}

void ResourcesProcessor::downloadFullResourceData(
    ContextPtr context,
    std::shared_ptr<QPromise<ProcessResourceStatus>> promise,
    qevercloud::Resource resource, ResourceKind resourceKind)
{
    Q_ASSERT(context);
    Q_ASSERT(resource.guid());

    auto noteStoreFuture = m_noteStoreProvider->noteStoreForNoteLocalId(
        resource.noteLocalId(), m_ctx, m_retryPolicy);

    const auto selfWeak = weak_from_this();

    auto noteStoreThenFuture = threading::then(
        std::move(noteStoreFuture),
        threading::TrackedTask{
            selfWeak,
            [this, context = context, promise = promise, resource = resource,
             resourceKind](
                const qevercloud::INoteStorePtr & noteStore) mutable {
                Q_ASSERT(noteStore);

                downloadFullResourceData(
                    std::move(context), std::move(promise), std::move(resource),
                    resourceKind, noteStore);
            }});

    threading::onFailed(
        std::move(noteStoreThenFuture),
        [context = std::move(context), promise = std::move(promise),
         resource = std::move(resource)](const QException & e) {
            if (const auto callback = context->callbackWeak.lock()) {
                callback->onResourceFailedToDownload(resource, e);
            }

            promise->addResult(
                ProcessResourceStatus::FailedToDownloadFullResourceData);

            promise->finish();
            return;
        });
}

void ResourcesProcessor::downloadFullResourceData(
    ContextPtr context,
    std::shared_ptr<QPromise<ProcessResourceStatus>> promise,
    qevercloud::Resource resource, ResourceKind resourceKind,
    const qevercloud::INoteStorePtr & noteStore)
{
    Q_ASSERT(context);
    Q_ASSERT(resource.guid());
    Q_ASSERT(noteStore);

    auto downloadFullResourceDataFuture =
        m_resourceFullDataDownloader->downloadFullResourceData(
            *resource.guid(), noteStore);

    const auto selfWeak = weak_from_this();

    auto thenFuture = threading::then(
        std::move(downloadFullResourceDataFuture),
        threading::TrackedTask{
            selfWeak,
            [this, context, promise,
             resourceKind](qevercloud::Resource resource) mutable {
                putResourceToLocalStorage(
                    context, promise, std::move(resource), resourceKind);
            }});

    threading::onFailed(
        std::move(thenFuture),
        [context = std::move(context), promise = std::move(promise),
         resource = std::move(resource)](const QException & e) mutable {
            if (const auto callback = context->callbackWeak.lock()) {
                callback->onResourceFailedToDownload(resource, e);
            }

            {
                const QMutexLocker locker{context->statusMutex.get()};
                context->status->m_resourcesWhichFailedToDownload
                    << DownloadResourcesStatus::ResourceWithException{
                           resource, std::shared_ptr<QException>(e.clone())};
            }

            bool shouldCancelProcessing = false;
            try {
                e.raise();
            }
            catch (const qevercloud::EDAMSystemException & se) {
                if (se.errorCode() ==
                    qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED) {
                    context->status->m_stopSynchronizationError =
                        StopSynchronizationError{
                            RateLimitReachedError{se.rateLimitDuration()}};
                    shouldCancelProcessing = true;
                }
                else if (
                    se.errorCode() == qevercloud::EDAMErrorCode::AUTH_EXPIRED) {
                    context->status->m_stopSynchronizationError =
                        StopSynchronizationError{AuthenticationExpiredError{}};
                    shouldCancelProcessing = true;
                }
            }
            catch (...) {
            }

            if (shouldCancelProcessing) {
                context->manualCanceler->cancel();
            }

            promise->addResult(
                ProcessResourceStatus::FailedToDownloadFullResourceData);

            promise->finish();
        });
}

void ResourcesProcessor::putResourceToLocalStorage(
    const ContextPtr & context,
    const std::shared_ptr<QPromise<ProcessResourceStatus>> & promise,
    qevercloud::Resource resource, ResourceKind putResourceKind)
{
    Q_ASSERT(context);
    Q_ASSERT(resource.guid());
    Q_ASSERT(resource.updateSequenceNum());

    auto putResourceFuture = m_localStorage->putResource(resource);

    auto thenFuture = threading::then(
        std::move(putResourceFuture), m_threadPool.get(),
        [promise, context, putResourceKind, resourceGuid = *resource.guid(),
         resourceUsn = *resource.updateSequenceNum()] {
            if (const auto callback = context->callbackWeak.lock()) {
                callback->onProcessedResource(resourceGuid, resourceUsn);
            }

            {
                const QMutexLocker locker{context->statusMutex.get()};
                context->status->m_processedResourceGuidsAndUsns[resourceGuid] =
                    resourceUsn;
            }

            promise->addResult(
                putResourceKind == ResourceKind::NewResource
                    ? ProcessResourceStatus::AddedResource
                    : ProcessResourceStatus::UpdatedResource);

            promise->finish();
        });

    threading::onFailed(
        std::move(thenFuture),
        [context, promise,
         resource = std::move(resource)](const QException & e) mutable {
            if (const auto callback = context->callbackWeak.lock()) {
                callback->onResourceFailedToProcess(resource, e);
            }

            {
                const QMutexLocker locker{context->statusMutex.get()};
                context->status->m_resourcesWhichFailedToProcess
                    << DownloadResourcesStatus::ResourceWithException{
                           std::move(resource),
                           std::shared_ptr<QException>(e.clone())};
            }

            promise->addResult(
                ProcessResourceStatus::FailedToPutResourceToLocalStorage);

            promise->finish();
        });
}

} // namespace quentier::synchronization
