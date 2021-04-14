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

#include <quentier/exception/DatabaseRequestException.h>

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QEventLoop>
#include <QMutex>
#include <QSemaphore>
#include <QSqlDatabase>
#include <QThread>
#include <QWaitCondition>

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstddef>

namespace quentier::local_storage::tests {

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
        DatabaseRequestException);
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
    QMutex threadMutex;
    QWaitCondition threadWaitCond;

    QMutex mainMutex;
    QWaitCondition mainWaitCond;

    const auto pool = std::make_shared<ConnectionPool>(
        QStringLiteral("localhost"), QStringLiteral("user"),
        QStringLiteral("password"), QStringLiteral("database"),
        QStringLiteral("QSQLITE"));

    const std::size_t threadCount = 3;
    QSemaphore threadSemaphore{static_cast<int>(threadCount)};

    const auto threadFunc = [&]
    {
        Q_UNUSED(pool->database());
        threadSemaphore.acquire();

        mainWaitCond.notify_one();

        threadMutex.lock();
        EXPECT_TRUE(threadWaitCond.wait(&threadMutex));

        threadMutex.unlock();
    };

    std::array<QThread*, threadCount> threads;
    for (std::size_t i = 0; i < threadCount; ++i)
    {
        threads[i] = QThread::create(threadFunc);
        threads[i]->start();
    }

    // Waiting for all threads to establish the connection
    for (;;) {
        if (threadSemaphore.available() == 0) {
            const auto connectionNames = QSqlDatabase::connectionNames();
            EXPECT_EQ(connectionNames.size(), static_cast<int>(threadCount));

            threadWaitCond.notify_all();
            break;
        }

        mainMutex.lock();
        mainWaitCond.wait(&mainMutex);
        mainMutex.unlock();
    }

    // Wait for all threads to finish
    for (std::size_t i = 0; i < threadCount; ++i)
    {
        threads[i]->wait();
    }

    // Give lambdas connected to threads finished signal a chance to fire
    QCoreApplication::processEvents();

    // Wait for database connections to close
    const int maxIterations = 100;
    for (int i = 0; i < maxIterations; ++i)
    {
        const auto connectionNames = QSqlDatabase::connectionNames();
        if (connectionNames.isEmpty()) {
            break;
        }

        mainMutex.lock();
        mainWaitCond.wait(&mainMutex, QDeadlineTimer{std::chrono::milliseconds(100)});
        mainMutex.unlock();

        QCoreApplication::processEvents();
    }

    const auto connectionNames = QSqlDatabase::connectionNames();
    EXPECT_TRUE(connectionNames.isEmpty());
}

TEST(ConnectionPoolTest, RemoveConnectionsInDestructor)
{
    QMutex threadMutex;
    QWaitCondition threadWaitCond;

    QMutex mainMutex;
    QWaitCondition mainWaitCond;

    auto pool = std::make_shared<ConnectionPool>(
        QStringLiteral("localhost"), QStringLiteral("user"),
        QStringLiteral("password"), QStringLiteral("database"),
        QStringLiteral("QSQLITE"));

    const std::size_t threadCount = 3;
    QSemaphore threadSemaphore{static_cast<int>(threadCount)};

    const auto threadFunc = [&]
    {
        Q_UNUSED(pool->database());
        threadSemaphore.acquire();

        mainWaitCond.notify_one();

        threadMutex.lock();
        EXPECT_TRUE(threadWaitCond.wait(&threadMutex));

        threadMutex.unlock();
    };

    std::array<QThread*, threadCount> threads;
    for (std::size_t i = 0; i < threadCount; ++i)
    {
        threads[i] = QThread::create(threadFunc);
        threads[i]->start();
    }

    // Waiting for all threads to establish the connection
    for (;;) {
        if (threadSemaphore.available() == 0) {
            const auto connectionNames = QSqlDatabase::connectionNames();
            EXPECT_EQ(connectionNames.size(), static_cast<int>(threadCount));
            break;
        }

        mainMutex.lock();
        mainWaitCond.wait(&mainMutex);
        mainMutex.unlock();
    }

    // Now threads are waiting. Destroying the pool and verifying that
    // connections are gone
    pool.reset();

    const auto connectionNames = QSqlDatabase::connectionNames();
    EXPECT_TRUE(connectionNames.isEmpty());

    // Can finish threads now
    threadWaitCond.notify_all();

    // Wait for all threads to finish
    for (std::size_t i = 0; i < threadCount; ++i)
    {
        threads[i]->wait();
    }
}

} // namespace quentier::local_storage::tests
