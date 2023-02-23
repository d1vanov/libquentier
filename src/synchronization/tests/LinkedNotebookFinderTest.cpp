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

#include <synchronization/LinkedNotebookFinder.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/local_storage/tests/mocks/MockILocalStorage.h>
#include <quentier/threading/Future.h>
#include <quentier/utility/UidGenerator.h>

#include <local_storage/sql/Notifier.h>

#include <qevercloud/types/builders/LinkedNotebookBuilder.h>
#include <qevercloud/types/builders/NotebookBuilder.h>

#include <QCoreApplication>

#include <gtest/gtest.h>

// clazy:excludeall=non-pod-global-static
// clazy:excludeall=returning-void-expression

namespace quentier::synchronization::tests {

using testing::Return;
using testing::StrictMock;

class LinkedNotebookFinderTest : public testing::Test
{
protected:
    const std::shared_ptr<local_storage::tests::mocks::MockILocalStorage>
        m_mockLocalStorage = std::make_shared<
            StrictMock<local_storage::tests::mocks::MockILocalStorage>>();
};

TEST_F(LinkedNotebookFinderTest, Ctor)
{
    EXPECT_NO_THROW(
        const auto linkedNotebookFinder =
            std::make_shared<LinkedNotebookFinder>(m_mockLocalStorage));
}

TEST_F(LinkedNotebookFinderTest, CtorNullLocalStorage)
{
    EXPECT_THROW(
        const auto linkedNotebookFinder =
            std::make_shared<LinkedNotebookFinder>(nullptr),
        InvalidArgument);
}

TEST_F(LinkedNotebookFinderTest, FindLinkedNotebookByGuid)
{
    const auto linkedNotebookFinder =
        std::make_shared<LinkedNotebookFinder>(m_mockLocalStorage);

    local_storage::sql::Notifier notifier;
    EXPECT_CALL(*m_mockLocalStorage, notifier).WillOnce(Return(&notifier));

    linkedNotebookFinder->init();

    const auto linkedNotebook = qevercloud::LinkedNotebookBuilder{}
                                    .setGuid(UidGenerator::Generate())
                                    .setUsername(QStringLiteral("username"))
                                    .build();

    EXPECT_CALL(
        *m_mockLocalStorage, findLinkedNotebookByGuid(*linkedNotebook.guid()))
        .WillOnce(
            Return(threading::makeReadyFuture<
                   std::optional<qevercloud::LinkedNotebook>>(linkedNotebook)));

    auto future =
        linkedNotebookFinder->findLinkedNotebookByGuid(*linkedNotebook.guid());

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    auto res = future.result();
    ASSERT_TRUE(res);
    EXPECT_EQ(*res, linkedNotebook);

    // The next call should not go to local storage but use cached value instead
    future =
        linkedNotebookFinder->findLinkedNotebookByGuid(*linkedNotebook.guid());

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    res = future.result();
    ASSERT_TRUE(res);
    EXPECT_EQ(*res, linkedNotebook);

    // If this linked notebook gets updated, it should be removed from the
    // cache so the next call would go to local storage again
    notifier.notifyLinkedNotebookPut(linkedNotebook);
    QCoreApplication::processEvents();

    EXPECT_CALL(
        *m_mockLocalStorage, findLinkedNotebookByGuid(*linkedNotebook.guid()))
        .WillOnce(
            Return(threading::makeReadyFuture<
                   std::optional<qevercloud::LinkedNotebook>>(linkedNotebook)));

    future =
        linkedNotebookFinder->findLinkedNotebookByGuid(*linkedNotebook.guid());

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    res = future.result();
    ASSERT_TRUE(res);
    EXPECT_EQ(*res, linkedNotebook);

    // The next call should not go to local storage but use cached value instead
    future =
        linkedNotebookFinder->findLinkedNotebookByGuid(*linkedNotebook.guid());

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    res = future.result();
    ASSERT_TRUE(res);
    EXPECT_EQ(*res, linkedNotebook);

    // If this linked notebook gets expunged, it should be removed from the
    // cache so the next call would go to local storage again
    notifier.notifyLinkedNotebookExpunged(*linkedNotebook.guid());
    QCoreApplication::processEvents();

    EXPECT_CALL(
        *m_mockLocalStorage, findLinkedNotebookByGuid(*linkedNotebook.guid()))
        .WillOnce(
            Return(threading::makeReadyFuture<
                   std::optional<qevercloud::LinkedNotebook>>(std::nullopt)));

    future =
        linkedNotebookFinder->findLinkedNotebookByGuid(*linkedNotebook.guid());

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    res = future.result();
    EXPECT_FALSE(res);

    // The next call should not go to local storage but use cached value instead
    future =
        linkedNotebookFinder->findLinkedNotebookByGuid(*linkedNotebook.guid());

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    res = future.result();
    EXPECT_FALSE(res);
}

TEST_F(LinkedNotebookFinderTest, FindNoLinkedNotebookByGuid)
{
    const auto linkedNotebookFinder =
        std::make_shared<LinkedNotebookFinder>(m_mockLocalStorage);

    local_storage::sql::Notifier notifier;
    EXPECT_CALL(*m_mockLocalStorage, notifier).WillOnce(Return(&notifier));

    linkedNotebookFinder->init();

    const auto guid = UidGenerator::Generate();

    EXPECT_CALL(*m_mockLocalStorage, findLinkedNotebookByGuid(guid))
        .WillOnce(
            Return(threading::makeReadyFuture<
                   std::optional<qevercloud::LinkedNotebook>>(std::nullopt)));

    auto future = linkedNotebookFinder->findLinkedNotebookByGuid(guid);

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    auto res = future.result();
    EXPECT_FALSE(res);

    // The next call should not go to local storage but use cached value instead
    future = linkedNotebookFinder->findLinkedNotebookByGuid(guid);

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    res = future.result();
    EXPECT_FALSE(res);

    // If this linked notebook gets put to local storage, its not found entry
    // should be removed from the cache so the next call would go to local
    // storage again
    const auto linkedNotebook = qevercloud::LinkedNotebookBuilder{}
                                    .setGuid(guid)
                                    .setUsername(QStringLiteral("username"))
                                    .build();
    notifier.notifyLinkedNotebookPut(linkedNotebook);
    QCoreApplication::processEvents();

    EXPECT_CALL(
        *m_mockLocalStorage, findLinkedNotebookByGuid(*linkedNotebook.guid()))
        .WillOnce(
            Return(threading::makeReadyFuture<
                   std::optional<qevercloud::LinkedNotebook>>(linkedNotebook)));

    future =
        linkedNotebookFinder->findLinkedNotebookByGuid(*linkedNotebook.guid());

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    res = future.result();
    ASSERT_TRUE(res);
    EXPECT_EQ(*res, linkedNotebook);

    // The next call should not go to local storage but use cached value instead
    future =
        linkedNotebookFinder->findLinkedNotebookByGuid(*linkedNotebook.guid());

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    res = future.result();
    ASSERT_TRUE(res);
    EXPECT_EQ(*res, linkedNotebook);

    // If this linked notebook gets expunged, it should be removed from the
    // cache so the next call would go to local storage again
    notifier.notifyLinkedNotebookExpunged(*linkedNotebook.guid());
    QCoreApplication::processEvents();

    EXPECT_CALL(
        *m_mockLocalStorage, findLinkedNotebookByGuid(*linkedNotebook.guid()))
        .WillOnce(
            Return(threading::makeReadyFuture<
                   std::optional<qevercloud::LinkedNotebook>>(std::nullopt)));

    future =
        linkedNotebookFinder->findLinkedNotebookByGuid(*linkedNotebook.guid());

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    res = future.result();
    EXPECT_FALSE(res);

    // The next call should not go to local storage but use cached value instead
    future =
        linkedNotebookFinder->findLinkedNotebookByGuid(*linkedNotebook.guid());

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    res = future.result();
    EXPECT_FALSE(res);
}

TEST_F(
    LinkedNotebookFinderTest,
    FindNoLinkedNotebookByNotebookLocalIdForUserOwnNotebook)
{
    const auto linkedNotebookFinder =
        std::make_shared<LinkedNotebookFinder>(m_mockLocalStorage);

    local_storage::sql::Notifier notifier;
    EXPECT_CALL(*m_mockLocalStorage, notifier).WillOnce(Return(&notifier));

    linkedNotebookFinder->init();

    const auto notebook = qevercloud::NotebookBuilder{}
                              .setGuid(UidGenerator::Generate())
                              .setLocalId(UidGenerator::Generate())
                              .setName(QStringLiteral("Notebook"))
                              .build();

    EXPECT_CALL(*m_mockLocalStorage, findNotebookByLocalId)
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::Notebook>>(
                notebook)));

    auto future = linkedNotebookFinder->findLinkedNotebookByNotebookLocalId(
        notebook.localId());

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    auto res = future.result();
    EXPECT_FALSE(res);

    // The next call should not go to local storage but use cached value instead
    future = linkedNotebookFinder->findLinkedNotebookByNotebookLocalId(
        notebook.localId());

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    res = future.result();
    EXPECT_FALSE(res);

    // If this notebook gets updated, it should be removed from the
    // cache so the next call would go to local storage again
    notifier.notifyNotebookPut(notebook);
    QCoreApplication::processEvents();

    EXPECT_CALL(*m_mockLocalStorage, findNotebookByLocalId)
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::Notebook>>(
                notebook)));

    future = linkedNotebookFinder->findLinkedNotebookByNotebookLocalId(
        notebook.localId());

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    res = future.result();
    EXPECT_FALSE(res);

    // The next call should not go to local storage but use cached value instead
    future = linkedNotebookFinder->findLinkedNotebookByNotebookLocalId(
        notebook.localId());

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    res = future.result();
    EXPECT_FALSE(res);

    // If this notebook gets expunged, it should be removed from the
    // cache so the next call would go to local storage again
    notifier.notifyNotebookExpunged(notebook.localId());
    QCoreApplication::processEvents();

    EXPECT_CALL(*m_mockLocalStorage, findNotebookByLocalId)
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::Notebook>>(
                std::nullopt)));

    future = linkedNotebookFinder->findLinkedNotebookByNotebookLocalId(
        notebook.localId());

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    res = future.result();
    EXPECT_FALSE(res);

    // The next call should not go to local storage but use cached value instead
    future = linkedNotebookFinder->findLinkedNotebookByNotebookLocalId(
        notebook.localId());

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    res = future.result();
    EXPECT_FALSE(res);
}

