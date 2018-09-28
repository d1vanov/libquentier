/*
 * Copyright 2018 Dmitry Ivanov
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

#include "LocalStoragePatch1To2.h"
#include "../LocalStorageManager_p.h"
#include "../LocalStorageShared.h"
#include <quentier/types/ErrorString.h>
#include <quentier/utility/ApplicationSettings.h>
#include <quentier/utility/StandardPaths.h>
#include <quentier/utility/StringUtils.h>
#include <quentier/utility/FileCopier.h>
#include <quentier/utility/EventLoopWithExitStatus.h>
#include <quentier/utility/Utility.h>
#include <quentier/logging/QuentierLogger.h>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QScopedPointer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QThread>

#define UPGRADE_1_TO_2_PERSISTENCE QStringLiteral("LocalStorageDatabaseUpgradeFromVersion1ToVersion2")

#define UPGRADE_1_TO_2_ALL_RESOURCE_DATA_COPIED_FROM_TABLE_TO_FILES_KEY QStringLiteral("AllResourceDataCopiedFromTableToFiles")
#define UPGRADE_1_TO_2_LOCAL_UIDS_FOR_RESOURCES_COPIED_TO_FILES_KEY QStringLiteral("LocalUidsOfResourcesCopiedToFiles")
#define UPGRADE_1_TO_2_ALL_RESOURCE_DATA_REMOVED_FROM_RESOURCE_TABLE QStringLiteral("AllResourceDataRemovedFromResourceTable")

#define RESOURCE_LOCAL_UID QStringLiteral("resourceLocalUid")

namespace quentier {

LocalStoragePatch1To2::LocalStoragePatch1To2(const Account & account,
                                             LocalStorageManagerPrivate & localStorageManager,
                                             QSqlDatabase & database, QObject * parent) :
    ILocalStoragePatch(parent),
    m_account(account),
    m_localStorageManager(localStorageManager),
    m_sqlDatabase(database)
{}

QString LocalStoragePatch1To2::patchShortDescription() const
{
    return tr("Move attachments data from SQLite database to plain files");
}

QString LocalStoragePatch1To2::patchLongDescription() const
{
    QString result;

    result += tr("This patch will move the data corresponding to notes' attachments from Quentier's primary SQLite " \
                 "database to plain files. This change of local storage structure is necessary to fix or prevent " \
                 "serious performance issues for accounts containing numerous large enough note attachments due to " \
                 "the way SQLite puts large data blocks together within the database file. If you are interested " \
                 "in technical details on this topic, consider consulting the following material");
    result += QStringLiteral(": <a href=\"https://www.sqlite.org/intern-v-extern-blob.html\">Internal Versus External BLOBs in SQLite</a>.\n\n");
    result += tr("The time required to apply this patch would depend on the general performance " \
                 "of disk I/O on your system and on the number of resources within your account");

    ErrorString errorDescription;
    int numResources = m_localStorageManager.enResourceCount(errorDescription);
    if (Q_UNLIKELY(numResources < 0)) {
        QNWARNING(QStringLiteral("Can't get the number of resources within the local storage database: ") << errorDescription);
    }
    else {
        QNINFO(QStringLiteral("Before applying local storage 1-to-2 patch: ") << numResources << QStringLiteral(" resources within the local storage"));
        result += QStringLiteral(" (");
        result += QString::number(numResources);
        result += QStringLiteral(")");
    }

    result += QStringLiteral(".\n\n");
    result += tr("If the account which local storage is to be upgraded is Evernote one and if you don't have any local " \
                 "unsynchronized changes there, you can consider just wiping out its data folder");
    result += QStringLiteral(" (");
    result += QDir::toNativeSeparators(accountPersistentStoragePath(m_account));
    result += QStringLiteral(") ");
    result += tr("and re-syncing it from Evernote instead of upgrading " \
                 "the local database - if your account contains many large enough attachments to notes, re-syncing can " \
                 "actually be faster than upgrading the local storage");
    result += QStringLiteral(".\n\n");
    result += tr("Note that after the upgrade previous versions of Quentier would no longer be able to use this account's local storage");
    result += QStringLiteral(".");
    return result;
}

bool LocalStoragePatch1To2::backupLocalStorage(ErrorString & errorDescription)
{
    QNINFO(QStringLiteral("LocalStoragePatch1To2::backupLocalStorage"));

    QString storagePath = accountPersistentStoragePath(m_account);

    QFileInfo shmDbFileInfo(storagePath + QStringLiteral("/qn.storage.sqlite-shm"));
    if (shmDbFileInfo.exists())
    {
        QString shmDbFilePath = shmDbFileInfo.absoluteFilePath();
        QString shmDbBackupFilePath = shmDbFilePath + QStringLiteral(".bak");

        QFileInfo shmDbBackupFileInfo(shmDbBackupFilePath);
        if (shmDbBackupFileInfo.exists() && !removeFile(shmDbBackupFilePath)) {
            errorDescription.setBase(QT_TR_NOOP("Can't backup local storage: failed to remove pre-existing SQLite shm backup file"));
            errorDescription.details() = QDir::toNativeSeparators(shmDbBackupFilePath);
            QNWARNING(errorDescription);
            return false;
        }

        if (!QFile::copy(shmDbFilePath, shmDbBackupFilePath)) {
            errorDescription.setBase(QT_TR_NOOP("Can't backup local storage: failed to backup SQLite shm file"));
            errorDescription.details() = QDir::toNativeSeparators(shmDbFilePath);
            QNWARNING(errorDescription);
            return false;
        }
    }

    QFileInfo walDbFileInfo(storagePath + QStringLiteral("/qn.storage.sqlite-wal"));
    if (walDbFileInfo.exists())
    {
        QString walDbFilePath = walDbFileInfo.absoluteFilePath();
        QString walDbBackupFilePath = walDbFilePath + QStringLiteral(".bak");

        QFileInfo walDbBackupFileInfo(walDbBackupFilePath);
        if (walDbBackupFileInfo.exists() && !removeFile(walDbBackupFilePath)) {
            errorDescription.setBase(QT_TR_NOOP("Can't backup local storage: failed to remove pre-existing SQLite wal backup file"));
            errorDescription.details() = QDir::toNativeSeparators(walDbBackupFilePath);
            QNWARNING(errorDescription);
            return false;
        }

        if (!QFile::copy(walDbFilePath, walDbBackupFilePath)) {
            errorDescription.setBase(QT_TR_NOOP("Can't backup local storage: failed to backup SQLite wal file"));
            errorDescription.details() = QDir::toNativeSeparators(walDbFilePath);
            QNWARNING(errorDescription);
            return false;
        }
    }

    EventLoopWithExitStatus backupEventLoop;

    QThread * pMainDbFileCopierThread = new QThread;
    QObject::connect(pMainDbFileCopierThread, QNSIGNAL(QThread,finished),
                     pMainDbFileCopierThread, QNSLOT(QThread,deleteLater));
    pMainDbFileCopierThread->start();

    FileCopier * pMainDbFileCopier = new FileCopier;
    QObject::connect(pMainDbFileCopier, QNSIGNAL(FileCopier,progressUpdate,double),
                     this, QNSIGNAL(LocalStoragePatch1To2,backupProgress,double));
    QObject::connect(pMainDbFileCopier, QNSIGNAL(FileCopier,notifyError,ErrorString),
                     &backupEventLoop, QNSLOT(EventLoopWithExitStatus,exitAsFailureWithErrorString,ErrorString));
    QObject::connect(pMainDbFileCopier, QNSIGNAL(FileCopier,finished),
                     &backupEventLoop, QNSLOT(EventLoopWithExitStatus,exitAsSuccess));
    QObject::connect(pMainDbFileCopier, QNSIGNAL(FileCopier,finished),
                     pMainDbFileCopier, QNSLOT(FileCopier,deleteLater));
    QObject::connect(pMainDbFileCopier, QNSIGNAL(FileCopier,finished),
                     pMainDbFileCopierThread, QNSLOT(QThread,quit));
    QObject::connect(this, QNSIGNAL(LocalStoragePatch1To2,copyDbFile,QString,QString),
                     pMainDbFileCopier, QNSLOT(FileCopier,copyFile,QString,QString));
    pMainDbFileCopier->moveToThread(pMainDbFileCopierThread);

    QTimer::singleShot(0, this, SLOT(startLocalStorageBackup()));

    int result = backupEventLoop.exec();

    QObject::disconnect(this, QNSIGNAL(LocalStoragePatch1To2,copyDbFile,QString,QString),
                        pMainDbFileCopier, QNSLOT(FileCopier,copyFile,QString,QString));

    if (result == EventLoopWithExitStatus::ExitStatus::Failure) {
        errorDescription = backupEventLoop.errorDescription();
        return false;
    }

    return true;
}

bool LocalStoragePatch1To2::restoreLocalStorageFromBackup(ErrorString & errorDescription)
{
    QNINFO(QStringLiteral("LocalStoragePatch1To2::restoreLocalStorageFromBackup"));

    QString storagePath = accountPersistentStoragePath(m_account);

    QFileInfo shmDbBackupFileInfo(storagePath + QStringLiteral("/qn.storage.sqlite-shm.bak"));
    if (shmDbBackupFileInfo.exists())
    {
        QString shmDbBackupFilePath = shmDbBackupFileInfo.absoluteFilePath();
        QString shmDbFilePath = shmDbBackupFilePath;
        shmDbFilePath.chop(4);

        QFileInfo shmDbFileInfo(shmDbFilePath);
        if (shmDbFileInfo.exists() && !removeFile(shmDbFilePath)) {
            errorDescription.setBase(QT_TR_NOOP("Can't restore local storage from backup: failed to remove pre-existing SQLite shm file"));
            errorDescription.details() = QDir::toNativeSeparators(shmDbFilePath);
            QNWARNING(errorDescription);
            return false;
        }

        if (!QFile::copy(shmDbBackupFilePath, shmDbFilePath)) {
            errorDescription.setBase(QT_TR_NOOP("Can't restore local storage from backup: failed to restore SQLite shm file"));
            errorDescription.details() = QDir::toNativeSeparators(shmDbFilePath);
            QNWARNING(errorDescription);
            return false;
        }
    }

    QFileInfo walDbBackupFileInfo(storagePath + QStringLiteral("/qn.storage.sqlite-wal.bak"));
    if (walDbBackupFileInfo.exists())
    {
        QString walDbBackupFilePath = walDbBackupFileInfo.absoluteFilePath();
        QString walDbFilePath = walDbBackupFilePath;
        walDbFilePath.chop(4);

        QFileInfo walDbFileInfo(walDbFilePath);
        if (walDbFileInfo.exists() && !removeFile(walDbFilePath)) {
            errorDescription.setBase(QT_TR_NOOP("Can't restore local storage from backup: failed to remove pre-existing SQLite wal file"));
            errorDescription.details() = QDir::toNativeSeparators(walDbFilePath);
            QNWARNING(errorDescription);
            return false;
        }

        if (!QFile::copy(walDbBackupFilePath, walDbFilePath)) {
            errorDescription.setBase(QT_TR_NOOP("Can't restore local storage from backup: failed to restore SQLite wal file"));
            errorDescription.details() = QDir::toNativeSeparators(walDbFilePath);
            QNWARNING(errorDescription);
            return false;
        }
    }

    EventLoopWithExitStatus restoreFromBackupEventLoop;

    QThread * pMainDbFileCopierThread = new QThread;
    QObject::connect(pMainDbFileCopierThread, QNSIGNAL(QThread,finished),
                     pMainDbFileCopierThread, QNSLOT(QThread,deleteLater));
    pMainDbFileCopierThread->start();

    FileCopier * pMainDbFileCopier = new FileCopier;
    QObject::connect(pMainDbFileCopier, QNSIGNAL(FileCopier,progressUpdate,double),
                     this, QNSIGNAL(LocalStoragePatch1To2,restoreBackupProgress,double));
    QObject::connect(pMainDbFileCopier, QNSIGNAL(FileCopier,notifyError,ErrorString),
                     &restoreFromBackupEventLoop, QNSLOT(EventLoopWithExitStatus,exitAsFailureWithErrorString,ErrorString));
    QObject::connect(pMainDbFileCopier, QNSIGNAL(FileCopier,finished),
                     &restoreFromBackupEventLoop, QNSLOT(EventLoopWithExitStatus,exitAsSuccess));
    QObject::connect(pMainDbFileCopier, QNSIGNAL(FileCopier,finished),
                     pMainDbFileCopier, QNSLOT(FileCopier,deleteLater));
    QObject::connect(pMainDbFileCopier, QNSIGNAL(FileCopier,finished),
                     pMainDbFileCopierThread, QNSLOT(QThread,quit));
    QObject::connect(this, QNSIGNAL(LocalStoragePatch1To2,copyDbFile,QString,QString),
                     pMainDbFileCopier, QNSLOT(FileCopier,copyFile,QString,QString));
    pMainDbFileCopier->moveToThread(pMainDbFileCopierThread);

    QTimer::singleShot(0, this, SLOT(startLocalStorageRestorationFromBackup()));

    int result = restoreFromBackupEventLoop.exec();

    QObject::disconnect(this, QNSIGNAL(LocalStoragePatch1To2,copyDbFile,QString,QString),
                        pMainDbFileCopier, QNSLOT(FileCopier,copyFile,QString,QString));

    if (result == EventLoopWithExitStatus::ExitStatus::Failure) {
        errorDescription = restoreFromBackupEventLoop.errorDescription();
        return false;
    }

    return true;
}

bool LocalStoragePatch1To2::removeLocalStorageBackup(ErrorString & errorDescription)
{
    QNINFO(QStringLiteral("LocalStoragePatch1To2::removeLocalStorageBackup"));

    QString storagePath = accountPersistentStoragePath(m_account);

    bool removedShmDbBackup = true;
    QFileInfo shmDbBackupFileInfo(storagePath + QStringLiteral("/qn.storage.sqlite-shm.bak"));
    if (shmDbBackupFileInfo.exists() && !removeFile(shmDbBackupFileInfo.absoluteFilePath())) {
        QNDEBUG(QStringLiteral("Failed to remove SQLite shm file's backup: ") << shmDbBackupFileInfo.absoluteFilePath());
        removedShmDbBackup = false;
    }

    bool removedWalDbBackup = true;
    QFileInfo walDbBackupFileInfo(storagePath + QStringLiteral("/qn.storage.sqlite-wal.bak"));
    if (walDbBackupFileInfo.exists() && !removeFile(walDbBackupFileInfo.absoluteFilePath())) {
        QNDEBUG(QStringLiteral("Failed to remove SQLite wal file's backup: ") << walDbBackupFileInfo.absoluteFilePath());
        removedWalDbBackup = false;
    }

    bool removedDbBackup = true;
    QFileInfo dbBackupFileInfo(storagePath + QStringLiteral("/qn.storage.sqlite.bak"));
    if (dbBackupFileInfo.exists() && !removeFile(dbBackupFileInfo.absoluteFilePath())) {
        QNWARNING(QStringLiteral("Failed to remove SQLite database's backup: ") << dbBackupFileInfo.absoluteFilePath());
        removedDbBackup = false;
    }

    if (!removedShmDbBackup || !removedWalDbBackup || !removedDbBackup) {
        errorDescription.setBase(QT_TR_NOOP("Failed to remove some of SQLite database's backups"));
        return false;
    }

    return true;
}

bool LocalStoragePatch1To2::apply(ErrorString & errorDescription)
{
    QNINFO(QStringLiteral("LocalStoragePatch1To2::apply"));

    ApplicationSettings databaseUpgradeInfo(m_account, UPGRADE_1_TO_2_PERSISTENCE);

    ErrorString errorPrefix(QT_TR_NOOP("failed to upgrade local storage from version 1 to version 2"));
    errorDescription.clear();

    double lastProgress = 0.0;
    QString storagePath = accountPersistentStoragePath(m_account);

    QStringList resourceLocalUids;
    bool allResourceDataCopiedFromTablesToFiles = databaseUpgradeInfo.value(UPGRADE_1_TO_2_ALL_RESOURCE_DATA_COPIED_FROM_TABLE_TO_FILES_KEY).toBool();
    if (!allResourceDataCopiedFromTablesToFiles)
    {
        // Part 1: extract the list of resource local uids from the local storage database
        resourceLocalUids = listResourceLocalUidsForDatabaseUpgradeFromVersion1ToVersion2(errorDescription);
        if (resourceLocalUids.isEmpty() && !errorDescription.isEmpty()) {
            return false;
        }

        lastProgress = 0.05;
        Q_EMIT progress(lastProgress);

        filterResourceLocalUidsForDatabaseUpgradeFromVersion1ToVersion2(resourceLocalUids);

        // Part 2: ensure the directories for resources data body and recognition data body exist, create them if necessary
        if (!ensureExistenceOfResouceDataDirsForDatabaseUpgradeFromVersion1ToVersion2(errorDescription)) {
            return false;
        }

        // Part 3: copy the data for each resource local uid into the local file
        databaseUpgradeInfo.beginWriteArray(UPGRADE_1_TO_2_LOCAL_UIDS_FOR_RESOURCES_COPIED_TO_FILES_KEY);
        QScopedPointer<ApplicationSettings::ApplicationSettingsArrayCloser> pProcessedResourceLocalUidsDatabaseUpgradeInfoCloser(
                                                        new ApplicationSettings::ApplicationSettingsArrayCloser(databaseUpgradeInfo));

        int numResources = resourceLocalUids.size();
        double singleResourceProgressFraction = (0.7 - lastProgress) / std::max(1.0, static_cast<double>(numResources));

        int processedResourceCounter = 0;
        for(auto it = resourceLocalUids.constBegin(), end = resourceLocalUids.constEnd(); it != end; ++it, ++processedResourceCounter)
        {
            const QString & resourceLocalUid = *it;

            QSqlQuery query(m_sqlDatabase);
            bool res = query.exec(QString::fromUtf8("SELECT noteLocalUid, dataBody, alternateDataBody FROM Resources WHERE resourceLocalUid='%1'").arg(resourceLocalUid));
            DATABASE_CHECK_AND_SET_ERROR()

            if (Q_UNLIKELY(!query.next())) {
                errorDescription = errorPrefix;
                errorDescription.appendBase(QT_TR_NOOP("failed to fetch resource information from local storage database"));
                errorDescription.details() = QStringLiteral("resource local uid = ") + resourceLocalUid;
                QNWARNING(errorDescription);
                return false;
            }

            QSqlRecord rec = query.record();

            QString noteLocalUid;
            QByteArray dataBody;
            QByteArray alternateDataBody;

#define EXTRACT_ENTRY(name, type) \
    { \
        int index = rec.indexOf(QStringLiteral(#name)); \
        if (index >= 0) { \
            QVariant value = rec.value(index); \
            if (!value.isNull()) { \
                name = qvariant_cast<type>(value); \
            } \
        } \
        else if (required) { \
            errorDescription = errorPrefix; \
            errorDescription.appendBase(QT_TRANSLATE_NOOP("LocalStoragePatch1To2", "failed to get resource data from local storage database")); \
            errorDescription.details() = QStringLiteral(#name); \
            QNWARNING(errorDescription); \
            return false; \
        } \
    }

            bool required = true;
            EXTRACT_ENTRY(noteLocalUid, QString)
            EXTRACT_ENTRY(dataBody, QByteArray)

            required = false;
            EXTRACT_ENTRY(alternateDataBody, QByteArray)

#undef EXTRACT_ENTRY

            // 3.1 Ensure the existence of dir for note resource's data body
            QDir noteResourceDataDir(storagePath + QStringLiteral("/Resources/data/") + noteLocalUid);
            if (!noteResourceDataDir.exists())
            {
                bool res = noteResourceDataDir.mkpath(noteResourceDataDir.absolutePath());
                if (!res) {
                    errorDescription = errorPrefix;
                    errorDescription.appendBase(QT_TR_NOOP("failed to create directory for resource data bodies for some note"));
                    errorDescription.details() = QStringLiteral("note local uid = ") + noteLocalUid;
                    QNWARNING(errorDescription);
                    return false;
                }
            }

            // 3.2 Write resource data body to a file
            QFile resourceDataFile(noteResourceDataDir.absolutePath() + QStringLiteral("/") + resourceLocalUid + QStringLiteral(".dat"));
            if (!resourceDataFile.open(QIODevice::WriteOnly)) {
                errorDescription = errorPrefix;
                errorDescription.appendBase(QT_TR_NOOP("failed to open resource data file for writing"));
                errorDescription.details() = QStringLiteral("resource local uid = ") + resourceLocalUid;
                QNWARNING(errorDescription);
                return false;
            }

            qint64 dataSize = dataBody.size();
            qint64 bytesWritten = resourceDataFile.write(dataBody);
            if (bytesWritten < 0) {
                errorDescription = errorPrefix;
                errorDescription.appendBase(QT_TR_NOOP("failed to write resource data body to a file"));
                errorDescription.details() = QStringLiteral("resource local uid = ") + resourceLocalUid;
                QNWARNING(errorDescription);
                return false;
            }

            if (bytesWritten < dataSize) {
                errorDescription = errorPrefix;
                errorDescription.appendBase(QT_TR_NOOP("failed to write whole resource data body to a file"));
                errorDescription.details() = QStringLiteral("resource local uid = ") + resourceLocalUid;
                QNWARNING(errorDescription);
                return false;
            }

            if (!resourceDataFile.flush()) {
                errorDescription = errorPrefix;
                errorDescription.appendBase(QT_TR_NOOP("failed to flush the resource data body to a file"));
                errorDescription.details() = QStringLiteral("resource local uid = ") + resourceLocalUid;
                QNWARNING(errorDescription);
                return false;
            }

            // 3.3 If there's no resource alternate data for this resource, we are done with it
            if (alternateDataBody.isEmpty())
            {
                databaseUpgradeInfo.setArrayIndex(processedResourceCounter);
                databaseUpgradeInfo.setValue(RESOURCE_LOCAL_UID, resourceLocalUid);
                lastProgress += singleResourceProgressFraction;
                QNDEBUG(QStringLiteral("Processed resource data (no alternate data) for resource local uid ")
                        << resourceLocalUid << QStringLiteral("; updated progress to ") << lastProgress);
                Q_EMIT progress(lastProgress);
                continue;
            }

            // 3.4 Ensure the existence of dir for note resource's alternate data body
            QDir noteResourceAlternateDataDir(storagePath + QStringLiteral("/Resource/alternateData/") + noteLocalUid);
            if (!noteResourceAlternateDataDir.exists())
            {
                bool res = noteResourceAlternateDataDir.mkpath(noteResourceAlternateDataDir.absolutePath());
                if (!res) {
                    errorDescription = errorPrefix;
                    errorDescription.appendBase(QT_TR_NOOP("failed to create directory for resource alternate data bodies for some note"));
                    errorDescription.details() = QStringLiteral("note local uid = ") + noteLocalUid;
                    QNWARNING(errorDescription);
                    return false;
                }
            }

            // 3.5 Write resource alternate data body to a file
            QFile resourceAlternateDataFile(noteResourceAlternateDataDir.absolutePath() + QStringLiteral("/") + resourceLocalUid + QStringLiteral(".dat"));
            if (!resourceAlternateDataFile.open(QIODevice::WriteOnly)) {
                errorDescription = errorPrefix;
                errorDescription.appendBase(QT_TR_NOOP("failed to open resource alternate data file for writing"));
                errorDescription.details() = QStringLiteral("resource local uid = ") + resourceLocalUid;
                QNWARNING(errorDescription);
                return false;
            }

            qint64 alternateDataSize = alternateDataBody.size();
            bytesWritten = resourceAlternateDataFile.write(alternateDataBody);
            if (bytesWritten < 0) {
                errorDescription = errorPrefix;
                errorDescription.appendBase(QT_TR_NOOP("failed to write resource alternate data body to a file"));
                errorDescription.details() = QStringLiteral("resource local uid = ") + resourceLocalUid;
                QNWARNING(errorDescription);
                return false;
            }

            if (bytesWritten < alternateDataSize) {
                errorDescription = errorPrefix;
                errorDescription.appendBase(QT_TR_NOOP("failed to write whole resource alternate data body to a file"));
                errorDescription.details() = QStringLiteral("resource local uid = ") + resourceLocalUid;
                QNWARNING(errorDescription);
                return false;
            }

            if (!resourceAlternateDataFile.flush()) {
                errorDescription = errorPrefix;
                errorDescription.appendBase(QT_TR_NOOP("failed to flush the resource alternate data body to a file"));
                errorDescription.details() = QStringLiteral("resource local uid = ") + resourceLocalUid;
                QNWARNING(errorDescription);
                return false;
            }

            databaseUpgradeInfo.setArrayIndex(processedResourceCounter);
            databaseUpgradeInfo.setValue(RESOURCE_LOCAL_UID, resourceLocalUid);
            lastProgress += singleResourceProgressFraction;
            QNDEBUG(QStringLiteral("Processed resource data and alternate data for resource local uid ")
                    << resourceLocalUid << QStringLiteral("; updated progress to ") << lastProgress);
            Q_EMIT progress(lastProgress);
        }

        pProcessedResourceLocalUidsDatabaseUpgradeInfoCloser.reset(Q_NULLPTR);

        QNDEBUG(QStringLiteral("Copied data bodies and alternate data bodies of all resources from database to files"));

        // Part 4: as data and alternate data for all resources has been written to files, need to mark that fact in database upgrade persistence
        databaseUpgradeInfo.setValue(UPGRADE_1_TO_2_ALL_RESOURCE_DATA_COPIED_FROM_TABLE_TO_FILES_KEY, true);

        Q_EMIT progress(0.7);
    }

    // Part 5: delete resource data body and alternate data body from resources table (unless already done)
    bool allResourceDataRemovedFromTables = false;
    if (allResourceDataCopiedFromTablesToFiles) {
        allResourceDataRemovedFromTables = databaseUpgradeInfo.value(UPGRADE_1_TO_2_ALL_RESOURCE_DATA_REMOVED_FROM_RESOURCE_TABLE).toBool();
    }

    if (!allResourceDataRemovedFromTables)
    {
        // 5.1 Set resource data body and alternate data body to null
        {
            QSqlQuery query(m_sqlDatabase);
            bool res = query.exec(QStringLiteral("UPDATE Resources SET dataBody=NULL, alternateDataBody=NULL"));
            DATABASE_CHECK_AND_SET_ERROR()
        }

        QNDEBUG(QStringLiteral("Set data bodies and alternate data bodies for resources to null in the database table"));
        Q_EMIT progress(0.8);

        // 5.2 Compact the database to reduce its size and make it faster to operate
        ErrorString compactionError;
        if (!m_localStorageManager.compactLocalStorage(compactionError)) {
            errorDescription = errorPrefix;
            errorDescription.appendBase(compactionError.base());
            errorDescription.appendBase(compactionError.additionalBases());
            errorDescription.details() = compactionError.details();
            QNWARNING(errorDescription);
            return false;
        }

        QNDEBUG(QStringLiteral("Compacted the local storage database"));
        Q_EMIT progress(0.9);

        // 5.3 Mark the removal of resource tables in upgrade persistence
        databaseUpgradeInfo.setValue(UPGRADE_1_TO_2_ALL_RESOURCE_DATA_REMOVED_FROM_RESOURCE_TABLE, true);
    }

    Q_EMIT progress(0.95);

    // Part 6: change the version in local storage database
    QSqlQuery query(m_sqlDatabase);
    bool res = query.exec(QStringLiteral("INSERT OR REPLACE INTO Auxiliary (version) VALUES(1)"));
    DATABASE_CHECK_AND_SET_ERROR()

    QNDEBUG(QStringLiteral("Finished upgrading the local storage from version 1 to version 2"));
    return true;
}

QStringList LocalStoragePatch1To2::listResourceLocalUidsForDatabaseUpgradeFromVersion1ToVersion2(ErrorString & errorDescription)
{
    QSqlQuery query(m_sqlDatabase);
    bool res = query.exec(QStringLiteral("SELECT resourceLocalUid FROM Resources"));
    if (Q_UNLIKELY(!res)) {
        errorDescription.setBase(QT_TR_NOOP("failed to collect the local ids of resources which need to be transferred to another table as a part of database upgrade"));
        errorDescription.details() = query.lastError().text();
        QNWARNING(errorDescription);
        return QStringList();
    }

    QStringList resourceLocalUids;
    resourceLocalUids.reserve(std::max(query.size(), 0));
    while(query.next())
    {
        QSqlRecord rec = query.record();
        QString resourceLocalUid = rec.value(QStringLiteral("resourceLocalUid")).toString();
        if (Q_UNLIKELY(resourceLocalUid.isEmpty())) {
            errorDescription.setBase(QT_TR_NOOP("failed to extract local uid of a resource which needs a transfer of its binary data into another table as a part of database upgrade"));
            QNWARNING(errorDescription);
            return QStringList();
        }

        resourceLocalUids << resourceLocalUid;
    }

    return resourceLocalUids;
}

void LocalStoragePatch1To2::filterResourceLocalUidsForDatabaseUpgradeFromVersion1ToVersion2(QStringList & resourceLocalUids)
{
    QNDEBUG(QStringLiteral("LocalStoragePatch1To2::filterResourceLocalUidsForDatabaseUpgradeFromVersion1ToVersion2"));

    ApplicationSettings databaseUpgradeInfo(m_account, UPGRADE_1_TO_2_PERSISTENCE);

    int numEntries = databaseUpgradeInfo.beginReadArray(UPGRADE_1_TO_2_LOCAL_UIDS_FOR_RESOURCES_COPIED_TO_FILES_KEY);
    QSet<QString> processedResourceLocalUids;
    processedResourceLocalUids.reserve(numEntries);
    for(int i = 0; i < numEntries; ++i) {
        databaseUpgradeInfo.setArrayIndex(i);
        QString str = databaseUpgradeInfo.value(RESOURCE_LOCAL_UID).toString();
        Q_UNUSED(processedResourceLocalUids.insert(str))
    }

    databaseUpgradeInfo.endArray();

    auto it = std::remove_if(resourceLocalUids.begin(), resourceLocalUids.end(),
                             StringUtils::StringFilterPredicate(processedResourceLocalUids));
    resourceLocalUids.erase(it, resourceLocalUids.end());
}

bool LocalStoragePatch1To2::ensureExistenceOfResouceDataDirsForDatabaseUpgradeFromVersion1ToVersion2(ErrorString & errorDescription)
{
    QNDEBUG(QStringLiteral("LocalStoragePatch1To2::ensureExistenceOfResouceDataDirsForDatabaseUpgradeFromVersion1ToVersion2"));

    QString storagePath = accountPersistentStoragePath(m_account);

    QDir resourcesDataBodyDir(storagePath + QStringLiteral("/Resources/data"));
    if (!resourcesDataBodyDir.exists())
    {
        bool res = resourcesDataBodyDir.mkpath(resourcesDataBodyDir.absolutePath());
        if (!res) {
            errorDescription.setBase(QT_TR_NOOP("failed to create directory for resource data body storage"));
            errorDescription.details() = QDir::toNativeSeparators(resourcesDataBodyDir.absolutePath());
            QNWARNING(errorDescription);
            return false;
        }
    }

    QDir resourcesAlternateDataBodyDir(storagePath + QStringLiteral("/Resources/alternateData"));
    if (!resourcesAlternateDataBodyDir.exists())
    {
        bool res = resourcesAlternateDataBodyDir.mkpath(resourcesAlternateDataBodyDir.absolutePath());
        if (!res) {
            errorDescription.setBase(QT_TR_NOOP("failed to create directory for resource alternate data body storage"));
            errorDescription.details() = QDir::toNativeSeparators(resourcesAlternateDataBodyDir.absolutePath());
            QNWARNING(errorDescription);
            return false;
        }
    }

    return true;
}

void LocalStoragePatch1To2::startLocalStorageBackup()
{
    QNDEBUG(QStringLiteral("LocalStoragePatch1To2::startLocalStorageBackup"));

    QString storagePath = accountPersistentStoragePath(m_account);
    QString sourceDbFilePath = storagePath + QStringLiteral("/qn.storage.sqlite");
    QString backupFilePath = sourceDbFilePath + QStringLiteral(".bak");
    Q_EMIT copyDbFile(sourceDbFilePath, backupFilePath);
}

void LocalStoragePatch1To2::startLocalStorageRestorationFromBackup()
{
    QNDEBUG(QStringLiteral("LocalStoragePatch1To2::startLocalStorageRestorationFromBackup"));

    QString storagePath = accountPersistentStoragePath(m_account);
    QString sourceDbFilePath = storagePath + QStringLiteral("/qn.storage.sqlite");
    QString backupDbFilePath = sourceDbFilePath + QStringLiteral(".bak");
    Q_EMIT copyDbFile(backupDbFilePath, sourceDbFilePath);
}

} // namespace quentier
