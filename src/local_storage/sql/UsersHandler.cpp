/*
 * Copyright 2021-2022 Dmitry Ivanov
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
#include "Notifier.h"
#include "Tasks.h"
#include "Transaction.h"
#include "TypeChecks.h"
#include "UsersHandler.h"

#include "utils/Common.h"
#include "utils/FillFromSqlRecordUtils.h"
#include "utils/PutToDatabaseUtils.h"
#include "utils/RemoveFromDatabaseUtils.h"

#include <quentier/exception/DatabaseRequestException.h>
#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/RuntimeError.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/threading/Future.h>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include <quentier/threading/Qt5Promise.h>
#endif

#include <QSqlQuery>
#include <QSqlRecord>
#include <QThreadPool>

#include <optional>
#include <type_traits>

namespace quentier::local_storage::sql {

UsersHandler::UsersHandler(
    ConnectionPoolPtr connectionPool, threading::QThreadPoolPtr threadPool,
    Notifier * notifier, threading::QThreadPtr writerThread) :
    m_connectionPool{std::move(connectionPool)},
    m_threadPool{std::move(threadPool)}, m_notifier{notifier},
    m_writerThread{std::move(writerThread)}
{
    if (Q_UNLIKELY(!m_connectionPool)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "local_storage::sql::UsersHandler",
            "UsersHandler ctor: connection pool is null")}};
    }

    if (Q_UNLIKELY(!m_threadPool)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "local_storage::sql::UsersHandler",
            "UsersHandler ctor: thread pool is null")}};
    }

    if (Q_UNLIKELY(!m_notifier)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "local_storage::sql::UsersHandler",
            "UsersHandler ctor: notifier is null")}};
    }

    if (Q_UNLIKELY(!m_writerThread)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "local_storage::sql::UsersHandler",
            "UsersHandler ctor: writer thread is null")}};
    }
}

QFuture<quint32> UsersHandler::userCount() const
{
    return makeReadTask<quint32>(
        makeTaskContext(), weak_from_this(),
        [](const UsersHandler & handler, QSqlDatabase & database,
           ErrorString & errorDescription) {
            return handler.userCountImpl(database, errorDescription);
        });
}

QFuture<void> UsersHandler::putUser(qevercloud::User user)
{
    return makeWriteTask<void>(
        makeTaskContext(), weak_from_this(),
        [user = std::move(user)](
            UsersHandler & handler, QSqlDatabase & database,
            ErrorString & errorDescription) {
            const bool res = utils::putUser(user, database, errorDescription);
            if (res) {
                handler.m_notifier->notifyUserPut(user);
            }
            return res;
        });
}

QFuture<std::optional<qevercloud::User>> UsersHandler::findUserById(
    qevercloud::UserID userId) const
{
    return makeReadTask<std::optional<qevercloud::User>>(
        makeTaskContext(), weak_from_this(),
        [userId](
            const UsersHandler & handler, QSqlDatabase & database,
            ErrorString & errorDescription) {
            return handler.findUserByIdImpl(userId, database, errorDescription);
        });
}

QFuture<void> UsersHandler::expungeUserById(qevercloud::UserID userId)
{
    return makeWriteTask<void>(
        makeTaskContext(), weak_from_this(),
        [userId](
            UsersHandler & handler, QSqlDatabase & database,
            ErrorString & errorDescription) {
            const bool res =
                handler.expungeUserByIdImpl(userId, database, errorDescription);
            if (res) {
                handler.m_notifier->notifyUserExpunged(userId);
            }
            return res;
        });
}

std::optional<quint32> UsersHandler::userCountImpl(
    QSqlDatabase & database, ErrorString & errorDescription) const
{
    QSqlQuery query{database};
    const bool res =
        query.exec(QStringLiteral("SELECT COUNT(id) FROM Users WHERE "
                                  "userDeletionTimestamp IS NULL"));
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::UsersHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::UsersHandler",
            "Cannot count users in the local storage database"),
        std::nullopt);

    if (!query.next()) {
        QNDEBUG(
            "local_storage::sql::UsersHandler",
            "Found no users in the local storage database");
        return 0;
    }

    bool conversionResult = false;
    const int count = query.value(0).toInt(&conversionResult);
    if (Q_UNLIKELY(!conversionResult)) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "local_storage::sql::UsersHandler",
            "Cannot count users in the local storage database: failed to "
            "convert user count to int"));
        QNWARNING("local_storage:sql", errorDescription);
        return std::nullopt;
    }

    return count;
}

std::optional<qevercloud::User> UsersHandler::findUserByIdImpl(
    const qevercloud::UserID userId, QSqlDatabase & database,
    ErrorString & errorDescription) const
{
    QNDEBUG(
        "local_storage::sql::UsersHandler",
        "UsersHandler::findUserByIdImpl: user id = " << userId);

    utils::SelectTransactionGuard transactionGuard{database};

    static const QString queryString = QStringLiteral(
        "SELECT * FROM Users LEFT OUTER JOIN UserAttributes "
        "ON Users.id = UserAttributes.id "
        "LEFT OUTER JOIN Accounting ON Users.id = Accounting.id "
        "LEFT OUTER JOIN AccountLimits ON Users.id = AccountLimits.id "
        "LEFT OUTER JOIN BusinessUserInfo ON Users.id = BusinessUserInfo.id "
        "WHERE Users.id = :id");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::UsersHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::UsersHandler",
            "Cannot find user in the local storage database: failed to prepare "
            "query"),
        std::nullopt);

    const auto id = QString::number(userId);
    query.bindValue(":id", id);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::UsersHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::UsersHandler",
            "Cannot find user in the local storage database"),
        std::nullopt);

    if (!query.next()) {
        return std::nullopt;
    }

    const auto record = query.record();
    qevercloud::User user;
    user.setId(userId);
    ErrorString error;
    if (!utils::fillUserFromSqlRecord(record, user, error)) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "local_storage::sql::UsersHandler",
            "Failed to find user by id in the local storage database"));
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING("local_storage::sql::UsersHandler", errorDescription);
        return std::nullopt;
    }

    if (user.attributes()) {
        if (!findUserAttributesViewedPromotionsById(
                id, database, *user.mutableAttributes(), errorDescription))
        {
            return std::nullopt;
        }

        if (!findUserAttributesRecentMailedAddressesById(
                id, database, *user.mutableAttributes(), errorDescription))
        {
            return std::nullopt;
        }
    }

    return user;
}

bool UsersHandler::findUserAttributesViewedPromotionsById(
    const QString & userId, QSqlDatabase & database,
    qevercloud::UserAttributes & userAttributes,
    ErrorString & errorDescription) const
{
    static const QString queryString = QStringLiteral(
        "SELECT * FROM UserAttributesViewedPromotions WHERE id = :id");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::UsersHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::UsersHandler",
            "Cannot find user attributes' viewed promotions in the local "
            "storage database: failed to prepare query"),
        false);

    query.bindValue(QStringLiteral(":id"), userId);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::UsersHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::UsersHandler",
            "Cannot find user attributes' viewed promotions in the local "
            "storage database"),
        false);

    while (query.next()) {
        const auto record = query.record();
        const int promotionIndex = record.indexOf(QStringLiteral("promotion"));
        if (promotionIndex < 0) {
            continue;
        }

        const QVariant value = record.value(promotionIndex);
        if (value.isNull()) {
            continue;
        }

        if (!userAttributes.viewedPromotions()) {
            userAttributes.setViewedPromotions(QStringList{});
        }

        *userAttributes.mutableViewedPromotions() << value.toString();
    }

    return true;
}

bool UsersHandler::findUserAttributesRecentMailedAddressesById(
    const QString & userId, QSqlDatabase & database,
    qevercloud::UserAttributes & userAttributes,
    ErrorString & errorDescription) const
{
    static const QString queryString = QStringLiteral(
        "SELECT * FROM UserAttributesRecentMailedAddresses WHERE id = :id");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::UsersHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::UsersHandler",
            "Cannot find user attributes' recent mailed addresses in the local "
            "storage database: failed to prepare query"),
        false);

    query.bindValue(QStringLiteral(":id"), userId);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::UsersHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::UsersHandler",
            "Cannot find user attributes' recent mailed addresses in the local "
            "storage database"),
        false);

    while (query.next()) {
        const auto record = query.record();
        const int addressIndex = record.indexOf(QStringLiteral("address"));
        if (addressIndex < 0) {
            continue;
        }

        const QVariant value = record.value(addressIndex);
        if (value.isNull()) {
            continue;
        }

        if (!userAttributes.recentMailedAddresses()) {
            userAttributes.setRecentMailedAddresses(QStringList{});
        }

        *userAttributes.mutableRecentMailedAddresses() << value.toString();
    }

    return true;
}

bool UsersHandler::expungeUserByIdImpl(
    qevercloud::UserID userId, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    QNDEBUG(
        "local_storage::sql::UsersHandler",
        "UsersHandler::expungeUserByIdImpl: user id = " << userId);

    static const QString queryString =
        QStringLiteral("DELETE FROM Users WHERE id=:id");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::UsersHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::UsersHandler",
            "Cannot expunge user from the local storage database: failed to "
            "prepare query"),
        false);

    query.bindValue(QStringLiteral(":id"), QString::number(userId));

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::UsersHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::UsersHandler",
            "Cannot expunge user from the local storage database: failed to "
            "prepare query"),
        false);

    return true;
}

TaskContext UsersHandler::makeTaskContext() const
{
    return TaskContext{
        m_threadPool, m_writerThread, m_connectionPool,
        ErrorString{QT_TRANSLATE_NOOP(
            "local_storage::sql::UsersHandler",
            "UsersHandler is already destroyed")},
        ErrorString{QT_TRANSLATE_NOOP(
            "local_storage::sql::NotebooksHandler",
            "Request has been calceled")}};
}

} // namespace quentier::local_storage::sql
