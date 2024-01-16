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

#include "Patch1To2.h"
#include "PatchUtils.h"

#include "../ConnectionPool.h"
#include "../ErrorHandling.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/RuntimeError.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/threading/Future.h>
#include <quentier/types/ErrorString.h>
#include <quentier/utility/ApplicationSettings.h>
#include <quentier/utility/EventLoopWithExitStatus.h>
#include <quentier/utility/FileCopier.h>
#include <quentier/utility/FileSystem.h>
#include <quentier/utility/StandardPaths.h>
#include <quentier/utility/StringUtils.h>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include <quentier/threading/Qt5Promise.h>
#endif

#include <QSqlQuery>
#include <QSqlRecord>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <utility>

namespace quentier::local_storage::sql {

namespace {

const char * gUpgrade1To2Persistence =
    "LocalStorageDatabaseUpgradeFromVersion1ToVersion2";

const char * gUpgrade1To2AllResourceDataCopiedFromTablesToFilesKey =
    "AllResourceDataCopiedFromTableToFiles";

const char * gUpgrade1To2LocalIdsForResourcesCopiedToFilesKey =
    "LocalUidsOfResourcesCopiedToFiles";

const char * gUpgrade1To2AllResourceDataRemovedFromTables =
    "AllResourceDataRemovedFromResourceTable";

const char * gResourceLocalIdColumn = "resourceLocalUid";

template <typename T>
bool extractEntry(const QSqlRecord & rec, const QString & name, T & entry)
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
    Account account, ConnectionPoolPtr connectionPool,
    threading::QThreadPtr writerThread) :
    PatchBase(
        std::move(connectionPool), std::move(writerThread),
        accountPersistentStoragePath(account),
        accountPersistentStoragePath(account) +
            QStringLiteral("/backup_upgrade_1_to_2_") +
            QDateTime::currentDateTime().toString(Qt::ISODate)),
    m_account{std::move(account)}
{
    if (Q_UNLIKELY(m_account.isEmpty())) {
        throw InvalidArgument{
            ErrorString{QStringLiteral("Patch1To2 ctor: account is empty")}};
    }
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

bool Patch1To2::backupLocalStorageSync(
    QPromise<void> & promise, ErrorString & errorDescription)
{
    QNDEBUG("local_storage::sql::patches", "Patch1To2::backupLocalStorageSync");

    return utils::backupLocalStorageDatabaseFiles(
        m_localStorageDir.absolutePath(), m_backupDir.absolutePath(), promise,
        errorDescription);
}

bool Patch1To2::restoreLocalStorageFromBackupSync(
    QPromise<void> & promise, ErrorString & errorDescription)
{
    QNINFO(
        "local_storage::sql::patches",
        "Patch1To2::restoreLocalStorageFromBackupSync");

    return utils::restoreLocalStorageDatabaseFilesFromBackup(
        m_localStorageDir.absolutePath(), m_backupDir.absolutePath(), promise,
        errorDescription);
}

bool Patch1To2::removeLocalStorageBackupSync(ErrorString & errorDescription)
{
    QNINFO(
        "local_storage::sql::patches",
        "Patch1To2::removeLocalStorageBackupSync");

    return utils::removeLocalStorageDatabaseFilesBackup(
        m_backupDir.absolutePath(), errorDescription);
}

bool Patch1To2::applySync(
    QPromise<void> & promise, ErrorString & errorDescription)
{
    QNDEBUG("local_storage::sql::patches", "Patch1To2::applySync");

    ApplicationSettings databaseUpgradeInfo{
        m_account, QString::fromUtf8(gUpgrade1To2Persistence)};

    ErrorString errorPrefix{QStringLiteral(
        "failed to upgrade local storage from version 1 to version 2")};

    errorDescription.clear();

    int lastProgress = 0;
    const QString storagePath = accountPersistentStoragePath(m_account);

    QStringList resourceLocalIds;

    const bool allResourceDataCopiedFromTablesToFiles =
        databaseUpgradeInfo
            .value(gUpgrade1To2AllResourceDataCopiedFromTablesToFilesKey)
            .toBool();

    auto database = m_connectionPool->database();

    if (!allResourceDataCopiedFromTablesToFiles) {
        // Part 1: extract the list of resource local uids from the local
        // storage database
        resourceLocalIds = listResourceLocalIds(database, errorDescription);

        if (resourceLocalIds.isEmpty() && !errorDescription.isEmpty()) {
            return false;
        }

        lastProgress = 5;
        promise.setProgressValue(lastProgress);

        filterResourceLocalIds(resourceLocalIds);

        // Part 2: ensure the directories for resources data body and
        // recognition data body exist, create them if necessary
        if (!ensureExistenceOfResouceDataDirs(errorDescription)) {
            return false;
        }

        // Part 3: copy the data for each resource local uid into the local file
        databaseUpgradeInfo.beginWriteArray(
            gUpgrade1To2LocalIdsForResourcesCopiedToFilesKey);

        auto pProcessedResourceLocalIdsDatabaseUpgradeInfoCloser =
            std::make_unique<ApplicationSettings::ArrayCloser>(
                databaseUpgradeInfo);

        const auto numResources = resourceLocalIds.size();
        double singleResourceProgressFraction = (0.01 * (70 - lastProgress)) /
            std::max(1.0, static_cast<double>(numResources));

        int processedResourceCounter = 0;
        for (const auto & resourceLocalId: std::as_const(resourceLocalIds)) {
            QSqlQuery query{database};

            const bool res =
                query.exec(QString::fromUtf8("SELECT noteLocalUid, dataBody, "
                                             "alternateDataBody FROM Resources "
                                             "WHERE resourceLocalUid='%1'")
                               .arg(resourceLocalId));

            ENSURE_DB_REQUEST_RETURN(
                res, query, "local_storage::sql::patches::1_to_2",
                QStringLiteral(
                    "failed to execute SQL query fetching resource data bodies "
                    "from tables"),
                false);

            if (Q_UNLIKELY(!query.next())) {
                errorDescription = errorPrefix;
                errorDescription.appendBase(QStringLiteral(
                    "failed to fetch resource information from the local "
                    "storage database"));

                errorDescription.details() =
                    QStringLiteral("resource local id = ") + resourceLocalId;

                QNWARNING("local_storage::sql::patches", errorDescription);
                return false;
            }

            const QSqlRecord rec = query.record();

            QString noteLocalId;
            if (!extractEntry(rec, QStringLiteral("noteLocalUid"), noteLocalId))
            {
                errorDescription = errorPrefix;
                errorDescription.appendBase(QStringLiteral(
                    "failed to get note local id corresponding to a resource"));
                QNWARNING("local_storage::sql::patches", errorDescription);
                return false;
            }

            QByteArray dataBody;
            if (!extractEntry(rec, QStringLiteral("dataBody"), dataBody)) {
                errorDescription = errorPrefix;
                errorDescription.appendBase(QStringLiteral(
                    "failed to get data body corresponding to a resource"));
                QNWARNING("local_storage::sql::patches", errorDescription);
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
                    errorDescription.appendBase(QStringLiteral(
                        "failed to create directory for resource data bodies "
                        "for some note"));

                    errorDescription.details() =
                        QStringLiteral("note local id = ") + noteLocalId;

                    QNWARNING("local_storage::sql::patches", errorDescription);
                    return false;
                }
            }

            // 3.2 Write resource data body to a file
            QFile resourceDataFile{
                noteResourceDataDir.absolutePath() + QStringLiteral("/") +
                resourceLocalId + QStringLiteral(".dat")};

            if (!resourceDataFile.open(QIODevice::WriteOnly)) {
                errorDescription = errorPrefix;
                errorDescription.appendBase(QStringLiteral(
                    "failed to open resource data file for writing"));

                errorDescription.details() =
                    QStringLiteral("resource local uid = ") + resourceLocalId;

                QNWARNING("local_storage::sql::patches", errorDescription);
                return false;
            }

            const qint64 dataSize = dataBody.size();
            qint64 bytesWritten = resourceDataFile.write(dataBody);
            if (bytesWritten < 0) {
                errorDescription = errorPrefix;
                errorDescription.appendBase(QStringLiteral(
                    "failed to write resource data body to a file"));

                errorDescription.details() =
                    QStringLiteral("resource local uid = ") + resourceLocalId;

                QNWARNING("local_storage::sql::patches", errorDescription);
                return false;
            }

            if (bytesWritten < dataSize) {
                errorDescription = errorPrefix;
                errorDescription.appendBase(QStringLiteral(
                    "failed to write whole resource data body to a file"));

                errorDescription.details() =
                    QStringLiteral("resource local id = ") + resourceLocalId;

                QNWARNING("local_storage::sql::patches", errorDescription);
                return false;
            }

            if (!resourceDataFile.flush()) {
                errorDescription = errorPrefix;
                errorDescription.appendBase(QStringLiteral(
                    "failed to flush the resource data body to a file"));

                errorDescription.details() =
                    QStringLiteral("resource local uid = ") + resourceLocalId;

                QNWARNING("local_storage::sql::patches", errorDescription);
                return false;
            }

            // 3.3 If there's no resource alternate data for this resource,
            // we are done with it
            if (alternateDataBody.isEmpty()) {
                databaseUpgradeInfo.setArrayIndex(processedResourceCounter);

                databaseUpgradeInfo.setValue(
                    gResourceLocalIdColumn, resourceLocalId);

                lastProgress += static_cast<int>(
                    std::round(singleResourceProgressFraction * 100.0));

                QNDEBUG(
                    "local_storage::sql::patches",
                    "Processed resource data (no alternate data) for resource "
                        << "local id " << resourceLocalId
                        << "; updated progress to " << lastProgress);

                promise.setProgressValue(lastProgress);
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
                    errorDescription.appendBase(QStringLiteral(
                        "failed to create directory for resource "
                        "alternate data bodies for some note"));

                    errorDescription.details() =
                        QStringLiteral("note local uid = ") + noteLocalId;

                    QNWARNING("local_storage::sql::patches", errorDescription);
                    return false;
                }
            }

            // 3.5 Write resource alternate data body to a file
            QFile resourceAlternateDataFile{
                noteResourceAlternateDataDir.absolutePath() +
                QStringLiteral("/") + resourceLocalId + QStringLiteral(".dat")};

            if (!resourceAlternateDataFile.open(QIODevice::WriteOnly)) {
                errorDescription = errorPrefix;
                errorDescription.appendBase(QStringLiteral(
                    "failed to open resource alternate data file for writing"));

                errorDescription.details() =
                    QStringLiteral("resource local id = ") + resourceLocalId;

                QNWARNING("local_storage::sql::patches", errorDescription);
                return false;
            }

            const qint64 alternateDataSize = alternateDataBody.size();
            bytesWritten = resourceAlternateDataFile.write(alternateDataBody);
            if (bytesWritten < 0) {
                errorDescription = errorPrefix;

                errorDescription.appendBase(QStringLiteral(
                    "failed to write resource alternate data body to a file"));

                errorDescription.details() =
                    QStringLiteral("resource local id = ") + resourceLocalId;

                QNWARNING("local_storage::sql::patches", errorDescription);
                return false;
            }

            if (bytesWritten < alternateDataSize) {
                errorDescription = errorPrefix;

                errorDescription.appendBase(QStringLiteral(
                    "failed to write whole resource alternate data body to a "
                    "file"));

                errorDescription.details() =
                    QStringLiteral("resource local id = ") + resourceLocalId;

                QNWARNING("local_storage::sql::patches", errorDescription);
                return false;
            }

            if (!resourceAlternateDataFile.flush()) {
                errorDescription = errorPrefix;

                errorDescription.appendBase(QStringLiteral(
                    "failed to flush the resource alternate data "
                    "body to a file"));

                errorDescription.details() =
                    QStringLiteral("resource local id = ") + resourceLocalId;

                QNWARNING("local_storage::sql::patches", errorDescription);
                return false;
            }

            databaseUpgradeInfo.setArrayIndex(processedResourceCounter);

            databaseUpgradeInfo.setValue(
                gResourceLocalIdColumn, resourceLocalId);

            lastProgress += static_cast<int>(
                std::round(singleResourceProgressFraction * 100.0));

            QNDEBUG(
                "local_storage::sql::patches",
                "Processed resource data and alternate data for resource local "
                    << "id " << resourceLocalId << "; updated progress to "
                    << lastProgress);

            promise.setProgressValue(lastProgress);
        }

        pProcessedResourceLocalIdsDatabaseUpgradeInfoCloser.reset(nullptr);

        QNDEBUG(
            "local_storage::sql::patches",
            "Copied data bodies and alternate "
                << "data bodies of all resources from database to files");

        // Part 4: as data and alternate data for all resources has been written
        // to files, need to mark that fact in database upgrade persistence
        databaseUpgradeInfo.setValue(
            gUpgrade1To2AllResourceDataCopiedFromTablesToFilesKey, true);

        promise.setProgressValue(70);
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
                QStringLiteral(
                    "failed to execute SQL query setting resource data bodies "
                    "in tables to null"),
                false);
        }

        QNDEBUG(
            "local_storage::sql::patches",
            "Set data bodies and alternate data bodies for resources to null "
                << "in the database table");

        promise.setProgressValue(80);

        // 5.2 Compact the database to reduce its size and make it faster to
        // operate
        ErrorString compactionError;
        if (!compactDatabase(database, compactionError)) {
            errorDescription = errorPrefix;
            errorDescription.appendBase(compactionError.base());
            errorDescription.appendBase(compactionError.additionalBases());
            errorDescription.details() = compactionError.details();
            QNWARNING("local_storage::sql::patches", errorDescription);
            return false;
        }

        QNDEBUG(
            "local_storage::sql::patches",
            "Compacted the local storage database");

        promise.setProgressValue(90);

        // 5.3 Mark the removal of resource tables in upgrade persistence
        databaseUpgradeInfo.setValue(
            gUpgrade1To2AllResourceDataRemovedFromTables, true);
    }

    promise.setProgressValue(95);

    // Part 6: change the version in local storage database
    QSqlQuery query{database};
    const bool res = query.exec(
        QStringLiteral("INSERT OR REPLACE INTO Auxiliary (version) VALUES(2)"));

    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::patches::1_to_2",
        QStringLiteral(
            "failed to execute SQL query increasing local storage version"),
        false);

    QNDEBUG(
        "local_storage::sql::patches",
        "Finished upgrading the local storage from version 1 to version 2");

    return true;
}

