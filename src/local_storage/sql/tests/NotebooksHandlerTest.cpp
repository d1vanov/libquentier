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

#include "../ConnectionPool.h"
#include "../TablesInitializer.h"
#include "../NotebooksHandler.h"

#include <quentier/exception/IQuentierException.h>

#include <QCoreApplication>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QThreadPool>

#include <gtest/gtest.h>

namespace quentier::local_storage::sql::tests {

namespace {

class NotebooksHandlerTest : public testing::Test
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
    QTemporaryDir m_temporaryDir;
};

} // namespace

TEST_F(NotebooksHandlerTest, Ctor)
{
    EXPECT_NO_THROW(
        const auto notebooksHandler = std::make_shared<NotebooksHandler>(
            m_connectionPool, QThreadPool::globalInstance(), m_writerThread,
            m_temporaryDir.path()));
}

TEST_F(NotebooksHandlerTest, CtorNullConnectionPool)
{
    EXPECT_THROW(
        const auto notebooksHandler = std::make_shared<NotebooksHandler>(
            nullptr, QThreadPool::globalInstance(), m_writerThread,
            m_temporaryDir.path()),
        IQuentierException);
}

TEST_F(NotebooksHandlerTest, CtorNullThreadPool)
{
    EXPECT_THROW(
        const auto notebooksHandler = std::make_shared<NotebooksHandler>(
            m_connectionPool, nullptr, m_writerThread,
            m_temporaryDir.path()),
        IQuentierException);
}

TEST_F(NotebooksHandlerTest, CtorNullWriterThread)
{
    EXPECT_THROW(
        const auto notebooksHandler = std::make_shared<NotebooksHandler>(
            m_connectionPool, QThreadPool::globalInstance(), nullptr,
            m_temporaryDir.path()),
        IQuentierException);
}

} // namespace quentier::local_storage::sql::tests
