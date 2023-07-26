/*
 * Copyright 2021-2022 Dmitry Ivanov
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
#include "../VersionHandler.h"
#include "../patches/Patch1To2.h"

#include <quentier/exception/IQuentierException.h>

#include <QCoreApplication>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QThreadPool>

#include <gtest/gtest.h>

namespace quentier::local_storage::sql::tests {

namespace {

const QString gTestAccountName = QStringLiteral("testAccountName");

class VersionHandlerTest : public testing::Test
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
        {
            auto nullDeleter = []([[maybe_unused]] QThreadPool * threadPool) {};
            m_threadPool = std::shared_ptr<QThreadPool>(
                QThreadPool::globalInstance(), std::move(nullDeleter));
        }

        m_account = Account{gTestAccountName, Account::Type::Local};
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
    threading::QThreadPtr m_writerThread;
    threading::QThreadPoolPtr m_threadPool;
    Account m_account;
};

} // namespace

TEST_F(VersionHandlerTest, Ctor)
{
    EXPECT_NO_THROW(
        const auto versionHandler = std::make_shared<VersionHandler>(
            m_account, m_connectionPool, m_threadPool,
            m_writerThread));
}

TEST_F(VersionHandlerTest, CtorEmptyAccount)
{
    EXPECT_THROW(
        const auto versionHandler = std::make_shared<VersionHandler>(
            Account{}, m_connectionPool, m_threadPool,
            m_writerThread),
        IQuentierException);
}

TEST_F(VersionHandlerTest, CtorNullConnectionPool)
{
    EXPECT_THROW(
        const auto versionHandler = std::make_shared<VersionHandler>(
            m_account, nullptr, m_threadPool, m_writerThread),
        IQuentierException);
}

TEST_F(VersionHandlerTest, CtorNullThreadPool)
{
    EXPECT_THROW(
        const auto versionHandler = std::make_shared<VersionHandler>(
            m_account, m_connectionPool, nullptr, m_writerThread),
        IQuentierException);
}

TEST_F(VersionHandlerTest, CtorNullWriterThread)
{
    EXPECT_THROW(
        const auto versionHandler = std::make_shared<VersionHandler>(
            m_account, m_connectionPool, m_threadPool,
            nullptr),
        IQuentierException);
}

TEST_F(VersionHandlerTest, HandleEmptyNewlyCreatedDatabase)
{
    const auto versionHandler = std::make_shared<VersionHandler>(
        m_account, m_connectionPool, m_threadPool,
        m_writerThread);

    auto isVersionTooHighFuture = versionHandler->isVersionTooHigh();
    isVersionTooHighFuture.waitForFinished();
    EXPECT_FALSE(isVersionTooHighFuture.result());

    auto requiresUpgradeFuture = versionHandler->requiresUpgrade();
    requiresUpgradeFuture.waitForFinished();
    EXPECT_FALSE(requiresUpgradeFuture.result());

    auto requiredPatchesFuture = versionHandler->requiredPatches();
    requiredPatchesFuture.waitForFinished();
    EXPECT_TRUE(requiredPatchesFuture.result().isEmpty());

    auto versionFuture = versionHandler->version();
    versionFuture.waitForFinished();
    EXPECT_EQ(versionFuture.result(), 2);

    auto highestSupportedVersionFuture =
        versionHandler->highestSupportedVersion();
    highestSupportedVersionFuture.waitForFinished();
    EXPECT_EQ(highestSupportedVersionFuture.result(), 2);
}

TEST_F(VersionHandlerTest, HandleDatabaseOfVersion1)
{
    {
        auto database = m_connectionPool->database();
        QSqlQuery query{database};
        const bool res = query.exec(QStringLiteral(
            "UPDATE Auxiliary SET version = 1 WHERE version = 2"));
        EXPECT_TRUE(res);
    }

    const auto versionHandler = std::make_shared<VersionHandler>(
        m_account, m_connectionPool, m_threadPool,
        m_writerThread);

    auto isVersionTooHighFuture = versionHandler->isVersionTooHigh();
    isVersionTooHighFuture.waitForFinished();
    EXPECT_FALSE(isVersionTooHighFuture.result());

    auto requiresUpgradeFuture = versionHandler->requiresUpgrade();
    requiresUpgradeFuture.waitForFinished();
    EXPECT_TRUE(requiresUpgradeFuture.result());

    auto requiredPatchesFuture = versionHandler->requiredPatches();
    requiredPatchesFuture.waitForFinished();
    const auto requiredPatches = requiredPatchesFuture.result();
    EXPECT_EQ(requiredPatches.size(), 1);

    const auto patch1to2 =
        std::dynamic_pointer_cast<Patch1To2>(requiredPatches[0]);
    EXPECT_TRUE(patch1to2);

    auto versionFuture = versionHandler->version();
    versionFuture.waitForFinished();
    EXPECT_EQ(versionFuture.result(), 1);

    auto highestSupportedVersionFuture =
        versionHandler->highestSupportedVersion();
    highestSupportedVersionFuture.waitForFinished();
    EXPECT_EQ(highestSupportedVersionFuture.result(), 2);
}

TEST_F(VersionHandlerTest, HandleDatabaseOfTooHighVersion)
{
    {
        auto database = m_connectionPool->database();
        QSqlQuery query{database};
        const bool res = query.exec(QStringLiteral(
            "UPDATE Auxiliary SET version = 999 WHERE version = 2"));
        EXPECT_TRUE(res);
    }

    const auto versionHandler = std::make_shared<VersionHandler>(
        m_account, m_connectionPool, m_threadPool,
        m_writerThread);

    auto isVersionTooHighFuture = versionHandler->isVersionTooHigh();
    isVersionTooHighFuture.waitForFinished();
    EXPECT_TRUE(isVersionTooHighFuture.result());

    auto requiresUpgradeFuture = versionHandler->requiresUpgrade();
    requiresUpgradeFuture.waitForFinished();
    EXPECT_FALSE(requiresUpgradeFuture.result());

    auto requiredPatchesFuture = versionHandler->requiredPatches();
    requiredPatchesFuture.waitForFinished();
    EXPECT_TRUE(requiredPatchesFuture.result().isEmpty());

    auto versionFuture = versionHandler->version();
    versionFuture.waitForFinished();
    EXPECT_EQ(versionFuture.result(), 999);

    auto highestSupportedVersionFuture =
        versionHandler->highestSupportedVersion();
    highestSupportedVersionFuture.waitForFinished();
    EXPECT_EQ(highestSupportedVersionFuture.result(), 2);
}

} // namespace quentier::local_storage::sql::tests