QStringList Patch1To2::listResourceLocalIds(
    QSqlDatabase & database, ErrorString & errorDescription) const
{
    QSqlQuery query{database};

    const bool res =
        query.exec(QStringLiteral("SELECT resourceLocalUid FROM Resources"));

    if (Q_UNLIKELY(!res)) {
        errorDescription.setBase(QStringLiteral(
            "failed to collect the local ids of resources which need to be "
            "transferred to another table as a part of database upgrade"));

        errorDescription.details() = query.lastError().text();
        QNWARNING("tests:local_storage", errorDescription);
        return {};
    }

    QStringList resourceLocalIds;
    resourceLocalIds.reserve(std::max(query.size(), 0));

    while (query.next()) {
        const QSqlRecord rec = query.record();

        const QString resourceLocalId =
            rec.value(QStringLiteral("resourceLocalUid")).toString();

        if (Q_UNLIKELY(resourceLocalId.isEmpty())) {
            errorDescription.setBase(QStringLiteral(
                "failed to extract local id of a resource which needs a "
                "transfer of its binary data into another table as a part of "
                "database upgrade"));
            QNWARNING("tests:local_storage", errorDescription);
            return {};
        }

        resourceLocalIds << resourceLocalId;
    }

    return resourceLocalIds;
}

void Patch1To2::filterResourceLocalIds(QStringList & resourceLocalIds) const
{
    QNDEBUG("local_storage:patches", "Patch1To2::filterResourceLocalIds");

    ApplicationSettings databaseUpgradeInfo{
        m_account, QString::fromUtf8(gUpgrade1To2Persistence)};

    const int numEntries = databaseUpgradeInfo.beginReadArray(
        gUpgrade1To2LocalIdsForResourcesCopiedToFilesKey);

    QSet<QString> processedResourceLocalIds;
    processedResourceLocalIds.reserve(numEntries);
    for (int i = 0; i < numEntries; ++i) {
        databaseUpgradeInfo.setArrayIndex(i);
        const QString str =
            databaseUpgradeInfo.value(gResourceLocalIdColumn).toString();
        Q_UNUSED(processedResourceLocalIds.insert(str))
    }

    databaseUpgradeInfo.endArray();

    const auto it = std::remove_if(
        resourceLocalIds.begin(), resourceLocalIds.end(),
        [&processedResourceLocalIds](const QString & localId) {
            return processedResourceLocalIds.contains(localId);
        });

    resourceLocalIds.erase(it, resourceLocalIds.end());
}

