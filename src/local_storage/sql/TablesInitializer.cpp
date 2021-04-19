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

#include "ConnectionPool.h"
#include "ErrorHandling.h"
#include "TablesInitializer.h"

#include <quentier/exception/DatabaseOpeningException.h>
#include <quentier/exception/DatabaseRequestException.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/types/ErrorString.h>

#include <QMutexLocker>
#include <QSqlError>
#include <QSqlQuery>
#include <QtGlobal>

#include <stdexcept>

#define ENSURE_TABLES_INITIALIZER_DB_REQUEST(res, query, message)              \
    ENSURE_DB_REQUEST(                                                         \
        res, query, "local_storage::sql::TablesInitializer", message)

namespace quentier::local_storage::sql {

TablesInitializer::TablesInitializer(DatabaseInfo databaseInfo)
    : m_databaseInfo{std::move(databaseInfo)}
{
    if (Q_UNLIKELY(!m_databaseInfo.connectionPool)) {
        ErrorString error(QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::TablesInitializer",
            "Cannot create TablesInitializer: connection pool is null"));

        QNWARNING("local_storage:sql:tables_initializer", error);
        throw DatabaseOpeningException{error};
    }

    if (Q_UNLIKELY(!m_databaseInfo.writerMutex)) {
        ErrorString error(QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::TablesInitializer",
            "Cannot create TablesInitializer: writer mutex is null"));

        QNWARNING("local_storage:sql:tables_initializer", error);
        throw DatabaseOpeningException{error};
    }
}

void TablesInitializer::initializeTables()
{
    auto databaseConnection = m_databaseInfo.connectionPool->database();

    QMutexLocker lock{m_databaseInfo.writerMutex.get()};
    initializeAuxiliaryTable(databaseConnection);

    // TODO: continue from here
}

void TablesInitializer::initializeAuxiliaryTable(QSqlDatabase & databaseConnection)
{
    QSqlQuery query{databaseConnection};
    bool res = query.exec(
        QStringLiteral("SELECT name FROM sqlite_master WHERE name='Auxiliary'"));

    ENSURE_TABLES_INITIALIZER_DB_REQUEST(
        res, query,
        "Cannot check the existence of Auxiliary table in the local storage"
        "database");

    const bool auxiliaryTableExists = query.next();
    QNDEBUG(
        "local_storage:sql:tables_initializer",
        "Auxiliary table "
            << (auxiliaryTableExists ? "already exists" : "doesn't exist yet"));

    if (auxiliaryTableExists) {
        return;
    }

    res = query.exec(
        QStringLiteral("CREATE TABLE Auxiliary("
                        "  lock    CHAR(1) PRIMARY KEY  NOT NULL DEFAULT "
                        "'X' CHECK (lock='X'), "
                        "  version INTEGER              NOT NULL DEFAULT 2"
                        ")"));
    ENSURE_TABLES_INITIALIZER_DB_REQUEST(
        res, query,
        "Cannot create Auxiliary table in the local storage database");

    res = query.exec(
        QStringLiteral("INSERT INTO Auxiliary (version) VALUES(2)"));

    ENSURE_TABLES_INITIALIZER_DB_REQUEST(
        res, query,
        "Cannot set version into Auxiliary table of the local storage database");
}

} // namespace quentier::local_storage::sql
