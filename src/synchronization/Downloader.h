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

#include "IDownloader.h"
#include "Fwd.h"

#include <quentier/local_storage/Fwd.h>
#include <quentier/synchronization/Fwd.h>
#include <quentier/types/Account.h>

#include <synchronization/types/SyncState.h>

#include <qevercloud/EDAMErrorCode.h>
#include <qevercloud/Fwd.h>
#include <qevercloud/services/Fwd.h>
#include <qevercloud/types/AccountLimits.h>
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
        IProtocolVersionCheckerPtr protocolVersionChecker,
        IUserInfoProviderPtr userInfoProvider,
        IAccountLimitsProviderPtr accountLimitsProvider,
        ISyncStateStoragePtr syncStateStorage,
        ISyncChunksProviderPtr syncChunksProvider,
        ISyncChunksStoragePtr syncChunksStorage,
        ILinkedNotebooksProcessorPtr linkedNotebooksProcessor,
        INotebooksProcessorPtr notebooksProcessor,
        IDurableNotesProcessorPtr notesProcessor,
        IDurableResourcesProcessorPtr resourcesProcessor,
        ISavedSearchesProcessorPtr savedSearchesProcessor,
        ITagsProcessorPtr tagsProcessor,
        IFullSyncStaleDataExpungerPtr fullSyncStaleDataExpunger,
        qevercloud::IRequestContextPtr ctx,
        qevercloud::INoteStorePtr noteStore,
        local_storage::ILocalStoragePtr localStorage,
        const QDir & syncPersistentStorageDir);

    ~Downloader() override;

    [[nodiscard]] QFuture<Result> download(
        utility::cancelers::ICancelerPtr canceler,
        ICallbackWeakPtr callbackWeak) override;

private:
    void readLastSyncState();

    [[nodiscard]] QFuture<Result> launchDownload(
        const IAuthenticationInfo & authenticationInfo,
        utility::cancelers::ICancelerPtr canceler,
        ICallbackWeakPtr callbackWeak);

    enum class SyncMode
    {
        Full,
        Incremental
    };

    void launchUserOwnDataDownload(
        std::shared_ptr<QPromise<Result>> promise,
        qevercloud::IRequestContextPtr ctx,
        utility::cancelers::ICancelerPtr canceler,
        ICallbackWeakPtr callbackWeak,
        SyncMode syncMode);

    void launchLinkedNotebooksDataDownload(
        std::shared_ptr<QPromise<Result>> promise,
        qevercloud::IRequestContextPtr ctx,
        utility::cancelers::ICancelerPtr canceler,
        ICallbackWeakPtr callbackWeak);

    [[nodiscard]] QFuture<qevercloud::User> fetchUser(
        qevercloud::IRequestContextPtr ctx);

    [[nodiscard]] QFuture<qevercloud::AccountLimits> fetchAccountLimits(
        qevercloud::ServiceLevel serviceLevel,
        qevercloud::IRequestContextPtr ctx);

    enum class CheckForFirstSync
    {
        Yes,
        No
    };

    void processUserOwnSyncChunks(
        QList<qevercloud::SyncChunk> syncChunks,
        std::shared_ptr<QPromise<Result>> promise,
        qevercloud::IRequestContextPtr ctx,
        utility::cancelers::ICancelerPtr canceler,
        ICallbackWeakPtr callbackWeak, SyncMode syncMode,
        CheckForFirstSync checkForFirstSync = CheckForFirstSync::Yes);

    void cancel(QPromise<Result> & promise);

private:
    const Account m_account;
    const IAuthenticationInfoProviderPtr m_authenticationInfoProvider;
    const IProtocolVersionCheckerPtr m_protocolVersionChecker;
    const IUserInfoProviderPtr m_userInfoProvider;
    const IAccountLimitsProviderPtr m_accountLimitsProvider;
    const ISyncStateStoragePtr m_syncStateStorage;
    const ISyncChunksProviderPtr m_syncChunksProvider;
    const ISyncChunksStoragePtr m_syncChunksStorage;
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

    QMutex m_mutex;
    std::optional<QFuture<Result>> m_future;
    std::optional<SyncState> m_lastSyncState;
    std::optional<QFuture<qevercloud::User>> m_userFuture;
    std::optional<QFuture<qevercloud::AccountLimits>> m_accountLimitsFuture;

    SyncChunksDataCountersPtr m_syncChunksDataCounters;
    SyncChunksDataCountersPtr m_linkedNotebookSyncChunksDataCounters;
};

} // namespace quentier::synchronization
