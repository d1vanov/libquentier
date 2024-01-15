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

#include <quentier/local_storage/LocalStorageOpenException.h>

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QFuture>
#include <QFutureSynchronizer>
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

TEST(ConnectionPoolTest, Ctor)
{
    EXPECT_NO_THROW(const auto pool = std::make_shared<ConnectionPool>(
        QStringLiteral("localhost"), QStringLiteral("user"),
        QStringLiteral("password"), QStringLiteral("database"),
        QStringLiteral("QSQLITE")));
}

TEST(ConnectionPoolTest, CtorThrowOnMissingSqlDriver)
{
    EXPECT_THROW(
        const auto pool = std::make_shared<ConnectionPool>(
            QStringLiteral("localhost"), QStringLiteral("user"),
            QStringLiteral("password"), QStringLiteral("database"),
            QStringLiteral("NonexistentDatabaseDriver")),
        LocalStorageOpenException);
}

TEST(ConnectionPoolTest, CreateConnectionForCurrentThread)
{
    auto connectionNames = QSqlDatabase::connectionNames();
    EXPECT_TRUE(connectionNames.isEmpty());

    const auto pool = std::make_shared<ConnectionPool>(
        QStringLiteral("localhost"), QStringLiteral("user"),
        QStringLiteral("password"), QStringLiteral("database"),
        QStringLiteral("QSQLITE"));

    connectionNames = QSqlDatabase::connectionNames();
    EXPECT_TRUE(connectionNames.isEmpty());

    Q_UNUSED(pool->database());
    connectionNames = QSqlDatabase::connectionNames();
    EXPECT_EQ(connectionNames.size(), 1);
    QSqlDatabase::removeDatabase(connectionNames[0]);
}

TEST(ConnectionPoolTest, CreateConnectionsForEachThread)
{
    const auto pool = std::make_shared<ConnectionPool>(
        QStringLiteral("localhost"), QStringLiteral("user"),
        QStringLiteral("password"), QStringLiteral("database"),
        QStringLiteral("QSQLITE"));

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

    const auto makeThreadFunc = [&](const std::size_t index)
    {
        return [&, index]
        {
            Q_UNUSED(pool->database());
            ASSERT_TRUE(threadSemaphore.tryAcquire());

            threadReadyPromises[index].finish();
            threadWaitPromises[index].future().waitForFinished();
        };
    };

    std::array<QThread*, threadCount> threads;
    for (std::size_t i = 0; i < threadCount; ++i)
    {
        threads[i] = QThread::create(makeThreadFunc(i));
        threads[i]->start();
    }

    // Wait for all threads to create their DB connections
    QFutureSynchronizer<void> threadReadyFutureSynchronizer;
    for (std::size_t i = 0; i < threadCount; ++i) {
        threadReadyFutureSynchronizer.addFuture(threadReadyPromises[i].future());
    }

    EXPECT_EQ(threadReadyFutureSynchronizer.futures().size(), static_cast<int>(threadCount));
    threadReadyFutureSynchronizer.waitForFinished();

    // Now each thread should have its own DB connection
    EXPECT_EQ(threadSemaphore.available(), 0);

    // Wait for database connections to appear in the list of connection names
    // It should really be synchronous but it appears that somewhere in the gory
    // guts of Qt it is actually asynchronous
    const int maxIterations = 500;
    bool gotExpectedConnectionNames = false;
    for (int i = 0; i < maxIterations; ++i)
    {
        auto connectionNames = QSqlDatabase::connectionNames();
        if (connectionNames.size() != static_cast<int>(threadCount)) {
            QThread::msleep(50);
            continue;
        }

        gotExpectedConnectionNames = true;
        break;
    }

    EXPECT_TRUE(gotExpectedConnectionNames);

    // Let the threads finish
    for (std::size_t i = 0; i < threadCount; ++i) {
        threadWaitPromises[i].finish();
        threads[i]->wait();
        threads[i]->deleteLater();
        threads[i] = nullptr;
    }

    // Give lambdas connected to threads finished signal a chance to fire
    QCoreApplication::processEvents();

    // Wait for database connections to close
    for (int i = 0; i < maxIterations; ++i)
    {
        const auto connectionNames = QSqlDatabase::connectionNames();
        if (connectionNames.isEmpty()) {
            break;
        }

        QThread::msleep(100);
        QCoreApplication::processEvents();
    }

    auto connectionNames = QSqlDatabase::connectionNames();
    EXPECT_TRUE(connectionNames.isEmpty());
}

TEST(ConnectionPoolTest, RemoveConnectionsInDestructor)
{
    auto pool = std::make_shared<ConnectionPool>(
        QStringLiteral("localhost"), QStringLiteral("user"),
        QStringLiteral("password"), QStringLiteral("database"),
        QStringLiteral("QSQLITE"));

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

    const auto makeThreadFunc = [&](const std::size_t index)
    {
        return [&, index]
        {
            Q_UNUSED(pool->database());
            ASSERT_TRUE(threadSemaphore.tryAcquire());

            threadReadyPromises[index].finish();
            threadWaitPromises[index].future().waitForFinished();
        };
    };

    std::array<QThread*, threadCount> threads;
    for (std::size_t i = 0; i < threadCount; ++i)
    {
        threads[i] = QThread::create(makeThreadFunc(i));
        threads[i]->start();
    }

    // Wait for all threads to create their DB connections
    QFutureSynchronizer<void> threadReadyFutureSynchronizer;
    for (std::size_t i = 0; i < threadCount; ++i) {
        threadReadyFutureSynchronizer.addFuture(threadReadyPromises[i].future());
    }

    EXPECT_EQ(threadReadyFutureSynchronizer.futures().size(), static_cast<int>(threadCount));
    threadReadyFutureSynchronizer.waitForFinished();

    // Now each thread should have its own DB connection
    EXPECT_EQ(threadSemaphore.available(), 0);

    // Wait for database connections to appear in the list of connection names
    // It should really be synchronous but it appears that somewhere in the gory
    // guts of Qt it is actually asynchronous
    const int maxIterations = 500;
    bool gotExpectedConnectionNames = false;
    for (int i = 0; i < maxIterations; ++i)
    {
        auto connectionNames = QSqlDatabase::connectionNames();
        if (connectionNames.size() != static_cast<int>(threadCount)) {
            QThread::msleep(50);
            continue;
        }

        gotExpectedConnectionNames = true;
        break;
    }

    EXPECT_TRUE(gotExpectedConnectionNames);

    // Destroying the pool and verifying that connections are gone
    pool.reset();

    auto connectionNames = QSqlDatabase::connectionNames();
    EXPECT_TRUE(connectionNames.isEmpty());

    // Can let the threads finish now
    for (std::size_t i = 0; i < threadCount; ++i) {
        threadWaitPromises[i].finish();
    }

    // Wait for all threads to finish
    for (std::size_t i = 0; i < threadCount; ++i)
    {
        threads[i]->wait();
        threads[i]->deleteLater();
    }
}

} // namespace quentier::local_storage::sql::tests
