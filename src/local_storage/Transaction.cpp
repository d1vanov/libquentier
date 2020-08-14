/*
 * Copyright 2016-2020 Dmitry Ivanov
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

#include "LocalStorageManager_p.h"

#include <quentier/exception/DatabaseRequestException.h>
#include <quentier/logging/QuentierLogger.h>

#include <QSqlError>
#include <QSqlQuery>

namespace quentier {

Transaction::Transaction(
    const QSqlDatabase & db,
    const LocalStorageManagerPrivate & localStorageManager, Type type) :
    m_db(db),
    m_localStorageManager(localStorageManager), m_type(type),
    m_committed(false), m_rolledBack(false), m_ended(false)
{
    init();
}

Transaction::~Transaction()
{
    if ((m_type != Type::Selection) && !m_committed && !m_rolledBack) {
        QSqlQuery query(m_db);
        bool res = query.exec(QStringLiteral("ROLLBACK"));
        if (!res) {
            ErrorString errorMessage(QT_TRANSLATE_NOOP(
                "Transaction", "Can't rollback the SQL transaction"));
            QSqlError error = query.lastError();
            QMetaObject::invokeMethod(
                const_cast<LocalStorageManagerPrivate *>(
                    &m_localStorageManager),
                "processPostTransactionException", Qt::QueuedConnection,
                Q_ARG(ErrorString, errorMessage), Q_ARG(QSqlError, error));
        }
    }
    else if ((m_type == Type::Selection) && !m_ended) {
        QSqlQuery query(m_db);
        bool res = query.exec(QStringLiteral("END"));
        if (!res) {
            ErrorString errorMessage(QT_TRANSLATE_NOOP(
                "Transaction", "Can't end the SQL transaction"));
            QSqlError error = query.lastError();
            QMetaObject::invokeMethod(
                const_cast<LocalStorageManagerPrivate *>(
                    &m_localStorageManager),
                "processPostTransactionException", Qt::QueuedConnection,
                Q_ARG(ErrorString, errorMessage), Q_ARG(QSqlError, error));
        }
    }
}

bool Transaction::commit(ErrorString & errorDescription)
{
    if (m_type == Type::Selection) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "Transaction", "Can't commit the transaction of selection type"));
        return false;
    }

    QSqlQuery query(m_db);
    bool res = query.exec(QStringLiteral("COMMIT"));
    if (!res) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "Transaction", "Can't commit the SQL transaction"));
        errorDescription.details() = query.lastError().text();
        QNWARNING(
            "local_storage",
            errorDescription << ", full last query error: "
                             << query.lastError());
        return false;
    }

    m_committed = true;
    return true;
}

bool Transaction::rollback(ErrorString & errorDescription)
{
    if (m_type == Type::Selection) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "Transaction", "Can't rollback the transaction of selection type"));
        return false;
    }

    QSqlQuery query(m_db);
    bool res = query.exec(QStringLiteral("ROLLBACK"));
    if (!res) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "Transaction", "Can't rollback the SQL transaction"));
        errorDescription.details() = query.lastError().text();
        QNWARNING(
            "local_storage",
            errorDescription << ", full last query error: "
                             << query.lastError());
        return false;
    }

    m_rolledBack = true;
    return true;
}

bool Transaction::end(ErrorString & errorDescription)
{
    if (m_type != Type::Selection) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "Transaction",
            "Only transactions used for selection queries should be "
            "explicitly ended without committing the changes"));
        return false;
    }

    QSqlQuery query(m_db);
    bool res = query.exec(QStringLiteral("END"));
    if (!res) {
        errorDescription.setBase(
            QT_TRANSLATE_NOOP("Transaction", "Can't end the SQL transaction"));
        errorDescription.details() = query.lastError().text();
        QNWARNING(
            "local_storage",
            errorDescription << ", full last query error: "
                             << query.lastError());
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

    QSqlQuery query(m_db);
    bool res = query.exec(queryString);
    if (!res) {
        QNERROR(
            "local_storage",
            "Error beginning the SQL transaction: " << query.lastError());

        ErrorString errorDescription(QT_TRANSLATE_NOOP(
            "Transaction", "Can't begin the SQL transaction"));

        errorDescription.details() = query.lastError().text();
        throw DatabaseRequestException(errorDescription);
    }
}

} // namespace quentier
