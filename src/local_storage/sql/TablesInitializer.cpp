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

#include "ConnectionPool.h"
#include "ErrorHandling.h"
#include "TablesInitializer.h"

#include <quentier/exception/DatabaseOpeningException.h>
#include <quentier/exception/DatabaseRequestException.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/types/ErrorString.h>

#include <QMutexLocker>
#include <QSqlError>
#include <QSqlQuery>
#include <QtGlobal>

#include <stdexcept>

namespace quentier::local_storage::sql {

void TablesInitializer::initializeTables(QSqlDatabase & databaseConnection)
{
    initializeAuxiliaryTable(databaseConnection);
    initializeUserTables(databaseConnection);
    initializeNotebookTables(databaseConnection);
    initializeNoteTables(databaseConnection);
    initializeResourceTables(databaseConnection);
    initializeTagsTables(databaseConnection);
    initializeSavedSearchTables(databaseConnection);
    initializeExtraTriggers(databaseConnection);
}

void TablesInitializer::initializeAuxiliaryTable(QSqlDatabase & databaseConnection)
{
    QSqlQuery query{databaseConnection};
    bool res = query.exec(
        QStringLiteral("SELECT name FROM sqlite_master WHERE name='Auxiliary'"));

    ENSURE_DB_REQUEST(
        res, query, "local_storage::sql::tables_initializer",
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::tables_initializer",
            "Cannot check the existence of Auxiliary table in the local storage"
            "database"));

    const bool auxiliaryTableExists = query.next();
    QNDEBUG(
        "local_storage:sql:tables_initializer",
        "Auxiliary table "
            << (auxiliaryTableExists ? "already exists" : "doesn't exist yet"));

    if (auxiliaryTableExists) {
        return;
    }

    res = query.exec(
        QStringLiteral("CREATE TABLE Auxiliary("
                        "  lock    CHAR(1) PRIMARY KEY  NOT NULL DEFAULT "
                        "'X' CHECK (lock='X'), "
                        "  version INTEGER              NOT NULL DEFAULT 2"
                        ")"));
    ENSURE_DB_REQUEST(
        res, query, "local_storage::sql::tables_initializer",
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::tables_initializer",
            "Cannot create Auxiliary table in the local storage database"));

    res = query.exec(
        QStringLiteral("INSERT INTO Auxiliary (version) VALUES(2)"));

    ENSURE_DB_REQUEST(
        res, query, "local_storage::sql::tables_initializer",
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::tables_initializer",
            "Cannot set version into Auxiliary table of the local storage "
            "database"));
}

