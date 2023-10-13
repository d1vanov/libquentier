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

#include "FakeAuthenticator.h"

#include <quentier/logging/QuentierLogger.h>
#include <quentier/synchronization/types/IAuthenticationInfo.h>
#include <quentier/synchronization/types/IAuthenticationInfoBuilder.h>
#include <quentier/threading/Factory.h>
#include <quentier/threading/Runnable.h>
#include <quentier/utility/DateTime.h>

#include <QDateTime>
#include <QMutexLocker>
#include <QThreadPool>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include <quentier/threading/Qt5Promise.h>
#endif

namespace quentier::synchronization::tests {

FakeAuthenticator::FakeAuthenticator(
    threading::QThreadPoolPtr threadPool,
    QList<AccountAuthInfo> accountAuthInfos) :
    m_threadPool{
        threadPool ? std::move(threadPool) : threading::globalThreadPool()},
    m_accountAuthInfos{std::move(accountAuthInfos)}
{
    Q_ASSERT(m_threadPool);
}

QList<FakeAuthenticator::AccountAuthInfo> FakeAuthenticator::accountAuthInfos() const
{
    const QMutexLocker locker{&m_mutex};
    return m_accountAuthInfos;
}

void FakeAuthenticator::putAccountAuthInfo(
    Account account, IAuthenticationInfoPtr authInfo)
{
    QNDEBUG(
        "tests::synchronization::FakeAuthenticator",
        "FakeAuthenticator::putAccountAuthInfo: account = " << account
            << "\nAuth info: " << *authInfo);

    const QMutexLocker locker{&m_mutex};
    m_accountAuthInfos
        << AccountAuthInfo{std::move(account), std::move(authInfo)};
}

IAuthenticationInfoPtr FakeAuthenticator::findAuthInfo(
    const Account & account) const
{
    const QMutexLocker locker{&m_mutex};
    for (const auto & accountAuthInfo: qAsConst(m_accountAuthInfos)) {
        if (accountAuthInfo.account == account) {
            QNDEBUG(
                "tests::synchronization::FakeAuthenticator",
                "FakeAuthenticator::findAuthInfo: found auth info: "
                    << *accountAuthInfo.authInfo
                    << "\nFor account: " << account);
            return accountAuthInfo.authInfo;
        }
    }

    return nullptr;
}

void FakeAuthenticator::removeAuthInfo(const Account & account)
{
    const QMutexLocker locker{&m_mutex};
    for (auto it = m_accountAuthInfos.begin(), end = m_accountAuthInfos.end();
         it != end; ++it)
    {
        if (it->account == account) {
            m_accountAuthInfos.erase(it);
            return;
        }
    }
}

void FakeAuthenticator::clear()
{
    const QMutexLocker locker{&m_mutex};
    m_accountAuthInfos.clear();
}

QFuture<IAuthenticationInfoPtr> FakeAuthenticator::authenticateNewAccount()
{
    int counter = m_counter.fetch_add(1, std::memory_order_acq_rel);

    Account account{
        QString::fromUtf8("Account %1").arg(counter),
        Account::Type::Evernote,
        qevercloud::UserID{counter},
        Account::EvernoteAccountType::Free,
        QStringLiteral("www.evernote.com"),
        QStringLiteral("shard id")};

    auto authInfoBuilder = createAuthenticationInfoBuilder();
    const auto now = QDateTime::currentMSecsSinceEpoch();

    auto authInfo = authInfoBuilder
        ->setUserId(account.id())
        .setAuthToken(QStringLiteral("Auth token"))
        .setAuthenticationTime(now)
        .setAuthTokenExpirationTime(now + 1000)
        .setNoteStoreUrl(QStringLiteral("Note store url"))
        .setShardId(account.shardId())
        .build();

    {
        const QMutexLocker locker{&m_mutex};
        m_accountAuthInfos << AccountAuthInfo{std::move(account), authInfo};
    }

    auto promise = std::make_shared<QPromise<IAuthenticationInfoPtr>>();
    auto future = promise->future();
    promise->start();

    std::unique_ptr<QRunnable> runnable{threading::createFunctionRunnable(
        [promise = std::move(promise),
         authInfo = std::move(authInfo)]() mutable {
            promise->addResult(std::move(authInfo));
            promise->finish();
        })};

    m_threadPool->start(runnable.release());
    return future;
}

QFuture<IAuthenticationInfoPtr> FakeAuthenticator::authenticateAccount(
    Account account) // NOLINT
{
    QNDEBUG(
        "tests::synchronization::FakeAuthenticator",
        "FakeAuthenticator::authenticateAccount: " << account);

    IAuthenticationInfoPtr authInfo = findAuthInfo(account);
    if (authInfo) {
        QNDEBUG(
            "tests::synchronization::FakeAuthenticator",
            "FakeAuthenticator::authenticateAccount: found authentication info: "
                << *authInfo);
    }

    auto promise = std::make_shared<QPromise<IAuthenticationInfoPtr>>();
    auto future = promise->future();
    promise->start();

    std::unique_ptr<QRunnable> runnable{threading::createFunctionRunnable(
        [promise = std::move(promise),
         authInfo = std::move(authInfo)]() mutable {
            promise->addResult(std::move(authInfo));
            promise->finish();
        })};

    m_threadPool->start(runnable.release());
    return future;
}

} // namespace quentier::synchronization::tests
