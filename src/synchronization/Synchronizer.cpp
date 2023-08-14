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

#include "Synchronizer.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/synchronization/Factory.h>
#include <quentier/threading/Future.h>
#include <quentier/threading/TrackedTask.h>

#include <synchronization/IAccountSynchronizer.h>
#include <synchronization/IAccountSynchronizerFactory.h>
#include <synchronization/IAuthenticationInfoProvider.h>
#include <synchronization/IProtocolVersionChecker.h>
#include <synchronization/SyncChunksDataCounters.h>
#include <synchronization/SyncEventsNotifier.h>
#include <synchronization/types/SendStatus.h>
#include <synchronization/types/SyncOptions.h>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include <quentier/threading/Qt5Promise.h>
#endif

namespace quentier::synchronization {

namespace {

class AccountSynchronizerCallback final : public IAccountSynchronizer::ICallback
{
public:
    explicit AccountSynchronizerCallback(
        std::shared_ptr<SyncEventsNotifier> notifier) :
        m_notifier{std::move(notifier)}
    {
        Q_ASSERT(m_notifier);
    }

public: // IDownloader::ICallback
    void onSyncChunksDownloadProgress(
        qint32 highestDownloadedUsn, qint32 highestServerUsn,
        qint32 lastPreviousUsn) override
    {
        m_notifier->notifySyncChunksDownloadProgress(
            highestDownloadedUsn, highestServerUsn, lastPreviousUsn);
    }

    void onSyncChunksDownloaded() override
    {
        m_notifier->notifySyncChunksDownloaded();
    }

    void onSyncChunksDataProcessingProgress(
        SyncChunksDataCountersPtr counters) override
    {
        m_notifier->notifySyncChunksDataProcessingProgress(std::move(counters));
    }

    void onStartLinkedNotebooksDataDownloading(
        const QList<qevercloud::LinkedNotebook> & linkedNotebooks) override
    {
        m_notifier->notifyStartLinkedNotebooksDataDownloading(linkedNotebooks);
    }

    void onLinkedNotebookSyncChunksDownloadProgress(
        qint32 highestDownloadedUsn, qint32 highestServerUsn,
        qint32 lastPreviousUsn,
        const qevercloud::LinkedNotebook & linkedNotebook) override
    {
        m_notifier->notifyLinkedNotebookSyncChunksDownloadProgress(
            highestDownloadedUsn, highestServerUsn, lastPreviousUsn,
            linkedNotebook);
    }

    void onLinkedNotebookSyncChunksDownloaded(
        const qevercloud::LinkedNotebook & linkedNotebook) override
    {
        m_notifier->notifyLinkedNotebookSyncChunksDownloaded(linkedNotebook);
    }

    void onLinkedNotebookSyncChunksDataProcessingProgress(
        SyncChunksDataCountersPtr counters,
        const qevercloud::LinkedNotebook & linkedNotebook) override
    {
        m_notifier->notifyLinkedNotebookSyncChunksDataProcessingProgress(
            std::move(counters), linkedNotebook);
    }

    void onNotesDownloadProgress(
        quint32 notesDownloaded, quint32 totalNotesToDownload) override
    {
        m_notifier->notifyNotesDownloadProgress(
            notesDownloaded, totalNotesToDownload);
    }

    void onLinkedNotebookNotesDownloadProgress(
        quint32 notesDownloaded, quint32 totalNotesToDownload,
        const qevercloud::LinkedNotebook & linkedNotebook) override
    {
        m_notifier->notifyLinkedNotebookNotesDownloadProgress(
            notesDownloaded, totalNotesToDownload, linkedNotebook);
    }

    void onResourcesDownloadProgress(
        quint32 resourcesDownloaded, quint32 totalResourcesToDownload) override
    {
        m_notifier->notifyResourcesDownloadProgress(
            resourcesDownloaded, totalResourcesToDownload);
    }

    void onLinkedNotebookResourcesDownloadProgress(
        quint32 resourcesDownloaded, quint32 totalResourcesToDownload,
        const qevercloud::LinkedNotebook & linkedNotebook) override
    {
        m_notifier->notifyLinkedNotebookResourcesDownloadProgress(
            resourcesDownloaded, totalResourcesToDownload, linkedNotebook);
    }

public: // ISender::ICallback
    void onUserOwnSendStatusUpdate(SendStatusPtr sendStatus) override
    {
        m_notifier->notifyUserOwnSendStatusUpdate(std::move(sendStatus));
    }