void TablesInitializer::initializeUserTables(QSqlDatabase & databaseConnection)
{
    QSqlQuery query{databaseConnection};

    bool res = query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS Users("
        "  id                           INTEGER PRIMARY KEY NOT NULL UNIQUE, "
        "  username                     TEXT                DEFAULT NULL, "
        "  email                        TEXT                DEFAULT NULL, "
        "  name                         TEXT                DEFAULT NULL, "
        "  timezone                     TEXT                DEFAULT NULL, "
        "  privilege                    INTEGER             DEFAULT NULL, "
        "  serviceLevel                 INTEGER             DEFAULT NULL, "
        "  userCreationTimestamp        INTEGER             DEFAULT NULL, "
        "  userModificationTimestamp    INTEGER             DEFAULT NULL, "
        "  userIsDirty                  INTEGER             NOT NULL, "
        "  userIsLocal                  INTEGER             NOT NULL, "
        "  userDeletionTimestamp        INTEGER             DEFAULT NULL, "
        "  userIsActive                 INTEGER             DEFAULT NULL, "
        "  userShardId                  TEXT                DEFAULT NULL, "
        "  userPhotoUrl                 TEXT                DEFAULT NULL, "
        "  userPhotoLastUpdateTimestamp INTEGER             DEFAULT NULL"
        ")"));

    ENSURE_DB_REQUEST(
        res, query, "local_storage::sql::tables_initializer",
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::tables_initializer",
            "Cannot create Users table in the local storage database"));

    res = query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS UserAttributes("
        "  id REFERENCES Users(id) ON UPDATE CASCADE, "
        "  defaultLocationName        TEXT                  DEFAULT NULL, "
        "  defaultLatitude            REAL                  DEFAULT NULL, "
        "  defaultLongitude           REAL                  DEFAULT NULL, "
        "  preactivation              INTEGER               DEFAULT NULL, "
        "  incomingEmailAddress       TEXT                  DEFAULT NULL, "
        "  comments                   TEXT                  DEFAULT NULL, "
        "  dateAgreedToTermsOfService INTEGER               DEFAULT NULL, "
        "  maxReferrals               INTEGER               DEFAULT NULL, "
        "  referralCount              INTEGER               DEFAULT NULL, "
        "  refererCode                TEXT                  DEFAULT NULL, "
        "  sentEmailDate              INTEGER               DEFAULT NULL, "
        "  sentEmailCount             INTEGER               DEFAULT NULL, "
        "  dailyEmailLimit            INTEGER               DEFAULT NULL, "
        "  emailOptOutDate            INTEGER               DEFAULT NULL, "
        "  partnerEmailOptInDate      INTEGER               DEFAULT NULL, "
        "  preferredLanguage          TEXT                  DEFAULT NULL, "
        "  preferredCountry           TEXT                  DEFAULT NULL, "
        "  clipFullPage               INTEGER               DEFAULT NULL, "
        "  twitterUserName            TEXT                  DEFAULT NULL, "
        "  twitterId                  TEXT                  DEFAULT NULL, "
        "  groupName                  TEXT                  DEFAULT NULL, "
        "  recognitionLanguage        TEXT                  DEFAULT NULL, "
        "  referralProof              TEXT                  DEFAULT NULL, "
        "  educationalDiscount        INTEGER               DEFAULT NULL, "
        "  businessAddress            TEXT                  DEFAULT NULL, "
        "  hideSponsorBilling         INTEGER               DEFAULT NULL, "
        "  useEmailAutoFiling         INTEGER               DEFAULT NULL, "
        "  reminderEmailConfig        INTEGER               DEFAULT NULL, "
        "  emailAddressLastConfirmed  INTEGER               DEFAULT NULL, "
        "  passwordUpdated            INTEGER               DEFAULT NULL, "
        "  salesforcePushEnabled      INTEGER               DEFAULT NULL, "
        "  shouldLogClientEvent       INTEGER               DEFAULT NULL)"));

    ENSURE_DB_REQUEST(
        res, query, "local_storage::sql::tables_initializer",
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::tables_initializer",
            "Cannot create UserAttributes table in the local storage "
            "database"));

    res = query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS UserAttributesViewedPromotions("
        "  id REFERENCES Users(id) ON UPDATE CASCADE, "
        "  promotion               TEXT                    DEFAULT NULL)"));

    ENSURE_DB_REQUEST(
        res, query, "local_storage::sql::tables_initializer",
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::tables_initializer",
            "Cannot create UserAttributesViewedPromotions table in the local "
            "storage database"));

    res = query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS UserAttributesRecentMailedAddresses("
        "  id REFERENCES Users(id) ON UPDATE CASCADE, "
        "  address                 TEXT                    DEFAULT NULL)"));

    ENSURE_DB_REQUEST(
        res, query, "local_storage::sql::tables_initializer",
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::tables_initializer",
            "Cannot create UserAttributesRecentMailedAddresses table in "
            "the local storage database"));

    res = query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS Accounting("
        "  id REFERENCES Users(id) ON UPDATE CASCADE, "
        "  uploadLimitEnd              INTEGER             DEFAULT NULL, "
        "  uploadLimitNextMonth        INTEGER             DEFAULT NULL, "
        "  premiumServiceStatus        INTEGER             DEFAULT NULL, "
        "  premiumOrderNumber          TEXT                DEFAULT NULL, "
        "  premiumCommerceService      TEXT                DEFAULT NULL, "
        "  premiumServiceStart         INTEGER             DEFAULT NULL, "
        "  premiumServiceSKU           TEXT                DEFAULT NULL, "
        "  lastSuccessfulCharge        INTEGER             DEFAULT NULL, "
        "  lastFailedCharge            INTEGER             DEFAULT NULL, "
        "  lastFailedChargeReason      TEXT                DEFAULT NULL, "
        "  nextPaymentDue              INTEGER             DEFAULT NULL, "
        "  premiumLockUntil            INTEGER             DEFAULT NULL, "
        "  updated                     INTEGER             DEFAULT NULL, "
        "  premiumSubscriptionNumber   TEXT                DEFAULT NULL, "
        "  lastRequestedCharge         INTEGER             DEFAULT NULL, "
        "  currency                    TEXT                DEFAULT NULL, "
        "  unitPrice                   INTEGER             DEFAULT NULL, "
        "  unitDiscount                INTEGER             DEFAULT NULL, "
        "  nextChargeDate              INTEGER             DEFAULT NULL, "
        "  availablePoints             INTEGER             DEFAULT NULL)"));

    ENSURE_DB_REQUEST(
        res, query, "local_storage::sql::tables_initializer",
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::tables_initializer",
            "Cannot create Accounting table in the local storage database"));

    res = query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS AccountLimits("
        "  id REFERENCES Users(id) ON UPDATE CASCADE, "
        "  userMailLimitDaily          INTEGER             DEFAULT NULL, "
        "  noteSizeMax                 INTEGER             DEFAULT NULL, "
        "  resourceSizeMax             INTEGER             DEFAULT NULL, "
        "  userLinkedNotebookMax       INTEGER             DEFAULT NULL, "
        "  uploadLimit                 INTEGER             DEFAULT NULL, "
        "  userNoteCountMax            INTEGER             DEFAULT NULL, "
        "  userNotebookCountMax        INTEGER             DEFAULT NULL, "
        "  userTagCountMax             INTEGER             DEFAULT NULL, "
        "  noteTagCountMax             INTEGER             DEFAULT NULL, "
        "  userSavedSearchesMax        INTEGER             DEFAULT NULL, "
        "  noteResourceCountMax        INTEGER             DEFAULT NULL)"));

    ENSURE_DB_REQUEST(
        res, query, "local_storage::sql::tables_initializer",
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::tables_initializer",
            "Cannot create AccountLimits table in the local storage database"));

    res = query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS BusinessUserInfo("
        "  id REFERENCES Users(id) ON UPDATE CASCADE, "
        "  businessId              INTEGER                 DEFAULT NULL, "
        "  businessName            TEXT                    DEFAULT NULL, "
        "  role                    INTEGER                 DEFAULT NULL, "
        "  businessInfoEmail       TEXT                    DEFAULT NULL)"));

    ENSURE_DB_REQUEST(
        res, query, "local_storage::sql::tables_initializer",
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::tables_initializer",
            "Cannot create BusinessUserInfo table in the local storage "
            "database"));

    res = query.exec(QStringLiteral(
        "CREATE TRIGGER IF NOT EXISTS on_user_delete_trigger "
        "BEFORE DELETE ON Users "
        "BEGIN "
        "DELETE FROM UserAttributes WHERE id=OLD.id; "
        "DELETE FROM UserAttributesViewedPromotions WHERE id=OLD.id; "
        "DELETE FROM UserAttributesRecentMailedAddresses WHERE id=OLD.id; "
        "DELETE FROM Accounting WHERE id=OLD.id; "
        "DELETE FROM AccountLimits WHERE id=OLD.id; "
        "DELETE FROM BusinessUserInfo WHERE id=OLD.id; "
        "END"));

    ENSURE_DB_REQUEST(
        res, query, "local_storage::sql::tables_initializer",
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::tables_initializer",
            "Cannot create trigger on user deletion in the local storage "
            "database"));
}

