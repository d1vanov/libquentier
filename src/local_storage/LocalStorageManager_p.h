/*
 * Copyright 2016-2021 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_LOCAL_STORAGE_LOCAL_STORAGE_MANAGER_PRIVATE_H
#define LIB_QUENTIER_LOCAL_STORAGE_LOCAL_STORAGE_MANAGER_PRIVATE_H

#include <quentier/local_storage/Fwd.h>
#include <quentier/local_storage/Lists.h>
#include <quentier/local_storage/LocalStorageManager.h>
#include <quentier/utility/StringUtils.h>
#include <quentier/utility/SuppressWarnings.h>

#include <qevercloud/generated/types/LinkedNotebook.h>
#include <qevercloud/generated/types/Note.h>
#include <qevercloud/generated/types/Notebook.h>
#include <qevercloud/generated/types/Resource.h>
#include <qevercloud/generated/types/SavedSearch.h>
#include <qevercloud/generated/types/SharedNotebook.h>
#include <qevercloud/generated/types/Tag.h>
#include <qevercloud/generated/types/User.h>

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

// Prevent boost::interprocess from automatic linkage to boost::datetime
#define BOOST_DATE_TIME_NO_LIB

SAVE_WARNINGS

// clang-format off
GCC_SUPPRESS_WARNING(-Wtype-limits)
// clang-format on

#include <boost/interprocess/sync/file_lock.hpp>

RESTORE_WARNINGS

#include <cstddef>

namespace quentier {

class LocalStoragePatchManager;
class NoteSearchQuery;

class Q_DECL_HIDDEN LocalStorageManagerPrivate final : public QObject
{
    Q_OBJECT
public:
    LocalStorageManagerPrivate(
        const Account & account,
        LocalStorageManager::StartupOptions options,
        QObject * parent = nullptr);

    ~LocalStorageManagerPrivate() noexcept override;

Q_SIGNALS:
    void upgradeProgress(double progress);

public:
    void switchUser(
        const Account & account, LocalStorageManager::StartupOptions options);

    [[nodiscard]] bool isLocalStorageVersionTooHigh(
        ErrorString & errorDescription);

    [[nodiscard]] bool localStorageRequiresUpgrade(
        ErrorString & errorDescription);

    [[nodiscard]] QList<ILocalStoragePatchPtr> requiredLocalStoragePatches();

    [[nodiscard]] qint32 localStorageVersion(ErrorString & errorDescription);
    [[nodiscard]] qint32 highestSupportedLocalStorageVersion() const;

    [[nodiscard]] int userCount(ErrorString & errorDescription) const;

    [[nodiscard]] bool addUser(
        const qevercloud::User & user, ErrorString & errorDescription);

    [[nodiscard]] bool updateUser(
        const qevercloud::User & user, ErrorString & errorDescription);

    [[nodiscard]] bool findUser(
        qevercloud::User & user, ErrorString & errorDescription) const;

    [[nodiscard]] bool deleteUser(
        const qevercloud::User & user, ErrorString & errorDescription);

    [[nodiscard]] bool expungeUser(
        const qevercloud::User & user, ErrorString & errorDescription);

    [[nodiscard]] int notebookCount(ErrorString & errorDescription) const;

    [[nodiscard]] bool addNotebook(
        qevercloud::Notebook & notebook, ErrorString & errorDescription);

    [[nodiscard]] bool updateNotebook(
        qevercloud::Notebook & notebook, ErrorString & errorDescription);

    [[nodiscard]] bool findNotebook(
        qevercloud::Notebook & notebook, ErrorString & errorDescription) const;

    [[nodiscard]] bool findDefaultNotebook(
        qevercloud::Notebook & notebook, ErrorString & errorDescription) const;

    [[nodiscard]] bool findLastUsedNotebook(
        qevercloud::Notebook & notebook, ErrorString & errorDescription) const;

    [[nodiscard]] bool findDefaultOrLastUsedNotebook(
        qevercloud::Notebook & notebook, ErrorString & errorDescription) const;

    [[nodiscard]] QList<qevercloud::Notebook> listAllNotebooks(
        ErrorString & errorDescription, std::size_t limit, std::size_t offset,
        const LocalStorageManager::ListNotebooksOrder & order,
        const LocalStorageManager::OrderDirection & orderDirection,
        std::optional<QString> linkedNotebookGuid) const;

    [[nodiscard]] QList<qevercloud::Notebook> listNotebooks(
        LocalStorageManager::ListObjectsOptions flag,
        ErrorString & errorDescription, std::size_t limit, std::size_t offset,
        const LocalStorageManager::ListNotebooksOrder & order,
        const LocalStorageManager::OrderDirection & orderDirection,
        std::optional<QString> linkedNotebookGuid) const;

    [[nodiscard]] QList<qevercloud::SharedNotebook> listAllSharedNotebooks(
        ErrorString & errorDescription) const;

    [[nodiscard]] QList<qevercloud::SharedNotebook>
    listSharedNotebooksPerNotebookGuid(
        const QString & notebookGuid, ErrorString & errorDescription) const;

    [[nodiscard]] QList<qevercloud::SharedNote> listSharedNotesPerNoteGuid(
        const QString & noteGuid, ErrorString & errorDescription) const;

    [[nodiscard]] QList<qevercloud::Resource> listResourcesPerNoteLocalId(
        const QString & noteLocalId, bool withBinaryData,
        ErrorString & errorDescription) const;

    [[nodiscard]] bool expungeNotebook(
        qevercloud::Notebook & notebook, ErrorString & errorDescription);

    [[nodiscard]] int linkedNotebookCount(ErrorString & errorDescription) const;

    [[nodiscard]] bool addLinkedNotebook(
        const qevercloud::LinkedNotebook & linkedNotebook,
        ErrorString & errorDescription);

    [[nodiscard]] bool updateLinkedNotebook(
        const qevercloud::LinkedNotebook & linkedNotebook,
        ErrorString & errorDescription);

    [[nodiscard]] bool findLinkedNotebook(
        qevercloud::LinkedNotebook & linkedNotebook,
        ErrorString & errorDescription) const;

    [[nodiscard]] QList<qevercloud::LinkedNotebook> listAllLinkedNotebooks(
        ErrorString & errorDescription, std::size_t limit, std::size_t offset,
        LocalStorageManager::ListLinkedNotebooksOrder order,
        const LocalStorageManager::OrderDirection & orderDirection) const;

    [[nodiscard]] QList<qevercloud::LinkedNotebook> listLinkedNotebooks(
        LocalStorageManager::ListObjectsOptions flag,
        ErrorString & errorDescription, std::size_t limit, std::size_t offset,
        const LocalStorageManager::ListLinkedNotebooksOrder & order,
        const LocalStorageManager::OrderDirection & orderDirection) const;

    [[nodiscard]] bool expungeLinkedNotebook(
        const qevercloud::LinkedNotebook & linkedNotebook,
        ErrorString & errorDescription);

    [[nodiscard]] int noteCount(
        ErrorString & errorDescription,
        LocalStorageManager::NoteCountOptions options) const;

    [[nodiscard]] int noteCountPerNotebook(
        const qevercloud::Notebook & notebook, ErrorString & errorDescription,
        LocalStorageManager::NoteCountOptions options) const;

    [[nodiscard]] int noteCountPerTag(
        const qevercloud::Tag & tag, ErrorString & errorDescription,
        LocalStorageManager::NoteCountOptions options) const;

    [[nodiscard]] bool noteCountsPerAllTags(
        QHash<QString, int> & noteCountsPerTagLocalId,
        ErrorString & errorDescription,
        LocalStorageManager::NoteCountOptions options) const;

    [[nodiscard]] int noteCountPerNotebooksAndTags(
        const QStringList & notebookLocalIds, const QStringList & tagLocalIds,
        ErrorString & errorDescription,
        LocalStorageManager::NoteCountOptions options) const;

    [[nodiscard]] QString noteCountOptionsToSqlQueryPart(
        LocalStorageManager::NoteCountOptions options) const;

    [[nodiscard]] bool addNote(
        qevercloud::Note & note, ErrorString & errorDescription);

    [[nodiscard]] bool updateNote(
        qevercloud::Note & note, LocalStorageManager::UpdateNoteOptions options,
        ErrorString & errorDescription);

    [[nodiscard]] bool findNote(
        qevercloud::Note & note, LocalStorageManager::GetNoteOptions options,
        ErrorString & errorDescription) const;

    [[nodiscard]] QList<qevercloud::Note> listNotesPerNotebook(
        const qevercloud::Notebook & notebook,
        LocalStorageManager::GetNoteOptions options,
        ErrorString & errorDescription,
        const LocalStorageManager::ListObjectsOptions & flag,
        std::size_t limit, std::size_t offset,
        const LocalStorageManager::ListNotesOrder & order,
        const LocalStorageManager::OrderDirection & orderDirection) const;

    [[nodiscard]] QList<qevercloud::Note> listNotesPerTag(
        const qevercloud::Tag & tag,
        LocalStorageManager::GetNoteOptions options,
        ErrorString & errorDescription,
        const LocalStorageManager::ListObjectsOptions & flag,
        std::size_t limit, std::size_t offset,
        const LocalStorageManager::ListNotesOrder & order,
        const LocalStorageManager::OrderDirection & orderDirection) const;

    [[nodiscard]] QList<qevercloud::Note> listNotesPerNotebooksAndTags(
        const QStringList & notebookLocalIds, const QStringList & tagLocalIds,
        LocalStorageManager::GetNoteOptions options,
        ErrorString & errorDescription,
        const LocalStorageManager::ListObjectsOptions & flag,
        std::size_t limit, std::size_t offset,
        const LocalStorageManager::ListNotesOrder & order,
        const LocalStorageManager::OrderDirection & orderDirection) const;

    [[nodiscard]] QList<qevercloud::Note> listNotesByLocalIds(
        const QStringList & noteLocalIds,
        LocalStorageManager::GetNoteOptions options,
        ErrorString & errorDescription,
        const LocalStorageManager::ListObjectsOptions & flag,
        std::size_t limit, std::size_t offset,
        LocalStorageManager::ListNotesOrder order,
        const LocalStorageManager::OrderDirection & orderDirection) const;

    [[nodiscard]] QList<qevercloud::Note> listNotes(
        LocalStorageManager::ListObjectsOptions flag,
        LocalStorageManager::GetNoteOptions options,
        ErrorString & errorDescription, std::size_t limit, std::size_t offset,
        const LocalStorageManager::ListNotesOrder & order,
        const LocalStorageManager::OrderDirection & orderDirection,
        std::optional<QString> linkedNotebookGuid) const;

    [[nodiscard]] QList<qevercloud::Note> listNotesImpl(
        const ErrorString & errorPrefix, const QString & sqlQueryCondition,
        LocalStorageManager::ListObjectsOptions flag,
        LocalStorageManager::GetNoteOptions options,
        ErrorString & errorDescription, std::size_t limit, std::size_t offset,
        const LocalStorageManager::ListNotesOrder & order,
        const LocalStorageManager::OrderDirection & orderDirection) const;

    [[nodiscard]] bool expungeNote(
        qevercloud::Note & note, ErrorString & errorDescription);

    [[nodiscard]] QStringList findNoteLocalIdsWithSearchQuery(
        const NoteSearchQuery & noteSearchQuery,
        ErrorString & errorDescription) const;

    [[nodiscard]] NoteList findNotesWithSearchQuery(
        const NoteSearchQuery & noteSearchQuery,
        LocalStorageManager::GetNoteOptions options,
        ErrorString & errorDescription) const;

    [[nodiscard]] int tagCount(ErrorString & errorDescription) const;

    [[nodiscard]] bool addTag(
        qevercloud::Tag & tag, ErrorString & errorDescription);

    [[nodiscard]] bool updateTag(
        qevercloud::Tag & tag, ErrorString & errorDescription);

    [[nodiscard]] bool findTag(
        qevercloud::Tag & tag, ErrorString & errorDescription) const;

    [[nodiscard]] QList<qevercloud::Tag> listAllTagsPerNote(
        const qevercloud::Note & note, ErrorString & errorDescription,
        const LocalStorageManager::ListObjectsOptions & flag,
        std::size_t limit, std::size_t offset,
        const LocalStorageManager::ListTagsOrder & order,
        const LocalStorageManager::OrderDirection & orderDirection) const;

    [[nodiscard]] QList<qevercloud::Tag> listAllTags(
        ErrorString & errorDescription, std::size_t limit, std::size_t offset,
        const LocalStorageManager::ListTagsOrder & order,
        const LocalStorageManager::OrderDirection & orderDirection,
        std::optional<QString> linkedNotebookGuid) const;

    [[nodiscard]] QList<qevercloud::Tag> listTags(
        LocalStorageManager::ListObjectsOptions flag,
        ErrorString & errorDescription, std::size_t limit, std::size_t offset,
        const LocalStorageManager::ListTagsOrder & order,
        const LocalStorageManager::OrderDirection & orderDirection,
        std::optional<QString> linkedNotebookGuid) const;

    [[nodiscard]] QList<std::pair<qevercloud::Tag, QStringList>>
    listTagsWithNoteLocalIds(
        LocalStorageManager::ListObjectsOptions flag,
        ErrorString & errorDescription, std::size_t limit, std::size_t offset,
        const LocalStorageManager::ListTagsOrder & order,
        const LocalStorageManager::OrderDirection & orderDirection,
        std::optional<QString> linkedNotebookGuid) const;

    [[nodiscard]] bool expungeTag(
        qevercloud::Tag & tag, QStringList & expungedChildTagLocalIds,
        ErrorString & errorDescription);

    [[nodiscard]] bool expungeNotelessTagsFromLinkedNotebooks(
        ErrorString & errorDescription);

    [[nodiscard]] int enResourceCount(ErrorString & errorDescription) const;

    [[nodiscard]] bool addEnResource(
        qevercloud::Resource & resource, ErrorString & errorDescription);

    [[nodiscard]] bool updateEnResource(
        qevercloud::Resource & resource, ErrorString & errorDescription);

    [[nodiscard]] bool findEnResource(
        qevercloud::Resource & resource,
        LocalStorageManager::GetResourceOptions options,
        ErrorString & errorDescription) const;

    [[nodiscard]] bool expungeEnResource(
        qevercloud::Resource & resource, ErrorString & errorDescription);

    [[nodiscard]] int savedSearchCount(ErrorString & errorDescription) const;

    [[nodiscard]] bool addSavedSearch(
        qevercloud::SavedSearch & search, ErrorString & errorDescription);

    [[nodiscard]] bool updateSavedSearch(
        qevercloud::SavedSearch & search, ErrorString & errorDescription);

    [[nodiscard]] bool findSavedSearch(
        qevercloud::SavedSearch & search, ErrorString & errorDescription) const;

    [[nodiscard]] QList<qevercloud::SavedSearch> listAllSavedSearches(
        ErrorString & errorDescription, std::size_t limit, std::size_t offset,
        const LocalStorageManager::ListSavedSearchesOrder & order,
        const LocalStorageManager::OrderDirection & orderDirection) const;

    [[nodiscard]] QList<qevercloud::SavedSearch> listSavedSearches(
        LocalStorageManager::ListObjectsOptions flag,
        ErrorString & errorDescription, std::size_t limit, std::size_t offset,
        const LocalStorageManager::ListSavedSearchesOrder & order,
        const LocalStorageManager::OrderDirection & orderDirection) const;

    [[nodiscard]] bool expungeSavedSearch(
        qevercloud::SavedSearch & search, ErrorString & errorDescription);

    [[nodiscard]] qint32 accountHighUsn(
        const QString & linkedNotebookGuid, ErrorString & errorDescription);

    [[nodiscard]] bool updateSequenceNumberFromTable(
        const QString & tableName, const QString & usnColumnName,
        const QString & queryCondition, qint32 & usn,
        ErrorString & errorDescription);

    [[nodiscard]] bool compactLocalStorage(ErrorString & errorDescription);

public Q_SLOTS:
    void processPostTransactionException(
        ErrorString message, const QSqlError & error);

private:
    void unlockDatabaseFile();

    [[nodiscard]] bool createTables(ErrorString & errorDescription);

    [[nodiscard]] bool insertOrReplaceNotebookRestrictions(
        const QString & localId,
        const qevercloud::NotebookRestrictions & notebookRestrictions,
        ErrorString & errorDescription);

    [[nodiscard]] bool insertOrReplaceSharedNotebook(
        const qevercloud::SharedNotebook & sharedNotebook,
        int indexInNotebook, ErrorString & errorDescription);

    [[nodiscard]] bool rowExists(
        const QString & tableName, const QString & uniqueKeyName,
        const QVariant & uniqueKeyValue) const;

    [[nodiscard]] bool insertOrReplaceUser(
        const qevercloud::User & user, ErrorString & errorDescription);

    [[nodiscard]] bool insertOrReplaceBusinessUserInfo(
        qevercloud::UserID id, const qevercloud::BusinessUserInfo & info,
        ErrorString & errorDescription);

    [[nodiscard]] bool insertOrReplaceAccounting(
        qevercloud::UserID id, const qevercloud::Accounting & accounting,
        ErrorString & errorDescription);

    [[nodiscard]] bool insertOrReplaceAccountLimits(
        qevercloud::UserID id,
        const qevercloud::AccountLimits & accountLimits,
        ErrorString & errorDescription);

    [[nodiscard]] bool insertOrReplaceUserAttributes(
        qevercloud::UserID id,
        const qevercloud::UserAttributes & attributes,
        ErrorString & errorDescription);

    [[nodiscard]] bool checkAndPrepareUserCountQuery() const;
    [[nodiscard]] bool checkAndPrepareInsertOrReplaceUserQuery();
    [[nodiscard]] bool checkAndPrepareInsertOrReplaceAccountingQuery();
    [[nodiscard]] bool checkAndPrepareInsertOrReplaceAccountLimitsQuery();
    [[nodiscard]] bool checkAndPrepareInsertOrReplaceBusinessUserInfoQuery();
    [[nodiscard]] bool checkAndPrepareInsertOrReplaceUserAttributesQuery();

    [[nodiscard]] bool
    checkAndPrepareInsertOrReplaceUserAttributesViewedPromotionsQuery();

    [[nodiscard]] bool
    checkAndPrepareInsertOrReplaceUserAttributesRecentMailedAddressesQuery();

    [[nodiscard]] bool checkAndPrepareDeleteUserQuery();

    [[nodiscard]] bool insertOrReplaceNotebook(
        const qevercloud::Notebook & notebook, ErrorString & errorDescription);

    [[nodiscard]] bool checkAndPrepareNotebookCountQuery() const;
    [[nodiscard]] bool checkAndPrepareInsertOrReplaceNotebookQuery();
    [[nodiscard]] bool
    checkAndPrepareInsertOrReplaceNotebookRestrictionsQuery();
    [[nodiscard]] bool checkAndPrepareInsertOrReplaceSharedNotebookQuery();

    [[nodiscard]] bool insertOrReplaceLinkedNotebook(
        const qevercloud::LinkedNotebook & linkedNotebook,
        ErrorString & errorDescription);

    [[nodiscard]] bool checkAndPrepareGetLinkedNotebookCountQuery() const;
    [[nodiscard]] bool checkAndPrepareInsertOrReplaceLinkedNotebookQuery();

    [[nodiscard]] bool getNoteLocalIdFromResource(
        const qevercloud::Resource & resource, QString & noteLocalId,
        ErrorString & errorDescription) const;

    [[nodiscard]] bool getNotebookLocalIdFromNote(
        const qevercloud::Note & note, QString & notebookLocalId,
        ErrorString & errorDescription) const;

    [[nodiscard]] bool getNotebookGuidForNote(
        const qevercloud::Note & note, QString & notebookGuid,
        ErrorString & errorDescription) const;

    [[nodiscard]] bool getNotebookLocalIdForGuid(
        const QString & notebookGuid, QString & notebookLocalId,
        ErrorString & errorDescription) const;

    [[nodiscard]] bool getNoteLocalIdForGuid(
        const QString & noteGuid, QString & noteLocalId,
        ErrorString & errorDescription) const;

    [[nodiscard]] bool getNoteGuidForLocalId(
        const QString & noteLocalId, QString & noteGuid,
        ErrorString & errorDescription) const;

    [[nodiscard]] bool getTagLocalIdForGuid(
        const QString & tagGuid, QString & tagLocalId,
        ErrorString & errorDescription) const;

    [[nodiscard]] bool getResourceLocalIdForGuid(
        const QString & resourceGuid, QString & resourceLocalId,
        ErrorString & errorDescription) const;

    [[nodiscard]] bool getSavedSearchLocalIdForGuid(
        const QString & savedSearchGuid, QString & savedSearchLocalId,
        ErrorString & errorDescription) const;

    [[nodiscard]] bool insertOrReplaceNote(
        qevercloud::Note & note,
        LocalStorageManager::UpdateNoteOptions options,
        ErrorString & errorDescription);

    [[nodiscard]] bool insertOrReplaceSharedNote(
        const qevercloud::SharedNote & sharedNote, const QString & noteGuid,
        int indexInNote, ErrorString & errorDescription);

    [[nodiscard]] bool insertOrReplaceNoteRestrictions(
        const QString & noteLocalId,
        const qevercloud::NoteRestrictions & noteRestrictions,
        ErrorString & errorDescription);

    [[nodiscard]] bool insertOrReplaceNoteLimits(
        const QString & noteLocalId, const qevercloud::NoteLimits & noteLimits,
        ErrorString & errorDescription);

    [[nodiscard]] bool checkAndPrepareInsertOrReplaceNoteQuery();
    [[nodiscard]] bool checkAndPrepareInsertOrReplaceSharedNoteQuery();
    [[nodiscard]] bool checkAndPrepareInsertOrReplaceNoteRestrictionsQuery();
    [[nodiscard]] bool checkAndPrepareInsertOrReplaceNoteLimitsQuery();
    [[nodiscard]] bool checkAndPrepareCanAddNoteToNotebookQuery() const;
    [[nodiscard]] bool checkAndPrepareCanUpdateNoteInNotebookQuery() const;
    [[nodiscard]] bool checkAndPrepareCanExpungeNoteInNotebookQuery() const;
    [[nodiscard]] bool checkAndPrepareInsertOrReplaceNoteIntoNoteTagsQuery();

    [[nodiscard]] bool insertOrReplaceTag(
        const qevercloud::Tag & tag, ErrorString & errorDescription);

    [[nodiscard]] bool checkAndPrepareTagCountQuery() const;
    [[nodiscard]] bool checkAndPrepareInsertOrReplaceTagQuery();
    [[nodiscard]] bool checkAndPrepareDeleteTagQuery();

    [[nodiscard]] bool complementTagParentInfo(
        qevercloud::Tag & tag, ErrorString & errorDescription);

    [[nodiscard]] bool insertOrReplaceResource(
        const qevercloud::Resource & resource, int indexInNote,
        ErrorString & errorDescription, bool setResourceBinaryData = true,
        bool useSeparateTransaction = true);

    [[nodiscard]] bool insertOrReplaceResourceAttributes(
        const QString & localId,
        const qevercloud::ResourceAttributes & attributes,
        ErrorString & errorDescription);

    [[nodiscard]] bool insertOrReplaceResourceMetadata(
        const qevercloud::Resource & resource, int indexInNote,
        bool setResourceDataProperties, ErrorString & errorDescription);

    [[nodiscard]] bool writeResourceBinaryDataToFiles(
        const qevercloud::Resource & resource, ErrorString & errorDescription);

    [[nodiscard]] bool writeResourceBinaryDataToFile(
        const QString & resourceLocalId, const QString & noteLocalId,
        const QByteArray & dataBody, bool isAlternateDataBody,
        bool replaceOriginalFile, ErrorString & errorDescription);

    [[nodiscard]] bool updateNoteResources(
        const qevercloud::Resource & resource, ErrorString & errorDescription);

    void setNoteIdsToNoteResources(qevercloud::Note & note) const;

    [[nodiscard]] bool removeResourceDataFiles(
        const qevercloud::Resource & resource, ErrorString & errorDescription);

    [[nodiscard]] bool removeResourceDataFilesForNote(
        const QString & noteLocalId, ErrorString & errorDescription);

    [[nodiscard]] bool removeResourceDataFilesForNotebook(
        const qevercloud::Notebook & notebook, ErrorString & errorDescription);

    [[nodiscard]] bool removeResourceDataFilesForLinkedNotebook(
        const qevercloud::LinkedNotebook & linkedNotebook,
        ErrorString & errorDescription);

    [[nodiscard]] bool
    checkAndPrepareInsertOrReplaceResourceMetadataWithDataPropertiesQuery();

    [[nodiscard]] bool
    checkAndPrepareUpdateResourceMetadataWithoutDataPropertiesQuery();

    [[nodiscard]] bool checkAndPrepareInsertOrReplaceNoteResourceQuery();

    [[nodiscard]] bool
    checkAndPrepareDeleteResourceFromResourceRecognitionTypesQuery();

    [[nodiscard]] bool
    checkAndPrepareInsertOrReplaceIntoResourceRecognitionDataQuery();

    [[nodiscard]] bool
    checkAndPrepareDeleteResourceFromResourceAttributesQuery();

    [[nodiscard]] bool
    checkAndPrepareDeleteResourceFromResourceAttributesApplicationDataKeysOnlyQuery();

    [[nodiscard]] bool
    checkAndPrepareDeleteResourceFromResourceAttributesApplicationDataFullMapQuery();

    [[nodiscard]] bool checkAndPrepareInsertOrReplaceResourceAttributesQuery();

    [[nodiscard]] bool
    checkAndPrepareInsertOrReplaceResourceAttributesApplicationDataKeysOnlyQuery();

    [[nodiscard]] bool
    checkAndPrepareInsertOrReplaceResourceAttributesApplicationDataFullMapQuery();

    [[nodiscard]] bool checkAndPrepareResourceCountQuery() const;

    [[nodiscard]] bool insertOrReplaceSavedSearch(
        const qevercloud::SavedSearch & search, ErrorString & errorDescription);

    [[nodiscard]] bool checkAndPrepareInsertOrReplaceSavedSearchQuery();
    [[nodiscard]] bool checkAndPrepareGetSavedSearchCountQuery() const;
    [[nodiscard]] bool checkAndPrepareExpungeSavedSearchQuery();

    [[nodiscard]] bool complementTagsWithNoteLocalIds(
        QList<std::pair<qevercloud::Tag, QStringList>> & tagsWithNoteLocalIds,
        ErrorString & errorDescription) const;

    [[nodiscard]] bool readResourceDataFromFiles(
        qevercloud::Resource & resource, ErrorString & errorDescription) const;

    enum class ReadResourceBinaryDataFromFileStatus
    {
        Success = 0,
        FileNotFound,
        Failure
    };

    [[nodiscard]] ReadResourceBinaryDataFromFileStatus
    readResourceBinaryDataFromFile(
        const QString & resourceLocalId, const QString & noteLocalId,
        bool isAlternateDataBody, QByteArray & dataBody,
        ErrorString & errorDescription) const;

    [[nodiscard]] bool fillResourceFromSqlRecord(
        const QSqlRecord & rec, qevercloud::Resource & resource,
        int & indexInNote, ErrorString & errorDescription) const;

    [[nodiscard]] bool fillResourceAttributesFromSqlRecord(
        const QSqlRecord & rec,
        qevercloud::ResourceAttributes & attributes) const;

    [[nodiscard]] bool
    fillResourceAttributesApplicationDataKeysOnlyFromSqlRecord(
        const QSqlRecord & rec,
        qevercloud::ResourceAttributes & attributes) const;

    [[nodiscard]] bool
    fillResourceAttributesApplicationDataFullMapFromSqlRecord(
        const QSqlRecord & rec,
        qevercloud::ResourceAttributes & attributes) const;

    void fillNoteAttributesFromSqlRecord(
        const QSqlRecord & rec, qevercloud::NoteAttributes & attributes) const;

    void fillNoteAttributesApplicationDataKeysOnlyFromSqlRecord(
        const QSqlRecord & rec, qevercloud::NoteAttributes & attributes) const;

    void fillNoteAttributesApplicationDataFullMapFromSqlRecord(
        const QSqlRecord & rec, qevercloud::NoteAttributes & attributes) const;

    void fillNoteAttributesClassificationsFromSqlRecord(
        const QSqlRecord & rec, qevercloud::NoteAttributes & attributes) const;

    [[nodiscard]] bool fillUserFromSqlRecord(
        const QSqlRecord & rec, qevercloud::User & user,
        ErrorString & errorDescription) const;

    [[nodiscard]] bool fillNoteFromSqlRecord(
        const QSqlRecord & record, qevercloud::Note & note,
        ErrorString & errorDescription) const;

    [[nodiscard]] bool fillSharedNoteFromSqlRecord(
        const QSqlRecord & record, qevercloud::SharedNote & sharedNote,
        int & indexInNote, ErrorString & errorDescription) const;

    [[nodiscard]] bool fillNoteTagIdFromSqlRecord(
        const QSqlRecord & record, const QString & column,
        QList<std::pair<QString, int>> & tagIdsAndIndices,
        QHash<QString, int> & tagIndexPerId,
        ErrorString & errorDescription) const;

    [[nodiscard]] bool fillNotebookFromSqlRecord(
        const QSqlRecord & record, qevercloud::Notebook & notebook,
        ErrorString & errorDescription) const;

    [[nodiscard]] bool fillSharedNotebookFromSqlRecord(
        const QSqlRecord & record, qevercloud::SharedNotebook & sharedNotebook,
        int & indexInNotebook, ErrorString & errorDescription) const;

    [[nodiscard]] bool fillLinkedNotebookFromSqlRecord(
        const QSqlRecord & record, qevercloud::LinkedNotebook & linkedNotebook,
        ErrorString & errorDescription) const;

    [[nodiscard]] bool fillSavedSearchFromSqlRecord(
        const QSqlRecord & rec, qevercloud::SavedSearch & search,
        ErrorString & errorDescription) const;

    [[nodiscard]] bool fillTagFromSqlRecord(
        const QSqlRecord & rec, qevercloud::Tag & tag,
        ErrorString & errorDescription) const;

    [[nodiscard]] QList<qevercloud::Tag> fillTagsFromSqlQuery(
        QSqlQuery & query, ErrorString & errorDescription) const;

    [[nodiscard]] bool findAndSetTagIdsPerNote(
        qevercloud::Note & note, ErrorString & errorDescription) const;

    [[nodiscard]] bool noteSearchQueryToSQL(
        const NoteSearchQuery & noteSearchQuery, QString & sql,
        ErrorString & errorDescription) const;

    [[nodiscard]] bool noteSearchQueryContentSearchTermsToSQL(
        const NoteSearchQuery & noteSearchQuery, QString & sql,
        ErrorString & errorDescription) const;

    void contentSearchTermToSQLQueryPart(
        QString & frontSearchTermModifier, QString & searchTerm,
        QString & backSearchTermModifier, QString & matchStatement) const;

    [[nodiscard]] bool tagNamesToTagLocalIds(
        const QStringList & tagNames, QStringList & tagLocalIds,
        ErrorString & errorDescription) const;

    [[nodiscard]] bool resourceMimeTypesToResourceLocalIds(
        const QStringList & resourceMimeTypes, QStringList & resourceLocalIds,
        ErrorString & errorDescription) const;

    [[nodiscard]] bool complementResourceNoteIds(
        qevercloud::Resource & resource, ErrorString & errorDescription) const;

    [[nodiscard]] bool partialUpdateNoteResources(
        const QString & noteLocalId,
        const QList<qevercloud::Resource> & updatedNoteResources,
        bool UpdateResourceBinaryData, ErrorString & errorDescription);

    // Figure out which previous resources are not present in the updated
    // resources list, which resources in the updated list are new (not present
    // in the previous resources list) and which resources are present in both
    // lists - these are being actually updated.
    void classifyNoteResources(
        const QList<qevercloud::Resource> & previousNoteResources,
        const QList<qevercloud::Resource> & updatedNoteResources,
        QSet<QString> & localIdsOfRemovedResources,
        QList<qevercloud::Resource> & addedResources,
        QList<qevercloud::Resource> & updatedResources) const;

    [[nodiscard]] bool expungeResources(
        const QSet<QString> & localIds, const QString & noteLocalId,
        ErrorString & errorDescription);

    [[nodiscard]] bool updateResourceIndexesInNote(
        const QList<std::pair<QString, int>> & resourceLocalIdsWithIndexesInNote,
        ErrorString & errorDescription);

    template <class T>
    [[nodiscard]] QString listObjectsOptionsToSqlQueryConditions(
        const LocalStorageManager::ListObjectsOptions & options,
        ErrorString & errorDescription) const;

    template <class T, class TOrderBy>
    [[nodiscard]] QList<T> listObjects(
        const LocalStorageManager::ListObjectsOptions & flag,
        ErrorString & errorDescription, std::size_t limit, std::size_t offset,
        const TOrderBy & orderBy,
        const LocalStorageManager::OrderDirection & orderDirection,
        const QString & additionalSqlQueryCondition = QString()) const;

    template <class T>
    [[nodiscard]] QString listObjectsGenericSqlQuery() const;

    template <class TOrderBy>
    [[nodiscard]] QString orderByToSqlTableColumn(
        const TOrderBy & orderBy) const;

    template <class T>
    [[nodiscard]] bool fillObjectsFromSqlQuery(
        QSqlQuery query, QList<T> & objects,
        ErrorString & errorDescription) const;

    template <class T>
    [[nodiscard]] bool fillObjectFromSqlRecord(
        const QSqlRecord & record, T & object,
        ErrorString & errorDescription) const;

    void clearDatabaseFile();

    void clearCachedQueries();

    struct QStringIntPairCompareByInt
    {
        [[nodiscard]] bool operator()(
            const std::pair<QString, int> & lhs,
            const std::pair<QString, int> & rhs) const noexcept;
    };

    struct HighUsnRequestData
    {
        HighUsnRequestData() = default;

        HighUsnRequestData(
            QString tableName, QString usnColumnName, QString queryCondition) :
            m_tableName(std::move(tableName)),
            m_usnColumnName(std::move(usnColumnName)),
            m_queryCondition(std::move(queryCondition))
        {}

        QString m_tableName;
        QString m_usnColumnName;
        QString m_queryCondition;
    };

    Account m_currentAccount;
    QString m_databaseFilePath;
    QSqlDatabase m_sqlDatabase;
    boost::interprocess::file_lock m_databaseFileLock;

    QSqlQuery m_insertOrReplaceSavedSearchQuery;
    bool m_insertOrReplaceSavedSearchQueryPrepared = false;

    mutable QSqlQuery m_getSavedSearchCountQuery;
    mutable bool m_getSavedSearchCountQueryPrepared = false;

    QSqlQuery m_insertOrReplaceResourceMetadataWithDataPropertiesQuery;
    bool m_insertOrReplaceResourceMetadataWithDataPropertiesQueryPrepared =
        false;

    QSqlQuery m_updateResourceMetadataWithoutDataPropertiesQuery;
    bool m_updateResourceMetadataWithoutDataPropertiesQueryPrepared = false;

    QSqlQuery m_insertOrReplaceNoteResourceQuery;
    bool m_insertOrReplaceNoteResourceQueryPrepared = false;

    QSqlQuery m_deleteResourceFromResourceRecognitionTypesQuery;
    bool m_deleteResourceFromResourceRecognitionTypesQueryPrepared = false;

    QSqlQuery m_insertOrReplaceIntoResourceRecognitionDataQuery;
    bool m_insertOrReplaceIntoResourceRecognitionDataQueryPrepared = false;

    QSqlQuery m_deleteResourceFromResourceAttributesQuery;
    bool m_deleteResourceFromResourceAttributesQueryPrepared = false;

    QSqlQuery
        m_deleteResourceFromResourceAttributesApplicationDataKeysOnlyQuery;
    bool
        m_deleteResourceFromResourceAttributesApplicationDataKeysOnlyQueryPrepared =
            false;

    QSqlQuery m_deleteResourceFromResourceAttributesApplicationDataFullMapQuery;
    bool
        m_deleteResourceFromResourceAttributesApplicationDataFullMapQueryPrepared =
            false;

    QSqlQuery m_insertOrReplaceResourceAttributesQuery;
    bool m_insertOrReplaceResourceAttributesQueryPrepared = false;

    QSqlQuery m_insertOrReplaceResourceAttributeApplicationDataKeysOnlyQuery;
    bool
        m_insertOrReplaceResourceAttributeApplicationDataKeysOnlyQueryPrepared =
            false;

    QSqlQuery m_insertOrReplaceResourceAttributeApplicationDataFullMapQuery;
    bool m_insertOrReplaceResourceAttributeApplicationDataFullMapQueryPrepared =
        false;

    mutable QSqlQuery m_getResourceCountQuery;
    mutable bool m_getResourceCountQueryPrepared = false;

    mutable QSqlQuery m_getTagCountQuery;
    mutable bool m_getTagCountQueryPrepared = false;

    QSqlQuery m_insertOrReplaceTagQuery;
    bool m_insertOrReplaceTagQueryPrepared = false;

    QSqlQuery m_insertOrReplaceNoteQuery;
    bool m_insertOrReplaceNoteQueryPrepared = false;

    QSqlQuery m_insertOrReplaceSharedNoteQuery;
    bool m_insertOrReplaceSharedNoteQueryPrepared = false;

    QSqlQuery m_insertOrReplaceNoteRestrictionsQuery;
    bool m_insertOrReplaceNoteRestrictionsQueryPrepared = false;

    QSqlQuery m_insertOrReplaceNoteLimitsQuery;
    bool m_insertOrReplaceNoteLimitsQueryPrepared = false;

    mutable QSqlQuery m_canAddNoteToNotebookQuery;
    mutable bool m_canAddNoteToNotebookQueryPrepared = false;

    mutable QSqlQuery m_canUpdateNoteInNotebookQuery;
    mutable bool m_canUpdateNoteInNotebookQueryPrepared = false;

    mutable QSqlQuery m_canExpungeNoteInNotebookQuery;
    mutable bool m_canExpungeNoteInNotebookQueryPrepared = false;

    QSqlQuery m_insertOrReplaceNoteIntoNoteTagsQuery;
    bool m_insertOrReplaceNoteIntoNoteTagsQueryPrepared = false;

    mutable QSqlQuery m_getLinkedNotebookCountQuery;
    mutable bool m_getLinkedNotebookCountQueryPrepared = false;

    QSqlQuery m_insertOrReplaceLinkedNotebookQuery;
    bool m_insertOrReplaceLinkedNotebookQueryPrepared = false;

    mutable QSqlQuery m_getNotebookCountQuery;
    mutable bool m_getNotebookCountQueryPrepared = false;

    QSqlQuery m_insertOrReplaceNotebookQuery;
    bool m_insertOrReplaceNotebookQueryPrepared = false;

    QSqlQuery m_insertOrReplaceNotebookRestrictionsQuery;
    bool m_insertOrReplaceNotebookRestrictionsQueryPrepared = false;

    QSqlQuery m_insertOrReplaceSharedNotebookQuery;
    bool m_insertOrReplaceSharedNotebookQueryPrepared = false;

    mutable QSqlQuery m_getUserCountQuery;
    mutable bool m_getUserCountQueryPrepared = false;

    QSqlQuery m_insertOrReplaceUserQuery;
    bool m_insertOrReplaceUserQueryPrepared = false;

    QSqlQuery m_insertOrReplaceUserAttributesQuery;
    bool m_insertOrReplaceUserAttributesQueryPrepared = false;

    QSqlQuery m_insertOrReplaceAccountingQuery;
    bool m_insertOrReplaceAccountingQueryPrepared = false;

    QSqlQuery m_insertOrReplaceAccountLimitsQuery;
    bool m_insertOrReplaceAccountLimitsQueryPrepared = false;

    QSqlQuery m_insertOrReplaceBusinessUserInfoQuery;
    bool m_insertOrReplaceBusinessUserInfoQueryPrepared = false;

    QSqlQuery m_insertOrReplaceUserAttributesViewedPromotionsQuery;
    bool m_insertOrReplaceUserAttributesViewedPromotionsQueryPrepared = false;

    QSqlQuery m_insertOrReplaceUserAttributesRecentMailedAddressesQuery;
    bool m_insertOrReplaceUserAttributesRecentMailedAddressesQueryPrepared =
        false;

    QSqlQuery m_deleteUserQuery;
    bool m_deleteUserQueryPrepared = false;

    LocalStoragePatchManager * m_pLocalStoragePatchManager = nullptr;

    StringUtils m_stringUtils;
    QList<QChar> m_preservedAsterisk;
};

} // namespace quentier

#endif // LIB_QUENTIER_LOCAL_STORAGE_LOCAL_STORAGE_MANAGER_PRIVATE_H
