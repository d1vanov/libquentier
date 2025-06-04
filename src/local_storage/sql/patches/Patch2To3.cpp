/*
 * Copyright 2021-2025 Dmitry Ivanov
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
#include <quentier/threading/Future.h>
#include <quentier/utility/ApplicationSettings.h>
#include <quentier/utility/FileSystem.h>
#include <quentier/utility/StandardPaths.h>
#include <quentier/utility/UidGenerator.h>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include <quentier/threading/Qt5Promise.h>
#endif

#include <qevercloud/utility/ToRange.h>

#include <QSqlQuery>
#include <QSqlRecord>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <utility>

namespace quentier::local_storage::sql {

Patch2To3::Patch2To3(
    Account account, ConnectionPoolPtr connectionPool,
    threading::QThreadPtr thread) :
    PatchBase(
        std::move(connectionPool), std::move(thread),
        accountPersistentStoragePath(account),
        accountPersistentStoragePath(account) +
            QStringLiteral("/backup_upgrade_2_to_3_") +
            QDateTime::currentDateTime().toString(Qt::ISODate)),
    m_account{std::move(account)}
{
    if (Q_UNLIKELY(m_account.isEmpty())) {
        throw InvalidArgument{
            ErrorString{QStringLiteral("Patch2To3 ctor: account is empty")}};
    }
}

QString Patch2To3::patchShortDescription() const
{
    return tr(
        "Proper support for transactional updates of resource data files and "
        "fixes for possibly missing related item guid fields for tags, notes "
        "and resources (attachments)");
}

QString Patch2To3::patchLongDescription() const
{
    QString result;
    QTextStream strm{&result};

    strm << tr("This patch performs two distinct changes");
    strm << ":\n";

    // First part of patch description

    strm << "1. ";

    strm << tr(
        "This patch updates several fields in notes, tags and resources tables "
        "which might be missing. These fields refer to Evernote assigned ids "
        "for related items i.e. notebook guid field stored in notes table, "
        "tag parent guid field, note guid field stored in resources table. "
        "In previous version of the app these fields might not have been "
        "updated properly so this patch would ensure their consistency");
    strm << "\n\n";

    // Second part of patch description

    strm << "2. ";
    strm << tr(
        "This patch slightly changes the placement of attachment data files "
        "within the local storage directory: it adds one more intermediate dir "
        "which has the meaning of unique version id of the attachment file.");

    strm << "\n";

    strm << tr(
        "Prior to this patch resource data files were stored according to the "
        "following scheme:");

    strm << "\n";
    strm << "Resources/data/<note local id>/<resource local id>.dat";
    strm << "\n";

    strm << tr(
        "After this patch there would be one additional element in the path");
    strm << ":\n";

    strm << "Resources/data/<note local id>/<version id>/"
         << "<resource local id>.dat\n";

    strm << tr(
        "This change is required in order to implement full support for "
        "transactional updates and removals of resource data files. Without "
        "this change interruptions of local storage operations (such as "
        "application crashes, computer switching off due to power failure etc."
        ") could leave it in inconsistent state.");
    strm << "\n\n";

    // Final note

    strm << tr(
        "The first part of the patch might take a while as it would need to "
        "scan through notes, resources and tags tables, detect missing fields "
        "and fill them. The time it would take depends on the amount of stored "
        "data in the account");
    strm << "\n";
    strm << tr(
        "The second part of the patch should not take long to apply as it just "
        "creates a couple more helper tables in the database and creates "
        "subdirs for existing resource data files");

    strm.flush();
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

    utility::ApplicationSettings databaseUpgradeInfo{
        m_account,
        QStringLiteral("LocalStorageDatabaseUpgradeFromVersion2ToVersion3")};

    if (!fixMissingGuidFields(databaseUpgradeInfo, promise, errorDescription)) {
        return false;
    }

    if (!updateResourcesStorage(databaseUpgradeInfo, promise, errorDescription))
    {
        return false;
    }

    return updateAuxiliaryTableVersion(errorDescription);
}

bool Patch2To3::fixMissingGuidFields(
    utility::ApplicationSettings & databaseUpgradeInfo,
    QPromise<void> & promise, // for progress updates
    ErrorString & errorDescription)
{
    int lastProgress = 0;

    // 1. Fill notebookGuid in Notes table where it is incorrectly null
    const QString notesTableNotebookGuidsFixedUpKey =
        QStringLiteral("NotesTableNotebookGuidsFixedUp");

    const bool notesTableNotebookGuidsFixedUp =
        databaseUpgradeInfo.value(notesTableNotebookGuidsFixedUpKey).toBool();

    if (!notesTableNotebookGuidsFixedUp) {
        auto database = m_connectionPool->database();
        Transaction transaction{database, Transaction::Type::Exclusive};

        QHash<QString, qevercloud::Guid> notebookGuidsByNotebookLocalIds;
        {
            QSqlQuery query{database};
            bool res = query.exec(
                QStringLiteral("SELECT localUid, guid FROM Notebooks WHERE "
                               "updateSequenceNumber IS NOT NULL AND "
                               "localUid IN "
                               "(SELECT DISTINCT notebookLocalUid FROM Notes "
                               "WHERE notebookGuid IS NULL)"));

            ENSURE_DB_REQUEST_RETURN(
                res, query, "local_storage::sql::patches::2_to_3",
                QStringLiteral("Cannot select notebook local ids and guids "
                               "from Notes table"),
                false);

            notebookGuidsByNotebookLocalIds.reserve(query.size());
            while (query.next()) {
                const QString notebookLocalId = query.value(0).toString();
                if (Q_UNLIKELY(notebookLocalId.isEmpty())) {
                    QNWARNING(
                        "local_storage::sql::patches::2_to_3",
                        "Encountered empty notebook local id on attempt "
                        "to list notebook local ids and guids where guids "
                        "are missing in Notes table");
                    continue;
                }

                qevercloud::Guid notebookGuid = query.value(1).toString();
                if (Q_UNLIKELY(notebookGuid.isEmpty())) {
                    QNWARNING(
                        "local_storage::sql::patches::2_to_3",
                        "Encountered empty notebook guid on attempt to list "
                        "notebook local ids and guids where guids are missing "
                        "in Notes table");
                    continue;
                }

                notebookGuidsByNotebookLocalIds[notebookLocalId] =
                    std::move(notebookGuid);
            }
        }

        for (const auto it: qevercloud::toRange(
                 std::as_const(notebookGuidsByNotebookLocalIds)))
        {
            const auto & notebookLocalId = it.key();
            const auto & notebookGuid = it.value();

            QSqlQuery query{database};
            bool res = query.prepare(
                QStringLiteral("UPDATE Notes SET notebookGuid = :notebookGuid "
                               "WHERE notebookLocalUid = :notebookLocalUid"));

            ENSURE_DB_REQUEST_RETURN(
                res, query, "local_storage::sql::patches::2_to_3",
                QStringLiteral("Cannot prepare query to update notebookGuid "
                               "in Notes table"),
                false);

            query.bindValue(QStringLiteral(":notebookGuid"), notebookGuid);
            query.bindValue(
                QStringLiteral(":notebookLocalUid"), notebookLocalId);

            res = query.exec();
            ENSURE_DB_REQUEST_RETURN(
                res, query, "local_storage::sql::patches::2_to_3",
                QStringLiteral("Cannot update notebookGuid in Notes table"),
                false);
        }

        bool res = transaction.commit();
        ENSURE_DB_REQUEST_RETURN(
            res, QSqlQuery{}, "local_storage::sql::tables_initializer",
            QStringLiteral(
                "Cannot update notebookGuid in Notes table: failed to "
                "commit transaction"),
            false);

        databaseUpgradeInfo.setValue(notesTableNotebookGuidsFixedUpKey, true);
        databaseUpgradeInfo.sync();
    }

    lastProgress = 15;
    promise.setProgressValue(lastProgress);

    // 2. Fill parentGuid in Tags table where it is incorrectly null
    const QString tagsTableParentGuidsFixedUpKey =
        QStringLiteral("TagsTableParentGuidsFixedUp");

    const bool tagsTableParentGuidsFixedUp =
        databaseUpgradeInfo.value(tagsTableParentGuidsFixedUpKey).toBool();

    if (!tagsTableParentGuidsFixedUp) {
        auto database = m_connectionPool->database();
        Transaction transaction{database, Transaction::Type::Exclusive};

        QHash<QString, qevercloud::Guid> tagGuidsByLocalId;
        {
            QSqlQuery query{database};
            bool res = query.exec(QStringLiteral(
                "SELECT localUid, guid FROM Tags WHERE localUid IN "
                "(SELECT DISTINCT parentLocalUid FROM Tags "
                "WHERE updateSequenceNumber IS NOT NULL "
                "AND parentGuid IS NULL)"));

            ENSURE_DB_REQUEST_RETURN(
                res, query, "local_storage::sql::patches::2_to_3",
                QStringLiteral("Cannot select tag local ids and guids from "
                               "Tags table"),
                false);

            tagGuidsByLocalId.reserve(query.size());
            while (query.next()) {
                const QString tagLocalId = query.value(0).toString();
                if (Q_UNLIKELY(tagLocalId.isEmpty())) {
                    QNWARNING(
                        "local_storage::sql::patches::2_to_3",
                        "Encountered empty tag local id on attempt to list tag "
                        "local ids and guids where parent guids are missing in "
                        "Tags table");
                    continue;
                }

                qevercloud::Guid tagGuid = query.value(1).toString();
                if (Q_UNLIKELY(tagGuid.isEmpty())) {
                    QNWARNING(
                        "local_storage::sql::patches::2_to_3",
                        "Encountered empty tag guid on attempt to list tag "
                        "local ids and guids where parent guids are missing in "
                        "Tags table");
                    continue;
                }

                tagGuidsByLocalId[tagLocalId] = std::move(tagGuid);
            }
        }

        for (const auto it:
             qevercloud::toRange(std::as_const(tagGuidsByLocalId)))
        {
            const auto & tagLocalId = it.key();
            const auto & tagGuid = it.value();

            QSqlQuery query{database};
            bool res = query.prepare(
                "UPDATE Tags SET parentGuid = :parentGuid "
                "WHERE parentLocalUid = :parentLocalUid");

            ENSURE_DB_REQUEST_RETURN(
                res, query, "local_storage::sql::patches::2_to_3",
                QStringLiteral("Cannot prepare query to update parentGuid "
                               "in Tags table"),
                false);

            query.bindValue(QStringLiteral(":parentGuid"), tagGuid);
            query.bindValue(QStringLiteral(":parentLocalUid"), tagLocalId);

            res = query.exec();
            ENSURE_DB_REQUEST_RETURN(
                res, query, "local_storage::sql::patches::2_to_3",
                QStringLiteral("Cannot update tagGuid in Tags table"), false);
        }

        bool res = transaction.commit();
        ENSURE_DB_REQUEST_RETURN(
            res, QSqlQuery{}, "local_storage::sql::tables_initializer",
            QStringLiteral(
                "Cannot update tagGuid in Tags table: failed to commit "
                "transaction"),
            false);

        databaseUpgradeInfo.setValue(tagsTableParentGuidsFixedUpKey, true);
        databaseUpgradeInfo.sync();
    }

    lastProgress = 35;
    promise.setProgressValue(lastProgress);

    // 3. Fill noteGuid in Resources table where it is incorrectly null
    const QString resourcesTableNoteGuidsFixedUpKey =
        QStringLiteral("ResourcesTableTagGuidsFixedUp");

    const bool resourcesTableNoteGuidsFixedUp =
        databaseUpgradeInfo.value(resourcesTableNoteGuidsFixedUpKey).toBool();

    if (!resourcesTableNoteGuidsFixedUp) {
        auto database = m_connectionPool->database();
        Transaction transaction{database, Transaction::Type::Exclusive};

        QHash<QString, qevercloud::Guid> noteGuidsByNoteLocalIds;
        {
            QSqlQuery query{database};
            bool res = query.exec(
                QStringLiteral("SELECT localUid, guid FROM Notes WHERE "
                               "updateSequenceNumber IS NOT NULL AND "
                               "localUid IN "
                               "(SELECT DISTINCT noteLocalUid FROM Resources "
                               "WHERE noteGuid IS NULL)"));

            ENSURE_DB_REQUEST_RETURN(
                res, query, "local_storage::sql::patches::2_to_3",
                QStringLiteral("Cannot select note local ids and guids "
                               "from Resources table"),
                false);

            noteGuidsByNoteLocalIds.reserve(query.size());
            while (query.next()) {
                const QString noteLocalId = query.value(0).toString();
                if (Q_UNLIKELY(noteLocalId.isEmpty())) {
                    QNWARNING(
                        "local_storage::sql::patches::2_to_3",
                        "Encountered empty note local id on attempt "
                        "to list note local ids and guids where guids "
                        "are missing in Resources table");
                    continue;
                }

                qevercloud::Guid noteGuid = query.value(1).toString();
                if (Q_UNLIKELY(noteGuid.isEmpty())) {
                    QNWARNING(
                        "local_storage::sql::patches::2_to_3",
                        "Encountered empty note guid on attempt "
                        "to list note local ids and guids where guids "
                        "are missing in Resources table");
                    continue;
                }

                noteGuidsByNoteLocalIds[noteLocalId] = std::move(noteGuid);
            }
        }

        for (const auto it:
             qevercloud::toRange(std::as_const(noteGuidsByNoteLocalIds)))
        {
            const auto & noteLocalId = it.key();
            const auto & noteGuid = it.value();

            QSqlQuery query{database};
            bool res = query.prepare(
                QStringLiteral("UPDATE Resources SET noteGuid = :noteGuid "
                               "WHERE noteLocalUid = :noteLocalUid"));

            ENSURE_DB_REQUEST_RETURN(
                res, query, "local_storage::sql::patches::2_to_3",
                QStringLiteral("Cannot prepare query to update noteGuid "
                               "in Resources table"),
                false);

            query.bindValue(QStringLiteral(":noteGuid"), noteGuid);
            query.bindValue(QStringLiteral(":noteLocalUid"), noteLocalId);

            res = query.exec();
            ENSURE_DB_REQUEST_RETURN(
                res, query, "local_storage::sql::patches::2_to_3",
                QStringLiteral("Cannot update noteGuid in Resources table"),
                false);
        }

        bool res = transaction.commit();
        ENSURE_DB_REQUEST_RETURN(
            res, QSqlQuery{}, "local_storage::sql::tables_initializer",
            QStringLiteral(
                "Cannot update noteGuid in Resources table: failed to "
                "commit transaction"),
            false);

        databaseUpgradeInfo.setValue(resourcesTableNoteGuidsFixedUpKey, true);
        databaseUpgradeInfo.sync();
    }

    lastProgress = 50;
    promise.setProgressValue(lastProgress);

    return true;
}

bool Patch2To3::updateResourcesStorage(
    utility::ApplicationSettings & databaseUpgradeInfo,
    QPromise<void> & promise, // for progress updates
    ErrorString & errorDescription)
{
    errorDescription.clear();

    const QString resourceBodyVersionIdTablesCreatedKey =
        QStringLiteral("ResourceBodyVersionIdTablesCreated");

    int lastProgress = 50;
    const bool resourceBodyVersionIdTablesCreated =
        databaseUpgradeInfo.value(resourceBodyVersionIdTablesCreatedKey)
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
            QStringLiteral("Cannot create ResourceDataBodyVersionIds table "
                           "in the local storage database"),
            false);

        res = query.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS ResourceAlternateDataBodyVersionIds("
            "  resourceLocalUid         TEXT PRIMARY KEY NOT NULL UNIQUE, "
            "  versionId                TEXT NOT NULL)"));

        ENSURE_DB_REQUEST_RETURN(
            res, query, "local_storage::sql::tables_initializer",
            QStringLiteral(
                "Cannot create ResourceAlternateDataBodyVersionIds table "
                "in the local storage database"),
            false);

        res = transaction.commit();
        ENSURE_DB_REQUEST_RETURN(
            res, query, "local_storage::sql::tables_initializer",
            QStringLiteral(
                "Cannot create tables for resource data and alternate data "
                "body version ids in the local storage database: failed to "
                "commit transaction"),
            false);

        databaseUpgradeInfo.setValue(
            resourceBodyVersionIdTablesCreatedKey, true);

        databaseUpgradeInfo.sync();

        QNINFO(
            "local_storage::sql::patches::2_to_3",
            "Patch2To3: created tables for resource body version ids tracking "
            "in the local storage database");
    }

    lastProgress = 55;
    promise.setProgressValue(lastProgress);

    const QString committedResourceBodyVersionIdsToDatabaseKey =
        QStringLiteral("ResourceBodyVersionIdsCommittedToDatabase");

    const bool committedResourceBodyVersionIdsToDatabase =
        databaseUpgradeInfo.value(committedResourceBodyVersionIdsToDatabaseKey)
            .toBool();

    QHash<QString, ResourceVersionIds> resourceVersionIds;
    if (!committedResourceBodyVersionIdsToDatabase) {
        resourceVersionIds = generateVersionIds();
        if (!putVersionIdsToDatabase(resourceVersionIds, errorDescription)) {
            return false;
        }

        databaseUpgradeInfo.setValue(
            committedResourceBodyVersionIdsToDatabaseKey, true);

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

    lastProgress = 65;
    promise.setProgressValue(lastProgress);

    const QString movedResourceBodyFilesKey =
        QStringLiteral("ResourceBodyFilesMovedToVersionIdFolders");

    const bool movedResourceBodyFilesToVersionFolders =
        databaseUpgradeInfo.value(movedResourceBodyFilesKey).toBool();

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
                    resourceBodyFileInfo.dir().absoluteFilePath(resourceLocalId)};

                if (!resourceLocalIdDir.exists() &&
                    !resourceLocalIdDir.mkpath(
                        resourceLocalIdDir.absolutePath()))
                {
                    errorDescription.setBase(QStringLiteral(
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
                    resourceLocalIdDir.absoluteFilePath(versionId) +
                    QStringLiteral(".dat"));
                if (!res) {
                    errorDescription.setBase(QStringLiteral(
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

            for (const auto & noteLocalIdSubdirInfo:
                 std::as_const(resourceDataBodiesDirSubdirs))
            {
                QDir noteLocalIdSubdir{
                    noteLocalIdSubdirInfo.absoluteFilePath()};
                const auto resourceFiles =
                    noteLocalIdSubdir.entryInfoList(QDir::Files);

                for (const auto & resourceDataBodyFile:
                     std::as_const(resourceFiles))
                {
                    if (!moveResourceBodyFile(
                            resourceDataBodyFile, ResourceBodyFileKind::Data))
                    {
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

            for (const auto & noteLocalIdSubdirInfo:
                 std::as_const(resourceAlternateDataBodiesDirSubdirs))
            {
                QDir noteLocalIdSubdir{
                    noteLocalIdSubdirInfo.absoluteFilePath()};
                const auto resourceFiles =
                    noteLocalIdSubdir.entryInfoList(QDir::Files);

                for (const auto & resourceDataBodyFile:
                     std::as_const(resourceFiles))
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

        databaseUpgradeInfo.setValue(movedResourceBodyFilesKey, true);
        databaseUpgradeInfo.sync();

        QNINFO(
            "local_storage::sql::patches::2_to_3",
            "Patch2To3: moved resource body files to version id dirs");
    }

    lastProgress = 95;
    promise.setProgressValue(lastProgress);

    return true;
}

bool Patch2To3::updateAuxiliaryTableVersion(ErrorString & errorDescription)
{
    auto database = m_connectionPool->database();
    QSqlQuery query{database};
    const bool res = query.exec(
        QStringLiteral("INSERT OR REPLACE INTO Auxiliary (version) VALUES(3)"));

    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::patches::2_to_3",
        QStringLiteral(
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
        localStorageDirPath + QStringLiteral("/Resources/data/")};

    const auto resourceDataBodiesDirSubdirs =
        resourceDataBodiesDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);

    for (const auto & noteLocalIdSubdirInfo:
         std::as_const(resourceDataBodiesDirSubdirs))
    {
        QDir noteLocalIdSubdir{noteLocalIdSubdirInfo.absoluteFilePath()};
        const auto resourceFiles = noteLocalIdSubdir.entryInfoList(QDir::Files);
        for (const auto & resourceDataBodyFile: std::as_const(resourceFiles)) {
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
        resourceAlternateDataBodiesDir.entryInfoList(
            QDir::Dirs | QDir::NoDotAndDotDot);

    for (const auto & noteLocalIdSubdirInfo:
         std::as_const(resourceAlternateDataBodiesDirSubdirs))
    {
        QDir noteLocalIdSubdir{noteLocalIdSubdirInfo.absoluteFilePath()};
        const auto resourceFiles = noteLocalIdSubdir.entryInfoList(QDir::Files);
        for (const auto & resourceAlternateDataBodyFile:
             std::as_const(resourceFiles))
        {
            if (resourceAlternateDataBodyFile.completeSuffix() !=
                QStringLiteral("dat"))
            {
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
            QStringLiteral(
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
            QStringLiteral(
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

    for (const auto it: qevercloud::toRange(std::as_const(resourceVersionIds)))
    {
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
                QStringLiteral(
                    "Cannot put resource body version id to the local "
                    "storage database: failed to prepare query"),
                false);

            query.bindValue(
                QStringLiteral(":resourceLocalUid"), resourceLocalId);

            query.bindValue(
                QStringLiteral(":versionId"),
                resourceBodyVersionIds.m_dataBodyVersionId);

            res = query.exec();
            ENSURE_DB_REQUEST_RETURN(
                res, query, "local_storage::sql::patches::2_to_3",
                QStringLiteral(
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
                res, query, "local_storage::sql::patches::2_to_3",
                QStringLiteral(
                    "Cannot put resource alternate body version id to "
                    "the local storage database: failed to prepare query"),
                false);

            query.bindValue(
                QStringLiteral(":resourceLocalUid"), resourceLocalId);

            query.bindValue(
                QStringLiteral(":versionId"),
                resourceBodyVersionIds.m_alternateDataBodyVersionId);

            res = query.exec();
            ENSURE_DB_REQUEST_RETURN(
                res, query, "local_storage::sql::patches::2_to_3",
                QStringLiteral(
                    "Cannot put resource alternate body version id to "
                    "the local storage database"),
                false);
        }
    }

    const bool res = transaction.commit();
    ENSURE_DB_REQUEST_RETURN(
        res, database, "local_storage::sql::patches::2_to_3",
        QStringLiteral(
            "Cannot put resource body version ids to "
            "the local storage database: failed to commit transaction"),
        false);

    return true;
}

} // namespace quentier::local_storage::sql