void TablesInitializer::initializeNotebookTables(
    QSqlDatabase & databaseConnection)
{
    QSqlQuery query{databaseConnection};

    bool res = query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS LinkedNotebooks("
        "  guid                            TEXT PRIMARY KEY  NOT NULL UNIQUE, "
        "  updateSequenceNumber            INTEGER           DEFAULT NULL, "
        "  isDirty                         INTEGER           DEFAULT NULL, "
        "  shareName                       TEXT              DEFAULT NULL, "
        "  username                        TEXT              DEFAULT NULL, "
        "  shardId                         TEXT              DEFAULT NULL, "
        "  sharedNotebookGlobalId          TEXT              DEFAULT NULL, "
        "  uri                             TEXT              DEFAULT NULL, "
        "  noteStoreUrl                    TEXT              DEFAULT NULL, "
        "  webApiUrlPrefix                 TEXT              DEFAULT NULL, "
        "  stack                           TEXT              DEFAULT NULL, "
        "  businessId                      INTEGER           DEFAULT NULL"
        ")"));

    ENSURE_DB_REQUEST(
        res, query, "local_storage::sql::tables_initializer",
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::tables_initializer",
            "Cannot create LinkedNotebooks table in the local storage "
            "database"));

    res = query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS Notebooks("
        "  localUid                        TEXT PRIMARY KEY  NOT NULL UNIQUE, "
        "  guid                            TEXT              DEFAULT NULL "
        "UNIQUE, "
        "  linkedNotebookGuid REFERENCES LinkedNotebooks(guid) ON UPDATE "
        "CASCADE, "
        "  updateSequenceNumber            INTEGER           DEFAULT NULL, "
        "  notebookName                    TEXT              DEFAULT NULL, "
        "  notebookNameUpper               TEXT              DEFAULT NULL, "
        "  creationTimestamp               INTEGER           DEFAULT NULL, "
        "  modificationTimestamp           INTEGER           DEFAULT NULL, "
        "  isDirty                         INTEGER           NOT NULL, "
        "  isLocal                         INTEGER           NOT NULL, "
        "  isDefault                       INTEGER           DEFAULT NULL "
        "UNIQUE, "
        "  isLastUsed                      INTEGER           DEFAULT NULL "
        "UNIQUE, "
        "  isFavorited                     INTEGER           DEFAULT NULL, "
        "  publishingUri                   TEXT              DEFAULT NULL, "
        "  publishingNoteSortOrder         INTEGER           DEFAULT NULL, "
        "  publishingAscendingSort         INTEGER           DEFAULT NULL, "
        "  publicDescription               TEXT              DEFAULT NULL, "
        "  isPublished                     INTEGER           DEFAULT NULL, "
        "  stack                           TEXT              DEFAULT NULL, "
        "  businessNotebookDescription     TEXT              DEFAULT NULL, "
        "  businessNotebookPrivilegeLevel  INTEGER           DEFAULT NULL, "
        "  businessNotebookIsRecommended   INTEGER           DEFAULT NULL, "
        "  contactId                       INTEGER           DEFAULT NULL, "
        "  recipientReminderNotifyEmail    INTEGER           DEFAULT NULL, "
        "  recipientReminderNotifyInApp    INTEGER           DEFAULT NULL, "
        "  recipientInMyList               INTEGER           DEFAULT NULL, "
        "  recipientStack                  TEXT              DEFAULT NULL, "
        "  UNIQUE(localUid, guid), "
        "  UNIQUE(notebookNameUpper, linkedNotebookGuid) "
        ")"));

    ENSURE_DB_REQUEST(
        res, query, "local_storage::sql::tables_initializer",
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::tables_initializer",
            "Cannot create Notebooks table in the local storage database"));

    res = query.exec(
        QStringLiteral("CREATE VIRTUAL TABLE IF NOT EXISTS NotebookFTS "
                       "USING FTS4(content=\"Notebooks\", "
                       "localUid, guid, notebookName)"));

    ENSURE_DB_REQUEST(
        res, query, "local_storage::sql::tables_initializer",
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::tables_initializer",
            "Cannot create NotebookFTS table in the local storage database"));

    res = query.exec(
        QStringLiteral("CREATE TRIGGER IF NOT EXISTS "
                       "NotebookFTS_BeforeDeleteTrigger "
                       "BEFORE DELETE ON Notebooks "
                       "BEGIN "
                       "DELETE FROM NotebookFTS WHERE localUid=old.localUid; "
                       "END"));

    ENSURE_DB_REQUEST(
        res, query, "local_storage::sql::tables_initializer",
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::tables_initializer",
            "Cannot create NotebookFTS before delete trigger in the local "
            "storage database"));

    res = query.exec(QStringLiteral(
        "CREATE TRIGGER IF NOT EXISTS "
        "NotebookFTS_AfterInsertTrigger "
        "AFTER INSERT ON Notebooks "
        "BEGIN "
        "INSERT INTO NotebookFTS(NotebookFTS) VALUES('rebuild'); "
        "END"));

    ENSURE_DB_REQUEST(
        res, query, "local_storage::sql::tables_initializer",
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::tables_initializer",
            "Cannot create NotebookFTS after insert trigger in the local "
            "storage database"));

    res = query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS NotebookRestrictions("
        "  localUid REFERENCES Notebooks(localUid) ON UPDATE CASCADE, "
        "  noReadNotes                 INTEGER      DEFAULT NULL, "
        "  noCreateNotes               INTEGER      DEFAULT NULL, "
        "  noUpdateNotes               INTEGER      DEFAULT NULL, "
        "  noExpungeNotes              INTEGER      DEFAULT NULL, "
        "  noShareNotes                INTEGER      DEFAULT NULL, "
        "  noEmailNotes                INTEGER      DEFAULT NULL, "
        "  noSendMessageToRecipients   INTEGER      DEFAULT NULL, "
        "  noUpdateNotebook            INTEGER      DEFAULT NULL, "
        "  noExpungeNotebook           INTEGER      DEFAULT NULL, "
        "  noSetDefaultNotebook        INTEGER      DEFAULT NULL, "
        "  noSetNotebookStack          INTEGER      DEFAULT NULL, "
        "  noPublishToPublic           INTEGER      DEFAULT NULL, "
        "  noPublishToBusinessLibrary  INTEGER      DEFAULT NULL, "
        "  noCreateTags                INTEGER      DEFAULT NULL, "
        "  noUpdateTags                INTEGER      DEFAULT NULL, "
        "  noExpungeTags               INTEGER      DEFAULT NULL, "
        "  noSetParentTag              INTEGER      DEFAULT NULL, "
        "  noCreateSharedNotebooks     INTEGER      DEFAULT NULL, "
        "  noShareNotesWithBusiness    INTEGER      DEFAULT NULL, "
        "  noRenameNotebook            INTEGER      DEFAULT NULL, "
        "  updateWhichSharedNotebookRestrictions    INTEGER     DEFAULT NULL, "
        "  expungeWhichSharedNotebookRestrictions   INTEGER     DEFAULT NULL "
        ")"));

    ENSURE_DB_REQUEST(
        res, query, "local_storage::sql::tables_initializer",
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::tables_initializer",
            "Cannot create NotebookRestrictions table in the local storage "
            "database"));

    res = query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS SharedNotebooks("
        "  sharedNotebookShareId                      INTEGER PRIMARY KEY   "
        "NOT NULL UNIQUE, "
        "  sharedNotebookUserId                       INTEGER    DEFAULT NULL, "
        "  sharedNotebookNotebookGuid REFERENCES Notebooks(guid) ON UPDATE "
        "CASCADE, "
        "  sharedNotebookEmail                        TEXT       DEFAULT NULL, "
        "  sharedNotebookIdentityId                   INTEGER    DEFAULT NULL, "
        "  sharedNotebookCreationTimestamp            INTEGER    DEFAULT NULL, "
        "  sharedNotebookModificationTimestamp        INTEGER    DEFAULT NULL, "
        "  sharedNotebookGlobalId                     TEXT       DEFAULT NULL, "
        "  sharedNotebookUsername                     TEXT       DEFAULT NULL, "
        "  sharedNotebookPrivilegeLevel               INTEGER    DEFAULT NULL, "
        "  sharedNotebookRecipientReminderNotifyEmail INTEGER    DEFAULT NULL, "
        "  sharedNotebookRecipientReminderNotifyInApp INTEGER    DEFAULT NULL, "
        "  sharedNotebookSharerUserId                 INTEGER    DEFAULT NULL, "
        "  sharedNotebookRecipientUsername            TEXT       DEFAULT NULL, "
        "  sharedNotebookRecipientUserId              INTEGER    DEFAULT NULL, "
        "  sharedNotebookRecipientIdentityId          INTEGER    DEFAULT NULL, "
        "  sharedNotebookAssignmentTimestamp          INTEGER    DEFAULT NULL, "
        "  indexInNotebook                            INTEGER    DEFAULT NULL, "
        "  UNIQUE(sharedNotebookShareId, sharedNotebookNotebookGuid) ON "
        "CONFLICT REPLACE)"));

    ENSURE_DB_REQUEST(
        res, query, "local_storage::sql::tables_initializer",
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::tables_initializer",
            "Cannot create SharedNotebooks table in the local storage "
            "database"));
}

