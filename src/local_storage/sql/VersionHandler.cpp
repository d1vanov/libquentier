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

#include "VersionHandler.h"
#include "ConnectionPool.h"
#include "ErrorHandling.h"

#include "patches/Patch1To2.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/RuntimeError.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/threading/Future.h>
#include <quentier/threading/Runnable.h>

#include <QException>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include <quentier/threading/Qt5Promise.h>
#endif

#include <QSqlQuery>
#include <QSqlRecord>
#include <QThreadPool>

namespace quentier::local_storage::sql {

VersionHandler::VersionHandler(
    Account account, ConnectionPoolPtr connectionPool,
    threading::QThreadPoolPtr threadPool, threading::QThreadPtr writerThread) :
    m_account{std::move(account)},
    m_connectionPool{std::move(connectionPool)},
    m_threadPool{std::move(threadPool)}, m_writerThread{std::move(writerThread)}
{
    if (Q_UNLIKELY(m_account.isEmpty())) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "local_storage::sql::VersionHandler",
            "VersionHandler ctor: account is empty")}};
    }

    if (Q_UNLIKELY(!m_connectionPool)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "local_storage::sql::VersionHandler",
            "VersionHandler ctor: connection pool is null")}};
    }

    if (Q_UNLIKELY(!m_threadPool)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "local_storage::sql::VersionHandler",
            "VersionHandler ctor: thread pool is null")}};
    }

    if (Q_UNLIKELY(!m_writerThread)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "local_storage::sql::VersionHandler",
            "VersionHandler ctor: writer thread is null")}};
    }
}

QFuture<bool> VersionHandler::isVersionTooHigh() const
{
    auto promise = std::make_shared<QPromise<bool>>();
    const auto future = promise->future();

    promise->start();

    auto * runnable = threading::createFunctionRunnable(
        [promise = std::move(promise), self_weak = weak_from_this()]() mutable {
            const auto self = self_weak.lock();
            if (!self) {
                promise->setException(
                    RuntimeError(ErrorString{QT_TRANSLATE_NOOP(
                        "local_storage::sql::VersionHandler",
                        "VersionHandler is already destroyed")}));
                promise->finish();
                return;
            }

            auto databaseConnection = self->m_connectionPool->database();

            ErrorString errorDescription;
            const qint32 currentVersion =
                self->versionImpl(databaseConnection, errorDescription);

            if (currentVersion < 0) {
                promise->setException(
                    DatabaseRequestException{errorDescription});
                promise->finish();
                return;
            }

            const qint32 highestSupportedVersion =
                self->highestSupportedVersionImpl();

            promise->addResult(currentVersion > highestSupportedVersion);
            promise->finish();
        });

    m_threadPool->start(runnable);
    return future;
}

QFuture<bool> VersionHandler::requiresUpgrade() const
{
    auto promise = std::make_shared<QPromise<bool>>();
    const auto future = promise->future();

    promise->start();

    auto * runnable = threading::createFunctionRunnable(
        [promise = std::move(promise), self_weak = weak_from_this()]() mutable {
            const auto self = self_weak.lock();
            if (!self) {
                promise->setException(
                    RuntimeError(ErrorString{QT_TRANSLATE_NOOP(
                        "local_storage::sql::VersionHandler",
                        "VersionHandler is already destroyed")}));
                promise->finish();
                return;
            }

            auto databaseConnection = self->m_connectionPool->database();

            ErrorString errorDescription;
            const qint32 currentVersion =
                self->versionImpl(databaseConnection, errorDescription);

            if (currentVersion < 0) {
                promise->setException(
                    DatabaseRequestException{errorDescription});
                promise->finish();
                return;
            }

            const qint32 highestSupportedVersion =
                self->highestSupportedVersionImpl();

            promise->addResult(currentVersion < highestSupportedVersion);
            promise->finish();
        });

    m_threadPool->start(runnable);
    return future;
}

QFuture<QList<IPatchPtr>> VersionHandler::requiredPatches() const
{
    auto promise = std::make_shared<QPromise<QList<IPatchPtr>>>();
    const auto future = promise->future();

    promise->start();

    auto * runnable = threading::createFunctionRunnable(
        [promise = std::move(promise), self_weak = weak_from_this()]() mutable {
            const auto self = self_weak.lock();
            if (!self) {
                promise->setException(
                    RuntimeError(ErrorString{QT_TRANSLATE_NOOP(
                        "local_storage::sql::VersionHandler",
                        "VersionHandler is already destroyed")}));
                promise->finish();
                return;
            }

            auto databaseConnection = self->m_connectionPool->database();

            ErrorString errorDescription;
            const qint32 currentVersion =
                self->versionImpl(databaseConnection, errorDescription);

            if (currentVersion < 0) {
                promise->setException(
                    DatabaseRequestException{errorDescription});
                promise->finish();
                return;
            }

            QList<IPatchPtr> patches;
            if (currentVersion == 1) {
                patches.append(std::make_shared<Patch1To2>(
                    self->m_account, self->m_connectionPool,
                    self->m_writerThread));
            }

            promise->addResult(patches);
            promise->finish();
        });

    m_threadPool->start(runnable);
    return future;
}

QFuture<qint32> VersionHandler::version() const
{
    auto promise = std::make_shared<QPromise<qint32>>();
    const auto future = promise->future();

    promise->start();

    auto * runnable = threading::createFunctionRunnable(
        [promise = std::move(promise), self_weak = weak_from_this()]() mutable {
            const auto self = self_weak.lock();
            if (!self) {
                promise->setException(
                    RuntimeError(ErrorString{QT_TRANSLATE_NOOP(
                        "local_storage::sql::VersionHandler",
                        "VersionHandler is already destroyed")}));
                promise->finish();
                return;
            }

            auto databaseConnection = self->m_connectionPool->database();

            ErrorString errorDescription;
            const qint32 currentVersion =
                self->versionImpl(databaseConnection, errorDescription);

            if (currentVersion < 0) {
                promise->setException(
                    DatabaseRequestException{errorDescription});
                promise->finish();
                return;
            }

            promise->addResult(currentVersion);
            promise->finish();
        });

    m_threadPool->start(runnable);
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
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::version_handler",
            "failed to execute SQL query checking whether "
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
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::version_handler",
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
    return 2;
}

} // namespace quentier::local_storage::sql
