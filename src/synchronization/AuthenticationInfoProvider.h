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
        IUserInfoProviderPtr userInfoProvider, QString host);

public:
    // IAuthenticationInfoProvider
    [[nodiscard]] QFuture<IAuthenticationInfoPtr>
        authenticateNewAccount() override;

    [[nodiscard]] QFuture<IAuthenticationInfoPtr> authenticateAccount(
        Account account, Mode mode = Mode::Cache) override;

    [[nodiscard]] QFuture<IAuthenticationInfoPtr>
        authenticateToLinkedNotebook(
            Account account, qevercloud::Guid linkedNotebookGuid,
            QString sharedNotebookGlobalId, QString noteStoreUrl,
            Mode mode = Mode::Cache) override;

private:
    void authenticateAccountWithoutCache(
        Account account,
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

    [[nodiscard]] QFuture<Account> findAccountForUserId(
        qevercloud::UserID userId, QString shardId);

    [[nodiscard]] QFuture<void> storeAuthenticationInfo(
        IAuthenticationInfoPtr info, Account account);

private:
    const IAuthenticatorPtr m_authenticator;
    const IKeychainServicePtr m_keychainService;
    const IUserInfoProviderPtr m_userInfoProvider;
    const QString m_host;
};

} // namespace quentier::synchronization