bool Patch1To2::ensureExistenceOfResouceDataDirs(ErrorString & errorDescription)
{
    QNDEBUG(
        "local_storage:patches", "Patch1To2::ensureExistenceOfResouceDataDirs");

    const QString storagePath = accountPersistentStoragePath(m_account);

    QDir resourcesDataBodyDir{storagePath + QStringLiteral("/Resources/data")};
    if (!resourcesDataBodyDir.exists()) {
        const bool res =
            resourcesDataBodyDir.mkpath(resourcesDataBodyDir.absolutePath());

        if (!res) {
            errorDescription.setBase(QStringLiteral(
                "failed to create directory for resource data body storage"));

            errorDescription.details() =
                QDir::toNativeSeparators(resourcesDataBodyDir.absolutePath());

            QNWARNING("tests:local_storage", errorDescription);
            return false;
        }
    }

    QDir resourcesAlternateDataBodyDir{
        storagePath + QStringLiteral("/Resources/alternateData")};

    if (!resourcesAlternateDataBodyDir.exists()) {
        const bool res = resourcesAlternateDataBodyDir.mkpath(
            resourcesAlternateDataBodyDir.absolutePath());

        if (!res) {
            errorDescription.setBase(QStringLiteral(
                "failed to create directory for resource alternate data body "
                "storage"));

            errorDescription.details() = QDir::toNativeSeparators(
                resourcesAlternateDataBodyDir.absolutePath());

            QNWARNING("tests:local_storage", errorDescription);
            return false;
        }
    }

    return true;
}

bool Patch1To2::compactDatabase(
    QSqlDatabase & database, ErrorString & errorDescription)
{
    QSqlQuery query{database};

    const bool res = query.exec(QStringLiteral("VACUUM"));

    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::patches::1_to_2",
        QStringLiteral(
            "failed to execute SQL query compacting the local storage "
            "database"),
        false);

    return true;
}

} // namespace quentier::local_storage::sql
