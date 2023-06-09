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
#include <quentier/synchronization/IAuthenticator.h>

#include <QList>
#include <QMutex>

#include <atomic>

namespace quentier::synchronization::tests {

class FakeAuthenticator final : public IAuthenticator
{
public:
    struct AccountAuthInfo
    {
        Account account;
        IAuthenticationInfoPtr authInfo;
    };

    FakeAuthenticator(
        threading::QThreadPoolPtr threadPool = nullptr,
        QList<AccountAuthInfo> accountAuthInfos = {});

    [[nodiscard]] QList<AccountAuthInfo> accountAuthInfos() const;

    void putAccountAuthInfo(Account account, IAuthenticationInfoPtr authInfo);

    [[nodiscard]] IAuthenticationInfoPtr findAuthInfo(
        const Account & account) const;

    void removeAuthInfo(const Account & account);
    void clear();

public: // IAuthenticator
    [[nodiscard]] QFuture<IAuthenticationInfoPtr>
        authenticateNewAccount() override;

    [[nodiscard]] QFuture<IAuthenticationInfoPtr> authenticateAccount(
        Account account) override;

private:
    const threading::QThreadPoolPtr m_threadPool;

    QList<AccountAuthInfo> m_accountAuthInfos;
    std::atomic<int> m_counter = 1;
    mutable QMutex m_mutex;
};

} // namespace quentier::synchronization::tests
