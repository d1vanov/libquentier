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

#include <synchronization/AccountSynchronizer.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/threading/Factory.h>

#include <synchronization/tests/mocks/MockIAuthenticationInfoProvider.h>
#include <synchronization/tests/mocks/MockIDownloader.h>
#include <synchronization/tests/mocks/MockISender.h>

namespace quentier::synchronization::tests {

using testing::StrictMock;

class AccountSynchronizerTest : public testing::Test
{
protected:
    const Account m_account = Account{
        QStringLiteral("Full Name"),
        Account::Type::Evernote,
        qevercloud::UserID{42},
        Account::EvernoteAccountType::Free,
        QStringLiteral("www.evernote.com"),
        QStringLiteral("shard id")};

    const std::shared_ptr<mocks::MockIDownloader> m_mockDownloader =
        std::make_shared<StrictMock<mocks::MockIDownloader>>();

    const std::shared_ptr<mocks::MockISender> m_mockSender =
        std::make_shared<StrictMock<mocks::MockISender>>();

    const std::shared_ptr<mocks::MockIAuthenticationInfoProvider>
        m_mockAuthenticationInfoProvider = std::make_shared<
            StrictMock<mocks::MockIAuthenticationInfoProvider>>();

    const threading::QThreadPoolPtr m_threadPool =
        threading::globalThreadPool();
};

TEST_F(AccountSynchronizerTest, Ctor)
{
    EXPECT_NO_THROW(
        const auto accountSynchronizer = std::make_shared<AccountSynchronizer>(
            m_account, m_mockDownloader, m_mockSender,
            m_mockAuthenticationInfoProvider, m_threadPool));
}

TEST_F(AccountSynchronizerTest, CtorEmptyAccount)
{
    EXPECT_THROW(
        const auto accountSynchronizer = std::make_shared<AccountSynchronizer>(
            Account{}, m_mockDownloader, m_mockSender,
            m_mockAuthenticationInfoProvider, m_threadPool),
        InvalidArgument);
}

TEST_F(AccountSynchronizerTest, CtorNullDownloader)
{
    EXPECT_THROW(
        const auto accountSynchronizer = std::make_shared<AccountSynchronizer>(
            m_account, nullptr, m_mockSender, m_mockAuthenticationInfoProvider,
            m_threadPool),
        InvalidArgument);
}

TEST_F(AccountSynchronizerTest, CtorNullSender)
{
    EXPECT_THROW(
        const auto accountSynchronizer = std::make_shared<AccountSynchronizer>(
            m_account, m_mockDownloader, nullptr,
            m_mockAuthenticationInfoProvider, m_threadPool),
        InvalidArgument);
}

TEST_F(AccountSynchronizerTest, CtorNullAuthenticationInfoProvider)
{
    EXPECT_THROW(
        const auto accountSynchronizer = std::make_shared<AccountSynchronizer>(
            m_account, m_mockDownloader, m_mockSender, nullptr, m_threadPool),
        InvalidArgument);
}

TEST_F(AccountSynchronizerTest, CtorNullThreadPool)
{
    EXPECT_NO_THROW(
        const auto accountSynchronizer = std::make_shared<AccountSynchronizer>(
            m_account, m_mockDownloader, m_mockSender,
            m_mockAuthenticationInfoProvider, nullptr));
}

} // namespace quentier::synchronization::tests
