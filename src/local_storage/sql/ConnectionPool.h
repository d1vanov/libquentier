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

#pragma once

#include <QHash>
#include <QPointer>
#include <QReadWriteLock>
#include <QSqlDatabase>
#include <QThread>

#include <memory>

namespace quentier::local_storage::sql {

class ConnectionPool final: public std::enable_shared_from_this<ConnectionPool>
{
public:
    explicit ConnectionPool(
        QString hostName, QString userName, QString password,
        QString databaseName, QString sqlDriverName);

    ~ConnectionPool();

    [[nodiscard]] QSqlDatabase database();

private:
    struct ConnectionData
    {
        QPointer<QThread> m_pThread;
        QString m_connectionName;
    };

    const QString m_hostName;
    const QString m_userName;
    const QString m_password;
    const QString m_databaseName;
    const QString m_sqlDriverName;

    QReadWriteLock m_connectionsLock;
    QHash<QThread*, ConnectionData> m_connections;
};

} // namespace quentier::local_storage::sql