    void onLinkedNotebookSendStatusUpdate(
        const qevercloud::Guid & linkedNotebookGuid,
        SendStatusPtr sendStatus) override
    {
        m_notifier->notifyLinkedNotebookSendStatusUpdate(
            linkedNotebookGuid, std::move(sendStatus));
    }

private:
    const std::shared_ptr<SyncEventsNotifier> m_notifier;
};

} // namespace

Synchronizer::Synchronizer(
    IAccountSynchronizerFactoryPtr accountSynchronizerFactory,
    IAuthenticationInfoProviderPtr authenticationInfoProvider,
    IProtocolVersionCheckerPtr protocolVersionChecker) :
    m_accountSynchronizerFactory{std::move(accountSynchronizerFactory)},
    m_authenticationInfoProvider{std::move(authenticationInfoProvider)},
    m_protocolVersionChecker{std::move(protocolVersionChecker)}
{
    if (Q_UNLIKELY(!m_accountSynchronizerFactory)) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "Synchronizer ctor: account synchronizer factory is null")}};
    }

    if (Q_UNLIKELY(!m_authenticationInfoProvider)) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "Synchronizer ctor: authentication info provider is null")}};
    }

    if (Q_UNLIKELY(!m_protocolVersionChecker)) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "Synchronizer ctor: protocol version checker is null")}};
    }
}

QFuture<IAuthenticationInfoPtr> Synchronizer::authenticateNewAccount()
{
    return m_authenticationInfoProvider->authenticateNewAccount();
}

QFuture<IAuthenticationInfoPtr> Synchronizer::authenticateAccount(
    Account account)
{
    return m_authenticationInfoProvider->authenticateAccount(
        std::move(account), IAuthenticationInfoProvider::Mode::Cache);
}

ISynchronizer::SyncResult Synchronizer::synchronizeAccount(
    Account account, local_storage::ILocalStoragePtr localStorage,
    utility::cancelers::ICancelerPtr canceler, ISyncOptionsPtr options,
    ISyncConflictResolverPtr syncConflictResolver)
{
    if (!options) {
        options = std::make_shared<SyncOptions>();
    }

    if (!syncConflictResolver) {
        syncConflictResolver = createSimpleSyncConflictResolver(localStorage);
    }

    auto notifier = std::make_shared<SyncEventsNotifier>();

    auto promise = std::make_shared<QPromise<ISyncResultPtr>>();
    auto future = promise->future();
    promise->start();

    auto authenticationInfoFuture =
        m_authenticationInfoProvider->authenticateAccount(
            account, IAuthenticationInfoProvider::Mode::Cache);

    const auto selfWeak = weak_from_this();
    auto * rawNotifier = notifier.get();

    threading::thenOrFailed(
        std::move(authenticationInfoFuture), promise,
        [this, selfWeak, promise, notifier,
         syncConflictResolver = std::move(syncConflictResolver),
         localStorage = std::move(localStorage), options = std::move(options),
         canceler = std::move(canceler), account = std::move(account)](
            const IAuthenticationInfoPtr & authenticationInfo) mutable {
            Q_ASSERT(authenticationInfo);

            const auto self = selfWeak.lock();
            if (!self) {
                return;
            }

            auto protocolVersionCheckFuture =
                m_protocolVersionChecker->checkProtocolVersion(
                    *authenticationInfo);

            threading::thenOrFailed(
                std::move(protocolVersionCheckFuture), promise,
                [this, selfWeak, promise, notifier = std::move(notifier),
                 syncConflictResolver = std::move(syncConflictResolver),
                 localStorage = std::move(localStorage),
                 options = std::move(options), account = std::move(account),
                 canceler = std::move(canceler)]() mutable {
                    const auto self = selfWeak.lock();
                    if (!self) {
                        return;
                    }

                    auto accountSynchronizer =
                        m_accountSynchronizerFactory->createAccountSynchronizer(
                            std::move(account), std::move(syncConflictResolver),
                            std::move(localStorage), std::move(options));

                    auto callback =
                        std::make_shared<AccountSynchronizerCallback>(notifier);

                    auto accountSyncFuture = accountSynchronizer->synchronize(
                        callback, std::move(canceler));

                    // Passing notifier to the lambda to prolong its lifetime
                    // until the result or promise's failure is available.
                    threading::thenOrFailed(
                        std::move(accountSyncFuture), promise,
                        [promise,
                         accountSynchronizer = std::move(accountSynchronizer),
                         notifier = std::move(notifier),
                         callback =
                             std::move(callback)](ISyncResultPtr result) {
                            promise->addResult(result);
                            promise->finish();
                        });
                });
        });

    return std::make_pair(std::move(future), rawNotifier);
}

void Synchronizer::revokeAuthentication(qevercloud::UserID userId)
{
    m_authenticationInfoProvider->clearCaches(
        IAuthenticationInfoProvider::ClearCacheOptions{
            IAuthenticationInfoProvider::ClearCacheOption::User{userId}});
}

} // namespace quentier::synchronization
