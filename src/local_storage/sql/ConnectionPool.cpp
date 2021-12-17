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

#include <quentier/exception/DatabaseOpeningException.h>
#include <quentier/exception/DatabaseRequestException.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/types/ErrorString.h>
#include <quentier/utility/SysInfo.h>
#include <quentier/utility/UidGenerator.h>

#include <QObject>
#include <QReadLocker>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QWriteLocker>

#include <sstream>
#include <thread>

namespace quentier::local_storage::sql {

ConnectionPool::ConnectionPool(
    QString hostName, QString userName, QString password,
    QString databaseName, QString sqlDriverName, QString connectionOptions) :
    m_hostName{std::move(hostName)},
    m_userName{std::move(userName)},
    m_password{std::move(password)},
    m_databaseName{std::move(databaseName)},
    m_sqlDriverName{std::move(sqlDriverName)},
    m_connectionOptions{std::move(connectionOptions)},
    m_pageSize{[]{ SysInfo sysInfo; return sysInfo.pageSize(); }()}
{
    const bool isSqlDriverAvailable =
        QSqlDatabase::isDriverAvailable(m_sqlDriverName);

    if (Q_UNLIKELY(!isSqlDriverAvailable)) {
        ErrorString error(QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::ConnectionPool",
            "SQL database driver is not available"));

        error.details() += m_sqlDriverName;
        error.details() += QStringLiteral("; available SQL drivers: ");

        const QStringList drivers = QSqlDatabase::drivers();
        for (const auto & driver: qAsConst(drivers)) {
            error.details() += driver;
            if (&driver != &drivers.back()) {
                error.details() += QStringLiteral(", ");
            }
        }

        QNWARNING("local_storage:sql:connection_pool", error);
        throw DatabaseRequestException(error);
    }
}

ConnectionPool::~ConnectionPool()
{
    for (auto it = m_connections.begin(), end = m_connections.end(); it != end;
         ++it)
    {
        QSqlDatabase::removeDatabase(it.value().m_connectionName);
    }
}

QSqlDatabase ConnectionPool::database()
{
    auto * pCurrentThread = QThread::currentThread();
    {
        QReadLocker lock{&m_connectionsLock};

        auto it = m_connections.find(pCurrentThread);
        if (it != m_connections.end()) {
            return QSqlDatabase::database(
                it->m_connectionName, /* open = */ true);
        }
    }

    QWriteLocker lock{&m_connectionsLock};

    // Try to find the existing connection again
    auto it = m_connections.find(pCurrentThread);
    if (it != m_connections.end())
    {
        return QSqlDatabase::database(it->m_connectionName, /* open = */ true);
    }

    std::ostringstream sstrm;
    auto threadId = std::this_thread::get_id();
    sstrm << threadId;

    // Will also add a unique identifier to the name of the connection
    // to prevent the following potential problem: if the same thread
    // calls QSqlDatabase::removeDatabase and shortly thereafter
    // QSqlDatabase::addDatabase with the same connection name, it might fail
    // with an error saying "duplicate connection name <...>, old connection
    // removed" and then the created connection would actually fail to do any
    // useful work. So will ensure that each newly created connection name
    // is unique, even if the same thread makes the connection again.
    QString connectionName =
        QStringLiteral("quentier_local_storage_db_connection_") +
        QString::fromStdString(sstrm.str()) + UidGenerator::Generate();

    m_connections[pCurrentThread] =
        ConnectionData{QPointer{pCurrentThread}, connectionName};

    QObject::connect(
        pCurrentThread,
        &QThread::finished,
        pCurrentThread,
        [pCurrentThread, self_weak = weak_from_this()]
        {
            auto self = self_weak.lock();
            if (!self) {
                return;
            }

            QWriteLocker lock{&self->m_connectionsLock};
            auto it = self->m_connections.find(pCurrentThread);
            if (Q_LIKELY(it != self->m_connections.end())) {
                QSqlDatabase::removeDatabase(it.value().m_connectionName);
                self->m_connections.erase(it);
            }
        },
        Qt::QueuedConnection);

    auto database = QSqlDatabase::addDatabase(m_sqlDriverName, connectionName);
    database.setHostName(m_hostName);
    database.setUserName(m_userName);
    database.setPassword(m_password);
    database.setDatabaseName(m_databaseName);
    database.setConnectOptions(m_connectionOptions);

    if (Q_UNLIKELY(!database.open())) {
        ErrorString error(QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::ConnectionPool",
            "Failed to open the database"));

        const auto lastError = database.lastError();
        error.details() += lastError.text();
        error.details() += QStringLiteral("; native error code = ");
        error.details() += lastError.nativeErrorCode();

        QNWARNING("local_storage:sql:connection_pool", error);
        throw DatabaseOpeningException(error);
    }

    QSqlQuery query{database};
    if (Q_UNLIKELY(!query.exec(QStringLiteral("PRAGMA foreign_keys = ON")))) {
        ErrorString error(QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::ConnectionPool",
            "Failed to enable foreign keys for the local storage database "
            "connection"));

        const auto lastError = query.lastError();
        error.details() += lastError.text();
        error.details() += QStringLiteral("; native error code = ");
        error.details() += lastError.nativeErrorCode();

        QNWARNING("local_storage:sql:connection_pool", error);
        throw DatabaseRequestException(error);
    }

    if (Q_UNLIKELY(!query.exec(
            QString::fromUtf8("PRAGMA page_size = %1").arg(m_pageSize))))
    {
        ErrorString error(QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::ConnectionPool",
            "Failed to set page size for the local storage database "
            "connection"));

        const auto lastError = query.lastError();
        error.details() += lastError.text();
        error.details() += QStringLiteral("; native error code = ");
        error.details() += lastError.nativeErrorCode();

        QNWARNING("local_storage:sql:connection_pool", error);
        throw DatabaseRequestException(error);
    }

    return database;
}

} // namespace quentier::local_storage::sql
