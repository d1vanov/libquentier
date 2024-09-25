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
#include "../TablesInitializer.h"
#include "../VersionHandler.h"
#include "../patches/Patch1To2.h"
#include "../patches/Patch2To3.h"

#include <quentier/exception/IQuentierException.h>

#include <QCoreApplication>
#include <QSqlDatabase>
#include <QSqlQuery>

#include <gtest/gtest.h>

// clazy:excludeall=returning-void-expression

namespace quentier::local_storage::sql::tests {

namespace {

const char * gTestAccountName = "testAccountName";

class VersionHandlerTest : public testing::Test
{
protected:
    void SetUp() override
    {
        m_connectionPool = utils::createConnectionPool();

        auto database = m_connectionPool->database();
        TablesInitializer::initializeTables(database);

        m_thread = std::make_shared<QThread>();
        m_thread->start();
        m_account =
            Account{QString::fromUtf8(gTestAccountName), Account::Type::Local};
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
    Account m_account;
};

} // namespace

TEST_F(VersionHandlerTest, Ctor)
{
    EXPECT_NO_THROW(
        const auto versionHandler = std::make_shared<VersionHandler>(
            m_account, m_connectionPool, m_thread));
}

TEST_F(VersionHandlerTest, CtorEmptyAccount)
{
    EXPECT_THROW(
        const auto versionHandler = std::make_shared<VersionHandler>(
            Account{}, m_connectionPool, m_thread),
        IQuentierException);
}

TEST_F(VersionHandlerTest, CtorNullConnectionPool)
{
    EXPECT_THROW(
        const auto versionHandler =
            std::make_shared<VersionHandler>(m_account, nullptr, m_thread),
        IQuentierException);
}

TEST_F(VersionHandlerTest, CtorNullThread)
{
    EXPECT_THROW(
        const auto versionHandler = std::make_shared<VersionHandler>(
            m_account, m_connectionPool, nullptr),
        IQuentierException);
}

TEST_F(VersionHandlerTest, HandleEmptyNewlyCreatedDatabase)
{
    const auto versionHandler =
        std::make_shared<VersionHandler>(m_account, m_connectionPool, m_thread);

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
    EXPECT_EQ(versionFuture.result(), 3);

    auto highestSupportedVersionFuture =
        versionHandler->highestSupportedVersion();
    highestSupportedVersionFuture.waitForFinished();
    EXPECT_EQ(highestSupportedVersionFuture.result(), 3);
}

TEST_F(VersionHandlerTest, HandleDatabaseOfVersion1)
{
    {
        auto database = m_connectionPool->database();
        QSqlQuery query{database};
        const bool res = query.exec(QStringLiteral(
            "UPDATE Auxiliary SET version = 1 WHERE version = 3"));
        EXPECT_TRUE(res);
    }

    const auto versionHandler =
        std::make_shared<VersionHandler>(m_account, m_connectionPool, m_thread);

    auto isVersionTooHighFuture = versionHandler->isVersionTooHigh();
    isVersionTooHighFuture.waitForFinished();
    EXPECT_FALSE(isVersionTooHighFuture.result());

    auto requiresUpgradeFuture = versionHandler->requiresUpgrade();
    requiresUpgradeFuture.waitForFinished();
    EXPECT_TRUE(requiresUpgradeFuture.result());

    auto requiredPatchesFuture = versionHandler->requiredPatches();
    requiredPatchesFuture.waitForFinished();
    const auto requiredPatches = requiredPatchesFuture.result();
    ASSERT_EQ(requiredPatches.size(), 2);

    const auto patch1to2 =
        std::dynamic_pointer_cast<Patch1To2>(requiredPatches[0]);
    EXPECT_TRUE(patch1to2);

    const auto patch2to3 =
        std::dynamic_pointer_cast<Patch2To3>(requiredPatches[1]);
    EXPECT_TRUE(patch2to3);

    auto versionFuture = versionHandler->version();
    versionFuture.waitForFinished();
    EXPECT_EQ(versionFuture.result(), 1);

    auto highestSupportedVersionFuture =
        versionHandler->highestSupportedVersion();
    highestSupportedVersionFuture.waitForFinished();
    EXPECT_EQ(highestSupportedVersionFuture.result(), 3);
}

TEST_F(VersionHandlerTest, HandleDatabaseOfVersion2)
{
    {
        auto database = m_connectionPool->database();
        QSqlQuery query{database};
        const bool res = query.exec(QStringLiteral(
            "UPDATE Auxiliary SET version = 2 WHERE version = 3"));
        EXPECT_TRUE(res);
    }

    const auto versionHandler =
        std::make_shared<VersionHandler>(m_account, m_connectionPool, m_thread);

    auto isVersionTooHighFuture = versionHandler->isVersionTooHigh();
    isVersionTooHighFuture.waitForFinished();
    EXPECT_FALSE(isVersionTooHighFuture.result());

    auto requiresUpgradeFuture = versionHandler->requiresUpgrade();
    requiresUpgradeFuture.waitForFinished();
    EXPECT_TRUE(requiresUpgradeFuture.result());

    auto requiredPatchesFuture = versionHandler->requiredPatches();
    requiredPatchesFuture.waitForFinished();
    const auto requiredPatches = requiredPatchesFuture.result();
    ASSERT_EQ(requiredPatches.size(), 1);

    const auto patch2to3 =
        std::dynamic_pointer_cast<Patch2To3>(requiredPatches[0]);
    EXPECT_TRUE(patch2to3);

    auto versionFuture = versionHandler->version();
    versionFuture.waitForFinished();
    EXPECT_EQ(versionFuture.result(), 2);

    auto highestSupportedVersionFuture =
        versionHandler->highestSupportedVersion();
    highestSupportedVersionFuture.waitForFinished();
    EXPECT_EQ(highestSupportedVersionFuture.result(), 3);
}

TEST_F(VersionHandlerTest, HandleDatabaseOfTooHighVersion)
{
    {
        auto database = m_connectionPool->database();
        QSqlQuery query{database};
        const bool res = query.exec(QStringLiteral(
            "UPDATE Auxiliary SET version = 999 WHERE version = 3"));
        EXPECT_TRUE(res);
    }

    const auto versionHandler =
        std::make_shared<VersionHandler>(m_account, m_connectionPool, m_thread);

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
    EXPECT_EQ(highestSupportedVersionFuture.result(), 3);
}

} // namespace quentier::local_storage::sql::tests
