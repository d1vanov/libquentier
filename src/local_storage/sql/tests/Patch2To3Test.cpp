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

#include "Utils.h"

#include "../ConnectionPool.h"
#include "../ErrorHandling.h"
#include "../Fwd.h"
#include "../TablesInitializer.h"
#include "../patches/Patch2To3.h"

#include <quentier/exception/IQuentierException.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/FileSystem.h>
#include <quentier/utility/StandardPaths.h>

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QtGlobal>
#include <QThread>

namespace quentier::local_storage::sql::tests {

namespace {

const QString gTestDbConnectionName =
    QStringLiteral("libquentier_local_storage_sql_patch2to3_test_db");


// Removes ResourceDataBodyVersionIds and ResourceAlternateDataBodyVersionIds
// tables from the local storage database in order to set up the situation
// as before applying the 2 to 3 patch
void removeBodyVersionIdTables(QSqlDatabase & database)
{
    QSqlQuery query{database};

    bool res = query.exec(QStringLiteral(
        "DROP TABLE IF EXISTS ResourceDataBodyVersionIds"));

    ENSURE_DB_REQUEST_THROW(
        res, query, "local_storage::sql::tests::Patch2To3Test",
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::tests::Patch2To3Test",
            "Failed to drop ResourceDataBodyVersionIds table"));

    res = query.exec(QStringLiteral(
        "DROP TABLE IF EXISTS ResourceAlternateDataBodyVersionIds"));

    ENSURE_DB_REQUEST_THROW(
        res, query, "local_storage::sql::tests::Patch2To3Test",
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::tests::Patch2To3Test",
            "Failed to drop ResourceAlternateDataBodyVersionIds table"));
}

// Prepares local storage database corresponding to version 2 in a temporary dir
// so that it can be upgraded from version 2 to version 3
// TODO: remove maybe_unused attribute later when more tests are added
[[maybe_unused]] void prepareLocalStorageForUpgrade(
    const QString & localStorageDirPath,
    ConnectionPool & connectionPool)
{
    utils::prepareLocalStorage(localStorageDirPath, connectionPool);

    auto database = connectionPool.database();
    removeBodyVersionIdTables(database);

    // TODO: fill the database with test content
}

} // namespace

TEST(Patch2To3Test, Ctor)
{
    Account account{*utils::gTestAccountName, Account::Type::Local};

    auto connectionPool = std::make_shared<ConnectionPool>(
        QStringLiteral("localhost"), QStringLiteral("user"),
        QStringLiteral("password"), QStringLiteral(":memory:"),
        QStringLiteral("QSQLITE"));

    auto pWriterThread = std::make_shared<QThread>();

    EXPECT_NO_THROW(const auto patch = std::make_shared<Patch2To3>(
        std::move(account), std::move(connectionPool),
        std::move(pWriterThread)));
}

TEST(Patch2To3Test, CtorEmptyAccount)
{
    auto connectionPool = std::make_shared<ConnectionPool>(
        QStringLiteral("localhost"), QStringLiteral("user"),
        QStringLiteral("password"), QStringLiteral(":memory:"),
        QStringLiteral("QSQLITE"));

    auto pWriterThread = std::make_shared<QThread>();

    EXPECT_THROW(
        const auto patch = std::make_shared<Patch2To3>(
            Account{}, std::move(connectionPool), std::move(pWriterThread)),
        IQuentierException);
}

TEST(Patch2To3Test, CtorNullConnectionPool)
{
    Account account{*utils::gTestAccountName, Account::Type::Local};

    auto pWriterThread = std::make_shared<QThread>();

    EXPECT_THROW(
        const auto patch = std::make_shared<Patch2To3>(
            std::move(account), nullptr, std::move(pWriterThread)),
        IQuentierException);
}

TEST(Patch2To3Test, CtorNullWriterThread)
{
    Account account{*utils::gTestAccountName, Account::Type::Local};

    auto connectionPool = std::make_shared<ConnectionPool>(
        QStringLiteral("localhost"), QStringLiteral("user"),
        QStringLiteral("password"), QStringLiteral(":memory:"),
        QStringLiteral("QSQLITE"));

    EXPECT_THROW(
        const auto patch = std::make_shared<Patch2To3>(
            std::move(account), std::move(connectionPool), nullptr),
        IQuentierException);
}

} // namespace quentier::local_storage::sql::tests
