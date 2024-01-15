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

#include "ErrorHandling.h"
#include "Transaction.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/logging/QuentierLogger.h>

#include <QSqlError>
#include <QSqlQuery>

namespace quentier::local_storage::sql {

Transaction::Transaction(const QSqlDatabase & database, Type type) :
    m_database{database},
    m_type{type}
{
    init();
}

Transaction::Transaction(Transaction && transaction) noexcept :
    m_database{transaction.m_database},
    m_type{transaction.m_type},
    m_committed{transaction.m_committed},
    m_rolledBack{transaction.m_rolledBack},
    m_ended{transaction.m_ended}
{
    transaction.m_ended = true;
}

Transaction::~Transaction()
{
    if ((m_type != Type::Selection) && !m_committed && !m_rolledBack &&
        !m_ended) {
        QSqlQuery query{m_database};
        bool res = query.exec(QStringLiteral("ROLLBACK"));
        if (!res) {
            QSqlError lastError = query.lastError();
            QNERROR(
                "local_storage:sql:Transaction",
                "Failed to roll back the transaction: "
                << lastError.text() << " (native error code = "
                << lastError.nativeErrorCode() << ")");
        }
    }
    else if ((m_type == Type::Selection) && !m_ended) {
        QSqlQuery query{m_database};
        bool res = query.exec(QStringLiteral("END"));
        if (!res) {
            QSqlError lastError = query.lastError();
            QNERROR(
                "local_storage:sql:Transaction",
                "Failed to end the transaction: "
                << lastError.text() << " (native error code = "
                << lastError.nativeErrorCode() << ")");
        }
    }
}

bool Transaction::commit()
{
    if (m_committed) {
        QNWARNING(
            "local_storage:sql:Transaction",
            "Detected attempt to commit the same transaction more than once");
        return m_committed;
    }

    if (m_rolledBack) {
        QNWARNING(
            "local_storage:sql:Transaction",
            "Commit called on already rolled back transaction");
        return false;
    }

    if (m_ended) {
        QNWARNING(
            "local_storage:sql:Transaction",
            "Commit called on already ended transaction");
        return false;
    }

    if (m_type == Type::Selection) {
        QNWARNING(
            "local_storage:sql:Transaction",
            "Cannot commit the transaction of selection type");
        return false;
    }

    QSqlQuery query{m_database};
    bool res = query.exec(QStringLiteral("COMMIT"));
    if (!res) {
        const auto lastError = query.lastError();
        QNWARNING(
            "local_storage::sql::Transaction",
            "Cannot commit the transaction: " << lastError.text()
                << " (native error code = " << lastError.nativeErrorCode()
                << ")");
        return false;
    }

    m_committed = true;
    return true;
}

bool Transaction::rollback()
{
    if (m_rolledBack) {
        QNWARNING(
            "local_storage:sql:Transaction",
            "Detected attempt to roll back the same transaction more than "
            "once");
        return m_rolledBack;
    }

    if (m_committed) {
        QNWARNING(
            "local_storage:sql:Transaction",
            "Rollback called on already committed transaction");
        return false;
    }

    if (m_type == Type::Selection) {
        QNWARNING(
            "local_storage:sql:Transaction",
            "Cannot rollback the transaction of selection type");
        return false;
    }

    QSqlQuery query{m_database};
    bool res = query.exec(QStringLiteral("ROLLBACK"));
    if (!res) {
        const auto lastError = query.lastError();
        QNWARNING(
            "local_storage::sql::Transaction",
            "Cannot rollback the transaction: " << lastError.text()
                << " (native error code = " << lastError.nativeErrorCode()
                << ")");
        return false;
    }

    m_rolledBack = m_database.rollback();
    return false;
}

bool Transaction::end()
{
    if (m_ended) {
        QNWARNING(
            "local_storage:sql:Transaction",
            "Transaction is already ended");
        return false;
    }

    QSqlQuery query{m_database};
    bool res = query.exec(QStringLiteral("END"));
    if (!res) {
        const auto lastError = query.lastError();
        QNWARNING(
            "local_storage:sql:Transaction",
            "Cannot end the transaction: " << lastError.text()
                << " (native error code = " << lastError.nativeErrorCode()
                << ")");
        return false;
    }

    m_ended = true;
    return true;
}

void Transaction::init()
{
    QString queryString = QStringLiteral("BEGIN");
    if (m_type == Type::Immediate) {
        queryString += QStringLiteral(" IMMEDIATE");
    }
    else if (m_type == Type::Exclusive) {
        queryString += QStringLiteral(" EXCLUSIVE");
    }

    QSqlQuery query{m_database};
    const bool res = query.exec(queryString);
    ENSURE_DB_REQUEST_THROW(
        res, query, "local_storage::sql::transaction",
        QStringLiteral(
            "Failed to begin transaction"));
}

} // namespace quentier::local_storage::sql
