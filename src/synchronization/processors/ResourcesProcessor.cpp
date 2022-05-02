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

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/RuntimeError.h>
#include <quentier/local_storage/ILocalStorage.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/synchronization/ISyncConflictResolver.h>
#include <quentier/threading/Future.h>
#include <quentier/threading/TrackedTask.h>

#include <qevercloud/services/INoteStore.h>
#include <qevercloud/types/SyncChunk.h>

#include <QFutureWatcher>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>

#include <algorithm>
#include <type_traits>

namespace quentier::synchronization {

namespace {

[[nodiscard]] QList<qevercloud::Resource> collectResources(
    const qevercloud::SyncChunk & syncChunk)
{
    if (!syncChunk.resources() || syncChunk.resources()->isEmpty()) {
        return {};
    }

    QList<qevercloud::Resource> resources;
    resources.reserve(syncChunk.resources()->size());
    for (const auto & resource: qAsConst(*syncChunk.resources())) {
        if (Q_UNLIKELY(!resource.guid())) {
            QNWARNING(
                "synchronization::ResourcesProcessor",
                "Detected resource without guid, skipping it: " << resource);
            continue;
        }

        if (Q_UNLIKELY(!resource.updateSequenceNum())) {
            QNWARNING(
                "synchronization::ResourcesProcessor",
                "Detected resource without update sequence number, skipping "
                    << "it: " << resource);
            continue;
        }

        if (Q_UNLIKELY(!resource.noteGuid())) {
            QNWARNING(
                "synchronization::ResourcesProcessor",
                "Detected resource without note guid, skipping it: "
                    << resource);
            continue;
        }

        resources << resource;
    }

    return resources;
}

} // namespace

ResourcesProcessor::ResourcesProcessor(
    local_storage::ILocalStoragePtr localStorage,
    ISyncConflictResolverPtr syncConflictResolver,
    IResourceFullDataDownloaderPtr resourceFullDataDownloader,
    qevercloud::INoteStorePtr noteStore) :
    m_localStorage{std::move(localStorage)},
    m_syncConflictResolver{std::move(syncConflictResolver)},
    m_resourceFullDataDownloader{std::move(resourceFullDataDownloader)},
    m_noteStore{std::move(noteStore)}
{
    if (Q_UNLIKELY(!m_localStorage)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::ResourcesProcessor",
            "ResourcesProcessor ctor: local storage is null")}};
    }

    if (Q_UNLIKELY(!m_syncConflictResolver)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::ResourcesProcessor",
            "ResourcesProcessor ctor: sync conflict resolver is null")}};
    }

    if (Q_UNLIKELY(!m_resourceFullDataDownloader)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::ResourcesProcessor",
            "ResourcesProcessor ctor: resource full data downloader is null")}};
    }

    if (Q_UNLIKELY(!m_noteStore)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::ResourcesProcessor",
            "ResourcesProcessor ctor: note store is null")}};
    }
}

QFuture<IResourcesProcessor::ProcessResourcesStatus>
    ResourcesProcessor::processResources(
        const QList<qevercloud::SyncChunk> & syncChunks)
{
    QNDEBUG(
        "synchronization::ResourcesProcessor",
        "ResourcesProcessor::processResources");

    QList<qevercloud::Resource> resources;
    for (const auto & syncChunk: qAsConst(syncChunks)) {
        resources << collectResources(syncChunk);
    }

    if (resources.isEmpty()) {
        QNDEBUG(
            "synchronization::ResourcesProcessor",
            "No new/updated resources in the sync chunks");

        return threading::makeReadyFuture<ProcessResourcesStatus>({});
    }

    const int resourceCount = resources.size();

    const auto selfWeak = weak_from_this();

    QList<QFuture<ProcessResourceStatus>> resourceFutures;
    resourceFutures.reserve(resourceCount);

    using FetchResourceOptions =
        local_storage::ILocalStorage::FetchResourceOptions;

    auto status = std::make_shared<ProcessResourcesStatus>();
    for (const auto & resource: qAsConst(resources)) {
        auto resourcePromise =
            std::make_shared<QPromise<ProcessResourceStatus>>();

        resourceFutures << resourcePromise->future();
        resourcePromise->start();

        Q_ASSERT(resource.guid());

        auto findResourceByGuidFuture = m_localStorage->findResourceByGuid(
            *resource.guid(), FetchResourceOptions{});

        auto thenFuture = threading::then(
            std::move(findResourceByGuidFuture),
            threading::TrackedTask{
                selfWeak,
                [this, updatedResource = resource, resourcePromise, status,
                 selfWeak](const std::optional<qevercloud::Resource> &
                               resource) mutable {
                    if (resource) {
                        ++status->m_totalUpdatedResources;
                        onFoundDuplicate(
                            resourcePromise, status, std::move(updatedResource),
                            *resource);
                        return;
                    }

                    ++status->m_totalNewResources;

                    // No duplicate by guid was found, will download full
                    // resource data and then put it into the local storage
                    downloadFullResourceData(
                        resourcePromise, status, updatedResource,
                        ResourceKind::NewResource);
                }});

        threading::onFailed(
            std::move(thenFuture),
            [resourcePromise, status, resource](const QException & e) {
                status->m_resourcesWhichFailedToProcess
                    << ProcessResourcesStatus::ResourceWithException{
                           resource, std::shared_ptr<QException>(e.clone())};

                resourcePromise->addResult(
                    ProcessResourceStatus::FailedToPutResourceToLocalStorage);

                resourcePromise->finish();
            });
    }

    auto allResourcesFuture =
        threading::whenAll<ProcessResourceStatus>(std::move(resourceFutures));

    auto promise = std::make_shared<QPromise<ProcessResourcesStatus>>();
    auto future = promise->future();

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
    const std::shared_ptr<ProcessResourcesStatus> & status,
    qevercloud::Resource updatedResource, qevercloud::Resource localResource)
{
    Q_ASSERT(updatedResource.noteGuid());

    bool shouldMakeLocalConflictNewResource = false;

    if (Q_UNLIKELY(!localResource.noteGuid())) {
        QNWARNING(
            "synchronization::ResourcesProcessor",
            "ResourcesProcessor::onFoundDuplicate: local resource has no "
                << "note guid: " << localResource);
        shouldMakeLocalConflictNewResource = true;
    }
    else if (*localResource.noteGuid() != *updatedResource.noteGuid()) {
        QNWARNING(
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
            resourcePromise, status, std::move(updatedResource),
            std::move(localResource));
        return;
    }

    putResourceToLocalStorage(
        resourcePromise, status, std::move(updatedResource),
        ResourceKind::UpdatedResource);
}

