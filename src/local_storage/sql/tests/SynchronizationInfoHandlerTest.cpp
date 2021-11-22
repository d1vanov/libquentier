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

#include "../SynchronizationInfoHandler.h"
#include "../ConnectionPool.h"
#include "../LinkedNotebooksHandler.h"
#include "../NotebooksHandler.h"
#include "../NotesHandler.h"
#include "../Notifier.h"
#include "../TablesInitializer.h"
#include "../TagsHandler.h"

#include <quentier/exception/IQuentierException.h>
#include <quentier/utility/UidGenerator.h>

#include <qevercloud/types/Note.h>
#include <qevercloud/types/Notebook.h>
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
#include <QThreadPool>

#include <gtest/gtest.h>

#include <algorithm>

// clazy:excludeall=returning-void-expression

namespace quentier::local_storage::sql::tests {

namespace {

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

class SynchronizationInfoHandlerTest : public testing::Test
{
protected:
    void SetUp() override
    {
        m_connectionPool = std::make_shared<ConnectionPool>(
            QStringLiteral("localhost"), QStringLiteral("user"),
            QStringLiteral("password"), QStringLiteral("file::memory:"),
            QStringLiteral("QSQLITE"),
            QStringLiteral("QSQLITE_OPEN_URI;QSQLITE_ENABLE_SHARED_CACHE"));

        auto database = m_connectionPool->database();
        TablesInitializer::initializeTables(database);

        m_writerThread = std::make_shared<QThread>();

        m_resourceDataFilesLock = std::make_shared<QReadWriteLock>();

        m_notifier = new Notifier;
        m_notifier->moveToThread(m_writerThread.get());

        QObject::connect(
            m_writerThread.get(), &QThread::finished, m_notifier,
            &QObject::deleteLater);

        m_writerThread->start();
    }

    void TearDown() override
    {
        m_writerThread->quit();
        m_writerThread->wait();

        // Give lambdas connected to threads finished signal a chance to fire
        QCoreApplication::processEvents();
    }

protected:
    ConnectionPoolPtr m_connectionPool;
    QThreadPtr m_writerThread;
    QReadWriteLockPtr m_resourceDataFilesLock;
    QTemporaryDir m_temporaryDir;
    Notifier * m_notifier;
};

} // namespace

TEST_F(SynchronizationInfoHandlerTest, Ctor)
{
    EXPECT_NO_THROW(
        const auto synchronizationInfoHandler =
            std::make_shared<SynchronizationInfoHandler>(
                m_connectionPool, QThreadPool::globalInstance(),
                m_writerThread));
}

TEST_F(SynchronizationInfoHandlerTest, CtorNullConnectionPool)
{
    EXPECT_THROW(
        const auto synchronizationInfoHandler =
            std::make_shared<SynchronizationInfoHandler>(
                nullptr, QThreadPool::globalInstance(), m_writerThread),
        IQuentierException);
}

TEST_F(SynchronizationInfoHandlerTest, CtorNullThreadPool)
{
    EXPECT_THROW(
        const auto synchronizationInfoHandler =
            std::make_shared<SynchronizationInfoHandler>(
                m_connectionPool, nullptr, m_writerThread),
        IQuentierException);
}

TEST_F(SynchronizationInfoHandlerTest, CtorNullWriterThread)
{
    EXPECT_THROW(
        const auto synchronizationInfoHandler =
            std::make_shared<SynchronizationInfoHandler>(
                m_connectionPool, QThreadPool::globalInstance(), nullptr),
        IQuentierException);
}

TEST_F(SynchronizationInfoHandlerTest, InitialUserOwnHighUsnShouldBeZero)
{
    const auto synchronizationInfoHandler =
        std::make_shared<SynchronizationInfoHandler>(
            m_connectionPool, QThreadPool::globalInstance(), m_writerThread);

    auto highUsn = synchronizationInfoHandler->highestUpdateSequenceNumber(
        SynchronizationInfoHandler::HighestUsnOption::WithinUserOwnContent);

    highUsn.waitForFinished();
    EXPECT_EQ(highUsn.result(), 0);
}

TEST_F(SynchronizationInfoHandlerTest, InitialOverallHighUsnShouldBeZero)
{
    const auto synchronizationInfoHandler =
        std::make_shared<SynchronizationInfoHandler>(
            m_connectionPool, QThreadPool::globalInstance(), m_writerThread);

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
            m_connectionPool, QThreadPool::globalInstance(), m_writerThread);

