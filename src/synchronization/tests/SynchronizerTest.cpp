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

#include <synchronization/Synchronizer.h>

#include <synchronization/tests/mocks/MockIAccountSynchronizer.h>
#include <synchronization/tests/mocks/MockIAccountSynchronizerFactory.h>
#include <synchronization/tests/mocks/MockIAuthenticationInfoProvider.h>
#include <synchronization/types/AuthenticationInfo.h>
#include <synchronization/types/SyncOptionsBuilder.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/local_storage/tests/mocks/MockILocalStorage.h>
#include <quentier/synchronization/tests/mocks/MockISyncConflictResolver.h>
#include <quentier/threading/Future.h>
#include <quentier/utility/cancelers/ManualCanceler.h>

#include <gtest/gtest.h>

// clazy:excludeall=non-pod-global-static
// clazy:excludeall=returning-void-expression

namespace quentier::synchronization::tests {

using testing::Return;
using testing::StrictMock;

class SynchronizerTest : public testing::Test
{
protected:
    const Account m_account = Account{
        QStringLiteral("Full Name"),
        Account::Type::Evernote,
        qevercloud::UserID{42},
        Account::EvernoteAccountType::Free,
        QStringLiteral("www.evernote.com"),
        QStringLiteral("shard id")};

    const std::shared_ptr<mocks::MockIAccountSynchronizer>
        m_mockAccountSynchronizer = std::make_shared<
            StrictMock<mocks::MockIAccountSynchronizer>>();

    const std::shared_ptr<mocks::MockIAccountSynchronizerFactory>
        m_mockAccountSynchronizerFactory = std::make_shared<
            StrictMock<mocks::MockIAccountSynchronizerFactory>>();

    const std::shared_ptr<mocks::MockIAuthenticationInfoProvider>
        m_mockAuthenticationInfoProvider = std::make_shared<
            StrictMock<mocks::MockIAuthenticationInfoProvider>>();

    const std::shared_ptr<mocks::MockISyncConflictResolver>
        m_mockSyncConflictResolver = std::make_shared<
            StrictMock<mocks::MockISyncConflictResolver>>();

    const std::shared_ptr<local_storage::tests::mocks::MockILocalStorage>
        m_mockLocalStorage = std::make_shared<
            StrictMock<local_storage::tests::mocks::MockILocalStorage>>();

    const utility::cancelers::ManualCancelerPtr m_canceler =
        std::make_shared<utility::cancelers::ManualCanceler>();
};

TEST_F(SynchronizerTest, Ctor)
{
    EXPECT_NO_THROW(
        const auto synchronizer = std::make_shared<Synchronizer>(
            m_mockAccountSynchronizerFactory,
            m_mockAuthenticationInfoProvider));
}

TEST_F(SynchronizerTest, CtorNullAccountSynchronizerFactory)
{
    EXPECT_THROW(
        const auto synchronizer = std::make_shared<Synchronizer>(
            nullptr, m_mockAuthenticationInfoProvider),
        InvalidArgument);
}

TEST_F(SynchronizerTest, CtorNullAuthenticationInfoProvider)
{
    EXPECT_THROW(
        const auto synchronizer = std::make_shared<Synchronizer>(
            m_mockAccountSynchronizerFactory, nullptr),
        InvalidArgument);
}

TEST_F(SynchronizerTest, AuthenticateNewAccount)
{
    const auto synchronizer = std::make_shared<Synchronizer>(
        m_mockAccountSynchronizerFactory,
        m_mockAuthenticationInfoProvider);

    const auto authenticationInfo = std::make_shared<AuthenticationInfo>();

    EXPECT_CALL(*m_mockAuthenticationInfoProvider, authenticateNewAccount)
        .WillOnce(Return(threading::makeReadyFuture<IAuthenticationInfoPtr>(
            authenticationInfo)));

    auto future = synchronizer->authenticateNewAccount();
    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);
    EXPECT_EQ(future.result(), authenticationInfo);
}

TEST_F(SynchronizerTest, AuthenticateAccount)
{
    const auto synchronizer = std::make_shared<Synchronizer>(
        m_mockAccountSynchronizerFactory,
        m_mockAuthenticationInfoProvider);

    const auto authenticationInfo = std::make_shared<AuthenticationInfo>();

    EXPECT_CALL(
        *m_mockAuthenticationInfoProvider,
        authenticateAccount(
            m_account, IAuthenticationInfoProvider::Mode::Cache))
        .WillOnce(Return(threading::makeReadyFuture<IAuthenticationInfoPtr>(
            authenticationInfo)));

    auto future = synchronizer->authenticateAccount(m_account);
    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);
    EXPECT_EQ(future.result(), authenticationInfo);
}

TEST_F(SynchronizerTest, RevokeAuthentication)
{
    const auto synchronizer = std::make_shared<Synchronizer>(
        m_mockAccountSynchronizerFactory,
        m_mockAuthenticationInfoProvider);

    const qevercloud::UserID userId = 42;

    EXPECT_CALL(
        *m_mockAuthenticationInfoProvider,
        clearCaches(IAuthenticationInfoProvider::ClearCacheOptions{
            IAuthenticationInfoProvider::ClearCacheOption::User{userId}}));

    synchronizer->revokeAuthentication(userId);
}

} // namespace quentier::synchronization::tests
