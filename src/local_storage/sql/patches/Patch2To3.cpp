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

#include "Patch2To3.h"

#include "../ConnectionPool.h"
#include "../ErrorHandling.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/RuntimeError.h>
#include <quentier/utility/FileCopier.h>
#include <quentier/utility/FileSystem.h>
#include <quentier/utility/StandardPaths.h>

#include <utility/Threading.h>

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <utility/Qt5Promise.h>
#else
#include <QPromise>
#endif

#include <QDir>

#include <algorithm>
#include <cmath>

namespace quentier::local_storage::sql {

namespace {

const QString gDbFileName = QStringLiteral("qn.storage.sqlite");

} // namespace

Patch2To3::Patch2To3(
    Account account, ConnectionPoolPtr connectionPool,
    QThreadPtr writerThread) :
    m_account{std::move(account)},
    m_connectionPool{std::move(connectionPool)},
    m_writerThread{std::move(writerThread)}
{
    if (Q_UNLIKELY(m_account.isEmpty())) {
        throw InvalidArgument{ErrorString{
            QT_TRANSLATE_NOOP(
                "local_storage::sql::patches::Patch2To3",
                "Patch2To3 ctor: account is empty")}};
    }

    if (Q_UNLIKELY(!m_connectionPool)) {
        throw InvalidArgument{ErrorString{
            QT_TRANSLATE_NOOP(
                "local_storage::sql::patches::Patch2To3",
                "Patch2To3 ctor: connection pool is null")}};
    }

    if (Q_UNLIKELY(!m_writerThread)) {
        throw InvalidArgument{ErrorString{
            QT_TRANSLATE_NOOP(
                "local_storage::sql::patches::Patch2To3",
                "Patch2To3 ctor: writer thread is null")}};
    }
}

QString Patch2To3::patchShortDescription() const
{
    return tr("Proper support for transactional updates of resource data files");
}

QString Patch2To3::patchLongDescription() const
{
    QString result;

    result += tr("This patch slightly changes the placement of attachment data "
                 "files within the local storage directory: it adds one more "
                 "intermediate dir which has the meaning of unique version id "
                 "of the attachment file.");

    result += QStringLiteral("\n");

    result += tr("Prior to this patch resource data files were stored "
                 "according to the following scheme:");

    result += QStringLiteral("\n\n");

    result += QStringLiteral(
        "Resources/data/<note local id>/<resource local id>.dat");

    result += QStringLiteral("\n\n");

    result += tr("After this patch there would be one additional element "
                 "in the path:");

    result += QStringLiteral("\n\n");

    result += QStringLiteral(
        "Resources/data/<note local id>/<version id>/<resource local id>.dat");

    result += QStringLiteral("\n\n");

    result += tr("The change is required in order to implement full support "
                 "for transactional updates and removals of resource data "
                 "files. Without this change interruptions of local storage "
                 "operations (such as application crashes, computer switching "
                 "off due to power failure etc.) could leave it in "
                 "inconsistent state.");

    result += QStringLiteral("\n\n");

    result += tr("The patch should not take long to apply as it just "
                 "creates a couple more helper tables in the database and "
                 "creates subdirs for existing resource data files");

    return result;
}

QFuture<void> Patch2To3::backupLocalStorage()
{
    QNINFO(
        "local_storage:sql:patches", "Patch2To3::backupLocalStorage");

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
                    "local_storage::sql::patches::Patch2To3",
                    "Cannot backup local storage: Patch2To3 object is "
                    "destroyed")};
                QNWARNING("local_storage:sql:patches", errorDescription);

                promise.setException(
                    RuntimeError{std::move(errorDescription)});
                promise.finish();
                return;
            }

            ErrorString errorDescription;
            const bool res = self->backupLocalStorageImpl(
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

QFuture<void> Patch2To3::restoreLocalStorageFromBackup()
{
    QNINFO(
        "local_storage:sql:patches",
        "Patch2To3::restoreLocalStorageFromBackup");

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
                    "local_storage::sql::patches::Patch2To3",
                    "Cannot restore local storage from backup: Patch2To3 "
                    "object is destroyed")};
                QNWARNING("local_storage:sql:patches", errorDescription);

                promise.setException(
                    RuntimeError{std::move(errorDescription)});
                promise.finish();
                return;
            }

            ErrorString errorDescription;
            const bool res = self->restoreLocalStorageFromBackupImpl(
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

QFuture<void> Patch2To3::removeLocalStorageBackup()
{
    QNDEBUG(
        "local_storage:sql:patches",
        "Patch2To3::removeLocalStorageBackup");

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
                    "local_storage::sql::patches::Patch2To3",
                    "Cannot remove local storage backup: Patch2To3 object is "
                    "destroyed")};
                QNWARNING("local_storage:sql:patches", errorDescription);

                promise.setException(
                    RuntimeError{std::move(errorDescription)});
                promise.finish();
                return;
            }

            ErrorString errorDescription;
            const bool res = self->removeLocalStorageBackupImpl(
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

QFuture<void> Patch2To3::apply()
{
    QNINFO("local_storage:sql:patches", "Patch2To3::apply");

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
                    "local_storage::sql::patches::Patch2To3",
                    "Cannot apply local storage patch: Patch2To3 object is "
                    "destroyed")};
                QNWARNING("local_storage:sql:patches", errorDescription);

                promise.setException(
                    RuntimeError{std::move(errorDescription)});
                promise.finish();
                return;
            }

            ErrorString errorDescription;
            const bool res = self->applyImpl(promise, errorDescription);
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

