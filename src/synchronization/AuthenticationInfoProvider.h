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

#include <quentier/synchronization/Fwd.h>
#include <quentier/types/Account.h>
#include <quentier/utility/Fwd.h>

#include <synchronization/Fwd.h>
#include <synchronization/IAuthenticationInfoProvider.h>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include <quentier/threading/Qt5Promise.h>
#endif

#include <qevercloud/Fwd.h>
#include <qevercloud/types/TypeAliases.h>

#include <QHash>
#include <QReadWriteLock>

#include <memory>

namespace quentier::synchronization {

struct AuthenticationInfo;

class AuthenticationInfoProvider final :
    public IAuthenticationInfoProvider,
    public std::enable_shared_from_this<AuthenticationInfoProvider>
{
public:
    AuthenticationInfoProvider(
        IAuthenticatorPtr authenticator, IKeychainServicePtr keychainService,
        IUserInfoProviderPtr userInfoProvider,
        INoteStoreFactoryPtr noteStoreFactory,
        qevercloud::IRequestContextPtr ctx,
        qevercloud::IRetryPolicyPtr retryPolicy, QString host);

public:
    // IAuthenticationInfoProvider
    [[nodiscard]] QFuture<IAuthenticationInfoPtr> authenticateNewAccount()
        override;

    [[nodiscard]] QFuture<IAuthenticationInfoPtr> authenticateAccount(
        Account account, Mode mode = Mode::Cache) override;

    [[nodiscard]] QFuture<IAuthenticationInfoPtr> authenticateToLinkedNotebook(
        Account account, qevercloud::LinkedNotebook linkedNotebook,
        Mode mode = Mode::Cache) override;

    void clearCaches(
        const ClearCacheOptions & clearCacheOptions = ClearCacheOptions{
            ClearCacheOption::All{}}) override;

private:
    void authenticateAccountWithoutCache(
        Account account,
        const std::shared_ptr<QPromise<IAuthenticationInfoPtr>> & promise);

    void authenticateToLinkedNotebookWithoutCache(
        Account account, qevercloud::LinkedNotebook linkedNotebook,
        const std::shared_ptr<QPromise<IAuthenticationInfoPtr>> & promise);

    /**
     * @return either nonnull pointer to AuthenticationInfo with data filled
     *         from persistent ApplicationSettings but without authentication
     *         token and shard id as these are stored in the keychain or
     *         null pointer if there is no persistent data for the passed
     *         account or if some error occurs when trying to read it
     */
    [[nodiscard]] std::shared_ptr<AuthenticationInfo>
        readAuthenticationInfoPart(const Account & account) const;

    struct LinkedNotebookTimestamps
    {
        qevercloud::Timestamp authenticationTimestamp = 0;
        qevercloud::Timestamp expirationTimestamp = 0;
    };

    [[nodiscard]] std::optional<LinkedNotebookTimestamps>
        readLinkedNotebookTimestamps(
            const Account & account,
            const qevercloud::Guid & linkedNotebookGuid) const;

    [[nodiscard]] QFuture<Account> findAccountForUserId(
        qevercloud::UserID userId, QString authToken, QString shardId,
        QList<QNetworkCookie> networkCookies);

    [[nodiscard]] QFuture<void> storeAuthenticationInfo(
        IAuthenticationInfoPtr authenticationInfo, Account account);

    [[nodiscard]] QFuture<void> storeLinkedNotebookAuthenticationInfo(
        IAuthenticationInfoPtr authenticationInfo,
        qevercloud::Guid linkedNotebookGuid, Account account);

    void clearAllUserCaches();
    void clearAllLinkedNotebookCaches();
    void clearUserCache(qevercloud::UserID userId);
    void clearLinkedNotebookCache(const qevercloud::Guid & linkedNotebookGuid);

    struct AccountAuthenticationInfo
    {
        Account account;
        IAuthenticationInfoPtr authenticationInfo;
    };

    void clearUserCacheImpl(
        const AccountAuthenticationInfo & authenticationInfo);

    void clearLinkedNotebookCacheImpl(
        const qevercloud::Guid & linkedNotebookGuid,
        const AccountAuthenticationInfo & authenticationInfo);

private:
    const IAuthenticatorPtr m_authenticator;
    const IKeychainServicePtr m_keychainService;
    const IUserInfoProviderPtr m_userInfoProvider;
    const INoteStoreFactoryPtr m_noteStoreFactory;
    const qevercloud::IRequestContextPtr m_ctx;
    const qevercloud::IRetryPolicyPtr m_retryPolicy;
    const QString m_host;

    QReadWriteLock m_authenticationInfosRWLock;
    QHash<qevercloud::UserID, AccountAuthenticationInfo> m_authenticationInfos;

    QReadWriteLock m_linkedNotebookAuthenticationInfosRWLock;
    QHash<qevercloud::Guid, AccountAuthenticationInfo>
        m_linkedNotebookAuthenticationInfos;
};

} // namespace quentier::synchronization
