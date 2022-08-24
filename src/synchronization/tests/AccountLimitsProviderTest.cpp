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

#include <synchronization/AccountLimitsProvider.h>
#include <synchronization/tests/mocks/qevercloud/services/MockIUserStore.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/threading/Future.h>
#include <quentier/utility/ApplicationSettings.h>

#include <qevercloud/RequestContext.h>
#include <qevercloud/types/builders/AccountLimitsBuilder.h>

#include <gtest/gtest.h>

// clazy:excludeall=non-pod-global-static
// clazy:excludeall=returning-void-expression

namespace quentier::synchronization::tests {

using testing::_;
using testing::Return;
using testing::StrictMock;

class AccountLimitsProviderTest : public testing::Test
{
protected:
    void SetUp() override
    {
        clearPersistence();
    }

    void TearDown() override
    {
        clearPersistence();
    };

private:
    void clearPersistence()
    {
        ApplicationSettings appSettings{
            m_account, QStringLiteral("SynchronizationPersistence")};

        appSettings.remove(QString::fromUtf8(""));
        appSettings.sync();
    }

protected:
    const Account m_account = Account{
        QStringLiteral("Full Name"),
        Account::Type::Evernote,
        qevercloud::UserID{42},
        Account::EvernoteAccountType::Free,
        QStringLiteral("https://www.evernote.com"),
        QStringLiteral("shard id")};

    const std::shared_ptr<mocks::qevercloud::MockIUserStore> m_mockUserStore =
        std::make_shared<StrictMock<mocks::qevercloud::MockIUserStore>>();

    const qevercloud::IRequestContextPtr m_ctx =
        qevercloud::newRequestContext();
};

TEST_F(AccountLimitsProviderTest, Ctor)
{
    EXPECT_NO_THROW(
        const auto accountLimitsProvider =
            std::make_shared<AccountLimitsProvider>(
                m_account, m_mockUserStore, m_ctx));
}

TEST_F(AccountLimitsProviderTest, CtorEmptyAccount)
{
    EXPECT_THROW(
        const auto accountLimitsProvider =
            std::make_shared<AccountLimitsProvider>(
                Account{}, m_mockUserStore, m_ctx),
        InvalidArgument);
}

TEST_F(AccountLimitsProviderTest, CtorNonEvernoteAccount)
{
    Account account{QStringLiteral("Full Name"), Account::Type::Local};

    EXPECT_THROW(
        const auto accountLimitsProvider =
            std::make_shared<AccountLimitsProvider>(
                std::move(account), m_mockUserStore, m_ctx),
        InvalidArgument);
}

TEST_F(AccountLimitsProviderTest, CtorNullUserStore)
{
    EXPECT_THROW(
        const auto accountLimitsProvider =
            std::make_shared<AccountLimitsProvider>(m_account, nullptr, m_ctx),
        InvalidArgument);
}

TEST_F(AccountLimitsProviderTest, CtorNullRequestContext)
{
    EXPECT_THROW(
        const auto accountLimitsProvider =
            std::make_shared<AccountLimitsProvider>(
                m_account, m_mockUserStore, nullptr),
        InvalidArgument);
}

TEST_F(AccountLimitsProviderTest, GetAccountLimits)
{
    const auto accountLimitsProvider = std::make_shared<AccountLimitsProvider>(
        m_account, m_mockUserStore, m_ctx);

    const auto accountLimits = qevercloud::AccountLimitsBuilder{}
                                   .setNoteTagCountMax(42)
                                   .setUploadLimit(200)
                                   .setUserTagCountMax(30)
                                   .setNoteSizeMax(2000)
                                   .setNoteResourceCountMax(30)
                                   .build();

    EXPECT_CALL(
        *m_mockUserStore,
        getAccountLimitsAsync(qevercloud::ServiceLevel::BASIC, _))
        .WillOnce(Return(threading::makeReadyFuture(accountLimits)));

    auto future =
        accountLimitsProvider->accountLimits(qevercloud::ServiceLevel::BASIC);

    ASSERT_TRUE(future.isFinished());

    ASSERT_EQ(future.resultCount(), 1);
    auto result = future.result();
    EXPECT_EQ(result, accountLimits);

    // The second call with the same argument should not trigger the call of
    // IUserStore as the result of the first call should be cached
    future =
        accountLimitsProvider->accountLimits(qevercloud::ServiceLevel::BASIC);

    ASSERT_TRUE(future.isFinished());

    ASSERT_EQ(future.resultCount(), 1);
    result = future.result();
    EXPECT_EQ(result, accountLimits);

    // The call with another argument value should trigger the call of
    // IUSerStore method
    EXPECT_CALL(
        *m_mockUserStore,
        getAccountLimitsAsync(qevercloud::ServiceLevel::PLUS, _))
        .WillOnce(Return(threading::makeReadyFuture(accountLimits)));

    future =
        accountLimitsProvider->accountLimits(qevercloud::ServiceLevel::PLUS);

    ASSERT_TRUE(future.isFinished());

    ASSERT_EQ(future.resultCount(), 1);
    result = future.result();
    EXPECT_EQ(result, accountLimits);
}

} // namespace quentier::synchronization::tests
