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
#include "../patches/Patch1To2.h"

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
    QStringLiteral("libquentier_local_storage_sql_patch1to2_test_db");

} // namespace

TEST(Patch1To2Test, Ctor)
{
    Account account{*utils::gTestAccountName, Account::Type::Local};

    auto connectionPool = std::make_shared<ConnectionPool>(
        QStringLiteral("localhost"), QStringLiteral("user"),
        QStringLiteral("password"), QStringLiteral(":memory:"),
        QStringLiteral("QSQLITE"));

    auto pWriterThread = std::make_shared<QThread>();

    EXPECT_NO_THROW(const auto patch = std::make_shared<Patch1To2>(
        std::move(account), std::move(connectionPool),
        std::move(pWriterThread)));
}

TEST(Patch1To2Test, CtorEmptyAccount)
{
    auto connectionPool = std::make_shared<ConnectionPool>(
        QStringLiteral("localhost"), QStringLiteral("user"),
        QStringLiteral("password"), QStringLiteral(":memory:"),
        QStringLiteral("QSQLITE"));

    auto pWriterThread = std::make_shared<QThread>();

    EXPECT_THROW(
        const auto patch = std::make_shared<Patch1To2>(
            Account{}, std::move(connectionPool), std::move(pWriterThread)),
        IQuentierException);
}

TEST(Patch1To2Test, CtorNullConnectionPool)
{
    Account account{*utils::gTestAccountName, Account::Type::Local};

    auto pWriterThread = std::make_shared<QThread>();

    EXPECT_THROW(
        const auto patch = std::make_shared<Patch1To2>(
            std::move(account), nullptr, std::move(pWriterThread)),
        IQuentierException);
}

TEST(Patch1To2Test, CtorNullWriterThread)
{
    Account account{*utils::gTestAccountName, Account::Type::Local};

    auto connectionPool = std::make_shared<ConnectionPool>(
        QStringLiteral("localhost"), QStringLiteral("user"),
        QStringLiteral("password"), QStringLiteral(":memory:"),
        QStringLiteral("QSQLITE"));

    EXPECT_THROW(
        const auto patch = std::make_shared<Patch1To2>(
            std::move(account), std::move(connectionPool), nullptr),
        IQuentierException);
}

} // namespace quentier::local_storage::sql::tests
