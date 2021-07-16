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
#include "PatchUtils.h"

#include "../ConnectionPool.h"
#include "../ErrorHandling.h"
#include "../Transaction.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/RuntimeError.h>
#include <quentier/utility/ApplicationSettings.h>
#include <quentier/utility/FileCopier.h>
#include <quentier/utility/FileSystem.h>
#include <quentier/utility/StandardPaths.h>
#include <quentier/utility/UidGenerator.h>

#include <utility/Threading.h>

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <utility/Qt5Promise.h>
#else
#include <QPromise>
#endif

#include <qevercloud/utility/ToRange.h>

#include <QSqlQuery>
#include <QTextStream>

#include <algorithm>
#include <cmath>

namespace quentier::local_storage::sql {

namespace {

const QString gUpgrade2To3Persistence =
    QStringLiteral("LocalStorageDatabaseUpgradeFromVersion2ToVersion3");

const QString gUpgrade2To3ResourceBodyVersionIdTablesCreatedKey =
    QStringLiteral("ResourceBodyVersionIdTablesCreated");

const QString gUpgrade2To3CommittedResourceBodyVersionIdsToDatabaseKey =
    QStringLiteral("ResourceBodyVersionIdsCommittedToDatabase");

const QString gDbFileName = QStringLiteral("qn.storage.sqlite");

} // namespace

Patch2To3::Patch2To3(
    Account account, ConnectionPoolPtr connectionPool,
    QThreadPtr writerThread) :
    PatchBase(
        std::move(connectionPool), std::move(writerThread),
        accountPersistentStoragePath(account),
        accountPersistentStoragePath(account) +
            QStringLiteral("/backup_upgrade_2_to_3_") +
            QDateTime::currentDateTime().toString(Qt::ISODate)),
    m_account{std::move(account)}
{
    if (Q_UNLIKELY(m_account.isEmpty())) {
        throw InvalidArgument{ErrorString{
            QT_TRANSLATE_NOOP(
                "local_storage::sql::patches::Patch2To3",
                "Patch2To3 ctor: account is empty")}};
    }
}

QString Patch2To3::patchShortDescription() const
{
    return tr(
        "Proper support for transactional updates of resource data files");
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

bool Patch2To3::backupLocalStorageSync(
    QPromise<void> & promise, ErrorString & errorDescription)
{
    QNDEBUG(
        "local_storage::sql::patches",
        "Patch2To3::backupLocalStorageSync");

    return utils::backupLocalStorageDatabaseFiles(
        m_localStorageDir.absolutePath(), m_backupDir.absolutePath(), promise,
        errorDescription);
}

bool Patch2To3::restoreLocalStorageFromBackupSync(
    QPromise<void> & promise, ErrorString & errorDescription)
{
    QNDEBUG(
        "local_storage::sql::patches",
        "Patch2To3::restoreLocalStorageFromBackupImpl");

    return utils::restoreLocalStorageDatabaseFilesFromBackup(
        m_localStorageDir.absolutePath(), m_backupDir.absolutePath(), promise,
        errorDescription);
}

bool Patch2To3::removeLocalStorageBackupSync(
    ErrorString & errorDescription)
{
    QNINFO(
        "local_storage::sql::patches",
        "Patch2To3::removeLocalStorageBackupSync");

    return utils::removeLocalStorageDatabaseFilesBackup(
        m_backupDir.absolutePath(), errorDescription);
}

bool Patch2To3::applySync(
    QPromise<void> & promise, ErrorString & errorDescription)
{
    QNDEBUG(
        "local_storage::sql::patches", "Patch2To3::applySync");

    ApplicationSettings databaseUpgradeInfo{m_account, gUpgrade2To3Persistence};

    ErrorString errorPrefix{
        QT_TR_NOOP("failed to upgrade local storage "
                   "from version 2 to version 3")};

    errorDescription.clear();

    int lastProgress = 0;
    const bool resourceBodyVersionIdTablesCreated =
        databaseUpgradeInfo
            .value(gUpgrade2To3ResourceBodyVersionIdTablesCreatedKey)
            .toBool();

    if (!resourceBodyVersionIdTablesCreated) {
        auto database = m_connectionPool->database();
        Transaction transaction{database, Transaction::Type::Exclusive};

        QSqlQuery query{database};
        bool res = query.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS ResourceDataBodyVersionIds("
            "  resourceLocalUid         TEXT PRIMARY KEY NOT NULL UNIQUE, "
            "  versionId                TEXT NOT NULL)"));

        ENSURE_DB_REQUEST_RETURN(
            res, query, "local_storage::sql::patches",
            QT_TR_NOOP(
                "Cannot create ResourceDataBodyVersionIds table "
                "in the local storage database"),
            false);

        res = query.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS ResourceAlternateDataBodyVersionIds("
            "  resourceLocalUid         TEXT PRIMARY KEY NOT NULL UNIQUE, "
            "  versionId                TEXT NOT NULL)"));

        ENSURE_DB_REQUEST_RETURN(
            res, query, "local_storage::sql::tables_initializer",
            QT_TR_NOOP(
                "Cannot create ResourceAlternateDataBodyVersionIds table "
                "in the local storage database"),
            false);

        res = transaction.commit();
        ENSURE_DB_REQUEST_RETURN(
            res, query, "local_storage::sql::tables_initializer",
            QT_TR_NOOP(
                "Cannot create tables for resource data and alternate data "
                "body version ids in the local storage database: failed to "
                "commit transaction"),
            false);

        databaseUpgradeInfo.setValue(
            gUpgrade2To3ResourceBodyVersionIdTablesCreatedKey, true);

        databaseUpgradeInfo.sync();

        lastProgress = 5;
        promise.setProgressValue(lastProgress);
    }

