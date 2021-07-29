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
#include <QSqlRecord>
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

const QString gUpgrade2To3MovedResourceBodyFilesKey =
    QStringLiteral("ResourceBodyFilesMovedToVersionIdFolders");

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
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "local_storage::sql::patches::2_to_3::Patch2To3",
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

    result +=
        tr("This patch slightly changes the placement of attachment data "
           "files within the local storage directory: it adds one more "
           "intermediate dir which has the meaning of unique version id "
           "of the attachment file.");

    result += QStringLiteral("\n");

    result +=
        tr("Prior to this patch resource data files were stored "
           "according to the following scheme:");

    result += QStringLiteral("\n\n");

    result += QStringLiteral(
        "Resources/data/<note local id>/<resource local id>.dat");

    result += QStringLiteral("\n\n");

    result +=
        tr("After this patch there would be one additional element "
           "in the path:");

    result += QStringLiteral("\n\n");

    result += QStringLiteral(
        "Resources/data/<note local id>/<version id>/<resource local id>.dat");

    result += QStringLiteral("\n\n");

    result +=
        tr("The change is required in order to implement full support "
           "for transactional updates and removals of resource data "
           "files. Without this change interruptions of local storage "
           "operations (such as application crashes, computer switching "
           "off due to power failure etc.) could leave it in "
           "inconsistent state.");

    result += QStringLiteral("\n\n");

    result +=
        tr("The patch should not take long to apply as it just "
           "creates a couple more helper tables in the database and "
           "creates subdirs for existing resource data files");

    return result;
}

bool Patch2To3::backupLocalStorageSync(
    QPromise<void> & promise, ErrorString & errorDescription)
{
    QNDEBUG(
        "local_storage::sql::patches::2_to_3",
        "Patch2To3::backupLocalStorageSync");

    return utils::backupLocalStorageDatabaseFiles(
        m_localStorageDir.absolutePath(), m_backupDir.absolutePath(), promise,
        errorDescription);
}

bool Patch2To3::restoreLocalStorageFromBackupSync(
    QPromise<void> & promise, ErrorString & errorDescription)
{
    QNDEBUG(
        "local_storage::sql::patches::2_to_3",
        "Patch2To3::restoreLocalStorageFromBackupImpl");

    return utils::restoreLocalStorageDatabaseFilesFromBackup(
        m_localStorageDir.absolutePath(), m_backupDir.absolutePath(), promise,
        errorDescription);
}

bool Patch2To3::removeLocalStorageBackupSync(ErrorString & errorDescription)
{
    QNINFO(
        "local_storage::sql::patches::2_to_3",
        "Patch2To3::removeLocalStorageBackupSync");

    return utils::removeLocalStorageDatabaseFilesBackup(
        m_backupDir.absolutePath(), errorDescription);
}