void TablesInitializer::initializeNoteTables(QSqlDatabase & databaseConnection)
{
    QSqlQuery query{databaseConnection};

    bool res = query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS Notes("
        "  localUid                        TEXT PRIMARY KEY     NOT NULL "
        "UNIQUE, "
        "  guid                            TEXT                 DEFAULT NULL "
        "UNIQUE, "
        "  updateSequenceNumber            INTEGER              DEFAULT NULL, "
        "  isDirty                         INTEGER              NOT NULL, "
        "  isLocal                         INTEGER              NOT NULL, "
        "  isFavorited                     INTEGER              NOT NULL, "
        "  title                           TEXT                 DEFAULT NULL, "
        "  titleNormalized                 TEXT                 DEFAULT NULL, "
        "  content                         TEXT                 DEFAULT NULL, "
        "  contentLength                   INTEGER              DEFAULT NULL, "
        "  contentHash                     TEXT                 DEFAULT NULL, "
        "  contentPlainText                TEXT                 DEFAULT NULL, "
        "  contentListOfWords              TEXT                 DEFAULT NULL, "
        "  contentContainsFinishedToDo     INTEGER              DEFAULT NULL, "
        "  contentContainsUnfinishedToDo   INTEGER              DEFAULT NULL, "
        "  contentContainsEncryption       INTEGER              DEFAULT NULL, "
        "  creationTimestamp               INTEGER              DEFAULT NULL, "
        "  modificationTimestamp           INTEGER              DEFAULT NULL, "
        "  deletionTimestamp               INTEGER              DEFAULT NULL, "
        "  isActive                        INTEGER              DEFAULT NULL, "
        "  hasAttributes                   INTEGER              NOT NULL, "
        "  thumbnail                       BLOB                 DEFAULT NULL, "
        "  notebookLocalUid REFERENCES Notebooks(localUid) ON UPDATE CASCADE, "
        "  notebookGuid REFERENCES Notebooks(guid) ON UPDATE CASCADE, "
        "  subjectDate                     INTEGER              DEFAULT NULL, "
        "  latitude                        REAL                 DEFAULT NULL, "
        "  longitude                       REAL                 DEFAULT NULL, "
        "  altitude                        REAL                 DEFAULT NULL, "
        "  author                          TEXT                 DEFAULT NULL, "
        "  source                          TEXT                 DEFAULT NULL, "
        "  sourceURL                       TEXT                 DEFAULT NULL, "
        "  sourceApplication               TEXT                 DEFAULT NULL, "
        "  shareDate                       INTEGER              DEFAULT NULL, "
        "  reminderOrder                   INTEGER              DEFAULT NULL, "
        "  reminderDoneTime                INTEGER              DEFAULT NULL, "
        "  reminderTime                    INTEGER              DEFAULT NULL, "
        "  placeName                       TEXT                 DEFAULT NULL, "
        "  contentClass                    TEXT                 DEFAULT NULL, "
        "  lastEditedBy                    TEXT                 DEFAULT NULL, "
        "  creatorId                       INTEGER              DEFAULT NULL, "
        "  lastEditorId                    INTEGER              DEFAULT NULL, "
        "  sharedWithBusiness              INTEGER              DEFAULT NULL, "
        "  conflictSourceNoteGuid          TEXT                 DEFAULT NULL, "
        "  noteTitleQuality                INTEGER              DEFAULT NULL, "
        "  applicationDataKeysOnly         TEXT                 DEFAULT NULL, "
        "  applicationDataKeysMap          TEXT                 DEFAULT NULL, "
        "  applicationDataValues           TEXT                 DEFAULT NULL, "
        "  classificationKeys              TEXT                 DEFAULT NULL, "
        "  classificationValues            TEXT                 DEFAULT NULL, "
        "  UNIQUE(localUid, guid)"
        ")"));

    ENSURE_DB_REQUEST(
        res, query, "local_storage::sql::tables_initializer",
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::tables_initializer",
            "Cannot create Notes table in the local storage database"));

    res = query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS SharedNotes("
        "  sharedNoteNoteGuid REFERENCES Notes(guid) ON UPDATE CASCADE, "
        "  sharedNoteSharerUserId               INTEGER DEFAULT NULL, "
        "  sharedNoteRecipientIdentityId        INTEGER DEFAULT NULL UNIQUE, "
        "  sharedNoteRecipientContactName       TEXT    DEFAULT NULL, "
        "  sharedNoteRecipientContactId         TEXT    DEFAULT NULL, "
        "  sharedNoteRecipientContactType       INTEGER DEFAULT NULL, "
        "  sharedNoteRecipientContactPhotoUrl   TEXT    DEFAULT NULL, "
        "  sharedNoteRecipientContactPhotoLastUpdated   INTEGER DEFAULT NULL, "
        "  sharedNoteRecipientContactMessagingPermit    BLOB    DEFAULT NULL, "
        "  sharedNoteRecipientContactMessagingPermitExpires "
        "INTEGER DEFAULT NULL, "
        "  sharedNoteRecipientUserId            INTEGER DEFAULT NULL, "
        "  sharedNoteRecipientDeactivated       INTEGER DEFAULT NULL, "
        "  sharedNoteRecipientSameBusiness      INTEGER DEFAULT NULL, "
        "  sharedNoteRecipientBlocked           INTEGER DEFAULT NULL, "
        "  sharedNoteRecipientUserConnected     INTEGER DEFAULT NULL, "
        "  sharedNoteRecipientEventId           INTEGER DEFAULT NULL, "
        "  sharedNotePrivilegeLevel             INTEGER DEFAULT NULL, "
        "  sharedNoteCreationTimestamp          INTEGER DEFAULT NULL, "
        "  sharedNoteModificationTimestamp      INTEGER DEFAULT NULL, "
        "  sharedNoteAssignmentTimestamp        INTEGER DEFAULT NULL, "
        "  indexInNote                          INTEGER DEFAULT NULL, "
        "  UNIQUE(sharedNoteNoteGuid, sharedNoteRecipientIdentityId) "
        "ON CONFLICT REPLACE)"));

    ENSURE_DB_REQUEST(
        res, query, "local_storage::sql::tables_initializer",
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::tables_initializer",
            "Cannot create SharedNotes table in the local storage database"));

    res = query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS NoteRestrictions("
        "  noteLocalUid REFERENCES Notes(localUid) ON UPDATE CASCADE, "
        "  noUpdateNoteTitle                INTEGER         DEFAULT NULL, "
        "  noUpdateNoteContent              INTEGER         DEFAULT NULL, "
        "  noEmailNote                      INTEGER         DEFAULT NULL, "
        "  noShareNote                      INTEGER         DEFAULT NULL, "
        "  noShareNotePublicly              INTEGER         DEFAULT NULL)"));

    ENSURE_DB_REQUEST(
        res, query, "local_storage::sql::tables_initializer",
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::tables_initializer",
            "Cannot create NoteRestrictions table in the local storage "
            "database"));

    // clang-format off
    res = query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS "
                                  "NoteRestrictionsByNoteLocalUid ON "
                                  "NoteRestrictions(noteLocalUid)"));
    // clang-format on

    ENSURE_DB_REQUEST(
        res, query, "local_storage::sql::tables_initializer",
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::tables_initializer",
            "Cannot create NoteRestrictionsByNoteLocalUid index in the local "
            "storage database"));

    res = query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS NoteLimits("
        "  noteLocalUid REFERENCES Notes(localUid) ON UPDATE CASCADE, "
        "  noteResourceCountMax             INTEGER         DEFAULT NULL, "
        "  uploadLimit                      INTEGER         DEFAULT NULL, "
        "  resourceSizeMax                  INTEGER         DEFAULT NULL, "
        "  noteSizeMax                      INTEGER         DEFAULT NULL, "
        "  uploaded                         INTEGER         DEFAULT NULL)"));

    ENSURE_DB_REQUEST(
        res, query, "local_storage::sql::tables_initializer",
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::tables_initializer",
            "Cannot create NoteLimits table in the local storage database"));

    // clang-format off
    res = query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS NotesNotebooks "
                                    "ON Notes(notebookLocalUid)"));
    // clang-format on

    ENSURE_DB_REQUEST(
        res, query, "local_storage::sql::tables_initializer",
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::tables_initializer",
            "Cannot create NotesNotebooks index in the local storage "
            "database"));

    res = query.exec(
        QStringLiteral("CREATE VIRTUAL TABLE IF NOT EXISTS NoteFTS "
                       "USING FTS4(content=\"Notes\", localUid, "
                       "titleNormalized, contentListOfWords, "
                       "contentContainsFinishedToDo, "
                       "contentContainsUnfinishedToDo, "
                       "contentContainsEncryption, creationTimestamp, "
                       "modificationTimestamp, isActive, "
                       "notebookLocalUid, notebookGuid, subjectDate, "
                       "latitude, longitude, altitude, author, source, "
                       "sourceApplication, reminderOrder, reminderDoneTime, "
                       "reminderTime, placeName, contentClass, "
                       "applicationDataKeysOnly, "
                       "applicationDataKeysMap, applicationDataValues)"));

    ENSURE_DB_REQUEST(
        res, query, "local_storage::sql::tables_initializer",
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::tables_initializer",
            "Cannot create NoteFTS table in the local storage database"));

    res = query.exec(
        QStringLiteral("CREATE TRIGGER IF NOT EXISTS "
                       "NoteFTS_BeforeDeleteTrigger "
                       "BEFORE DELETE ON Notes "
                       "BEGIN "
                       "DELETE FROM NoteFTS WHERE localUid=old.localUid; "
                       "END"));

    ENSURE_DB_REQUEST(
        res, query, "local_storage::sql::tables_initializer",
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::tables_initializer",
            "Cannot create NoteFTS before delete trigger in the local storage "
            "database"));

    res = query.exec(
        QStringLiteral("CREATE TRIGGER IF NOT EXISTS "
                       "NoteFTS_AfterInsertTrigger "
                       "AFTER INSERT ON Notes "
                       "BEGIN "
                       "INSERT INTO NoteFTS(NoteFTS) VALUES('rebuild'); "
                       "END"));

    ENSURE_DB_REQUEST(
        res, query, "local_storage::sql::tables_initializer",
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::tables_initializer",
            "Cannot create NoteFTS after insert trigger in the local storage "
            "database"));

    res = query.exec(
        QStringLiteral("CREATE TRIGGER IF NOT EXISTS "
                       "on_notebook_delete_trigger "
                       "BEFORE DELETE ON Notebooks "
                       "BEGIN "
                       "DELETE FROM NotebookRestrictions WHERE "
                       "NotebookRestrictions.localUid=OLD.localUid; "
                       "DELETE FROM SharedNotebooks WHERE "
                       "SharedNotebooks.sharedNotebookNotebookGuid=OLD.guid; "
                       "DELETE FROM Notes WHERE "
                       "Notes.notebookLocalUid=OLD.localUid; "
                       "END"));

    ENSURE_DB_REQUEST(
        res, query, "local_storage::sql::tables_initializer",
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::tables_initializer",
            "Cannot create on notebook delete trigger in the local storage "
            "database"));
}

