/*
 * Copyright 2021-2024 Dmitry Ivanov
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

#include "Utils.h"

#include "../ConnectionPool.h"
#include "../LinkedNotebooksHandler.h"
#include "../NotebooksHandler.h"
#include "../NotesHandler.h"
#include "../Notifier.h"
#include "../SavedSearchesHandler.h"
#include "../SynchronizationInfoHandler.h"
#include "../TablesInitializer.h"
#include "../TagsHandler.h"

#include <quentier/exception/IQuentierException.h>
#include <quentier/utility/UidGenerator.h>

#include <qevercloud/types/LinkedNotebook.h>
#include <qevercloud/types/Note.h>
#include <qevercloud/types/Notebook.h>
#include <qevercloud/types/SavedSearch.h>
#include <qevercloud/types/Tag.h>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFlags>
#include <QList>
#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <algorithm>
#include <utility>

// clazy:excludeall=returning-void-expression

namespace quentier::local_storage::sql::tests {

namespace {

[[nodiscard]] QList<qevercloud::LinkedNotebook> createLinkedNotebooks(
    const int count = 3, const qint32 smallestUsn = 0,
    const qint32 smallestIndex = 1)
{
    QList<qevercloud::LinkedNotebook> result;
    result.reserve(std::max(count, 0));
    for (int i = 0; i < count; ++i) {
        qevercloud::LinkedNotebook linkedNotebook;
        linkedNotebook.setGuid(UidGenerator::Generate());

        linkedNotebook.setUri(QStringLiteral("uri"));
        linkedNotebook.setUpdateSequenceNum(smallestUsn + i);
        linkedNotebook.setNoteStoreUrl(QStringLiteral("noteStoreUrl"));
        linkedNotebook.setWebApiUrlPrefix(QStringLiteral("webApiUrlPrefix"));
        linkedNotebook.setUsername(
            QStringLiteral("Linked notebook#") +
            QString::number(smallestIndex + i));

        result << linkedNotebook;
    }
    return result;
}

[[nodiscard]] QList<qevercloud::Notebook> createNotebooks(
    const int count = 3, const qint32 smallestUsn = 0,
    const std::optional<qevercloud::Guid> & linkedNotebookGuid = std::nullopt,
    const qint32 smallestIndex = 1)
{
    QList<qevercloud::Notebook> result;
    result.reserve(std::max(count, 0));
    for (int i = 0; i < count; ++i) {
        qevercloud::Notebook notebook;
        notebook.setGuid(UidGenerator::Generate());
        notebook.setUpdateSequenceNum(smallestUsn + i);
        notebook.setName(
            QStringLiteral("Notebook #") + QString::number(smallestIndex + i));

        notebook.setLinkedNotebookGuid(linkedNotebookGuid);
        result << notebook;
    }
    return result;
};

[[nodiscard]] QList<qevercloud::Tag> createTags(
    const int count = 3, const qint32 smallestUsn = 0,
    const std::optional<qevercloud::Guid> & linkedNotebookGuid = std::nullopt,
    const qint32 smallestIndex = 1)
{
    QList<qevercloud::Tag> result;
    result.reserve(std::max(count, 0));
    for (int i = 0; i < count; ++i) {
        qevercloud::Tag tag;
        tag.setGuid(UidGenerator::Generate());
        tag.setUpdateSequenceNum(smallestUsn + i);
        tag.setName(
            QStringLiteral("Tag #") + QString::number(smallestIndex + i));

        tag.setLinkedNotebookGuid(linkedNotebookGuid);
        result << tag;
    }

    return result;
}

[[nodiscard]] QList<qevercloud::Note> createNotes(
    const QString & notebookLocalId,
    const std::optional<qevercloud::Guid> & notebookGuid, const int count = 3,
    const qint32 smallestUsn = 0, const qint32 smallestIndex = 1)
{
    const auto now = QDateTime::currentMSecsSinceEpoch();
    QList<qevercloud::Note> result;
    result.reserve(std::max(count, 0));
    for (int i = 0; i < count; ++i) {
        qevercloud::Note note;
        note.setLocallyModified(true);
        note.setLocalOnly(false);
        note.setLocallyFavorited(true);
        note.setNotebookLocalId(notebookLocalId);
        note.setNotebookGuid(notebookGuid);
        note.setGuid(UidGenerator::Generate());

        note.setUpdateSequenceNum(smallestUsn + i);

        note.setTitle(
            QStringLiteral("Note #") + QString::number(smallestIndex + i));

        note.setContent(
            QStringLiteral("<en-note><h1>Hello, world</h1></en-note>"));
        note.setContentHash(QCryptographicHash::hash(
            note.content()->toUtf8(), QCryptographicHash::Md5));

        note.setContentLength(note.content()->size());

        note.setCreated(now);
        note.setUpdated(now);

        result << note;
    }
    return result;
}

[[nodiscard]] QList<qevercloud::Resource> createResources(
    const QString & noteLocalId,
    const std::optional<qevercloud::Guid> & noteGuid, const int count = 3,
    const qint32 smallestUsn = 0)
{
    QList<qevercloud::Resource> result;
    result.reserve(std::max(count, 0));
    for (int i = 0; i < count; ++i) {
        qevercloud::Resource resource;
        resource.setLocallyModified(true);

        if (noteGuid) {
            resource.setGuid(UidGenerator::Generate());
            resource.setUpdateSequenceNum(smallestUsn + i);
        }

        resource.setNoteLocalId(noteLocalId);
        resource.setNoteGuid(noteGuid);

        resource.setMime("application/text-plain");

        resource.setWidth(10);
        resource.setHeight(20);

        result << resource;
    }
    return result;
}

[[nodiscard]] QList<qevercloud::SavedSearch> createSavedSearches(
    const int count = 3, const qint32 smallestUsn = 0,
    const qint32 smallestIndex = 1)
{
    QList<qevercloud::SavedSearch> result;
    result.reserve(std::max(count, 0));
    for (int i = 0; i < count; ++i) {
        qevercloud::SavedSearch search;
        search.setGuid(UidGenerator::Generate());
        search.setUpdateSequenceNum(smallestUsn + i);
        search.setName(
            QStringLiteral("Saved search #") +
            QString::number(smallestIndex + i));

        result << search;
    }
    return result;
}

class SynchronizationInfoHandlerTest : public testing::Test
{
protected:
    void SetUp() override
    {
        m_connectionPool = utils::createConnectionPool();

        auto database = m_connectionPool->database();
        TablesInitializer::initializeTables(database);

        m_thread = std::make_shared<QThread>();
        m_notifier = new Notifier;
        m_notifier->moveToThread(m_thread.get());

        QObject::connect(
            m_thread.get(), &QThread::finished, m_notifier,
            &QObject::deleteLater);

        m_thread->start();
    }

    void TearDown() override
    {
        m_thread->quit();
        m_thread->wait();

        // Give lambdas connected to threads finished signal a chance to fire
        QCoreApplication::processEvents();
    }

protected:
    ConnectionPoolPtr m_connectionPool;
    threading::QThreadPtr m_thread;
    QTemporaryDir m_temporaryDir;
    Notifier * m_notifier;
};

} // namespace

TEST_F(SynchronizationInfoHandlerTest, Ctor)
{
    EXPECT_NO_THROW(
        const auto synchronizationInfoHandler =
            std::make_shared<SynchronizationInfoHandler>(
                m_connectionPool, m_thread));
}

TEST_F(SynchronizationInfoHandlerTest, CtorNullConnectionPool)
{
    EXPECT_THROW(
        const auto synchronizationInfoHandler =
            std::make_shared<SynchronizationInfoHandler>(nullptr, m_thread),
        IQuentierException);
}

TEST_F(SynchronizationInfoHandlerTest, CtorNullThread)
{
    EXPECT_THROW(
        const auto synchronizationInfoHandler =
            std::make_shared<SynchronizationInfoHandler>(
                m_connectionPool, nullptr),
        IQuentierException);
}

TEST_F(SynchronizationInfoHandlerTest, InitialUserOwnHighUsnShouldBeZero)
{
    const auto synchronizationInfoHandler =
        std::make_shared<SynchronizationInfoHandler>(
            m_connectionPool, m_thread);

    auto highUsn = synchronizationInfoHandler->highestUpdateSequenceNumber(
        SynchronizationInfoHandler::HighestUsnOption::WithinUserOwnContent);

    highUsn.waitForFinished();
    EXPECT_EQ(highUsn.result(), 0);
}

TEST_F(SynchronizationInfoHandlerTest, InitialOverallHighUsnShouldBeZero)
{
    const auto synchronizationInfoHandler =
        std::make_shared<SynchronizationInfoHandler>(
            m_connectionPool, m_thread);

    auto highUsn = synchronizationInfoHandler->highestUpdateSequenceNumber(
        SynchronizationInfoHandler::HighestUsnOption::
            WithinUserOwnContentAndLinkedNotebooks);

    highUsn.waitForFinished();
    EXPECT_EQ(highUsn.result(), 0);
}

TEST_F(
    SynchronizationInfoHandlerTest,
    InitialHighUsnForNonexistentLinkedNotebookShouldBeZero)
{
    const auto synchronizationInfoHandler =
        std::make_shared<SynchronizationInfoHandler>(
            m_connectionPool, m_thread);

    auto highUsn = synchronizationInfoHandler->highestUpdateSequenceNumber(
        UidGenerator::Generate());

    highUsn.waitForFinished();
    EXPECT_EQ(highUsn.result(), 0);
}

TEST_F(SynchronizationInfoHandlerTest, HighestUsnWithinUserOwnNotebooks)
{
    const auto notebooksHandler = std::make_shared<NotebooksHandler>(
        m_connectionPool, m_notifier, m_thread, m_temporaryDir.path());

    const int notebookCount = 3;
    const qint32 smallestUsn = 42;
    {
        auto notebooks = createNotebooks(notebookCount, smallestUsn);
        for (auto & notebook: notebooks) {
            auto putNotebookFuture =
                notebooksHandler->putNotebook(std::move(notebook));

            putNotebookFuture.waitForFinished();
        }
    }

    const auto synchronizationInfoHandler =
        std::make_shared<SynchronizationInfoHandler>(
            m_connectionPool, m_thread);

    auto highUsn = synchronizationInfoHandler->highestUpdateSequenceNumber(
        SynchronizationInfoHandler::HighestUsnOption::WithinUserOwnContent);

    highUsn.waitForFinished();
    EXPECT_EQ(highUsn.result(), smallestUsn + notebookCount - 1);

    highUsn = synchronizationInfoHandler->highestUpdateSequenceNumber(
        SynchronizationInfoHandler::HighestUsnOption::
            WithinUserOwnContentAndLinkedNotebooks);

    highUsn.waitForFinished();
    EXPECT_EQ(highUsn.result(), smallestUsn + notebookCount - 1);
}

TEST_F(
    SynchronizationInfoHandlerTest, HighestUsnWithinNotebooksFromLinkedNotebook)
{
    const auto notebooksHandler = std::make_shared<NotebooksHandler>(
        m_connectionPool, m_notifier, m_thread, m_temporaryDir.path());

    const int notebookCount = 3;
    const qint32 smallestUsn = 42;
    const qevercloud::Guid linkedNotebookGuid = UidGenerator::Generate();
    {
        const auto linkedNotebooksHandler =
            std::make_shared<LinkedNotebooksHandler>(
                m_connectionPool, m_notifier, m_thread, m_temporaryDir.path());

        qevercloud::LinkedNotebook linkedNotebook;
        linkedNotebook.setGuid(linkedNotebookGuid);

        auto putLinkedNotebookFuture =
            linkedNotebooksHandler->putLinkedNotebook(linkedNotebook);

        putLinkedNotebookFuture.waitForFinished();

        auto notebooks =
            createNotebooks(notebookCount, smallestUsn, linkedNotebookGuid);
        for (auto & notebook: notebooks) {
            auto putNotebookFuture =
                notebooksHandler->putNotebook(std::move(notebook));

            putNotebookFuture.waitForFinished();
        }
    }

    const auto synchronizationInfoHandler =
        std::make_shared<SynchronizationInfoHandler>(
            m_connectionPool, m_thread);

    auto highUsn = synchronizationInfoHandler->highestUpdateSequenceNumber(
        SynchronizationInfoHandler::HighestUsnOption::WithinUserOwnContent);

    highUsn.waitForFinished();
    EXPECT_EQ(highUsn.result(), 0);

    highUsn = synchronizationInfoHandler->highestUpdateSequenceNumber(
        SynchronizationInfoHandler::HighestUsnOption::
            WithinUserOwnContentAndLinkedNotebooks);

    highUsn.waitForFinished();
    EXPECT_EQ(highUsn.result(), smallestUsn + notebookCount - 1);

    highUsn = synchronizationInfoHandler->highestUpdateSequenceNumber(
        linkedNotebookGuid);

    highUsn.waitForFinished();
    EXPECT_EQ(highUsn.result(), smallestUsn + notebookCount - 1);
}

TEST_F(SynchronizationInfoHandlerTest, HighestUsnWithinUserOwnTags)
{
    const auto tagsHandler =
        std::make_shared<TagsHandler>(m_connectionPool, m_notifier, m_thread);

    const int tagCount = 3;
    const qint32 smallestUsn = 42;
    {
        auto tags = createTags(tagCount, smallestUsn);
        for (auto & tag: tags) {
            auto putTagFuture = tagsHandler->putTag(std::move(tag));
            putTagFuture.waitForFinished();
        }
    }

    const auto synchronizationInfoHandler =
        std::make_shared<SynchronizationInfoHandler>(
            m_connectionPool, m_thread);

    auto highUsn = synchronizationInfoHandler->highestUpdateSequenceNumber(
        SynchronizationInfoHandler::HighestUsnOption::WithinUserOwnContent);

    highUsn.waitForFinished();
    EXPECT_EQ(highUsn.result(), smallestUsn + tagCount - 1);

    highUsn = synchronizationInfoHandler->highestUpdateSequenceNumber(
        SynchronizationInfoHandler::HighestUsnOption::
            WithinUserOwnContentAndLinkedNotebooks);

    highUsn.waitForFinished();
    EXPECT_EQ(highUsn.result(), smallestUsn + tagCount - 1);
}

TEST_F(SynchronizationInfoHandlerTest, HighestUsnWithinTagsFromLinkedNotebook)
{
    const auto tagsHandler =
        std::make_shared<TagsHandler>(m_connectionPool, m_notifier, m_thread);

    const int tagCount = 3;
    const qint32 smallestUsn = 42;
    const qevercloud::Guid linkedNotebookGuid = UidGenerator::Generate();
    {
        const auto linkedNotebooksHandler =
            std::make_shared<LinkedNotebooksHandler>(
                m_connectionPool, m_notifier, m_thread, m_temporaryDir.path());

        qevercloud::LinkedNotebook linkedNotebook;
        linkedNotebook.setGuid(linkedNotebookGuid);

        auto putLinkedNotebookFuture =
            linkedNotebooksHandler->putLinkedNotebook(linkedNotebook);

        putLinkedNotebookFuture.waitForFinished();

        auto tags = createTags(tagCount, smallestUsn, linkedNotebookGuid);
        for (auto & tag: tags) {
            auto putTagFuture = tagsHandler->putTag(std::move(tag));
            putTagFuture.waitForFinished();
        }
    }

    const auto synchronizationInfoHandler =
        std::make_shared<SynchronizationInfoHandler>(
            m_connectionPool, m_thread);

    auto highUsn = synchronizationInfoHandler->highestUpdateSequenceNumber(
        SynchronizationInfoHandler::HighestUsnOption::WithinUserOwnContent);

    highUsn.waitForFinished();
    EXPECT_EQ(highUsn.result(), 0);

    highUsn = synchronizationInfoHandler->highestUpdateSequenceNumber(
        SynchronizationInfoHandler::HighestUsnOption::
            WithinUserOwnContentAndLinkedNotebooks);

    highUsn.waitForFinished();
    EXPECT_EQ(highUsn.result(), smallestUsn + tagCount - 1);

    highUsn = synchronizationInfoHandler->highestUpdateSequenceNumber(
        linkedNotebookGuid);

    highUsn.waitForFinished();
    EXPECT_EQ(highUsn.result(), smallestUsn + tagCount - 1);
}

TEST_F(SynchronizationInfoHandlerTest, HighestUsnWithinUserOwnNotes)
{
    const auto notebooksHandler = std::make_shared<NotebooksHandler>(
        m_connectionPool, m_notifier, m_thread, m_temporaryDir.path());

    const int notebookCount = 1;
    const qint32 smallestUsn = 41;
    auto notebooks = createNotebooks(notebookCount, smallestUsn);
    ASSERT_EQ(notebooks.size(), 1);
    const auto & notebook = notebooks[0];

    auto putNotebookFuture = notebooksHandler->putNotebook(notebook);
    putNotebookFuture.waitForFinished();

    const auto notesHandler = std::make_shared<NotesHandler>(
        m_connectionPool, m_notifier, m_thread, m_temporaryDir.path());

    const int noteCount = 3;
    {
        auto notes = createNotes(
            notebook.localId(), notebook.guid(), noteCount, smallestUsn + 1);

        for (auto & note: notes) {
            auto putNoteFuture = notesHandler->putNote(std::move(note));
            putNoteFuture.waitForFinished();
        }
    }

    const auto synchronizationInfoHandler =
        std::make_shared<SynchronizationInfoHandler>(
            m_connectionPool, m_thread);

    auto highUsn = synchronizationInfoHandler->highestUpdateSequenceNumber(
        SynchronizationInfoHandler::HighestUsnOption::WithinUserOwnContent);

    highUsn.waitForFinished();
    EXPECT_EQ(highUsn.result(), smallestUsn + noteCount);

    highUsn = synchronizationInfoHandler->highestUpdateSequenceNumber(
        SynchronizationInfoHandler::HighestUsnOption::
            WithinUserOwnContentAndLinkedNotebooks);

    highUsn.waitForFinished();
    EXPECT_EQ(highUsn.result(), smallestUsn + noteCount);
}

TEST_F(SynchronizationInfoHandlerTest, HighestUsnWithinNotesFromLinkedNotebook)
{
    const auto notebooksHandler = std::make_shared<NotebooksHandler>(
        m_connectionPool, m_notifier, m_thread, m_temporaryDir.path());

    const int notebookCount = 1;
    const qint32 smallestUsn = 41;
    const qevercloud::Guid linkedNotebookGuid = UidGenerator::Generate();
    {
        const auto linkedNotebooksHandler =
            std::make_shared<LinkedNotebooksHandler>(
                m_connectionPool, m_notifier, m_thread, m_temporaryDir.path());

        qevercloud::LinkedNotebook linkedNotebook;
        linkedNotebook.setGuid(linkedNotebookGuid);

        auto putLinkedNotebookFuture =
            linkedNotebooksHandler->putLinkedNotebook(linkedNotebook);

        putLinkedNotebookFuture.waitForFinished();
    }

    auto notebooks =
        createNotebooks(notebookCount, smallestUsn, linkedNotebookGuid);

    ASSERT_EQ(notebooks.size(), 1);
    const auto & notebook = notebooks[0];

    auto putNotebookFuture = notebooksHandler->putNotebook(notebook);
    putNotebookFuture.waitForFinished();

    const auto notesHandler = std::make_shared<NotesHandler>(
        m_connectionPool, m_notifier, m_thread, m_temporaryDir.path());

    const int noteCount = 3;
    {
        auto notes = createNotes(
            notebook.localId(), notebook.guid(), noteCount, smallestUsn + 1);

        for (auto & note: notes) {
            auto putNoteFuture = notesHandler->putNote(std::move(note));
            putNoteFuture.waitForFinished();
        }
    }

    const auto synchronizationInfoHandler =
        std::make_shared<SynchronizationInfoHandler>(
            m_connectionPool, m_thread);

    auto highUsn = synchronizationInfoHandler->highestUpdateSequenceNumber(
        SynchronizationInfoHandler::HighestUsnOption::WithinUserOwnContent);

    highUsn.waitForFinished();
    EXPECT_EQ(highUsn.result(), 0);

    highUsn = synchronizationInfoHandler->highestUpdateSequenceNumber(
        SynchronizationInfoHandler::HighestUsnOption::
            WithinUserOwnContentAndLinkedNotebooks);

    highUsn.waitForFinished();
    EXPECT_EQ(highUsn.result(), smallestUsn + noteCount);

    highUsn = synchronizationInfoHandler->highestUpdateSequenceNumber(
        linkedNotebookGuid);

    highUsn.waitForFinished();
    EXPECT_EQ(highUsn.result(), smallestUsn + noteCount);
}

TEST_F(
    SynchronizationInfoHandlerTest, HighestUsnWithinUserOwnNotesWithResources)
{
    const auto notebooksHandler = std::make_shared<NotebooksHandler>(
        m_connectionPool, m_notifier, m_thread, m_temporaryDir.path());

    const int notebookCount = 1;
    const qint32 smallestUsn = 41;
    auto notebooks = createNotebooks(notebookCount, smallestUsn);
    ASSERT_EQ(notebooks.size(), 1);
    const auto & notebook = notebooks[0];

    auto putNotebookFuture = notebooksHandler->putNotebook(notebook);
    putNotebookFuture.waitForFinished();

    const auto notesHandler = std::make_shared<NotesHandler>(
        m_connectionPool, m_notifier, m_thread, m_temporaryDir.path());

    const int noteCount = 3;
    const int resourcePerNoteCount = 3;
    {
        auto notes = createNotes(
            notebook.localId(), notebook.guid(), noteCount, smallestUsn + 1);

        int i = 0;
        for (auto & note: notes) {
            auto resources = createResources(
                note.localId(), note.guid(), resourcePerNoteCount,
                smallestUsn + 1 + noteCount + i * resourcePerNoteCount);

            ++i;

            note.setResources(resources);

            auto putNoteFuture = notesHandler->putNote(std::move(note));
            putNoteFuture.waitForFinished();
        }
    }

    const auto synchronizationInfoHandler =
        std::make_shared<SynchronizationInfoHandler>(
            m_connectionPool, m_thread);

    auto highUsn = synchronizationInfoHandler->highestUpdateSequenceNumber(
        SynchronizationInfoHandler::HighestUsnOption::WithinUserOwnContent);

    highUsn.waitForFinished();
    EXPECT_EQ(
        highUsn.result(), smallestUsn + noteCount * (1 + resourcePerNoteCount));

    highUsn = synchronizationInfoHandler->highestUpdateSequenceNumber(
        SynchronizationInfoHandler::HighestUsnOption::
            WithinUserOwnContentAndLinkedNotebooks);

    highUsn.waitForFinished();
    EXPECT_EQ(
        highUsn.result(), smallestUsn + noteCount * (1 + resourcePerNoteCount));
}

TEST_F(
    SynchronizationInfoHandlerTest,
    HighestUsnWithinNotesWithResourcesFromLinkedNotebook)
{
    const auto notebooksHandler = std::make_shared<NotebooksHandler>(
        m_connectionPool, m_notifier, m_thread, m_temporaryDir.path());

    const int notebookCount = 1;
    const qint32 smallestUsn = 41;
    const qevercloud::Guid linkedNotebookGuid = UidGenerator::Generate();
    {
        const auto linkedNotebooksHandler =
            std::make_shared<LinkedNotebooksHandler>(
                m_connectionPool, m_notifier, m_thread, m_temporaryDir.path());

        qevercloud::LinkedNotebook linkedNotebook;
        linkedNotebook.setGuid(linkedNotebookGuid);

        auto putLinkedNotebookFuture =
            linkedNotebooksHandler->putLinkedNotebook(linkedNotebook);

        putLinkedNotebookFuture.waitForFinished();
    }

    auto notebooks =
        createNotebooks(notebookCount, smallestUsn, linkedNotebookGuid);

    ASSERT_EQ(notebooks.size(), 1);
    const auto & notebook = notebooks[0];

    auto putNotebookFuture = notebooksHandler->putNotebook(notebook);
    putNotebookFuture.waitForFinished();

    const auto notesHandler = std::make_shared<NotesHandler>(
        m_connectionPool, m_notifier, m_thread, m_temporaryDir.path());

    const int noteCount = 3;
    const int resourcePerNoteCount = 3;
    {
        auto notes = createNotes(
            notebook.localId(), notebook.guid(), noteCount, smallestUsn + 1);

        int i = 0;
        for (auto & note: notes) {
            auto resources = createResources(
                note.localId(), note.guid(), resourcePerNoteCount,
                smallestUsn + 1 + noteCount + i * resourcePerNoteCount);

            ++i;

            note.setResources(resources);

            auto putNoteFuture = notesHandler->putNote(std::move(note));
            putNoteFuture.waitForFinished();
        }
    }

    const auto synchronizationInfoHandler =
        std::make_shared<SynchronizationInfoHandler>(
            m_connectionPool, m_thread);

    auto highUsn = synchronizationInfoHandler->highestUpdateSequenceNumber(
        SynchronizationInfoHandler::HighestUsnOption::WithinUserOwnContent);

    highUsn.waitForFinished();
    EXPECT_EQ(highUsn.result(), 0);

    highUsn = synchronizationInfoHandler->highestUpdateSequenceNumber(
        SynchronizationInfoHandler::HighestUsnOption::
            WithinUserOwnContentAndLinkedNotebooks);

    highUsn.waitForFinished();
    EXPECT_EQ(
        highUsn.result(), smallestUsn + noteCount * (1 + resourcePerNoteCount));

    highUsn = synchronizationInfoHandler->highestUpdateSequenceNumber(
        linkedNotebookGuid);

    highUsn.waitForFinished();
    EXPECT_EQ(
        highUsn.result(), smallestUsn + noteCount * (1 + resourcePerNoteCount));
}

TEST_F(SynchronizationInfoHandlerTest, HighestUsnWithinSavedSearches)
{
    const auto savedSearchesHandler = std::make_shared<SavedSearchesHandler>(
        m_connectionPool, m_notifier, m_thread);

    const int savedSearchCount = 3;
    const qint32 smallestUsn = 42;
    {
        auto savedSearches = createSavedSearches(savedSearchCount, smallestUsn);
        for (auto & savedSearch: savedSearches) {
            auto putSavedSearchFuture =
                savedSearchesHandler->putSavedSearch(savedSearch);

            putSavedSearchFuture.waitForFinished();
        }
    }

    const auto synchronizationInfoHandler =
        std::make_shared<SynchronizationInfoHandler>(
            m_connectionPool, m_thread);

    auto highUsn = synchronizationInfoHandler->highestUpdateSequenceNumber(
        SynchronizationInfoHandler::HighestUsnOption::WithinUserOwnContent);

    highUsn.waitForFinished();
    EXPECT_EQ(highUsn.result(), smallestUsn + savedSearchCount - 1);

    highUsn = synchronizationInfoHandler->highestUpdateSequenceNumber(
        SynchronizationInfoHandler::HighestUsnOption::
            WithinUserOwnContentAndLinkedNotebooks);

    highUsn.waitForFinished();
    EXPECT_EQ(highUsn.result(), smallestUsn + savedSearchCount - 1);
}

TEST_F(SynchronizationInfoHandlerTest, HighestUsnWithinLinkedNotebooks)
{
    const auto linkedNotebooksHandler =
        std::make_shared<LinkedNotebooksHandler>(
            m_connectionPool, m_notifier, m_thread, m_temporaryDir.path());

    const int linkedNotebookCount = 3;
    const qint32 smallestUsn = 42;
    {
        auto linkedNotebooks =
            createLinkedNotebooks(linkedNotebookCount, smallestUsn);

        for (auto & linkedNotebook: linkedNotebooks) {
            auto putLinkedNotebookFuture =
                linkedNotebooksHandler->putLinkedNotebook(
                    std::move(linkedNotebook));

            putLinkedNotebookFuture.waitForFinished();
        }
    }

    const auto synchronizationInfoHandler =
        std::make_shared<SynchronizationInfoHandler>(
            m_connectionPool, m_thread);

    auto highUsn = synchronizationInfoHandler->highestUpdateSequenceNumber(
        SynchronizationInfoHandler::HighestUsnOption::WithinUserOwnContent);

    highUsn.waitForFinished();
    EXPECT_EQ(highUsn.result(), smallestUsn + linkedNotebookCount - 1);

    highUsn = synchronizationInfoHandler->highestUpdateSequenceNumber(
        SynchronizationInfoHandler::HighestUsnOption::
            WithinUserOwnContentAndLinkedNotebooks);

    highUsn.waitForFinished();
    EXPECT_EQ(highUsn.result(), smallestUsn + linkedNotebookCount - 1);
}

TEST_F(SynchronizationInfoHandlerTest, HighestUsnWithinUserOwnAccount)
{
    const auto notebooksHandler = std::make_shared<NotebooksHandler>(
        m_connectionPool, m_notifier, m_thread, m_temporaryDir.path());

    const auto notesHandler = std::make_shared<NotesHandler>(
        m_connectionPool, m_notifier, m_thread, m_temporaryDir.path());

    const int notebookCount = 3;
    const int noteCount = 3;
    const int resourcePerNoteCount = 3;

    qint32 smallestUsn = 42;
    {
        auto notebooks = createNotebooks(notebookCount, smallestUsn);
        for (auto & notebook: notebooks) {
            const auto notebookLocalId = notebook.localId();
            const auto notebookGuid = notebook.guid();

            auto putNotebookFuture =
                notebooksHandler->putNotebook(std::move(notebook));

            putNotebookFuture.waitForFinished();

            auto notes = createNotes(
                notebookLocalId, notebookGuid, noteCount,
                smallestUsn + notebookCount + 1);

            int i = 0;
            for (auto & note: notes) {
                auto resources = createResources(
                    note.localId(), note.guid(), resourcePerNoteCount,
                    smallestUsn + notebookCount + 1 + noteCount +
                        i * resourcePerNoteCount);

                ++i;

                note.setResources(resources);

                auto putNoteFuture = notesHandler->putNote(std::move(note));
                putNoteFuture.waitForFinished();
            }
        }
    }

    smallestUsn += notebookCount + 1 + noteCount * (1 + resourcePerNoteCount);

    const auto savedSearchesHandler = std::make_shared<SavedSearchesHandler>(
        m_connectionPool, m_notifier, m_thread);

    const int savedSearchCount = 3;
    {
        auto savedSearches = createSavedSearches(savedSearchCount, smallestUsn);
        for (auto & savedSearch: savedSearches) {
            auto putSavedSearchFuture =
                savedSearchesHandler->putSavedSearch(savedSearch);

            putSavedSearchFuture.waitForFinished();
        }
    }

    smallestUsn += savedSearchCount;

    const auto tagsHandler =
        std::make_shared<TagsHandler>(m_connectionPool, m_notifier, m_thread);

    const int tagCount = 3;
    {
        auto tags = createTags(tagCount, smallestUsn);
        for (auto & tag: tags) {
            auto putTagFuture = tagsHandler->putTag(std::move(tag));
            putTagFuture.waitForFinished();
        }
    }

    smallestUsn += tagCount;

    const auto linkedNotebooksHandler =
        std::make_shared<LinkedNotebooksHandler>(
            m_connectionPool, m_notifier, m_thread, m_temporaryDir.path());

    const int linkedNotebookCount = 3;
    QStringList linkedNotebookGuids;
    linkedNotebookGuids.reserve(linkedNotebookCount);
    {
        auto linkedNotebooks =
            createLinkedNotebooks(linkedNotebookCount, smallestUsn);

        for (auto & linkedNotebook: linkedNotebooks) {
            linkedNotebookGuids << linkedNotebook.guid().value();

            auto putLinkedNotebookFuture =
                linkedNotebooksHandler->putLinkedNotebook(
                    std::move(linkedNotebook));

            putLinkedNotebookFuture.waitForFinished();
        }
    }

    smallestUsn += linkedNotebookCount;

    const auto synchronizationInfoHandler =
        std::make_shared<SynchronizationInfoHandler>(
            m_connectionPool, m_thread);

    auto highUsn = synchronizationInfoHandler->highestUpdateSequenceNumber(
        SynchronizationInfoHandler::HighestUsnOption::WithinUserOwnContent);

    highUsn.waitForFinished();
    EXPECT_EQ(highUsn.result(), smallestUsn - 1);

    highUsn = synchronizationInfoHandler->highestUpdateSequenceNumber(
        SynchronizationInfoHandler::HighestUsnOption::
            WithinUserOwnContentAndLinkedNotebooks);

    highUsn.waitForFinished();
    EXPECT_EQ(highUsn.result(), smallestUsn - 1);

    for (const auto & linkedNotebookGuid: std::as_const(linkedNotebookGuids)) {
        highUsn = synchronizationInfoHandler->highestUpdateSequenceNumber(
            linkedNotebookGuid);

        highUsn.waitForFinished();
        EXPECT_EQ(highUsn.result(), 0);
    }
}

TEST_F(SynchronizationInfoHandlerTest, HighestUsnWithinLinkedNotebookContent)
{
    const auto linkedNotebooksHandler =
        std::make_shared<LinkedNotebooksHandler>(
            m_connectionPool, m_notifier, m_thread, m_temporaryDir.path());

    const int linkedNotebookCount = 3;
    qint32 smallestUsn = 42;
    QStringList linkedNotebookGuids;
    linkedNotebookGuids.reserve(linkedNotebookCount);
    {
        auto linkedNotebooks =
            createLinkedNotebooks(linkedNotebookCount, smallestUsn);

        for (auto & linkedNotebook: linkedNotebooks) {
            linkedNotebookGuids << linkedNotebook.guid().value();

            auto putLinkedNotebookFuture =
                linkedNotebooksHandler->putLinkedNotebook(
                    std::move(linkedNotebook));

            putLinkedNotebookFuture.waitForFinished();
        }
    }

    smallestUsn += linkedNotebookCount;
    const int userOwnDataSmallestUsn = smallestUsn;

    const auto notebooksHandler = std::make_shared<NotebooksHandler>(
        m_connectionPool, m_notifier, m_thread, m_temporaryDir.path());

    const auto notesHandler = std::make_shared<NotesHandler>(
        m_connectionPool, m_notifier, m_thread, m_temporaryDir.path());

    const auto tagsHandler =
        std::make_shared<TagsHandler>(m_connectionPool, m_notifier, m_thread);

    const int notebookCount = 3;
    const int noteCount = 3;
    const int resourcePerNoteCount = 3;
    const int tagCount = 3;

    for (const auto & linkedNotebookGuid: std::as_const(linkedNotebookGuids)) {
        auto notebooks =
            createNotebooks(notebookCount, smallestUsn, linkedNotebookGuid);

        for (auto & notebook: notebooks) {
            const auto notebookLocalId = notebook.localId();
            const auto notebookGuid = notebook.guid();

            auto putNotebookFuture =
                notebooksHandler->putNotebook(std::move(notebook));

            putNotebookFuture.waitForFinished();

            auto notes = createNotes(
                notebookLocalId, notebookGuid, noteCount,
                smallestUsn + notebookCount + 1);

            int i = 0;
            for (auto & note: notes) {
                auto resources = createResources(
                    note.localId(), note.guid(), resourcePerNoteCount,
                    smallestUsn + notebookCount + 1 + noteCount +
                        i * resourcePerNoteCount);

                ++i;

                note.setResources(resources);

                auto putNoteFuture = notesHandler->putNote(std::move(note));
                putNoteFuture.waitForFinished();
            }
        }

        smallestUsn +=
            notebookCount + 1 + noteCount * (1 + resourcePerNoteCount);

        auto tags = createTags(tagCount, smallestUsn, linkedNotebookGuid);
        for (auto & tag: tags) {
            auto putTagFuture = tagsHandler->putTag(std::move(tag));
            putTagFuture.waitForFinished();
        }

        smallestUsn += tagCount;
    }

    const auto synchronizationInfoHandler =
        std::make_shared<SynchronizationInfoHandler>(
            m_connectionPool, m_thread);

    auto highUsn = synchronizationInfoHandler->highestUpdateSequenceNumber(
        SynchronizationInfoHandler::HighestUsnOption::WithinUserOwnContent);

    highUsn.waitForFinished();
    EXPECT_EQ(highUsn.result(), userOwnDataSmallestUsn - 1);

    highUsn = synchronizationInfoHandler->highestUpdateSequenceNumber(
        SynchronizationInfoHandler::HighestUsnOption::
            WithinUserOwnContentAndLinkedNotebooks);

    highUsn.waitForFinished();
    EXPECT_EQ(highUsn.result(), smallestUsn - 1);
}

} // namespace quentier::local_storage::sql::tests
