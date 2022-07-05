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

#include "ResourcesProcessor.h"

#include "IResourceFullDataDownloader.h"

#include <synchronization/conflict_resolvers/Utils.h>
#include <synchronization/sync_chunks/Utils.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/RuntimeError.h>
#include <quentier/local_storage/ILocalStorage.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/synchronization/ISyncConflictResolver.h>
#include <quentier/threading/Future.h>
#include <quentier/threading/TrackedTask.h>
#include <quentier/utility/UidGenerator.h>
#include <quentier/utility/cancelers/AnyOfCanceler.h>
#include <quentier/utility/cancelers/FutureCanceler.h>
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
    IResourceFullDataDownloaderPtr resourceFullDataDownloader) :
    m_localStorage{std::move(localStorage)},
    m_resourceFullDataDownloader{std::move(resourceFullDataDownloader)}
{
    if (Q_UNLIKELY(!m_localStorage)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::ResourcesProcessor",
            "ResourcesProcessor ctor: local storage is null")}};
    }

    if (Q_UNLIKELY(!m_resourceFullDataDownloader)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::ResourcesProcessor",
            "ResourcesProcessor ctor: resource full data downloader is null")}};
    }
}

QFuture<DownloadResourcesStatus> ResourcesProcessor::processResources(
    const QList<qevercloud::SyncChunk> & syncChunks,
    ICallbackWeakPtr callbackWeak)
{
    QNDEBUG(
        "synchronization::ResourcesProcessor",
        "ResourcesProcessor::processResources");

    QList<qevercloud::Resource> resources;
    for (const auto & syncChunk: qAsConst(syncChunks)) {
        resources << utils::collectResourcesFromSyncChunk(syncChunk);
    }

    if (resources.isEmpty()) {
        QNDEBUG(
            "synchronization::ResourcesProcessor", "No new/updated resources");

        return threading::makeReadyFuture<DownloadResourcesStatus>({});
    }

    const int resourceCount = resources.size();

    const auto selfWeak = weak_from_this();

    QList<QFuture<ProcessResourceStatus>> resourceFutures;
    resourceFutures.reserve(resourceCount);

    using FetchResourceOptions =
        local_storage::ILocalStorage::FetchResourceOptions;

    auto status = std::make_shared<DownloadResourcesStatus>();

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
    auto manualCanceler = std::make_shared<utility::cancelers::ManualCanceler>();

    auto promise = std::make_shared<QPromise<DownloadResourcesStatus>>();
    auto future = promise->future();

    auto futureCanceler = std::make_shared<
        utility::cancelers::FutureCanceler<DownloadResourcesStatus>>(future);

    auto anyOfCanceler = std::make_shared<utility::cancelers::AnyOfCanceler>(
        QList<utility::cancelers::ICancelerPtr>{
            manualCanceler, std::move(futureCanceler)});

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
                [this, updatedResource = resource, resourcePromise, status,
                 selfWeak, manualCanceler, anyOfCanceler,
                 callbackWeak](const std::optional<qevercloud::Resource> &
                                   resource) mutable {
                    if (anyOfCanceler->isCanceled()) {
                        const auto & guid = *updatedResource.guid();
                        status->cancelledResourceGuidsAndUsns[guid] =
                            updatedResource.updateSequenceNum().value();

                        if (const auto callback = callbackWeak.lock()) {
                            callback->onResourceProcessingCancelled(
                                updatedResource);
                        }

                        resourcePromise->addResult(
                            ProcessResourceStatus::Canceled);

                        resourcePromise->finish();
                        return;
                    }

                    if (resource) {
                        ++status->totalUpdatedResources;
                        onFoundDuplicate(
                            resourcePromise, status, manualCanceler,
                            anyOfCanceler, std::move(callbackWeak),
                            std::move(updatedResource), *resource);
                        return;
                    }

                    ++status->totalNewResources;

                    // No duplicate by guid was found, will download full
                    // resource data and then put it into the local storage
                    downloadFullResourceData(
                        resourcePromise, status, manualCanceler,
                        std::move(callbackWeak), updatedResource,
                        ResourceKind::NewResource);
                }});

        threading::onFailed(
            std::move(thenFuture),
            [resourcePromise, status, resource,
             callbackWeak](const QException & e) {
                if (const auto callback = callbackWeak.lock()) {
                    callback->onResourceFailedToProcess(resource, e);
                }

                status->resourcesWhichFailedToProcess
                    << DownloadResourcesStatus::ResourceWithException{
                           resource, std::shared_ptr<QException>(e.clone())};

                resourcePromise->addResult(
                    ProcessResourceStatus::FailedToPutResourceToLocalStorage);

                resourcePromise->finish();
            });
    }

    auto allResourcesFuture =
        threading::whenAll<ProcessResourceStatus>(std::move(resourceFutures));

    promise->setProgressRange(0, 100);
    promise->setProgressValue(0);
    threading::mapFutureProgress(allResourcesFuture, promise);

    promise->start();

    threading::thenOrFailed(
        std::move(allResourcesFuture), promise,
        [promise, status](const QList<ProcessResourceStatus> & statuses) {
            Q_UNUSED(statuses)

            promise->addResult(*status);
            promise->finish();
        });

    return future;
}

