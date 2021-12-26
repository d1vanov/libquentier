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

#include "IUsersHandler.h"

#include <QtGlobal>

#include <memory>
#include <optional>

class QSqlDatabase;

namespace quentier {

class ErrorString;

} // namespace quentier

namespace quentier::local_storage::sql {

class UsersHandler final:
    public IUsersHandler,
    public std::enable_shared_from_this<UsersHandler>
{
public:
    explicit UsersHandler(
        ConnectionPoolPtr connectionPool, QThreadPool * threadPool,
        Notifier * notifier, QThreadPtr writerThread);

    [[nodiscard]] QFuture<quint32> userCount() const override;
    [[nodiscard]] QFuture<void> putUser(qevercloud::User user) override;

    [[nodiscard]] QFuture<std::optional<qevercloud::User>> findUserById(
        qevercloud::UserID userId) const override;

    [[nodiscard]] QFuture<void> expungeUserById(
        qevercloud::UserID userId) override;

private:
    [[nodiscard]] std::optional<quint32> userCountImpl(
        QSqlDatabase & database, ErrorString & errorDescription) const;

    [[nodiscard]] std::optional<qevercloud::User> findUserByIdImpl(
        qevercloud::UserID userId, QSqlDatabase & database,
        ErrorString & errorDescription) const;

    [[nodiscard]] bool findUserAttributesViewedPromotionsById(
        const QString & userId, QSqlDatabase & database,
        qevercloud::UserAttributes & userAttributes,
        ErrorString & errorDescription) const;

    [[nodiscard]] bool findUserAttributesRecentMailedAddressesById(
        const QString & userId, QSqlDatabase & database,
        qevercloud::UserAttributes & userAttributes,
        ErrorString & errorDescription) const;

    [[nodiscard]] bool expungeUserByIdImpl(
        qevercloud::UserID userId, QSqlDatabase & database,
        ErrorString & errorDescription);

    [[nodiscard]] TaskContext makeTaskContext() const;

private:
    ConnectionPoolPtr m_connectionPool;
    QThreadPool * m_threadPool;
    Notifier * m_notifier;
    QThreadPtr m_writerThread;
};

} // namespace quentier::local_storage::sql