void ResourcesProcessor::onFoundNoteOwningConflictingResource(
    const std::shared_ptr<QPromise<ProcessResourceStatus>> & resourcePromise,
    const std::shared_ptr<ProcessResourcesStatus> & status,
    const qevercloud::Resource & localResource, qevercloud::Note localNote,
    qevercloud::Resource updatedResource)
{
    Q_ASSERT(updatedResource.guid());
    Q_ASSERT(updatedResource.updateSequenceNum());

    const auto updatedResourceGuid = *updatedResource.guid();
    const auto updatedResourceUsn = *updatedResource.updateSequenceNum();

    // Local note would be turned into the conflicting local one with local
    // duplicates of all resources.
    if (localNote.resources()) {
        const auto it = std::find_if(
            localNote.mutableResources()->begin(),
            localNote.mutableResources()->end(),
            [localResourceLocalId = localResource.localId()](
                const qevercloud::Resource & resource)
            {
                return resource.localId() == localResourceLocalId;
            });
        if (it == localNote.mutableResources()->end()) {
            localNote.setResources(
                QList<qevercloud::Resource>{} << localResource);
        }
        else {
            *it = localResource;
        }
    }
    else {
        localNote.setResources(
            QList<qevercloud::Resource>{} << localResource);
    }

    Q_ASSERT(localNote.guid());
    const auto noteGuid = *localNote.guid();

    localNote.setGuid(std::nullopt);
    localNote.setUpdateSequenceNum(std::nullopt);
    localNote.setLocallyModified(true);

    if (localNote.resources()) {
        for (auto & resource: *localNote.mutableResources()) {
            resource.setGuid(std::nullopt);
            resource.setUpdateSequenceNum(std::nullopt);
            resource.setNoteGuid(std::nullopt);
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
            [this, selfWeak, resourcePromise, status, updatedResource]() mutable {
                putResourceToLocalStorage(
                    resourcePromise, status, std::move(updatedResource),
                    ResourceKind::UpdatedResource);
            }});

    threading::onFailed(
        std::move(thenFuture),
        [resourcePromise, status, updatedResource = std::move(updatedResource)](
            const QException & e) mutable {
            status->m_resourcesWhichFailedToProcess
                << ProcessResourcesStatus::ResourceWithException{
                       std::move(updatedResource),
                       std::shared_ptr<QException>(e.clone())};

            resourcePromise->addResult(
                ProcessResourceStatus::FailedToPutResourceToLocalStorage);

            resourcePromise->finish();
        });
}

void ResourcesProcessor::handleResourceConflict(
    const std::shared_ptr<QPromise<ProcessResourceStatus>> & resourcePromise,
    const std::shared_ptr<ProcessResourcesStatus> & status,
    qevercloud::Resource updatedResource, qevercloud::Resource localResource)
{
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
            [this, status, resourcePromise,
             localResource = std::move(localResource),
             updatedResource = std::move(updatedResource)](
                std::optional<qevercloud::Note> note) mutable {
                if (Q_UNLIKELY(!note)) {
                    ErrorString error{QT_TRANSLATE_NOOP(
                        "synchronization::ResourcesProcessor",
                        "Failed to resolve resources conflict: note "
                        "owning the conflicting resource was not found by "
                        "guid")};
                    error.details() = *updatedResource.noteGuid();

                    status->m_resourcesWhichFailedToProcess
                        << ProcessResourcesStatus::ResourceWithException{
                               std::move(updatedResource),
                               std::make_shared<RuntimeError>(
                                   std::move(error))};

                    resourcePromise->addResult(
                        ProcessResourceStatus::FailedToResolveResourceConflict);

                    resourcePromise->finish();
                    return;
                }

                onFoundNoteOwningConflictingResource(
                    resourcePromise, status, localResource, std::move(*note),
                    std::move(updatedResource));
            }});

    threading::onFailed(
        std::move(thenFuture),
        [resourcePromise, status, updatedResource = std::move(updatedResource)](
            const QException & e) mutable {
            status->m_resourcesWhichFailedToProcess
                << ProcessResourcesStatus::ResourceWithException{
                       std::move(updatedResource),
                       std::shared_ptr<QException>(e.clone())};

            resourcePromise->addResult(
                ProcessResourceStatus::FailedToResolveResourceConflict);

            resourcePromise->finish();
        });
}

void ResourcesProcessor::putResourceToLocalStorage(
    const std::shared_ptr<QPromise<ProcessResourceStatus>> & resourcePromise,
    const std::shared_ptr<ProcessResourcesStatus> & status,
    qevercloud::Resource resource, ResourceKind putResourceKind)
{
    // TODO: implement
}

} // namespace quentier::synchronization
