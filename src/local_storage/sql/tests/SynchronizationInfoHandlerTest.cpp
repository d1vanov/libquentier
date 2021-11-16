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

namespace quentier::local_storage::sql::tests {

namespace {

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

} // namespace quentier::local_storage::sql::tests
