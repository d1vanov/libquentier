/*
 * Copyright 2021 Dmitry Ivanov
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

#include <local_storage/sql/LocalStorage.h>
#include <local_storage/sql/Notifier.h>

#include <local_storage/sql/tests/mocks/MockILinkedNotebooksHandler.h>
#include <local_storage/sql/tests/mocks/MockINotebooksHandler.h>
#include <local_storage/sql/tests/mocks/MockINotesHandler.h>
#include <local_storage/sql/tests/mocks/MockIResourcesHandler.h>
#include <local_storage/sql/tests/mocks/MockISavedSearchesHandler.h>
#include <local_storage/sql/tests/mocks/MockISynchronizationInfoHandler.h>
#include <local_storage/sql/tests/mocks/MockITagsHandler.h>
#include <local_storage/sql/tests/mocks/MockIUsersHandler.h>
#include <local_storage/sql/tests/mocks/MockIVersionHandler.h>

#include <utility/Threading.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/utility/UidGenerator.h>

#include <gtest/gtest.h>

// clazy:excludeall=returning-void-expression

namespace quentier::local_storage::sql::tests {

using testing::Return;
using testing::StrictMock;

class LocalStorageTest : public testing::Test
{
protected:
    [[nodiscard]] ILocalStoragePtr createLocalStorage()
    {
        return std::make_shared<LocalStorage>(
            m_mockLinkedNotebooksHandler,
            m_mockNotebooksHandler,
            m_mockNotesHandler,
            m_mockResourcesHandler,
            m_mockSavedSearchesHandler,
            m_mockSynchronizationInfoHandler,
            m_mockTagsHandler,
            m_mockVersionHandler,
            m_mockUsersHandler,
            m_notifier.get());
    }

protected:
    std::shared_ptr<mocks::MockILinkedNotebooksHandler> m_mockLinkedNotebooksHandler =
        std::make_shared<StrictMock<mocks::MockILinkedNotebooksHandler>>();

    std::shared_ptr<mocks::MockINotebooksHandler> m_mockNotebooksHandler =
        std::make_shared<StrictMock<mocks::MockINotebooksHandler>>();

    std::shared_ptr<mocks::MockINotesHandler> m_mockNotesHandler =
        std::make_shared<StrictMock<mocks::MockINotesHandler>>();

    std::shared_ptr<mocks::MockIResourcesHandler> m_mockResourcesHandler =
        std::make_shared<StrictMock<mocks::MockIResourcesHandler>>();

    std::shared_ptr<mocks::MockISavedSearchesHandler> m_mockSavedSearchesHandler =
        std::make_shared<StrictMock<mocks::MockISavedSearchesHandler>>();

    std::shared_ptr<mocks::MockISynchronizationInfoHandler> m_mockSynchronizationInfoHandler =
        std::make_shared<StrictMock<mocks::MockISynchronizationInfoHandler>>();

    std::shared_ptr<mocks::MockITagsHandler> m_mockTagsHandler =
        std::make_shared<StrictMock<mocks::MockITagsHandler>>();

    std::shared_ptr<mocks::MockIUsersHandler> m_mockUsersHandler =
        std::make_shared<StrictMock<mocks::MockIUsersHandler>>();

    std::shared_ptr<mocks::MockIVersionHandler> m_mockVersionHandler =
        std::make_shared<StrictMock<mocks::MockIVersionHandler>>();

    std::unique_ptr<Notifier> m_notifier = std::make_unique<Notifier>();
};

TEST_F(LocalStorageTest, Ctor)
{
    EXPECT_NO_THROW((LocalStorage{
        m_mockLinkedNotebooksHandler, m_mockNotebooksHandler,
        m_mockNotesHandler, m_mockResourcesHandler, m_mockSavedSearchesHandler,
        m_mockSynchronizationInfoHandler, m_mockTagsHandler,
        m_mockVersionHandler, m_mockUsersHandler, m_notifier.get()}));
}

TEST_F(LocalStorageTest, CtorNullLinkedNotebooksHandler)
{
    EXPECT_THROW(
        (LocalStorage{
            nullptr, m_mockNotebooksHandler,
            m_mockNotesHandler, m_mockResourcesHandler,
            m_mockSavedSearchesHandler, m_mockSynchronizationInfoHandler,
            m_mockTagsHandler, m_mockVersionHandler, m_mockUsersHandler,
            m_notifier.get()}),
        InvalidArgument);
}

TEST_F(LocalStorageTest, CtorNullNotebooksHandler)
{
    EXPECT_THROW(
        (LocalStorage{
            m_mockLinkedNotebooksHandler, nullptr,
            m_mockNotesHandler, m_mockResourcesHandler,
            m_mockSavedSearchesHandler, m_mockSynchronizationInfoHandler,
            m_mockTagsHandler, m_mockVersionHandler, m_mockUsersHandler,
            m_notifier.get()}),
        InvalidArgument);
}

TEST_F(LocalStorageTest, CtorNullNotesHandler)
{
    EXPECT_THROW(
        (LocalStorage{
            m_mockLinkedNotebooksHandler, m_mockNotebooksHandler,
            nullptr, m_mockResourcesHandler,
            m_mockSavedSearchesHandler, m_mockSynchronizationInfoHandler,
            m_mockTagsHandler, m_mockVersionHandler, m_mockUsersHandler,
            m_notifier.get()}),
        InvalidArgument);
}

TEST_F(LocalStorageTest, CtorNullResourcesHandler)
{
    EXPECT_THROW(
        (LocalStorage{
            m_mockLinkedNotebooksHandler, m_mockNotebooksHandler,
            m_mockNotesHandler, nullptr,
            m_mockSavedSearchesHandler, m_mockSynchronizationInfoHandler,
            m_mockTagsHandler, m_mockVersionHandler, m_mockUsersHandler,
            m_notifier.get()}),
        InvalidArgument);
}

TEST_F(LocalStorageTest, CtorNullSavedSearchesHandler)
{
    EXPECT_THROW(
        (LocalStorage{
            m_mockLinkedNotebooksHandler, m_mockNotebooksHandler,
            m_mockNotesHandler, m_mockResourcesHandler,
            nullptr, m_mockSynchronizationInfoHandler,
            m_mockTagsHandler, m_mockVersionHandler, m_mockUsersHandler,
            m_notifier.get()}),
        InvalidArgument);
}

TEST_F(LocalStorageTest, CtorNullSynchronizationInfoHandler)
{
    EXPECT_THROW(
        (LocalStorage{
            m_mockLinkedNotebooksHandler, m_mockNotebooksHandler,
            m_mockNotesHandler, m_mockResourcesHandler,
            m_mockSavedSearchesHandler, nullptr,
            m_mockTagsHandler, m_mockVersionHandler, m_mockUsersHandler,
            m_notifier.get()}),
        InvalidArgument);
}

TEST_F(LocalStorageTest, CtorNullTagsHandler)
{
    EXPECT_THROW(
        (LocalStorage{
            m_mockLinkedNotebooksHandler, m_mockNotebooksHandler,
            m_mockNotesHandler, m_mockResourcesHandler,
            m_mockSavedSearchesHandler, m_mockSynchronizationInfoHandler,
            nullptr, m_mockVersionHandler, m_mockUsersHandler,
            m_notifier.get()}),
        InvalidArgument);
}

TEST_F(LocalStorageTest, CtorNullVersionHandler)
{
    EXPECT_THROW(
        (LocalStorage{
            m_mockLinkedNotebooksHandler, m_mockNotebooksHandler,
            m_mockNotesHandler, m_mockResourcesHandler,
            m_mockSavedSearchesHandler, m_mockSynchronizationInfoHandler,
            m_mockTagsHandler, nullptr, m_mockUsersHandler,
            m_notifier.get()}),
        InvalidArgument);
}

TEST_F(LocalStorageTest, CtorNullUsersHandler)
{
    EXPECT_THROW(
        (LocalStorage{
            m_mockLinkedNotebooksHandler, m_mockNotebooksHandler,
            m_mockNotesHandler, m_mockResourcesHandler,
            m_mockSavedSearchesHandler, m_mockSynchronizationInfoHandler,
            m_mockTagsHandler, m_mockVersionHandler, nullptr,
            m_notifier.get()}),
        InvalidArgument);
}

TEST_F(LocalStorageTest, CtorNullNotifier)
{
    EXPECT_THROW(
        (LocalStorage{
            m_mockLinkedNotebooksHandler, m_mockNotebooksHandler,
            m_mockNotesHandler, m_mockResourcesHandler,
            m_mockSavedSearchesHandler, m_mockSynchronizationInfoHandler,
            m_mockTagsHandler, m_mockVersionHandler, m_mockUsersHandler,
            nullptr}),
        InvalidArgument);
}

TEST_F(LocalStorageTest, ForwardIsVersionTooHighToVersionHandler)
{
    const auto localStorage = createLocalStorage();

    EXPECT_CALL(*m_mockVersionHandler, isVersionTooHigh)
        .WillOnce(Return(utility::makeReadyFuture<bool>(false)));

    const auto res = localStorage->isVersionTooHigh();
    EXPECT_TRUE(res.isFinished());
    ASSERT_EQ(res.resultCount(), 1);
    EXPECT_FALSE(res.result());
}

TEST_F(LocalStorageTest, ForwardRequiresUpgradeToVersionHandler)
{
    const auto localStorage = createLocalStorage();

    EXPECT_CALL(*m_mockVersionHandler, requiresUpgrade)
        .WillOnce(Return(utility::makeReadyFuture<bool>(true)));

    const auto res = localStorage->requiresUpgrade();
    EXPECT_TRUE(res.isFinished());
    ASSERT_EQ(res.resultCount(), 1);
    EXPECT_TRUE(res.result());
}

TEST_F(LocalStorageTest, ForwardRequiredPatchesToVersionHandler)
{
    const auto localStorage = createLocalStorage();

    EXPECT_CALL(*m_mockVersionHandler, requiredPatches)
        .WillOnce(Return(utility::makeReadyFuture<QList<IPatchPtr>>({})));

    const auto res = localStorage->requiredPatches();
    EXPECT_TRUE(res.isFinished());
    ASSERT_EQ(res.resultCount(), 1);
    EXPECT_TRUE(res.result().isEmpty());
}

TEST_F(LocalStorageTest, ForwardVersionToVersionHandler)
{
    const auto localStorage = createLocalStorage();

    EXPECT_CALL(*m_mockVersionHandler, version)
        .WillOnce(Return(utility::makeReadyFuture<qint32>(3)));

    const auto res = localStorage->version();
    EXPECT_TRUE(res.isFinished());
    ASSERT_EQ(res.resultCount(), 1);
    EXPECT_EQ(res.result(), 3);
}

TEST_F(LocalStorageTest, ForwardHighestSupportedVersionToVersionHandler)
{
    const auto localStorage = createLocalStorage();

    EXPECT_CALL(*m_mockVersionHandler, highestSupportedVersion)
        .WillOnce(Return(utility::makeReadyFuture<qint32>(3)));

    const auto res = localStorage->highestSupportedVersion();
    EXPECT_TRUE(res.isFinished());
    ASSERT_EQ(res.resultCount(), 1);
    EXPECT_EQ(res.result(), 3);
}

TEST_F(LocalStorageTest, ForwardUserCountToUsersHandler)
{
    const auto localStorage = createLocalStorage();

    const quint32 userCount = 3;
    EXPECT_CALL(*m_mockUsersHandler, userCount)
        .WillOnce(Return(utility::makeReadyFuture(userCount)));

    const auto res = localStorage->userCount();
    EXPECT_TRUE(res.isFinished());
    ASSERT_EQ(res.resultCount(), 1);
    EXPECT_EQ(res.result(), userCount);
}

TEST_F(LocalStorageTest, ForwardPutUserToUsersHandler)
{
    const auto localStorage = createLocalStorage();

    EXPECT_CALL(*m_mockUsersHandler, putUser)
        .WillOnce(Return(utility::makeReadyFuture()));

    const auto res = localStorage->putUser(qevercloud::User{});
    EXPECT_TRUE(res.isFinished());
}

TEST_F(LocalStorageTest, ForwardFindUserByIdToUsersHandler)
{
    const auto localStorage = createLocalStorage();

    qevercloud::User user;
    user.setId(qevercloud::UserID{42});

    EXPECT_CALL(*m_mockUsersHandler, findUserById)
        .WillOnce(
            [=](qevercloud::UserID id) mutable {
                EXPECT_EQ(id, user.id());
                return utility::makeReadyFuture(std::move(user));
            });

    const auto res = localStorage->findUserById(user.id().value());
    EXPECT_TRUE(res.isFinished());
    ASSERT_EQ(res.resultCount(), 1);
    EXPECT_EQ(res.result(), user);
}

TEST_F(LocalStorageTest, ForwardExpungeUserByIdToUsersHandler)
{
    const auto localStorage = createLocalStorage();

    qevercloud::User user;
    user.setId(qevercloud::UserID{42});

    EXPECT_CALL(*m_mockUsersHandler, expungeUserById)
        .WillOnce(
            [&](qevercloud::UserID id)
            {
                EXPECT_EQ(id, user.id());
                return utility::makeReadyFuture();
            });

    const auto res = localStorage->expungeUserById(user.id().value());
    EXPECT_TRUE(res.isFinished());
}

TEST_F(LocalStorageTest, ForwardNotebookCountToNotebooksHandler)
{
    const auto localStorage = createLocalStorage();

    const quint32 notebookCount = 4;
    EXPECT_CALL(*m_mockNotebooksHandler, notebookCount)
        .WillOnce(Return(utility::makeReadyFuture(notebookCount)));

    const auto res = localStorage->notebookCount();
    EXPECT_TRUE(res.isFinished());
    ASSERT_EQ(res.resultCount(), 1);
    EXPECT_EQ(res.result(), notebookCount);
}

TEST_F(LocalStorageTest, ForwardPutNotebookToNotebooksHandler)
{
    const auto localStorage = createLocalStorage();

    EXPECT_CALL(*m_mockNotebooksHandler, putNotebook)
        .WillOnce(Return(utility::makeReadyFuture()));

    const auto res = localStorage->putNotebook(qevercloud::Notebook{});
    EXPECT_TRUE(res.isFinished());
}

TEST_F(LocalStorageTest, ForwardFindNotebookByLocalIdToNotebooksHandler)
{
    const auto localStorage = createLocalStorage();

    qevercloud::Notebook notebook;
    notebook.setName(QStringLiteral("Notebook"));

    EXPECT_CALL(*m_mockNotebooksHandler, findNotebookByLocalId)
        .WillOnce(
            [=](QString localId) mutable { // NOLINT
                EXPECT_EQ(localId, notebook.localId());
                return utility::makeReadyFuture(std::move(notebook));
            });

    const auto res = localStorage->findNotebookByLocalId(notebook.localId());
    EXPECT_TRUE(res.isFinished());
    ASSERT_EQ(res.resultCount(), 1);
    EXPECT_EQ(res.result(), notebook);
}

TEST_F(LocalStorageTest, ForwardFindNotebookByGuidToNotebooksHandler)
{
    const auto localStorage = createLocalStorage();

    qevercloud::Notebook notebook;
    notebook.setName(QStringLiteral("Notebook"));
    notebook.setGuid(UidGenerator::Generate());
    notebook.setUpdateSequenceNum(42);

    EXPECT_CALL(*m_mockNotebooksHandler, findNotebookByGuid)
        .WillOnce(
            [=](qevercloud::Guid guid) mutable { // NOLINT
                EXPECT_EQ(guid, notebook.guid());
                return utility::makeReadyFuture(std::move(notebook));
            });

    const auto res = localStorage->findNotebookByGuid(notebook.guid().value());
    EXPECT_TRUE(res.isFinished());
    ASSERT_EQ(res.resultCount(), 1);
    EXPECT_EQ(res.result(), notebook);
}

TEST_F(LocalStorageTest, ForwardFindNotebookByNameToNotebooksHandler)
{
    const auto localStorage = createLocalStorage();

    qevercloud::Notebook notebook;
    notebook.setName(QStringLiteral("Notebook"));
    notebook.setGuid(UidGenerator::Generate());
    notebook.setUpdateSequenceNum(42);
    notebook.setLinkedNotebookGuid(UidGenerator::Generate());

    EXPECT_CALL(*m_mockNotebooksHandler, findNotebookByName)
        .WillOnce(
            [=](QString name, // NOLINT
                std::optional<qevercloud::Guid> linkedNotebookGuid) mutable { // NOLINT
                EXPECT_EQ(name, notebook.name());
                EXPECT_EQ(linkedNotebookGuid, notebook.linkedNotebookGuid());
                return utility::makeReadyFuture(std::move(notebook));
            });

    const auto res = localStorage->findNotebookByName(
        notebook.name().value(), notebook.linkedNotebookGuid());

    EXPECT_TRUE(res.isFinished());
    ASSERT_EQ(res.resultCount(), 1);
    EXPECT_EQ(res.result(), notebook);
}

TEST_F(LocalStorageTest, ForwardFindDefaultNotebookToNotebooksHandler)
{
    const auto localStorage = createLocalStorage();

    qevercloud::Notebook notebook;
    notebook.setName(QStringLiteral("Notebook"));

    EXPECT_CALL(*m_mockNotebooksHandler, findDefaultNotebook)
        .WillOnce(Return(utility::makeReadyFuture(notebook)));

    const auto res = localStorage->findDefaultNotebook();
    EXPECT_TRUE(res.isFinished());
    ASSERT_EQ(res.resultCount(), 1);
    EXPECT_EQ(res.result(), notebook);
}

TEST_F(LocalStorageTest, ForwardExpungeNotebookByLocalIdToNotebooksHandler)
{
    const auto localStorage = createLocalStorage();

    const auto localId = UidGenerator::Generate();
    EXPECT_CALL(*m_mockNotebooksHandler, expungeNotebookByLocalId(localId))
        .WillOnce(Return(utility::makeReadyFuture()));

    const auto res = localStorage->expungeNotebookByLocalId(localId);
    EXPECT_TRUE(res.isFinished());
}

TEST_F(LocalStorageTest, ForwardExpungeNotebookByGuidToNotebooksHandler)
{
    const auto localStorage = createLocalStorage();

    const auto guid = UidGenerator::Generate();
    EXPECT_CALL(*m_mockNotebooksHandler, expungeNotebookByGuid(guid))
        .WillOnce(Return(utility::makeReadyFuture()));

    const auto res = localStorage->expungeNotebookByGuid(guid);
    EXPECT_TRUE(res.isFinished());
}

TEST_F(LocalStorageTest, ForwardExpungeNotebookByNameToNotebooksHandler)
{
    const auto localStorage = createLocalStorage();

    const auto name = QStringLiteral("Notebook");
    const auto linkedNotebookGuid = UidGenerator::Generate();

    EXPECT_CALL(
        *m_mockNotebooksHandler,
        expungeNotebookByName(name, std::make_optional(linkedNotebookGuid)))
        .WillOnce(Return(utility::makeReadyFuture()));

    const auto res =
        localStorage->expungeNotebookByName(name, linkedNotebookGuid);

    EXPECT_TRUE(res.isFinished());
}

TEST_F(LocalStorageTest, ForwardListNotebooksToNotebooksHandler)
{
    const auto localStorage = createLocalStorage();

    const auto notebooks = QList<qevercloud::Notebook>{}
        << qevercloud::Notebook{};

    EXPECT_CALL(*m_mockNotebooksHandler, listNotebooks)
        .WillOnce(Return(utility::makeReadyFuture(notebooks)));

    const auto res = localStorage->listNotebooks();
    EXPECT_TRUE(res.isFinished());
    ASSERT_EQ(res.resultCount(), 1);
    EXPECT_EQ(res.result(), notebooks);
}

TEST_F(LocalStorageTest, ForwardListSharedNotebooksToNotebooksHandler)
{
    const auto localStorage = createLocalStorage();

    const auto guid = UidGenerator::Generate();
    const auto sharedNotebooks = QList<qevercloud::SharedNotebook>{}
        << qevercloud::SharedNotebook{};

    EXPECT_CALL(*m_mockNotebooksHandler, listSharedNotebooks(guid))
        .WillOnce(Return(utility::makeReadyFuture(sharedNotebooks)));

    const auto res = localStorage->listSharedNotebooks(guid);
    EXPECT_TRUE(res.isFinished());
    ASSERT_EQ(res.resultCount(), 1);
    EXPECT_EQ(res.result(), sharedNotebooks);
}

TEST_F(LocalStorageTest, ForwardLinkedNotebookCountToLinkedNotebooksHandler)
{
    const auto localStorage = createLocalStorage();

    const quint32 linkedNotebookCount = 5;
    EXPECT_CALL(*m_mockLinkedNotebooksHandler, linkedNotebookCount)
        .WillOnce(Return(utility::makeReadyFuture(linkedNotebookCount)));

    const auto res = localStorage->linkedNotebookCount();
    EXPECT_TRUE(res.isFinished());
    ASSERT_EQ(res.resultCount(), 1);
    EXPECT_EQ(res.result(), linkedNotebookCount);
}

TEST_F(LocalStorageTest, ForwardPutLinkedNotebookToLinkedNotebooksHandler)
{
    const auto localStorage = createLocalStorage();

    EXPECT_CALL(*m_mockLinkedNotebooksHandler, putLinkedNotebook)
        .WillOnce(Return(utility::makeReadyFuture()));

    const auto res =
        localStorage->putLinkedNotebook(qevercloud::LinkedNotebook{});

    EXPECT_TRUE(res.isFinished());
}

TEST_F(
    LocalStorageTest, ForwardFindLinkedNotebookByGuidToLinkedNotebooksHandler)
{
    const auto localStorage = createLocalStorage();

    qevercloud::LinkedNotebook linkedNotebook;
    linkedNotebook.setGuid(UidGenerator::Generate());
    linkedNotebook.setUsername(QStringLiteral("username"));
    linkedNotebook.setUpdateSequenceNum(42);

    EXPECT_CALL(*m_mockLinkedNotebooksHandler, findLinkedNotebookByGuid)
        .WillOnce(
            [=](qevercloud::Guid guid) mutable { // NOLINT
                EXPECT_EQ(guid, linkedNotebook.guid());
                return utility::makeReadyFuture(std::move(linkedNotebook));
            });

    const auto res =
        localStorage->findLinkedNotebookByGuid(linkedNotebook.guid().value());

    EXPECT_TRUE(res.isFinished());
    ASSERT_EQ(res.resultCount(), 1);
    EXPECT_EQ(res.result(), linkedNotebook);
}

TEST_F(
    LocalStorageTest,
    ForwardExpungeLinkedNotebookByGuidToLinkedNotebooksHandler)
{
    const auto localStorage = createLocalStorage();

    const auto guid = UidGenerator::Generate();
    EXPECT_CALL(
        *m_mockLinkedNotebooksHandler, expungeLinkedNotebookByGuid(guid))
        .WillOnce(Return(utility::makeReadyFuture()));

    const auto res = localStorage->expungeLinkedNotebookByGuid(guid);
    EXPECT_TRUE(res.isFinished());
}

TEST_F(
    LocalStorageTest,
    ForwardListLinkedNotebooksToLinkedNotebooksHandler)
{
    const auto localStorage = createLocalStorage();

    const auto linkedNotebooks = QList<qevercloud::LinkedNotebook>{}
        << qevercloud::LinkedNotebook{};

    EXPECT_CALL(*m_mockLinkedNotebooksHandler, listLinkedNotebooks)
        .WillOnce(Return(utility::makeReadyFuture(linkedNotebooks)));

    const auto res = localStorage->listLinkedNotebooks();
    EXPECT_TRUE(res.isFinished());
    ASSERT_EQ(res.resultCount(), 1);
    EXPECT_EQ(res.result(), linkedNotebooks);
}

} // namespace quentier::local_storage::sql::tests
