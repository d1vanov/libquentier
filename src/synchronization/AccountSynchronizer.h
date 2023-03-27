/*
 * Copyright 2023 Dmitry Ivanov
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

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include <quentier/threading/Qt5Promise.h>
#endif

#include <memory>

namespace quentier::synchronization {

class AccountSynchronizer final :
    public IAccountSynchronizer,
    public std::enable_shared_from_this<AccountSynchronizer>
{
public:
    AccountSynchronizer(
        Account account, IDownloaderPtr downloader, ISenderPtr sender,
        IAuthenticationInfoProviderPtr authenticationInfoProvider,
        threading::QThreadPoolPtr threadPool);

public: // IAccountSynchronizer
    [[nodiscard]] QFuture<ISyncResultPtr> synchronize(
        ICallbackWeakPtr callbackWeak,
        utility::cancelers::ICancelerPtr canceler) override;

private:
    struct Context
    {
        std::shared_ptr<QPromise<ISyncResultPtr>> promise;
        ICallbackWeakPtr callbackWeak;
        utility::cancelers::ICancelerPtr canceler;
        SyncResultPtr previousSyncResult;
        bool sendNeeded = true;
    };

    using ContextPtr = std::shared_ptr<Context>;

    void synchronizeImpl(ContextPtr context);

    void onDownloadFinished(
        ContextPtr context, const IDownloader::Result & downloadResult);

    void onDownloadFailed(ContextPtr context, const QException & e);

    [[nodiscard]] bool processDownloadStopSynchronizationError(
        const ContextPtr & context, const IDownloader::Result & downloadResult);

    void appendToPreviousSyncResult(
        Context & context, const IDownloader::Result & downloadResult) const;

    void appendToPreviousSyncResult(
        Context & context, const ISender::Result & sendResult) const;

    void send(ContextPtr context);
    void onSendFinished(ContextPtr context, const ISender::Result & sendResult);

    [[nodiscard]] bool processSendStopSynchronizationError(
        const ContextPtr & context, const ISender::Result & sendResult);

    void clearAuthenticationCachesAndRestartSync(ContextPtr context);

private:
    const Account m_account;
    const IDownloaderPtr m_downloader;
    const ISenderPtr m_sender;
    const IAuthenticationInfoProviderPtr m_authenticationInfoProvider;
    const threading::QThreadPoolPtr m_threadPool;
};

} // namespace quentier::synchronization
