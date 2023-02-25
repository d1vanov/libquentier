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

#include <synchronization/NoteThumbnailDownloaderFactory.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/threading/Future.h>

#include <synchronization/tests/mocks/MockIAuthenticationInfoProvider.h>
#include <synchronization/tests/mocks/MockILinkedNotebookFinder.h>

#include <qevercloud/RequestContext.h>
#include <qevercloud/types/builders/LinkedNotebookBuilder.h>

#include <gtest/gtest.h>

// clazy:excludeall=non-pod-global-static
// clazy:excludeall=returning-void-expression

namespace quentier::synchronization::tests {

using testing::Return;
using testing::StrictMock;

class NoteThumbnailDownloaderFactoryTest : public testing::Test
{
protected:
    const Account m_account = Account{
        QStringLiteral("Full Name"),
        Account::Type::Evernote,
        qevercloud::UserID{42},
        Account::EvernoteAccountType::Free,
        QStringLiteral("www.evernote.com"),
        QStringLiteral("shard id")};

    const std::shared_ptr<mocks::MockILinkedNotebookFinder>
        m_mockLinkedNotebookFinder =
            std::make_shared<StrictMock<mocks::MockILinkedNotebookFinder>>();

    const std::shared_ptr<mocks::MockIAuthenticationInfoProvider>
        m_mockAuthenticationInfoProvider = std::make_shared<
            StrictMock<mocks::MockIAuthenticationInfoProvider>>();
};

TEST_F(NoteThumbnailDownloaderFactoryTest, Ctor)
{
    EXPECT_NO_THROW(
        const auto noteThumbnailDownloaderFactory =
            std::make_shared<NoteThumbnailDownloaderFactory>(
                m_account, m_mockAuthenticationInfoProvider,
                m_mockLinkedNotebookFinder));
}

TEST_F(NoteThumbnailDownloaderFactoryTest, CtorEmptyAccount)
{
    EXPECT_THROW(
        const auto noteThumbnailDownloaderFactory =
            std::make_shared<NoteThumbnailDownloaderFactory>(
                Account{}, m_mockAuthenticationInfoProvider,
                m_mockLinkedNotebookFinder),
        InvalidArgument);
}

TEST_F(NoteThumbnailDownloaderFactoryTest, CtorNullAuthenticationInfoProvider)
{
    EXPECT_THROW(
        const auto noteThumbnailDownloaderFactory =
            std::make_shared<NoteThumbnailDownloaderFactory>(
                m_account, nullptr, m_mockLinkedNotebookFinder),
        InvalidArgument);
}

TEST_F(NoteThumbnailDownloaderFactoryTest, CtorNullLinkedNotebookFinder)
{
    EXPECT_THROW(
        const auto noteThumbnailDownloaderFactory =
            std::make_shared<NoteThumbnailDownloaderFactory>(
                Account{}, m_mockAuthenticationInfoProvider, nullptr),
        InvalidArgument);
}

} // namespace quentier::synchronization::tests