void TablesInitializer::initializeResourceTables(
    QSqlDatabase & databaseConnection)
{
    QSqlQuery query{databaseConnection};

    bool res = query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS Resources("
        "  resourceLocalUid                TEXT PRIMARY KEY NOT NULL UNIQUE, "
        "  resourceGuid                    TEXT         DEFAULT NULL UNIQUE, "
        "  noteLocalUid REFERENCES Notes(localUid) ON UPDATE CASCADE, "
        "  noteGuid REFERENCES Notes(guid) ON UPDATE CASCADE, "
        "  resourceUpdateSequenceNumber    INTEGER          DEFAULT NULL, "
        "  resourceIsDirty                 INTEGER          NOT NULL, "
        "  dataSize                        INTEGER          DEFAULT NULL, "
        "  dataHash                        TEXT             DEFAULT NULL, "
        "  mime                            TEXT             DEFAULT NULL, "
        "  width                           INTEGER          DEFAULT NULL, "
        "  height                          INTEGER          DEFAULT NULL, "
        "  recognitionDataBody             TEXT             DEFAULT NULL, "
        "  recognitionDataSize             INTEGER          DEFAULT NULL, "
        "  recognitionDataHash             TEXT             DEFAULT NULL, "
        "  alternateDataSize               INTEGER          DEFAULT NULL, "
        "  alternateDataHash               TEXT             DEFAULT NULL, "
        "  resourceIndexInNote             INTEGER          DEFAULT NULL, "
        "  UNIQUE(resourceLocalUid, resourceGuid)"
        ")"));

    ENSURE_DB_REQUEST(
        res, query, "local_storage::sql::tables_initializer",
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::tables_initializer",
            "Cannot create Resources table in the local storage database"));

    res = query.exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS ResourceMimeIndex ON Resources(mime)"));

    ENSURE_DB_REQUEST(
        res, query, "local_storage::sql::tables_initializer",
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::tables_initializer",
            "Cannot create ResourcesMimeIndex index in the local storage "
            "database"));

    res = query.exec(
        QStringLiteral("CREATE TABLE IF NOT EXISTS ResourceRecognitionData("
                       "  resourceLocalUid REFERENCES "
                       "Resources(resourceLocalUid) ON UPDATE CASCADE, "
                       "  noteLocalUid REFERENCES Notes(localUid)              "
                       "   ON UPDATE CASCADE, "
                       "  recognitionData                 TEXT                 "
                       "   DEFAULT NULL)"));

    ENSURE_DB_REQUEST(
        res, query, "local_storage::sql::tables_initializer",
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::tables_initializer",
            "Cannot create ResourceRecognitionData table in the local storage "
            "database"));

    res = query.exec(
        QStringLiteral("CREATE INDEX IF NOT EXISTS "
                       "ResourceRecognitionDataIndex "
                       "ON ResourceRecognitionData(recognitionData)"));

    ENSURE_DB_REQUEST(
        res, query, "local_storage::sql::tables_initializer",
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::tables_initializer",
            "Cannot create ResourceRecognitionDataIndex index in the local "
            "storage database"));

    res = query.exec(
        QStringLiteral("CREATE VIRTUAL TABLE IF NOT EXISTS "
                       "ResourceRecognitionDataFTS USING FTS4"
                       "(content=\"ResourceRecognitionData\", "
                       "resourceLocalUid, noteLocalUid, recognitionData)"));

    ENSURE_DB_REQUEST(
        res, query, "local_storage::sql::tables_initializer",
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::tables_initializer",
            "Cannot create ResourceRecognitionDataFTS table in the local "
            "storage database"));

    res = query.exec(
        QStringLiteral("CREATE TRIGGER IF NOT EXISTS "
                       "ResourceRecognitionDataFTS_BeforeDeleteTrigger "
                       "BEFORE DELETE ON ResourceRecognitionData "
                       "BEGIN "
                       "DELETE FROM ResourceRecognitionDataFTS "
                       "WHERE recognitionData=old.recognitionData; "
                       "END"));

    ENSURE_DB_REQUEST(
        res, query, "local_storage::sql::tables_initializer",
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::tables_initializer",
            "Cannot create ResourceRecognitionDataFTS before delete trigger in "
            "the local storage database"));

    res = query.exec(
        QStringLiteral("CREATE TRIGGER IF NOT EXISTS "
                       "ResourceRecognitionDataFTS_AfterInsertTrigger "
                       "AFTER INSERT ON ResourceRecognitionData "
                       "BEGIN "
                       "INSERT INTO ResourceRecognitionDataFTS("
                       "ResourceRecognitionDataFTS) VALUES('rebuild'); "
                       "END"));

    ENSURE_DB_REQUEST(
        res, query, "local_storage::sql::tables_initializer",
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::tables_initializer",
            "Cannot create ResourceRecognitionDataFTS after insert trigger in "
            "the local storage database"));

    res = query.exec(
        QStringLiteral("CREATE VIRTUAL TABLE IF NOT EXISTS "
                       "ResourceMimeFTS USING FTS4(content=\"Resources\", "
                       "resourceLocalUid, mime)"));

    ENSURE_DB_REQUEST(
        res, query, "local_storage::sql::tables_initializer",
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::tables_initializer",
            "Cannot create ResourceMimeFTS virtual table in the local storage "
            "database"));

    res = query.exec(
        QStringLiteral("CREATE TRIGGER IF NOT EXISTS "
                       "ResourceMimeFTS_BeforeDeleteTrigger "
                       "BEFORE DELETE ON Resources "
                       "BEGIN "
                       "DELETE FROM ResourceMimeFTS WHERE mime=old.mime; "
                       "END"));

    ENSURE_DB_REQUEST(
        res, query, "local_storage::sql::tables_initializer",
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::tables_initializer",
            "Cannot create ResourceMimeFTS before delete trigger in the local "
            "storage database"));

    res = query.exec(
        QStringLiteral("CREATE TRIGGER IF NOT EXISTS "
                       "ResourceMimeFTS_AfterInsertTrigger "
                       "AFTER INSERT ON Resources "
                       "BEGIN "
                       "INSERT INTO ResourceMimeFTS(ResourceMimeFTS) "
                       "VALUES('rebuild'); "
                       "END"));

    ENSURE_DB_REQUEST(
        res, query, "local_storage::sql::tables_initializer",
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::tables_initializer",
            "Cannot create ResourceMimeFTS after insert trigger in the local "
            "storage database"));

    res = query.exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS ResourceNote ON Resources(noteLocalUid)"));

    ENSURE_DB_REQUEST(
        res, query, "local_storage::sql::tables_initializer",
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::tables_initializer",
            "Cannot create ResourceNote index in the local storage database"));

    res = query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS ResourceAttributes("
        "  resourceLocalUid REFERENCES Resources(resourceLocalUid) ON UPDATE "
        "CASCADE, "
        "  resourceSourceURL       TEXT                DEFAULT NULL, "
        "  timestamp               INTEGER             DEFAULT NULL, "
        "  resourceLatitude        REAL                DEFAULT NULL, "
        "  resourceLongitude       REAL                DEFAULT NULL, "
        "  resourceAltitude        REAL                DEFAULT NULL, "
        "  cameraMake              TEXT                DEFAULT NULL, "
        "  cameraModel             TEXT                DEFAULT NULL, "
        "  clientWillIndex         INTEGER             DEFAULT NULL, "
        "  fileName                TEXT                DEFAULT NULL, "
        "  attachment              INTEGER             DEFAULT NULL, "
        "  UNIQUE(resourceLocalUid) "
        ")"));

    ENSURE_DB_REQUEST(
        res, query, "local_storage::sql::tables_initializer",
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::tables_initializer",
            "Cannot create ResourceAttributes table in the local storage "
            "database"));

    res = query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS ResourceAttributesApplicationDataKeysOnly("
        "  resourceLocalUid REFERENCES Resources(resourceLocalUid) ON UPDATE "
        "CASCADE, "
        "  resourceKey             TEXT                DEFAULT NULL, "
        "  UNIQUE(resourceLocalUid, resourceKey)"
        ")"));

    ENSURE_DB_REQUEST(
        res, query, "local_storage::sql::tables_initializer",
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::tables_initializer",
            "Cannot create ResourceAttributesApplicationDataKeysOnly table in "
            "the local storage database"));

    res = query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS ResourceAttributesApplicationDataFullMap("
        "  resourceLocalUid REFERENCES Resources(resourceLocalUid) ON UPDATE "
        "CASCADE, "
        "  resourceMapKey          TEXT                DEFAULT NULL, "
        "  resourceValue           TEXT                DEFAULT NULL, "
        "  UNIQUE(resourceLocalUid, resourceMapKey) ON CONFLICT REPLACE"
        ")"));

    ENSURE_DB_REQUEST(
        res, query, "local_storage::sql::tables_initializer",
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::tables_initializer",
            "Cannot create ResourceAttributesApplicationDataFullMap table in "
            "the local storage database"));

    res = query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS NoteResources("
        "  localNote     REFERENCES Notes(localUid)             ON UPDATE "
        "CASCADE, "
        "  note          REFERENCES Notes(guid)                 ON UPDATE "
        "CASCADE, "
        "  localResource REFERENCES Resources(resourceLocalUid) ON UPDATE "
        "CASCADE, "
        "  resource      REFERENCES Resources(resourceGuid)     ON UPDATE "
        "CASCADE, "
        "  UNIQUE(localNote, localResource) ON CONFLICT REPLACE)"));

    ENSURE_DB_REQUEST(
        res, query, "local_storage::sql::tables_initializer",
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::tables_initializer",
            "Cannot create NoteResources table in the local storage database"));

    res = query.exec(
        QStringLiteral("CREATE INDEX IF NOT EXISTS NoteResourcesNote ON "
                       "NoteResources(localNote)"));

    ENSURE_DB_REQUEST(
        res, query, "local_storage::sql::tables_initializer",
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::tables_initializer",
            "Cannot create NoteResourcesNote index in the local storage "
            "database"));
}

