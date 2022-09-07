/*
 * Copyright 2021-2022 Dmitry Ivanov
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

#include <quentier/exception/InvalidArgument.h>
#include <quentier/threading/Future.h>
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
            m_mockLinkedNotebooksHandler, m_mockNotebooksHandler,
            m_mockNotesHandler, m_mockResourcesHandler,
            m_mockSavedSearchesHandler, m_mockSynchronizationInfoHandler,
            m_mockTagsHandler, m_mockVersionHandler, m_mockUsersHandler,
            m_notifier.get());
    }

protected:
    std::shared_ptr<mocks::MockILinkedNotebooksHandler>
        m_mockLinkedNotebooksHandler =
            std::make_shared<StrictMock<mocks::MockILinkedNotebooksHandler>>();

    std::shared_ptr<mocks::MockINotebooksHandler> m_mockNotebooksHandler =
        std::make_shared<StrictMock<mocks::MockINotebooksHandler>>();

    std::shared_ptr<mocks::MockINotesHandler> m_mockNotesHandler =
        std::make_shared<StrictMock<mocks::MockINotesHandler>>();

    std::shared_ptr<mocks::MockIResourcesHandler> m_mockResourcesHandler =
        std::make_shared<StrictMock<mocks::MockIResourcesHandler>>();

    std::shared_ptr<mocks::MockISavedSearchesHandler>
        m_mockSavedSearchesHandler =
            std::make_shared<StrictMock<mocks::MockISavedSearchesHandler>>();

    std::shared_ptr<mocks::MockISynchronizationInfoHandler>
        m_mockSynchronizationInfoHandler = std::make_shared<
            StrictMock<mocks::MockISynchronizationInfoHandler>>();

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
            nullptr, m_mockNotebooksHandler, m_mockNotesHandler,
            m_mockResourcesHandler, m_mockSavedSearchesHandler,
            m_mockSynchronizationInfoHandler, m_mockTagsHandler,
            m_mockVersionHandler, m_mockUsersHandler, m_notifier.get()}),
        InvalidArgument);
}

TEST_F(LocalStorageTest, CtorNullNotebooksHandler)
{
    EXPECT_THROW(
        (LocalStorage{
            m_mockLinkedNotebooksHandler, nullptr, m_mockNotesHandler,
            m_mockResourcesHandler, m_mockSavedSearchesHandler,
            m_mockSynchronizationInfoHandler, m_mockTagsHandler,
            m_mockVersionHandler, m_mockUsersHandler, m_notifier.get()}),
        InvalidArgument);
}

TEST_F(LocalStorageTest, CtorNullNotesHandler)
{
    EXPECT_THROW(
        (LocalStorage{
            m_mockLinkedNotebooksHandler, m_mockNotebooksHandler, nullptr,
            m_mockResourcesHandler, m_mockSavedSearchesHandler,
            m_mockSynchronizationInfoHandler, m_mockTagsHandler,
            m_mockVersionHandler, m_mockUsersHandler, m_notifier.get()}),
        InvalidArgument);
}

TEST_F(LocalStorageTest, CtorNullResourcesHandler)
{
    EXPECT_THROW(
        (LocalStorage{
            m_mockLinkedNotebooksHandler, m_mockNotebooksHandler,
            m_mockNotesHandler, nullptr, m_mockSavedSearchesHandler,
            m_mockSynchronizationInfoHandler, m_mockTagsHandler,
            m_mockVersionHandler, m_mockUsersHandler, m_notifier.get()}),
        InvalidArgument);
}

TEST_F(LocalStorageTest, CtorNullSavedSearchesHandler)
{
    EXPECT_THROW(
        (LocalStorage{
            m_mockLinkedNotebooksHandler, m_mockNotebooksHandler,
            m_mockNotesHandler, m_mockResourcesHandler, nullptr,
            m_mockSynchronizationInfoHandler, m_mockTagsHandler,
            m_mockVersionHandler, m_mockUsersHandler, m_notifier.get()}),
        InvalidArgument);
}

TEST_F(LocalStorageTest, CtorNullSynchronizationInfoHandler)
{
    EXPECT_THROW(
        (LocalStorage{
            m_mockLinkedNotebooksHandler, m_mockNotebooksHandler,
            m_mockNotesHandler, m_mockResourcesHandler,
            m_mockSavedSearchesHandler, nullptr, m_mockTagsHandler,
            m_mockVersionHandler, m_mockUsersHandler, m_notifier.get()}),
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
            m_mockTagsHandler, nullptr, m_mockUsersHandler, m_notifier.get()}),
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
        .WillOnce(Return(threading::makeReadyFuture<bool>(false)));

    const auto res = localStorage->isVersionTooHigh();
    ASSERT_TRUE(res.isFinished());
    ASSERT_EQ(res.resultCount(), 1);
    EXPECT_FALSE(res.result());
}

TEST_F(LocalStorageTest, ForwardRequiresUpgradeToVersionHandler)
{
    const auto localStorage = createLocalStorage();

    EXPECT_CALL(*m_mockVersionHandler, requiresUpgrade)
        .WillOnce(Return(threading::makeReadyFuture<bool>(true)));

    const auto res = localStorage->requiresUpgrade();
    ASSERT_TRUE(res.isFinished());
    ASSERT_EQ(res.resultCount(), 1);
    EXPECT_TRUE(res.result());
}

TEST_F(LocalStorageTest, ForwardRequiredPatchesToVersionHandler)
{
    const auto localStorage = createLocalStorage();

    EXPECT_CALL(*m_mockVersionHandler, requiredPatches)
        .WillOnce(Return(threading::makeReadyFuture<QList<IPatchPtr>>({})));

    const auto res = localStorage->requiredPatches();
    ASSERT_TRUE(res.isFinished());
    ASSERT_EQ(res.resultCount(), 1);
    EXPECT_TRUE(res.result().isEmpty());
}

TEST_F(LocalStorageTest, ForwardVersionToVersionHandler)
{
    const auto localStorage = createLocalStorage();

    EXPECT_CALL(*m_mockVersionHandler, version)
        .WillOnce(Return(threading::makeReadyFuture<qint32>(3)));

    const auto res = localStorage->version();
    ASSERT_TRUE(res.isFinished());
    ASSERT_EQ(res.resultCount(), 1);
    EXPECT_EQ(res.result(), 3);
}

TEST_F(LocalStorageTest, ForwardHighestSupportedVersionToVersionHandler)
{
    const auto localStorage = createLocalStorage();

    EXPECT_CALL(*m_mockVersionHandler, highestSupportedVersion)
        .WillOnce(Return(threading::makeReadyFuture<qint32>(3)));

    const auto res = localStorage->highestSupportedVersion();
    ASSERT_TRUE(res.isFinished());
    ASSERT_EQ(res.resultCount(), 1);
    EXPECT_EQ(res.result(), 3);
}

TEST_F(LocalStorageTest, ForwardUserCountToUsersHandler)
{
    const auto localStorage = createLocalStorage();

    const quint32 userCount = 3;
    EXPECT_CALL(*m_mockUsersHandler, userCount)
        .WillOnce(Return(threading::makeReadyFuture(userCount)));

    const auto res = localStorage->userCount();
    ASSERT_TRUE(res.isFinished());
    ASSERT_EQ(res.resultCount(), 1);
    EXPECT_EQ(res.result(), userCount);
}

TEST_F(LocalStorageTest, ForwardPutUserToUsersHandler)
{
    const auto localStorage = createLocalStorage();

    EXPECT_CALL(*m_mockUsersHandler, putUser)
        .WillOnce(Return(threading::makeReadyFuture()));

    const auto res = localStorage->putUser(qevercloud::User{});
    ASSERT_TRUE(res.isFinished());
}

TEST_F(LocalStorageTest, ForwardFindUserByIdToUsersHandler)
{
    const auto localStorage = createLocalStorage();

    qevercloud::User user;
    user.setId(qevercloud::UserID{42});

    EXPECT_CALL(*m_mockUsersHandler, findUserById)
        .WillOnce([=](qevercloud::UserID id) mutable {
            EXPECT_EQ(id, user.id());
            return threading::makeReadyFuture(
                std::make_optional(std::move(user)));
        });

    const auto res = localStorage->findUserById(user.id().value());
    ASSERT_TRUE(res.isFinished());
    ASSERT_EQ(res.resultCount(), 1);
    EXPECT_EQ(res.result(), user);
}

TEST_F(LocalStorageTest, ForwardExpungeUserByIdToUsersHandler)
{
    const auto localStorage = createLocalStorage();

    qevercloud::User user;
    user.setId(qevercloud::UserID{42});

    EXPECT_CALL(*m_mockUsersHandler, expungeUserById)
        .WillOnce([&](qevercloud::UserID id) {
            EXPECT_EQ(id, user.id());
            return threading::makeReadyFuture();
        });

    const auto res = localStorage->expungeUserById(user.id().value());
    ASSERT_TRUE(res.isFinished());
}

TEST_F(LocalStorageTest, ForwardNotebookCountToNotebooksHandler)
{
    const auto localStorage = createLocalStorage();

    const quint32 notebookCount = 4;
    EXPECT_CALL(*m_mockNotebooksHandler, notebookCount)
        .WillOnce(Return(threading::makeReadyFuture(notebookCount)));

    const auto res = localStorage->notebookCount();
    ASSERT_TRUE(res.isFinished());
    ASSERT_EQ(res.resultCount(), 1);
    EXPECT_EQ(res.result(), notebookCount);
}

TEST_F(LocalStorageTest, ForwardPutNotebookToNotebooksHandler)
{
    const auto localStorage = createLocalStorage();

    const auto notebook = qevercloud::Notebook{};
    EXPECT_CALL(*m_mockNotebooksHandler, putNotebook(notebook))
        .WillOnce(Return(threading::makeReadyFuture()));

    const auto res = localStorage->putNotebook(notebook);
    ASSERT_TRUE(res.isFinished());
}

TEST_F(LocalStorageTest, ForwardFindNotebookByLocalIdToNotebooksHandler)
{
    const auto localStorage = createLocalStorage();

    qevercloud::Notebook notebook;
    notebook.setName(QStringLiteral("Notebook"));

    const auto & localId = notebook.localId();

    EXPECT_CALL(*m_mockNotebooksHandler, findNotebookByLocalId(localId))
        .WillOnce(
            Return(threading::makeReadyFuture(std::make_optional(notebook))));

    const auto res = localStorage->findNotebookByLocalId(localId);
    ASSERT_TRUE(res.isFinished());
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

    const auto guid = notebook.guid().value();

    EXPECT_CALL(*m_mockNotebooksHandler, findNotebookByGuid(guid))
        .WillOnce(
            Return(threading::makeReadyFuture(std::make_optional(notebook))));

    const auto res = localStorage->findNotebookByGuid(guid);
    ASSERT_TRUE(res.isFinished());
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

    const auto name = notebook.name().value();
    const auto & linkedNotebookGuid = notebook.linkedNotebookGuid();

    EXPECT_CALL(
        *m_mockNotebooksHandler, findNotebookByName(name, linkedNotebookGuid))
        .WillOnce(
            Return(threading::makeReadyFuture(std::make_optional(notebook))));

    const auto res = localStorage->findNotebookByName(name, linkedNotebookGuid);
    ASSERT_TRUE(res.isFinished());
    ASSERT_EQ(res.resultCount(), 1);
    EXPECT_EQ(res.result(), notebook);
}

TEST_F(LocalStorageTest, ForwardFindDefaultNotebookToNotebooksHandler)
{
    const auto localStorage = createLocalStorage();

    qevercloud::Notebook notebook;
    notebook.setName(QStringLiteral("Notebook"));

    EXPECT_CALL(*m_mockNotebooksHandler, findDefaultNotebook)
        .WillOnce(
            Return(threading::makeReadyFuture(std::make_optional(notebook))));

    const auto res = localStorage->findDefaultNotebook();
    ASSERT_TRUE(res.isFinished());
    ASSERT_EQ(res.resultCount(), 1);
    EXPECT_EQ(res.result(), notebook);
}

TEST_F(LocalStorageTest, ForwardExpungeNotebookByLocalIdToNotebooksHandler)
{
    const auto localStorage = createLocalStorage();

    const auto localId = UidGenerator::Generate();
    EXPECT_CALL(*m_mockNotebooksHandler, expungeNotebookByLocalId(localId))
        .WillOnce(Return(threading::makeReadyFuture()));

    const auto res = localStorage->expungeNotebookByLocalId(localId);
    ASSERT_TRUE(res.isFinished());
}

TEST_F(LocalStorageTest, ForwardExpungeNotebookByGuidToNotebooksHandler)
{
    const auto localStorage = createLocalStorage();

    const auto guid = UidGenerator::Generate();
    EXPECT_CALL(*m_mockNotebooksHandler, expungeNotebookByGuid(guid))
        .WillOnce(Return(threading::makeReadyFuture()));

    const auto res = localStorage->expungeNotebookByGuid(guid);
    ASSERT_TRUE(res.isFinished());
}

TEST_F(LocalStorageTest, ForwardExpungeNotebookByNameToNotebooksHandler)
{
    const auto localStorage = createLocalStorage();

    const auto name = QStringLiteral("Notebook");
    const auto linkedNotebookGuid = UidGenerator::Generate();

    EXPECT_CALL(
        *m_mockNotebooksHandler,
        expungeNotebookByName(name, std::make_optional(linkedNotebookGuid)))
        .WillOnce(Return(threading::makeReadyFuture()));

    const auto res =
        localStorage->expungeNotebookByName(name, linkedNotebookGuid);

    ASSERT_TRUE(res.isFinished());
}

TEST_F(LocalStorageTest, ForwardListNotebooksToNotebooksHandler)
{
    const auto localStorage = createLocalStorage();

    const auto notebooks = QList<qevercloud::Notebook>{}
        << qevercloud::Notebook{};

    const auto listOptions = ILocalStorage::ListNotebooksOptions{};
    EXPECT_CALL(*m_mockNotebooksHandler, listNotebooks(listOptions))
        .WillOnce(Return(threading::makeReadyFuture(notebooks)));

    const auto res = localStorage->listNotebooks(listOptions);
    ASSERT_TRUE(res.isFinished());
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
        .WillOnce(Return(threading::makeReadyFuture(sharedNotebooks)));

    const auto res = localStorage->listSharedNotebooks(guid);
    ASSERT_TRUE(res.isFinished());
    ASSERT_EQ(res.resultCount(), 1);
    EXPECT_EQ(res.result(), sharedNotebooks);
}

TEST_F(LocalStorageTest, ForwardLinkedNotebookCountToLinkedNotebooksHandler)
{
    const auto localStorage = createLocalStorage();

    const quint32 linkedNotebookCount = 5;
    EXPECT_CALL(*m_mockLinkedNotebooksHandler, linkedNotebookCount)
        .WillOnce(Return(threading::makeReadyFuture(linkedNotebookCount)));

    const auto res = localStorage->linkedNotebookCount();
    ASSERT_TRUE(res.isFinished());
    ASSERT_EQ(res.resultCount(), 1);
    EXPECT_EQ(res.result(), linkedNotebookCount);
}

TEST_F(LocalStorageTest, ForwardPutLinkedNotebookToLinkedNotebooksHandler)
{
    const auto localStorage = createLocalStorage();

    EXPECT_CALL(*m_mockLinkedNotebooksHandler, putLinkedNotebook)
        .WillOnce(Return(threading::makeReadyFuture()));

    const auto res =
        localStorage->putLinkedNotebook(qevercloud::LinkedNotebook{});

    ASSERT_TRUE(res.isFinished());
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
        .WillOnce([=](qevercloud::Guid guid) mutable { // NOLINT
            EXPECT_EQ(guid, linkedNotebook.guid());
            return threading::makeReadyFuture(
                std::make_optional(std::move(linkedNotebook)));
        });

    const auto res =
        localStorage->findLinkedNotebookByGuid(linkedNotebook.guid().value());

    ASSERT_TRUE(res.isFinished());
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
        .WillOnce(Return(threading::makeReadyFuture()));

    const auto res = localStorage->expungeLinkedNotebookByGuid(guid);
    ASSERT_TRUE(res.isFinished());
}

TEST_F(LocalStorageTest, ForwardListLinkedNotebooksToLinkedNotebooksHandler)
{
    const auto localStorage = createLocalStorage();

    const auto linkedNotebooks = QList<qevercloud::LinkedNotebook>{}
        << qevercloud::LinkedNotebook{};

    EXPECT_CALL(*m_mockLinkedNotebooksHandler, listLinkedNotebooks)
        .WillOnce(Return(threading::makeReadyFuture(linkedNotebooks)));

    const auto res = localStorage->listLinkedNotebooks();
    ASSERT_TRUE(res.isFinished());
    ASSERT_EQ(res.resultCount(), 1);
    EXPECT_EQ(res.result(), linkedNotebooks);
}

TEST_F(LocalStorageTest, ForwardNoteCountToNotesHandler)
{
    const auto localStorage = createLocalStorage();

    const quint32 noteCount = 7;

    const ILocalStorage::NoteCountOptions options =
        ILocalStorage::NoteCountOptions{
            ILocalStorage::NoteCountOption::IncludeNonDeletedNotes} |
        ILocalStorage::NoteCountOption::IncludeDeletedNotes;

    EXPECT_CALL(*m_mockNotesHandler, noteCount(options))
        .WillOnce(Return(threading::makeReadyFuture(noteCount)));

    const auto res = localStorage->noteCount(options);
    ASSERT_TRUE(res.isFinished());
    ASSERT_EQ(res.resultCount(), 1);
    EXPECT_EQ(res.result(), noteCount);
}

TEST_F(LocalStorageTest, ForwardNoteCountPerNotebookLocalIdToNotesHandler)
{
    const auto localStorage = createLocalStorage();

    const quint32 noteCount = 8;
    const auto notebookLocalId = UidGenerator::Generate();

    const ILocalStorage::NoteCountOptions options =
        ILocalStorage::NoteCountOptions{
            ILocalStorage::NoteCountOption::IncludeNonDeletedNotes} |
        ILocalStorage::NoteCountOption::IncludeDeletedNotes;

    EXPECT_CALL(
        *m_mockNotesHandler,
        noteCountPerNotebookLocalId(notebookLocalId, options))
        .WillOnce(Return(threading::makeReadyFuture(noteCount)));

    const auto res =
        localStorage->noteCountPerNotebookLocalId(notebookLocalId, options);

    ASSERT_TRUE(res.isFinished());
    ASSERT_EQ(res.resultCount(), 1);
    EXPECT_EQ(res.result(), noteCount);
}

TEST_F(LocalStorageTest, ForwardNoteCountPerTagLocalIdToNotesHandler)
{
    const auto localStorage = createLocalStorage();

    const quint32 noteCount = 9;
    const auto tagLocalId = UidGenerator::Generate();

    const ILocalStorage::NoteCountOptions options =
        ILocalStorage::NoteCountOptions{
            ILocalStorage::NoteCountOption::IncludeNonDeletedNotes} |
        ILocalStorage::NoteCountOption::IncludeDeletedNotes;

    EXPECT_CALL(
        *m_mockNotesHandler, noteCountPerTagLocalId(tagLocalId, options))
        .WillOnce(Return(threading::makeReadyFuture(noteCount)));

    const auto res = localStorage->noteCountPerTagLocalId(tagLocalId, options);
    ASSERT_TRUE(res.isFinished());
    ASSERT_EQ(res.resultCount(), 1);
    EXPECT_EQ(res.result(), noteCount);
}

TEST_F(LocalStorageTest, ForwardNoteCountPerTagsToNotesHandler)
{
    const auto localStorage = createLocalStorage();

    QHash<QString, quint32> noteCounts;
    noteCounts[UidGenerator::Generate()] = 10;

    const ILocalStorage::NoteCountOptions noteCountOptions =
        ILocalStorage::NoteCountOptions{
            ILocalStorage::NoteCountOption::IncludeNonDeletedNotes} |
        ILocalStorage::NoteCountOption::IncludeDeletedNotes;

    const auto listTagsOptions = ILocalStorage::ListTagsOptions{};
    EXPECT_CALL(
        *m_mockNotesHandler,
        noteCountsPerTags(listTagsOptions, noteCountOptions))
        .WillOnce(Return(threading::makeReadyFuture(noteCounts)));

    const auto res =
        localStorage->noteCountsPerTags(listTagsOptions, noteCountOptions);

    ASSERT_TRUE(res.isFinished());
    ASSERT_EQ(res.resultCount(), 1);
    EXPECT_EQ(res.result(), noteCounts);
}

TEST_F(
    LocalStorageTest, ForwardNoteCountPerNotebookAndTagLocalIdsToNotesHandler)
{
    const auto localStorage = createLocalStorage();

    const quint32 noteCount = 11;
    const auto notebookLocalIds = QStringList{} << UidGenerator::Generate();
    const auto tagLocalIds = QStringList{} << UidGenerator::Generate();

    const ILocalStorage::NoteCountOptions options =
        ILocalStorage::NoteCountOptions{
            ILocalStorage::NoteCountOption::IncludeNonDeletedNotes} |
        ILocalStorage::NoteCountOption::IncludeDeletedNotes;

    EXPECT_CALL(
        *m_mockNotesHandler,
        noteCountPerNotebookAndTagLocalIds(
            notebookLocalIds, tagLocalIds, options))
        .WillOnce(Return(threading::makeReadyFuture(noteCount)));

    const auto res = localStorage->noteCountPerNotebookAndTagLocalIds(
        notebookLocalIds, tagLocalIds, options);

    ASSERT_TRUE(res.isFinished());
    ASSERT_EQ(res.resultCount(), 1);
    EXPECT_EQ(res.result(), noteCount);
}

TEST_F(LocalStorageTest, ForwardPutNoteToNotesHandler)
{
    const auto localStorage = createLocalStorage();

    const auto note = qevercloud::Note{};
    EXPECT_CALL(*m_mockNotesHandler, putNote(note))
        .WillOnce(Return(threading::makeReadyFuture()));

    const auto res = localStorage->putNote(note);
    ASSERT_TRUE(res.isFinished());
}

TEST_F(LocalStorageTest, ForwardUpdateNoteToNotesHandler)
{
    const auto localStorage = createLocalStorage();

    const auto note = qevercloud::Note{};

    const auto options =
        ILocalStorage::UpdateNoteOptions{
            ILocalStorage::UpdateNoteOption::UpdateResourceMetadata} |
        ILocalStorage::UpdateNoteOption::UpdateResourceBinaryData |
        ILocalStorage::UpdateNoteOption::UpdateTags;

    EXPECT_CALL(*m_mockNotesHandler, updateNote(note, options))
        .WillOnce(Return(threading::makeReadyFuture()));

    const auto res = localStorage->updateNote(note, options);
    ASSERT_TRUE(res.isFinished());
}

TEST_F(LocalStorageTest, ForwardFindNoteByLocalIdToNotesHandler)
{
    const auto localStorage = createLocalStorage();

    const auto note = qevercloud::Note{};
    const auto & localId = note.localId();

    const auto options =
        ILocalStorage::FetchNoteOptions{
            ILocalStorage::FetchNoteOption::WithResourceMetadata} |
        ILocalStorage::FetchNoteOption::WithResourceBinaryData;

    EXPECT_CALL(*m_mockNotesHandler, findNoteByLocalId(localId, options))
        .WillOnce(Return(threading::makeReadyFuture(std::make_optional(note))));

    const auto res = localStorage->findNoteByLocalId(localId, options);
    ASSERT_TRUE(res.isFinished());
    ASSERT_EQ(res.resultCount(), 1);
    EXPECT_EQ(res.result(), note);
}

TEST_F(LocalStorageTest, ForwardFindNoteByGuidToNotesHandler)
{
    const auto localStorage = createLocalStorage();

    auto note = qevercloud::Note{};
    note.setGuid(UidGenerator::Generate());

    const auto guid = note.guid().value();

    const auto options =
        ILocalStorage::FetchNoteOptions{
            ILocalStorage::FetchNoteOption::WithResourceMetadata} |
        ILocalStorage::FetchNoteOption::WithResourceBinaryData;

    EXPECT_CALL(*m_mockNotesHandler, findNoteByGuid(guid, options))
        .WillOnce(Return(threading::makeReadyFuture(std::make_optional(note))));

    const auto res = localStorage->findNoteByGuid(guid, options);
    ASSERT_TRUE(res.isFinished());
    ASSERT_EQ(res.resultCount(), 1);
    EXPECT_EQ(res.result(), note);
}

TEST_F(LocalStorageTest, ForwardListNotesToNotesHandler)
{
    const auto localStorage = createLocalStorage();

    const auto notes = QList<qevercloud::Note>{} << qevercloud::Note{};

    const auto fetchOptions =
        ILocalStorage::FetchNoteOptions{
            ILocalStorage::FetchNoteOption::WithResourceMetadata} |
        ILocalStorage::FetchNoteOption::WithResourceBinaryData;

    const auto listOptions = ILocalStorage::ListNotesOptions{};
    EXPECT_CALL(*m_mockNotesHandler, listNotes(fetchOptions, listOptions))
        .WillOnce(Return(threading::makeReadyFuture(notes)));

    const auto res = localStorage->listNotes(fetchOptions, listOptions);
    ASSERT_TRUE(res.isFinished());
    ASSERT_EQ(res.resultCount(), 1);
    EXPECT_EQ(res.result(), notes);
}

TEST_F(LocalStorageTest, ForwardListNotesPerNotebookLocalIdToNotesHandler)
{
    const auto localStorage = createLocalStorage();

    const auto notes = QList<qevercloud::Note>{} << qevercloud::Note{};
    const auto notebookLocalId = UidGenerator::Generate();

    const auto fetchOptions =
        ILocalStorage::FetchNoteOptions{
            ILocalStorage::FetchNoteOption::WithResourceMetadata} |
        ILocalStorage::FetchNoteOption::WithResourceBinaryData;

    const auto listOptions = ILocalStorage::ListNotesOptions{};
    EXPECT_CALL(
        *m_mockNotesHandler,
        listNotesPerNotebookLocalId(notebookLocalId, fetchOptions, listOptions))
        .WillOnce(Return(threading::makeReadyFuture(notes)));

    const auto res = localStorage->listNotesPerNotebookLocalId(
        notebookLocalId, fetchOptions, listOptions);

    ASSERT_TRUE(res.isFinished());
    ASSERT_EQ(res.resultCount(), 1);
    EXPECT_EQ(res.result(), notes);
}

TEST_F(LocalStorageTest, ForwardListNotesPerTagLocalIdToNotesHandler)
{
    const auto localStorage = createLocalStorage();

    const auto notes = QList<qevercloud::Note>{} << qevercloud::Note{};
    const auto tagLocalId = UidGenerator::Generate();

    const auto fetchOptions =
        ILocalStorage::FetchNoteOptions{
            ILocalStorage::FetchNoteOption::WithResourceMetadata} |
        ILocalStorage::FetchNoteOption::WithResourceBinaryData;

    const auto listOptions = ILocalStorage::ListNotesOptions{};
    EXPECT_CALL(
        *m_mockNotesHandler,
        listNotesPerTagLocalId(tagLocalId, fetchOptions, listOptions))
        .WillOnce(Return(threading::makeReadyFuture(notes)));

    const auto res = localStorage->listNotesPerTagLocalId(
        tagLocalId, fetchOptions, listOptions);

    ASSERT_TRUE(res.isFinished());
    ASSERT_EQ(res.resultCount(), 1);
    EXPECT_EQ(res.result(), notes);
}

TEST_F(
    LocalStorageTest, ForwardListNotesPerNotebookAndTagLocalIdsToNotesHandler)
{
    const auto localStorage = createLocalStorage();

    const auto notes = QList<qevercloud::Note>{} << qevercloud::Note{};
    const auto notebookLocalIds = QStringList{} << UidGenerator::Generate();
    const auto tagLocalIds = QStringList{} << UidGenerator::Generate();

    const auto fetchOptions =
        ILocalStorage::FetchNoteOptions{
            ILocalStorage::FetchNoteOption::WithResourceMetadata} |
        ILocalStorage::FetchNoteOption::WithResourceBinaryData;

    const auto listOptions = ILocalStorage::ListNotesOptions{};
    EXPECT_CALL(
        *m_mockNotesHandler,
        listNotesPerNotebookAndTagLocalIds(
            notebookLocalIds, tagLocalIds, fetchOptions, listOptions))
        .WillOnce(Return(threading::makeReadyFuture(notes)));

    const auto res = localStorage->listNotesPerNotebookAndTagLocalIds(
        notebookLocalIds, tagLocalIds, fetchOptions, listOptions);

    ASSERT_TRUE(res.isFinished());
    ASSERT_EQ(res.resultCount(), 1);
    EXPECT_EQ(res.result(), notes);
}

TEST_F(LocalStorageTest, ForwardListNotesByLocalIdsToNotesHandler)
{
    const auto localStorage = createLocalStorage();

    const auto notes = QList<qevercloud::Note>{} << qevercloud::Note{};
    const auto noteLocalIds = QStringList{} << notes.begin()->localId();

    const auto fetchOptions =
        ILocalStorage::FetchNoteOptions{
            ILocalStorage::FetchNoteOption::WithResourceMetadata} |
        ILocalStorage::FetchNoteOption::WithResourceBinaryData;

    const auto listOptions = ILocalStorage::ListNotesOptions{};
    EXPECT_CALL(
        *m_mockNotesHandler,
        listNotesByLocalIds(noteLocalIds, fetchOptions, listOptions))
        .WillOnce(Return(threading::makeReadyFuture(notes)));

    const auto res = localStorage->listNotesByLocalIds(
        noteLocalIds, fetchOptions, listOptions);

    ASSERT_TRUE(res.isFinished());
    ASSERT_EQ(res.resultCount(), 1);
    EXPECT_EQ(res.result(), notes);
}

TEST_F(LocalStorageTest, ForwardQueryNotesToNotesHandler)
{
    const auto localStorage = createLocalStorage();

    const auto notes = QList<qevercloud::Note>{} << qevercloud::Note{};

    NoteSearchQuery query;

    ErrorString errorDescription;
    ASSERT_TRUE(
        query.setQueryString(QStringLiteral("Something"), errorDescription));

    const auto fetchOptions =
        ILocalStorage::FetchNoteOptions{
            ILocalStorage::FetchNoteOption::WithResourceMetadata} |
        ILocalStorage::FetchNoteOption::WithResourceBinaryData;

    EXPECT_CALL(*m_mockNotesHandler, queryNotes(query, fetchOptions))
        .WillOnce(Return(threading::makeReadyFuture(notes)));

    const auto res = localStorage->queryNotes(query, fetchOptions);
    ASSERT_TRUE(res.isFinished());
    ASSERT_EQ(res.resultCount(), 1);
    EXPECT_EQ(res.result(), notes);
}

TEST_F(LocalStorageTest, ForwardQueryNoteLocalIdsToNotesHandler)
{
    const auto localStorage = createLocalStorage();

    const auto noteLocalIds = QStringList{} << UidGenerator::Generate();

    NoteSearchQuery query;

    ErrorString errorDescription;
    ASSERT_TRUE(
        query.setQueryString(QStringLiteral("Something"), errorDescription));

    EXPECT_CALL(*m_mockNotesHandler, queryNoteLocalIds(query))
        .WillOnce(Return(threading::makeReadyFuture(noteLocalIds)));

    const auto res = localStorage->queryNoteLocalIds(query);
    ASSERT_TRUE(res.isFinished());
    ASSERT_EQ(res.resultCount(), 1);
    EXPECT_EQ(res.result(), noteLocalIds);
}

TEST_F(LocalStorageTest, ForwardExpungeNoteByLocalIdToNotesHandler)
{
    const auto localStorage = createLocalStorage();

    const auto localId = UidGenerator::Generate();
    EXPECT_CALL(*m_mockNotesHandler, expungeNoteByLocalId(localId))
        .WillOnce(Return(threading::makeReadyFuture()));

    const auto res = localStorage->expungeNoteByLocalId(localId);
    ASSERT_TRUE(res.isFinished());
}

TEST_F(LocalStorageTest, ForwardExpungeNoteByGuidToNotesHandler)
{
    const auto localStorage = createLocalStorage();

    const auto guid = UidGenerator::Generate();
    EXPECT_CALL(*m_mockNotesHandler, expungeNoteByGuid(guid))
        .WillOnce(Return(threading::makeReadyFuture()));

    const auto res = localStorage->expungeNoteByGuid(guid);
    ASSERT_TRUE(res.isFinished());
}

TEST_F(LocalStorageTest, ForwardTagCountToTagsHandler)
{
    const auto localStorage = createLocalStorage();

    const quint32 tagCount = 12;
    EXPECT_CALL(*m_mockTagsHandler, tagCount)
        .WillOnce(Return(threading::makeReadyFuture(tagCount)));

    const auto res = localStorage->tagCount();
    ASSERT_TRUE(res.isFinished());
    ASSERT_EQ(res.resultCount(), 1);
    EXPECT_EQ(res.result(), tagCount);
}

TEST_F(LocalStorageTest, ForwardPutTagToTagsHandler)
{
    const auto localStorage = createLocalStorage();

    const auto tag = qevercloud::Tag{};
    EXPECT_CALL(*m_mockTagsHandler, putTag(tag))
        .WillOnce(Return(threading::makeReadyFuture()));

    const auto res = localStorage->putTag(tag);
    ASSERT_TRUE(res.isFinished());
}

TEST_F(LocalStorageTest, ForwardFindTagByLocalIdToTagsHandler)
{
    const auto localStorage = createLocalStorage();

    qevercloud::Tag tag;
    tag.setName(QStringLiteral("Tag"));

    const auto & localId = tag.localId();

    EXPECT_CALL(*m_mockTagsHandler, findTagByLocalId(localId))
        .WillOnce(Return(threading::makeReadyFuture(std::make_optional(tag))));

    const auto res = localStorage->findTagByLocalId(localId);
    ASSERT_TRUE(res.isFinished());
    ASSERT_EQ(res.resultCount(), 1);
    EXPECT_EQ(res.result(), tag);
}

TEST_F(LocalStorageTest, ForwardFindTagByGuidToTagsHandler)
{
    const auto localStorage = createLocalStorage();

    qevercloud::Tag tag;
    tag.setName(QStringLiteral("Tag"));
    tag.setGuid(UidGenerator::Generate());
    tag.setUpdateSequenceNum(42);

    const auto guid = tag.guid().value();

    EXPECT_CALL(*m_mockTagsHandler, findTagByGuid(guid))
        .WillOnce(Return(threading::makeReadyFuture(std::make_optional(tag))));

    const auto res = localStorage->findTagByGuid(guid);
    ASSERT_TRUE(res.isFinished());
    ASSERT_EQ(res.resultCount(), 1);
    EXPECT_EQ(res.result(), tag);
}

TEST_F(LocalStorageTest, ForwardFindTagByNameToTagsHandler)
{
    const auto localStorage = createLocalStorage();

    qevercloud::Tag tag;
    tag.setName(QStringLiteral("Tag"));
    tag.setGuid(UidGenerator::Generate());
    tag.setUpdateSequenceNum(42);
    tag.setLinkedNotebookGuid(UidGenerator::Generate());

    const auto name = tag.name().value();
    const auto & linkedNotebookGuid = tag.linkedNotebookGuid();

    EXPECT_CALL(*m_mockTagsHandler, findTagByName(name, linkedNotebookGuid))
        .WillOnce(Return(threading::makeReadyFuture(std::make_optional(tag))));

    const auto res = localStorage->findTagByName(name, linkedNotebookGuid);
    ASSERT_TRUE(res.isFinished());
    ASSERT_EQ(res.resultCount(), 1);
    EXPECT_EQ(res.result(), tag);
}

TEST_F(LocalStorageTest, ForwardListTagsToTagsHandler)
{
    const auto localStorage = createLocalStorage();

    const auto tags = QList<qevercloud::Tag>{} << qevercloud::Tag{};

    const auto listOptions = ILocalStorage::ListTagsOptions{};
    EXPECT_CALL(*m_mockTagsHandler, listTags(listOptions))
        .WillOnce(Return(threading::makeReadyFuture(tags)));

    const auto res = localStorage->listTags(listOptions);
    ASSERT_TRUE(res.isFinished());
    ASSERT_EQ(res.resultCount(), 1);
    EXPECT_EQ(res.result(), tags);
}

TEST_F(LocalStorageTest, ForwardListTagsPerNoteLocalIdToNotesHandler)
{
    const auto localStorage = createLocalStorage();

    const auto tags = QList<qevercloud::Tag>{} << qevercloud::Tag{};

    const auto listOptions = ILocalStorage::ListTagsOptions{};
    const auto & localId = tags[0].localId();
    EXPECT_CALL(
        *m_mockTagsHandler, listTagsPerNoteLocalId(localId, listOptions))
        .WillOnce(Return(threading::makeReadyFuture(tags)));

    const auto res = localStorage->listTagsPerNoteLocalId(localId, listOptions);
    ASSERT_TRUE(res.isFinished());
    ASSERT_EQ(res.resultCount(), 1);
    EXPECT_EQ(res.result(), tags);
}

TEST_F(LocalStorageTest, ForwardExpungeTagByLocalIdToTagsHandler)
{
    const auto localStorage = createLocalStorage();

    const auto localId = UidGenerator::Generate();
    EXPECT_CALL(*m_mockTagsHandler, expungeTagByLocalId(localId))
        .WillOnce(Return(threading::makeReadyFuture()));

    const auto res = localStorage->expungeTagByLocalId(localId);
    ASSERT_TRUE(res.isFinished());
}

TEST_F(LocalStorageTest, ForwardExpungeTagByGuidToTagsHandler)
{
    const auto localStorage = createLocalStorage();

    const auto guid = UidGenerator::Generate();
    EXPECT_CALL(*m_mockTagsHandler, expungeTagByGuid(guid))
        .WillOnce(Return(threading::makeReadyFuture()));

    const auto res = localStorage->expungeTagByGuid(guid);
    ASSERT_TRUE(res.isFinished());
}

TEST_F(LocalStorageTest, ForwardExpungeTagByNameToTagsHandler)
{
    const auto localStorage = createLocalStorage();

    const auto name = QStringLiteral("Tag");
    const auto linkedNotebookGuid = UidGenerator::Generate();

    EXPECT_CALL(
        *m_mockTagsHandler,
        expungeTagByName(name, std::make_optional(linkedNotebookGuid)))
        .WillOnce(Return(threading::makeReadyFuture()));

    const auto res = localStorage->expungeTagByName(name, linkedNotebookGuid);

    ASSERT_TRUE(res.isFinished());
}

TEST_F(LocalStorageTest, ForwardResourceCountToResourcesHandler)
{
    const auto localStorage = createLocalStorage();

    const quint32 resourceCount = 13;

    const ILocalStorage::NoteCountOptions options =
        ILocalStorage::NoteCountOptions{
            ILocalStorage::NoteCountOption::IncludeNonDeletedNotes} |
        ILocalStorage::NoteCountOption::IncludeDeletedNotes;

    EXPECT_CALL(*m_mockResourcesHandler, resourceCount(options))
        .WillOnce(Return(threading::makeReadyFuture(resourceCount)));

    const auto res = localStorage->resourceCount(options);
    ASSERT_TRUE(res.isFinished());
    ASSERT_EQ(res.resultCount(), 1);
    EXPECT_EQ(res.result(), resourceCount);
}

TEST_F(LocalStorageTest, ForwardResourceCountPerNoteLocalIdToResourcesHandler)
{
    const auto localStorage = createLocalStorage();

    const quint32 resourceCount = 13;
    const auto noteLocalId = UidGenerator::Generate();

    EXPECT_CALL(
        *m_mockResourcesHandler, resourceCountPerNoteLocalId(noteLocalId))
        .WillOnce(Return(threading::makeReadyFuture(resourceCount)));

    const auto res = localStorage->resourceCountPerNoteLocalId(noteLocalId);
    ASSERT_TRUE(res.isFinished());
    ASSERT_EQ(res.resultCount(), 1);
    EXPECT_EQ(res.result(), resourceCount);
}

TEST_F(LocalStorageTest, ForwardPutResourceToResourcesHandler)
{
    const auto localStorage = createLocalStorage();

    const auto resource = qevercloud::Resource{};
    EXPECT_CALL(*m_mockResourcesHandler, putResource(resource))
        .WillOnce(Return(threading::makeReadyFuture()));

    const auto res = localStorage->putResource(resource);
    ASSERT_TRUE(res.isFinished());
}

TEST_F(LocalStorageTest, ForwardFindResourceByLocalIdToNotesHandler)
{
    const auto localStorage = createLocalStorage();

    const auto resource = qevercloud::Resource{};
    const auto & localId = resource.localId();

    const auto options = ILocalStorage::FetchResourceOptions{
        ILocalStorage::FetchResourceOption::WithBinaryData};

    EXPECT_CALL(
        *m_mockResourcesHandler, findResourceByLocalId(localId, options))
        .WillOnce(
            Return(threading::makeReadyFuture(std::make_optional(resource))));

    const auto res = localStorage->findResourceByLocalId(localId, options);
    ASSERT_TRUE(res.isFinished());
    ASSERT_EQ(res.resultCount(), 1);
    EXPECT_EQ(res.result(), resource);
}

TEST_F(LocalStorageTest, ForwardFindResourceByGuidToResourcesHandler)
{
    const auto localStorage = createLocalStorage();

    auto resource = qevercloud::Resource{};
    resource.setGuid(UidGenerator::Generate());

    const auto guid = resource.guid().value();

    const auto options = ILocalStorage::FetchResourceOptions{
        ILocalStorage::FetchResourceOption::WithBinaryData};

    EXPECT_CALL(*m_mockResourcesHandler, findResourceByGuid(guid, options))
        .WillOnce(
            Return(threading::makeReadyFuture(std::make_optional(resource))));

    const auto res = localStorage->findResourceByGuid(guid, options);
    ASSERT_TRUE(res.isFinished());
    ASSERT_EQ(res.resultCount(), 1);
    EXPECT_EQ(res.result(), resource);
}

TEST_F(LocalStorageTest, ForwardExpungeResourceByLocalIdToResourcesHandler)
{
    const auto localStorage = createLocalStorage();

    const auto localId = UidGenerator::Generate();
    EXPECT_CALL(*m_mockResourcesHandler, expungeResourceByLocalId(localId))
        .WillOnce(Return(threading::makeReadyFuture()));

    const auto res = localStorage->expungeResourceByLocalId(localId);
    ASSERT_TRUE(res.isFinished());
}

TEST_F(LocalStorageTest, ForwardExpungeResourceByGuidToResourcesHandler)
{
    const auto localStorage = createLocalStorage();

    const auto guid = UidGenerator::Generate();
    EXPECT_CALL(*m_mockResourcesHandler, expungeResourceByGuid(guid))
        .WillOnce(Return(threading::makeReadyFuture()));

    const auto res = localStorage->expungeResourceByGuid(guid);
    ASSERT_TRUE(res.isFinished());
}

TEST_F(LocalStorageTest, ForwardSavedSearchCountToSavedSearchesHandler)
{
    const auto localStorage = createLocalStorage();

    const quint32 savedSearchCount = 15;
    EXPECT_CALL(*m_mockSavedSearchesHandler, savedSearchCount)
        .WillOnce(Return(threading::makeReadyFuture(savedSearchCount)));

    const auto res = localStorage->savedSearchCount();
    ASSERT_TRUE(res.isFinished());
    ASSERT_EQ(res.resultCount(), 1);
    EXPECT_EQ(res.result(), savedSearchCount);
}

TEST_F(LocalStorageTest, ForwardPutSavedSearchToSavedSearchesHandler)
{
    const auto localStorage = createLocalStorage();

    const auto savedSearch = qevercloud::SavedSearch{};
    EXPECT_CALL(*m_mockSavedSearchesHandler, putSavedSearch(savedSearch))
        .WillOnce(Return(threading::makeReadyFuture(savedSearch)));

    const auto res = localStorage->putSavedSearch(savedSearch);
    ASSERT_TRUE(res.isFinished());
}

TEST_F(LocalStorageTest, ForwardFindSavedSearchByLocalIdToSavedSearchesHandler)
{
    const auto localStorage = createLocalStorage();

    const auto savedSearch = qevercloud::SavedSearch{};
    const auto & localId = savedSearch.localId();

    EXPECT_CALL(*m_mockSavedSearchesHandler, findSavedSearchByLocalId(localId))
        .WillOnce(Return(
            threading::makeReadyFuture(std::make_optional(savedSearch))));

    const auto res = localStorage->findSavedSearchByLocalId(localId);
    ASSERT_TRUE(res.isFinished());
    ASSERT_EQ(res.resultCount(), 1);
    EXPECT_EQ(res.result(), savedSearch);
}

TEST_F(LocalStorageTest, ForwardFindSavedSearchByGuidToSavedSearchesHandler)
{
    const auto localStorage = createLocalStorage();

    auto savedSearch = qevercloud::SavedSearch{};
    savedSearch.setGuid(UidGenerator::Generate());

    const auto guid = savedSearch.guid().value();

    EXPECT_CALL(*m_mockSavedSearchesHandler, findSavedSearchByGuid(guid))
        .WillOnce(Return(
            threading::makeReadyFuture(std::make_optional(savedSearch))));

    const auto res = localStorage->findSavedSearchByGuid(guid);
    ASSERT_TRUE(res.isFinished());
    ASSERT_EQ(res.resultCount(), 1);
    EXPECT_EQ(res.result(), savedSearch);
}

TEST_F(LocalStorageTest, ForwardFindSavedSearchByNameToSavedSearchesHandler)
{
    const auto localStorage = createLocalStorage();

    auto savedSearch = qevercloud::SavedSearch{};
    savedSearch.setName(QStringLiteral("Saved search"));

    const auto name = savedSearch.name().value();

    EXPECT_CALL(*m_mockSavedSearchesHandler, findSavedSearchByName(name))
        .WillOnce(Return(
            threading::makeReadyFuture(std::make_optional(savedSearch))));

    const auto res = localStorage->findSavedSearchByName(name);
    ASSERT_TRUE(res.isFinished());
    ASSERT_EQ(res.resultCount(), 1);
    EXPECT_EQ(res.result(), savedSearch);
}

TEST_F(LocalStorageTest, ForwardListSavedSearchesToSavedSearchesHandler)
{
    const auto localStorage = createLocalStorage();

    const auto savedSearches = QList<qevercloud::SavedSearch>{}
        << qevercloud::SavedSearch{};

    const auto listOptions = ILocalStorage::ListSavedSearchesOptions{};
    EXPECT_CALL(*m_mockSavedSearchesHandler, listSavedSearches(listOptions))
        .WillOnce(Return(threading::makeReadyFuture(savedSearches)));

    const auto res = localStorage->listSavedSearches(listOptions);
    ASSERT_TRUE(res.isFinished());
    ASSERT_EQ(res.resultCount(), 1);
    EXPECT_EQ(res.result(), savedSearches);
}

TEST_F(
    LocalStorageTest, ForwardExpungeSavedSearchByLocalIdToSavedSearchesHandler)
{
    const auto localStorage = createLocalStorage();

    const auto localId = UidGenerator::Generate();
    EXPECT_CALL(
        *m_mockSavedSearchesHandler, expungeSavedSearchByLocalId(localId))
        .WillOnce(Return(threading::makeReadyFuture()));

    const auto res = localStorage->expungeSavedSearchByLocalId(localId);
    ASSERT_TRUE(res.isFinished());
}

TEST_F(LocalStorageTest, ForwardExpungeSavedSearchByGuidToSavedSearchesHandler)
{
    const auto localStorage = createLocalStorage();

    const auto guid = UidGenerator::Generate();
    EXPECT_CALL(*m_mockSavedSearchesHandler, expungeSavedSearchByGuid(guid))
        .WillOnce(Return(threading::makeReadyFuture()));

    const auto res = localStorage->expungeSavedSearchByGuid(guid);
    ASSERT_TRUE(res.isFinished());
}

TEST_F(
    LocalStorageTest,
    ForwardHighestUpdateSequenceNumberForUserOwnAccountToSynchronizationInfoHandler)
{
    const auto localStorage = createLocalStorage();

    const qint32 usn = 42;
    const auto option = ILocalStorage::HighestUsnOption::WithinUserOwnContent;

    EXPECT_CALL(
        *m_mockSynchronizationInfoHandler, highestUpdateSequenceNumber(option))
        .WillOnce(Return(threading::makeReadyFuture(usn)));

    const auto res = localStorage->highestUpdateSequenceNumber(option);
    ASSERT_TRUE(res.isFinished());
    ASSERT_EQ(res.resultCount(), 1);
    EXPECT_EQ(res.result(), usn);
}

TEST_F(
    LocalStorageTest,
    ForwardHighestUpdateSequenceNumberForLinkedNotebooksToSynchronizationInfoHandler)
{
    const auto localStorage = createLocalStorage();

    const qint32 usn = 43;
    const auto linkedNotebookGuid = UidGenerator::Generate();

    EXPECT_CALL(
        *m_mockSynchronizationInfoHandler,
        highestUpdateSequenceNumber(linkedNotebookGuid))
        .WillOnce(Return(threading::makeReadyFuture(usn)));

    const auto res =
        localStorage->highestUpdateSequenceNumber(linkedNotebookGuid);

    ASSERT_TRUE(res.isFinished());
    ASSERT_EQ(res.resultCount(), 1);
    EXPECT_EQ(res.result(), usn);
}

TEST_F(LocalStorageTest, ReturnNotifierPassedInConstructor)
{
    const auto localStorage = createLocalStorage();
    EXPECT_EQ(localStorage->notifier(), m_notifier.get());
}

} // namespace quentier::local_storage::sql::tests