    const bool committedResourceBodyVersionIdsToDatabase =
        databaseUpgradeInfo
            .value(gUpgrade2To3CommittedResourceBodyVersionIdsToDatabaseKey)
            .toBool();

    QHash<QString, ResourceVersionIds> resourceVersionIds;
    if (!committedResourceBodyVersionIdsToDatabase) {
        resourceVersionIds = generateVersionIds();
        if (!putVersionIdsToDatabase(resourceVersionIds, errorDescription)) {
            return false;
        }

        databaseUpgradeInfo.setValue(
            gUpgrade2To3CommittedResourceBodyVersionIdsToDatabaseKey,
            true);

        databaseUpgradeInfo.sync();
    }
    else {
        // TODO: fill resource version ids from the database
    }

    // TODO: move resource files according to version ids unless already done
    return true;
}

QHash<QString, Patch2To3::ResourceVersionIds> Patch2To3::generateVersionIds()
    const
{
    QHash<QString, Patch2To3::ResourceVersionIds> resourceVersionIds;
    const QString localStorageDirPath = m_localStorageDir.absolutePath();

    QDir resourceDataBodiesDir{
        localStorageDirPath + QStringLiteral("/Resources/data")};

    const auto resourceDataBodiesDirSubdirs =
        resourceDataBodiesDir.entryInfoList();
    for (const auto & noteLocalIdSubdir: qAsConst(resourceDataBodiesDirSubdirs))
    {
        if (!noteLocalIdSubdir.isDir()) {
            continue;
        }

        const auto resourceFiles =
            noteLocalIdSubdir.dir().entryInfoList();
        for (const auto & resourceDataBodyFile: qAsConst(resourceFiles))
        {
            if (!resourceDataBodyFile.isFile()) {
                continue;
            }

            if (resourceDataBodyFile.completeSuffix() != QStringLiteral("dat"))
            {
                continue;
            }

            // File's base name is resource's local id
            resourceVersionIds[resourceDataBodyFile.baseName()]
                .m_dataBodyVersionId = UidGenerator::Generate();
        }
    }

    QDir resourceAlternateDataBodiesDir{
        localStorageDirPath + QStringLiteral("/Resources/alternateData")};

    const auto resourceAlternateDataBodiesDirSubdirs =
        resourceAlternateDataBodiesDir.entryInfoList();
    for (const auto & noteLocalIdSubdir:
         qAsConst(resourceAlternateDataBodiesDirSubdirs))
    {
        if (!noteLocalIdSubdir.isDir()) {
            continue;
        }

        const auto resourceFiles =
            noteLocalIdSubdir.dir().entryInfoList();
        for (const auto & resourceAlternateDataBodyFile: qAsConst(resourceFiles))
        {
            if (!resourceAlternateDataBodyFile.isFile()) {
                continue;
            }

            if (resourceAlternateDataBodyFile.completeSuffix() !=
                QStringLiteral("dat")) {
                continue;
            }

            // File's base name is resource's local id
            resourceVersionIds[resourceAlternateDataBodyFile.baseName()]
                .m_alternateDataBodyVersionId = UidGenerator::Generate();
        }
    }

    return resourceVersionIds;
}