void TablesInitializer::initializeTagsTables(QSqlDatabase & databaseConnection)
{
    QSqlQuery query{databaseConnection};

    bool res = query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS Tags("
        "  localUid              TEXT PRIMARY KEY     NOT NULL UNIQUE, "
        "  guid                  TEXT                 DEFAULT NULL UNIQUE, "
        "  linkedNotebookGuid REFERENCES LinkedNotebooks(guid) ON UPDATE "
        "CASCADE, "
        "  updateSequenceNumber  INTEGER              DEFAULT NULL, "
        "  name                  TEXT                 DEFAULT NULL, "
        "  nameLower             TEXT                 DEFAULT NULL, "
        "  parentGuid REFERENCES Tags(guid)           ON UPDATE CASCADE "
        "DEFAULT NULL, "
        "  parentLocalUid REFERENCES Tags(localUid)   ON UPDATE CASCADE "
        "DEFAULT NULL, "
        "  isDirty               INTEGER              NOT NULL, "
        "  isLocal               INTEGER              NOT NULL, "
        "  isFavorited           INTEGER              NOT NULL, "
        "  UNIQUE(localUid, guid), "
        "  UNIQUE(nameLower, linkedNotebookGuid) "
        ")"));

    ENSURE_DB_REQUEST(
        res, query, "local_storage::sql::tables_initializer",
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::tables_initializer",
            "Cannot create Tags table in the local storage database"));

    res = query.exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS TagNameUpperIndex ON Tags(nameLower)"));

    ENSURE_DB_REQUEST(
        res, query, "local_storage::sql::tables_initializer",
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::tables_initializer",
            "Cannot create TagNameUpperIndex index in the local storage "
            "database"));

    res = query.exec(QStringLiteral(
        "CREATE VIRTUAL TABLE IF NOT EXISTS TagFTS "
        "USING FTS4(content=\"Tags\", localUid, guid, nameLower)"));

    ENSURE_DB_REQUEST(
        res, query, "local_storage::sql::tables_initializer",
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::tables_initializer",
            "Cannot create TagFTS virtual table in the local storage "
            "database"));

    res = query.exec(
        QStringLiteral("CREATE TRIGGER IF NOT EXISTS "
                       "TagFTS_BeforeDeleteTrigger "
                       "BEFORE DELETE ON Tags "
                       "BEGIN "
                       "DELETE FROM TagFTS WHERE localUid=old.localUid; "
                       "END"));

    ENSURE_DB_REQUEST(
        res, query, "local_storage::sql::tables_initializer",
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::tables_initializer",
            "Cannot create TagFTS before delete trigger in the local storage "
            "database"));

    res = query.exec(
        QStringLiteral("CREATE TRIGGER IF NOT EXISTS "
                       "TagFTS_AfterInsertTrigger AFTER INSERT ON Tags "
                       "BEGIN "
                       "INSERT INTO TagFTS(TagFTS) VALUES('rebuild'); "
                       "END"));

    ENSURE_DB_REQUEST(
        res, query, "local_storage::sql::tables_initializer",
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::tables_initializer",
            "Cannot create TagFTS after insert trigger in the local storage "
            "database"));

    // clang-format off
    res = query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS TagsSearchName "
                                    "ON Tags(nameLower)"));
    // clang-format on

    ENSURE_DB_REQUEST(
        res, query, "local_storage::sql::tables_initializer",
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::tables_initializer",
            "Cannot create TagSearchName index in the local storage database"));

    res = query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS NoteTags("
        "  localNote REFERENCES Notes(localUid) ON UPDATE CASCADE, "
        "  note REFERENCES Notes(guid)          ON UPDATE CASCADE, "
        "  localTag REFERENCES Tags(localUid)   ON UPDATE CASCADE, "
        "  tag  REFERENCES Tags(guid)           ON UPDATE CASCADE, "
        "  tagIndexInNote        INTEGER        DEFAULT NULL, "
        "  UNIQUE(localNote, localTag) ON CONFLICT REPLACE"
        ")"));

    ENSURE_DB_REQUEST(
        res, query, "local_storage::sql::tables_initializer",
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::tables_initializer",
            "Cannot create NoteTags table in the local storage database"));

    // clang-format off
    res = query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS NoteTagsNote "
                                    "ON NoteTags(localNote)"));
    // clang-format on

    ENSURE_DB_REQUEST(
        res, query, "local_storage::sql::tables_initializer",
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::tables_initializer",
            "Cannot create NoteTagsNote index in the local storage database"));
}

