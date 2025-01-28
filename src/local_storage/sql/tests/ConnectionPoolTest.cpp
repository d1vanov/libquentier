/*
 * Copyright 2021-2025 Dmitry Ivanov
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

#include <local_storage/sql/tests/mocks/MockISqlDatabaseWrapper.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/local_storage/LocalStorageOpenException.h>

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QFuture>
#include <QFutureSynchronizer>
#include <QMutex>
#include <QSemaphore>
#include <QSqlDatabase>
#include <QThread>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include <quentier/threading/Qt5Promise.h>
#endif

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstddef>

// clazy:excludeall=returning-void-expression

namespace quentier::local_storage::sql::tests {

using testing::Return;
using testing::StrictMock;

class ConnectionPoolTest : public testing::Test
{
protected:
    const std::shared_ptr<mocks::MockISqlDatabaseWrapper>
        m_mockSqlDatabaseWrapper =
            std::make_shared<StrictMock<mocks::MockISqlDatabaseWrapper>>();
};

TEST_F(ConnectionPoolTest, Ctor)
{
    EXPECT_CALL(
        *m_mockSqlDatabaseWrapper, isDriverAvailable(QStringLiteral("QSQLITE")))
        .WillOnce(Return(true));

    EXPECT_NO_THROW(
        const auto pool = std::make_shared<ConnectionPool>(
            m_mockSqlDatabaseWrapper, QStringLiteral("localhost"),
            QStringLiteral("user"), QStringLiteral("password"),
            QStringLiteral("database"), QStringLiteral("QSQLITE")));
}

TEST_F(ConnectionPoolTest, CtorNullSqlDatabaseWrapper)
{
    EXPECT_THROW(
        const auto pool = std::make_shared<ConnectionPool>(
            nullptr, QStringLiteral("localhost"), QStringLiteral("user"),
            QStringLiteral("password"), QStringLiteral("database"),
            QStringLiteral("QSQLITE")),
        InvalidArgument);
}

TEST_F(ConnectionPoolTest, CtorThrowOnMissingSqlDriver)
{
    EXPECT_CALL(
        *m_mockSqlDatabaseWrapper,
        isDriverAvailable(QStringLiteral("NonexistentDatabaseDriver")))
        .WillOnce(Return(false));

    EXPECT_CALL(*m_mockSqlDatabaseWrapper, drivers)
        .WillOnce(Return(QStringList{} << QStringLiteral("QSQLITE")));

    EXPECT_THROW(
        const auto pool = std::make_shared<ConnectionPool>(
            m_mockSqlDatabaseWrapper, QStringLiteral("localhost"),
            QStringLiteral("user"), QStringLiteral("password"),
            QStringLiteral("database"),
            QStringLiteral("NonexistentDatabaseDriver")),
        LocalStorageOpenException);
}

TEST_F(ConnectionPoolTest, CreateConnectionForCurrentThread)
{
    EXPECT_CALL(
        *m_mockSqlDatabaseWrapper, isDriverAvailable(QStringLiteral("QSQLITE")))
        .WillOnce(Return(true));

    const auto pool = std::make_shared<ConnectionPool>(
        m_mockSqlDatabaseWrapper, QStringLiteral("localhost"),
        QStringLiteral("user"), QStringLiteral("password"),
        QStringLiteral("database"), QStringLiteral("QSQLITE"));

    QString connectionName;
    EXPECT_CALL(*m_mockSqlDatabaseWrapper, addDatabase)
        .WillOnce([&](const QString & type, const QString & name) {
            EXPECT_EQ(type, QStringLiteral("QSQLITE"));
            connectionName = name;
            return QSqlDatabase::addDatabase(type, name);
        });

    Q_UNUSED(pool->database());

    EXPECT_CALL(*m_mockSqlDatabaseWrapper, removeDatabase)
        .WillOnce([=](const QString & name) {
            EXPECT_EQ(name, connectionName);
            QSqlDatabase::removeDatabase(name);
        });
}

TEST_F(ConnectionPoolTest, CreateConnectionsForEachThread)
{
    EXPECT_CALL(
        *m_mockSqlDatabaseWrapper, isDriverAvailable(QStringLiteral("QSQLITE")))
        .WillOnce(Return(true));

    const auto pool = std::make_shared<ConnectionPool>(
        m_mockSqlDatabaseWrapper, QStringLiteral("localhost"),
        QStringLiteral("user"), QStringLiteral("password"),
        QStringLiteral("database"), QStringLiteral("QSQLITE"));

    const std::size_t threadCount = 3;
    QSemaphore threadSemaphore{static_cast<int>(threadCount)};

    std::array<QPromise<void>, threadCount> threadReadyPromises;
    for (auto & promise: threadReadyPromises) {
        promise.start();
    }

    std::array<QPromise<void>, threadCount> threadWaitPromises;
    for (auto & promise: threadWaitPromises) {
        promise.start();
    }

    QMutex connectionNamesMutex;
    QSet<QString> connectionNames;

    EXPECT_CALL(*m_mockSqlDatabaseWrapper, addDatabase)
        .Times(threadCount)
        .WillRepeatedly([&](const QString & type, const QString & name) {
            EXPECT_EQ(type, QStringLiteral("QSQLITE"));
            {
                const QMutexLocker locker{&connectionNamesMutex};
                connectionNames.insert(name);
            }
            return QSqlDatabase::addDatabase(type, name);
        });

    EXPECT_CALL(*m_mockSqlDatabaseWrapper, removeDatabase)
        .Times(threadCount)
        .WillRepeatedly([&](const QString & name) {
            {
                const QMutexLocker locker{&connectionNamesMutex};
                const bool removed = connectionNames.remove(name);
                EXPECT_TRUE(removed);
            }

            QSqlDatabase::removeDatabase(name);
        });

    const auto makeThreadFunc = [&](const std::size_t index) {
        return [&, index] {
            Q_UNUSED(pool->database());
            ASSERT_TRUE(threadSemaphore.tryAcquire());

            threadReadyPromises[index].finish();
            threadWaitPromises[index].future().waitForFinished();
        };
    };

    std::array<QThread *, threadCount> threads;
    for (std::size_t i = 0; i < threadCount; ++i) {
        threads[i] = QThread::create(makeThreadFunc(i));
        threads[i]->start();
    }

    // Wait for all threads to create their DB connections
    QFutureSynchronizer<void> threadReadyFutureSynchronizer;
    for (std::size_t i = 0; i < threadCount; ++i) {
        threadReadyFutureSynchronizer.addFuture(
            threadReadyPromises[i].future());
    }

    EXPECT_EQ(
        threadReadyFutureSynchronizer.futures().size(),
        static_cast<int>(threadCount));
    threadReadyFutureSynchronizer.waitForFinished();

    // Now each thread should have its own DB connection
    EXPECT_EQ(threadSemaphore.available(), 0);

    // Check that all expected connections are established
    {
        const QMutexLocker locker{&connectionNamesMutex};
        EXPECT_EQ(connectionNames.size(), static_cast<int>(threadCount));
    }

    // Let the threads finish
    for (std::size_t i = 0; i < threadCount; ++i) {
        threadWaitPromises[i].finish();
        threads[i]->wait();
        threads[i]->deleteLater();
        threads[i] = nullptr;
    }

    // Give lambdas connected to threads finished signal a chance to fire
    QCoreApplication::processEvents();

    // Ensure that all connections were closed
    EXPECT_TRUE(connectionNames.isEmpty());
}

TEST_F(ConnectionPoolTest, RemoveConnectionsInDestructor)
{
    EXPECT_CALL(
        *m_mockSqlDatabaseWrapper, isDriverAvailable(QStringLiteral("QSQLITE")))
        .WillOnce(Return(true));

    auto pool = std::make_shared<ConnectionPool>(
        m_mockSqlDatabaseWrapper, QStringLiteral("localhost"),
        QStringLiteral("user"), QStringLiteral("password"),
        QStringLiteral("database"), QStringLiteral("QSQLITE"));

    const std::size_t threadCount = 3;
    QSemaphore threadSemaphore{static_cast<int>(threadCount)};

    std::array<QPromise<void>, threadCount> threadReadyPromises;
    for (auto & promise: threadReadyPromises) {
        promise.start();
    }

    std::array<QPromise<void>, threadCount> threadWaitPromises;
    for (auto & promise: threadWaitPromises) {
        promise.start();
    }

    QMutex connectionNamesMutex;
    QSet<QString> connectionNames;

    EXPECT_CALL(*m_mockSqlDatabaseWrapper, addDatabase)
        .Times(threadCount)
        .WillRepeatedly([&](const QString & type, const QString & name) {
            EXPECT_EQ(type, QStringLiteral("QSQLITE"));
            {
                const QMutexLocker locker{&connectionNamesMutex};
                connectionNames.insert(name);
            }
            return QSqlDatabase::addDatabase(type, name);
        });

    EXPECT_CALL(*m_mockSqlDatabaseWrapper, removeDatabase)
        .Times(threadCount)
        .WillRepeatedly([&](const QString & name) {
            {
                const QMutexLocker locker{&connectionNamesMutex};
                const bool removed = connectionNames.remove(name);
                EXPECT_TRUE(removed);
            }

            QSqlDatabase::removeDatabase(name);
        });

    const auto makeThreadFunc = [&](const std::size_t index) {
        return [&, index] {
            Q_UNUSED(pool->database());
            ASSERT_TRUE(threadSemaphore.tryAcquire());

            threadReadyPromises[index].finish();
            threadWaitPromises[index].future().waitForFinished();
        };
    };

    std::array<QThread *, threadCount> threads;
    for (std::size_t i = 0; i < threadCount; ++i) {
        threads[i] = QThread::create(makeThreadFunc(i));
        threads[i]->start();
    }

    // Wait for all threads to create their DB connections
    QFutureSynchronizer<void> threadReadyFutureSynchronizer;
    for (std::size_t i = 0; i < threadCount; ++i) {
        threadReadyFutureSynchronizer.addFuture(
            threadReadyPromises[i].future());
    }

    EXPECT_EQ(
        threadReadyFutureSynchronizer.futures().size(),
        static_cast<int>(threadCount));
    threadReadyFutureSynchronizer.waitForFinished();

    // Now each thread should have its own DB connection
    EXPECT_EQ(threadSemaphore.available(), 0);

    // Check that all expected connections are established
    {
        const QMutexLocker locker{&connectionNamesMutex};
        EXPECT_EQ(connectionNames.size(), static_cast<int>(threadCount));
    }

    // Destroying the pool and verifying that connections are gone
    pool.reset();
    EXPECT_TRUE(connectionNames.isEmpty());

    // Can let the threads finish now
    for (std::size_t i = 0; i < threadCount; ++i) {
        threadWaitPromises[i].finish();
    }

    // Wait for all threads to finish
    for (std::size_t i = 0; i < threadCount; ++i) {
        threads[i]->wait();
        threads[i]->deleteLater();
    }
}

} // namespace quentier::local_storage::sql::tests