void ResourcesProcessor::onFoundDuplicate(
    const std::shared_ptr<QPromise<ProcessResourceStatus>> & resourcePromise,
    const std::shared_ptr<DownloadResourcesStatus> & status,
    const utility::cancelers::ManualCancelerPtr & manualCanceler,
    const utility::cancelers::AnyOfCancelerPtr & anyOfCanceler,
    ICallbackWeakPtr && callbackWeak, qevercloud::Resource updatedResource,
    qevercloud::Resource localResource)
{
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
            resourcePromise, status, manualCanceler, anyOfCanceler,
            std::move(callbackWeak), std::move(updatedResource),
            std::move(localResource));
        return;
    }

    downloadFullResourceData(
        resourcePromise, status, manualCanceler, std::move(callbackWeak),
        updatedResource, ResourceKind::UpdatedResource);
}

void ResourcesProcessor::onFoundNoteOwningConflictingResource(
    const std::shared_ptr<QPromise<ProcessResourceStatus>> & resourcePromise,
    const std::shared_ptr<DownloadResourcesStatus> & status,
    const utility::cancelers::ManualCancelerPtr & manualCanceler,
    const utility::cancelers::AnyOfCancelerPtr & anyOfCanceler,
    ICallbackWeakPtr && callbackWeak,
    const qevercloud::Resource & localResource, qevercloud::Note localNote,
    qevercloud::Resource updatedResource)
{
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
        std::move(putLocalNoteFuture),
        threading::TrackedTask{
            selfWeak,
            [this, selfWeak, resourcePromise, status, manualCanceler,
             anyOfCanceler, callbackWeak, updatedResource]() mutable {
                if (anyOfCanceler->isCanceled()) {
                    const auto & guid = *updatedResource.guid();
                    status->cancelledResourceGuidsAndUsns[guid] =
                        updatedResource.updateSequenceNum().value();

                    if (const auto callback = callbackWeak.lock()) {
                        callback->onResourceProcessingCancelled(
                            updatedResource);
                    }

                    resourcePromise->addResult(
                        ProcessResourceStatus::Canceled);

                    resourcePromise->finish();
                    return;
                }

                downloadFullResourceData(
                    resourcePromise, status, manualCanceler,
                    std::move(callbackWeak), updatedResource,
                    ResourceKind::UpdatedResource);
            }});

    threading::onFailed(
        std::move(thenFuture),
        [resourcePromise, status, updatedResource = std::move(updatedResource),
         callbackWeak = std::move(callbackWeak)](const QException & e) mutable {
            if (const auto callback = callbackWeak.lock()) {
                callback->onResourceFailedToProcess(updatedResource, e);
            }

            status->resourcesWhichFailedToProcess
                << DownloadResourcesStatus::ResourceWithException{
                       std::move(updatedResource),
                       std::shared_ptr<QException>(e.clone())};

            resourcePromise->addResult(
                ProcessResourceStatus::FailedToPutResourceToLocalStorage);

            resourcePromise->finish();
        });
}

void ResourcesProcessor::handleResourceConflict(
    const std::shared_ptr<QPromise<ProcessResourceStatus>> & resourcePromise,
    const std::shared_ptr<DownloadResourcesStatus> & status,
    const utility::cancelers::ManualCancelerPtr & manualCanceler,
    const utility::cancelers::AnyOfCancelerPtr & anyOfCanceler,
    ICallbackWeakPtr && callbackWeak, qevercloud::Resource updatedResource,
    qevercloud::Resource localResource)
{
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
            [this, status, resourcePromise, manualCanceler, anyOfCanceler,
             callbackWeak, localResource = std::move(localResource),
             updatedResource](std::optional<qevercloud::Note> note) mutable {
                if (Q_UNLIKELY(!note)) {
                    ErrorString error{QT_TRANSLATE_NOOP(
                        "synchronization::ResourcesProcessor",
                        "Failed to resolve resources conflict: note "
                        "owning the conflicting resource was not found by "
                        "guid")};
                    error.details() = *updatedResource.noteGuid();

                    status->resourcesWhichFailedToProcess
                        << DownloadResourcesStatus::ResourceWithException{
                               std::move(updatedResource),
                               std::make_shared<RuntimeError>(
                                   std::move(error))};

                    if (const auto callback = callbackWeak.lock()) {
                        callback->onResourceFailedToProcess(
                            status->resourcesWhichFailedToProcess.last().first,
                            *status->resourcesWhichFailedToProcess.last()
                                 .second);
                    }

                    resourcePromise->addResult(
                        ProcessResourceStatus::FailedToResolveResourceConflict);

                    resourcePromise->finish();
                    return;
                }

                onFoundNoteOwningConflictingResource(
                    resourcePromise, status, manualCanceler, anyOfCanceler,
                    std::move(callbackWeak), localResource, std::move(*note),
                    std::move(updatedResource));
            }});

    threading::onFailed(
        std::move(thenFuture),
        [resourcePromise, status, callbackWeak,
         updatedResource =
             std::move(updatedResource)](const QException & e) mutable {
            if (const auto callback = callbackWeak.lock()) {
                callback->onResourceFailedToProcess(updatedResource, e);
            }

            status->resourcesWhichFailedToProcess
                << DownloadResourcesStatus::ResourceWithException{
                       std::move(updatedResource),
                       std::shared_ptr<QException>(e.clone())};

            resourcePromise->addResult(
                ProcessResourceStatus::FailedToResolveResourceConflict);

            resourcePromise->finish();
        });
}

