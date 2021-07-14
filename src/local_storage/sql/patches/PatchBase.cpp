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

#include "PatchBase.h"

#include "../ConnectionPool.h"
#include "../ErrorHandling.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/RuntimeError.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/types/ErrorString.h>

#include <utility/Threading.h>

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <utility/Qt5Promise.h>
#endif

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#endif

namespace quentier::local_storage::sql {

PatchBase::PatchBase(
    ConnectionPoolPtr connectionPool, QThreadPtr writerThread,
    const QString & localStorageDirPath, const QString & backupDirPath) :
    m_connectionPool{std::move(connectionPool)},
    m_localStorageDir{localStorageDirPath},
    m_backupDir{backupDirPath},
    m_writerThread{std::move(writerThread)}
{
    if (Q_UNLIKELY(!m_connectionPool)) {
        throw InvalidArgument{ErrorString{
            QT_TRANSLATE_NOOP(
                "local_storage::sql::patches::PatchBase",
                "PatchBase ctor: connection pool is null")}};
    }

    if (Q_UNLIKELY(!m_writerThread)) {
        throw InvalidArgument{ErrorString{
            QT_TRANSLATE_NOOP(
                "local_storage::sql::patches::PatchBase",
                "PatchBase ctor: writer thread is null")}};
    }
}

QFuture<void> PatchBase::backupLocalStorage()
{
    QNINFO(
        "local_storage::sql::patches", "PatchBase::backupLocalStorage");

    QPromise<void> promise;
    auto future = promise.future();

    promise.setProgressRange(0, 100);
    promise.start();

    utility::postToThread(
        m_writerThread.get(),
        [self_weak = weak_from_this(), promise = std::move(promise)] () mutable
        {
            auto self = self_weak.lock();
            if (!self) {
                ErrorString errorDescription{QT_TRANSLATE_NOOP(
                    "local_storage::sql::patches::PatchBase",
                    "Cannot backup local storage: PatchBase object is "
                    "destroyed")};
                QNWARNING("local_storage::sql::patches", errorDescription);

                promise.setException(
                    RuntimeError{std::move(errorDescription)});
                promise.finish();
                return;
            }

            ErrorString errorDescription;
            const bool res = self->backupLocalStorageSync(
                promise, errorDescription);

            if (!res) {
                promise.setException(
                    RuntimeError{std::move(errorDescription)});
                promise.finish();
                return;
            }

            promise.finish();
        });

    return future;
}

QFuture<void> PatchBase::restoreLocalStorageFromBackup()
{
    QNINFO(
        "local_storage::sql::patches",
        "PatchBase::restoreLocalStorageFromBackup");

    QPromise<void> promise;
    auto future = promise.future();

    promise.setProgressRange(0, 100);
    promise.start();

    utility::postToThread(
        m_writerThread.get(),
        [self_weak = weak_from_this(), promise = std::move(promise)] () mutable
        {
            auto self = self_weak.lock();
            if (!self) {
                ErrorString errorDescription{QT_TRANSLATE_NOOP(
                    "local_storage::sql::patches::PatchBase",
                    "Cannot restore local storage from backup: PatchBase "
                    "object is destroyed")};
                QNWARNING("local_storage::sql::patches", errorDescription);

                promise.setException(
                    RuntimeError{std::move(errorDescription)});
                promise.finish();
                return;
            }

            ErrorString errorDescription;
            const bool res = self->restoreLocalStorageFromBackupSync(
                promise, errorDescription);

            if (!res) {
                promise.setException(
                    RuntimeError{std::move(errorDescription)});
                promise.finish();
                return;
            }

            promise.finish();
        });

    return future;
}

QFuture<void> PatchBase::removeLocalStorageBackup()
{
    QNDEBUG(
        "local_storage::sql::patches",
        "PatchBase::removeLocalStorageBackup");

    QPromise<void> promise;
    auto future = promise.future();
    promise.start();

    utility::postToThread(
        m_writerThread.get(),
        [self_weak = weak_from_this(), promise = std::move(promise)] () mutable
        {
            auto self = self_weak.lock();
            if (!self) {
                ErrorString errorDescription{QT_TRANSLATE_NOOP(
                    "local_storage::sql::patches::PatchBase",
                    "Cannot remove local storage backup: PatchBase object is "
                    "destroyed")};
                QNWARNING("local_storage:sql:patches", errorDescription);

                promise.setException(
                    RuntimeError{std::move(errorDescription)});
                promise.finish();
                return;
            }

            ErrorString errorDescription;
            const bool res = self->removeLocalStorageBackupSync(
                errorDescription);

            if (!res) {
                promise.setException(
                    RuntimeError{std::move(errorDescription)});
                promise.finish();
                return;
            }

            promise.finish();
        });

    return future;
}

QFuture<void> PatchBase::apply()
{
    QNINFO("local_storage::sql::patches", "PatchBase::apply");

    QPromise<void> promise;
    auto future = promise.future();

    promise.setProgressRange(0, 100);
    promise.start();

    utility::postToThread(
        m_writerThread.get(),
        [self_weak = weak_from_this(), promise = std::move(promise)] () mutable
        {
            auto self = self_weak.lock();
            if (!self) {
                ErrorString errorDescription{QT_TRANSLATE_NOOP(
                    "local_storage::sql::patches::PatchBase",
                    "Cannot apply local storage patch: PatchBase object is "
                    "destroyed")};
                QNWARNING("local_storage::sql::patches", errorDescription);

                promise.setException(
                    RuntimeError{std::move(errorDescription)});
                promise.finish();
                return;
            }

            ErrorString errorDescription;
            const bool res = self->applySync(promise, errorDescription);
            if (!res) {
                promise.setException(
                    RuntimeError{std::move(errorDescription)});
                promise.finish();
                return;
            }

            promise.finish();
        });

    return future;
}

} // namespace quentier::local_storage::sql