    auto highUsn = synchronizationInfoHandler->highestUpdateSequenceNumber(
        UidGenerator::Generate());

    highUsn.waitForFinished();
    EXPECT_EQ(highUsn.result(), 0);
}

TEST_F(SynchronizationInfoHandlerTest, HighestUsnWithinUserOwnNotebooks)
{
    const auto notebooksHandler = std::make_shared<NotebooksHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

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
            m_connectionPool, QThreadPool::globalInstance(), m_writerThread);

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
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    const int notebookCount = 3;
    const qint32 smallestUsn = 42;
    const qevercloud::Guid linkedNotebookGuid = UidGenerator::Generate();
    {
        const auto linkedNotebooksHandler =
            std::make_shared<LinkedNotebooksHandler>(
                m_connectionPool, QThreadPool::globalInstance(), m_notifier,
                m_writerThread, m_temporaryDir.path());

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
            m_connectionPool, QThreadPool::globalInstance(), m_writerThread);

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
    const auto tagsHandler = std::make_shared<TagsHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread);

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
            m_connectionPool, QThreadPool::globalInstance(), m_writerThread);

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
    const auto tagsHandler = std::make_shared<TagsHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread);

    const int tagCount = 3;
    const qint32 smallestUsn = 42;
    const qevercloud::Guid linkedNotebookGuid = UidGenerator::Generate();
    {
        const auto linkedNotebooksHandler =
            std::make_shared<LinkedNotebooksHandler>(
                m_connectionPool, QThreadPool::globalInstance(), m_notifier,
                m_writerThread, m_temporaryDir.path());

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
            m_connectionPool, QThreadPool::globalInstance(), m_writerThread);

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
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    const int notebookCount = 1;
    const qint32 smallestUsn = 41;
    auto notebooks = createNotebooks(notebookCount, smallestUsn);
    ASSERT_EQ(notebooks.size(), 1);
    const auto & notebook = notebooks[0];

    auto putNotebookFuture = notebooksHandler->putNotebook(notebook);
    putNotebookFuture.waitForFinished();

    const auto notesHandler = std::make_shared<NotesHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

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
            m_connectionPool, QThreadPool::globalInstance(), m_writerThread);

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
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    const int notebookCount = 1;
    const qint32 smallestUsn = 41;
    const qevercloud::Guid linkedNotebookGuid = UidGenerator::Generate();
    {
        const auto linkedNotebooksHandler =
            std::make_shared<LinkedNotebooksHandler>(
                m_connectionPool, QThreadPool::globalInstance(), m_notifier,
                m_writerThread, m_temporaryDir.path());

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
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

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
            m_connectionPool, QThreadPool::globalInstance(), m_writerThread);

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
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    const int notebookCount = 1;
    const qint32 smallestUsn = 41;
    auto notebooks = createNotebooks(notebookCount, smallestUsn);
    ASSERT_EQ(notebooks.size(), 1);
    const auto & notebook = notebooks[0];

    auto putNotebookFuture = notebooksHandler->putNotebook(notebook);
    putNotebookFuture.waitForFinished();

    const auto notesHandler = std::make_shared<NotesHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

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
            m_connectionPool, QThreadPool::globalInstance(), m_writerThread);

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
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    const int notebookCount = 1;
    const qint32 smallestUsn = 41;
    const qevercloud::Guid linkedNotebookGuid = UidGenerator::Generate();
    {
        const auto linkedNotebooksHandler =
            std::make_shared<LinkedNotebooksHandler>(
                m_connectionPool, QThreadPool::globalInstance(), m_notifier,
                m_writerThread, m_temporaryDir.path());

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
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

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
            m_connectionPool, QThreadPool::globalInstance(), m_writerThread);

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

} // namespace quentier::local_storage::sql::tests
