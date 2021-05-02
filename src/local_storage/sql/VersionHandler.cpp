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
#include "ErrorHandling.h"
#include "VersionHandler.h"

#include "../src/utility/ThreadingPrivate.h"

#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/Threading.h>

#include <QException>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include "../../utility/Qt5Promise.h"
#endif

#include <QSqlQuery>
#include <QSqlRecord>
#include <QThreadPool>

namespace quentier::local_storage::sql {

class Q_DECL_HIDDEN VersionHandlerDeadException final: public QException
{
public:
    VersionHandlerDeadException()
        : m_errorDescription{
            "quentier::local_storage::sql::VersionHandler is dead"}
    {}

    void raise() const override
    {
        throw *this;
    }

    [[nodiscard]] VersionHandlerDeadException * clone() const override
    {
        return new VersionHandlerDeadException;
    }

    [[nodiscard]] const char * what() const noexcept override
    {
        return m_errorDescription.constData();
    }

private:
    QByteArray m_errorDescription;
};

VersionHandler::VersionHandler(
    ConnectionPoolPtr pConnectionPool, QThreadPool * pThreadPool) :
    m_pConnectionPool{std::move(pConnectionPool)},
    m_pThreadPool{pThreadPool}
{
    Q_ASSERT(m_pConnectionPool);
    Q_ASSERT(m_pThreadPool);
}

QFuture<bool> VersionHandler::isVersionTooHigh() const
{
    auto pResultPromise= std::make_shared<QPromise<bool>>();
    QFuture<bool> future = pResultPromise->future();

    auto * pRunnable = utility::createFunctionRunnable(
        [pResultPromise = std::move(pResultPromise),
         self_weak = weak_from_this()] () mutable
        {
            pResultPromise->start();

            const auto self = self_weak.lock();
            if (!self) {
                pResultPromise->setException(VersionHandlerDeadException());
                pResultPromise->finish();
                return;
            }

            auto databaseConnection = self->m_pConnectionPool->database();

            const qint32 currentVersion = self->versionImpl(databaseConnection);
            if (currentVersion < 0) {
                pResultPromise->addResult(false);
                pResultPromise->finish();
                return;
            }

            const qint32 highestSupportedVersion =
                self->highestSupportedVersionImpl();

            pResultPromise->addResult(currentVersion > highestSupportedVersion);
            pResultPromise->finish();
        });

    m_pThreadPool->start(pRunnable);
    return future;
}

QFuture<bool> VersionHandler::requiresUpgrade() const
{
    auto pResultPromise= std::make_shared<QPromise<bool>>();
    QFuture<bool> future = pResultPromise->future();

    auto * pRunnable = utility::createFunctionRunnable(
        [pResultPromise = std::move(pResultPromise),
         self_weak = weak_from_this()] () mutable
        {
            pResultPromise->start();

            const auto self = self_weak.lock();
            if (!self) {
                pResultPromise->setException(VersionHandlerDeadException());
                pResultPromise->finish();
                return;
            }

            auto databaseConnection = self->m_pConnectionPool->database();

            const qint32 currentVersion = self->versionImpl(databaseConnection);
            if (currentVersion < 0) {
                pResultPromise->addResult(false);
                pResultPromise->finish();
                return;
            }

            const qint32 highestSupportedVersion =
                self->highestSupportedVersionImpl();

            pResultPromise->addResult(currentVersion < highestSupportedVersion);
            pResultPromise->finish();
        });

    m_pThreadPool->start(pRunnable);
    return future;
}

QFuture<QList<ILocalStoragePatchPtr>> VersionHandler::requiredPatches() const
{
    auto pResultPromise= std::make_shared<QPromise<QList<ILocalStoragePatchPtr>>>();
    QFuture<QList<ILocalStoragePatchPtr>> future = pResultPromise->future();

    auto * pRunnable = utility::createFunctionRunnable(
        [pResultPromise = std::move(pResultPromise),
         self_weak = weak_from_this()] () mutable
        {
            pResultPromise->start();

            const auto self = self_weak.lock();
            if (!self) {
                pResultPromise->setException(VersionHandlerDeadException());
                pResultPromise->finish();
                return;
            }

            auto databaseConnection = self->m_pConnectionPool->database();

            const qint32 currentVersion = self->versionImpl(databaseConnection);

            QList<ILocalStoragePatchPtr> patches;
            if (currentVersion == 1) {
                // TODO: append patch from 1 to 2 when all the interface
                // problems get figured out
                /*
                patches.append(std::make_shared<LocalStoragePatch1To2>(
                        m_account, m_localStorageManager, m_sqlDatabase));
                */
            }

            pResultPromise->addResult(patches);
            pResultPromise->finish();
        });

    m_pThreadPool->start(pRunnable);
    return future;
}

QFuture<qint32> VersionHandler::version() const
{
    auto pResultPromise= std::make_shared<QPromise<qint32>>();
    QFuture<qint32> future = pResultPromise->future();

    auto * pRunnable = utility::createFunctionRunnable(
        [pResultPromise = std::move(pResultPromise),
         self_weak = weak_from_this()] () mutable
        {
            pResultPromise->start();

            const auto self = self_weak.lock();
            if (!self) {
                pResultPromise->setException(VersionHandlerDeadException());
                pResultPromise->finish();
                return;
            }

            auto databaseConnection = self->m_pConnectionPool->database();

            const qint32 currentVersion = self->versionImpl(databaseConnection);
            pResultPromise->addResult(currentVersion);
            pResultPromise->finish();
        });

    m_pThreadPool->start(pRunnable);
    return future;
}

QFuture<qint32> VersionHandler::highestSupportedVersion() const
{
    return utility::makeReadyFuture(highestSupportedVersionImpl());
}

qint32 VersionHandler::versionImpl(QSqlDatabase & databaseConnection) const
{
    const QString queryString =
        QStringLiteral("SELECT version FROM Auxiliary LIMIT 1");

    QSqlQuery query{databaseConnection};
    bool res = query.exec(queryString);
    ENSURE_DB_REQUEST(
        res, query, "local_storage::sql::version_handler",
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::version_handler",
            "failed to execute SQL query checking whether "
             "the database requires an upgrade"));

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
        ErrorString errorDescription{QT_TRANSLATE_NOOP(
                "quentier::local_storage::sql::version_handler",
                "failed to decode the current local storage database "
                "version")};
        QNWARNING(
            "local_storage::sql::version_handler",
            errorDescription << ", value = " << value);
        throw DatabaseRequestException{errorDescription};
    }

    QNDEBUG("local_storage::sql::version_handler", "Version = " << version);
    return version;
}

qint32 VersionHandler::highestSupportedVersionImpl() const noexcept
{
    return 2;
}

} // namespace quentier::local_storage::sql