bool Patch2To3::backupLocalStorageImpl(
    QPromise<void> & promise, ErrorString & errorDescription)
{
    QNDEBUG(
        "local_storage:sql:patches",
        "Patch2To3::backupLocalStorageImpl");

    if (promise.isCanceled()) {
        errorDescription.setBase(
            QT_TRANSLATE_NOOP(
                "local_storage::sql::patches::Patch2To3",
                "Local storage backup has been canceled"));
        QNINFO("local_storage:sql:patches", errorDescription);
        return false;
    }

    QString storagePath = accountPersistentStoragePath(m_account);

    m_backupDirPath = storagePath +
        QStringLiteral("/backup_upgrade_2_to_3_") +
        QDateTime::currentDateTime().toString(Qt::ISODate);

    QDir backupDir{m_backupDirPath};
    if (!backupDir.exists()) {
        if (!backupDir.mkpath(m_backupDirPath)) {
            errorDescription.setBase(
                QT_TR_NOOP("Cannot create a backup copy of the local storage: "
                           "failed to create folder for backup files"));

            errorDescription.details() =
                QDir::toNativeSeparators(m_backupDirPath);

            QNWARNING("local_storage:sql:patches", errorDescription);
            return false;
        }
    }

    // First sort out shm and wal files; they are typically quite small
    // compared to the main db file so won't even bother computing the progress
    // for their copying separately

    const QFileInfo shmDbFileInfo{
        storagePath + QStringLiteral("/qn.storage.sqlite-shm")};

    if (shmDbFileInfo.exists()) {
        const QString shmDbFileName = shmDbFileInfo.fileName();

        const QString shmDbBackupFilePath =
            m_backupDirPath + QStringLiteral("/") + shmDbFileName;

        const QFileInfo shmDbBackupFileInfo{shmDbBackupFilePath};
        if (shmDbBackupFileInfo.exists() && !removeFile(shmDbBackupFilePath)) {
            errorDescription.setBase(
                QT_TR_NOOP("Can't backup local storage: failed to remove "
                           "pre-existing SQLite shm backup file"));

            errorDescription.details() =
                QDir::toNativeSeparators(shmDbBackupFilePath);

            QNWARNING("local_storage:sql:patches", errorDescription);
            return false;
        }

        const QString shmDbFilePath = shmDbFileInfo.absoluteFilePath();
        if (!QFile::copy(shmDbFilePath, shmDbBackupFilePath)) {
            errorDescription.setBase(
                QT_TR_NOOP("Can't backup local storage: "
                           "failed to backup SQLite shm file"));

            errorDescription.details() =
                QDir::toNativeSeparators(shmDbFilePath);

            QNWARNING("local_storage:sql:patches", errorDescription);
            return false;
        }
    }

    const QFileInfo walDbFileInfo{
        storagePath + QStringLiteral("/qn.storage.sqlite-wal")};

    if (walDbFileInfo.exists()) {
        const QString walDbFileName = walDbFileInfo.fileName();

        const QString walDbBackupFilePath =
            m_backupDirPath + QStringLiteral("/") + walDbFileName;

        const QFileInfo walDbBackupFileInfo{walDbBackupFilePath};
        if (walDbBackupFileInfo.exists() && !removeFile(walDbBackupFilePath)) {
            errorDescription.setBase(
                QT_TR_NOOP("Can't backup local storage: failed to remove "
                           "pre-existing SQLite wal backup file"));

            errorDescription.details() =
                QDir::toNativeSeparators(walDbBackupFilePath);

            QNWARNING("local_storage:sql:patches", errorDescription);
            return false;
        }

        QString walDbFilePath = walDbFileInfo.absoluteFilePath();
        if (!QFile::copy(walDbFilePath, walDbBackupFilePath)) {
            errorDescription.setBase(
                QT_TR_NOOP("Can't backup local storage: "
                           "failed to backup SQLite wal file"));

            errorDescription.details() =
                QDir::toNativeSeparators(walDbFilePath);

            QNWARNING("local_storage:sql:patches", errorDescription);
            return false;
        }
    }

    // Check if the process needs to continue i.e. that it was not canceled

    if (promise.isCanceled())
    {
        errorDescription.setBase(
            QT_TRANSLATE_NOOP(
                "local_storage::sql::patches::Patch2To3",
                "Local storage backup has been canceled"));
        QNINFO("local_storage:sql:patches", errorDescription);
        return false;
    }

    // Copy the main db file's contents to the backup location
    auto pFileCopier = std::make_unique<FileCopier>();

    QObject::connect(
        pFileCopier.get(), &FileCopier::progressUpdate, pFileCopier.get(),
        [&](double progress) {
            promise.setProgressValue(std::clamp(
                static_cast<int>(std::round(progress * 100.0)), 0, 100));
        });

    bool detectedError = false;

    QObject::connect(
        pFileCopier.get(), &FileCopier::notifyError,
        pFileCopier.get(),
        [&detectedError, &errorDescription](ErrorString error) {
            errorDescription = std::move(error);
            detectedError = true;
        });

    const QString sourceDbFilePath =
        storagePath + QStringLiteral("/") + gDbFileName;

    const QString backupDbFilePath =
        m_backupDirPath + QStringLiteral("/") + gDbFileName;

    pFileCopier->copyFile(sourceDbFilePath, backupDbFilePath);
    return !detectedError;
}