bool Patch2To3::putVersionIdsToDatabase(
    const QHash<QString, ResourceVersionIds> & resourceVersionIds,
    ErrorString & errorDescription)
{
    auto database = m_connectionPool->database();
    Transaction transaction{database, Transaction::Type::Exclusive};

    for (const auto it: qevercloud::toRange(qAsConst(resourceVersionIds))) {
        const auto & resourceLocalId = it.key();
        const auto & resourceBodyVersionIds = it.value();

        if (!resourceBodyVersionIds.m_dataBodyVersionId.isEmpty()) {
            QSqlQuery query{database};
            static const QString queryString = QStringLiteral(
                "INSERT OR REPLACE INTO ResourceDataBodyVersionIds("
                "resourceLocalUid, versionId) VALUES(:resourceLocalUid, "
                ":versionId)");

            bool res = query.prepare(queryString);
            ENSURE_DB_REQUEST_RETURN(
                res, query, "local_storage::sql::patches",
                QT_TR_NOOP(
                    "Cannot put resource body version id to the local "
                    "storage database: failed to prepare query"),
                false);

            query.bindValue(
                QStringLiteral(":resouceLocalUid"), resourceLocalId);

            query.bindValue(
                QStringLiteral(":versionId"),
                resourceBodyVersionIds.m_dataBodyVersionId);

            res = query.exec();
            ENSURE_DB_REQUEST_RETURN(
                res, query, "local_storage::sql::patches",
                QT_TR_NOOP(
                    "Cannot put resource body version id to the local "
                    "storage database"),
                false);
        }

        if (!resourceBodyVersionIds.m_alternateDataBodyVersionId.isEmpty()) {
            QSqlQuery query{database};
            static const QString queryString = QStringLiteral(
                "INSERT OR REPLACE INTO ResourceAlternateDataBodyVersionIds("
                "resourceLocalUid, versionId) VALUES(:resourceLocalUid, "
                ":versionId)");

            bool res = query.prepare(queryString);
            ENSURE_DB_REQUEST_RETURN(
                res, query, "local_storage::sql::patches",
                QT_TR_NOOP(
                    "Cannot put resource alternate body version id to "
                    "the local storage database: failed to prepare query"),
                false);

            query.bindValue(
                QStringLiteral(":resouceLocalUid"), resourceLocalId);

            query.bindValue(
                QStringLiteral(":versionId"),
                resourceBodyVersionIds.m_alternateDataBodyVersionId);

            res = query.exec();
            ENSURE_DB_REQUEST_RETURN(
                res, query, "local_storage::sql::patches",
                QT_TR_NOOP(
                    "Cannot put resource alternate body version id to "
                    "the local storage database"),
                false);
        }
    }

    const bool res = transaction.commit();
    ENSURE_DB_REQUEST_RETURN(
        res, database, "local_storage::sql::patches",
        QT_TR_NOOP(
            "Cannot put resource body version ids to "
            "the local storage database: failed to commit transaction"),
        false);

    return true;
}

} // namespace quentier::local_storage::sql
