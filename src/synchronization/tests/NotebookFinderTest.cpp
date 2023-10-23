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

#include <synchronization/NotebookFinder.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/local_storage/tests/mocks/MockILocalStorage.h>
#include <quentier/threading/Future.h>
#include <quentier/utility/UidGenerator.h>

#include <local_storage/sql/Notifier.h>

#include <qevercloud/types/builders/NoteBuilder.h>
#include <qevercloud/types/builders/NotebookBuilder.h>

#include <QCoreApplication>

#include <gtest/gtest.h>

// clazy:excludeall=non-pod-global-static
// clazy:excludeall=returning-void-expression

namespace quentier::synchronization::tests {

using testing::Return;
using testing::StrictMock;

class NotebookFinderTest : public testing::Test
{
protected:
    const std::shared_ptr<local_storage::tests::mocks::MockILocalStorage>
        m_mockLocalStorage = std::make_shared<
            StrictMock<local_storage::tests::mocks::MockILocalStorage>>();
};

TEST_F(NotebookFinderTest, Ctor)
{
    EXPECT_NO_THROW(
        const auto notebookFinder =
            std::make_shared<NotebookFinder>(m_mockLocalStorage));
}

TEST_F(NotebookFinderTest, CtorNullLocalStorage)
{
    EXPECT_THROW(
        const auto notebookFinder = std::make_shared<NotebookFinder>(nullptr),
        InvalidArgument);
}

TEST_F(NotebookFinderTest, FindNotebookByLocalId)
{
    const auto notebookFinder =
        std::make_shared<NotebookFinder>(m_mockLocalStorage);

    local_storage::sql::Notifier notifier;
    EXPECT_CALL(*m_mockLocalStorage, notifier).WillOnce(Return(&notifier));

    notebookFinder->init();

    const auto notebook = qevercloud::NotebookBuilder{}
                              .setLocalId(UidGenerator::Generate())
                              .setName(QStringLiteral("Notebook"))
                              .build();

    EXPECT_CALL(*m_mockLocalStorage, findNotebookByLocalId(notebook.localId()))
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::Notebook>>(
                notebook)));

    auto future = notebookFinder->findNotebookByLocalId(notebook.localId());

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    auto res = future.result();
    ASSERT_TRUE(res);
    EXPECT_EQ(*res, notebook);

    // The next call should not go to local storage but use cached value instead
    future = notebookFinder->findNotebookByLocalId(notebook.localId());

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    res = future.result();
    ASSERT_TRUE(res);
    EXPECT_EQ(*res, notebook);

    // If this notebook gets updated, it should be removed from the
    // cache so the next call would go to local storage again
    notifier.notifyNotebookPut(notebook);
    QCoreApplication::processEvents();

    EXPECT_CALL(*m_mockLocalStorage, findNotebookByLocalId(notebook.localId()))
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::Notebook>>(
                notebook)));

    future = notebookFinder->findNotebookByLocalId(notebook.localId());
    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    res = future.result();
    ASSERT_TRUE(res);
    EXPECT_EQ(*res, notebook);

    // The next call should not go to local storage but use cached value instead
    future = notebookFinder->findNotebookByLocalId(notebook.localId());

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    res = future.result();
    ASSERT_TRUE(res);
    EXPECT_EQ(*res, notebook);

    // If this notebook gets expunged, it should be removed from the
    // cache so the next call would go to local storage again
    notifier.notifyNotebookExpunged(notebook.localId());
    QCoreApplication::processEvents();

    EXPECT_CALL(*m_mockLocalStorage, findNotebookByLocalId(notebook.localId()))
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::Notebook>>(
                std::nullopt)));

    future = notebookFinder->findNotebookByLocalId(notebook.localId());

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    res = future.result();
    EXPECT_FALSE(res);

    // The next call should not go to local storage but use cached value instead
    future = notebookFinder->findNotebookByLocalId(notebook.localId());

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    res = future.result();
    EXPECT_FALSE(res);
}