void ResourcesProcessor::downloadFullResourceData(
    const std::shared_ptr<QPromise<ProcessResourceStatus>> & resourcePromise,
    const std::shared_ptr<DownloadResourcesStatus> & status,
    const utility::cancelers::ManualCancelerPtr & manualCanceler,
    ICallbackWeakPtr && callbackWeak, const qevercloud::Resource & resource,
    ResourceKind resourceKind)
{
    Q_ASSERT(resource.guid());

    auto downloadFullResourceDataFuture =
        m_resourceFullDataDownloader->downloadFullResourceData(
            *resource.guid());

    const auto selfWeak = weak_from_this();

    auto thenFuture = threading::then(
        std::move(downloadFullResourceDataFuture),
        threading::TrackedTask{
            selfWeak,
            [this, resourcePromise, status, callbackWeak,
             resourceKind](qevercloud::Resource resource) mutable {
                putResourceToLocalStorage(
                    resourcePromise, status, std::move(callbackWeak),
                    std::move(resource), resourceKind);
            }});

    threading::onFailed(
        std::move(thenFuture),
        [resourcePromise, status, resource, manualCanceler,
         callbackWeak = std::move(callbackWeak)](const QException & e) {
            status->resourcesWhichFailedToDownload
                << DownloadResourcesStatus::ResourceWithException{
                       resource, std::shared_ptr<QException>(e.clone())};

            if (const auto callback = callbackWeak.lock()) {
                callback->onResourceFailedToDownload(resource, e);
            }

            bool shouldCancelProcessing = false;
            try {
                e.raise();
            }
            catch (const qevercloud::EDAMSystemException & se) {
                if ((se.errorCode() ==
                     qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED) ||
                    (se.errorCode() == qevercloud::EDAMErrorCode::AUTH_EXPIRED))
                {
                    shouldCancelProcessing = true;
                }
            }
            catch (...) {
            }

            if (shouldCancelProcessing) {
                manualCanceler->cancel();
            }

            resourcePromise->addResult(
                ProcessResourceStatus::FailedToDownloadFullResourceData);

            resourcePromise->finish();
        });
}

void ResourcesProcessor::putResourceToLocalStorage(
    const std::shared_ptr<QPromise<ProcessResourceStatus>> & resourcePromise,
    const std::shared_ptr<DownloadResourcesStatus> & status,
    ICallbackWeakPtr && callbackWeak, qevercloud::Resource resource,
    ResourceKind putResourceKind)
{
    Q_ASSERT(resource.guid());
    Q_ASSERT(resource.updateSequenceNum());

    auto putResourceFuture = m_localStorage->putResource(resource);

    auto thenFuture = threading::then(
        std::move(putResourceFuture),
        [resourcePromise, status, putResourceKind, callbackWeak,
         resourceGuid = *resource.guid(),
         resourceUsn = *resource.updateSequenceNum()] {
            status->processedResourceGuidsAndUsns[resourceGuid] = resourceUsn;

            if (const auto callback = callbackWeak.lock()) {
                callback->onProcessedResource(resourceGuid, resourceUsn);
            }

            resourcePromise->addResult(
                putResourceKind == ResourceKind::NewResource
                    ? ProcessResourceStatus::AddedResource
                    : ProcessResourceStatus::UpdatedResource);

            resourcePromise->finish();
        });

    threading::onFailed(
        std::move(thenFuture),
        [resourcePromise, status, callbackWeak,
         resource = std::move(resource)](const QException & e) mutable {
            if (const auto callback = callbackWeak.lock()) {
                callback->onResourceFailedToProcess(resource, e);
            }

            status->resourcesWhichFailedToProcess
                << DownloadResourcesStatus::ResourceWithException{
                       std::move(resource),
                       std::shared_ptr<QException>(e.clone())};

            resourcePromise->addResult(
                ProcessResourceStatus::FailedToPutResourceToLocalStorage);

            resourcePromise->finish();
        });
}

} // namespace quentier::synchronization