bool Patch2To3::restoreLocalStorageFromBackupImpl(
    QPromise<void> & promise,
    ErrorString & errorDescription)
{
    QNDEBUG(
        "local_storage:sql:patches",
        "Patch2To3::restoreLocalStorageFromBackupImpl");

    QString storagePath = accountPersistentStoragePath(m_account);

    // First sort out shm and wal files; they are typically quite small
    // compared to the main db file so won't even bother computing the progress
    // for their restoration from backup separately

    QString shmDbFileName = QStringLiteral("qn.storage.sqlite-shm");

    QFileInfo shmDbBackupFileInfo{
        m_backupDirPath + QStringLiteral("/") + shmDbFileName};

    if (shmDbBackupFileInfo.exists()) {
        QString shmDbBackupFilePath = shmDbBackupFileInfo.absoluteFilePath();

        QString shmDbFilePath =
            storagePath + QStringLiteral("/") + shmDbFileName;

        QFileInfo shmDbFileInfo{shmDbFilePath};
        if (shmDbFileInfo.exists() && !removeFile(shmDbFilePath)) {
            errorDescription.setBase(
                QT_TR_NOOP("Can't restore the local storage "
                           "from backup: failed to remove "
                           "the pre-existing SQLite shm file"));

            errorDescription.details() =
                QDir::toNativeSeparators(shmDbFilePath);

            QNWARNING("local_storage:sql:patches", errorDescription);
            return false;
        }

        if (!QFile::copy(shmDbBackupFilePath, shmDbFilePath)) {
            errorDescription.setBase(
                QT_TR_NOOP("Can't restore the local storage "
                           "from backup: failed to restore "
                           "the SQLite shm file"));

            errorDescription.details() =
                QDir::toNativeSeparators(shmDbFilePath);

            QNWARNING("local_storage:sql:patches", errorDescription);
            return false;
        }
    }

    QString walDbFileName = QStringLiteral("qn.storage.sqlite-wal");

    QFileInfo walDbBackupFileInfo{
        m_backupDirPath + QStringLiteral("/") + walDbFileName};

    if (walDbBackupFileInfo.exists()) {
        QString walDbBackupFilePath = walDbBackupFileInfo.absoluteFilePath();

        const QString walDbFilePath =
            storagePath + QStringLiteral("/") + walDbFileName;

        const QFileInfo walDbFileInfo{walDbFilePath};
        if (walDbFileInfo.exists() && !removeFile(walDbFilePath)) {
            errorDescription.setBase(
                QT_TR_NOOP("Can't restore the local storage "
                           "from backup: failed to remove "
                           "the pre-existing SQLite wal file"));

            errorDescription.details() =
                QDir::toNativeSeparators(walDbFilePath);

            QNWARNING("local_storage:sql:patches", errorDescription);
            return false;
        }

        if (!QFile::copy(walDbBackupFilePath, walDbFilePath)) {
            errorDescription.setBase(
                QT_TR_NOOP("Can't restore the local storage "
                           "from backup: failed to restore "
                           "the SQLite wal file"));

            errorDescription.details() =
                QDir::toNativeSeparators(walDbFilePath);

            QNWARNING("local_storage:sql:patches", errorDescription);
            return false;
        }
    }

    // Restore the main db file's contents from the backup location

    auto pFileCopier = std::make_unique<FileCopier>();

    QObject::connect(
        pFileCopier.get(), &FileCopier::progressUpdate, pFileCopier.get(),
        [&](double progress) {
            promise.setProgressValue(std::clamp(
                static_cast<int>(std::round(progress * 100.0)), 0, 100));
        });

    bool detectedError = false;

    QObject::connect(
        pFileCopier.get(), &FileCopier::notifyError,
        pFileCopier.get(),
        [&detectedError, &errorDescription](ErrorString error) {
            errorDescription = std::move(error);
            detectedError = true;
        });

    const QString sourceDbFilePath =
        storagePath + QStringLiteral("/") + gDbFileName;

    const QString backupDbFilePath =
        m_backupDirPath + QStringLiteral("/") + gDbFileName;

    pFileCopier->copyFile(backupDbFilePath, sourceDbFilePath);
    return !detectedError;
}