TEST_F(NotebookFinderTest, FindNoNotebookByLocalId)
{
    const auto notebookFinder =
        std::make_shared<NotebookFinder>(m_mockLocalStorage);

    local_storage::sql::Notifier notifier;
    EXPECT_CALL(*m_mockLocalStorage, notifier).WillOnce(Return(&notifier));

    notebookFinder->init();

    const auto localId = UidGenerator::Generate();

    EXPECT_CALL(*m_mockLocalStorage, findNotebookByLocalId(localId))
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::Notebook>>(
                std::nullopt)));

    auto future = notebookFinder->findNotebookByLocalId(localId);

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    auto res = future.result();
    EXPECT_FALSE(res);

    // The next call should not go to local storage but use cached value instead
    future = notebookFinder->findNotebookByLocalId(localId);

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    res = future.result();
    EXPECT_FALSE(res);

    // If this notebook gets put to local storage, its not found entry
    // should be removed from the cache so the next call would go to local
    // storage again
    const auto notebook = qevercloud::NotebookBuilder{}
                              .setLocalId(localId)
                              .setName(QStringLiteral("Notebook"))
                              .build();
    notifier.notifyNotebookPut(notebook);
    QCoreApplication::processEvents();

    EXPECT_CALL(*m_mockLocalStorage, findNotebookByLocalId(notebook.localId()))
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::Notebook>>(
                notebook)));

    future = notebookFinder->findNotebookByLocalId(notebook.localId());

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    res = future.result();
    ASSERT_TRUE(res);
    EXPECT_EQ(*res, notebook);

    // The next call should not go to local storage but use cached value instead
    future = notebookFinder->findNotebookByLocalId(notebook.localId());

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    res = future.result();
    ASSERT_TRUE(res);
    EXPECT_EQ(*res, notebook);

    // If this notebook gets expunged, it should be removed from the
    // cache so the next call would go to local storage again
    notifier.notifyNotebookExpunged(notebook.localId());
    QCoreApplication::processEvents();

    EXPECT_CALL(*m_mockLocalStorage, findNotebookByLocalId(notebook.localId()))
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::Notebook>>(
                std::nullopt)));

    future = notebookFinder->findNotebookByLocalId(notebook.localId());

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    res = future.result();
    EXPECT_FALSE(res);

    // The next call should not go to local storage but use cached value instead
    future = notebookFinder->findNotebookByLocalId(notebook.localId());

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    res = future.result();
    EXPECT_FALSE(res);
}

TEST_F(NotebookFinderTest, FindNoNotebookByNoteLocalIdForNonexistentNote)
{
    const auto notebookFinder =
        std::make_shared<NotebookFinder>(m_mockLocalStorage);

    local_storage::sql::Notifier notifier;
    EXPECT_CALL(*m_mockLocalStorage, notifier).WillOnce(Return(&notifier));

    notebookFinder->init();

    const auto noteLocalId = UidGenerator::Generate();

    EXPECT_CALL(
        *m_mockLocalStorage,
        findNoteByLocalId(
            noteLocalId, local_storage::ILocalStorage::FetchNoteOptions{}))
        .WillOnce(
            Return(threading::makeReadyFuture<std::optional<qevercloud::Note>>(
                std::nullopt)));

    auto future = notebookFinder->findNotebookByNoteLocalId(noteLocalId);

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    auto res = future.result();
    EXPECT_FALSE(res);

    // The next call should not go to local storage but use cached value instead
    future = notebookFinder->findNotebookByNoteLocalId(noteLocalId);

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    res = future.result();
    EXPECT_FALSE(res);

    const auto notebookLocalId = UidGenerator::Generate();

    const auto note = qevercloud::NoteBuilder{}
                          .setLocalId(noteLocalId)
                          .setNotebookLocalId(notebookLocalId)
                          .build();

    // If this note gets put, the entry should be removed from the
    // cache so the next call would go to local storage again
    notifier.notifyNotePut(note);
    QCoreApplication::processEvents();

    const auto notebook = qevercloud::NotebookBuilder{}
                              .setLocalId(notebookLocalId)
                              .setName(QStringLiteral("Notebook"))
                              .build();

    EXPECT_CALL(
        *m_mockLocalStorage,
        findNoteByLocalId(
            noteLocalId, local_storage::ILocalStorage::FetchNoteOptions{}))
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::Note>>(note)));

    EXPECT_CALL(*m_mockLocalStorage, findNotebookByLocalId(notebookLocalId))
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::Notebook>>(
                notebook)));

    future = notebookFinder->findNotebookByNoteLocalId(note.localId());

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    res = future.result();
    ASSERT_TRUE(res);
    EXPECT_EQ(*res, notebook);

    // The next call should not go to local storage but use cached value instead
    future = notebookFinder->findNotebookByNoteLocalId(note.localId());

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    res = future.result();
    ASSERT_TRUE(res);
    EXPECT_EQ(*res, notebook);

    // If this note gets expunged, the entry should be removed from the
    // cache so the next call would go to local storage again
    notifier.notifyNoteExpunged(note.localId());
    QCoreApplication::processEvents();

    EXPECT_CALL(
        *m_mockLocalStorage,
        findNoteByLocalId(
            note.localId(), local_storage::ILocalStorage::FetchNoteOptions{}))
        .WillOnce(
            Return(threading::makeReadyFuture<std::optional<qevercloud::Note>>(
                std::nullopt)));

    future = notebookFinder->findNotebookByNoteLocalId(note.localId());

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    res = future.result();
    EXPECT_FALSE(res);

    // The next call should not go to local storage but use cached value instead
    future = notebookFinder->findNotebookByNoteLocalId(note.localId());

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    res = future.result();
    EXPECT_FALSE(res);
}

