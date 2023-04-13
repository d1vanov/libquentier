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

#include "Fwd.h"
#include "IDownloader.h"

#include <quentier/local_storage/Fwd.h>
#include <quentier/synchronization/Fwd.h>
#include <quentier/synchronization/types/Fwd.h>
#include <quentier/types/Account.h>

#include <synchronization/Fwd.h>
#include <synchronization/types/Fwd.h>
#include <synchronization/types/SyncState.h>

#include <qevercloud/EDAMErrorCode.h>
#include <qevercloud/Fwd.h>
#include <qevercloud/services/Fwd.h>
#include <qevercloud/types/AccountLimits.h>
#include <qevercloud/types/LinkedNotebook.h>
#include <qevercloud/types/User.h>

#include <QDir>
#include <QMutex>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include <quentier/threading/Qt5Promise.h>
#endif

#include <optional>

namespace quentier::synchronization {

class Downloader final :
    public IDownloader,
    public std::enable_shared_from_this<Downloader>
{
public:
    Downloader(
        Account account,
        IAuthenticationInfoProviderPtr authenticationInfoProvider,
        ISyncStateStoragePtr syncStateStorage,
        ISyncChunksProviderPtr syncChunksProvider,
        ILinkedNotebooksProcessorPtr linkedNotebooksProcessor,
        INotebooksProcessorPtr notebooksProcessor,
        IDurableNotesProcessorPtr notesProcessor,
        IDurableResourcesProcessorPtr resourcesProcessor,
        ISavedSearchesProcessorPtr savedSearchesProcessor,
        ITagsProcessorPtr tagsProcessor,
        IFullSyncStaleDataExpungerPtr fullSyncStaleDataExpunger,
        qevercloud::IRequestContextPtr ctx, qevercloud::INoteStorePtr noteStore,
        local_storage::ILocalStoragePtr localStorage,
        const QDir & syncPersistentStorageDir);

    ~Downloader() override;

    [[nodiscard]] QFuture<Result> download(
        utility::cancelers::ICancelerPtr canceler,
        ICallbackWeakPtr callbackWeak) override;

private:
    [[nodiscard]] QFuture<Result> launchDownload(
        const IAuthenticationInfo & authenticationInfo,
        SyncStatePtr lastSyncState,
        utility::cancelers::ICancelerPtr canceler,
        ICallbackWeakPtr callbackWeak);

    enum class SyncMode
    {
        Full,
        Incremental
    };

    enum class CheckForFirstSync
    {
        Yes,
        No
    };

    struct DownloadContext
    {
        SyncStatePtr lastSyncState;
        QList<qevercloud::SyncChunk> syncChunks;
        std::shared_ptr<QPromise<Result>> promise;
        qevercloud::IRequestContextPtr ctx;
        utility::cancelers::ICancelerPtr canceler;
        ICallbackWeakPtr callbackWeak;

        // Linked notebook to which this DownloadContext belongs
        std::optional<qevercloud::LinkedNotebook> linkedNotebook;

        // Running result
        SyncChunksDataCountersPtr syncChunksDataCounters;
        DownloadNotesStatusPtr downloadNotesStatus;
        DownloadResourcesStatusPtr downloadResourcesStatus;
    };

    using DownloadContextPtr = std::shared_ptr<DownloadContext>;

    class LinkedNotebooksProcessorCallback;
    class NotebooksProcessorCallback;
    class NotesProcessorCallback;
    class ResourcesProcessorCallback;
    class SavedSearchesProcessorCallback;
    class TagsProcessorCallback;

    void launchUserOwnDataDownload(
        DownloadContextPtr downloadContext, SyncMode syncMode);

    void listLinkedNotebooksAndLaunchDataDownload(
        DownloadContextPtr downloadContext, SyncMode syncMode);

    void launchLinkedNotebooksDataDownload(
        DownloadContextPtr downloadContext, SyncMode syncMode,
        QList<qevercloud::LinkedNotebook> linkedNotebooks);

    [[nodiscard]] QFuture<Result> startLinkedNotebookDataDownload(
        const DownloadContextPtr & downloadContext, SyncMode syncMode,
        qevercloud::LinkedNotebook linkedNotebook);

    void processSyncChunks(
        DownloadContextPtr downloadContext, SyncMode syncMode,
        CheckForFirstSync checkForFirstSync = CheckForFirstSync::Yes);

    static void updateSyncState(const DownloadContext & downloadContext);

    void downloadNotes(DownloadContextPtr downloadContext, SyncMode syncMode);

    void downloadResources(
        DownloadContextPtr downloadContext, SyncMode syncMode);

    void finalize(DownloadContextPtr & downloadContext);
    void cancel(QPromise<Result> & promise);

private:
    const Account m_account;
    const IAuthenticationInfoProviderPtr m_authenticationInfoProvider;
    const ISyncStateStoragePtr m_syncStateStorage;
    const ISyncChunksProviderPtr m_syncChunksProvider;
    const ILinkedNotebooksProcessorPtr m_linkedNotebooksProcessor;
    const INotebooksProcessorPtr m_notebooksProcessor;
    const IDurableNotesProcessorPtr m_notesProcessor;
    const IDurableResourcesProcessorPtr m_resourcesProcessor;
    const ISavedSearchesProcessorPtr m_savedSearchesProcessor;
    const ITagsProcessorPtr m_tagsProcessor;
    const IFullSyncStaleDataExpungerPtr m_fullSyncStaleDataExpunger;
    const qevercloud::IRequestContextPtr m_ctx;
    const qevercloud::INoteStorePtr m_noteStore;
    const local_storage::ILocalStoragePtr m_localStorage;
    const QDir m_syncPersistentStorageDir;

    std::shared_ptr<QMutex> m_mutex;

    SyncChunksDataCountersPtr m_syncChunksDataCounters;
    SyncChunksDataCountersPtr m_linkedNotebookSyncChunksDataCounters;
};

} // namespace quentier::synchronization
