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

#include "../NotesHandler.h"
#include "../ConnectionPool.h"
#include "../LinkedNotebooksHandler.h"
#include "../NotebooksHandler.h"
#include "../Notifier.h"
#include "../TablesInitializer.h"

#include <quentier/exception/IQuentierException.h>
#include <quentier/utility/UidGenerator.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QFlags>
#include <QFutureSynchronizer>
#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QThreadPool>

#include <gtest/gtest.h>

#include <array>
#include <iterator>

// clazy:excludeall=non-pod-global-static

namespace quentier::local_storage::sql::tests {

class NotesHandlerTestNotifierListener : public QObject
{
    Q_OBJECT
public:
    explicit NotesHandlerTestNotifierListener(QObject * parent = nullptr) :
        QObject(parent)
    {}

    [[nodiscard]] const QList<qevercloud::Note> & putNotes() const
    {
        return m_putNotes;
    }

    [[nodiscard]] const QStringList & expungedNoteLocalIds() const
    {
        return m_expungedNoteLocalIds;
    }

public Q_SLOTS:
    void onNotePut(qevercloud::Note note) // NOLINT
    {
        m_putNotes << note;
    }

    void onNotebookExpunged(QString noteLocalId) // NOLINT
    {
        m_expungedNoteLocalIds << noteLocalId;
    }

private:
    QList<qevercloud::Note> m_putNotes;
    QStringList m_expungedNoteLocalIds;
};

namespace {

class NotesHandlerTest : public testing::Test
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

TEST_F(NotesHandlerTest, Ctor)
{
    EXPECT_NO_THROW(
        const auto notesHandler = std::make_shared<NotesHandler>(
            m_connectionPool, QThreadPool::globalInstance(), m_notifier,
            m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock));
}

TEST_F(NotesHandlerTest, CtorNullConnectionPool)
{
    EXPECT_THROW(
        const auto notesHandler = std::make_shared<NotesHandler>(
            nullptr, QThreadPool::globalInstance(), m_notifier, m_writerThread,
            m_temporaryDir.path(), m_resourceDataFilesLock),
        IQuentierException);
}

TEST_F(NotesHandlerTest, CtorNullThreadPool)
{
    EXPECT_THROW(
        const auto notesHandler = std::make_shared<NotesHandler>(
            m_connectionPool, nullptr, m_notifier, m_writerThread,
            m_temporaryDir.path(), m_resourceDataFilesLock),
        IQuentierException);
}

TEST_F(NotesHandlerTest, CtorNullNotifier)
{
    EXPECT_THROW(
        const auto notesHandler = std::make_shared<NotesHandler>(
            m_connectionPool, QThreadPool::globalInstance(), nullptr,
            m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock),
        IQuentierException);
}

TEST_F(NotesHandlerTest, CtorNullWriterThread)
{
    EXPECT_THROW(
        const auto notesHandler = std::make_shared<NotesHandler>(
            m_connectionPool, QThreadPool::globalInstance(), m_notifier,
            nullptr, m_temporaryDir.path(), m_resourceDataFilesLock),
        IQuentierException);
}

TEST_F(NotesHandlerTest, CtorNullResourceDataFilesLock)
{
    EXPECT_THROW(
        const auto notesHandler = std::make_shared<NotesHandler>(
            m_connectionPool, QThreadPool::globalInstance(), m_notifier,
            m_writerThread, m_temporaryDir.path(), nullptr),
        IQuentierException);
}

TEST_F(NotesHandlerTest, ShouldHaveZeroNonDeletedNoteCountWhenThereAreNoNotes)
{
    const auto notesHandler = std::make_shared<NotesHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    using NoteCountOption = NotesHandler::NoteCountOption;
    using NoteCountOptions = NotesHandler::NoteCountOptions;

    auto noteCountFuture = notesHandler->noteCount(
        NoteCountOptions{NoteCountOption::IncludeNonDeletedNotes});

    noteCountFuture.waitForFinished();
    EXPECT_EQ(noteCountFuture.result(), 0U);
}

TEST_F(NotesHandlerTest, ShouldHaveZeroDeletedNoteCountWhenThereAreNoNotes)
{
    const auto notesHandler = std::make_shared<NotesHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    using NoteCountOption = NotesHandler::NoteCountOption;
    using NoteCountOptions = NotesHandler::NoteCountOptions;

    auto noteCountFuture = notesHandler->noteCount(
        NoteCountOptions{NoteCountOption::IncludeDeletedNotes});

    noteCountFuture.waitForFinished();
    EXPECT_EQ(noteCountFuture.result(), 0U);
}

TEST_F(NotesHandlerTest, ShouldHaveZeroNoteCountWhenThereAreNoNotes)
{
    const auto notesHandler = std::make_shared<NotesHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    using NoteCountOption = NotesHandler::NoteCountOption;
    using NoteCountOptions = NotesHandler::NoteCountOptions;

    auto noteCountFuture = notesHandler->noteCount(
        NoteCountOptions{NoteCountOption::IncludeDeletedNotes} |
        NoteCountOption::IncludeNonDeletedNotes);

    noteCountFuture.waitForFinished();
    EXPECT_EQ(noteCountFuture.result(), 0U);
}

TEST_F(
    NotesHandlerTest,
    ShouldHaveZeroNonDeletedNoteCountPerNotebookLocalIdWhenThereAreNoNotes)
{
    const auto notesHandler = std::make_shared<NotesHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    using NoteCountOption = NotesHandler::NoteCountOption;
    using NoteCountOptions = NotesHandler::NoteCountOptions;

    auto noteCountFuture = notesHandler->noteCountPerNotebookLocalId(
        UidGenerator::Generate(),
        NoteCountOptions{NoteCountOption::IncludeNonDeletedNotes});

    noteCountFuture.waitForFinished();
    EXPECT_EQ(noteCountFuture.result(), 0U);
}

TEST_F(
    NotesHandlerTest,
    ShouldHaveZeroDeletedNoteCountPerNotebookLocalIdWhenThereAreNoNotes)
{
    const auto notesHandler = std::make_shared<NotesHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    using NoteCountOption = NotesHandler::NoteCountOption;
    using NoteCountOptions = NotesHandler::NoteCountOptions;

    auto noteCountFuture = notesHandler->noteCountPerNotebookLocalId(
        UidGenerator::Generate(),
        NoteCountOptions{NoteCountOption::IncludeDeletedNotes});

    noteCountFuture.waitForFinished();
    EXPECT_EQ(noteCountFuture.result(), 0U);
}

TEST_F(
    NotesHandlerTest,
    ShouldHaveZeroNoteCountPerNotebookLocalIdWhenThereAreNoNotes)
{
    const auto notesHandler = std::make_shared<NotesHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    using NoteCountOption = NotesHandler::NoteCountOption;
    using NoteCountOptions = NotesHandler::NoteCountOptions;

    auto noteCountFuture = notesHandler->noteCountPerNotebookLocalId(
        UidGenerator::Generate(),
        NoteCountOptions{NoteCountOption::IncludeNonDeletedNotes} |
            NoteCountOption::IncludeDeletedNotes);

    noteCountFuture.waitForFinished();
    EXPECT_EQ(noteCountFuture.result(), 0U);
}

TEST_F(
    NotesHandlerTest,
    ShouldHaveZeroNonDeletedNoteCountPerTagLocalIdWhenThereAreNoNotes)
{
    const auto notesHandler = std::make_shared<NotesHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    using NoteCountOption = NotesHandler::NoteCountOption;
    using NoteCountOptions = NotesHandler::NoteCountOptions;

    auto noteCountFuture = notesHandler->noteCountPerTagLocalId(
        UidGenerator::Generate(),
        NoteCountOptions{NoteCountOption::IncludeNonDeletedNotes});

    noteCountFuture.waitForFinished();
    EXPECT_EQ(noteCountFuture.result(), 0U);
}

TEST_F(
    NotesHandlerTest,
    ShouldHaveZeroDeletedNoteCountPerTagLocalIdWhenThereAreNoNotes)
{
    const auto notesHandler = std::make_shared<NotesHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    using NoteCountOption = NotesHandler::NoteCountOption;
    using NoteCountOptions = NotesHandler::NoteCountOptions;

    auto noteCountFuture = notesHandler->noteCountPerTagLocalId(
        UidGenerator::Generate(),
        NoteCountOptions{NoteCountOption::IncludeDeletedNotes});

    noteCountFuture.waitForFinished();
    EXPECT_EQ(noteCountFuture.result(), 0U);
}

TEST_F(
    NotesHandlerTest,
    ShouldHaveZeroNoteCountPerTagLocalIdWhenThereAreNoNotes)
{
    const auto notesHandler = std::make_shared<NotesHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    using NoteCountOption = NotesHandler::NoteCountOption;
    using NoteCountOptions = NotesHandler::NoteCountOptions;

    auto noteCountFuture = notesHandler->noteCountPerTagLocalId(
        UidGenerator::Generate(),
        NoteCountOptions{NoteCountOption::IncludeNonDeletedNotes} |
            NoteCountOption::IncludeDeletedNotes);

    noteCountFuture.waitForFinished();
    EXPECT_EQ(noteCountFuture.result(), 0U);
}

TEST_F(NotesHandlerTest, ShouldHaveZeroNoteCountsPerTagsWhenThereAreNeitherNotesNorTags)
{
    const auto notesHandler = std::make_shared<NotesHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    using NoteCountOption = NotesHandler::NoteCountOption;
    using NoteCountOptions = NotesHandler::NoteCountOptions;

    auto listTagsOptions =
        ILocalStorage::ListOptions<ILocalStorage::ListTagsOrder>{};

    listTagsOptions.m_flags = ILocalStorage::ListObjectsOptions{
        ILocalStorage::ListObjectsOption::ListAll};

    auto noteCountsFuture = notesHandler->noteCountsPerTags(
        listTagsOptions,
        NoteCountOptions{NoteCountOption::IncludeNonDeletedNotes} |
            NoteCountOption::IncludeDeletedNotes);

    noteCountsFuture.waitForFinished();
    EXPECT_EQ(noteCountsFuture.result().size(), 0);
}

} // namespace quentier::local_storage::sql::tests

#include "NotesHandlerTest.moc"
