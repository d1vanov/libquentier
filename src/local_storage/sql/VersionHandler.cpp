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

#include "VersionHandler.h"
#include "ConnectionPool.h"
#include "ErrorHandling.h"

#include "patches/Patch1To2.h"
#include "patches/Patch2To3.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/RuntimeError.h>
#include <quentier/local_storage/LocalStorageOperationException.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/threading/Future.h>
#include <quentier/threading/Post.h>

#include <QException>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include <quentier/threading/Qt5Promise.h>
#endif

#include <QSqlQuery>
#include <QSqlRecord>

namespace quentier::local_storage::sql {

VersionHandler::VersionHandler(
    Account account, ConnectionPoolPtr connectionPool,
    threading::QThreadPtr thread) :
    m_account{std::move(account)}, m_connectionPool{std::move(connectionPool)},
    m_thread{std::move(thread)}
{
    if (Q_UNLIKELY(m_account.isEmpty())) {
        throw InvalidArgument{ErrorString{
            QStringLiteral("VersionHandler ctor: account is empty")}};
    }

    if (Q_UNLIKELY(!m_connectionPool)) {
        throw InvalidArgument{ErrorString{
            QStringLiteral("VersionHandler ctor: connection pool is null")}};
    }

    if (Q_UNLIKELY(!m_thread)) {
        throw InvalidArgument{
            ErrorString{QStringLiteral("VersionHandler ctor: thread is null")}};
    }
}

QFuture<bool> VersionHandler::isVersionTooHigh() const
{
    auto promise = std::make_shared<QPromise<bool>>();
    const auto future = promise->future();

    promise->start();

    auto task = [promise = std::move(promise),
                 self_weak = weak_from_this()]() mutable {
        const auto self = self_weak.lock();
        if (!self) {
            promise->setException(RuntimeError(ErrorString{
                QStringLiteral("VersionHandler is already destroyed")}));
            promise->finish();
            return;
        }

        auto databaseConnection = self->m_connectionPool->database();

        ErrorString errorDescription;
        const qint32 currentVersion =
            self->versionImpl(databaseConnection, errorDescription);

        if (currentVersion < 0) {
            promise->setException(
                LocalStorageOperationException{errorDescription});
            promise->finish();
            return;
        }

        const qint32 highestSupportedVersion =
            self->highestSupportedVersionImpl();

        promise->addResult(currentVersion > highestSupportedVersion);
        promise->finish();
    };

    if (m_thread.get() == QThread::currentThread()) {
        task();
    }
    else {
        threading::postToThread(m_thread.get(), std::move(task));
    }

    return future;
}

QFuture<bool> VersionHandler::requiresUpgrade() const
{
    auto promise = std::make_shared<QPromise<bool>>();
    const auto future = promise->future();

    promise->start();

    auto task = [promise = std::move(promise),
                 self_weak = weak_from_this()]() mutable {
        const auto self = self_weak.lock();
        if (!self) {
            promise->setException(RuntimeError(ErrorString{
                QStringLiteral("VersionHandler is already destroyed")}));
            promise->finish();
            return;
        }

        auto databaseConnection = self->m_connectionPool->database();

        ErrorString errorDescription;
        const qint32 currentVersion =
            self->versionImpl(databaseConnection, errorDescription);

        if (currentVersion < 0) {
            promise->setException(
                LocalStorageOperationException{errorDescription});
            promise->finish();
            return;
        }

        const qint32 highestSupportedVersion =
            self->highestSupportedVersionImpl();

        promise->addResult(currentVersion < highestSupportedVersion);
        promise->finish();
    };

    if (m_thread.get() == QThread::currentThread()) {
        task();
    }
    else {
        threading::postToThread(m_thread.get(), std::move(task));
    }

    return future;
}

QFuture<QList<IPatchPtr>> VersionHandler::requiredPatches() const
{
    auto promise = std::make_shared<QPromise<QList<IPatchPtr>>>();
    const auto future = promise->future();

    promise->start();

    auto task = [promise = std::move(promise),
                 self_weak = weak_from_this()]() mutable {
        const auto self = self_weak.lock();
        if (!self) {
            promise->setException(RuntimeError(ErrorString{
                QStringLiteral("VersionHandler is already destroyed")}));
            promise->finish();
            return;
        }

        auto databaseConnection = self->m_connectionPool->database();

        ErrorString errorDescription;
        const qint32 currentVersion =
            self->versionImpl(databaseConnection, errorDescription);

        if (currentVersion < 0) {
            promise->setException(
                LocalStorageOperationException{errorDescription});
            promise->finish();
            return;
        }

        QList<IPatchPtr> patches;
        if (currentVersion < 2) {
            patches.append(std::make_shared<Patch1To2>(
                self->m_account, self->m_connectionPool, self->m_thread));
        }

        if (currentVersion < 3) {
            patches.append(std::make_shared<Patch2To3>(
                self->m_account, self->m_connectionPool, self->m_thread));
        }

        promise->addResult(patches);
        promise->finish();
    };

    if (m_thread.get() == QThread::currentThread()) {
        task();
    }
    else {
        threading::postToThread(m_thread.get(), std::move(task));
    }

    return future;
}

QFuture<qint32> VersionHandler::version() const
{
    auto promise = std::make_shared<QPromise<qint32>>();
    const auto future = promise->future();

    promise->start();

    auto task = [promise = std::move(promise),
                 self_weak = weak_from_this()]() mutable {
        const auto self = self_weak.lock();
        if (!self) {
            promise->setException(RuntimeError(ErrorString{
                QStringLiteral("VersionHandler is already destroyed")}));
            promise->finish();
            return;
        }

        auto databaseConnection = self->m_connectionPool->database();

        ErrorString errorDescription;
        const qint32 currentVersion =
            self->versionImpl(databaseConnection, errorDescription);

        if (currentVersion < 0) {
            promise->setException(
                LocalStorageOperationException{errorDescription});
            promise->finish();
            return;
        }

        promise->addResult(currentVersion);
        promise->finish();
    };

    if (m_thread.get() == QThread::currentThread()) {
        task();
    }
    else {
        threading::postToThread(m_thread.get(), std::move(task));
    }

    return future;
}

QFuture<qint32> VersionHandler::highestSupportedVersion() const
{
    return threading::makeReadyFuture(highestSupportedVersionImpl());
}

qint32 VersionHandler::versionImpl(
    QSqlDatabase & databaseConnection, ErrorString & errorDescription) const
{
    const QString queryString =
        QStringLiteral("SELECT version FROM Auxiliary LIMIT 1");

    QSqlQuery query{databaseConnection};
    bool res = query.exec(queryString);

    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::version_handler",
        QStringLiteral("failed to execute SQL query checking whether "
                       "the database requires an upgrade"),
        -1);

    if (!query.next()) {
        QNDEBUG(
            "local_storage::sql::version_handler",
            "No version was found within the local "
                << "storage database, assuming version 1");
        return 1;
    }

    const QVariant value = query.record().value(QStringLiteral("version"));
    bool conversionResult = false;
    const int version = value.toInt(&conversionResult);
    if (Q_UNLIKELY(!conversionResult)) {
        errorDescription.setBase(QStringLiteral(
            "failed to decode the current local storage database "
            "version"));
        QNWARNING(
            "local_storage::sql::version_handler",
            errorDescription << ", value = " << value);
        return -1;
    }

    QNDEBUG("local_storage::sql::version_handler", "Version = " << version);
    return version;
}

qint32 VersionHandler::highestSupportedVersionImpl() const noexcept
{
    return 3;
}

} // namespace quentier::local_storage::sql