bool Patch2To3::applySync(
    QPromise<void> & promise, ErrorString & errorDescription)
{
    QNDEBUG("local_storage::sql::patches::2_to_3", "Patch2To3::applySync");

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
            res, query, "local_storage::sql::patches::2_to_3",
            QT_TR_NOOP("Cannot create ResourceDataBodyVersionIds table "
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

        QNINFO(
            "local_storage::sql::patches::2_to_3",
            "Patch2To3: created tables for resource body version ids tracking "
            "in the local storage database");
    }

    lastProgress = 5;
    promise.setProgressValue(lastProgress);

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
            gUpgrade2To3CommittedResourceBodyVersionIdsToDatabaseKey, true);

        databaseUpgradeInfo.sync();

        QNINFO(
            "local_storage::sql::patches::2_to_3",
            "Patch2To3: generated version ids for existing resource body files "
            "and saved them in the local storage database");
    }
    else {
        auto versionIds = fetchVersionIdsFromDatabase(errorDescription);
        if (!versionIds) {
            return false;
        }

        resourceVersionIds = std::move(*versionIds);
    }

    lastProgress = 15;
    promise.setProgressValue(lastProgress);

    const bool movedResourceBodyFilesToVersionFolders =
        databaseUpgradeInfo.value(gUpgrade2To3MovedResourceBodyFilesKey)
            .toBool();

    if (!movedResourceBodyFilesToVersionFolders) {
        const QString localStorageDirPath = m_localStorageDir.absolutePath();

        enum class ResourceBodyFileKind
        {
            Data,
            AlternateData
        };

        // clang-format off
        const auto moveResourceBodyFile =
            [&resourceVersionIds, &errorDescription](
                const QFileInfo & resourceBodyFileInfo,
                const ResourceBodyFileKind kind) -> bool
            {
                if (resourceBodyFileInfo.completeSuffix() !=
                    QStringLiteral("dat"))
                {
                    return true;
                }

                // File's base name is resource's local id
                const auto resourceLocalId = resourceBodyFileInfo.baseName();
                auto it = resourceVersionIds.find(resourceLocalId);
                if (Q_UNLIKELY(it == resourceVersionIds.end())) {
                    QNWARNING(
                        "local_storage::sql::patches::2_to_3",
                        "Detected resource body file which has no "
                        "corresponding version id: "
                            << resourceBodyFileInfo.absoluteFilePath());
                    return true;
                }

                const auto & versionId =
                    (kind == ResourceBodyFileKind::AlternateData
                     ? it.value().m_alternateDataBodyVersionId
                     : it.value().m_dataBodyVersionId);

                if (Q_UNLIKELY(versionId.isEmpty())) {
                    QNWARNING(
                        "local_storage::sql::patches::2_to_3",
                        "Detected resource body file which has empty "
                        "corresponding version id: "
                            << resourceBodyFileInfo.absoluteFilePath());
                    return true;
                }

                QDir resourceLocalIdDir{
                    resourceBodyFileInfo.dir().absolutePath() +
                    QStringLiteral("/") + resourceLocalId};

                if (!resourceLocalIdDir.exists() &&
                    !resourceLocalIdDir.mkpath(
                        resourceLocalIdDir.absolutePath()))
                {
                    errorDescription.setBase(QT_TR_NOOP(
                        "Failed to create dir for resource body files"));
                    errorDescription.details() =
                        resourceBodyFileInfo.absoluteFilePath();
                    QNWARNING(
                        "local_storage::sql::patches::2_to_3",
                        errorDescription);
                    return false;
                }

                QFile resourceBodyFile{
                    resourceBodyFileInfo.absoluteFilePath()};
                bool res = resourceBodyFile.rename(
                    resourceLocalIdDir.absoluteFilePath(versionId));
                if (!res) {
                    errorDescription.setBase(QT_TR_NOOP(
                        "Failed to move resource body file"));
                    errorDescription.details() =
                        resourceBodyFileInfo.absoluteFilePath();
                    errorDescription.details() +=
                        QStringLiteral(", version id: ");
                    errorDescription.details() += versionId;
                    QNWARNING(
                        "local_storage::sql::patches::2_to_3",
                        errorDescription);
                    return false;
                }

                return true;
            };
        // clang-format on

        {
            QDir resourceDataBodiesDir{
                localStorageDirPath + QStringLiteral("/Resources/data")};

            const auto resourceDataBodiesDirSubdirs =
                resourceDataBodiesDir.entryInfoList(
                    QDir::Dirs | QDir::NoDotAndDotDot);

            for (const auto & noteLocalIdSubdir:
                 qAsConst(resourceDataBodiesDirSubdirs)) {
                const auto resourceFiles =
                    noteLocalIdSubdir.dir().entryInfoList(QDir::Files);

                for (const auto & resourceDataBodyFile: qAsConst(resourceFiles))
                {
                    if (!moveResourceBodyFile(
                            resourceDataBodyFile, ResourceBodyFileKind::Data)) {
                        return false;
                    }
                }
            }
        }

        {
            QDir resourceAlternateDataBodiesDir{
                localStorageDirPath +
                QStringLiteral("/Resources/alternateData")};

            const auto resourceAlternateDataBodiesDirSubdirs =
                resourceAlternateDataBodiesDir.entryInfoList(
                    QDir::Dirs | QDir::NoDotAndDotDot);

            for (const auto & noteLocalIdSubdir:
                 qAsConst(resourceAlternateDataBodiesDirSubdirs))
            {
                const auto resourceFiles =
                    noteLocalIdSubdir.dir().entryInfoList(QDir::Files);

                for (const auto & resourceDataBodyFile: qAsConst(resourceFiles))
                {
                    if (!moveResourceBodyFile(
                            resourceDataBodyFile,
                            ResourceBodyFileKind::AlternateData))
                    {
                        return false;
                    }
                }
            }
        }

        databaseUpgradeInfo.setValue(
            gUpgrade2To3MovedResourceBodyFilesKey, true);

        databaseUpgradeInfo.sync();

        QNINFO(
            "local_storage::sql::patches::2_to_3",
            "Patch2To3: moved resource body files to version id dirs");
    }

    lastProgress = 95;
    promise.setProgressValue(lastProgress);

    // Update version in the Auxiliary table
    auto database = m_connectionPool->database();
    QSqlQuery query{database};
    const bool res = query.exec(
        QStringLiteral("INSERT OR REPLACE INTO Auxiliary (version) VALUES(3)"));

    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::patches::2_to_3",
        QT_TR_NOOP(
            "failed to execute SQL query increasing local storage version"),
        false);

    QNDEBUG(
        "local_storage::sql::patches::2_to_3",
        "Finished upgrading the local storage from version 2 to version 3");

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
        resourceDataBodiesDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const auto & noteLocalIdSubdir: qAsConst(resourceDataBodiesDirSubdirs))
    {
        const auto resourceFiles =
            noteLocalIdSubdir.dir().entryInfoList(QDir::Files);
        for (const auto & resourceDataBodyFile: qAsConst(resourceFiles)) {
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

        const auto resourceFiles = noteLocalIdSubdir.dir().entryInfoList();
        for (const auto & resourceAlternateDataBodyFile:
             qAsConst(resourceFiles)) {
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

std::optional<QHash<QString, Patch2To3::ResourceVersionIds>>
    Patch2To3::fetchVersionIdsFromDatabase(ErrorString & errorDescription) const
{
    auto database = m_connectionPool->database();
    Transaction transaction{database, Transaction::Type::Selection};

    QHash<QString, ResourceVersionIds> result;

    // clang-format off
    const auto fillValues = [](const QSqlRecord & record)
        -> std::optional<std::pair<QString, QString>>
    {
        const int localIdIndex =
            record.indexOf(QStringLiteral("resourceLocalUid"));
        if (localIdIndex < 0) {
            return std::nullopt;
        }

        const int versionIdIndex =
            record.indexOf(QStringLiteral("versionId"));
        if (versionIdIndex < 0) {
            return std::nullopt;
        }

        return std::make_pair(
            record.value(localIdIndex).toString(),
            record.value(versionIdIndex).toString());
    };
    // clang-format on

    {
        static const QString queryString = QStringLiteral(
            "SELECT resourceLocalUid, versionId FROM "
            "ResourceDataBodyVersionIds");

        QSqlQuery query{database};
        bool res = query.exec(queryString);
        ENSURE_DB_REQUEST_RETURN(
            res, query, "local_storage::sql::patches::2_to_3",
            QT_TR_NOOP(
                "Cannot select resource data body version ids from the local "
                "storage database"),
            std::nullopt);

        while (query.next()) {
            auto values = fillValues(query.record());
            if (!values) {
                continue;
            }

            result[values->first].m_dataBodyVersionId =
                std::move(values->second);
        }
    }

    {
        static const QString queryString = QStringLiteral(
            "SELECT resourceLocalUid, versionId FROM "
            "ResourceAlternateDataBodyVersionIds");

        QSqlQuery query{database};
        bool res = query.exec(queryString);
        ENSURE_DB_REQUEST_RETURN(
            res, query, "local_storage::sql::patches::2_to_3",
            QT_TR_NOOP(
                "Cannot select resource alternate data body version ids from "
                "the local storage database"),
            std::nullopt);

        while (query.next()) {
            auto values = fillValues(query.record());
            if (!values) {
                continue;
            }

            result[values->first].m_alternateDataBodyVersionId =
                std::move(values->second);
        }
    }

    return result;
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
                res, query, "local_storage::sql::patches::2_to_3",
                QT_TR_NOOP("Cannot put resource body version id to the local "
                           "storage database: failed to prepare query"),
                false);

            query.bindValue(
                QStringLiteral(":resouceLocalUid"), resourceLocalId);

            query.bindValue(
                QStringLiteral(":versionId"),
                resourceBodyVersionIds.m_dataBodyVersionId);

            res = query.exec();
            ENSURE_DB_REQUEST_RETURN(
                res, query, "local_storage::sql::patches::2_to_3",
                QT_TR_NOOP("Cannot put resource body version id to the local "
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
                res, query, "local_storage::sql::patches::2_to_3",
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
                res, query, "local_storage::sql::patches::2_to_3",
                QT_TR_NOOP("Cannot put resource alternate body version id to "
                           "the local storage database"),
                false);
        }
    }

    const bool res = transaction.commit();
    ENSURE_DB_REQUEST_RETURN(
        res, database, "local_storage::sql::patches::2_to_3",
        QT_TR_NOOP("Cannot put resource body version ids to "
                   "the local storage database: failed to commit transaction"),
        false);

    return true;
}

} // namespace quentier::local_storage::sql