TEST_F(NotebookFinderTest, FindNotebookByNoteLocalId)
{
    const auto notebookFinder =
        std::make_shared<NotebookFinder>(m_mockLocalStorage);

    local_storage::sql::Notifier notifier;
    EXPECT_CALL(*m_mockLocalStorage, notifier).WillOnce(Return(&notifier));

    notebookFinder->init();

    const auto notebook = qevercloud::NotebookBuilder{}
                              .setGuid(UidGenerator::Generate())
                              .setLocalId(UidGenerator::Generate())
                              .setName(QStringLiteral("Notebook"))
                              .build();

    const auto note = qevercloud::NoteBuilder{}
                          .setLocalId(UidGenerator::Generate())
                          .setNotebookLocalId(notebook.localId())
                          .setTitle(QStringLiteral("Note"))
                          .build();

    EXPECT_CALL(
        *m_mockLocalStorage,
        findNoteByLocalId(
            note.localId(), local_storage::ILocalStorage::FetchNoteOptions{}))
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::Note>>(note)));

    EXPECT_CALL(*m_mockLocalStorage, findNotebookByLocalId(notebook.localId()))
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::Notebook>>(
                notebook)));

    auto future = notebookFinder->findNotebookByNoteLocalId(note.localId());

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    auto res = future.result();
    ASSERT_TRUE(res);
    EXPECT_EQ(*res, notebook);

    // The next call should not go to local storage but use cached value instead
    future = notebookFinder->findNotebookByNoteLocalId(note.localId());

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    res = future.result();
    ASSERT_TRUE(res);
    EXPECT_EQ(*res, notebook);

    // If this note gets updated, the entry should be removed from the
    // cache so the next call would go to local storage again
    notifier.notifyNoteUpdated(note, {});
    QCoreApplication::processEvents();

    EXPECT_CALL(
        *m_mockLocalStorage,
        findNoteByLocalId(
            note.localId(), local_storage::ILocalStorage::FetchNoteOptions{}))
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::Note>>(note)));

    future = notebookFinder->findNotebookByNoteLocalId(note.localId());

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    res = future.result();
    ASSERT_TRUE(res);
    EXPECT_EQ(*res, notebook);

    // The next call should not go to local storage but use cached value instead
    future = notebookFinder->findNotebookByNoteLocalId(note.localId());

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    res = future.result();
    ASSERT_TRUE(res);
    EXPECT_EQ(*res, notebook);

    // If this notebook gets expunged, it should be removed from the
    // cache so the next call would go to local storage again
    notifier.notifyNotebookExpunged(notebook.localId());
    QCoreApplication::processEvents();

    EXPECT_CALL(
        *m_mockLocalStorage,
        findNoteByLocalId(
            note.localId(), local_storage::ILocalStorage::FetchNoteOptions{}))
        .WillOnce(
            Return(threading::makeReadyFuture<std::optional<qevercloud::Note>>(
                std::nullopt)));

    future = notebookFinder->findNotebookByNoteLocalId(note.localId());

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    res = future.result();
    EXPECT_FALSE(res);

    // The next call should not go to local storage but use cached value instead
    future = notebookFinder->findNotebookByNoteLocalId(note.localId());

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    res = future.result();
    EXPECT_FALSE(res);

    // Now imitate the fact that this note was put to local storage again
    // and ensure that the expunging of the notebook would be processed
    // as needed
    notifier.notifyNotePut(note);
    QCoreApplication::processEvents();

    EXPECT_CALL(
        *m_mockLocalStorage,
        findNoteByLocalId(
            note.localId(), local_storage::ILocalStorage::FetchNoteOptions{}))
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::Note>>(note)));

    EXPECT_CALL(*m_mockLocalStorage, findNotebookByLocalId(notebook.localId()))
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::Notebook>>(
                notebook)));

    future = notebookFinder->findNotebookByNoteLocalId(note.localId());

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    res = future.result();
    ASSERT_TRUE(res);
    EXPECT_EQ(*res, notebook);

    // If this notebook gets expunged, the entry should be removed from the
    // cache so the next call would go to local storage again
    notifier.notifyNotebookExpunged(notebook.localId());
    QCoreApplication::processEvents();

    EXPECT_CALL(
        *m_mockLocalStorage,
        findNoteByLocalId(
            note.localId(), local_storage::ILocalStorage::FetchNoteOptions{}))
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::Note>>(note)));

    EXPECT_CALL(*m_mockLocalStorage, findNotebookByLocalId(notebook.localId()))
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::Notebook>>(
                std::nullopt)));

    future = notebookFinder->findNotebookByNoteLocalId(note.localId());

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    res = future.result();
    EXPECT_FALSE(res);

    // The next call should not go to local storage but use cached value instead
    future = notebookFinder->findNotebookByNoteLocalId(note.localId());

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    res = future.result();
    EXPECT_FALSE(res);
}

