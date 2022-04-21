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

#pragma once

#include "IResourcesProcessor.h"

#include <quentier/local_storage/Fwd.h>
#include <quentier/synchronization/Fwd.h>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include <quentier/threading/Qt5Promise.h>
#endif

#include <synchronization/Fwd.h>

#include <qevercloud/services/Fwd.h>

namespace quentier::synchronization {

class ResourcesProcessor final :
    public IResourcesProcessor,
    public std::enable_shared_from_this<ResourcesProcessor>
{
public:
    explicit ResourcesProcessor(
        local_storage::ILocalStoragePtr localStorage,
        ISyncConflictResolverPtr syncConflictResolver,
        IResourceFullDataDownloaderPtr resourceFullDataDownloader,
        qevercloud::INoteStorePtr noteStore);

    [[nodiscard]] QFuture<ProcessResourcesStatus> processResources(
        const QList<qevercloud::SyncChunk> & syncChunks) override;

private:
    enum class ProcessResourceStatus
    {
        AddedResource,
        UpdatedResource,
        IgnoredResource,
        FailedToDownloadFullResourceData,
        FailedToPutResourceToLocalStorage,
        FailedToResolveResourceConflict
    };

    void onFoundDuplicate(
        const std::shared_ptr<QPromise<ProcessResourceStatus>> & resourcePromise,
        const std::shared_ptr<ProcessResourcesStatus> & status,
        qevercloud::Resource updatedResource,
        qevercloud::Resource localResource);

    enum class ResourceKind
    {
        NewResource,
        UpdatedResource
    };

    void downloadFullResourceData(
        const std::shared_ptr<QPromise<ProcessResourceStatus>> & resourcePromise,
        const std::shared_ptr<ProcessResourcesStatus> & status,
        const qevercloud::Resource & resource, ResourceKind resourceKind);

    void putResourceToLocalStorage(
        const std::shared_ptr<QPromise<ProcessResourceStatus>> & notePromise,
        const std::shared_ptr<ProcessResourcesStatus> & status,
        qevercloud::Resource resource, ResourceKind putResourceKind);

private:
    const local_storage::ILocalStoragePtr m_localStorage;
    const ISyncConflictResolverPtr m_syncConflictResolver;
    const IResourceFullDataDownloaderPtr m_resourceFullDataDownloader;
    const qevercloud::INoteStorePtr m_noteStore;
};

} // namespace quentier::synchronization
