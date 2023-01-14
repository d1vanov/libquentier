/*
 * Copyright 2021-2023 Dmitry Ivanov
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

#include "PatchUtils.h"

#include <quentier/logging/QuentierLogger.h>
#include <quentier/types/ErrorString.h>
#include <quentier/utility/FileCopier.h>
#include <quentier/utility/FileSystem.h>

#include <QDir>

#include <algorithm>
#include <cmath>
#include <memory>

namespace quentier::local_storage::sql::utils {

namespace {

const QString gDbFileName = QStringLiteral("qn.storage.sqlite");

} // namespace

bool backupLocalStorageDatabaseFiles(
    const QString & localStorageDirPath, const QString & backupDirPath,
    QPromise<void> & promise, ErrorString & errorDescription)
{
    QNDEBUG(
        "local_storage::sql::patches::utils",
        "backupLocalStorageDatabaseFiles: from "
            << QDir::toNativeSeparators(localStorageDirPath) << " to "
            << QDir::toNativeSeparators(backupDirPath));

    if (promise.isCanceled()) {
        errorDescription.setBase(
            QStringLiteral("Local storage backup has been canceled"));
        QNINFO("local_storage::sql::patches::utils", errorDescription);
        return false;
    }

    QDir backupDir{backupDirPath};
    if (!backupDir.exists()) {
        if (!backupDir.mkpath(backupDirPath)) {
            errorDescription.setBase(QStringLiteral(
                "Cannot create a backup copy of the local storage: "
                "failed to create folder for backup files"));

            errorDescription.details() =
                QDir::toNativeSeparators(backupDirPath);

            QNWARNING("local_storage::sql::patches::utils", errorDescription);
            return false;
        }
    }

    // First sort out shm and wal files; they are typically quite small
    // compared to the main db file so won't even bother computing the progress
    // for their copying separately

    const QFileInfo shmDbFileInfo{
        localStorageDirPath + QStringLiteral("/qn.storage.sqlite-shm")};

    if (shmDbFileInfo.exists()) {
        const QString shmDbFileName = shmDbFileInfo.fileName();

        const QString shmDbBackupFilePath =
            backupDirPath + QStringLiteral("/") + shmDbFileName;

        const QFileInfo shmDbBackupFileInfo{shmDbBackupFilePath};
        if (shmDbBackupFileInfo.exists() && !removeFile(shmDbBackupFilePath)) {
            errorDescription.setBase(
                QStringLiteral("Can't backup local storage: failed to remove "
                               "pre-existing SQLite shm backup file"));

            errorDescription.details() =
                QDir::toNativeSeparators(shmDbBackupFilePath);

            QNWARNING("local_storage::sql::patches::utils", errorDescription);
            return false;
        }

        const QString shmDbFilePath = shmDbFileInfo.absoluteFilePath();
        if (!QFile::copy(shmDbFilePath, shmDbBackupFilePath)) {
            errorDescription.setBase(QStringLiteral(
                "Can't backup local storage: failed to backup SQLite shm "
                "file"));

            errorDescription.details() =
                QDir::toNativeSeparators(shmDbFilePath);

            QNWARNING("local_storage::sql::patches::utils", errorDescription);
            return false;
        }
    }

    const QFileInfo walDbFileInfo{
        localStorageDirPath + QStringLiteral("/qn.storage.sqlite-wal")};

    if (walDbFileInfo.exists()) {
        const QString walDbFileName = walDbFileInfo.fileName();

        const QString walDbBackupFilePath =
            backupDirPath + QStringLiteral("/") + walDbFileName;

        const QFileInfo walDbBackupFileInfo{walDbBackupFilePath};
        if (walDbBackupFileInfo.exists() && !removeFile(walDbBackupFilePath)) {
            errorDescription.setBase(QStringLiteral(
                "Can't backup local storage: failed to remove pre-existing "
                "SQLite wal backup file"));

            errorDescription.details() =
                QDir::toNativeSeparators(walDbBackupFilePath);

            QNWARNING("local_storage::sql::patches::utils", errorDescription);
            return false;
        }

        QString walDbFilePath = walDbFileInfo.absoluteFilePath();
        if (!QFile::copy(walDbFilePath, walDbBackupFilePath)) {
            errorDescription.setBase(QStringLiteral(
                "Can't backup local storage: failed to backup SQLite wal "
                "file"));

            errorDescription.details() =
                QDir::toNativeSeparators(walDbFilePath);

            QNWARNING("local_storage::sql::patches::utils", errorDescription);
            return false;
        }
    }

    // Check if the process needs to continue i.e. that it was not canceled

    if (promise.isCanceled()) {
        errorDescription.setBase(
            QStringLiteral("Local storage backup has been canceled"));
        QNINFO("local_storage::sql::patches::utils", errorDescription);
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
        pFileCopier.get(), &FileCopier::notifyError, pFileCopier.get(),
        [&detectedError, &errorDescription](ErrorString error) {
            errorDescription = std::move(error);
            detectedError = true;
        });

    const QString sourceDbFilePath =
        localStorageDirPath + QStringLiteral("/") + gDbFileName;

    const QString backupDbFilePath =
        backupDirPath + QStringLiteral("/") + gDbFileName;

    pFileCopier->copyFile(sourceDbFilePath, backupDbFilePath);
    return !detectedError;
}

bool restoreLocalStorageDatabaseFilesFromBackup(
    const QString & localStorageDirPath, const QString & backupDirPath,
    QPromise<void> & promise, ErrorString & errorDescription)
{
    QNINFO(
        "local_storage::sql::patches::utils",
        "restoreLocalStorageDatabaseFilesFromBackup: from "
            << QDir::toNativeSeparators(localStorageDirPath) << " to "
            << QDir::toNativeSeparators(backupDirPath));

    // First sort out shm and wal files; they are typically quite small
    // compared to the main db file so won't even bother computing the progress
    // for their restoration from backup separately

    QString shmDbFileName = QStringLiteral("qn.storage.sqlite-shm");

    QFileInfo shmDbBackupFileInfo{
        backupDirPath + QStringLiteral("/") + shmDbFileName};

    if (shmDbBackupFileInfo.exists()) {
        QString shmDbBackupFilePath = shmDbBackupFileInfo.absoluteFilePath();

        QString shmDbFilePath =
            localStorageDirPath + QStringLiteral("/") + shmDbFileName;

        QFileInfo shmDbFileInfo{shmDbFilePath};
        if (shmDbFileInfo.exists() && !removeFile(shmDbFilePath)) {
            errorDescription.setBase(QStringLiteral(
                "Can't restore the local storage from backup: failed to remove "
                "the pre-existing SQLite shm file"));

            errorDescription.details() =
                QDir::toNativeSeparators(shmDbFilePath);

            QNWARNING("local_storage::sql::patches::utils", errorDescription);
            return false;
        }

        if (!QFile::copy(shmDbBackupFilePath, shmDbFilePath)) {
            errorDescription.setBase(QStringLiteral(
                "Can't restore the local storage from backup: failed to "
                "restore the SQLite shm file"));

            errorDescription.details() =
                QDir::toNativeSeparators(shmDbFilePath);

            QNWARNING("local_storage::sql::patches::utils", errorDescription);
            return false;
        }
    }

    QString walDbFileName = QStringLiteral("qn.storage.sqlite-wal");

    QFileInfo walDbBackupFileInfo{
        backupDirPath + QStringLiteral("/") + walDbFileName};

    if (walDbBackupFileInfo.exists()) {
        QString walDbBackupFilePath = walDbBackupFileInfo.absoluteFilePath();

        const QString walDbFilePath =
            localStorageDirPath + QStringLiteral("/") + walDbFileName;

        const QFileInfo walDbFileInfo{walDbFilePath};
        if (walDbFileInfo.exists() && !removeFile(walDbFilePath)) {
            errorDescription.setBase(QStringLiteral(
                "Can't restore the local storage from backup: failed to remove "
                "the pre-existing SQLite wal file"));

            errorDescription.details() =
                QDir::toNativeSeparators(walDbFilePath);

            QNWARNING("local_storage::sql::patches::utils", errorDescription);
            return false;
        }

        if (!QFile::copy(walDbBackupFilePath, walDbFilePath)) {
            errorDescription.setBase(QStringLiteral(
                "Can't restore the local storage from backup: failed to "
                "restore the SQLite wal file"));

            errorDescription.details() =
                QDir::toNativeSeparators(walDbFilePath);

            QNWARNING("local_storage::sql::patches::utils", errorDescription);
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
        pFileCopier.get(), &FileCopier::notifyError, pFileCopier.get(),
        [&detectedError, &errorDescription](ErrorString error) {
            errorDescription = std::move(error);
            detectedError = true;
        });

    const QString sourceDbFilePath =
        localStorageDirPath + QStringLiteral("/") + gDbFileName;

    const QString backupDbFilePath =
        backupDirPath + QStringLiteral("/") + gDbFileName;

    pFileCopier->copyFile(backupDbFilePath, sourceDbFilePath);
    return !detectedError;
}

bool removeLocalStorageDatabaseFilesBackup(
    const QString & backupDirPath, ErrorString & errorDescription)
{
    QNINFO(
        "local_storage::sql::patches::utils",
        "removeLocalStorageDatabaseFilesBackup: from "
            << QDir::toNativeSeparators(backupDirPath));

    bool removedShmDbBackup = true;

    const QFileInfo shmDbBackupFileInfo{
        backupDirPath + QStringLiteral("/qn.storage.sqlite-shm")};

    if (shmDbBackupFileInfo.exists() &&
        !removeFile(shmDbBackupFileInfo.absoluteFilePath()))
    {
        QNDEBUG(
            "local_storage::sql::patches::utils",
            "Failed to remove the SQLite shm file's backup: "
                << shmDbBackupFileInfo.absoluteFilePath());

        removedShmDbBackup = false;
    }

    bool removedWalDbBackup = true;

    const QFileInfo walDbBackupFileInfo{
        backupDirPath + QStringLiteral("/qn.storage.sqlite-wal")};

    if (walDbBackupFileInfo.exists() &&
        !removeFile(walDbBackupFileInfo.absoluteFilePath()))
    {
        QNDEBUG(
            "local_storage::sql::patches::utils",
            "Failed to remove the SQLite wal file's backup: "
                << walDbBackupFileInfo.absoluteFilePath());

        removedWalDbBackup = false;
    }

    bool removedDbBackup = true;

    const QFileInfo dbBackupFileInfo{
        backupDirPath + QStringLiteral("/qn.storage.sqlite")};

    if (dbBackupFileInfo.exists() &&
        !removeFile(dbBackupFileInfo.absoluteFilePath()))
    {
        QNWARNING(
            "local_storage::sql::patches::utils",
            "Failed to remove the SQLite database's backup: "
                << dbBackupFileInfo.absoluteFilePath());

        removedDbBackup = false;
    }

    bool removedBackupDir = true;
    QDir backupDir{backupDirPath};
    if (!backupDir.rmdir(backupDirPath)) {
        QNWARNING(
            "local_storage::sql::patches::utils",
            "Failed to remove the SQLite database's backup folder: "
                << backupDirPath);

        removedBackupDir = false;
    }

    if (!removedShmDbBackup || !removedWalDbBackup || !removedDbBackup ||
        !removedBackupDir)
    {
        errorDescription.setBase(QStringLiteral(
            "Failed to remove some of SQLite database's backups"));
        return false;
    }

    return true;
}

} // namespace quentier::local_storage::sql::utils
