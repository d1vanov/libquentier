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

#include "Fwd.h"

#include <quentier/local_storage/Fwd.h>

#include <qevercloud/types/User.h>

#include <QFuture>
#include <QtGlobal>

#include <memory>
#include <optional>

class QSqlDatabase;
class QSqlRecord;

namespace quentier {

class ErrorString;

} // namespace quentier

namespace quentier::local_storage::sql {

class UsersHandler final: public std::enable_shared_from_this<UsersHandler>
{
public:
    explicit UsersHandler(
        ConnectionPoolPtr connectionPool, QThreadPool * threadPool,
        QThreadPtr writerThread);

    [[nodiscard]] QFuture<quint32> userCount() const;
    [[nodiscard]] QFuture<void> putUser(qevercloud::User user);

    [[nodiscard]] QFuture<std::optional<qevercloud::User>> findUserById(
        qevercloud::UserID userId) const;

    [[nodiscard]] QFuture<void> expungeUserById(qevercloud::UserID userId);
    [[nodiscard]] QFuture<QList<qevercloud::User>> listUsers() const;

private:
    [[nodiscard]] std::optional<quint32> userCountImpl(
        QSqlDatabase & database, ErrorString & errorDescription) const;

    [[nodiscard]] bool putUserImpl(
        const qevercloud::User & user, QSqlDatabase & database,
        ErrorString & errorDescription);

    [[nodiscard]] bool putCommonUserData(
        const qevercloud::User & user, const QString & userId,
        QSqlDatabase & database, ErrorString & errorDescription);

    [[nodiscard]] bool putUserAttributes(
        const qevercloud::UserAttributes & userAttributes,
        const QString & userId,
        QSqlDatabase & database, ErrorString & errorDescription);

    [[nodiscard]] bool removeUserAttributes(
        const QString & userId, QSqlDatabase & database,
        ErrorString & errorDescription);

    [[nodiscard]] bool putAccounting(
        const qevercloud::Accounting & accounting, const QString & userId,
        QSqlDatabase & database, ErrorString & errorDescription);

    [[nodiscard]] bool removeAccounting(
        const QString & userId, QSqlDatabase & database,
        ErrorString & errorDescription);

    [[nodiscard]] bool putAccountLimits(
        const qevercloud::AccountLimits & accountLimits, const QString & userId,
        QSqlDatabase & database, ErrorString & errorDescription);

    [[nodiscard]] bool removeAccountLimits(
        const QString & userId, QSqlDatabase & database,
        ErrorString & errorDescription);

    [[nodiscard]] bool putBusinessUserInfo(
        const qevercloud::BusinessUserInfo & info, const QString & userId,
        QSqlDatabase & database, ErrorString & errorDescription);

    [[nodiscard]] bool removeBusinessUserInfo(
        const QString & userId, QSqlDatabase & database,
        ErrorString & errorDescription);

    [[nodiscard]] std::optional<qevercloud::User> findUserByIdImpl(
        qevercloud::UserID userId, QSqlDatabase & database,
        ErrorString & errorDescription) const;

    [[nodiscard]] bool fillUserFromSqlRecord(
        const QSqlRecord & record, qevercloud::User & user,
        ErrorString & errorDescription) const;

    [[nodiscard]] bool expungeUserByIdImpl(
        qevercloud::UserID userId, QSqlDatabase & database,
        ErrorString & errorDescription);

    [[nodiscard]] QList<qevercloud::User> listUsersImpl(
        QSqlDatabase & database, ErrorString & errorDescription) const;

private:
    ConnectionPoolPtr m_connectionPool;
    QThreadPool * m_threadPool;
    QThreadPtr m_writerThread;
};

} // namespace quentier::local_storage::sql
