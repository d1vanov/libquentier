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
#include "VersionHandler.h"

#include <quentier/utility/Threading.h>

#include <QException>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include "../../utility/Qt5Promise.h"
#endif

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

            const qint32 currentVersion = self->versionImpl();
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
    // TODO: implement
    QPromise<bool> promise;
    QFuture<bool> future = promise.future();

    promise.start();
    promise.addResult(false);
    promise.finish();

    return future;
}

QFuture<QList<ILocalStoragePatchPtr>> VersionHandler::requiredPatches() const
{
    // TODO: implement
    QPromise<QList<ILocalStoragePatchPtr>> promise;
    QFuture<QList<ILocalStoragePatchPtr>> future = promise.future();

    promise.start();
    promise.addResult(QList<ILocalStoragePatchPtr>{});
    promise.finish();

    return future;
}

QFuture<qint32> VersionHandler::version() const
{
    // TODO: implement
    QPromise<qint32> promise;
    QFuture<qint32> future = promise.future();

    promise.start();
    promise.addResult(false);
    promise.finish();

    return future;
}

QFuture<qint32> VersionHandler::highestSupportedVersion() const
{
    // TODO: implement
    QPromise<qint32> promise;
    QFuture<qint32> future = promise.future();

    promise.start();
    promise.addResult(false);
    promise.finish();

    return future;
}

qint32 VersionHandler::versionImpl() const
{
    // TODO: implement
    return 0;
}

qint32 VersionHandler::highestSupportedVersionImpl() const
{
    // TODO: implement
    return 0;
}

} // namespace quentier::local_storage::sql
