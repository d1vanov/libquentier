/*
 * Copyright 2023-2024 Dmitry Ivanov
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

#include <quentier/threading/Fwd.h>
#include <quentier/types/Account.h>

#include <synchronization/Fwd.h>
#include <synchronization/IAccountSynchronizer.h>
#include <synchronization/types/Fwd.h>

#include <qevercloud/types/TypeAliases.h>

#include <QHash>
#include <QList>
#include <QMutex>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include <quentier/threading/Qt5Promise.h>
#endif

#include <memory>

class QDebug;

namespace quentier::synchronization {

class AccountSynchronizer final :
    public IAccountSynchronizer,
    public std::enable_shared_from_this<AccountSynchronizer>
{
public:
    AccountSynchronizer(
        Account account, IDownloaderPtr downloader, ISenderPtr sender,
        IAuthenticationInfoProviderPtr authenticationInfoProvider,
        ISyncStateStoragePtr syncStateStorage,
        ISyncChunksStoragePtr syncChunksStorage);

public: // IAccountSynchronizer
    [[nodiscard]] QFuture<ISyncResultPtr> synchronize(
        ICallbackWeakPtr callbackWeak,
        utility::cancelers::ICancelerPtr canceler) override;

private:
    class CallbackWrapper;
    using CallbackWrapperPtr = std::shared_ptr<CallbackWrapper>;

    struct Context
    {
        std::shared_ptr<QPromise<ISyncResultPtr>> promise;
        CallbackWrapperPtr callbackWrapper;
        utility::cancelers::ICancelerPtr canceler;
        SyncResultPtr previousSyncResult;
        bool sendNeeded = true;

        std::shared_ptr<QMutex> syncChunksMutex;
        QList<qevercloud::SyncChunk> downloadedUserOwnSyncChunks;
        QHash<qevercloud::Guid, QList<qevercloud::SyncChunk>>
            downloadedLinkedNotebookSyncChunks;
    };

    using ContextPtr = std::shared_ptr<Context>;
    using ContextWeakPtr = std::weak_ptr<Context>;

    enum class SendAfterDownload
    {
        Yes,
        No
    };

    friend QDebug & operator<<(
        QDebug & dbg, SendAfterDownload sendAfterDownload);

    void synchronizeImpl(
        ContextPtr context,
        SendAfterDownload sendAfterDownload = SendAfterDownload::Yes);

    void onDownloadFinished(
        ContextPtr context, const IDownloader::Result & downloadResult,
        SendAfterDownload sendAfterDownload);

    void onDownloadFailed(ContextPtr context, const QException & e);

    void updateStoredSyncState(const IDownloader::Result & downloadResult);

    [[nodiscard]] bool processDownloadStopSynchronizationError(
        const ContextPtr & context, const IDownloader::Result & downloadResult);

    void appendToPreviousSyncResult(
        Context & context, const IDownloader::Result & downloadResult) const;

    void appendToPreviousSyncResult(
        Context & context, const ISender::Result & sendResult) const;

    void send(ContextPtr context);
    void onSendFinished(ContextPtr context, const ISender::Result & sendResult);

    void updateStoredSyncState(const ISender::Result & sendResult);

    [[nodiscard]] bool processSendStopSynchronizationError(
        const ContextPtr & context, const ISender::Result & sendResult);

    void storeDownloadedSyncChunks(Context & context);
    void clearAuthenticationCachesAndRestartSync(ContextPtr context);

    void finalize(Context & context);

    void clearIntermediatePersistence(const ISyncResult & syncResult);

private:
    const Account m_account;
    const IDownloaderPtr m_downloader;
    const ISenderPtr m_sender;
    const IAuthenticationInfoProviderPtr m_authenticationInfoProvider;
    const ISyncStateStoragePtr m_syncStateStorage;
    const ISyncChunksStoragePtr m_syncChunksStorage;
};

} // namespace quentier::synchronization
