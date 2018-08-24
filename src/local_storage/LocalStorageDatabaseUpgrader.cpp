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

#include "LocalStorageDatabaseUpgrader.h"
#include "LocalStorageShared.h"
#include "LocalStorageManager_p.h"
#include <quentier/utility/ApplicationSettings.h>
#include <quentier/utility/StandardPaths.h>
#include <quentier/logging/QuentierLogger.h>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QDir>

#define UPGRADE_1_TO_2_ALL_RESOURCE_DATA_COPIED_FROM_TABLE_TO_FILES_KEY QStringLiteral("AllResourceDataCopiedFromTableToFiles")
#define UPGRADE_1_TO_2_LOCAL_UIDS_FOR_RESOURCE_DATA_COPIED_TO_FILE_KEY QStringLiteral("LocalUidsOfResourceDataCopiedToFiles")
#define UPGRADE_1_TO_2_LOCAL_UIDS_FOR_RESOURCE_ALTERNATE_DATA_COPIED_TO_FILE_KEY QStringLiteral("LocalUidsOfResourceAlternateDataCopiedToFiles")

namespace quentier {

LocalStorageDatabaseUpgrader::LocalStorageDatabaseUpgrader(const Account & account,
                                                           LocalStorageManagerPrivate & localStorageManager,
                                                           QSqlDatabase & sqlDatabase, QObject * parent) :
    QObject(parent),
    m_account(account),
    m_localStorageManager(localStorageManager),
    m_sqlDatabase(sqlDatabase)
{}

bool LocalStorageDatabaseUpgrader::databaseRequiresUpgrade(ErrorString & errorDescription) const
{
    QNDEBUG(QStringLiteral("LocalStorageDatabaseUpgrader::databaseRequiresUpgrade"));

    int version = m_localStorageManager.localStorageVersion(errorDescription);
    if (version <= 0) {
        return false;
    }

    /*
    if (version == 1) {
        return true;
    }

    return false;
    */

    return true;
}

bool LocalStorageDatabaseUpgrader::upgradeDatabase(ErrorString & errorDescription)
{
    QNDEBUG(QStringLiteral("LocalStorageDatabaseUpgrader::upgradeDatabase"));

    int version = m_localStorageManager.localStorageVersion(errorDescription);
    if (version <= 0) {
        return false;
    }

    /*
    if (version == 1) {
        return upgradeDatabaseFromVersion1ToVersion2(errorDescription);
    }
    */

    return true;
}

bool LocalStorageDatabaseUpgrader::upgradeDatabaseFromVersion1ToVersion2(ErrorString & errorDescription)
{
    QNINFO(QStringLiteral("LocalStorageDatabaseUpgrader::upgradeDatabaseFromVersion1ToVersion2"));

    ApplicationSettings databaseUpgadeInfo(m_account, QStringLiteral("LocalStorageDatabaseUpgradeFromVersion1ToVersion2"));

    ErrorString errorPrefix(QT_TR_NOOP("failed to upgrade local storage from version 1 to version 2"));
    errorDescription.clear();
    double lastProgress = 0.0;

    QStringList resourceLocalUids;
    bool allResourceDataCopiedFromTablesToFiles = databaseUpgadeInfo.value(UPGRADE_1_TO_2_ALL_RESOURCE_DATA_COPIED_FROM_TABLE_TO_FILES_KEY).toBool();
    if (!allResourceDataCopiedFromTablesToFiles)
    {
        // Part 1: extract the list of resource local uids from the local storage database
        resourceLocalUids = listResourceLocalUidsForDatabaseUpgradeFromVersion1ToVersion2(errorDescription);
        if (resourceLocalUids.isEmpty() && !errorDescription.isEmpty()) {
            return false;
        }

        lastProgress = 0.05;
        Q_EMIT upgradeProgress(lastProgress);

        // Part 2: ensure the directories for resources data body and recognition data body exist, create them if necessary
        if (!ensureExistenceOfResouceDataDirsForDatabaseUpgradeFromVersion1ToVersion2(errorDescription)) {
            return false;
        }

        // Part 3: copy the data for each resource local uid into the local file

        // TODO: read the array of resource local uids which data has already been copied to plain files + the same for alternate data
        // TODO: copy both data columns of remaining resources to plain files, each time updating the resource local uid in persistent databaseUpgradeInfo

        int numResources = resourceLocalUids.size();
        for(auto it = resourceLocalUids.constBegin(), end = resourceLocalUids.constEnd(); it != end; ++it)
        {
            QSqlQuery query(m_sqlDatabase);
            bool res = query.exec(QString::fromUtf8("SELECT dataBody, alternateDataBody FROM Resources WHERE resourceLocalUid='%1'").arg(*it));
            DATABASE_CHECK_AND_SET_ERROR()

            // TODO: continue from here
        }
    }

    return true;
}

QStringList LocalStorageDatabaseUpgrader::listResourceLocalUidsForDatabaseUpgradeFromVersion1ToVersion2(ErrorString & errorDescription)
{
    QSqlQuery query(m_sqlDatabase);
    bool res = query.exec(QStringLiteral("SELECT COUNT(resourceLocalUid) FROM Resources"));
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

bool LocalStorageDatabaseUpgrader::ensureExistenceOfResouceDataDirsForDatabaseUpgradeFromVersion1ToVersion2(ErrorString & errorDescription)
{
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

} // namespace quentier
