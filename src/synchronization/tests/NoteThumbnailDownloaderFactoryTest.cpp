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
#include <quentier/exception/RuntimeError.h>
#include <quentier/threading/Future.h>
#include <quentier/utility/UidGenerator.h>

#include <synchronization/tests/mocks/MockIAuthenticationInfoProvider.h>
#include <synchronization/tests/mocks/MockILinkedNotebookFinder.h>
#include <synchronization/types/AuthenticationInfo.h>

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

TEST_F(
    NoteThumbnailDownloaderFactoryTest,
    NoNoteThumbnailDownloaderIfFindingLinkedNotebookFails)
{
    const auto noteThumbnailDownloaderFactory =
        std::make_shared<NoteThumbnailDownloaderFactory>(
            m_account, m_mockAuthenticationInfoProvider,
            m_mockLinkedNotebookFinder);

    const auto notebookLocalId = UidGenerator::Generate();

    EXPECT_CALL(
        *m_mockLinkedNotebookFinder,
        findLinkedNotebookByNotebookLocalId(notebookLocalId))
        .WillOnce(Return(threading::makeExceptionalFuture<
                         std::optional<qevercloud::LinkedNotebook>>(
            RuntimeError{ErrorString{QStringLiteral("some error")}})));

    auto future = noteThumbnailDownloaderFactory->createNoteThumbnailDownloader(
        notebookLocalId);

    ASSERT_TRUE(future.isFinished());
    EXPECT_THROW(future.result(), RuntimeError);
}

TEST_F(
    NoteThumbnailDownloaderFactoryTest,
    NoUserOwnNoteThumbnailDownloaderIfFindingAuthenticationInfoFails)
{
    const auto noteThumbnailDownloaderFactory =
        std::make_shared<NoteThumbnailDownloaderFactory>(
            m_account, m_mockAuthenticationInfoProvider,
            m_mockLinkedNotebookFinder);

    const auto notebookLocalId = UidGenerator::Generate();

    EXPECT_CALL(
        *m_mockLinkedNotebookFinder,
        findLinkedNotebookByNotebookLocalId(notebookLocalId))
        .WillOnce(
            Return(threading::makeReadyFuture<
                   std::optional<qevercloud::LinkedNotebook>>(std::nullopt)));

    EXPECT_CALL(
        *m_mockAuthenticationInfoProvider,
        authenticateAccount(
            m_account, IAuthenticationInfoProvider::Mode::Cache))
        .WillOnce(
            Return(threading::makeExceptionalFuture<IAuthenticationInfoPtr>(
                RuntimeError{ErrorString{QStringLiteral("some error")}})));

    auto future = noteThumbnailDownloaderFactory->createNoteThumbnailDownloader(
        notebookLocalId);

    ASSERT_TRUE(future.isFinished());
    EXPECT_THROW(future.result(), RuntimeError);
}

TEST_F(NoteThumbnailDownloaderFactoryTest, UserOwnNoteThumbnailDownloader)
{
    const auto noteThumbnailDownloaderFactory =
        std::make_shared<NoteThumbnailDownloaderFactory>(
            m_account, m_mockAuthenticationInfoProvider,
            m_mockLinkedNotebookFinder);

    const auto notebookLocalId = UidGenerator::Generate();

    EXPECT_CALL(
        *m_mockLinkedNotebookFinder,
        findLinkedNotebookByNotebookLocalId(notebookLocalId))
        .WillOnce(
            Return(threading::makeReadyFuture<
                   std::optional<qevercloud::LinkedNotebook>>(std::nullopt)));

    EXPECT_CALL(
        *m_mockAuthenticationInfoProvider,
        authenticateAccount(
            m_account, IAuthenticationInfoProvider::Mode::Cache))
        .WillOnce(Return(threading::makeReadyFuture<IAuthenticationInfoPtr>(
            std::make_shared<AuthenticationInfo>())));

    auto future = noteThumbnailDownloaderFactory->createNoteThumbnailDownloader(
        notebookLocalId);

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    auto result = future.result();
    EXPECT_TRUE(result);
}

TEST_F(
    NoteThumbnailDownloaderFactoryTest,
    NoLinkedNotebookThumbnailDownloaderIfFindingAuthenticationInfoFails)
{
    const auto noteThumbnailDownloaderFactory =
        std::make_shared<NoteThumbnailDownloaderFactory>(
            m_account, m_mockAuthenticationInfoProvider,
            m_mockLinkedNotebookFinder);

    const auto notebookLocalId = UidGenerator::Generate();
    const qevercloud::Guid linkedNotebookGuid = UidGenerator::Generate();

    const auto linkedNotebook = qevercloud::LinkedNotebookBuilder{}
                                    .setGuid(linkedNotebookGuid)
                                    .setUsername(QStringLiteral("username"))
                                    .setUpdateSequenceNum(43)
                                    .build();

    EXPECT_CALL(
        *m_mockLinkedNotebookFinder,
        findLinkedNotebookByNotebookLocalId(notebookLocalId))
        .WillOnce(
            Return(threading::makeReadyFuture<
                   std::optional<qevercloud::LinkedNotebook>>(linkedNotebook)));

    EXPECT_CALL(
        *m_mockAuthenticationInfoProvider,
        authenticateToLinkedNotebook(
            m_account, linkedNotebook,
            IAuthenticationInfoProvider::Mode::Cache))
        .WillOnce(
            Return(threading::makeExceptionalFuture<IAuthenticationInfoPtr>(
                RuntimeError{ErrorString{QStringLiteral("some error")}})));

    auto future = noteThumbnailDownloaderFactory->createNoteThumbnailDownloader(
        notebookLocalId);

    ASSERT_TRUE(future.isFinished());
    EXPECT_THROW(future.result(), RuntimeError);
}

TEST_F(NoteThumbnailDownloaderFactoryTest, LinkedNotebookThumbnailDownloader)
{
    const auto noteThumbnailDownloaderFactory =
        std::make_shared<NoteThumbnailDownloaderFactory>(
            m_account, m_mockAuthenticationInfoProvider,
            m_mockLinkedNotebookFinder);

    const auto notebookLocalId = UidGenerator::Generate();
    const qevercloud::Guid linkedNotebookGuid = UidGenerator::Generate();

    const auto linkedNotebook = qevercloud::LinkedNotebookBuilder{}
                                    .setGuid(linkedNotebookGuid)
                                    .setUsername(QStringLiteral("username"))
                                    .setUpdateSequenceNum(43)
                                    .build();

    EXPECT_CALL(
        *m_mockLinkedNotebookFinder,
        findLinkedNotebookByNotebookLocalId(notebookLocalId))
        .WillOnce(
            Return(threading::makeReadyFuture<
                   std::optional<qevercloud::LinkedNotebook>>(linkedNotebook)));

    EXPECT_CALL(
        *m_mockAuthenticationInfoProvider,
        authenticateToLinkedNotebook(
            m_account, linkedNotebook,
            IAuthenticationInfoProvider::Mode::Cache))
        .WillOnce(Return(threading::makeReadyFuture<IAuthenticationInfoPtr>(
            std::make_shared<AuthenticationInfo>())));

    auto future = noteThumbnailDownloaderFactory->createNoteThumbnailDownloader(
        notebookLocalId);

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    auto result = future.result();
    EXPECT_TRUE(result);
}

} // namespace quentier::synchronization::tests