bool Patch2To3::removeLocalStorageBackupImpl(
    ErrorString & errorDescription)
{
    QNINFO(
        "local_storage:sql:patches",
        "Patch2To3::removeLocalStorageBackup");

    bool removedShmDbBackup = true;

    const QFileInfo shmDbBackupFileInfo{
        m_backupDirPath + QStringLiteral("/qn.storage.sqlite-shm")};

    if (shmDbBackupFileInfo.exists() &&
        !removeFile(shmDbBackupFileInfo.absoluteFilePath()))
    {
        QNDEBUG(
            "local_storage:sql:patches",
            "Failed to remove the SQLite shm file's backup: "
                << shmDbBackupFileInfo.absoluteFilePath());

        removedShmDbBackup = false;
    }

    bool removedWalDbBackup = true;

    const QFileInfo walDbBackupFileInfo{
        m_backupDirPath + QStringLiteral("/qn.storage.sqlite-wal")};

    if (walDbBackupFileInfo.exists() &&
        !removeFile(walDbBackupFileInfo.absoluteFilePath()))
    {
        QNDEBUG(
            "local_storage:sql:patches",
            "Failed to remove the SQLite wal file's backup: "
                << walDbBackupFileInfo.absoluteFilePath());

        removedWalDbBackup = false;
    }

    bool removedDbBackup = true;

    const QFileInfo dbBackupFileInfo{
        m_backupDirPath + QStringLiteral("/qn.storage.sqlite")};

    if (dbBackupFileInfo.exists() &&
        !removeFile(dbBackupFileInfo.absoluteFilePath()))
    {
        QNWARNING(
            "local_storage:sql:patches",
            "Failed to remove the SQLite database's backup: "
                << dbBackupFileInfo.absoluteFilePath());

        removedDbBackup = false;
    }

    bool removedBackupDir = true;
    QDir backupDir{m_backupDirPath};
    if (!backupDir.rmdir(m_backupDirPath)) {
        QNWARNING(
            "local_storage:sql:patches",
            "Failed to remove the SQLite database's backup folder: "
                << m_backupDirPath);

        removedBackupDir = false;
    }

    if (!removedShmDbBackup || !removedWalDbBackup || !removedDbBackup ||
        !removedBackupDir)
    {
        errorDescription.setBase(
            QT_TR_NOOP("Failed to remove some of SQLite database's backups"));
        return false;
    }

    return true;
}

bool Patch2To3::applyImpl(
    QPromise<void> & promise, ErrorString & errorDescription)
{
    // TODO: implement
    Q_UNUSED(promise)
    Q_UNUSED(errorDescription)
    return true;
}

} // namespace quentier::local_storage::sql
