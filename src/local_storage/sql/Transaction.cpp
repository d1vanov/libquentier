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

#include "Transaction.h"

#include <quentier/exception/DatabaseRequestException.h>
#include <quentier/exception/InvalidArgument.h>
#include <quentier/logging/QuentierLogger.h>

#include <QSqlDriver>
#include <QSqlError>

namespace quentier::local_storage::sql {

Transaction::Transaction(const QSqlDatabase & database) :
    m_database{database}
{
    const auto * driver = m_database.driver();
    if (Q_UNLIKELY(!driver)) {
        auto errorDescription = ErrorString{
            QT_TRANSLATE_NOOP(
                "local_storage::sql::Transaction",
                "Failed to create local storage transaction: no SQL driver")};
        QNWARNING("local_storage::sql::Transaction", errorDescription);
        throw InvalidArgument{std::move(errorDescription)};
    }

    if (Q_UNLIKELY(!driver->hasFeature(QSqlDriver::Transactions))) {
        auto errorDescription = ErrorString{
            QT_TRANSLATE_NOOP(
                "local_storage::sql::Transaction",
                "Failed to create local storage transaction: SQL driver "
                "doesn't support transactions")};
        errorDescription.details() = database.driverName();
        QNWARNING("local_storage::sql::Transaction", errorDescription);
        throw InvalidArgument{std::move(errorDescription)};
    }

    const bool res = m_database.transaction();
    if (Q_UNLIKELY(!res)) {
        auto errorDescription = ErrorString{
            QT_TRANSLATE_NOOP(
                "local_storage::sql::Transaction",
                "Failed to start local storage transaction")};
        const auto lastError = m_database.lastError();
        errorDescription.details() = lastError.text();
        errorDescription.details() += QStringLiteral(" (native error code = ");
        errorDescription.details() += lastError.nativeErrorCode();
        errorDescription.details() += QStringLiteral(")");
        QNWARNING("local_storage::sql::Transaction", errorDescription);
        throw DatabaseRequestException{std::move(errorDescription)};
    }
}

Transaction::~Transaction()
{
    if (!m_committed && !m_rolledBack) {
        if (!rollback()) {
            const auto lastError = m_database.lastError();
            QNERROR(
                "local_storage:sql:Transaction",
                "Failed to roll back the transaction: "
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

    m_committed = m_database.commit();
    return m_committed;
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

    m_rolledBack = m_database.rollback();
    return false;
}

} // namespace quentier::local_storage::sql
