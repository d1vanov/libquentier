/*
 * Copyright 2016-2020 Dmitry Ivanov
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

#include <quentier/local_storage/Lists.h>
#include <quentier/local_storage/LocalStorageManager.h>
#include <quentier/types/LinkedNotebook.h>
#include <quentier/types/Note.h>
#include <quentier/types/Notebook.h>
#include <quentier/types/Resource.h>
#include <quentier/types/SavedSearch.h>
#include <quentier/types/SharedNotebook.h>
#include <quentier/types/Tag.h>
#include <quentier/types/User.h>
#include <quentier/utility/StringUtils.h>
#include <quentier/utility/SuppressWarnings.h>

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

namespace quentier {

QT_FORWARD_DECLARE_CLASS(LocalStoragePatchManager)
QT_FORWARD_DECLARE_CLASS(NoteSearchQuery)

class Q_DECL_HIDDEN LocalStorageManagerPrivate final : public QObject
{
    Q_OBJECT
public:
    LocalStorageManagerPrivate(
        const Account & account,
        const LocalStorageManager::StartupOptions options,
        QObject * parent = nullptr);

    virtual ~LocalStorageManagerPrivate() override;

Q_SIGNALS:
    void upgradeProgress(double progress);

public:
    void switchUser(
        const Account & account,
        const LocalStorageManager::StartupOptions options);

    bool isLocalStorageVersionTooHigh(ErrorString & errorDescription);
    bool localStorageRequiresUpgrade(ErrorString & errorDescription);
    QVector<std::shared_ptr<ILocalStoragePatch>> requiredLocalStoragePatches();
    qint32 localStorageVersion(ErrorString & errorDescription);
    qint32 highestSupportedLocalStorageVersion() const;

    int userCount(ErrorString & errorDescription) const;
    bool addUser(const User & user, ErrorString & errorDescription);
    bool updateUser(const User & user, ErrorString & errorDescription);
    bool findUser(User & user, ErrorString & errorDescription) const;
    bool deleteUser(const User & user, ErrorString & errorDescription);
    bool expungeUser(const User & user, ErrorString & errorDescription);

    int notebookCount(ErrorString & errorDescription) const;
    bool addNotebook(Notebook & notebook, ErrorString & errorDescription);
    bool updateNotebook(Notebook & notebook, ErrorString & errorDescription);

    bool findNotebook(
        Notebook & notebook, ErrorString & errorDescription) const;

    bool findDefaultNotebook(
        Notebook & notebook, ErrorString & errorDescription) const;

    bool findLastUsedNotebook(
        Notebook & notebook, ErrorString & errorDescription) const;

    bool findDefaultOrLastUsedNotebook(
        Notebook & notebook, ErrorString & errorDescription) const;

    QList<Notebook> listAllNotebooks(
        ErrorString & errorDescription, const size_t limit, const size_t offset,
        const LocalStorageManager::ListNotebooksOrder & order,
        const LocalStorageManager::OrderDirection & orderDirection,
        const QString & linkedNotebookGuid) const;

    QList<Notebook> listNotebooks(
        const LocalStorageManager::ListObjectsOptions flag,
        ErrorString & errorDescription, const size_t limit, const size_t offset,
        const LocalStorageManager::ListNotebooksOrder & order,
        const LocalStorageManager::OrderDirection & orderDirection,
        const QString & linkedNotebookGuid) const;

    QList<SharedNotebook> listAllSharedNotebooks(
        ErrorString & errorDescription) const;

    QList<SharedNotebook> listSharedNotebooksPerNotebookGuid(
        const QString & notebookGuid, ErrorString & errorDescription) const;

    bool expungeNotebook(Notebook & notebook, ErrorString & errorDescription);

    int linkedNotebookCount(ErrorString & errorDescription) const;

    bool addLinkedNotebook(
        const LinkedNotebook & linkedNotebook, ErrorString & errorDescription);

    bool updateLinkedNotebook(
        const LinkedNotebook & linkedNotebook, ErrorString & errorDescription);

    bool findLinkedNotebook(
        LinkedNotebook & linkedNotebook, ErrorString & errorDescription) const;

    QList<LinkedNotebook> listAllLinkedNotebooks(
        ErrorString & errorDescription, const size_t limit, const size_t offset,
        const LocalStorageManager::ListLinkedNotebooksOrder order,
        const LocalStorageManager::OrderDirection & orderDirection) const;

    QList<LinkedNotebook> listLinkedNotebooks(
        const LocalStorageManager::ListObjectsOptions flag,
        ErrorString & errorDescription, const size_t limit, const size_t offset,
        const LocalStorageManager::ListLinkedNotebooksOrder & order,
        const LocalStorageManager::OrderDirection & orderDirection) const;

    bool expungeLinkedNotebook(
        const LinkedNotebook & linkedNotebook, ErrorString & errorDescription);

    int noteCount(
        ErrorString & errorDescription,
        const LocalStorageManager::NoteCountOptions options) const;

    int noteCountPerNotebook(
        const Notebook & notebook, ErrorString & errorDescription,
        const LocalStorageManager::NoteCountOptions options) const;

    int noteCountPerTag(
        const Tag & tag, ErrorString & errorDescription,
        const LocalStorageManager::NoteCountOptions options) const;

    bool noteCountsPerAllTags(
        QHash<QString, int> & noteCountsPerTagLocalUid,
        ErrorString & errorDescription,
        const LocalStorageManager::NoteCountOptions options) const;

    int noteCountPerNotebooksAndTags(
        const QStringList & notebookLocalUids, const QStringList & tagLocalUids,
        ErrorString & errorDescription,
        const LocalStorageManager::NoteCountOptions options) const;

    QString noteCountOptionsToSqlQueryPart(
        const LocalStorageManager::NoteCountOptions options) const;

    bool addNote(Note & note, ErrorString & errorDescription);

    bool updateNote(
        Note & note, const LocalStorageManager::UpdateNoteOptions options,
        ErrorString & errorDescription);

    bool findNote(
        Note & note, const LocalStorageManager::GetNoteOptions options,
        ErrorString & errorDescription) const;

    QList<Note> listNotesPerNotebook(
        const Notebook & notebook,
        const LocalStorageManager::GetNoteOptions options,
        ErrorString & errorDescription,
        const LocalStorageManager::ListObjectsOptions & flag,
        const size_t limit, const size_t offset,
        const LocalStorageManager::ListNotesOrder & order,
        const LocalStorageManager::OrderDirection & orderDirection) const;

    QList<Note> listNotesPerTag(
        const Tag & tag, const LocalStorageManager::GetNoteOptions options,
        ErrorString & errorDescription,
        const LocalStorageManager::ListObjectsOptions & flag,
        const size_t limit, const size_t offset,
        const LocalStorageManager::ListNotesOrder & order,
        const LocalStorageManager::OrderDirection & orderDirection) const;

    QList<Note> listNotesPerNotebooksAndTags(
        const QStringList & notebookLocalUids, const QStringList & tagLocalUids,
        const LocalStorageManager::GetNoteOptions options,
        ErrorString & errorDescription,
        const LocalStorageManager::ListObjectsOptions & flag,
        const size_t limit, const size_t offset,
        const LocalStorageManager::ListNotesOrder & order,
        const LocalStorageManager::OrderDirection & orderDirection) const;

    QList<Note> listNotesByLocalUids(
        const QStringList & noteLocalUids,
        const LocalStorageManager::GetNoteOptions options,
        ErrorString & errorDescription,
        const LocalStorageManager::ListObjectsOptions & flag,
        const size_t limit, const size_t offset,
        const LocalStorageManager::ListNotesOrder order,
        const LocalStorageManager::OrderDirection & orderDirection) const;

    QList<Note> listNotes(
        const LocalStorageManager::ListObjectsOptions flag,
        const LocalStorageManager::GetNoteOptions options,
        ErrorString & errorDescription, const size_t limit, const size_t offset,
        const LocalStorageManager::ListNotesOrder & order,
        const LocalStorageManager::OrderDirection & orderDirection,
        const QString & linkedNotebookGuid) const;

    QList<Note> listNotesImpl(
        const ErrorString & errorPrefix, const QString & sqlQueryCondition,
        const LocalStorageManager::ListObjectsOptions flag,
        const LocalStorageManager::GetNoteOptions options,
        ErrorString & errorDescription, const size_t limit, const size_t offset,
        const LocalStorageManager::ListNotesOrder & order,
        const LocalStorageManager::OrderDirection & orderDirection) const;

    bool expungeNote(Note & note, ErrorString & errorDescription);

    QStringList findNoteLocalUidsWithSearchQuery(
        const NoteSearchQuery & noteSearchQuery,
        ErrorString & errorDescription) const;

    NoteList findNotesWithSearchQuery(
        const NoteSearchQuery & noteSearchQuery,
        const LocalStorageManager::GetNoteOptions options,
        ErrorString & errorDescription) const;

    int tagCount(ErrorString & errorDescription) const;
    bool addTag(Tag & tag, ErrorString & errorDescription);
    bool updateTag(Tag & tag, ErrorString & errorDescription);
    bool findTag(Tag & tag, ErrorString & errorDescription) const;

    QList<Tag> listAllTagsPerNote(
        const Note & note, ErrorString & errorDescription,
        const LocalStorageManager::ListObjectsOptions & flag,
        const size_t limit, const size_t offset,
        const LocalStorageManager::ListTagsOrder & order,
        const LocalStorageManager::OrderDirection & orderDirection) const;

    QList<Tag> listAllTags(
        ErrorString & errorDescription, const size_t limit, const size_t offset,
        const LocalStorageManager::ListTagsOrder & order,
        const LocalStorageManager::OrderDirection & orderDirection,
        const QString & linkedNotebookGuid) const;

    QList<Tag> listTags(
        const LocalStorageManager::ListObjectsOptions flag,
        ErrorString & errorDescription, const size_t limit, const size_t offset,
        const LocalStorageManager::ListTagsOrder & order,
        const LocalStorageManager::OrderDirection & orderDirection,
        const QString & linkedNotebookGuid) const;

    QList<std::pair<Tag, QStringList>> listTagsWithNoteLocalUids(
        const LocalStorageManager::ListObjectsOptions flag,
        ErrorString & errorDescription, const size_t limit, const size_t offset,
        const LocalStorageManager::ListTagsOrder & order,
        const LocalStorageManager::OrderDirection & orderDirection,
        const QString & linkedNotebookGuid) const;

    bool expungeTag(
        Tag & tag, QStringList & expungedChildTagLocalUids,
        ErrorString & errorDescription);

    bool expungeNotelessTagsFromLinkedNotebooks(ErrorString & errorDescription);

    int enResourceCount(ErrorString & errorDescription) const;
    bool addEnResource(Resource & resource, ErrorString & errorDescription);
    bool updateEnResource(Resource & resource, ErrorString & errorDescription);

    bool findEnResource(
        Resource & resource,
        const LocalStorageManager::GetResourceOptions options,
        ErrorString & errorDescription) const;

    bool expungeEnResource(Resource & resource, ErrorString & errorDescription);

    int savedSearchCount(ErrorString & errorDescription) const;
    bool addSavedSearch(SavedSearch & search, ErrorString & errorDescription);

    bool updateSavedSearch(
        SavedSearch & search, ErrorString & errorDescription);

    bool findSavedSearch(
        SavedSearch & search, ErrorString & errorDescription) const;

    QList<SavedSearch> listAllSavedSearches(
        ErrorString & errorDescription, const size_t limit, const size_t offset,
        const LocalStorageManager::ListSavedSearchesOrder & order,
        const LocalStorageManager::OrderDirection & orderDirection) const;

    QList<SavedSearch> listSavedSearches(
        const LocalStorageManager::ListObjectsOptions flag,
        ErrorString & errorDescription, const size_t limit, const size_t offset,
        const LocalStorageManager::ListSavedSearchesOrder & order,
        const LocalStorageManager::OrderDirection & orderDirection) const;

    bool expungeSavedSearch(
        SavedSearch & search, ErrorString & errorDescription);

    qint32 accountHighUsn(
        const QString & linkedNotebookGuid, ErrorString & errorDescription);

    bool updateSequenceNumberFromTable(
        const QString & tableName, const QString & usnColumnName,
        const QString & queryCondition, qint32 & usn,
        ErrorString & errorDescription);

    bool compactLocalStorage(ErrorString & errorDescription);

public Q_SLOTS:
    void processPostTransactionException(ErrorString message, QSqlError error);

private:
    LocalStorageManagerPrivate() = delete;
    Q_DISABLE_COPY(LocalStorageManagerPrivate)

    void unlockDatabaseFile();

    bool createTables(ErrorString & errorDescription);

    bool insertOrReplaceNotebookRestrictions(
        const QString & localUid,
        const qevercloud::NotebookRestrictions & notebookRestrictions,
        ErrorString & errorDescription);

    bool insertOrReplaceSharedNotebook(
        const SharedNotebook & sharedNotebook, ErrorString & errorDescription);

    bool rowExists(
        const QString & tableName, const QString & uniqueKeyName,
        const QVariant & uniqueKeyValue) const;

    bool insertOrReplaceUser(const User & user, ErrorString & errorDescription);

    bool insertOrReplaceBusinessUserInfo(
        const qevercloud::UserID id, const qevercloud::BusinessUserInfo & info,
        ErrorString & errorDescription);

    bool insertOrReplaceAccounting(
        const qevercloud::UserID id, const qevercloud::Accounting & accounting,
        ErrorString & errorDescription);

    bool insertOrReplaceAccountLimits(
        const qevercloud::UserID id,
        const qevercloud::AccountLimits & accountLimits,
        ErrorString & errorDescription);

    bool insertOrReplaceUserAttributes(
        const qevercloud::UserID id,
        const qevercloud::UserAttributes & attributes,
        ErrorString & errorDescription);

    bool checkAndPrepareUserCountQuery() const;
    bool checkAndPrepareInsertOrReplaceUserQuery();
    bool checkAndPrepareInsertOrReplaceAccountingQuery();
    bool checkAndPrepareInsertOrReplaceAccountLimitsQuery();
    bool checkAndPrepareInsertOrReplaceBusinessUserInfoQuery();
    bool checkAndPrepareInsertOrReplaceUserAttributesQuery();
    bool checkAndPrepareInsertOrReplaceUserAttributesViewedPromotionsQuery();

    bool
    checkAndPrepareInsertOrReplaceUserAttributesRecentMailedAddressesQuery();

    bool checkAndPrepareDeleteUserQuery();

    bool insertOrReplaceNotebook(
        const Notebook & notebook, ErrorString & errorDescription);

    bool checkAndPrepareNotebookCountQuery() const;
    bool checkAndPrepareInsertOrReplaceNotebookQuery();
    bool checkAndPrepareInsertOrReplaceNotebookRestrictionsQuery();
    bool checkAndPrepareInsertOrReplaceSharedNotebookQuery();

    bool insertOrReplaceLinkedNotebook(
        const LinkedNotebook & linkedNotebook, ErrorString & errorDescription);

    bool checkAndPrepareGetLinkedNotebookCountQuery() const;
    bool checkAndPrepareInsertOrReplaceLinkedNotebookQuery();

    bool getNoteLocalUidFromResource(
        const Resource & resource, QString & noteLocalUid,
        ErrorString & errorDescription) const;

    bool getNotebookLocalUidFromNote(
        const Note & note, QString & notebookLocalUid,
        ErrorString & errorDescription) const;

    bool getNotebookGuidForNote(
        const Note & note, QString & notebookGuid,
        ErrorString & errorDescription) const;

    bool getNotebookLocalUidForGuid(
        const QString & notebookGuid, QString & notebookLocalUid,
        ErrorString & errorDescription) const;

    bool getNoteLocalUidForGuid(
        const QString & noteGuid, QString & noteLocalUid,
        ErrorString & errorDescription) const;

    bool getNoteGuidForLocalUid(
        const QString & noteLocalUid, QString & noteGuid,
        ErrorString & errorDescription) const;

    bool getTagLocalUidForGuid(
        const QString & tagGuid, QString & tagLocalUid,
        ErrorString & errorDescription) const;

    bool getResourceLocalUidForGuid(
        const QString & resourceGuid, QString & resourceLocalUid,
        ErrorString & errorDescription) const;

    bool getSavedSearchLocalUidForGuid(
        const QString & savedSearchGuid, QString & savedSearchLocalUid,
        ErrorString & errorDescription) const;

    bool insertOrReplaceNote(
        Note & note, const LocalStorageManager::UpdateNoteOptions options,
        ErrorString & errorDescription);

    bool insertOrReplaceSharedNote(
        const SharedNote & sharedNote, ErrorString & errorDescription);

    bool insertOrReplaceNoteRestrictions(
        const QString & noteLocalUid,
        const qevercloud::NoteRestrictions & noteRestrictions,
        ErrorString & errorDescription);

    bool insertOrReplaceNoteLimits(
        const QString & noteLocalUid, const qevercloud::NoteLimits & noteLimits,
        ErrorString & errorDescription);

    bool checkAndPrepareInsertOrReplaceNoteQuery();
    bool checkAndPrepareInsertOrReplaceSharedNoteQuery();
    bool checkAndPrepareInsertOrReplaceNoteRestrictionsQuery();
    bool checkAndPrepareInsertOrReplaceNoteLimitsQuery();
    bool checkAndPrepareCanAddNoteToNotebookQuery() const;
    bool checkAndPrepareCanUpdateNoteInNotebookQuery() const;
    bool checkAndPrepareCanExpungeNoteInNotebookQuery() const;
    bool checkAndPrepareInsertOrReplaceNoteIntoNoteTagsQuery();

    bool insertOrReplaceTag(const Tag & tag, ErrorString & errorDescription);
    bool checkAndPrepareTagCountQuery() const;
    bool checkAndPrepareInsertOrReplaceTagQuery();
    bool checkAndPrepareDeleteTagQuery();
    bool complementTagParentInfo(Tag & tag, ErrorString & errorDescription);

    bool insertOrReplaceResource(
        const Resource & resource, ErrorString & errorDescription,
        const bool setResourceBinaryData = true,
        const bool useSeparateTransaction = true);

    bool insertOrReplaceResourceAttributes(
        const QString & localUid,
        const qevercloud::ResourceAttributes & attributes,
        ErrorString & errorDescription);

    bool insertOrReplaceResourceMetadata(
        const Resource & resource, const bool setResourceDataProperties,
        ErrorString & errorDescription);

    bool writeResourceBinaryDataToFiles(
        const Resource & resource, ErrorString & errorDescription);

    bool writeResourceBinaryDataToFile(
        const QString & resourceLocalUid, const QString & noteLocalUid,
        const QByteArray & dataBody, const bool isAlternateDataBody,
        const bool replaceOriginalFile, ErrorString & errorDescription);

    bool updateNoteResources(
        const Resource & resource, ErrorString & errorDescription);

    void setNoteIdsToNoteResources(Note & note) const;

    bool removeResourceDataFiles(
        const Resource & resource, ErrorString & errorDescription);

    bool removeResourceDataFilesForNote(
        const QString & noteLocalUid, ErrorString & errorDescription);

    bool removeResourceDataFilesForNotebook(
        const Notebook & notebook, ErrorString & errorDescription);

    bool removeResourceDataFilesForLinkedNotebook(
        const LinkedNotebook & linkedNotebook, ErrorString & errorDescription);

    bool
    checkAndPrepareInsertOrReplaceResourceMetadataWithDataPropertiesQuery();

    bool checkAndPrepareUpdateResourceMetadataWithoutDataPropertiesQuery();
    bool checkAndPrepareInsertOrReplaceNoteResourceQuery();
    bool checkAndPrepareDeleteResourceFromResourceRecognitionTypesQuery();
    bool checkAndPrepareInsertOrReplaceIntoResourceRecognitionDataQuery();
    bool checkAndPrepareDeleteResourceFromResourceAttributesQuery();

    bool
    checkAndPrepareDeleteResourceFromResourceAttributesApplicationDataKeysOnlyQuery();

    bool
    checkAndPrepareDeleteResourceFromResourceAttributesApplicationDataFullMapQuery();

    bool checkAndPrepareInsertOrReplaceResourceAttributesQuery();

    bool
    checkAndPrepareInsertOrReplaceResourceAttributesApplicationDataKeysOnlyQuery();

    bool
    checkAndPrepareInsertOrReplaceResourceAttributesApplicationDataFullMapQuery();

    bool checkAndPrepareResourceCountQuery() const;

    bool insertOrReplaceSavedSearch(
        const SavedSearch & search, ErrorString & errorDescription);

    bool checkAndPrepareInsertOrReplaceSavedSearchQuery();
    bool checkAndPrepareGetSavedSearchCountQuery() const;
    bool checkAndPrepareExpungeSavedSearchQuery();

    bool complementTagsWithNoteLocalUids(
        QList<std::pair<Tag, QStringList>> & tagsWithNoteLocalUids,
        ErrorString & errorDescription) const;

    bool readResourceDataFromFiles(
        Resource & resource, ErrorString & errorDescription) const;

    enum class ReadResourceBinaryDataFromFileStatus
    {
        Success = 0,
        FileNotFound,
        Failure
    };

    ReadResourceBinaryDataFromFileStatus readResourceBinaryDataFromFile(
        const QString & resourceLocalUid, const QString & noteLocalUid,
        const bool isAlternateDataBody, QByteArray & dataBody,
        ErrorString & errorDescription) const;

    void fillResourceFromSqlRecord(
        const QSqlRecord & rec, Resource & resource) const;

    bool fillResourceAttributesFromSqlRecord(
        const QSqlRecord & rec,
        qevercloud::ResourceAttributes & attributes) const;

    bool fillResourceAttributesApplicationDataKeysOnlyFromSqlRecord(
        const QSqlRecord & rec,
        qevercloud::ResourceAttributes & attributes) const;

    bool fillResourceAttributesApplicationDataFullMapFromSqlRecord(
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

    bool fillUserFromSqlRecord(
        const QSqlRecord & rec, User & user,
        ErrorString & errorDescription) const;

    bool fillNoteFromSqlRecord(
        const QSqlRecord & record, Note & note,
        ErrorString & errorDescription) const;

    bool fillSharedNoteFromSqlRecord(
        const QSqlRecord & record, SharedNote & sharedNote,
        ErrorString & errorDescription) const;

    bool fillNoteTagIdFromSqlRecord(
        const QSqlRecord & record, const QString & column,
        QList<std::pair<QString, int>> & tagIdsAndIndices,
        QHash<QString, int> & tagIndexPerId,
        ErrorString & errorDescription) const;

    bool fillNotebookFromSqlRecord(
        const QSqlRecord & record, Notebook & notebook,
        ErrorString & errorDescription) const;

    bool fillSharedNotebookFromSqlRecord(
        const QSqlRecord & record, SharedNotebook & sharedNotebook,
        ErrorString & errorDescription) const;

    bool fillLinkedNotebookFromSqlRecord(
        const QSqlRecord & record, LinkedNotebook & linkedNotebook,
        ErrorString & errorDescription) const;

    bool fillSavedSearchFromSqlRecord(
        const QSqlRecord & rec, SavedSearch & search,
        ErrorString & errorDescription) const;

    bool fillTagFromSqlRecord(
        const QSqlRecord & rec, Tag & tag,
        ErrorString & errorDescription) const;

    QList<Tag> fillTagsFromSqlQuery(
        QSqlQuery & query, ErrorString & errorDescription) const;

    bool findAndSetTagIdsPerNote(
        Note & note, ErrorString & errorDescription) const;

    bool findAndSetResourcesPerNote(
        Note & note, const LocalStorageManager::GetResourceOptions options,
        ErrorString & errorDescription) const;

    void sortSharedNotebooks(Notebook & notebook) const;
    void sortSharedNotes(Note & note) const;

    QList<qevercloud::SharedNotebook> listEnSharedNotebooksPerNotebookGuid(
        const QString & notebookGuid, ErrorString & errorDescription) const;

    bool noteSearchQueryToSQL(
        const NoteSearchQuery & noteSearchQuery, QString & sql,
        ErrorString & errorDescription) const;

    bool noteSearchQueryContentSearchTermsToSQL(
        const NoteSearchQuery & noteSearchQuery, QString & sql,
        ErrorString & errorDescription) const;

    void contentSearchTermToSQLQueryPart(
        QString & frontSearchTermModifier, QString & searchTerm,
        QString & backSearchTermModifier, QString & matchStatement) const;

    bool tagNamesToTagLocalUids(
        const QStringList & tagNames, QStringList & tagLocalUids,
        ErrorString & errorDescription) const;

    bool resourceMimeTypesToResourceLocalUids(
        const QStringList & resourceMimeTypes, QStringList & resourceLocalUids,
        ErrorString & errorDescription) const;

    bool complementResourceNoteIds(
        Resource & resource, ErrorString & errorDescription) const;

    bool partialUpdateNoteResources(
        const QString & noteLocalUid,
        const QList<Resource> & updatedNoteResources,
        const bool UpdateResourceBinaryData, ErrorString & errorDescription);

    template <class T>
    QString listObjectsOptionsToSqlQueryConditions(
        const LocalStorageManager::ListObjectsOptions & flag,
        ErrorString & errorDescription) const;

    template <class T, class TOrderBy>
    QList<T> listObjects(
        const LocalStorageManager::ListObjectsOptions & flag,
        ErrorString & errorDescription, const size_t limit, const size_t offset,
        const TOrderBy & orderBy,
        const LocalStorageManager::OrderDirection & orderDirection,
        const QString & additionalSqlQueryCondition = QString()) const;

    template <class T>
    QString listObjectsGenericSqlQuery() const;

    template <class TOrderBy>
    QString orderByToSqlTableColumn(const TOrderBy & orderBy) const;

    template <class T>
    bool fillObjectsFromSqlQuery(
        QSqlQuery query, QList<T> & objects,
        ErrorString & errorDescription) const;

    template <class T>
    bool fillObjectFromSqlRecord(
        const QSqlRecord & record, T & object,
        ErrorString & errorDescription) const;

    void clearDatabaseFile();

    void clearCachedQueries();

    struct SharedNotebookCompareByIndex
    {
        bool operator()(
            const SharedNotebook & lhs, const SharedNotebook & rhs) const;
    };

    struct SharedNoteCompareByIndex
    {
        bool operator()(const SharedNote & lhs, const SharedNote & rhs) const;
    };

    struct ResourceCompareByIndex
    {
        bool operator()(const Resource & lhs, const Resource & rhs) const;
    };

    struct QStringIntPairCompareByInt
    {
        bool operator()(
            const std::pair<QString, int> & lhs,
            const std::pair<QString, int> & rhs) const;
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
    QVector<QChar> m_preservedAsterisk;
};

} // namespace quentier

#endif // LIB_QUENTIER_LOCAL_STORAGE_LOCAL_STORAGE_MANAGER_PRIVATE_H
