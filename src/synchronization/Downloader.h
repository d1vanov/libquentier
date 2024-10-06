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

#include "Fwd.h"
#include "IDownloader.h"

#include <quentier/local_storage/Fwd.h>
#include <quentier/synchronization/Fwd.h>
#include <quentier/synchronization/types/Fwd.h>
#include <quentier/threading/Fwd.h>
#include <quentier/types/Account.h>

#include <synchronization/Fwd.h>
#include <synchronization/SynchronizationMode.h>
#include <synchronization/types/Fwd.h>
#include <synchronization/types/SyncState.h>

#include <qevercloud/EDAMErrorCode.h>
#include <qevercloud/Fwd.h>
#include <qevercloud/services/Fwd.h>
#include <qevercloud/types/AccountLimits.h>
#include <qevercloud/types/LinkedNotebook.h>
#include <qevercloud/types/User.h>

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
        INoteStoreProviderPtr noteStoreProvider,
        ILinkedNotebookTagsCleanerPtr linkedNotebookTagsCleaner,
        local_storage::ILocalStoragePtr localStorage,
        qevercloud::IRequestContextPtr ctx = {},
        qevercloud::IRetryPolicyPtr retryPolicy = {});

    ~Downloader() override;

    [[nodiscard]] QFuture<Result> download(
        utility::cancelers::ICancelerPtr canceler,
        ICallbackWeakPtr callbackWeak) override;

private:
    [[nodiscard]] QFuture<Result> launchDownload(
        const IAuthenticationInfo & authenticationInfo,
        SyncStatePtr lastSyncState, utility::cancelers::ICancelerPtr canceler,
        ICallbackWeakPtr callbackWeak);

    enum class CheckForFirstSync
    {
        Yes,
        No
    };

    struct DownloadContext
    {
        SyncStatePtr lastSyncState;
        threading::QMutexPtr lastSyncStateMutex;

        QList<qevercloud::SyncChunk> syncChunks;
        std::shared_ptr<QPromise<Result>> promise;
        qevercloud::IRequestContextPtr ctx;
        utility::cancelers::ICancelerPtr canceler;
        ICallbackWeakPtr callbackWeak;

        std::optional<qevercloud::SyncState> serverSyncState;

        // Linked notebook to which this DownloadContext belongs
        std::optional<qevercloud::LinkedNotebook> linkedNotebook;

        // Authentication token to be used for downloading notes and resources
        // from linked notebook if this download context belongs to a linked
        // notebook.
        std::optional<QString> linkedNotebookAuthToken;

        // Running result
        SyncChunksDataCountersPtr syncChunksDataCounters;
        bool syncChunksDownloaded = false;
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
        DownloadContextPtr downloadContext, SynchronizationMode syncMode);

    void listLinkedNotebooksAndLaunchDataDownload(
        DownloadContextPtr downloadContext, SynchronizationMode syncMode);

    void launchLinkedNotebooksDataDownload(
        DownloadContextPtr downloadContext, SynchronizationMode syncMode,
        QList<qevercloud::LinkedNotebook> linkedNotebooks);

    void launchLinkedNotebookDataDownload(
        DownloadContextPtr downloadContext, SynchronizationMode syncMode,
        qevercloud::LinkedNotebook linkedNotebook,
        const qevercloud::INoteStorePtr & noteStore,
        const std::shared_ptr<QPromise<Result>> & linkedNotebookResultPromise);

    [[nodiscard]] QFuture<Result>
        fetchAuthInfoAndStartLinkedNotebookDataDownload(
            const DownloadContextPtr & downloadContext,
            qevercloud::SyncState linkedNotebookSyncState,
            SynchronizationMode syncMode,
            qevercloud::LinkedNotebook linkedNotebook);

    void startLinkedNotebookDataDownload(
        DownloadContextPtr downloadContext, SynchronizationMode syncMode);

    void processSyncChunks(
        DownloadContextPtr downloadContext, SynchronizationMode syncMode,
        CheckForFirstSync checkForFirstSync = CheckForFirstSync::Yes);

    void initializeTotalsInSyncChunksDataCounters(
        const QList<qevercloud::SyncChunk> & syncChunks,
        SyncChunksDataCounters & syncChunksDataCounters) const;

    static void updateSyncState(const DownloadContext & downloadContext);

    void downloadNotes(
        DownloadContextPtr downloadContext, SynchronizationMode syncMode);

    void downloadResources(
        DownloadContextPtr downloadContext, SynchronizationMode syncMode);

    static void finalize(
        const DownloadContextPtr & downloadContext,
        QHash<qevercloud::Guid, LocalResult> linkedNotebookResults = {});

    static void cancel(QPromise<Result> & promise);

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
    const INoteStoreProviderPtr m_noteStoreProvider;
    const ILinkedNotebookTagsCleanerPtr m_linkedNotebookTagsCleaner;
    const local_storage::ILocalStoragePtr m_localStorage;
    const qevercloud::IRequestContextPtr m_ctx;
    const qevercloud::IRetryPolicyPtr m_retryPolicy;

    std::shared_ptr<QMutex> m_mutex;

    SyncChunksDataCountersPtr m_syncChunksDataCounters;
    SyncChunksDataCountersPtr m_linkedNotebookSyncChunksDataCounters;
};

} // namespace quentier::synchronization
