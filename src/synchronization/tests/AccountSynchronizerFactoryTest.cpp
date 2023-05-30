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

#include <synchronization/AccountSynchronizerFactory.h>

#include <local_storage/sql/Notifier.h>
#include <synchronization/tests/mocks/MockIAuthenticationInfoProvider.h>
#include <synchronization/types/SyncOptionsBuilder.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/local_storage/tests/mocks/MockILocalStorage.h>
#include <quentier/synchronization/tests/mocks/MockISyncConflictResolver.h>
#include <quentier/synchronization/tests/mocks/MockISyncStateStorage.h>
#include <quentier/utility/FileSystem.h>

#include <QTemporaryDir>

#include <gtest/gtest.h>

// clazy:excludeall=non-pod-global-static
// clazy:excludeall=returning-void-expression

namespace quentier::synchronization::tests {

using testing::Return;
using testing::StrictMock;

class AccountSynchronizerFactoryTest : public testing::Test
{
protected:
    void TearDown() override
    {
        QDir dir{m_temporaryDir.path()};
        const auto entries =
            dir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);

        for (const auto & entry: qAsConst(entries)) {
            if (entry.isDir()) {
                ASSERT_TRUE(removeDir(entry.absoluteFilePath()));
            }
            else {
                ASSERT_TRUE(removeFile(entry.absoluteFilePath()));
            }
        }
    }

protected:
    const Account m_account = Account{
        QStringLiteral("Full Name"),
        Account::Type::Evernote,
        qevercloud::UserID{42},
        Account::EvernoteAccountType::Free,
        QStringLiteral("www.evernote.com"),
        QStringLiteral("shard id")};

    const std::shared_ptr<mocks::MockISyncStateStorage> m_mockSyncStateStorage =
        std::make_shared<StrictMock<mocks::MockISyncStateStorage>>();

    const std::shared_ptr<mocks::MockIAuthenticationInfoProvider>
        m_mockAuthenticationInfoProvider = std::make_shared<
            StrictMock<mocks::MockIAuthenticationInfoProvider>>();

    const std::shared_ptr<mocks::MockISyncConflictResolver>
        m_mockSyncConflictResolver =
            std::make_shared<StrictMock<mocks::MockISyncConflictResolver>>();

    const std::shared_ptr<local_storage::tests::mocks::MockILocalStorage>
        m_mockLocalStorage = std::make_shared<
            StrictMock<local_storage::tests::mocks::MockILocalStorage>>();

    QTemporaryDir m_temporaryDir;
};

TEST_F(AccountSynchronizerFactoryTest, Ctor)
{
    EXPECT_NO_THROW(
        const auto factory = std::make_shared<AccountSynchronizerFactory>(
            m_mockSyncStateStorage, m_mockAuthenticationInfoProvider,
            QDir{m_temporaryDir.path()}));
}

TEST_F(AccountSynchronizerFactoryTest, CtorNullSyncStateStorage)
{
    EXPECT_THROW(
        const auto factory = std::make_shared<AccountSynchronizerFactory>(
            nullptr, m_mockAuthenticationInfoProvider,
            QDir{m_temporaryDir.path()}),
        InvalidArgument);
}

TEST_F(AccountSynchronizerFactoryTest, CtorNullAuthenticationInfoProvider)
{
    EXPECT_THROW(
        const auto factory = std::make_shared<AccountSynchronizerFactory>(
            m_mockSyncStateStorage, nullptr, QDir{m_temporaryDir.path()}),
        InvalidArgument);
}

TEST_F(AccountSynchronizerFactoryTest, CreateAccountSynchronizerForEmptyAccount)
{
    const auto factory = std::make_shared<AccountSynchronizerFactory>(
        m_mockSyncStateStorage, m_mockAuthenticationInfoProvider,
        QDir{m_temporaryDir.path()});

    EXPECT_THROW(
        auto future = factory->createAccountSynchronizer(
            Account{}, m_mockSyncConflictResolver, m_mockLocalStorage,
            SyncOptionsBuilder{}.build()),
        InvalidArgument);
}

TEST_F(AccountSynchronizerFactoryTest, CreateAccountSynchronizerForLocalAccount)
{
    const auto factory = std::make_shared<AccountSynchronizerFactory>(
        m_mockSyncStateStorage, m_mockAuthenticationInfoProvider,
        QDir{m_temporaryDir.path()});

    const Account account =
        Account{QStringLiteral("Full Name"), Account::Type::Local};

    EXPECT_THROW(
        auto synchronizer = factory->createAccountSynchronizer(
            account, m_mockSyncConflictResolver, m_mockLocalStorage,
            SyncOptionsBuilder{}.build()),
        InvalidArgument);
}

TEST_F(AccountSynchronizerFactoryTest, CreateAccountSynchronizer)
{
    const auto factory = std::make_shared<AccountSynchronizerFactory>(
        m_mockSyncStateStorage, m_mockAuthenticationInfoProvider,
        QDir{m_temporaryDir.path()});

    local_storage::sql::Notifier notifier;
    EXPECT_CALL(*m_mockLocalStorage, notifier)
        .WillRepeatedly(Return(&notifier));

    auto synchronizer = factory->createAccountSynchronizer(
        m_account, m_mockSyncConflictResolver, m_mockLocalStorage,
        SyncOptionsBuilder{}.build());
    EXPECT_TRUE(synchronizer);
}

} // namespace quentier::synchronization::tests