TEST_F(
    LinkedNotebookFinderTest,
    FindNoLinkedNotebookByNotebookLocalIdForNonexistentNotebook)
{
    const auto linkedNotebookFinder =
        std::make_shared<LinkedNotebookFinder>(m_mockLocalStorage);

    local_storage::sql::Notifier notifier;
    EXPECT_CALL(*m_mockLocalStorage, notifier).WillOnce(Return(&notifier));

    linkedNotebookFinder->init();

    const auto localId = UidGenerator::Generate();

    EXPECT_CALL(*m_mockLocalStorage, findNotebookByLocalId)
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::Notebook>>(
                std::nullopt)));

    auto future =
        linkedNotebookFinder->findLinkedNotebookByNotebookLocalId(localId);

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    auto res = future.result();
    EXPECT_FALSE(res);

    // The next call should not go to local storage but use cached value instead
    future = linkedNotebookFinder->findLinkedNotebookByNotebookLocalId(localId);

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    res = future.result();
    EXPECT_FALSE(res);

    // If this notebook gets put to local storage, its not found entry should be
    // removed from the cache so the next call would go to local storage again
    const auto notebook = qevercloud::NotebookBuilder{}
                              .setGuid(UidGenerator::Generate())
                              .setLocalId(localId)
                              .setName(QStringLiteral("Notebook"))
                              .build();

    notifier.notifyNotebookPut(notebook);
    QCoreApplication::processEvents();

    EXPECT_CALL(*m_mockLocalStorage, findNotebookByLocalId)
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::Notebook>>(
                notebook)));

    future = linkedNotebookFinder->findLinkedNotebookByNotebookLocalId(localId);

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    res = future.result();
    EXPECT_FALSE(res);

    // The next call should not go to local storage but use cached value instead
    future = linkedNotebookFinder->findLinkedNotebookByNotebookLocalId(localId);

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    res = future.result();
    EXPECT_FALSE(res);

    // If this notebook gets expunged, it should be removed from the
    // cache so the next call would go to local storage again
    notifier.notifyNotebookExpunged(notebook.localId());
    QCoreApplication::processEvents();

    EXPECT_CALL(*m_mockLocalStorage, findNotebookByLocalId)
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::Notebook>>(
                std::nullopt)));

    future = linkedNotebookFinder->findLinkedNotebookByNotebookLocalId(localId);

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    res = future.result();
    EXPECT_FALSE(res);

    // The next call should not go to local storage but use cached value instead
    future = linkedNotebookFinder->findLinkedNotebookByNotebookLocalId(localId);

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    res = future.result();
    EXPECT_FALSE(res);
}

