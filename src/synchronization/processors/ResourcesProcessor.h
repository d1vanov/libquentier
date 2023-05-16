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

#pragma once

#include "IResourcesProcessor.h"

#include <quentier/local_storage/Fwd.h>
#include <quentier/synchronization/Fwd.h>
#include <quentier/threading/Fwd.h>
#include <quentier/utility/cancelers/Fwd.h>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include <quentier/threading/Qt5Promise.h>
#endif

#include <synchronization/Fwd.h>

#include <qevercloud/Fwd.h>

#include <QMutex>

#include <memory>

namespace quentier::synchronization {

class ResourcesProcessor final :
    public IResourcesProcessor,
    public std::enable_shared_from_this<ResourcesProcessor>
{
public:
    ResourcesProcessor(
        local_storage::ILocalStoragePtr localStorage,
        IResourceFullDataDownloaderPtr resourceFullDataDownloader,
        INoteStoreProviderPtr noteStoreProvider,
        INotebookFinderPtr notebookFinder,
        qevercloud::IRequestContextPtr ctx = {},
        qevercloud::IRetryPolicyPtr retryPolicy = {},
        threading::QThreadPoolPtr threadPool = {});

    [[nodiscard]] QFuture<DownloadResourcesStatusPtr> processResources(
        const QList<qevercloud::SyncChunk> & syncChunks,
        utility::cancelers::ICancelerPtr canceler,
        ICallbackWeakPtr callbackWeak) override;

private:
    enum class ProcessResourceStatus
    {
        AddedResource,
        UpdatedResource,
        IgnoredResource,
        FailedToDownloadFullResourceData,
        FailedToPutResourceToLocalStorage,
        FailedToResolveResourceConflict,
        Canceled
    };

    struct Context
    {
        utility::cancelers::ManualCancelerPtr manualCanceler;
        utility::cancelers::ICancelerPtr canceler;
        ICallbackWeakPtr callbackWeak;

        DownloadResourcesStatusPtr status;
        threading::QMutexPtr statusMutex;
    };

    using ContextPtr = std::shared_ptr<Context>;

    void onFoundDuplicate(
        ContextPtr context,
        std::shared_ptr<QPromise<ProcessResourceStatus>> promise,
        qevercloud::Resource updatedResource,
        qevercloud::Resource localResource);

    void onFoundNoteOwningConflictingResource(
        const ContextPtr & context,
        const std::shared_ptr<QPromise<ProcessResourceStatus>> & promise,
        qevercloud::Resource updatedResource,
        const qevercloud::Resource & localResource, qevercloud::Note localNote);

    void handleResourceConflict(
        const ContextPtr & context,
        const std::shared_ptr<QPromise<ProcessResourceStatus>> & promise,
        qevercloud::Resource updatedResource,
        qevercloud::Resource localResource);

    enum class ResourceKind
    {
        NewResource,
        UpdatedResource
    };

    void downloadFullResourceData(
        ContextPtr context,
        std::shared_ptr<QPromise<ProcessResourceStatus>> promise,
        qevercloud::Resource resource, ResourceKind resourceKind);

    void downloadFullResourceData(
        ContextPtr context,
        std::shared_ptr<QPromise<ProcessResourceStatus>> promise,
        qevercloud::Resource resource, ResourceKind resourceKind,
        const qevercloud::Notebook & notebook);

    void downloadFullResourceData(
        ContextPtr context,
        std::shared_ptr<QPromise<ProcessResourceStatus>> promise,
        qevercloud::Resource resource, ResourceKind resourceKind,
        const qevercloud::INoteStorePtr & noteStore);

    void putResourceToLocalStorage(
        const ContextPtr & context,
        const std::shared_ptr<QPromise<ProcessResourceStatus>> & promise,
        qevercloud::Resource resource, ResourceKind resourceKind);

private:
    const local_storage::ILocalStoragePtr m_localStorage;
    const IResourceFullDataDownloaderPtr m_resourceFullDataDownloader;
    const INoteStoreProviderPtr m_noteStoreProvider;
    const INotebookFinderPtr m_notebookFinder;
    const qevercloud::IRequestContextPtr m_ctx;
    const qevercloud::IRetryPolicyPtr m_retryPolicy;
    const threading::QThreadPoolPtr m_threadPool;
};

} // namespace quentier::synchronization
