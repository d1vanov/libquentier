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

#include "Utils.h"

#include "../ConnectionPool.h"
#include "../SqlDatabaseWrapper.h"
#include "../TablesInitializer.h"

#include <gtest/gtest.h>

#include <QDir>

namespace quentier::local_storage::sql::tests::utils {

void prepareLocalStorage(
    const QString & localStorageDirPath, ConnectionPool & connectionPool)
{
    QDir dir{localStorageDirPath};
    if (!dir.exists()) {
        const bool res = dir.mkpath(localStorageDirPath);
        Q_ASSERT(res);
    }

    static const QString databaseName = QStringLiteral("qn.storage.sqlite");

    ensureFile(dir, databaseName);

    QFileInfo testDatabaseFileInfo{dir.absoluteFilePath(databaseName)};
    EXPECT_TRUE(testDatabaseFileInfo.exists());
    EXPECT_TRUE(testDatabaseFileInfo.isFile());
    EXPECT_TRUE(testDatabaseFileInfo.isReadable());
    EXPECT_TRUE(testDatabaseFileInfo.isWritable());

    auto database = connectionPool.database();
    database.setHostName(QStringLiteral("localhost"));
    database.setDatabaseName(testDatabaseFileInfo.absoluteFilePath());

    TablesInitializer::initializeTables(database);
}

void ensureFile(const QDir & dir, const QString & fileName)
{
    QFile file{dir.filePath(fileName)};

    EXPECT_TRUE(file.open(QIODevice::WriteOnly));
    file.write(QByteArray::fromRawData("0", 1));
    file.flush();
    file.close();

    file.resize(0);
}

ConnectionPoolPtr createConnectionPool()
{
    static int counter = 1;
    return std::make_shared<ConnectionPool>(
        std::make_shared<SqlDatabaseWrapper>(),
        QStringLiteral("localhost"), QStringLiteral("user"),
        QStringLiteral("password"),
        QString::fromUtf8("file::memdb%1?mode=memory&cache=shared")
            .arg(counter++),
        QStringLiteral("QSQLITE"), QStringLiteral("QSQLITE_OPEN_URI"));
}

} // namespace quentier::local_storage::sql::tests::utils