TEST_F(NotebookFinderTest, FindNoNotebookByNoteGuidForNonexistentNote)
{
    const auto notebookFinder =
        std::make_shared<NotebookFinder>(m_mockLocalStorage);

    local_storage::sql::Notifier notifier;
    EXPECT_CALL(*m_mockLocalStorage, notifier).WillOnce(Return(&notifier));

    notebookFinder->init();

    const auto noteGuid = UidGenerator::Generate();

    EXPECT_CALL(
        *m_mockLocalStorage,
        findNoteByGuid(
            noteGuid, local_storage::ILocalStorage::FetchNoteOptions{}))
        .WillOnce(
            Return(threading::makeReadyFuture<std::optional<qevercloud::Note>>(
                std::nullopt)));

    auto future = notebookFinder->findNotebookByNoteGuid(noteGuid);

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    auto res = future.result();
    EXPECT_FALSE(res);

    // The next call should not go to local storage but use cached value instead
    future = notebookFinder->findNotebookByNoteGuid(noteGuid);

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    res = future.result();
    EXPECT_FALSE(res);

    const auto notebookLocalId = UidGenerator::Generate();

    const auto note = qevercloud::NoteBuilder{}
                          .setGuid(noteGuid)
                          .setNotebookLocalId(notebookLocalId)
                          .build();

    // If this note gets put, the entry should be removed from the
    // cache so the next call would go to local storage again
    notifier.notifyNotePut(note);
    QCoreApplication::processEvents();

    const auto notebook = qevercloud::NotebookBuilder{}
                              .setLocalId(notebookLocalId)
                              .setName(QStringLiteral("Notebook"))
                              .build();

    EXPECT_CALL(
        *m_mockLocalStorage,
        findNoteByGuid(
            noteGuid, local_storage::ILocalStorage::FetchNoteOptions{}))
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::Note>>(note)));

    EXPECT_CALL(*m_mockLocalStorage, findNotebookByLocalId(notebookLocalId))
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::Notebook>>(
                notebook)));

    future = notebookFinder->findNotebookByNoteGuid(note.guid().value());

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    res = future.result();
    ASSERT_TRUE(res);
    EXPECT_EQ(*res, notebook);

    // The next call should not go to local storage but use cached value instead
    future = notebookFinder->findNotebookByNoteGuid(note.guid().value());

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    res = future.result();
    ASSERT_TRUE(res);
    EXPECT_EQ(*res, notebook);

    // If this note gets expunged, the entry should be removed from the
    // cache so the next call would go to local storage again
    notifier.notifyNoteExpunged(note.localId());
    QCoreApplication::processEvents();

    EXPECT_CALL(
        *m_mockLocalStorage,
        findNoteByGuid(
            note.guid().value(),
            local_storage::ILocalStorage::FetchNoteOptions{}))
        .WillOnce(
            Return(threading::makeReadyFuture<std::optional<qevercloud::Note>>(
                std::nullopt)));

    future = notebookFinder->findNotebookByNoteGuid(note.guid().value());

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    res = future.result();
    EXPECT_FALSE(res);

    // The next call should not go to local storage but use cached value instead
    future = notebookFinder->findNotebookByNoteGuid(note.guid().value());

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    res = future.result();
    EXPECT_FALSE(res);
}

