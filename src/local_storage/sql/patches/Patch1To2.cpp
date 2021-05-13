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

#include "Patch1To2.h"
#include "../ConnectionPool.h"
#include "../ErrorHandling.h"

#include <quentier/logging/QuentierLogger.h>
#include <quentier/types/ErrorString.h>
#include <quentier/utility/ApplicationSettings.h>
#include <quentier/utility/EventLoopWithExitStatus.h>
#include <quentier/utility/FileCopier.h>
#include <quentier/utility/FileSystem.h>
#include <quentier/utility/StandardPaths.h>

#include <QDir>
#include <QSqlRecord>
#include <QSqlQuery>
#include <QTimer>

namespace quentier::local_storage::sql {

namespace {

const QString gUpgrade1To2Persistence =
    QStringLiteral("LocalStorageDatabaseUpgradeFromVersion1ToVersion2");

const QString gUpgrade1To2AllResourceDataCopiedFromTablesToFilesKey =
    QStringLiteral("AllResourceDataCopiedFromTableToFiles");

const QString gUpgrade1To2LocalIdsForResourcesCopiedToFilesKey =
    QStringLiteral("LocalUidsOfResourcesCopiedToFiles");

const QString gUpgrade1To2AllResourceDataRemovedFromTables =
    QStringLiteral("AllResourceDataRemovedFromResourceTable");

const QString gResourceLocalIdColumn = QStringLiteral("resourceLocalUid");
const QString gDbFileName = QStringLiteral("qn.storage.sqlite");

template <typename T>
bool extractEntry(
    const QSqlRecord & rec, const QString & name, T & entry)
{
    const int index = rec.indexOf(name);
    if (index >= 0) {
        const QVariant value = rec.value(index);
        if (!value.isNull()) {
            entry = qvariant_cast<T>(value);
            return true;
        }
    }

    return false;
}

} // namespace

Patch1To2::Patch1To2(
    Account account, ConnectionPoolPtr pConnectionPool, QObject * parent) :
    ILocalStoragePatch(parent),
    m_account{std::move(account)},
    m_pConnectionPool{std::move(pConnectionPool)}
{
    Q_ASSERT(m_pConnectionPool);
}

QString Patch1To2::patchShortDescription() const
{
    return tr("Move attachments data from SQLite database to plain files");
}

QString Patch1To2::patchLongDescription() const
{
    QString result;

    result +=
        tr("This patch will move the data corresponding to notes' attachments "
           "from Quentier's primary SQLite database to separate files. "
           "This change of local storage structure is necessary to fix or "
           "prevent serious performance issues for accounts containing "
           "numerous large enough note attachments due to the way SQLite puts "
           "large data blocks together within the database file. If you are "
           "interested in technical details on this topic, consider consulting "
           "the following material");

    result += QStringLiteral(
        ": <a href=\"https://www.sqlite.org/intern-v-extern-blob.html\">"
        "Internal Versus External BLOBs in SQLite</a>.\n\n");

    result +=
        tr("The time required to apply this patch would depend on the general "
           "performance of disk I/O on your system and on the number of "
           "resources within your account");

    ErrorString errorDescription;
    auto database = m_pConnectionPool->database();

    const int numResources = resourceCount(database, errorDescription);
    if (Q_UNLIKELY(numResources < 0)) {
        QNWARNING(
            "local_storage:sql:patches",
            "Can't get the number of resources "
                << "within the local storage database: " << errorDescription);
    }
    else {
        QNINFO(
            "local_storage:patches",
            "Before applying local storage 1-to-2 "
                << "patch: " << numResources << " resources within the local "
                << "storage");

        result += QStringLiteral(" (");
        result += QString::number(numResources);
        result += QStringLiteral(")");
    }

    result += QStringLiteral(".\n\n");

    result +=
        tr("If the account which local storage is to be upgraded is "
           "Evernote one and if you don't have any local "
           "unsynchronized changes there, you can consider just wiping out "
           "its data folder");

    result += QStringLiteral(" (");
    result += QDir::toNativeSeparators(accountPersistentStoragePath(m_account));
    result += QStringLiteral(") ");

    result +=
        tr("and re-syncing it from Evernote instead of upgrading "
           "the local database - if your account contains many large "
           "enough attachments to notes, re-syncing can "
           "actually be faster than upgrading the local storage");

    result += QStringLiteral(".\n\n");

    result +=
        tr("Note that after the upgrade previous versions of Quentier would "
           "no longer be able to use this account's local storage");

    result += QStringLiteral(".");
    return result;
}

bool Patch1To2::backupLocalStorage(ErrorString & errorDescription)
{
    QNINFO(
        "local_storage:patches", "Patch1To2::backupLocalStorage");

    QString storagePath = accountPersistentStoragePath(m_account);

    m_backupDirPath = storagePath + QStringLiteral("/backup_upgrade_1_to_2_") +
        QDateTime::currentDateTime().toString(Qt::ISODate);

    QDir backupDir{m_backupDirPath};
    if (!backupDir.exists()) {
        if (!backupDir.mkpath(m_backupDirPath)) {
            errorDescription.setBase(
                QT_TR_NOOP("Can't backup local storage: failed to create "
                           "folder for backup files"));

            errorDescription.details() =
                QDir::toNativeSeparators(m_backupDirPath);

            QNWARNING("local_storage:patches", errorDescription);
            return false;
        }
    }

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

    EventLoopWithExitStatus backupEventLoop;

    auto * pMainDbFileCopierThread = new QThread;

    QObject::connect(
        pMainDbFileCopierThread, &QThread::finished, pMainDbFileCopierThread,
        &QThread::deleteLater);

    pMainDbFileCopierThread->start();

    auto * pMainDbFileCopier = new FileCopier;
    QPointer<FileCopier> pFileCopierQPtr = pMainDbFileCopier;

    QObject::connect(
        pMainDbFileCopier, &FileCopier::progressUpdate, this,
        &Patch1To2::backupProgress);

    QObject::connect(
        pMainDbFileCopier, &FileCopier::notifyError, &backupEventLoop,
        &EventLoopWithExitStatus::exitAsFailureWithErrorString);

    QObject::connect(
        pMainDbFileCopier, &FileCopier::finished, &backupEventLoop,
        &EventLoopWithExitStatus::exitAsSuccess);

    QObject::connect(
        pMainDbFileCopier, &FileCopier::finished, pMainDbFileCopier,
        &FileCopier::deleteLater);

    QObject::connect(
        pMainDbFileCopier, &FileCopier::finished, pMainDbFileCopierThread,
        &QThread::quit);

    QObject::connect(
        this, &Patch1To2::copyDbFile, pMainDbFileCopier,
        &FileCopier::copyFile);

    pMainDbFileCopier->moveToThread(pMainDbFileCopierThread);

    QTimer::singleShot(0, this, SLOT(startLocalStorageBackup()));

    Q_UNUSED(backupEventLoop.exec())
    auto status = backupEventLoop.exitStatus();

    if (!pFileCopierQPtr.isNull()) {
        QObject::disconnect(
            this, &Patch1To2::copyDbFile, pMainDbFileCopier,
            &FileCopier::copyFile);
    }

    if (status == EventLoopWithExitStatus::ExitStatus::Failure) {
        errorDescription = backupEventLoop.errorDescription();
        return false;
    }

    return true;
}

bool Patch1To2::restoreLocalStorageFromBackup(
    ErrorString & errorDescription)
{
    QNINFO(
        "local_storage:sql:patches",
        "Patch1To2::restoreLocalStorageFromBackup");

    QString storagePath = accountPersistentStoragePath(m_account);
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

    EventLoopWithExitStatus restoreFromBackupEventLoop;

    auto * pMainDbFileCopierThread = new QThread;

    QObject::connect(
        pMainDbFileCopierThread, &QThread::finished, pMainDbFileCopierThread,
        &QThread::deleteLater);

    pMainDbFileCopierThread->start();

    auto * pMainDbFileCopier = new FileCopier;
    QPointer<FileCopier> pFileCopierQPtr = pMainDbFileCopier;

    QObject::connect(
        pMainDbFileCopier, &FileCopier::progressUpdate, this,
        &Patch1To2::restoreBackupProgress);

    QObject::connect(
        pMainDbFileCopier, &FileCopier::notifyError,
        &restoreFromBackupEventLoop,
        &EventLoopWithExitStatus::exitAsFailureWithErrorString);

    QObject::connect(
        pMainDbFileCopier, &FileCopier::finished, &restoreFromBackupEventLoop,
        &EventLoopWithExitStatus::exitAsSuccess);

    QObject::connect(
        pMainDbFileCopier, &FileCopier::finished, pMainDbFileCopier,
        &FileCopier::deleteLater);

    QObject::connect(
        pMainDbFileCopier, &FileCopier::finished, pMainDbFileCopierThread,
        &QThread::quit);

    QObject::connect(
        this, &Patch1To2::copyDbFile, pMainDbFileCopier,
        &FileCopier::copyFile);

    pMainDbFileCopier->moveToThread(pMainDbFileCopierThread);

    QTimer::singleShot(
        0, this, SLOT(startLocalStorageRestorationFromBackup()));

    Q_UNUSED(restoreFromBackupEventLoop.exec())
    const auto status = restoreFromBackupEventLoop.exitStatus();

    if (!pFileCopierQPtr.isNull()) {
        QObject::disconnect(
            this, &Patch1To2::copyDbFile, pMainDbFileCopier,
            &FileCopier::copyFile);
    }

    if (status == EventLoopWithExitStatus::ExitStatus::Failure) {
        errorDescription = restoreFromBackupEventLoop.errorDescription();
        return false;
    }

    return true;
}

bool Patch1To2::removeLocalStorageBackup(
    ErrorString & errorDescription)
{
    QNINFO(
        "local_storage:sql:patches",
        "Patch1To2::removeLocalStorageBackup");

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

bool Patch1To2::apply(ErrorString & errorDescription)
{
    QNINFO("local_storage:sql:patches", "Patch1To2::apply");

    ApplicationSettings databaseUpgradeInfo{m_account, gUpgrade1To2Persistence};

    ErrorString errorPrefix{
        QT_TR_NOOP("failed to upgrade local storage "
                   "from version 1 to version 2")};

    errorDescription.clear();

    double lastProgress = 0.0;
    const QString storagePath = accountPersistentStoragePath(m_account);

    QStringList resourceLocalIds;

    const bool allResourceDataCopiedFromTablesToFiles =
        databaseUpgradeInfo
            .value(gUpgrade1To2AllResourceDataCopiedFromTablesToFilesKey)
            .toBool();

    auto database = m_pConnectionPool->database();

    if (!allResourceDataCopiedFromTablesToFiles) {
        // Part 1: extract the list of resource local uids from the local
        // storage database
        resourceLocalIds =
            listResourceLocalIdsForDatabaseUpgradeFromVersion1ToVersion2(
                errorDescription);

        if (resourceLocalIds.isEmpty() && !errorDescription.isEmpty()) {
            return false;
        }

        lastProgress = 0.05;
        Q_EMIT progress(lastProgress);

        filterResourceLocalIdsForDatabaseUpgradeFromVersion1ToVersion2(
            resourceLocalIds);

        // Part 2: ensure the directories for resources data body and
        // recognition data body exist, create them if necessary
        if (!ensureExistenceOfResouceDataDirsForDatabaseUpgradeFromVersion1ToVersion2(
                errorDescription))
        {
            return false;
        }

        // Part 3: copy the data for each resource local uid into the local file
        databaseUpgradeInfo.beginWriteArray(
            gUpgrade1To2LocalIdsForResourcesCopiedToFilesKey);

        auto pProcessedResourceLocalIdsDatabaseUpgradeInfoCloser =
            std::make_unique<ApplicationSettings::ArrayCloser>(
                databaseUpgradeInfo);

        const int numResources = resourceLocalIds.size();
        double singleResourceProgressFraction = (0.7 - lastProgress) /
            std::max(1.0, static_cast<double>(numResources));

        int processedResourceCounter = 0;
        for (const auto & resourceLocalId: qAsConst(resourceLocalIds)) {
            QSqlQuery query{database};

            const bool res =
                query.exec(QString::fromUtf8("SELECT noteLocalUid, dataBody, "
                                             "alternateDataBody FROM Resources "
                                             "WHERE resourceLocalUid='%1'")
                               .arg(resourceLocalId));

            ENSURE_DB_REQUEST_RETURN(
                res, query, "local_storage::sql::patches::1_to_2",
                QT_TR_NOOP(
                    "failed to execute SQL query fetching resource data bodies "
                    "from tables"),
                false);

            if (Q_UNLIKELY(!query.next())) {
                errorDescription = errorPrefix;
                errorDescription.appendBase(
                    QT_TR_NOOP("failed to fetch resource "
                               "information from the local "
                               "storage database"));

                errorDescription.details() =
                    QStringLiteral("resource local id = ") + resourceLocalId;

                QNWARNING("local_storage:sql:patches", errorDescription);
                return false;
            }

            const QSqlRecord rec = query.record();

            QString noteLocalId;
            if (!extractEntry(rec, QStringLiteral("noteLocalUid"), noteLocalId))
            {
                errorDescription = errorPrefix;
                errorDescription.appendBase(
                    QT_TR_NOOP("failed to get note local id corresponding "
                               "to a resource"));
                QNWARNING("local_storage:sql:patches", errorDescription);
                return false;
            }

            QByteArray dataBody;
            if (!extractEntry(rec, QStringLiteral("dataBody"), dataBody)) {
                errorDescription = errorPrefix;
                errorDescription.appendBase(
                    QT_TR_NOOP("failed to get data body corresponding "
                               "to a resource"));
                QNWARNING("local_storage:sql:patches", errorDescription);
                return false;
            }

            QByteArray alternateDataBody;
            Q_UNUSED(extractEntry(
                rec, QStringLiteral("alternateDataBody"), alternateDataBody))

            // 3.1 Ensure the existence of dir for note resource's data body
            QDir noteResourceDataDir{
                storagePath + QStringLiteral("/Resources/data/") + noteLocalId};

            if (!noteResourceDataDir.exists()) {
                const bool res = noteResourceDataDir.mkpath(
                    noteResourceDataDir.absolutePath());
                if (!res) {
                    errorDescription = errorPrefix;
                    errorDescription.appendBase(
                        QT_TR_NOOP("failed to create directory "
                                   "for resource data bodies "
                                   "for some note"));

                    errorDescription.details() =
                        QStringLiteral("note local id = ") + noteLocalId;

                    QNWARNING("local_storage:sql:patches", errorDescription);
                    return false;
                }
            }

            // 3.2 Write resource data body to a file
            QFile resourceDataFile{
                noteResourceDataDir.absolutePath() + QStringLiteral("/") +
                resourceLocalId + QStringLiteral(".dat")};

            if (!resourceDataFile.open(QIODevice::WriteOnly)) {
                errorDescription = errorPrefix;
                errorDescription.appendBase(
                    QT_TR_NOOP("failed to open resource "
                               "data file for writing"));

                errorDescription.details() =
                    QStringLiteral("resource local uid = ") + resourceLocalId;

                QNWARNING("local_storage:sql:patches", errorDescription);
                return false;
            }

            const qint64 dataSize = dataBody.size();
            qint64 bytesWritten = resourceDataFile.write(dataBody);
            if (bytesWritten < 0) {
                errorDescription = errorPrefix;
                errorDescription.appendBase(
                    QT_TR_NOOP("failed to write resource "
                               "data body to a file"));

                errorDescription.details() =
                    QStringLiteral("resource local uid = ") + resourceLocalId;

                QNWARNING("local_storage:sql:patches", errorDescription);
                return false;
            }

            if (bytesWritten < dataSize) {
                errorDescription = errorPrefix;
                errorDescription.appendBase(
                    QT_TR_NOOP("failed to write whole "
                               "resource data body to a file"));

                errorDescription.details() =
                    QStringLiteral("resource local id = ") + resourceLocalId;

                QNWARNING("local_storage:sql:patches", errorDescription);
                return false;
            }

            if (!resourceDataFile.flush()) {
                errorDescription = errorPrefix;
                errorDescription.appendBase(
                    QT_TR_NOOP("failed to flush the resource "
                               "data body to a file"));

                errorDescription.details() =
                    QStringLiteral("resource local uid = ") + resourceLocalId;

                QNWARNING("local_storage:sql:patches", errorDescription);
                return false;
            }

            // 3.3 If there's no resource alternate data for this resource,
            // we are done with it
            if (alternateDataBody.isEmpty()) {
                databaseUpgradeInfo.setArrayIndex(processedResourceCounter);

                databaseUpgradeInfo.setValue(
                    gResourceLocalIdColumn, resourceLocalId);

                lastProgress += singleResourceProgressFraction;

                QNDEBUG(
                    "local_storage:patches",
                    "Processed resource data (no "
                        << "alternate data) for resource local id "
                        << resourceLocalId << "; updated progress to "
                        << lastProgress);

                Q_EMIT progress(lastProgress);
                continue;
            }

            // 3.4 Ensure the existence of dir for note resource's alternate
            // data body
            QDir noteResourceAlternateDataDir{
                storagePath + QStringLiteral("/Resource/alternateData/") +
                noteLocalId};

            if (!noteResourceAlternateDataDir.exists()) {
                const bool res = noteResourceAlternateDataDir.mkpath(
                    noteResourceAlternateDataDir.absolutePath());

                if (!res) {
                    errorDescription = errorPrefix;
                    errorDescription.appendBase(
                        QT_TR_NOOP("failed to create directory for resource "
                                   "alternate data bodies for some note"));

                    errorDescription.details() =
                        QStringLiteral("note local uid = ") + noteLocalId;

                    QNWARNING("local_storage:sql:patches", errorDescription);
                    return false;
                }
            }

            // 3.5 Write resource alternate data body to a file
            QFile resourceAlternateDataFile{
                noteResourceAlternateDataDir.absolutePath() +
                QStringLiteral("/") + resourceLocalId +
                QStringLiteral(".dat")};

            if (!resourceAlternateDataFile.open(QIODevice::WriteOnly)) {
                errorDescription = errorPrefix;
                errorDescription.appendBase(
                    QT_TR_NOOP("failed to open resource alternate data file "
                               "for writing"));

                errorDescription.details() =
                    QStringLiteral("resource local id = ") + resourceLocalId;

                QNWARNING("local_storage:sql:patches", errorDescription);
                return false;
            }

            const qint64 alternateDataSize = alternateDataBody.size();
            bytesWritten = resourceAlternateDataFile.write(alternateDataBody);
            if (bytesWritten < 0) {
                errorDescription = errorPrefix;

                errorDescription.appendBase(
                    QT_TR_NOOP("failed to write resource alternate data body "
                               "to a file"));

                errorDescription.details() =
                    QStringLiteral("resource local id = ") + resourceLocalId;

                QNWARNING("local_storage:sql:patches", errorDescription);
                return false;
            }

            if (bytesWritten < alternateDataSize) {
                errorDescription = errorPrefix;

                errorDescription.appendBase(
                    QT_TR_NOOP("failed to write whole resource alternate data "
                               "body to a file"));

                errorDescription.details() =
                    QStringLiteral("resource local id = ") + resourceLocalId;

                QNWARNING("local_storage:sql:patches", errorDescription);
                return false;
            }

            if (!resourceAlternateDataFile.flush()) {
                errorDescription = errorPrefix;

                errorDescription.appendBase(
                    QT_TR_NOOP("failed to flush the resource alternate data "
                               "body to a file"));

                errorDescription.details() =
                    QStringLiteral("resource local id = ") + resourceLocalId;

                QNWARNING("local_storage:sql:patches", errorDescription);
                return false;
            }

            databaseUpgradeInfo.setArrayIndex(processedResourceCounter);

            databaseUpgradeInfo.setValue(
                gResourceLocalIdColumn, resourceLocalId);

            lastProgress += singleResourceProgressFraction;

            QNDEBUG(
                "local_storage:sql:patches",
                "Processed resource data and alternate data for resource local "
                    << "id " << resourceLocalId << "; updated progress to "
                    << lastProgress);

            Q_EMIT progress(lastProgress);
        }

        pProcessedResourceLocalIdsDatabaseUpgradeInfoCloser.reset(nullptr);

        QNDEBUG(
            "local_storage:sql:patches",
            "Copied data bodies and alternate "
                << "data bodies of all resources from database to files");

        // Part 4: as data and alternate data for all resources has been written
        // to files, need to mark that fact in database upgrade persistence
        databaseUpgradeInfo.setValue(
            gUpgrade1To2AllResourceDataCopiedFromTablesToFilesKey, true);

        Q_EMIT progress(0.7);
    }

    // Part 5: delete resource data body and alternate data body from resources
    // table (unless already done)
    bool allResourceDataRemovedFromTables = false;
    if (allResourceDataCopiedFromTablesToFiles) {
        allResourceDataRemovedFromTables =
            databaseUpgradeInfo
                .value(gUpgrade1To2AllResourceDataRemovedFromTables)
                .toBool();
    }

    if (!allResourceDataRemovedFromTables) {
        // 5.1 Set resource data body and alternate data body to null
        {
            QSqlQuery query{database};
            const bool res =
                query.exec(QStringLiteral("UPDATE Resources SET dataBody=NULL, "
                                          "alternateDataBody=NULL"));
            ENSURE_DB_REQUEST_RETURN(
                res, query, "local_storage::sql::patches::1_to_2",
                QT_TR_NOOP(
                    "failed to execute SQL query setting resource data bodies "
                    "in tables to null"),
                false);
        }

        QNDEBUG(
            "local_storage:patches",
            "Set data bodies and alternate data "
                << "bodies for resources to null in the database table");

        Q_EMIT progress(0.8);

        // 5.2 Compact the database to reduce its size and make it faster to
        // operate
        ErrorString compactionError;
        if (!compactDatabase(database, compactionError)) {
            errorDescription = errorPrefix;
            errorDescription.appendBase(compactionError.base());
            errorDescription.appendBase(compactionError.additionalBases());
            errorDescription.details() = compactionError.details();
            QNWARNING("local_storage:sql:patches", errorDescription);
            return false;
        }

        QNDEBUG(
            "local_storage:patches", "Compacted the local storage database");
        Q_EMIT progress(0.9);

        // 5.3 Mark the removal of resource tables in upgrade persistence
        databaseUpgradeInfo.setValue(
            gUpgrade1To2AllResourceDataRemovedFromTables, true);
    }

    Q_EMIT progress(0.95);

    // Part 6: change the version in local storage database
    QSqlQuery query{database};
    const bool res = query.exec(
        QStringLiteral("INSERT OR REPLACE INTO Auxiliary (version) VALUES(2)"));

    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::patches::1_to_2",
        QT_TR_NOOP(
            "failed to execute SQL query increasing local storage version"),
        false);

    QNDEBUG(
        "local_storage:patches",
        "Finished upgrading the local storage from version 1 to version 2");

    return true;
}

int Patch1To2::resourceCount(
    QSqlDatabase & database, ErrorString & errorDescription) const
{
    QSqlQuery query{database};

    const bool res = query.exec(
        QStringLiteral("SELECT COUNT(*) FROM Resources"));

    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::patches::1_to_2",
        QT_TR_NOOP("failed to execute SQL query fetching resource count"),
        false);

    if (!query.next()) {
        QNDEBUG(
            "local_storage:sql:patches",
            "Found no resources in the local storage database");
        return 0;
    }

    bool conversionResult = false;
    const int count = query.value(0).toInt(&conversionResult);
    if (!conversionResult) {
        errorDescription.setBase(
            QT_TR_NOOP("failed to convert resource count to int"));
        QNWARNING("local_storage:sql:patches", errorDescription);
        return -1;
    }

    return count;
}

bool Patch1To2::compactDatabase(
    QSqlDatabase & database, ErrorString & errorDescription)
{
    QSqlQuery query{database};

    const bool res = query.exec(QStringLiteral("VACUUM"));

    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::patches::1_to_2",
        QT_TR_NOOP(
            "failed to execute SQL query compacting the local storage "
            "database"),
        false);

    return true;
}

} // namespace quentier::local_storage::sql