TEST_F(LinkedNotebookFinderTest, FindLinkedNotebookByNotebookLocalId)
{
    const auto linkedNotebookFinder =
        std::make_shared<LinkedNotebookFinder>(m_mockLocalStorage);

    local_storage::sql::Notifier notifier;
    EXPECT_CALL(*m_mockLocalStorage, notifier).WillOnce(Return(&notifier));

    linkedNotebookFinder->init();

    const auto linkedNotebook = qevercloud::LinkedNotebookBuilder{}
                                    .setGuid(UidGenerator::Generate())
                                    .setUsername(QStringLiteral("username"))
                                    .build();

    const auto notebook = qevercloud::NotebookBuilder{}
                              .setGuid(UidGenerator::Generate())
                              .setLocalId(UidGenerator::Generate())
                              .setName(QStringLiteral("Notebook"))
                              .setLinkedNotebookGuid(linkedNotebook.guid())
                              .build();

    EXPECT_CALL(*m_mockLocalStorage, findNotebookByLocalId)
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::Notebook>>(
                notebook)));

    EXPECT_CALL(
        *m_mockLocalStorage, findLinkedNotebookByGuid(*linkedNotebook.guid()))
        .WillOnce(
            Return(threading::makeReadyFuture<
                   std::optional<qevercloud::LinkedNotebook>>(linkedNotebook)));

    auto future = linkedNotebookFinder->findLinkedNotebookByNotebookLocalId(
        notebook.localId());

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    auto res = future.result();
    ASSERT_TRUE(res);
    EXPECT_EQ(*res, linkedNotebook);

    // The next call should not go to local storage but use cached value instead
    future = linkedNotebookFinder->findLinkedNotebookByNotebookLocalId(
        notebook.localId());

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    res = future.result();
    ASSERT_TRUE(res);
    EXPECT_EQ(*res, linkedNotebook);

    // If this notebook gets updated, it should be removed from the
    // cache so the next call would go to local storage again
    notifier.notifyNotebookPut(notebook);
    QCoreApplication::processEvents();

    EXPECT_CALL(*m_mockLocalStorage, findNotebookByLocalId)
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::Notebook>>(
                notebook)));

    future = linkedNotebookFinder->findLinkedNotebookByNotebookLocalId(
        notebook.localId());

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    res = future.result();
    ASSERT_TRUE(res);
    EXPECT_EQ(*res, linkedNotebook);

    // The next call should not go to local storage but use cached value instead
    future = linkedNotebookFinder->findLinkedNotebookByNotebookLocalId(
        notebook.localId());

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    res = future.result();
    ASSERT_TRUE(res);
    EXPECT_EQ(*res, linkedNotebook);

    // If this notebook gets expunged, it should be removed from the
    // cache so the next call would go to local storage again
    notifier.notifyNotebookExpunged(notebook.localId());
    QCoreApplication::processEvents();

    EXPECT_CALL(*m_mockLocalStorage, findNotebookByLocalId)
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::Notebook>>(
                std::nullopt)));

    future = linkedNotebookFinder->findLinkedNotebookByNotebookLocalId(
        notebook.localId());

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    res = future.result();
    EXPECT_FALSE(res);

    // The next call should not go to local storage but use cached value instead
    future = linkedNotebookFinder->findLinkedNotebookByNotebookLocalId(
        notebook.localId());

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    res = future.result();
    EXPECT_FALSE(res);

    // Now imitate the fact that this notebook was put to local storage again
    // and ensure that the expunging of the linked notebook would be processed
    // as needed
    notifier.notifyNotebookPut(notebook);
    QCoreApplication::processEvents();

    EXPECT_CALL(*m_mockLocalStorage, findNotebookByLocalId)
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::Notebook>>(
                notebook)));

    future = linkedNotebookFinder->findLinkedNotebookByNotebookLocalId(
        notebook.localId());

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    res = future.result();
    ASSERT_TRUE(res);
    EXPECT_EQ(*res, linkedNotebook);

    // If this linked notebook gets expunged, it should be removed from the
    // cache so the next call would go to local storage again
    notifier.notifyLinkedNotebookExpunged(*notebook.linkedNotebookGuid());
    QCoreApplication::processEvents();

    EXPECT_CALL(*m_mockLocalStorage, findNotebookByLocalId)
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::Notebook>>(
                notebook)));

    EXPECT_CALL(
        *m_mockLocalStorage, findLinkedNotebookByGuid(*linkedNotebook.guid()))
        .WillOnce(
            Return(threading::makeReadyFuture<
                   std::optional<qevercloud::LinkedNotebook>>(std::nullopt)));

    future = linkedNotebookFinder->findLinkedNotebookByNotebookLocalId(
        notebook.localId());

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    res = future.result();
    EXPECT_FALSE(res);

    // The next call should not go to local storage but use cached value instead
    future = linkedNotebookFinder->findLinkedNotebookByNotebookLocalId(
        notebook.localId());

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    res = future.result();
    EXPECT_FALSE(res);
}

} // namespace quentier::synchronization::tests
