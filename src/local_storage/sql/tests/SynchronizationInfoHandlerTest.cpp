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
#include "../Notifier.h"
#include "../TablesInitializer.h"

#include <quentier/exception/IQuentierException.h>
#include <quentier/utility/UidGenerator.h>

#include <qevercloud/types/Notebook.h>

#include <QCoreApplication>
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

namespace quentier::local_storage::sql::tests {

namespace {

[[nodiscard]] QList<qevercloud::Notebook> createNotebooks(
    const int count = 3, const qint32 smallestUsn = 0,
    const std::optional<qevercloud::Guid> & linkedNotebookGuid = std::nullopt,
    const qint32 smallestIndex = 1)
{
    QList<qevercloud::Notebook> result;
    result.reserve(std::max(count, 0));
    for (int i = 0; i < count; ++i)
    {
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
            m_writerThread.get(),
            &QThread::finished,
            m_notifier,
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

TEST_F(SynchronizationInfoHandlerTest, HighestUsnWithinNotebooksFromLinkedNotebook)
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

} // namespace quentier::local_storage::sql::tests