void TablesInitializer::initializeSavedSearchTables(
    QSqlDatabase & databaseConnection)
{
    QSqlQuery query{databaseConnection};

    bool res = query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS SavedSearches("
        "  localUid                        TEXT PRIMARY KEY    NOT NULL "
        "UNIQUE, "
        "  guid                            TEXT                DEFAULT NULL "
        "UNIQUE, "
        "  name                            TEXT                DEFAULT NULL, "
        "  nameLower                       TEXT                DEFAULT NULL "
        "UNIQUE, "
        "  query                           TEXT                DEFAULT NULL, "
        "  format                          INTEGER             DEFAULT NULL, "
        "  updateSequenceNumber            INTEGER             DEFAULT NULL, "
        "  isDirty                         INTEGER             NOT NULL, "
        "  isLocal                         INTEGER             NOT NULL, "
        "  includeAccount                  INTEGER             DEFAULT NULL, "
        "  includePersonalLinkedNotebooks  INTEGER             DEFAULT NULL, "
        "  includeBusinessLinkedNotebooks  INTEGER             DEFAULT NULL, "
        "  isFavorited                     INTEGER             NOT NULL, "
        "  UNIQUE(localUid, guid))"));

    ENSURE_DB_REQUEST(
        res, query, "local_storage::sql::tables_initializer",
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::tables_initializer",
            "Cannot create SavedSearches table in the local storage database"));
}