TEST_F(NotebookFinderTest, FindNotebookByNoteGuid)
{
    const auto notebookFinder =
        std::make_shared<NotebookFinder>(m_mockLocalStorage);

    local_storage::sql::Notifier notifier;
    EXPECT_CALL(*m_mockLocalStorage, notifier).WillOnce(Return(&notifier));

    notebookFinder->init();

    const auto notebook = qevercloud::NotebookBuilder{}
                              .setGuid(UidGenerator::Generate())
                              .setLocalId(UidGenerator::Generate())
                              .setName(QStringLiteral("Notebook"))
                              .build();

    const auto note = qevercloud::NoteBuilder{}
                          .setGuid(UidGenerator::Generate())
                          .setNotebookLocalId(notebook.localId())
                          .setTitle(QStringLiteral("Note"))
                          .build();

    EXPECT_CALL(
        *m_mockLocalStorage,
        findNoteByGuid(
            note.guid().value(),
            local_storage::ILocalStorage::FetchNoteOptions{}))
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::Note>>(note)));

    EXPECT_CALL(*m_mockLocalStorage, findNotebookByLocalId(notebook.localId()))
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::Notebook>>(
                notebook)));

    auto future = notebookFinder->findNotebookByNoteGuid(note.guid().value());

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    auto res = future.result();
    ASSERT_TRUE(res);
    EXPECT_EQ(*res, notebook);

    // The next call should not go to local storage but use cached value instead
    future = notebookFinder->findNotebookByNoteGuid(note.guid().value());

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    res = future.result();
    ASSERT_TRUE(res);
    EXPECT_EQ(*res, notebook);

    // If this note gets updated, the entry should be removed from the
    // cache so the next call would go to local storage again
    notifier.notifyNoteUpdated(note, {});
    QCoreApplication::processEvents();

    EXPECT_CALL(
        *m_mockLocalStorage,
        findNoteByGuid(
            note.guid().value(),
            local_storage::ILocalStorage::FetchNoteOptions{}))
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::Note>>(note)));

    future = notebookFinder->findNotebookByNoteGuid(note.guid().value());

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    res = future.result();
    ASSERT_TRUE(res);
    EXPECT_EQ(*res, notebook);

    // The next call should not go to local storage but use cached value instead
    future = notebookFinder->findNotebookByNoteGuid(note.guid().value());

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    res = future.result();
    ASSERT_TRUE(res);
    EXPECT_EQ(*res, notebook);

    // If this notebook gets expunged, it should be removed from the
    // cache so the next call would go to local storage again
    notifier.notifyNotebookExpunged(notebook.localId());
    QCoreApplication::processEvents();

    EXPECT_CALL(
        *m_mockLocalStorage,
        findNoteByGuid(
            note.guid().value(),
            local_storage::ILocalStorage::FetchNoteOptions{}))
        .WillOnce(
            Return(threading::makeReadyFuture<std::optional<qevercloud::Note>>(
                std::nullopt)));

    future = notebookFinder->findNotebookByNoteGuid(note.guid().value());

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    res = future.result();
    EXPECT_FALSE(res);

    // The next call should not go to local storage but use cached value instead
    future = notebookFinder->findNotebookByNoteGuid(note.guid().value());

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    res = future.result();
    EXPECT_FALSE(res);

    // Now imitate the fact that this note was put to local storage again
    // and ensure that the expunging of the notebook would be processed
    // as needed
    notifier.notifyNotePut(note);
    QCoreApplication::processEvents();

    EXPECT_CALL(
        *m_mockLocalStorage,
        findNoteByGuid(
            note.guid().value(),
            local_storage::ILocalStorage::FetchNoteOptions{}))
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::Note>>(note)));

    EXPECT_CALL(*m_mockLocalStorage, findNotebookByLocalId(notebook.localId()))
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::Notebook>>(
                notebook)));

    future = notebookFinder->findNotebookByNoteGuid(note.guid().value());

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    res = future.result();
    ASSERT_TRUE(res);
    EXPECT_EQ(*res, notebook);

    // If this notebook gets expunged, the entry should be removed from the
    // cache so the next call would go to local storage again
    notifier.notifyNotebookExpunged(notebook.localId());
    QCoreApplication::processEvents();

    EXPECT_CALL(
        *m_mockLocalStorage,
        findNoteByGuid(
            note.guid().value(),
            local_storage::ILocalStorage::FetchNoteOptions{}))
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::Note>>(note)));

    EXPECT_CALL(*m_mockLocalStorage, findNotebookByLocalId(notebook.localId()))
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::Notebook>>(
                std::nullopt)));

    future = notebookFinder->findNotebookByNoteGuid(note.guid().value());

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    res = future.result();
    EXPECT_FALSE(res);

    // The next call should not go to local storage but use cached value instead
    future = notebookFinder->findNotebookByNoteGuid(note.guid().value());

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    res = future.result();
    EXPECT_FALSE(res);
}

} // namespace quentier::synchronization::tests
