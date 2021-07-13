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

#include "../TablesInitializer.h"

#include <gtest/gtest.h>

#include <QFile>
#include <QSqlDatabase>
#include <QSqlRecord>
#include <QSqlQuery>
#include <QTextStream>
#include <QVariant>

namespace quentier::local_storage::sql::tests {

TEST(TablesInitializerTest, InitializeTables)
{
    QSqlDatabase db = QSqlDatabase::addDatabase(
        QStringLiteral("QSQLITE"),
        QStringLiteral("quentier::local_storage::sql::TableInitializerTestDb"));
    db.setDatabaseName(QStringLiteral(":memory:"));
    EXPECT_TRUE(db.open());

    TablesInitializer::initializeTables(db);

    QSqlQuery query{db};
    bool res = query.exec(QStringLiteral("SELECT * FROM sqlite_master"));
    EXPECT_TRUE(res);

    QString masterTable;
    QTextStream strm(&masterTable);
    while (query.next()) {
        const auto record = query.record();
        for (int i = 0; i < record.count(); ++i) {
            const auto part = record.value(i).toString().simplified();
            if (!part.isEmpty()) {
                strm << part;
                strm << " ";
            }
        }
    }

    masterTable = masterTable.trimmed();

    QFile referenceMasterTableFile{QStringLiteral(":/expected_db_schema.txt")};
    EXPECT_TRUE(referenceMasterTableFile.open(QIODevice::ReadOnly));
    const auto referenceMasterTable = QString::fromUtf8(referenceMasterTableFile.readAll()).simplified();

    QFile updatedMasterTable{QStringLiteral("/tmp/master_table_schema.txt")};
    EXPECT_TRUE(updatedMasterTable.open(QIODevice::WriteOnly));
    updatedMasterTable.write(masterTable.toUtf8());
    updatedMasterTable.close();

    EXPECT_EQ(masterTable, referenceMasterTable);
    db.close();
}

} // namespace quentier::local_storage::sql::tests
