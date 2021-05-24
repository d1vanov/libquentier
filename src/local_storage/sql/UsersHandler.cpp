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
#include "UsersHandler.h"

#include <quentier/exception/DatabaseRequestException.h>
#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/RuntimeError.h>

#include <utility/Threading.h>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include <utility/Qt5Promise.h>
#endif

#include <QThreadPool>

namespace quentier::local_storage::sql {

UsersHandler::UsersHandler(
    ConnectionPoolPtr connectionPool, QThreadPool * threadPool,
    QThreadPtr writerThread) :
    m_connectionPool{std::move(connectionPool)},
    m_threadPool{threadPool},
    m_writerThread{std::move(writerThread)}
{
    if (Q_UNLIKELY(!m_connectionPool)) {
        throw InvalidArgument{ErrorString{
            QT_TRANSLATE_NOOP(
                "local_storage::sql::UsersHandler",
                "UsersHandler ctor: connection pool is null")}};
    }

    if (Q_UNLIKELY(!m_threadPool)) {
        throw InvalidArgument{ErrorString{
            QT_TRANSLATE_NOOP(
                "local_storage::sql::UsersHandler",
                "UsersHandler ctor: thread pool is null")}};
    }

    if (Q_UNLIKELY(!m_writerThread)) {
        throw InvalidArgument{ErrorString{
            QT_TRANSLATE_NOOP(
                "local_storage::sql::UsersHandler",
                "UsersHandler ctor: writer thread is null")}};
    }
}

QFuture<quint32> UsersHandler::userCount() const
{
    auto promise = std::make_shared<QPromise<quint32>>();
    auto future = promise->future();

    promise->start();

    auto * runnable = utility::createFunctionRunnable(
        [promise = std::move(promise),
         self_weak = weak_from_this()]
         {
            const auto self = self_weak.lock();
            if (!self) {
                promise->setException(RuntimeError(ErrorString{
                    QT_TRANSLATE_NOOP(
                        "local_storage::sql::UsersHandler",
                        "UsersHandler is dead")}));
                promise->finish();
                return;
            }

            auto databaseConnection = self->m_connectionPool->database();

            ErrorString errorDescription;
            const auto userCount = self->userCountImpl(
                databaseConnection, errorDescription);

            if (!userCount) {
                promise->setException(
                    DatabaseRequestException{errorDescription});
                promise->finish();
                return;
            }

            promise->addResult(*userCount);
            promise->finish();
         });

    m_threadPool->start(runnable);
    return future;
}

QFuture<void> UsersHandler::putUser(qevercloud::User user)
{
    auto promise = std::make_shared<QPromise<void>>();
    auto future = promise->future();

    promise->start();

    auto * runnable = utility::createFunctionRunnable(
        [promise = std::move(promise),
         self_weak = weak_from_this(),
         user = std::move(user)]
         {
            const auto self = self_weak.lock();
            if (!self) {
                promise->setException(RuntimeError(ErrorString{
                    QT_TRANSLATE_NOOP(
                        "local_storage::sql::UsersHandler",
                        "UsersHandler is dead")}));
                promise->finish();
                return;
            }

            auto databaseConnection = self->m_connectionPool->database();

            ErrorString errorDescription;
            const bool res = self->putUserImpl(
                user, databaseConnection, errorDescription);

            if (!res) {
                promise->setException(
                    DatabaseRequestException{errorDescription});
            }

            promise->finish();
         });

    m_threadPool->start(runnable);
    return future;
}

QFuture<std::optional<qevercloud::User>> UsersHandler::findUserById(
    qevercloud::UserID userId) const
{
    auto promise =
        std::make_shared<QPromise<std::optional<qevercloud::User>>>();

    auto future = promise->future();

    promise->start();

    // TODO: implement
    Q_UNUSED(userId)

    return future;
}

QFuture<void> UsersHandler::expungeUserById(qevercloud::UserID userId)
{
    auto promise = std::make_shared<QPromise<void>>();
    auto future = promise->future();

    promise->start();

    // TODO: implement
    Q_UNUSED(userId)

    return future;
}

QFuture<QList<qevercloud::User>> UsersHandler::listUsers() const
{
    auto promise = std::make_shared<QPromise<QList<qevercloud::User>>>();
    auto future = promise->future();

    promise->start();

    // TODO: implement

    return future;
}

std::optional<quint32> UsersHandler::userCountImpl(
    QSqlDatabase & database, ErrorString & errorDescription) const
{
    // TODO: implement
    Q_UNUSED(database)
    Q_UNUSED(errorDescription)
    return {};
}

bool UsersHandler::putUserImpl(
    const qevercloud::User & user, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    // TODO: implement
    Q_UNUSED(user)
    Q_UNUSED(database)
    Q_UNUSED(errorDescription)
    return true;
}

} // namespace quentier::local_storage::sql