void TablesInitializer::initializeExtraTriggers(
    QSqlDatabase & databaseConnection)
{
    QSqlQuery query{databaseConnection};

    // clang-format off
    bool res = query.exec(
        QStringLiteral("CREATE TRIGGER IF NOT EXISTS "
                       "on_linked_notebook_delete_trigger "
                       "BEFORE DELETE ON LinkedNotebooks "
                       "BEGIN "
                       "DELETE FROM Notebooks WHERE "
                       "Notebooks.linkedNotebookGuid=OLD.guid; "
                       "DELETE FROM Tags WHERE "
                       "Tags.linkedNotebookGuid=OLD.guid; "
                       "END"));
    // clang-format on

    ENSURE_DB_REQUEST(
        res, query, "local_storage::sql::tables_initializer",
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::tables_initializer",
            "Cannot create on linked notebook delete trigger in the local "
            "storage database"));

    res = query.exec(
        QStringLiteral("CREATE TRIGGER IF NOT EXISTS "
                       "on_note_delete_trigger "
                       "BEFORE DELETE ON Notes "
                       "BEGIN "
                       "DELETE FROM Resources WHERE "
                       "Resources.noteLocalUid=OLD.localUid; "
                       "DELETE FROM ResourceRecognitionData WHERE "
                       "ResourceRecognitionData.noteLocalUid=OLD.localUid; "
                       "DELETE FROM NoteTags WHERE "
                       "NoteTags.localNote=OLD.localUid; "
                       "DELETE FROM NoteResources WHERE "
                       "NoteResources.localNote=OLD.localUid; "
                       "DELETE FROM SharedNotes WHERE "
                       "SharedNotes.sharedNoteNoteGuid=OLD.guid; "
                       "DELETE FROM NoteRestrictions WHERE "
                       "NoteRestrictions.noteLocalUid=OLD.localUid; "
                       "DELETE FROM NoteLimits WHERE "
                       "NoteLimits.noteLocalUid=OLD.localUid; "
                       "END"));

    ENSURE_DB_REQUEST(
        res, query, "local_storage::sql::tables_initializer",
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::tables_initializer",
            "Cannot create on note delete trigger in the local storage "
            "database"));

    res = query.exec(QStringLiteral(
        "CREATE TRIGGER IF NOT EXISTS "
        "on_resource_delete_trigger "
        "BEFORE DELETE ON Resources "
        "BEGIN "
        "DELETE FROM ResourceRecognitionData "
        "WHERE ResourceRecognitionData.resourceLocalUid="
        "OLD.resourceLocalUid; "
        "DELETE FROM ResourceAttributes WHERE "
        "ResourceAttributes.resourceLocalUid=OLD.resourceLocalUid; "
        "DELETE FROM ResourceAttributesApplicationDataKeysOnly WHERE "
        "ResourceAttributesApplicationDataKeysOnly.resourceLocalUid="
        "OLD.resourceLocalUid; "
        "DELETE FROM ResourceAttributesApplicationDataFullMap WHERE "
        "ResourceAttributesApplicationDataFullMap.resourceLocalUid="
        "OLD.resourceLocalUid; "
        "DELETE FROM NoteResources WHERE "
        "NoteResources.localResource=OLD.resourceLocalUid; "
        "END"));

    ENSURE_DB_REQUEST(
        res, query, "local_storage::sql::tables_initializer",
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::tables_initializer",
            "Cannot create on resource delete trigger in the local storage "
            "database"));

    res = query.exec(
        QStringLiteral("CREATE TRIGGER IF NOT EXISTS on_tag_delete_trigger "
                       "BEFORE DELETE ON Tags "
                       "BEGIN "
                       "DELETE FROM NoteTags WHERE "
                       "NoteTags.localTag=OLD.localUid; "
                       "END"));

    ENSURE_DB_REQUEST(
        res, query, "local_storage::sql::tables_initializer",
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::tables_initializer",
            "Cannot create on tag delete trigger in the local storage "
            "database"));
}

} // namespace quentier::local_storage::sql
