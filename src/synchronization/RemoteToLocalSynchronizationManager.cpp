/*
 * Copyright 2016 Dmitry Ivanov
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

#include "RemoteToLocalSynchronizationManager.h"
#include "InkNoteImageDownloader.h"
#include "NoteThumbnailDownloader.h"
#include <quentier/utility/Utility.h>
#include <quentier/local_storage/LocalStorageManagerAsync.h>
#include <quentier/types/Resource.h>
#include <quentier/utility/QuentierCheckPtr.h>
#include <quentier/utility/ApplicationSettings.h>
#include <quentier/utility/StandardPaths.h>
#include <quentier/utility/SysInfo.h>
#include <quentier/utility/TagSortByParentChildRelations.h>
#include <quentier/logging/QuentierLogger.h>
#include <QTimerEvent>
#include <QThreadPool>
#include <QFileInfo>
#include <QDir>
#include <algorithm>

#define ACCOUNT_LIMITS_KEY_GROUP QStringLiteral("account_limits/")
#define ACCOUNT_LIMITS_LAST_SYNC_TIME_KEY QStringLiteral("last_sync_time")
#define ACCOUNT_LIMITS_SERVICE_LEVEL_KEY QStringLiteral("service_level")
#define ACCOUNT_LIMITS_USER_MAIL_LIMIT_DAILY_KEY QStringLiteral("user_mail_limit_daily")
#define ACCOUNT_LIMITS_NOTE_SIZE_MAX_KEY QStringLiteral("note_size_max")
#define ACCOUNT_LIMITS_RESOURCE_SIZE_MAX_KEY QStringLiteral("resource_size_max")
#define ACCOUNT_LIMITS_USER_LINKED_NOTEBOOK_MAX_KEY QStringLiteral("user_linked_notebook_max")
#define ACCOUNT_LIMITS_UPLOAD_LIMIT_KEY QStringLiteral("upload_limit")
#define ACCOUNT_LIMITS_USER_NOTE_COUNT_MAX_KEY QStringLiteral("user_note_count_max")
#define ACCOUNT_LIMITS_USER_NOTEBOOK_COUNT_MAX_KEY QStringLiteral("user_notebook_count_max")
#define ACCOUNT_LIMITS_USER_TAG_COUNT_MAX_KEY QStringLiteral("user_tag_count_max")
#define ACCOUNT_LIMITS_NOTE_TAG_COUNT_MAX_KEY QStringLiteral("note_tag_count_max")
#define ACCOUNT_LIMITS_USER_SAVED_SEARCH_COUNT_MAX_KEY QStringLiteral("user_saved_search_count_max")
#define ACCOUNT_LIMITS_NOTE_RESOURCE_COUNT_MAX_KEY QStringLiteral("note_resource_count_max")

#define SYNC_SETTINGS_KEY_GROUP QStringLiteral("SynchronizationSettings")
#define SHOULD_DOWNLOAD_NOTE_THUMBNAILS QStringLiteral("DownloadNoteThumbnails")
#define SHOULD_DOWNLOAD_INK_NOTE_IMAGES QStringLiteral("DownloadInkNoteImages")
#define INK_NOTE_IMAGES_STORAGE_PATH_KEY QStringLiteral("InkNoteImagesStoragePath")

#define THIRTY_DAYS_IN_MSEC (2592000000)

#define APPEND_NOTE_DETAILS(errorDescription, note) \
   if (note.hasTitle()) \
   { \
       errorDescription.details() = note.title(); \
   } \
   else if (note.hasContent()) \
   { \
       QString previewText = note.plainText(); \
       if (!previewText.isEmpty()) { \
           previewText.truncate(30); \
           errorDescription.details() = previewText; \
       } \
   }

#define SET_ITEM_TYPE_TO_ERROR() \
    errorDescription.details() = QStringLiteral("item type = "); \
    errorDescription.details() += typeName

#define SET_CANT_FIND_BY_NAME_ERROR() \
    ErrorString errorDescription(QT_TR_NOOP("Found a data item with empty name in the local storage")); \
    SET_ITEM_TYPE_TO_ERROR(); \
    QNWARNING(errorDescription << QStringLiteral(": ") << element)

#define SET_CANT_FIND_BY_GUID_ERROR() \
    ErrorString errorDescription(QT_TR_NOOP("Found a data item with empty guid")); \
    SET_ITEM_TYPE_TO_ERROR(); \
    QNWARNING(errorDescription << QStringLiteral(": ") << element)

#define SET_EMPTY_PENDING_LIST_ERROR() \
    ErrorString errorDescription(QT_TR_NOOP("Detected attempt to find a data item within the list " \
                                            "of remote items waiting for processing but that list is empty")); \
    QNWARNING(errorDescription << QStringLiteral(": ") << element)

#define SET_CANT_FIND_IN_PENDING_LIST_ERROR() \
    ErrorString errorDescription(QT_TR_NOOP("can't find the data item within the list " \
                                            "of remote elements waiting for processing")); \
    SET_ITEM_TYPE_TO_ERROR(); \
    QNWARNING(errorDescription << QStringLiteral(": ") << element)

namespace quentier {

RemoteToLocalSynchronizationManager::RemoteToLocalSynchronizationManager(IManager & manager, const QString & host, QObject * parent) :
    QObject(parent),
    m_manager(manager),
    m_connectedToLocalStorage(false),
    m_host(host),
    m_maxSyncChunksPerOneDownload(50),
    m_lastSyncMode(SyncMode::FullSync),
    m_lastUpdateCount(0),
    m_lastSyncTime(0),
    m_onceSyncDone(false),
    m_lastUsnOnStart(-1),
    m_lastSyncChunksDownloadedUsn(-1),
    m_syncChunksDownloaded(false),
    m_fullNoteContentsDownloaded(false),
    m_expungedFromServerToClient(false),
    m_linkedNotebooksSyncChunksDownloaded(false),
    m_active(false),
    m_edamProtocolVersionChecked(false),
    m_syncChunks(),
    m_linkedNotebookSyncChunks(),
    m_linkedNotebookGuidsForWhichSyncChunksWereDownloaded(),
    m_accountLimits(),
    m_tags(),
    m_tagsPendingProcessing(),
    m_tagsPendingAddOrUpdate(),
    m_expungedTags(),
    m_findTagByNameRequestIds(),
    m_findTagByGuidRequestIds(),
    m_addTagRequestIds(),
    m_updateTagRequestIds(),
    m_expungeTagRequestIds(),
    m_pendingTagsSyncStart(false),
    m_tagSyncCache(m_manager.localStorageManagerAsync(), QStringLiteral("")),
    m_tagSyncCachesByLinkedNotebookGuids(),
    m_linkedNotebookGuidsByTagGuids(),
    m_expungeNotelessTagsRequestId(),
    m_savedSearches(),
    m_savedSearchesPendingAddOrUpdate(),
    m_expungedSavedSearches(),
    m_findSavedSearchByNameRequestIds(),
    m_findSavedSearchByGuidRequestIds(),
    m_addSavedSearchRequestIds(),
    m_updateSavedSearchRequestIds(),
    m_expungeSavedSearchRequestIds(),
    m_savedSearchSyncCache(m_manager.localStorageManagerAsync()),
    m_linkedNotebooks(),
    m_linkedNotebooksPendingAddOrUpdate(),
    m_expungedLinkedNotebooks(),
    m_findLinkedNotebookRequestIds(),
    m_addLinkedNotebookRequestIds(),
    m_updateLinkedNotebookRequestIds(),
    m_expungeLinkedNotebookRequestIds(),
    m_pendingLinkedNotebooksSyncStart(false),
    m_allLinkedNotebooks(),
    m_listAllLinkedNotebooksRequestId(),
    m_allLinkedNotebooksListed(false),
    m_authenticationToken(),
    m_shardId(),
    m_authenticationTokenExpirationTime(0),
    m_pendingAuthenticationTokenAndShardId(false),
    m_user(),
    m_findUserRequestId(),
    m_addOrUpdateUserRequestId(),
    m_onceAddedOrUpdatedUserInLocalStorage(false),
    m_authenticationTokensAndShardIdsByLinkedNotebookGuid(),
    m_authenticationTokenExpirationTimesByLinkedNotebookGuid(),
    m_pendingAuthenticationTokensForLinkedNotebooks(false),
    m_syncStatesByLinkedNotebookGuid(),
    m_lastUpdateCountByLinkedNotebookGuid(),
    m_lastSyncTimeByLinkedNotebookGuid(),
    m_linkedNotebookGuidsForWhichFullSyncWasPerformed(),
    m_linkedNotebookGuidsOnceFullySynced(),
    m_notebooks(),
    m_notebooksPendingAddOrUpdate(),
    m_expungedNotebooks(),
    m_findNotebookByNameRequestIds(),
    m_findNotebookByGuidRequestIds(),
    m_addNotebookRequestIds(),
    m_updateNotebookRequestIds(),
    m_expungeNotebookRequestIds(),
    m_pendingNotebooksSyncStart(false),
    m_notebookSyncCache(m_manager.localStorageManagerAsync(), QStringLiteral("")),
    m_notebookSyncCachesByLinkedNotebookGuids(),
    m_linkedNotebookGuidsByNotebookGuids(),
    m_notes(),
    m_notesPendingAddOrUpdate(),
    m_originalNumberOfNotes(0),
    m_numNotesDownloaded(0),
    m_expungedNotes(),
    m_findNoteByGuidRequestIds(),
    m_addNoteRequestIds(),
    m_updateNoteRequestIds(),
    m_expungeNoteRequestIds(),
    m_guidsOfProcessedNonExpungedNotes(),
    m_notesWithFindRequestIdsPerFindNotebookRequestId(),
    m_notebooksPerNoteIds(),
    m_resources(),
    m_resourcesPendingAddOrUpdate(),
    m_originalNumberOfResources(0),
    m_numResourcesDownloaded(0),
    m_findResourceByGuidRequestIds(),
    m_addResourceRequestIds(),
    m_updateResourceRequestIds(),
    m_resourcesByMarkNoteOwningResourceDirtyRequestIds(),
    m_resourcesWithFindRequestIdsPerFindNoteRequestId(),
    m_inkNoteResourceDataPerFindNotebookRequestId(),
    m_resourceGuidsPendingInkNoteImageDownloadPerNoteGuid(),
    m_notesPendingInkNoteImagesDownloadByFindNotebookRequestId(),
    m_notesPendingThumbnailDownloadByFindNotebookRequestId(),
    m_notesPendingThumbnailDownloadByGuid(),
    m_resourceFoundFlagPerFindResourceRequestId(),
    m_localUidsOfElementsAlreadyAttemptedToFindByName(),
    m_guidsOfNotesPendingDownloadForAddingToLocalStorage(),
    m_notesPendingDownloadForUpdatingInLocalStorageByGuid(),
    m_notesOwningResourcesPendingDownloadForAddingToLocalStorageByResourceGuid(),
    m_resourcesPendingDownloadForUpdatingInLocalStorageWithNotesByResourceGuid(),
    m_fullSyncStaleDataItemsSyncedGuids(),
    m_pFullSyncStaleDataItemsExpunger(Q_NULLPTR),
    m_fullSyncStaleDataItemsExpungersByLinkedNotebookGuid(),
    m_notesToAddPerAPICallPostponeTimerId(),
    m_notesToUpdatePerAPICallPostponeTimerId(),
    m_resourcesToAddWithNotesPerAPICallPostponeTimerId(),
    m_resourcesToUpdateWithNotesPerAPICallPostponeTimerId(),
    m_postponedConflictingResourceDataPerAPICallPostponeTimerId(),
    m_afterUsnForSyncChunkPerAPICallPostponeTimerId(),
    m_getLinkedNotebookSyncStateBeforeStartAPICallPostponeTimerId(),
    m_downloadLinkedNotebookSyncChunkAPICallPostponeTimerId(),
    m_getSyncStateBeforeStartAPICallPostponeTimerId(0),
    m_syncUserPostponeTimerId(0),
    m_syncAccountLimitsPostponeTimerId(0),
    m_gotLastSyncParameters(false)
{
    QObject::connect(&(m_manager.noteStore()), QNSIGNAL(NoteStore,getNoteAsyncFinished,qint32,qevercloud::Note,qint32,ErrorString),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onGetNoteAsyncFinished,qint32,qevercloud::Note,qint32,ErrorString));
    QObject::connect(&(m_manager.noteStore()), QNSIGNAL(NoteStore,getResourceAsyncFinished,qint32,qevercloud::Resource,qint32,ErrorString),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onGetResourceAsyncFinished,qint32,qevercloud::Resource,qint32,ErrorString));
}

bool RemoteToLocalSynchronizationManager::active() const
{
    return m_active;
}

void RemoteToLocalSynchronizationManager::setAccount(const Account & account)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::setAccount: ") << account);

    if (m_user.hasId() && (m_user.id() != account.id())) {
        QNDEBUG(QStringLiteral("Switching to a different user, clearing the current state"));
        clearAll();
    }

    m_user.setId(account.id());
    m_user.setName(account.name());
    m_user.setUsername(account.name());

    Account::EvernoteAccountType::type accountEnType = account.evernoteAccountType();
    switch(accountEnType)
    {
    case Account::EvernoteAccountType::Plus:
        m_user.setServiceLevel(qevercloud::ServiceLevel::PLUS);
        break;
    case Account::EvernoteAccountType::Premium:
        m_user.setServiceLevel(qevercloud::ServiceLevel::PREMIUM);
        break;
    default:
        m_user.setServiceLevel(qevercloud::ServiceLevel::BASIC);
        break;
    }

    qevercloud::AccountLimits accountLimits;
    accountLimits.userMailLimitDaily = account.mailLimitDaily();
    accountLimits.noteSizeMax = account.noteSizeMax();
    accountLimits.resourceSizeMax = account.resourceSizeMax();
    accountLimits.userLinkedNotebookMax = account.linkedNotebookMax();
    accountLimits.userNoteCountMax = account.noteCountMax();
    accountLimits.userNotebookCountMax = account.notebookCountMax();
    accountLimits.userTagCountMax = account.tagCountMax();
    accountLimits.noteTagCountMax = account.noteTagCountMax();
    accountLimits.userSavedSearchesMax = account.savedSearchCountMax();
    accountLimits.noteResourceCountMax = account.noteResourceCountMax();

    m_user.setAccountLimits(std::move(accountLimits));
}

Account RemoteToLocalSynchronizationManager::account() const
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::account"));

    QString name;
    if (m_user.hasName()) {
        name = m_user.name();
    }

    if (name.isEmpty() && m_user.hasUsername()) {
        name = m_user.username();
    }

    Account::EvernoteAccountType::type accountEnType = Account::EvernoteAccountType::Free;
    if (m_user.hasServiceLevel())
    {
        switch(m_user.serviceLevel())
        {
        case qevercloud::ServiceLevel::PLUS:
            accountEnType = Account::EvernoteAccountType::Plus;
            break;
        case qevercloud::ServiceLevel::PREMIUM:
            accountEnType = Account::EvernoteAccountType::Premium;
            break;
        case qevercloud::ServiceLevel::BASIC:
        default:
            break;
        }
    }

    qevercloud::UserID userId = -1;
    if (m_user.hasId()) {
        userId = m_user.id();
    }

    QString shardId;
    if (m_user.hasShardId()) {
        shardId = m_user.shardId();
    }

    Account account(name, Account::Type::Evernote, userId, accountEnType, m_host, shardId);
    account.setEvernoteAccountLimits(m_accountLimits);
    return account;
}

bool RemoteToLocalSynchronizationManager::syncUser(const qevercloud::UserID userId, ErrorString & errorDescription,
                                                   const bool writeUserDataToLocalStorage)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::syncUser: user id = ") << userId
            << QStringLiteral(", write user data to local storage = ")
            << (writeUserDataToLocalStorage ? QStringLiteral("true") : QStringLiteral("false")));

    m_user = User();
    m_user.setId(userId);

    // Checking the protocol version first
    if (!checkProtocolVersion(errorDescription)) {
        QNDEBUG(QStringLiteral("Protocol version check failed: ") << errorDescription);
        return false;
    }

    bool waitIfRateLimitReached = false;

    // Retrieving the latest user info then, to figure out the service level and stuff like that
    if (!syncUserImpl(waitIfRateLimitReached, errorDescription, writeUserDataToLocalStorage)) {
        QNDEBUG(QStringLiteral("Syncing the user has failed: ") << errorDescription);
        return false;
    }

    if (!checkAndSyncAccountLimits(waitIfRateLimitReached, errorDescription)) {
        QNDEBUG(QStringLiteral("Syncing the user's account limits has failed: ") << errorDescription);
        return false;
    }

    QNTRACE(QStringLiteral("Synchronized user data: ") << m_user);
    return true;
}

const User & RemoteToLocalSynchronizationManager::user() const
{
    return m_user;
}

bool RemoteToLocalSynchronizationManager::shouldDownloadThumbnailsForNotes() const
{
    ApplicationSettings appSettings(account(), SYNCHRONIZATION_PERSISTENCE_NAME);
    appSettings.beginGroup(SYNC_SETTINGS_KEY_GROUP);
    bool res = (appSettings.contains(SHOULD_DOWNLOAD_NOTE_THUMBNAILS)
                ? appSettings.value(SHOULD_DOWNLOAD_NOTE_THUMBNAILS).toBool()
                : false);
    appSettings.endGroup();
    return res;
}

bool RemoteToLocalSynchronizationManager::shouldDownloadInkNoteImages() const
{
    ApplicationSettings appSettings(account(), SYNCHRONIZATION_PERSISTENCE_NAME);
    appSettings.beginGroup(SYNC_SETTINGS_KEY_GROUP);
    bool res = (appSettings.contains(SHOULD_DOWNLOAD_INK_NOTE_IMAGES)
                ? appSettings.value(SHOULD_DOWNLOAD_INK_NOTE_IMAGES).toBool()
                : false);
    appSettings.endGroup();
    return res;
}

QString RemoteToLocalSynchronizationManager::inkNoteImagesStoragePath() const
{
    ApplicationSettings appSettings(account(), SYNCHRONIZATION_PERSISTENCE_NAME);
    appSettings.beginGroup(SYNC_SETTINGS_KEY_GROUP);
    QString path = (appSettings.contains(INK_NOTE_IMAGES_STORAGE_PATH_KEY)
                    ? appSettings.value(INK_NOTE_IMAGES_STORAGE_PATH_KEY).toString()
                    : defaultInkNoteImageStoragePath());
    appSettings.endGroup();
    return path;
}

void RemoteToLocalSynchronizationManager::start(qint32 afterUsn)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::start: afterUsn = ") << afterUsn);

    m_lastUsnOnStart = afterUsn;

    if (!m_gotLastSyncParameters) {
        Q_EMIT requestLastSyncParameters();
        return;
    }

    clear();

    connectToLocalStorage();
    m_lastUsnOnStart = afterUsn;
    m_active = true;

    ErrorString errorDescription;

    // Checking the protocol version first
    if (!checkProtocolVersion(errorDescription)) {
        Q_EMIT failure(errorDescription);
        return;
    }

    bool waitIfRateLimitReached = true;

    // Retrieving the latest user info then, to figure out the service level and stuff like that
    if (!syncUserImpl(waitIfRateLimitReached, errorDescription))
    {
        if (m_syncUserPostponeTimerId == 0) {
            // Not a "rate limit exceeded" error
            Q_EMIT failure(errorDescription);
        }

        return;
    }

    if (!checkAndSyncAccountLimits(waitIfRateLimitReached, errorDescription))
    {
        if (m_syncAccountLimitsPostponeTimerId == 0) {
            // Not a "rate limit exceeded" error
            Q_EMIT failure(errorDescription);
        }

        return;
    }

    m_lastSyncMode = ((afterUsn == 0)
                      ? SyncMode::FullSync
                      : SyncMode::IncrementalSync);

    if (m_onceSyncDone || (afterUsn != 0))
    {
        bool asyncWait = false;
        bool error = false;

        // check the sync state of user's own account, this may produce the asynchronous chain of events or some error
        bool res = checkUserAccountSyncState(asyncWait, error, afterUsn);
        if (error || asyncWait) {
            return;
        }

        if (!res)
        {
            QNTRACE(QStringLiteral("The service has no updates for user's own account, need to check for updates from linked notebooks"));

            m_fullNoteContentsDownloaded = true;
            Q_EMIT synchronizedContentFromUsersOwnAccount(m_lastUpdateCount, m_lastSyncTime);

            m_expungedFromServerToClient = true;

            res = checkLinkedNotebooksSyncStates(asyncWait, error);
            if (asyncWait || error) {
                return;
            }

            if (!res) {
                QNTRACE(QStringLiteral("The service has no updates for any of linked notebooks"));
                finalize();
            }

            startLinkedNotebooksSync();
            return;
        }
        // Otherwise the sync of all linked notebooks from user's account would start after the sync of user's account
        // (Because the sync of user's account can bring in the new linked notebooks or remove any of them)
    }

    downloadSyncChunksAndLaunchSync(afterUsn);
}

void RemoteToLocalSynchronizationManager::stop()
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::stop"));

    if (!m_active) {
        QNDEBUG(QStringLiteral("Already stopped"));
        return;
    }

    clear();
    resetCurrentSyncState();

    Q_EMIT stopped();
}

template <>
void RemoteToLocalSynchronizationManager::emitAddRequest<Tag>(const Tag & tag)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::emitAddRequest<Tag>: ") << tag);

    registerTagPendingAddOrUpdate(tag);

    QUuid addTagRequestId = QUuid::createUuid();
    Q_UNUSED(m_addTagRequestIds.insert(addTagRequestId));
    QNTRACE(QStringLiteral("Emitting the request to add tag to local storage: request id = ")
            << addTagRequestId << QStringLiteral(", tag: ") << tag);
    Q_EMIT addTag(tag, addTagRequestId);
}

template <>
void RemoteToLocalSynchronizationManager::emitAddRequest<SavedSearch>(const SavedSearch & search)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::emitAddRequest<SavedSearch>: ") << search);

    registerSavedSearchPendingAddOrUpdate(search);

    QUuid addSavedSearchRequestId = QUuid::createUuid();
    Q_UNUSED(m_addSavedSearchRequestIds.insert(addSavedSearchRequestId));
    QNTRACE(QStringLiteral("Emitting the request to add saved search to local storage: request id = ")
            << addSavedSearchRequestId << QStringLiteral(", saved search: ") << search);
    Q_EMIT addSavedSearch(search, addSavedSearchRequestId);
}

template <>
void RemoteToLocalSynchronizationManager::emitAddRequest<LinkedNotebook>(const LinkedNotebook & linkedNotebook)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::emitAddRequest<LinkedNotebook>: ") << linkedNotebook);

    registerLinkedNotebookPendingAddOrUpdate(linkedNotebook);

    QUuid addLinkedNotebookRequestId = QUuid::createUuid();
    Q_UNUSED(m_addLinkedNotebookRequestIds.insert(addLinkedNotebookRequestId));
    QNTRACE(QStringLiteral("Emitting the request to add linked notebook to local storage: request id = ")
            << addLinkedNotebookRequestId << QStringLiteral(", linked notebook: ") << linkedNotebook);
    Q_EMIT addLinkedNotebook(linkedNotebook, addLinkedNotebookRequestId);
}

template <>
void RemoteToLocalSynchronizationManager::emitAddRequest<Notebook>(const Notebook & notebook)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::emitAddRequest<Notebook>: ") << notebook);

    registerNotebookPendingAddOrUpdate(notebook);

    QUuid addNotebookRequestId = QUuid::createUuid();
    Q_UNUSED(m_addNotebookRequestIds.insert(addNotebookRequestId));
    QNTRACE(QStringLiteral("Emitting the request to add notebook to local storage: request id = ")
            << addNotebookRequestId << QStringLiteral(", notebook: ") << notebook);
    Q_EMIT addNotebook(notebook, addNotebookRequestId);
}

template <>
void RemoteToLocalSynchronizationManager::emitAddRequest<Note>(const Note & note)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::emitAddRequest<Note>: ") << note);

    registerNotePendingAddOrUpdate(note);

    QUuid addNoteRequestId = QUuid::createUuid();
    Q_UNUSED(m_addNoteRequestIds.insert(addNoteRequestId));
    QNTRACE(QStringLiteral("Emitting the request to add note to local storage: request id = ")
            << addNoteRequestId << QStringLiteral(", note: ") << note);
    Q_EMIT addNote(note, addNoteRequestId);
}

void RemoteToLocalSynchronizationManager::onFindUserCompleted(User user, QUuid requestId)
{
    if (requestId != m_findUserRequestId) {
        return;
    }

    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onFindUserCompleted: user = ") << user
            << QStringLiteral("\nRequest id = ") << requestId);

    m_user = user;
    m_findUserRequestId = QUuid();

    // Updating the user info as user was found in the local storage
    m_addOrUpdateUserRequestId = QUuid::createUuid();
    QNTRACE(QStringLiteral("Emitting the request to update user in the local storage database: request id = ")
            << m_addOrUpdateUserRequestId << QStringLiteral(", user = ") << m_user);
    Q_EMIT updateUser(m_user, m_addOrUpdateUserRequestId);
}

void RemoteToLocalSynchronizationManager::onFindUserFailed(User user, ErrorString errorDescription, QUuid requestId)
{
    if (requestId != m_findUserRequestId) {
        return;
    }

    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onFindUserFailed: user = ") << user
            << QStringLiteral("\nError description = ") << errorDescription << QStringLiteral(", request id = ")
            << requestId);

    m_findUserRequestId = QUuid();

    // Adding the user info as user was not found in the local storage
    m_addOrUpdateUserRequestId = QUuid::createUuid();
    QNTRACE(QStringLiteral("Emitting the request to add user to the local storage database: request id = ")
            << m_addOrUpdateUserRequestId << QStringLiteral(", user = ") << m_user);
    Q_EMIT addUser(m_user, m_addOrUpdateUserRequestId);
}

void RemoteToLocalSynchronizationManager::onFindNotebookCompleted(Notebook notebook, QUuid requestId)
{
    QNTRACE(QStringLiteral("RemoteToLocalSynchronizationManager::onFindNotebookCompleted: request id = ")
            << requestId << QStringLiteral(", notebook: ") << notebook);

    bool foundByGuid = onFoundDuplicateByGuid(notebook, requestId, QStringLiteral("Notebook"),
                                              m_notebooks, m_notebooksPendingAddOrUpdate,
                                              m_findNotebookByGuidRequestIds);
    if (foundByGuid) {
        return;
    }

    bool foundByName = onFoundDuplicateByName(notebook, requestId, QStringLiteral("Notebook"), m_notebooks,
                                              m_notebooksPendingAddOrUpdate, m_findNotebookByNameRequestIds);
    if (foundByName) {
        return;
    }

    NoteDataPerFindNotebookRequestId::iterator rit = m_notesWithFindRequestIdsPerFindNotebookRequestId.find(requestId);
    if (rit != m_notesWithFindRequestIdsPerFindNotebookRequestId.end())
    {
        QNDEBUG(QStringLiteral("Found notebook needed for note synchronization"));

        const QPair<Note,QUuid> & noteWithFindRequestId = rit.value();
        const Note & note = noteWithFindRequestId.first;
        const QUuid & findNoteRequestId = noteWithFindRequestId.second;

        QString noteGuid = (note.hasGuid() ? note.guid() : QString());
        QString noteLocalUid = note.localUid();

        QPair<QString,QString> key(noteGuid, noteLocalUid);

        // NOTE: notebook for notes is only required for its pair of guid + local uid,
        // it shouldn't prohibit the creation or update of the notes during the synchronization procedure
        notebook.setCanCreateNotes(true);
        notebook.setCanUpdateNotes(true);

        m_notebooksPerNoteIds[key] = notebook;

        Q_UNUSED(onFoundDuplicateByGuid(note, findNoteRequestId, QStringLiteral("Note"),
                                        m_notes, m_notesPendingAddOrUpdate, m_findNoteByGuidRequestIds));
        return;
    }

    auto iit = m_inkNoteResourceDataPerFindNotebookRequestId.find(requestId);
    if (iit != m_inkNoteResourceDataPerFindNotebookRequestId.end())
    {
        QNDEBUG(QStringLiteral("Found notebook for ink note image downloading for note resource"));

        InkNoteResourceData resourceData = iit.value();
        Q_UNUSED(m_inkNoteResourceDataPerFindNotebookRequestId.erase(iit))

        setupInkNoteImageDownloading(resourceData.m_resourceGuid, resourceData.m_resourceHeight,
                                     resourceData.m_resourceWidth, resourceData.m_noteGuid, notebook);
        return;
    }

    auto iit_note = m_notesPendingInkNoteImagesDownloadByFindNotebookRequestId.find(requestId);
    if (iit_note != m_notesPendingInkNoteImagesDownloadByFindNotebookRequestId.end())
    {
        QNDEBUG(QStringLiteral("Found notebook for ink note images downloading for note"));

        Note note = iit_note.value();
        Q_UNUSED(m_notesPendingInkNoteImagesDownloadByFindNotebookRequestId.erase(iit_note))

        bool res = setupInkNoteImageDownloadingForNote(note, notebook);
        if (Q_UNLIKELY(!res))
        {
            QNDEBUG(QStringLiteral("Wasn't able to set up the ink note image downloading for note: ")
                    << note << QStringLiteral("\nNotebook: ") << notebook);

            // NOTE: treat it as a recoverable failure, just ignore it and consider the note properly downloaded
            checkAndIncrementNoteDownloadProgress(note.hasGuid() ? note.guid() : QString());
            checkServerDataMergeCompletion();
        }

        return;
    }

    auto thumbnailIt = m_notesPendingThumbnailDownloadByFindNotebookRequestId.find(requestId);
    if (thumbnailIt != m_notesPendingThumbnailDownloadByFindNotebookRequestId.end())
    {
        QNDEBUG(QStringLiteral("Found note for note thumbnail downloading"));

        Note note = thumbnailIt.value();
        Q_UNUSED(m_notesPendingThumbnailDownloadByFindNotebookRequestId.erase(thumbnailIt))

        bool res = setupNoteThumbnailDownloading(note, notebook);
        if (Q_UNLIKELY(!res))
        {
            QNDEBUG(QStringLiteral("Wasn't able to set up the thumbnail downloading for note: ")
                    << note << QStringLiteral("\nNotebook: ") << notebook);

            // NOTE: treat it as a recoverable failure, just ignore it and consider the note properly downloaded
            checkAndIncrementNoteDownloadProgress(note.hasGuid() ? note.guid() : QString());
            checkServerDataMergeCompletion();
        }

        return;
    }
}

void RemoteToLocalSynchronizationManager::onFindNotebookFailed(Notebook notebook, ErrorString errorDescription,
                                                               QUuid requestId)
{
    QNTRACE(QStringLiteral("RemoteToLocalSynchronizationManager::onFindNotebookFailed: request id = ")
            << requestId << QStringLiteral(", error description: ") << errorDescription
            << QStringLiteral(", notebook: ") << notebook);

    bool failedToFindByGuid = onNoDuplicateByGuid(notebook, requestId, errorDescription, QStringLiteral("Notebook"),
                                                  m_notebooks, m_findNotebookByGuidRequestIds);
    if (failedToFindByGuid) {
        return;
    }

    bool failedToFindByName = onNoDuplicateByName(notebook, requestId, errorDescription, QStringLiteral("Notebook"),
                                                  m_notebooks, m_findNotebookByNameRequestIds);
    if (failedToFindByName) {
        return;
    }

    NoteDataPerFindNotebookRequestId::iterator rit = m_notesWithFindRequestIdsPerFindNotebookRequestId.find(requestId);
    if (rit != m_notesWithFindRequestIdsPerFindNotebookRequestId.end()) {
        ErrorString errorDescription(QT_TR_NOOP("Failed to find the notebook for one of synchronized notes"));
        QNWARNING(errorDescription << QStringLiteral(": ") << notebook);
        Q_EMIT failure(errorDescription);
        return;
    }

    auto iit = m_inkNoteResourceDataPerFindNotebookRequestId.find(requestId);
    if (iit != m_inkNoteResourceDataPerFindNotebookRequestId.end()) {
        Q_UNUSED(m_inkNoteResourceDataPerFindNotebookRequestId.erase(iit))
        QNWARNING(QStringLiteral("Can't find the notebook for the purpose of setting up the ink note image downloading"));
        return;
    }

    auto iit_note = m_notesPendingInkNoteImagesDownloadByFindNotebookRequestId.find(requestId);
    if (iit_note != m_notesPendingInkNoteImagesDownloadByFindNotebookRequestId.end())
    {
        Note note = iit_note.value();
        Q_UNUSED(m_notesPendingInkNoteImagesDownloadByFindNotebookRequestId.erase(iit_note))

        if (note.hasGuid())
        {
            // We might already have this note's resources mapped by note's guid
            // as "pending ink note image download", need to remove this mapping
            Q_UNUSED(m_resourceGuidsPendingInkNoteImageDownloadPerNoteGuid.remove(note.guid()))
        }

        QNWARNING(QStringLiteral("Can't find the notebook for the purpose of setting up the ink note image downloading"));

        // NOTE: incrementing note download progress here because we haven't incremented it
        // on the receipt of full note data before setting up the ink note image downloading
        checkAndIncrementNoteDownloadProgress(note.hasGuid() ? note.guid() : QString());

        // NOTE: handle the failure to download the ink note image as a recoverable error
        // i.e. consider the note successfully downloaded anyway - hence, need to check if that
        // was the last note pending its downloading events sequence
        checkServerDataMergeCompletion();

        return;
    }

    auto thumbnailIt = m_notesPendingThumbnailDownloadByFindNotebookRequestId.find(requestId);
    if (thumbnailIt != m_notesPendingThumbnailDownloadByFindNotebookRequestId.end())
    {
        Note note = thumbnailIt.value();
        Q_UNUSED(m_notesPendingThumbnailDownloadByFindNotebookRequestId.erase(thumbnailIt))

        if (note.hasGuid())
        {
            // We might already have this note within those "pending the thumbnail download",
            // need to remove it from there
            auto dit = m_notesPendingThumbnailDownloadByGuid.find(note.guid());
            if (dit != m_notesPendingThumbnailDownloadByGuid.end()) {
                Q_UNUSED(m_notesPendingThumbnailDownloadByGuid.erase(dit))
            }
        }

        QNWARNING(QStringLiteral("Can't find the notebook for the purpose of setting up the note thumbnail downloading"));

        // NOTE: incrementing note download progress here because we haven't incremented it
        // on the receipt of full note data before setting up the thumbnails downloading
        checkAndIncrementNoteDownloadProgress(note.hasGuid() ? note.guid() : QString());

        // NOTE: handle the failure to download the note thumbnail as a recoverable error
        // i.e. consider the note successfully downloaded anyway - hence, need to check if that
        // was the last note pending its downloading events sequence
        checkServerDataMergeCompletion();

        return;
    }
}

void RemoteToLocalSynchronizationManager::onFindNoteCompleted(Note note, bool withResourceBinaryData, QUuid requestId)
{
    Q_UNUSED(withResourceBinaryData);

    QSet<QUuid>::iterator it = m_findNoteByGuidRequestIds.find(requestId);
    if (it != m_findNoteByGuidRequestIds.end())
    {
        QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onFindNoteCompleted: note = ")
                << note << QStringLiteral(", requestId = ") << requestId);

        // NOTE: erase is required for proper work of the macro; the request would be re-inserted below if macro doesn't return from the method
        Q_UNUSED(m_findNoteByGuidRequestIds.erase(it));

        // Need to find Notebook corresponding to the note in order to proceed
        if (!note.hasNotebookGuid())
        {
            ErrorString errorDescription(QT_TR_NOOP("Found duplicate note in the local storage which doesn't have "
                                                    "a notebook guid"));
            APPEND_NOTE_DETAILS(errorDescription, note)

            QNWARNING(errorDescription << QStringLiteral(": ") << note);
            Q_EMIT failure(errorDescription);
            return;
        }

        QUuid findNotebookPerNoteRequestId = QUuid::createUuid();
        m_notesWithFindRequestIdsPerFindNotebookRequestId[findNotebookPerNoteRequestId] =
                QPair<Note,QUuid>(note, requestId);

        Notebook notebookToFind;
        notebookToFind.unsetLocalUid();
        notebookToFind.setGuid(note.notebookGuid());

        Q_UNUSED(m_findNoteByGuidRequestIds.insert(requestId));

        Q_EMIT findNotebook(notebookToFind, findNotebookPerNoteRequestId);
        return;
    }

    ResourceDataPerFindNoteRequestId::iterator rit = m_resourcesWithFindRequestIdsPerFindNoteRequestId.find(requestId);
    if (rit != m_resourcesWithFindRequestIdsPerFindNoteRequestId.end())
    {
        QPair<Resource,QUuid> resourceWithFindRequestId = rit.value();

        Q_UNUSED(m_resourcesWithFindRequestIdsPerFindNoteRequestId.erase(rit))

        if (Q_UNLIKELY(!note.hasGuid())) {
            ErrorString errorDescription(QT_TR_NOOP("Found the note necessary for the resource synchronization "
                                                    "but it doesn't have a guid"));
            APPEND_NOTE_DETAILS(errorDescription, note)

            QNWARNING(errorDescription << QStringLiteral(": ") << note);
            Q_EMIT failure(errorDescription);
            return;
        }

        const Notebook * pNotebook = getNotebookPerNote(note);

        Resource & resource = resourceWithFindRequestId.first;
        const QUuid & findResourceRequestId = resourceWithFindRequestId.second;

        if (shouldDownloadThumbnailsForNotes())
        {
            auto noteThumbnailDownloadIt = m_notesPendingThumbnailDownloadByGuid.find(note.guid());
            if (noteThumbnailDownloadIt == m_notesPendingThumbnailDownloadByGuid.end())
            {
                QNDEBUG(QStringLiteral("Need to download the thumbnail for the note with added or updated resource"));

                // NOTE: don't care whether was capable to start downloading the note thumnail,
                // if not, this error is simply ignored
                if (pNotebook) {
                    Q_UNUSED(setupNoteThumbnailDownloading(note, *pNotebook))
                }
                else {
                    Q_UNUSED(findNotebookForNoteThumbnailDownloading(note))
                }
            }
        }

        if (resource.hasGuid() && resource.hasMime() && resource.hasWidth() && resource.hasHeight() &&
            (resource.mime() == QStringLiteral("application/vnd.evernote.ink")))
        {
            QNDEBUG(QStringLiteral("The resource appears to be the one for the ink note, need to download the image for it; "
                                   "but first need to understand whether the note owning the resource is from the current "
                                   "user's account or from some linked notebook"));

            if (pNotebook)
            {
                setupInkNoteImageDownloading(resource.guid(), resource.height(), resource.width(), note.guid(), *pNotebook);
            }
            else if (note.hasNotebookLocalUid() || note.hasNotebookGuid())
            {
                InkNoteResourceData resourceData;
                resourceData.m_resourceGuid = resource.guid();
                resourceData.m_noteGuid = note.guid();
                resourceData.m_resourceHeight = resource.height();
                resourceData.m_resourceWidth = resource.width();

                Notebook dummyNotebook;
                if (note.hasNotebookLocalUid()) {
                    dummyNotebook.setLocalUid(note.notebookLocalUid());
                }
                else {
                    dummyNotebook.setLocalUid(QString());
                    dummyNotebook.setGuid(note.notebookGuid());
                }

                QUuid findNotebookForInkNoteSetupRequestId = QUuid::createUuid();
                m_inkNoteResourceDataPerFindNotebookRequestId[findNotebookForInkNoteSetupRequestId] = resourceData;
                QNTRACE(QStringLiteral("Emitting the request to find a notebook for the ink note image download resolution: ")
                        << findNotebookForInkNoteSetupRequestId << QStringLiteral(", resource guid = ") << resourceData.m_resourceGuid
                        << QStringLiteral(", resource height = ") << resourceData.m_resourceHeight << QStringLiteral(", resource width = ")
                        << resourceData.m_resourceWidth << QStringLiteral(", note guid = ") << note.guid() << QStringLiteral(", notebook: ")
                        << dummyNotebook);
                Q_EMIT findNotebook(dummyNotebook, findNotebookForInkNoteSetupRequestId);
            }
            else
            {
                QNWARNING(QStringLiteral("Can't download the ink note image: note has neither notebook local uid not notebook guid: ")
                          << note);
            }
        }

        auto resourceFoundIt = m_resourceFoundFlagPerFindResourceRequestId.find(findResourceRequestId);
        if (resourceFoundIt == m_resourceFoundFlagPerFindResourceRequestId.end()) {
            QNWARNING(QStringLiteral("Duplicate of synchronized resource was not found in the local storage database! "
                                     "Attempting to add it to local storage"));
            registerResourcePendingAddOrUpdate(resource);
            getFullResourceDataAsyncAndAddToLocalStorage(resource, note);
            return;
        }

        if (!resource.isDirty()) {
            QNDEBUG(QStringLiteral("Found duplicate resource in local storage which is not marked dirty => "
                                   "overriding it with the version received from the remote storage"));
            registerResourcePendingAddOrUpdate(resource);
            getFullResourceDataAsyncAndUpdateInLocalStorage(resource, note);
            return;
        }

        QNDEBUG(QStringLiteral("Found duplicate resource in local storage which is marked dirty => "
                               "will treat it as a conflict of notes"));

        Note conflictingNote = createConflictingNote(note);

        Note updatedNote(note);
        updatedNote.setDirty(false);
        updatedNote.setLocal(false);

        processResourceConflictAsNoteConflict(updatedNote, conflictingNote, resource);
    }
}

void RemoteToLocalSynchronizationManager::onFindNoteFailed(Note note, bool withResourceBinaryData,
                                                           ErrorString errorDescription, QUuid requestId)
{
    Q_UNUSED(withResourceBinaryData)
    Q_UNUSED(errorDescription)

    QSet<QUuid>::iterator it = m_findNoteByGuidRequestIds.find(requestId);
    if (it != m_findNoteByGuidRequestIds.end())
    {
        QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onFindNoteFailed: note = ") << note
                << QStringLiteral(", requestId = ") << requestId);

        Q_UNUSED(m_findNoteByGuidRequestIds.erase(it));

        auto it = findItemByGuid(m_notes, note, QStringLiteral("Note"));
        if (it == m_notes.end()) {
            return;
        }

        // Removing the note from the list of notes waiting for processing
        // but also remembering it for further reference
        Q_UNUSED(m_notes.erase(it));

        if (note.hasGuid()) {
            Q_UNUSED(m_guidsOfProcessedNonExpungedNotes.insert(note.guid()))
        }

        getFullNoteDataAsyncAndAddToLocalStorage(note);
        return;
    }

    ResourceDataPerFindNoteRequestId::iterator rit = m_resourcesWithFindRequestIdsPerFindNoteRequestId.find(requestId);
    if (rit != m_resourcesWithFindRequestIdsPerFindNoteRequestId.end())
    {
        QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onFindNoteFailed: note = ") << note
                << QStringLiteral(", requestId = ") << requestId);

        Q_UNUSED(m_resourcesWithFindRequestIdsPerFindNoteRequestId.erase(rit));

        ErrorString errorDescription(QT_TR_NOOP("Can't find note containing the synchronized resource in the local storage"));
        APPEND_NOTE_DETAILS(errorDescription, note)

        QNWARNING(errorDescription << QStringLiteral(", note attempted to be found: ") << note);
        Q_EMIT failure(errorDescription);
        return;
    }
}

void RemoteToLocalSynchronizationManager::onFindTagCompleted(Tag tag, QUuid requestId)
{
    bool foundByGuid = onFoundDuplicateByGuid(tag, requestId, QStringLiteral("Tag"),
                                              m_tags, m_tagsPendingAddOrUpdate, m_findTagByGuidRequestIds);
    if (foundByGuid) {
        return;
    }

    bool foundByName = onFoundDuplicateByName(tag, requestId, QStringLiteral("Tag"), m_tags,
                                              m_tagsPendingAddOrUpdate, m_findTagByNameRequestIds);
    if (foundByName) {
        return;
    }
}

void RemoteToLocalSynchronizationManager::onFindTagFailed(Tag tag, ErrorString errorDescription, QUuid requestId)
{
    bool failedToFindByGuid = onNoDuplicateByGuid(tag, requestId, errorDescription, QStringLiteral("Tag"),
                                                  m_tags, m_findTagByGuidRequestIds);
    if (failedToFindByGuid) {
        return;
    }

    bool failedToFindByName = onNoDuplicateByName(tag, requestId, errorDescription, QStringLiteral("Tag"),
                                                  m_tags, m_findTagByNameRequestIds);
    if (failedToFindByName) {
        return;
    }
}

void RemoteToLocalSynchronizationManager::onFindResourceCompleted(Resource resource,
                                                                  bool withResourceBinaryData, QUuid requestId)
{
    Q_UNUSED(withResourceBinaryData)

    QSet<QUuid>::iterator rit = m_findResourceByGuidRequestIds.find(requestId);
    if (rit != m_findResourceByGuidRequestIds.end())
    {
        QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onFindResourceCompleted: resource = ")
                << resource << QStringLiteral(", requestId = ") << requestId);

        Q_UNUSED(m_findResourceByGuidRequestIds.erase(rit))

        auto it = findItemByGuid(m_resources, resource, QStringLiteral("Resource"));
        if (it == m_resources.end()) {
            return;
        }

        // Removing the resource from the list of resources waiting for processing
        Q_UNUSED(m_resources.erase(it))

        // need to find the note owning the resource to proceed
        if (!resource.hasNoteGuid()) {
            ErrorString errorDescription(QT_TR_NOOP("Found duplicate resource in the local storage which "
                                                    "doesn't have a note guid"));
            QNWARNING(errorDescription << QStringLiteral(": ") << resource);
            Q_EMIT failure(errorDescription);
            return;
        }

        Q_UNUSED(m_resourceFoundFlagPerFindResourceRequestId.insert(requestId))

        QUuid findNotePerResourceRequestId = QUuid::createUuid();
        m_resourcesWithFindRequestIdsPerFindNoteRequestId[findNotePerResourceRequestId] =
            QPair<Resource,QUuid>(resource, requestId);

        Note noteToFind;
        noteToFind.unsetLocalUid();
        noteToFind.setGuid(resource.noteGuid());

        QNTRACE(QStringLiteral("Emitting the request to find resource's note by guid: request id = ") << findNotePerResourceRequestId
                << QStringLiteral(", note: ") << noteToFind);
        Q_EMIT findNote(noteToFind, /* with resource binary data = */ true, findNotePerResourceRequestId);
        return;
    }
}

void RemoteToLocalSynchronizationManager::onFindResourceFailed(Resource resource,
                                                               bool withResourceBinaryData,
                                                               ErrorString errorDescription, QUuid requestId)
{
    Q_UNUSED(withResourceBinaryData)

    QSet<QUuid>::iterator rit = m_findResourceByGuidRequestIds.find(requestId);
    if (rit != m_findResourceByGuidRequestIds.end())
    {
        QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onFindResourceFailed: resource = ")
                << resource << QStringLiteral(", requestId = ") << requestId);

        Q_UNUSED(m_findResourceByGuidRequestIds.erase(rit))

        auto it = findItemByGuid(m_resources, resource, QStringLiteral("Resource"));
        if (it == m_resources.end()) {
            return;
        }

        // Removing the resource from the list of resources waiting for processing
        Q_UNUSED(m_resources.erase(it))

        // need to find the note owning the resource to proceed
        if (!resource.hasNoteGuid()) {
            errorDescription.setBase(QT_TR_NOOP("Detected resource which doesn't have note guid set"));
            QNWARNING(errorDescription << QStringLiteral(": ") << resource);
            Q_EMIT failure(errorDescription);
            return;
        }

        QUuid findNotePerResourceRequestId = QUuid::createUuid();
        m_resourcesWithFindRequestIdsPerFindNoteRequestId[findNotePerResourceRequestId] =
            QPair<Resource,QUuid>(resource, requestId);

        Note noteToFind;
        noteToFind.unsetLocalUid();
        noteToFind.setGuid(resource.noteGuid());

        QNTRACE(QStringLiteral("Emitting the request to find resource's note by guid: request id = ") << findNotePerResourceRequestId
                << QStringLiteral(", note: ") << noteToFind);
        Q_EMIT findNote(noteToFind, /* with resource binary data = */ false, findNotePerResourceRequestId);
        return;
    }
}

void RemoteToLocalSynchronizationManager::onFindLinkedNotebookCompleted(LinkedNotebook linkedNotebook,
                                                                        QUuid requestId)
{
    Q_UNUSED(onFoundDuplicateByGuid(linkedNotebook, requestId, QStringLiteral("LinkedNotebook"), m_linkedNotebooks,
                                    m_linkedNotebooksPendingAddOrUpdate, m_findLinkedNotebookRequestIds))
}

void RemoteToLocalSynchronizationManager::onFindLinkedNotebookFailed(LinkedNotebook linkedNotebook,
                                                                     ErrorString errorDescription, QUuid requestId)
{
    QSet<QUuid>::iterator rit = m_findLinkedNotebookRequestIds.find(requestId);
    if (rit == m_findLinkedNotebookRequestIds.end()) {
        return;
    }

    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onFindLinkedNotebookFailed: ")
            << linkedNotebook << QStringLiteral(", errorDescription = ") << errorDescription
            << QStringLiteral(", requestId = ") << requestId);

    Q_UNUSED(m_findLinkedNotebookRequestIds.erase(rit));

    LinkedNotebooksList::iterator it = findItemByGuid(m_linkedNotebooks, linkedNotebook, QStringLiteral("LinkedNotebook"));
    if (it == m_linkedNotebooks.end()) {
        return;
    }

    // This linked notebook was not found in the local storage by guid, adding it there
    emitAddRequest(linkedNotebook);
}

void RemoteToLocalSynchronizationManager::onFindSavedSearchCompleted(SavedSearch savedSearch, QUuid requestId)
{
    bool foundByGuid = onFoundDuplicateByGuid(savedSearch, requestId, QStringLiteral("SavedSearch"), m_savedSearches,
                                              m_savedSearchesPendingAddOrUpdate, m_findSavedSearchByGuidRequestIds);
    if (foundByGuid) {
        return;
    }

    bool foundByName = onFoundDuplicateByName(savedSearch, requestId, QStringLiteral("SavedSearch"), m_savedSearches,
                                              m_savedSearchesPendingAddOrUpdate, m_findSavedSearchByNameRequestIds);
    if (foundByName) {
        return;
    }
}

void RemoteToLocalSynchronizationManager::onFindSavedSearchFailed(SavedSearch savedSearch,
                                                                  ErrorString errorDescription, QUuid requestId)
{
    bool failedToFindByGuid = onNoDuplicateByGuid(savedSearch, requestId, errorDescription, QStringLiteral("SavedSearch"),
                                                  m_savedSearches, m_findSavedSearchByGuidRequestIds);
    if (failedToFindByGuid) {
        return;
    }

    bool failedToFindByName = onNoDuplicateByName(savedSearch, requestId, errorDescription, QStringLiteral("SavedSearch"),
                                                  m_savedSearches, m_findSavedSearchByNameRequestIds);
    if (failedToFindByName) {
        return;
    }
}

template <class ElementType>
void RemoteToLocalSynchronizationManager::onAddDataElementCompleted(const ElementType & element,
                                                                    const QUuid & requestId,
                                                                    const QString & typeName,
                                                                    QSet<QUuid> & addElementRequestIds)
{
    QSet<QUuid>::iterator it = addElementRequestIds.find(requestId);
    if (it != addElementRequestIds.end())
    {
        QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onAddDataElementCompleted<") << typeName
                << QStringLiteral(">: ") << typeName << QStringLiteral(" = ") << element << QStringLiteral(", requestId = ")
                << requestId);

        Q_UNUSED(addElementRequestIds.erase(it));
        performPostAddOrUpdateChecks<ElementType>(element);
        checkServerDataMergeCompletion();
    }
}

void RemoteToLocalSynchronizationManager::onAddUserCompleted(User user, QUuid requestId)
{
    if (requestId != m_addOrUpdateUserRequestId) {
        return;
    }

    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onAddUserCompleted: user = ") << user
            << QStringLiteral("\nRequest id = ") << requestId);

    m_addOrUpdateUserRequestId = QUuid();
    m_onceAddedOrUpdatedUserInLocalStorage = true;
}

void RemoteToLocalSynchronizationManager::onAddUserFailed(User user, ErrorString errorDescription, QUuid requestId)
{
    if (requestId != m_addOrUpdateUserRequestId) {
        return;
    }

    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onAddUserFailed: ") << user
            << QStringLiteral("\nRequest id = ") << requestId);

    ErrorString error(QT_TR_NOOP("Failed to add the user data fetched from the remote database to the local storage"));
    error.additionalBases().append(errorDescription.base());
    error.additionalBases().append(errorDescription.additionalBases());
    error.details() = errorDescription.details();
    Q_EMIT failure(error);

    m_addOrUpdateUserRequestId = QUuid();
}

void RemoteToLocalSynchronizationManager::onAddTagCompleted(Tag tag, QUuid requestId)
{
    onAddDataElementCompleted(tag, requestId, QStringLiteral("Tag"), m_addTagRequestIds);
}

void RemoteToLocalSynchronizationManager::onAddSavedSearchCompleted(SavedSearch search, QUuid requestId)
{
    onAddDataElementCompleted(search, requestId, QStringLiteral("SavedSearch"), m_addSavedSearchRequestIds);
}

template <class ElementType>
void RemoteToLocalSynchronizationManager::onAddDataElementFailed(const ElementType & element, const QUuid & requestId,
                                                                 const ErrorString & errorDescription, const QString & typeName,
                                                                 QSet<QUuid> & addElementRequestIds)
{
    QSet<QUuid>::iterator it = addElementRequestIds.find(requestId);
    if (it != addElementRequestIds.end())
    {
        QNWARNING(QStringLiteral("RemoteToLocalSynchronizationManager::onAddDataElementFailed<") << typeName
                  << QStringLiteral(">: ") << typeName << QStringLiteral(" = ") << element << QStringLiteral("\nError description = ")
                  << errorDescription << QStringLiteral(", requestId = ") << requestId);

        Q_UNUSED(addElementRequestIds.erase(it));

        ErrorString error(QT_TR_NOOP("Failed to add the data item fetched from the remote database to the local storage"));
        error.additionalBases().append(errorDescription.base());
        error.additionalBases().append(errorDescription.additionalBases());
        error.details() = errorDescription.details();
        QNWARNING(error);
        Q_EMIT failure(error);
    }
}

void RemoteToLocalSynchronizationManager::onAddTagFailed(Tag tag, ErrorString errorDescription, QUuid requestId)
{
    onAddDataElementFailed(tag, requestId, errorDescription, QStringLiteral("Tag"), m_addTagRequestIds);
}

void RemoteToLocalSynchronizationManager::onAddSavedSearchFailed(SavedSearch search, ErrorString errorDescription, QUuid requestId)
{
    onAddDataElementFailed(search, requestId, errorDescription, QStringLiteral("SavedSearch"), m_addSavedSearchRequestIds);
}

void RemoteToLocalSynchronizationManager::onUpdateUserCompleted(User user, QUuid requestId)
{
    if (requestId != m_addOrUpdateUserRequestId)
    {
        if (user.hasId() && m_user.hasId() && (user.id() == m_user.id())) {
            QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onUpdateUserCompleted: external update of current user, request id = ")
                    << requestId);
            m_user = user;
        }

        return;
    }

    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onUpdateUserCompleted: user = ") << user
            << QStringLiteral("\nRequest id = ") << requestId);

    m_addOrUpdateUserRequestId = QUuid();
    m_onceAddedOrUpdatedUserInLocalStorage = true;
}

void RemoteToLocalSynchronizationManager::onUpdateUserFailed(User user, ErrorString errorDescription, QUuid requestId)
{
    if (requestId != m_addOrUpdateUserRequestId) {
        return;
    }

    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onUpdateUserFailed: user = ") << user
            << QStringLiteral("\nError description = ") << errorDescription << QStringLiteral(", request id = ")
            << requestId);

    ErrorString error(QT_TR_NOOP("Can't update the user data fetched from the remote database in the local storage"));
    error.additionalBases().append(errorDescription.base());
    error.additionalBases().append(errorDescription.additionalBases());
    error.details() = errorDescription.details();
    Q_EMIT failure(error);

    m_addOrUpdateUserRequestId = QUuid();
}

template <class ElementType>
void RemoteToLocalSynchronizationManager::onUpdateDataElementCompleted(const ElementType & element,
                                                                       const QUuid & requestId, const QString & typeName,
                                                                       QSet<QUuid> & updateElementRequestIds)
{
    QSet<QUuid>::iterator rit = updateElementRequestIds.find(requestId);
    if (rit == updateElementRequestIds.end()) {
        return;
    }

    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizartionManager::onUpdateDataElementCompleted<") << typeName
            << QStringLiteral(">: ") << typeName << QStringLiteral(" = ") << element << QStringLiteral(", requestId = ") << requestId);

    Q_UNUSED(updateElementRequestIds.erase(rit));
    performPostAddOrUpdateChecks<ElementType>(element);
    checkServerDataMergeCompletion();
}

void RemoteToLocalSynchronizationManager::onUpdateTagCompleted(Tag tag, QUuid requestId)
{
    onUpdateDataElementCompleted(tag, requestId, QStringLiteral("Tag"), m_updateTagRequestIds);
}

void RemoteToLocalSynchronizationManager::onUpdateSavedSearchCompleted(SavedSearch search, QUuid requestId)
{
    onUpdateDataElementCompleted(search, requestId, QStringLiteral("SavedSearch"), m_updateSavedSearchRequestIds);
}

template <class ElementType>
void RemoteToLocalSynchronizationManager::onUpdateDataElementFailed(const ElementType & element, const QUuid & requestId,
                                                                    const ErrorString & errorDescription, const QString & typeName,
                                                                    QSet<QUuid> & updateElementRequestIds)
{
    QSet<QUuid>::iterator it = updateElementRequestIds.find(requestId);
    if (it == updateElementRequestIds.end()) {
        return;
    }

    QNWARNING(QStringLiteral("RemoteToLocalSynchronizationManager::onUpdateDataElementFailed<") << typeName
              << QStringLiteral(">: ") << typeName << QStringLiteral(" = ") << element << QStringLiteral(", errorDescription = ")
              << errorDescription << QStringLiteral(", requestId = ") << requestId);

    Q_UNUSED(updateElementRequestIds.erase(it));

    ErrorString error(QT_TR_NOOP("Can't update the item in the local storage"));
    error.additionalBases().append(errorDescription.base());
    error.additionalBases().append(errorDescription.additionalBases());
    error.details() = errorDescription.details();
    Q_EMIT failure(error);
}

void RemoteToLocalSynchronizationManager::onUpdateTagFailed(Tag tag, ErrorString errorDescription, QUuid requestId)
{
    onUpdateDataElementFailed(tag, requestId, errorDescription, QStringLiteral("Tag"), m_updateTagRequestIds);
}

void RemoteToLocalSynchronizationManager::onExpungeTagCompleted(Tag tag, QStringList expungedChildTagLocalUids, QUuid requestId)
{
    onExpungeDataElementCompleted(tag, requestId, QStringLiteral("Tag"), m_expungeTagRequestIds);
    Q_UNUSED(expungedChildTagLocalUids)
}

void RemoteToLocalSynchronizationManager::onExpungeTagFailed(Tag tag, ErrorString errorDescription, QUuid requestId)
{
    onExpungeDataElementFailed(tag, requestId, errorDescription, QStringLiteral("Tag"), m_expungeTagRequestIds);
}

void RemoteToLocalSynchronizationManager::onExpungeNotelessTagsFromLinkedNotebooksCompleted(QUuid requestId)
{
    if (requestId == m_expungeNotelessTagsRequestId) {
        QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onExpungeNotelessTagsFromLinkedNotebooksCompleted"));
        m_expungeNotelessTagsRequestId = QUuid();
        finalize();
        return;
    }
}

void RemoteToLocalSynchronizationManager::onExpungeNotelessTagsFromLinkedNotebooksFailed(ErrorString errorDescription, QUuid requestId)
{
    if (requestId != m_expungeNotelessTagsRequestId) {
        return;
    }

    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onExpungeNotelessTagsFromLinkedNotebooksFailed: ")
            << errorDescription);
    m_expungeNotelessTagsRequestId = QUuid();

    ErrorString error(QT_TR_NOOP("Failed to expunge the noteless tags belonging to linked notebooks from the local storage"));
    error.additionalBases().append(errorDescription.base());
    error.additionalBases().append(errorDescription.additionalBases());
    error.details() = errorDescription.details();
    Q_EMIT failure(error);
}

void RemoteToLocalSynchronizationManager::onUpdateSavedSearchFailed(SavedSearch search, ErrorString errorDescription,
                                                                    QUuid requestId)
{
    onUpdateDataElementFailed(search, requestId, errorDescription, QStringLiteral("SavedSearch"), m_updateSavedSearchRequestIds);
}

void RemoteToLocalSynchronizationManager::onExpungeSavedSearchCompleted(SavedSearch search, QUuid requestId)
{
    onExpungeDataElementCompleted(search, requestId, QStringLiteral("SavedSearch"), m_expungeSavedSearchRequestIds);
}

void RemoteToLocalSynchronizationManager::onExpungeSavedSearchFailed(SavedSearch search, ErrorString errorDescription, QUuid requestId)
{
    onExpungeDataElementFailed(search, requestId, errorDescription, QStringLiteral("SavedSearch"), m_expungeSavedSearchRequestIds);
}

template <>
void RemoteToLocalSynchronizationManager::performPostAddOrUpdateChecks<Tag>(const Tag & tag)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::performPostAddOrUpdateChecks<Tag>: ") << tag);

    unregisterTagPendingAddOrUpdate(tag);
    syncNextTagPendingProcessing();
    checkNotebooksAndTagsSyncCompletionAndLaunchNotesSync();

    if (m_tagsPendingProcessing.isEmpty() && m_addTagRequestIds.isEmpty() && m_updateTagRequestIds.isEmpty()) {
        expungeTags();
    }
}

template <>
void RemoteToLocalSynchronizationManager::performPostAddOrUpdateChecks<Notebook>(const Notebook & notebook)
{
    unregisterNotebookPendingAddOrUpdate(notebook);
    checkNotebooksAndTagsSyncCompletionAndLaunchNotesSync();
}

template <>
void RemoteToLocalSynchronizationManager::performPostAddOrUpdateChecks<Note>(const Note & note)
{
    unregisterNotePendingAddOrUpdate(note);
    checkNotesSyncCompletionAndLaunchResourcesSync();

    if (m_findNoteByGuidRequestIds.isEmpty() && m_guidsOfNotesPendingDownloadForAddingToLocalStorage.isEmpty() &&
        m_notesPendingDownloadForUpdatingInLocalStorageByGuid.isEmpty() &&
        m_addNoteRequestIds.isEmpty() && m_updateNoteRequestIds.isEmpty() &&
        m_notesToAddPerAPICallPostponeTimerId.isEmpty() && m_notesToUpdatePerAPICallPostponeTimerId.isEmpty())
    {
        if (!m_resources.isEmpty() ||
            !m_notesOwningResourcesPendingDownloadForAddingToLocalStorageByResourceGuid.isEmpty() ||
            !m_resourcesPendingDownloadForUpdatingInLocalStorageWithNotesByResourceGuid.isEmpty() ||
            !m_resourcesToAddWithNotesPerAPICallPostponeTimerId.isEmpty() ||
            !m_resourcesToUpdateWithNotesPerAPICallPostponeTimerId.isEmpty() ||
            !m_postponedConflictingResourceDataPerAPICallPostponeTimerId.isEmpty())
        {
            return;
        }

        if (!m_expungedNotes.isEmpty()) {
            expungeNotes();
        }
        else if (!m_expungedNotebooks.isEmpty()) {
            expungeNotebooks();
        }
        else {
            expungeLinkedNotebooks();
        }
    }
}

template <>
void RemoteToLocalSynchronizationManager::performPostAddOrUpdateChecks<Resource>(const Resource & resource)
{
    unregisterResourcePendingAddOrUpdate(resource);

    if (m_findResourceByGuidRequestIds.isEmpty() && m_addResourceRequestIds.isEmpty() &&
        m_updateResourceRequestIds.isEmpty() &&
        m_resourcesByMarkNoteOwningResourceDirtyRequestIds.isEmpty() &&
        m_resourcesWithFindRequestIdsPerFindNoteRequestId.isEmpty() &&
        m_inkNoteResourceDataPerFindNotebookRequestId.isEmpty() &&
        m_resourceGuidsPendingInkNoteImageDownloadPerNoteGuid.isEmpty() &&
        m_notesPendingInkNoteImagesDownloadByFindNotebookRequestId.isEmpty() &&
        m_notesPendingThumbnailDownloadByFindNotebookRequestId.isEmpty() &&
        m_notesPendingThumbnailDownloadByGuid.isEmpty() &&
        m_notesOwningResourcesPendingDownloadForAddingToLocalStorageByResourceGuid.isEmpty() &&
        m_resourcesPendingDownloadForUpdatingInLocalStorageWithNotesByResourceGuid.isEmpty() &&
        m_resourcesToAddWithNotesPerAPICallPostponeTimerId.isEmpty() &&
        m_resourcesToUpdateWithNotesPerAPICallPostponeTimerId.isEmpty() &&
        m_postponedConflictingResourceDataPerAPICallPostponeTimerId.isEmpty())
    {
        if (!m_expungedNotes.isEmpty()) {
            expungeNotes();
        }
        else if (!m_expungedNotebooks.isEmpty()) {
            expungeNotebooks();
        }
        else {
            expungeLinkedNotebooks();
        }
    }
}

template <>
void RemoteToLocalSynchronizationManager::performPostAddOrUpdateChecks<SavedSearch>(const SavedSearch & search)
{
    unregisterSavedSearchPendingAddOrUpdate(search);

    if (m_addSavedSearchRequestIds.isEmpty() && m_updateSavedSearchRequestIds.isEmpty()) {
        expungeSavedSearches();
    }
}

template <class ElementType>
void RemoteToLocalSynchronizationManager::unsetLocalUid(ElementType & element)
{
    element.unsetLocalUid();
}

template <>
void RemoteToLocalSynchronizationManager::unsetLocalUid<LinkedNotebook>(LinkedNotebook &)
{
    // do nothing, local uid doesn't make any sense to linked notebook
}

template <class ElementType>
void RemoteToLocalSynchronizationManager::setNonLocalAndNonDirty(ElementType & element)
{
    element.setLocal(false);
    element.setDirty(false);
}

template <>
void RemoteToLocalSynchronizationManager::setNonLocalAndNonDirty<LinkedNotebook>(LinkedNotebook & linkedNotebook)
{
    linkedNotebook.setDirty(false);
}

template <class ElementType>
void RemoteToLocalSynchronizationManager::onExpungeDataElementCompleted(const ElementType & element, const QUuid & requestId,
                                                                        const QString & typeName, QSet<QUuid> & expungeElementRequestIds)
{
    auto it = expungeElementRequestIds.find(requestId);
    if (it == expungeElementRequestIds.end()) {
        return;
    }

    QNTRACE(QStringLiteral("Expunged ") << typeName << QStringLiteral(" from local storage: ") << element);
    Q_UNUSED(expungeElementRequestIds.erase(it))

    performPostExpungeChecks<ElementType>();
    checkExpungesCompletion();
}

template <class ElementType>
void RemoteToLocalSynchronizationManager::onExpungeDataElementFailed(const ElementType & element, const QUuid & requestId,
                                                                     const ErrorString & errorDescription, const QString & typeName,
                                                                     QSet<QUuid> & expungeElementRequestIds)
{
    Q_UNUSED(element)
    Q_UNUSED(typeName)

    auto it = expungeElementRequestIds.find(requestId);
    if (it == expungeElementRequestIds.end()) {
        return;
    }

    QNDEBUG(QStringLiteral("Failed to expunge ") << typeName
            << QStringLiteral(" from the local storage; won't panic since most likely the corresponding data element "
                              "has never existed in the local storage in the first place. Error description: ")
            << errorDescription);
    Q_UNUSED(expungeElementRequestIds.erase(it))

    performPostExpungeChecks<ElementType>();
    checkExpungesCompletion();
}

void RemoteToLocalSynchronizationManager::expungeTags()
{
    if (m_expungedTags.isEmpty()) {
        return;
    }

    Tag tagToExpunge;
    tagToExpunge.unsetLocalUid();

    const int numExpungedTags = m_expungedTags.size();
    for(int i = 0; i < numExpungedTags; ++i)
    {
        const QString & expungedTagGuid = m_expungedTags[i];
        tagToExpunge.setGuid(expungedTagGuid);

        QUuid expungeTagRequestId = QUuid::createUuid();
        Q_UNUSED(m_expungeTagRequestIds.insert(expungeTagRequestId));
        Q_EMIT expungeTag(tagToExpunge, expungeTagRequestId);
    }

    m_expungedTags.clear();
}

void RemoteToLocalSynchronizationManager::expungeSavedSearches()
{
    if (m_expungedSavedSearches.isEmpty()) {
        return;
    }

    SavedSearch searchToExpunge;
    searchToExpunge.unsetLocalUid();

    const int numExpungedSearches = m_expungedSavedSearches.size();
    for(int i = 0; i < numExpungedSearches; ++i)
    {
        const QString & expungedSavedSerchGuid = m_expungedSavedSearches[i];
        searchToExpunge.setGuid(expungedSavedSerchGuid);

        QUuid expungeSavedSearchRequestId = QUuid::createUuid();
        Q_UNUSED(m_expungeSavedSearchRequestIds.insert(expungeSavedSearchRequestId));
        Q_EMIT expungeSavedSearch(searchToExpunge, expungeSavedSearchRequestId);
    }

    m_expungedSavedSearches.clear();
}

void RemoteToLocalSynchronizationManager::expungeLinkedNotebooks()
{
    if (m_expungedLinkedNotebooks.isEmpty()) {
        return;
    }

    LinkedNotebook linkedNotebookToExpunge;

    const int numExpungedLinkedNotebooks = m_expungedLinkedNotebooks.size();
    for(int i = 0; i < numExpungedLinkedNotebooks; ++i)
    {
        const QString & expungedLinkedNotebookGuid = m_expungedLinkedNotebooks[i];
        linkedNotebookToExpunge.setGuid(expungedLinkedNotebookGuid);

        QUuid expungeLinkedNotebookRequestId = QUuid::createUuid();
        Q_UNUSED(m_expungeLinkedNotebookRequestIds.insert(expungeLinkedNotebookRequestId));
        Q_EMIT expungeLinkedNotebook(linkedNotebookToExpunge, expungeLinkedNotebookRequestId);
    }

    m_expungedLinkedNotebooks.clear();
}

void RemoteToLocalSynchronizationManager::expungeNotebooks()
{
    if (m_expungedNotebooks.isEmpty()) {
        return;
    }

    Notebook notebookToExpunge;
    notebookToExpunge.unsetLocalUid();

    const int numExpungedNotebooks = m_expungedNotebooks.size();
    for(int i = 0; i < numExpungedNotebooks; ++i)
    {
        const QString & expungedNotebookGuid = m_expungedNotebooks[i];
        notebookToExpunge.setGuid(expungedNotebookGuid);

        QUuid expungeNotebookRequestId = QUuid::createUuid();
        Q_UNUSED(m_expungeNotebookRequestIds.insert(expungeNotebookRequestId));
        Q_EMIT expungeNotebook(notebookToExpunge, expungeNotebookRequestId);
    }

    m_expungedNotebooks.clear();
}

void RemoteToLocalSynchronizationManager::expungeNotes()
{
    if (m_expungedNotes.isEmpty()) {
        return;
    }

    Note noteToExpunge;
    noteToExpunge.unsetLocalUid();

    const int numExpungedNotes = m_expungedNotes.size();
    for(int i = 0; i < numExpungedNotes; ++i)
    {
        const QString & expungedNoteGuid = m_expungedNotes[i];
        noteToExpunge.setGuid(expungedNoteGuid);

        QUuid expungeNoteRequestId = QUuid::createUuid();
        Q_UNUSED(m_expungeNoteRequestIds.insert(expungeNoteRequestId));
        Q_EMIT expungeNote(noteToExpunge, expungeNoteRequestId);
    }

    m_expungedNotes.clear();
}

template <class ElementType>
void RemoteToLocalSynchronizationManager::performPostExpungeChecks()
{
    // do nothing by default
}

template <>
void RemoteToLocalSynchronizationManager::performPostExpungeChecks<Note>()
{
    if (!m_expungeNoteRequestIds.isEmpty()) {
        return;
    }

    if (!m_expungedNotebooks.isEmpty()) {
        expungeNotebooks();
        return;
    }

    expungeSavedSearches();
    expungeTags();
    expungeLinkedNotebooks();
}

template <>
void RemoteToLocalSynchronizationManager::performPostExpungeChecks<Notebook>()
{
    if (!m_expungeNotebookRequestIds.isEmpty()) {
        return;
    }

    expungeSavedSearches();
    expungeTags();
    expungeLinkedNotebooks();
}

void RemoteToLocalSynchronizationManager::expungeFromServerToClient()
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::expungeFromServerToClient"));

    expungeNotes();
    expungeNotebooks();
    expungeSavedSearches();
    expungeTags();
    expungeLinkedNotebooks();

    checkExpungesCompletion();
}

void RemoteToLocalSynchronizationManager::checkExpungesCompletion()
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::checkExpungesCompletion"));

    if (m_expungedTags.isEmpty() && m_expungeTagRequestIds.isEmpty() &&
        m_expungedNotebooks.isEmpty() && m_expungeNotebookRequestIds.isEmpty() &&
        m_expungedSavedSearches.isEmpty() && m_expungeSavedSearchRequestIds.isEmpty() &&
        m_expungedLinkedNotebooks.isEmpty() && m_expungeLinkedNotebookRequestIds.isEmpty() &&
        m_expungedNotes.isEmpty() && m_expungeNoteRequestIds.isEmpty())
    {
        QNDEBUG(QStringLiteral("No pending expunge requests"));

        if (syncingLinkedNotebooksContent())
        {
            m_expungeNotelessTagsRequestId = QUuid::createUuid();
            QNTRACE(QStringLiteral("Emitting the request to expunge noteless tags from local storage: request id = ")
                    << m_expungeNotelessTagsRequestId);
            Q_EMIT expungeNotelessTagsFromLinkedNotebooks(m_expungeNotelessTagsRequestId);
        }
        else if (!m_expungedFromServerToClient)
        {
            m_expungedFromServerToClient = true;
            Q_EMIT expungedFromServerToClient();

            startLinkedNotebooksSync();
        }
    }
}

template <class ElementType>
void RemoteToLocalSynchronizationManager::checkAndAddLinkedNotebookBinding(ElementType & element)
{
    Q_UNUSED(element);
    // Do nothing in default instantiation, only tags and notebooks need to be processed specifically
}

template <>
void RemoteToLocalSynchronizationManager::checkAndAddLinkedNotebookBinding<Notebook>(Notebook & notebook)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::checkAndAddLinkedNotebookBinding<Notebook>: ") << notebook);

    if (!notebook.hasGuid()) {
        QNDEBUG(QStringLiteral("The notebook has no guid"));
        return;
    }

    auto it = m_linkedNotebookGuidsByNotebookGuids.find(notebook.guid());
    if (it == m_linkedNotebookGuidsByNotebookGuids.end()) {
        QNDEBUG(QStringLiteral("Found no linked notebook guid for notebook guid ") << notebook.guid());
        return;
    }

    notebook.setLinkedNotebookGuid(it.value());
    QNDEBUG(QStringLiteral("Set linked notebook guid ") << it.value() << QStringLiteral(" to the notebook"));

    // NOTE: the notebook coming from the linked notebook might be marked as
    // default and/or last used which might not make much sense in the context
    // ofthe user's own default and/or last used notebooks so removing these two
    // properties
    notebook.setLastUsed(false);
    notebook.setDefaultNotebook(false);
}

template <>
void RemoteToLocalSynchronizationManager::checkAndAddLinkedNotebookBinding<Tag>(Tag & tag)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::checkAndAddLinkedNotebookBinding<Tag>: ") << tag);

    if (!tag.hasGuid()) {
        QNDEBUG(QStringLiteral("The tag has no guid"));
        return;
    }

    auto it = m_linkedNotebookGuidsByTagGuids.find(tag.guid());
    if (it == m_linkedNotebookGuidsByTagGuids.end()) {
        QNDEBUG(QStringLiteral("Found no linked notebook guid for tag guid ") << tag.guid());
        return;
    }

    tag.setLinkedNotebookGuid(it.value());
    QNDEBUG(QStringLiteral("Set linked notebook guid ") << it.value() << QStringLiteral(" to the tag"));
}

template <>
void RemoteToLocalSynchronizationManager::emitFindByGuidRequest<qevercloud::Tag>(const qevercloud::Tag & qecTag)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::emitFindByGuidRequest<Tag>: tag = ") << qecTag);

    if (Q_UNLIKELY(!qecTag.guid.isSet())) {
        ErrorString errorDescription(QT_TR_NOOP("Internal error: detected attempt to find tag by guid using "
                                                "tag which doesn't have a guid"));
        QNWARNING(errorDescription << QStringLiteral(": ") << qecTag);
        Q_EMIT failure(errorDescription);
        return;
    }

    Tag tag;
    tag.unsetLocalUid();
    tag.setGuid(qecTag.guid.ref());
    checkAndAddLinkedNotebookBinding(tag);

    QUuid requestId = QUuid::createUuid();
    Q_UNUSED(m_findTagByGuidRequestIds.insert(requestId));
    QNTRACE(QStringLiteral("Emitting the request to find tag in the local storage: request id = ") << requestId
            << QStringLiteral(", tag: ") << tag);
    Q_EMIT findTag(tag, requestId);
}

template <>
void RemoteToLocalSynchronizationManager::emitFindByGuidRequest<qevercloud::SavedSearch>(const qevercloud::SavedSearch & qecSearch)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::emitFindByGuidRequest<SavedSearch>: search = ") << qecSearch);

    if (Q_UNLIKELY(!qecSearch.guid.isSet())) {
        ErrorString errorDescription(QT_TR_NOOP("Internal error: detected attempt to find saved search by guid using "
                                                "saved search which doesn't have a guid"));
        QNWARNING(errorDescription << QStringLiteral(": ") << qecSearch);
        Q_EMIT failure(errorDescription);
        return;
    }

    SavedSearch search;
    search.unsetLocalUid();
    search.setGuid(qecSearch.guid.ref());

    QUuid requestId = QUuid::createUuid();
    Q_UNUSED(m_findSavedSearchByGuidRequestIds.insert(requestId));
    QNTRACE(QStringLiteral("Emitting the request to find saved search in the local storage: request id = ") << requestId
            << QStringLiteral(", saved search: ") << search);
    Q_EMIT findSavedSearch(search, requestId);
}

template <>
void RemoteToLocalSynchronizationManager::emitFindByGuidRequest<qevercloud::Notebook>(const qevercloud::Notebook & qecNotebook)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::emitFindByGuidRequest<Notebook>: notebook = ") << qecNotebook);

    if (Q_UNLIKELY(!qecNotebook.guid.isSet())) {
        ErrorString errorDescription(QT_TR_NOOP("Internal error: detected attempt to find notebook by guid using notebook "
                                                "which doesn't have a guid"));
        QNWARNING(errorDescription << QStringLiteral(": ") << qecNotebook);
        Q_EMIT failure(errorDescription);
        return;
    }

    Notebook notebook;
    notebook.unsetLocalUid();
    notebook.setGuid(qecNotebook.guid.ref());
    checkAndAddLinkedNotebookBinding(notebook);

    QUuid requestId = QUuid::createUuid();
    Q_UNUSED(m_findNotebookByGuidRequestIds.insert(requestId));
    QNTRACE(QStringLiteral("Emitting the request to find notebook in the local storage: request id = ") << requestId
            << QStringLiteral(", notebook: ") << notebook);
    Q_EMIT findNotebook(notebook, requestId);
}

template <>
void RemoteToLocalSynchronizationManager::emitFindByGuidRequest<qevercloud::LinkedNotebook>(const qevercloud::LinkedNotebook & qecLinkedNotebook)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::emitFindByGuidRequest<LinkedNotebook>: linked notebook = ")
            << qecLinkedNotebook);

    if (Q_UNLIKELY(!qecLinkedNotebook.guid.isSet())) {
        ErrorString errorDescription(QT_TR_NOOP("Internal error: detected attempt to find linked notebook by guid using "
                                                "linked notebook which doesn't have a guid"));
        QNWARNING(errorDescription << QStringLiteral(": ") << qecLinkedNotebook);
        Q_EMIT failure(errorDescription);
        return;
    }

    LinkedNotebook linkedNotebook(qecLinkedNotebook);

    QUuid requestId = QUuid::createUuid();
    Q_UNUSED(m_findLinkedNotebookRequestIds.insert(requestId));
    QNTRACE(QStringLiteral("Emitting the request to find linked notebook in the local storage: request id = ") << requestId
            << QStringLiteral(", linked notebook: ") << linkedNotebook);
    Q_EMIT findLinkedNotebook(linkedNotebook, requestId);
}

template <>
void RemoteToLocalSynchronizationManager::emitFindByGuidRequest<qevercloud::Note>(const qevercloud::Note & qecNote)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::emitFindByGuidRequest<Note>: note = ") << qecNote);

    if (Q_UNLIKELY(!qecNote.guid.isSet())) {
        ErrorString errorDescription(QT_TR_NOOP("Internal error: detected attempt to find note by guid using "
                                                "note which doesn't have a guid"));
        QNWARNING(errorDescription << QStringLiteral(": ") << qecNote);
        Q_EMIT failure(errorDescription);
        return;
    }

    if (Q_UNLIKELY(!qecNote.notebookGuid.isSet())) {
        ErrorString errorDescription(QT_TR_NOOP("Internal error: the note from the Evernote service has no notebook guid"));
        QNWARNING(errorDescription << QStringLiteral(": ") << qecNote);
        Q_EMIT failure(errorDescription);
        return;
    }

    Note note;
    note.unsetLocalUid();
    note.setGuid(qecNote.guid.ref());
    note.setNotebookGuid(qecNote.notebookGuid.ref());

    QUuid requestId = QUuid::createUuid();
    Q_UNUSED(m_findNoteByGuidRequestIds.insert(requestId));
    bool withResourceDataOption = false;
    QNTRACE(QStringLiteral("Emiting the request to find note in the local storage: request id = ") << requestId
            << QStringLiteral(", note: ") << note);
    Q_EMIT findNote(note, withResourceDataOption, requestId);
}

template <>
void RemoteToLocalSynchronizationManager::emitFindByGuidRequest<qevercloud::Resource>(const qevercloud::Resource & qecResource)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::emitFindByGuidRequest<Resource>: resource = ") << qecResource);

    if (Q_UNLIKELY(!qecResource.guid.isSet())) {
        ErrorString errorDescription(QT_TR_NOOP("Internal error: detected attempt to find resource by guid using "
                                                "resource which doesn't have a guid"));
        QNWARNING(errorDescription << QStringLiteral(": ") << qecResource);
        Q_EMIT failure(errorDescription);
        return;
    }

    if (Q_UNLIKELY(!qecResource.noteGuid.isSet())) {
        ErrorString errorDescription(QT_TR_NOOP("Internal error: detected attempt to find resource by guid using "
                                                "resource which doesn't have a note guid"));
        QNWARNING(errorDescription << QStringLiteral(": ") << qecResource);
        Q_EMIT failure(errorDescription);
        return;
    }

    Resource resource;

    // NOTE: this is very important! If the resource is not dirty, the failure to find it in the local storage
    // (i.e. when the resource is new) might cause the sync conflict resulting in conflicts of notes
    resource.setDirty(false);

    resource.setLocal(false);
    resource.unsetLocalUid();
    resource.setGuid(qecResource.guid.ref());
    resource.setNoteGuid(qecResource.noteGuid.ref());

    QUuid requestId = QUuid::createUuid();
    Q_UNUSED(m_findResourceByGuidRequestIds.insert(requestId));
    bool withBinaryData = false;
    QNTRACE(QStringLiteral("Emitting the request to find resource in the local storage: request id = ") << requestId
            << QStringLiteral(", resource: ") << resource);
    Q_EMIT findResource(resource, withBinaryData, requestId);
}

void RemoteToLocalSynchronizationManager::onAddLinkedNotebookCompleted(LinkedNotebook linkedNotebook, QUuid requestId)
{
    handleLinkedNotebookAdded(linkedNotebook);

    auto it = m_addLinkedNotebookRequestIds.find(requestId);
    if (it != m_addLinkedNotebookRequestIds.end())
    {
        QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onAddLinkedNotebookCompleted: linked notebook = ")
                << linkedNotebook << QStringLiteral(", request id = ") << requestId);

        Q_UNUSED(m_addLinkedNotebookRequestIds.erase(it))
        checkServerDataMergeCompletion();
    }
}

void RemoteToLocalSynchronizationManager::onAddLinkedNotebookFailed(LinkedNotebook linkedNotebook,
                                                                    ErrorString errorDescription, QUuid requestId)
{
    onAddDataElementFailed(linkedNotebook, requestId, errorDescription,
                           QStringLiteral("LinkedNotebook"), m_addLinkedNotebookRequestIds);
}

void RemoteToLocalSynchronizationManager::onUpdateLinkedNotebookCompleted(LinkedNotebook linkedNotebook,
                                                                          QUuid requestId)
{
    handleLinkedNotebookUpdated(linkedNotebook);

    auto it = m_updateLinkedNotebookRequestIds.find(requestId);
    if (it != m_updateLinkedNotebookRequestIds.end())
    {
        QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onUpdateLinkedNotebookCompleted: linkedNotebook = ")
                << linkedNotebook << QStringLiteral(", requestId = ") << requestId);

        Q_UNUSED(m_updateLinkedNotebookRequestIds.erase(it));
        checkServerDataMergeCompletion();
    }
}

void RemoteToLocalSynchronizationManager::onUpdateLinkedNotebookFailed(LinkedNotebook linkedNotebook,
                                                                       ErrorString errorDescription, QUuid requestId)
{
    QSet<QUuid>::iterator it = m_updateLinkedNotebookRequestIds.find(requestId);
    if (it != m_updateLinkedNotebookRequestIds.end())
    {
        QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onUpdateLinkedNotebookFailed: linkedNotebook = ")
                << linkedNotebook << QStringLiteral(", errorDescription = ") << errorDescription
                << QStringLiteral(", requestId = ") << requestId);

        ErrorString error(QT_TR_NOOP("Failed to update a linked notebook in the local storage"));
        error.additionalBases().append(errorDescription.base());
        error.additionalBases().append(errorDescription.additionalBases());
        error.details() = errorDescription.details();
        Q_EMIT failure(error);
    }
}

void RemoteToLocalSynchronizationManager::onExpungeLinkedNotebookCompleted(LinkedNotebook linkedNotebook, QUuid requestId)
{
    onExpungeDataElementCompleted(linkedNotebook, requestId, QStringLiteral("Linked notebook"), m_expungeLinkedNotebookRequestIds);

    if (Q_UNLIKELY(!linkedNotebook.hasGuid())) {
        QNWARNING(QStringLiteral("Detected expunging of a linked notebook without guid: ") << linkedNotebook);
        return;
    }

    const QString & linkedNotebookGuid = linkedNotebook.guid();

    auto notebookSyncCacheIt = m_notebookSyncCachesByLinkedNotebookGuids.find(linkedNotebookGuid);
    if (notebookSyncCacheIt != m_notebookSyncCachesByLinkedNotebookGuids.end())
    {
        NotebookSyncCache * pNotebookSyncCache = notebookSyncCacheIt.value();
        if (pNotebookSyncCache) {
            pNotebookSyncCache->disconnect();
            pNotebookSyncCache->setParent(Q_NULLPTR);
            pNotebookSyncCache->deleteLater();
        }

        Q_UNUSED(m_notebookSyncCachesByLinkedNotebookGuids.erase(notebookSyncCacheIt))
    }

    auto tagSyncCacheIt = m_tagSyncCachesByLinkedNotebookGuids.find(linkedNotebookGuid);
    if (tagSyncCacheIt != m_tagSyncCachesByLinkedNotebookGuids.end())
    {
        TagSyncCache * pTagSyncCache = tagSyncCacheIt.value();
        if (pTagSyncCache) {
            pTagSyncCache->disconnect();
            pTagSyncCache->setParent(Q_NULLPTR);
            pTagSyncCache->deleteLater();
        }

        Q_UNUSED(m_tagSyncCachesByLinkedNotebookGuids.erase(tagSyncCacheIt))
    }
}

void RemoteToLocalSynchronizationManager::onExpungeLinkedNotebookFailed(LinkedNotebook linkedNotebook, ErrorString errorDescription,
                                                                        QUuid requestId)
{
    onExpungeDataElementFailed(linkedNotebook, requestId, errorDescription, QStringLiteral("Linked notebook"),
                               m_expungeLinkedNotebookRequestIds);
}

void RemoteToLocalSynchronizationManager::onListAllLinkedNotebooksCompleted(size_t limit, size_t offset,
                                                                            LocalStorageManager::ListLinkedNotebooksOrder::type order,
                                                                            LocalStorageManager::OrderDirection::type orderDirection,
                                                                            QList<LinkedNotebook> linkedNotebooks, QUuid requestId)
{
    if (requestId != m_listAllLinkedNotebooksRequestId) {
        return;
    }

    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onListAllLinkedNotebooksCompleted: limit = ") << limit
            << QStringLiteral(", offset = ") << offset << QStringLiteral(", order = ") << order
            << QStringLiteral(", order direction = ") << orderDirection << QStringLiteral(", requestId = ") << requestId);

    m_allLinkedNotebooks = linkedNotebooks;
    m_allLinkedNotebooksListed = true;

    startLinkedNotebooksSync();
}

void RemoteToLocalSynchronizationManager::onListAllLinkedNotebooksFailed(size_t limit, size_t offset,
                                                                         LocalStorageManager::ListLinkedNotebooksOrder::type order,
                                                                         LocalStorageManager::OrderDirection::type orderDirection,
                                                                         ErrorString errorDescription, QUuid requestId)
{
    if (requestId != m_listAllLinkedNotebooksRequestId) {
        return;
    }

    QNWARNING(QStringLiteral("RemoteToLocalSynchronizationManager::onListAllLinkedNotebooksFailed: limit = ") << limit
              << QStringLiteral(", offset = ") << offset << QStringLiteral(", order = ") << order << QStringLiteral(", order direction = ")
              << orderDirection << QStringLiteral(", error description = ") << errorDescription << QStringLiteral("; request id = ")
              << requestId);

    m_allLinkedNotebooksListed = false;

    ErrorString error(QT_TR_NOOP("Failed to list all linked notebooks from the local storage"));
    error.additionalBases().append(errorDescription.base());
    error.additionalBases().append(errorDescription.additionalBases());
    error.details() = errorDescription.details();
    Q_EMIT failure(error);
}

void RemoteToLocalSynchronizationManager::onAddNotebookCompleted(Notebook notebook, QUuid requestId)
{
    onAddDataElementCompleted(notebook, requestId, QStringLiteral("Notebook"), m_addNotebookRequestIds);
}

void RemoteToLocalSynchronizationManager::onAddNotebookFailed(Notebook notebook, ErrorString errorDescription,
                                                              QUuid requestId)
{
    onAddDataElementFailed(notebook, requestId, errorDescription, QStringLiteral("Notebook"), m_addNotebookRequestIds);
}

void RemoteToLocalSynchronizationManager::onUpdateNotebookCompleted(Notebook notebook, QUuid requestId)
{
    onUpdateDataElementCompleted(notebook, requestId, QStringLiteral("Notebook"), m_updateNotebookRequestIds);
}

void RemoteToLocalSynchronizationManager::onUpdateNotebookFailed(Notebook notebook, ErrorString errorDescription,
                                                                 QUuid requestId)
{
    onUpdateDataElementFailed(notebook, requestId, errorDescription, QStringLiteral("Notebook"), m_updateNotebookRequestIds);
}

void RemoteToLocalSynchronizationManager::onExpungeNotebookCompleted(Notebook notebook, QUuid requestId)
{
    onExpungeDataElementCompleted(notebook, requestId, QStringLiteral("Notebook"), m_expungeNotebookRequestIds);
}

void RemoteToLocalSynchronizationManager::onExpungeNotebookFailed(Notebook notebook, ErrorString errorDescription, QUuid requestId)
{
    onExpungeDataElementFailed(notebook, requestId, errorDescription, QStringLiteral("Notebook"), m_expungeNotebookRequestIds);
}

void RemoteToLocalSynchronizationManager::onAddNoteCompleted(Note note, QUuid requestId)
{
    onAddDataElementCompleted(note, requestId, QStringLiteral("Note"), m_addNoteRequestIds);
}

void RemoteToLocalSynchronizationManager::onAddNoteFailed(Note note, ErrorString errorDescription, QUuid requestId)
{
    onAddDataElementFailed(note, requestId, errorDescription, QStringLiteral("Note"), m_addNoteRequestIds);
}

void RemoteToLocalSynchronizationManager::onUpdateNoteCompleted(Note note, bool updateResources,
                                                                bool updateTags, QUuid requestId)
{
    Q_UNUSED(updateResources)
    Q_UNUSED(updateTags)

    auto it = m_updateNoteRequestIds.find(requestId);
    if (it != m_updateNoteRequestIds.end())
    {
        QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onUpdateNoteCompleted: note = ") << note
                << QStringLiteral("\nRequestId = ") << requestId);

        Q_UNUSED(m_updateNoteRequestIds.erase(it));

        performPostAddOrUpdateChecks(note);
        checkServerDataMergeCompletion();
        return;
    }

    auto tit = m_updateNoteWithThumbnailRequestIds.find(requestId);
    if (tit != m_updateNoteWithThumbnailRequestIds.end())
    {
        QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onUpdateNoteCompleted: note with updated thumbnail = ")
                << note << QStringLiteral("\nRequestId = ") << requestId);

        Q_UNUSED(m_updateNoteWithThumbnailRequestIds.erase(tit))

        checkAndIncrementNoteDownloadProgress(note.hasGuid() ? note.guid() : QString());
        performPostAddOrUpdateChecks(note);
        checkServerDataMergeCompletion();
        return;
    }

    auto mdit = m_resourcesByMarkNoteOwningResourceDirtyRequestIds.find(requestId);
    if (mdit != m_resourcesByMarkNoteOwningResourceDirtyRequestIds.end())
    {
        QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onUpdateNoteCompleted: note owning added or updated resource ")
                << QStringLiteral("was marked as dirty: request id = ") << requestId << QStringLiteral(", note: ") << note);

        Resource resource = mdit.value();
        Q_UNUSED(m_resourcesByMarkNoteOwningResourceDirtyRequestIds.erase(mdit))
        performPostAddOrUpdateChecks(resource);
        checkServerDataMergeCompletion();
        return;
    }
}

void RemoteToLocalSynchronizationManager::onUpdateNoteFailed(Note note, bool updateResources, bool updateTags,
                                                             ErrorString errorDescription, QUuid requestId)
{
    Q_UNUSED(updateResources)
    Q_UNUSED(updateTags)

    auto it = m_updateNoteRequestIds.find(requestId);
    if (it != m_updateNoteRequestIds.end())
    {
        QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onUpdateNoteFailed: note = ") << note
                << QStringLiteral("\nErrorDescription = ") << errorDescription << QStringLiteral("\nRequestId = ")
                << requestId);

        Q_UNUSED(m_updateNoteRequestIds.erase(it))

        ErrorString error(QT_TR_NOOP("Failed to update the note in the local storage"));
        error.additionalBases().append(errorDescription.base());
        error.additionalBases().append(errorDescription.additionalBases());
        error.details() = errorDescription.details();
        QNWARNING(error);

        Q_EMIT failure(error);
        return;
    }

    auto tit = m_updateNoteWithThumbnailRequestIds.find(requestId);
    if (tit != m_updateNoteWithThumbnailRequestIds.end())
    {
        QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onUpdateNoteFailed: note with thumbnail = ") << note
                << QStringLiteral("\nErrorDescription = ") << errorDescription << QStringLiteral("\nRequestId = ")
                << requestId);

        QNWARNING(errorDescription);

        Q_UNUSED(m_updateNoteWithThumbnailRequestIds.erase(tit))

        checkAndIncrementNoteDownloadProgress(note.hasGuid() ? note.guid() : QString());
        checkServerDataMergeCompletion();
        return;
    }

    auto mdit = m_resourcesByMarkNoteOwningResourceDirtyRequestIds.find(requestId);
    if (mdit != m_resourcesByMarkNoteOwningResourceDirtyRequestIds.end())
    {
        QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onUpdateNoteFailed: failed to mark the resource owning note as dirty: ")
                << errorDescription << QStringLiteral(", request id = ") << requestId << QStringLiteral(", note: ")
                << note);

        Q_UNUSED(m_resourcesByMarkNoteOwningResourceDirtyRequestIds.erase(mdit))

        ErrorString error(QT_TR_NOOP("Failed to mark the resource owning note dirty in the local storage"));
        error.additionalBases().append(errorDescription.base());
        error.additionalBases().append(errorDescription.additionalBases());
        error.details() = errorDescription.details();
        QNWARNING(error);

        Q_EMIT failure(error);
        return;
    }
}

void RemoteToLocalSynchronizationManager::onExpungeNoteCompleted(Note note, QUuid requestId)
{
    onExpungeDataElementCompleted(note, requestId, QStringLiteral("Note"), m_expungeNoteRequestIds);
}

void RemoteToLocalSynchronizationManager::onExpungeNoteFailed(Note note, ErrorString errorDescription, QUuid requestId)
{
    onExpungeDataElementFailed(note, requestId, errorDescription, QStringLiteral("Note"), m_expungeNoteRequestIds);
}

void RemoteToLocalSynchronizationManager::onAddResourceCompleted(Resource resource, QUuid requestId)
{
    onAddDataElementCompleted(resource, requestId, QStringLiteral("Resource"), m_addResourceRequestIds);
}

void RemoteToLocalSynchronizationManager::onAddResourceFailed(Resource resource, ErrorString errorDescription, QUuid requestId)
{
    onAddDataElementFailed(resource, requestId, errorDescription, QStringLiteral("Resource"), m_addResourceRequestIds);
}

void RemoteToLocalSynchronizationManager::onUpdateResourceCompleted(Resource resource, QUuid requestId)
{
    onUpdateDataElementCompleted<Resource>(resource, requestId, QStringLiteral("Resource"), m_updateResourceRequestIds);
}

void RemoteToLocalSynchronizationManager::onUpdateResourceFailed(Resource resource, ErrorString errorDescription, QUuid requestId)
{
    auto it = m_updateResourceRequestIds.find(requestId);
    if (it != m_updateResourceRequestIds.end())
    {
        QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onUpdateResourceFailed: resource = ")
                << resource << QStringLiteral("\nrequestId = ") << requestId);

        ErrorString error(QT_TR_NOOP("Failed to update the resource in the local storage"));
        error.additionalBases().append(errorDescription.base());
        error.additionalBases().append(errorDescription.additionalBases());
        error.details() = errorDescription.details();
        Q_EMIT failure(error);
    }
}

void RemoteToLocalSynchronizationManager::onInkNoteImageDownloadFinished(bool status, QString resourceGuid,
                                                                         QString noteGuid, ErrorString errorDescription)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onInkNoteImageDownloadFinished: status = ")
            << (status ? QStringLiteral("true") : QStringLiteral("false"))
            << QStringLiteral(", resource guid = ") << resourceGuid << QStringLiteral(", note guid = ") << noteGuid
            << QStringLiteral(", error description = ") << errorDescription);

    if (!status) {
        QNWARNING(errorDescription);
    }

    if (m_resourceGuidsPendingInkNoteImageDownloadPerNoteGuid.remove(noteGuid, resourceGuid)) {
        checkAndIncrementNoteDownloadProgress(noteGuid);
        checkServerDataMergeCompletion();
    }
    else {
        QNDEBUG(QStringLiteral("No such combination of note guid and resource guid was found pending ink note image download"));
    }
}

void RemoteToLocalSynchronizationManager::onNoteThumbnailDownloadingFinished(bool status, QString noteGuid,
                                                                             QByteArray downloadedThumbnailImageData,
                                                                             ErrorString errorDescription)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onNoteThumbnailDownloadingFinished: status = ")
            << (status ? QStringLiteral("true") : QStringLiteral("false")) << QStringLiteral(", note guid = ")
            << noteGuid << QStringLiteral(", error description = ") << errorDescription);

    if (!status) {
        QNWARNING(errorDescription);
        checkAndIncrementNoteDownloadProgress(noteGuid);
        checkServerDataMergeCompletion();
        return;
    }

    auto it = m_notesPendingThumbnailDownloadByGuid.find(noteGuid);
    if (Q_UNLIKELY(it == m_notesPendingThumbnailDownloadByGuid.end())) {
        QNDEBUG(QStringLiteral("Received note thumbnail downloaded event for note which was not pending it; "
                               "the slot invoking must be the stale one, ignoring it"));
        return;
    }

    Note note = it.value();
    Q_UNUSED(m_notesPendingThumbnailDownloadByGuid.erase(it))

    QImage thumbnailImage;
    bool res = thumbnailImage.loadFromData(downloadedThumbnailImageData, "PNG");
    if (Q_UNLIKELY(!res)) {
        QNWARNING("Wasn't able to load the thumbnail image from the downloaded data");
        checkAndIncrementNoteDownloadProgress(noteGuid);
        checkServerDataMergeCompletion();
        return;
    }

    note.setThumbnail(thumbnailImage);

    QUuid updateNoteRequestId = QUuid::createUuid();
    Q_UNUSED(m_updateNoteWithThumbnailRequestIds.insert(updateNoteRequestId))

    QNTRACE(QStringLiteral("Emitting the request to update note with downloaded thumbnail: request id = ")
            << updateNoteRequestId << QStringLiteral(", note: ") << note);
    Q_EMIT updateNote(note, /* update resources = */ false, /* update tags = */ false, updateNoteRequestId);
}

void RemoteToLocalSynchronizationManager::onAuthenticationInfoReceived(QString authToken, QString shardId,
                                                                       qevercloud::Timestamp expirationTime)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onAuthenticationInfoReceived: expiration time = ")
            << printableDateTimeFromTimestamp(expirationTime));

    bool wasPending = m_pendingAuthenticationTokenAndShardId;

    // NOTE: we only need this authentication information to download the thumbnails and ink note images
    m_authenticationToken = authToken;
    m_shardId = shardId;
    m_authenticationTokenExpirationTime = expirationTime;
    m_pendingAuthenticationTokenAndShardId = false;

    if (!wasPending) {
        return;
    }

    launchSync();
}

void RemoteToLocalSynchronizationManager::onAuthenticationTokensForLinkedNotebooksReceived(QHash<QString, QPair<QString,QString> > authenticationTokensAndShardIdsByLinkedNotebookGuid,
                                                                                           QHash<QString, qevercloud::Timestamp> authenticationTokenExpirationTimesByLinkedNotebookGuid)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onAuthenticationTokensForLinkedNotebooksReceived"));

    bool wasPending = m_pendingAuthenticationTokensForLinkedNotebooks;

    m_authenticationTokensAndShardIdsByLinkedNotebookGuid = authenticationTokensAndShardIdsByLinkedNotebookGuid;
    m_authenticationTokenExpirationTimesByLinkedNotebookGuid = authenticationTokenExpirationTimesByLinkedNotebookGuid;
    m_pendingAuthenticationTokensForLinkedNotebooks = false;

    if (!wasPending) {
        QNDEBUG(QStringLiteral("Authentication tokens for linked notebooks were not requested"));
        return;
    }

    startLinkedNotebooksSync();
}

void RemoteToLocalSynchronizationManager::onLastSyncParametersReceived(qint32 lastUpdateCount, qevercloud::Timestamp lastSyncTime,
                                                                       QHash<QString,qint32> lastUpdateCountByLinkedNotebookGuid,
                                                                       QHash<QString,qevercloud::Timestamp> lastSyncTimeByLinkedNotebookGuid)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onLastSyncParametersReceived: last update count = ")
            << lastUpdateCount << QStringLiteral(", last sync time = ") << lastSyncTime
            << QStringLiteral(", last update counts per linked notebook = ") << lastUpdateCountByLinkedNotebookGuid
            << QStringLiteral(", last sync time per linked notebook = ") << lastSyncTimeByLinkedNotebookGuid);

    m_lastUpdateCount = lastUpdateCount;
    m_lastSyncTime = lastSyncTime;
    m_lastUpdateCountByLinkedNotebookGuid = lastUpdateCountByLinkedNotebookGuid;
    m_lastSyncTimeByLinkedNotebookGuid = lastSyncTimeByLinkedNotebookGuid;

    m_gotLastSyncParameters = true;

    if ((m_lastUpdateCount > 0) && (m_lastSyncTime > 0)) {
        m_onceSyncDone = true;
    }

    m_linkedNotebookGuidsOnceFullySynced.clear();
    for(auto it = m_lastSyncTimeByLinkedNotebookGuid.constBegin(), end = m_lastSyncTimeByLinkedNotebookGuid.constEnd(); it != end; ++it)
    {
        const QString & linkedNotebookGuid = it.key();
        qevercloud::Timestamp lastSyncTime = it.value();
        if (lastSyncTime != 0) {
            Q_UNUSED(m_linkedNotebookGuidsOnceFullySynced.insert(linkedNotebookGuid))
        }
    }

    start(m_lastUsnOnStart);
}

void RemoteToLocalSynchronizationManager::setDownloadNoteThumbnails(const bool flag)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::setDownloadNoteThumbnails: flag = ")
            << (flag ? QStringLiteral("true") : QStringLiteral("false")));

    ApplicationSettings appSettings(account(), SYNCHRONIZATION_PERSISTENCE_NAME);
    appSettings.beginGroup(SYNC_SETTINGS_KEY_GROUP);
    appSettings.setValue(SHOULD_DOWNLOAD_NOTE_THUMBNAILS, flag);
    appSettings.endGroup();
}

void RemoteToLocalSynchronizationManager::setDownloadInkNoteImages(const bool flag)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::setDownloadInkNoteImages: flag = ")
            << (flag ? QStringLiteral("true") : QStringLiteral("false")));

    ApplicationSettings appSettings(account(), SYNCHRONIZATION_PERSISTENCE_NAME);
    appSettings.beginGroup(SYNC_SETTINGS_KEY_GROUP);
    appSettings.setValue(SHOULD_DOWNLOAD_INK_NOTE_IMAGES, flag);
    appSettings.endGroup();
}

void RemoteToLocalSynchronizationManager::setInkNoteImagesStoragePath(const QString & path)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::setInkNoteImagesStoragePath: path = ") << path);

    QString actualPath = path;

    QFileInfo pathInfo(path);
    if (!pathInfo.exists())
    {
        QDir pathDir(path);
        bool res = pathDir.mkpath(path);
        if (!res) {
            actualPath = defaultInkNoteImageStoragePath();
            QNWARNING(QStringLiteral("Could not create folder for ink note images storage: ")
                      << path << QStringLiteral(", fallback to using the default path ") << actualPath);
        }
        else {
            QNDEBUG(QStringLiteral("Successfully created the folder for ink note images storage: ") << actualPath);
        }
    }
    else if (Q_UNLIKELY(!pathInfo.isDir()))
    {
        actualPath = defaultInkNoteImageStoragePath();
        QNWARNING(QStringLiteral("The specified ink note images storage path is not a directory: ")
                  << path << QStringLiteral(", fallback to using the default path ") << actualPath);
    }
    else if (Q_UNLIKELY(!pathInfo.isWritable())) {
        actualPath = defaultInkNoteImageStoragePath();
        QNWARNING(QStringLiteral("The specified ink note images storage path is not writable: ")
                  << path << QStringLiteral(", fallback to using the default path ") << actualPath);
    }

    ApplicationSettings appSettings(account(), SYNCHRONIZATION_PERSISTENCE_NAME);
    appSettings.beginGroup(SYNC_SETTINGS_KEY_GROUP);
    appSettings.setValue(INK_NOTE_IMAGES_STORAGE_PATH_KEY, actualPath);
    appSettings.endGroup();
}

void RemoteToLocalSynchronizationManager::collectNonProcessedItemsSmallestUsns(qint32 & usn, QHash<QString,qint32> & usnByLinkedNotebookGuid)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::collectNonProcessedItemsSmallestUsns"));

    usn = -1;
    usnByLinkedNotebookGuid.clear();

    bool syncingLinkedNotebooks = syncingLinkedNotebooksContent();
    QNTRACE(QStringLiteral("Syncing linked notebooks = ") << (syncingLinkedNotebooks ? QStringLiteral("true") : QStringLiteral("false")));

    if (!syncingLinkedNotebooks)
    {
        if (!m_syncChunksDownloaded) {
            QNDEBUG(QStringLiteral("Not all sync chunks from user's own account were downloaded (if any) => there are no valid USNs to return"));
            return;
        }

        qint32 smallestUsn = nonProcessedItemsSmallestUsn();
        if (smallestUsn > 0) {
            QNDEBUG(QStringLiteral("Found the smallest USN of non-processed items within the user's own account: ")
                    << smallestUsn);
            usn = smallestUsn - 1;  // NOTE: decrement this USN because that would give the USN *after which* the next sync should start
        }

        return;
    }

    if (!m_allLinkedNotebooksListed) {
        QNDEBUG(QStringLiteral("Not all linked notebooks are listed"));
        return;
    }

    if (!m_linkedNotebooksSyncChunksDownloaded) {
        QNDEBUG(QStringLiteral("Not all sync chunks from linked notebooks were downloaded (if any) => there are no valid USNs to return"));
        return;
    }

    for(auto it = m_allLinkedNotebooks.constBegin(), end = m_allLinkedNotebooks.constEnd(); it != end; ++it)
    {
        const LinkedNotebook & linkedNotebook = *it;

        if (Q_UNLIKELY(!linkedNotebook.hasGuid())) {
            QNWARNING(QStringLiteral("Detected a linked notebook without guid: ") << linkedNotebook);
            continue;
        }

        qint32 smallestUsn = nonProcessedItemsSmallestUsn(linkedNotebook.guid());
        if (smallestUsn >= 0) {
            QNDEBUG(QStringLiteral("Found the smallest USN of non-processed items within linked notebook with guid ")
                    << linkedNotebook.guid() << QStringLiteral(": ") << smallestUsn);
            usnByLinkedNotebookGuid[linkedNotebook.guid()] = (smallestUsn - 1);
            continue;
        }
    }
}

void RemoteToLocalSynchronizationManager::onGetNoteAsyncFinished(qint32 errorCode, qevercloud::Note qecNote, qint32 rateLimitSeconds,
                                                                 ErrorString errorDescription)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onGetNoteAsyncFinished: error code = ")
            << errorCode << QStringLiteral(", rate limit seconds = ") << rateLimitSeconds
            << QStringLiteral(", error description: ") << errorDescription
            << QStringLiteral(", note: ") << qecNote);

    if (Q_UNLIKELY(!qecNote.guid.isSet())) {
        errorDescription.setBase(QT_TR_NOOP("Internal error: just downloaded note has no guid"));
        QNWARNING(errorDescription << QStringLiteral(", note: ") << qecNote);
        Q_EMIT failure(errorDescription);
        return;
    }

    QString noteGuid = qecNote.guid.ref();

    auto addIt = m_guidsOfNotesPendingDownloadForAddingToLocalStorage.find(noteGuid);
    auto updateIt = ((addIt == m_guidsOfNotesPendingDownloadForAddingToLocalStorage.end())
                     ? m_notesPendingDownloadForUpdatingInLocalStorageByGuid.find(noteGuid)
                     : m_notesPendingDownloadForUpdatingInLocalStorageByGuid.end());

    bool needToAddNote = (addIt != m_guidsOfNotesPendingDownloadForAddingToLocalStorage.end());
    bool needToUpdateNote = (updateIt != m_notesPendingDownloadForUpdatingInLocalStorageByGuid.end());

    Note note;

    if (needToAddNote) {
        Q_UNUSED(m_guidsOfNotesPendingDownloadForAddingToLocalStorage.erase(addIt))
    }
    else if (needToUpdateNote) {
        note = updateIt.value();
        Q_UNUSED(m_notesPendingDownloadForUpdatingInLocalStorageByGuid.erase(updateIt))
    }

    overrideLocalNoteWithRemoteNote(note, qecNote);

    if (Q_UNLIKELY(!needToAddNote && !needToUpdateNote)) {
        errorDescription.setBase(QT_TR_NOOP("Internal error: the downloaded note was not expected"));
        QNWARNING(errorDescription << QStringLiteral(", note: ") << note);
        Q_EMIT failure(errorDescription);
        return;
    }

    if (errorCode == qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED)
    {
        if (rateLimitSeconds <= 0) {
            errorDescription.setBase(QT_TR_NOOP("QEverCloud or Evernote protocol error: caught RATE_LIMIT_REACHED "
                                                "exception but the number of seconds to wait is zero or negative"));
            errorDescription.details() = QString::number(rateLimitSeconds);
            Q_EMIT failure(errorDescription);
            return;
        }

        int timerId = startTimer(SEC_TO_MSEC(rateLimitSeconds));
        if (Q_UNLIKELY(timerId == 0)) {
            errorDescription.setBase(QT_TR_NOOP("Failed to start a timer to postpone the Evernote API call "
                                                "due to rate limit exceeding"));
            Q_EMIT failure(errorDescription);
            return;
        }

        if (needToAddNote) {
            m_notesToAddPerAPICallPostponeTimerId[timerId] = note;
        }
        else {
            m_notesToUpdatePerAPICallPostponeTimerId[timerId] = note;
        }

        Q_EMIT rateLimitExceeded(rateLimitSeconds);
        return;
    }
    else if (errorCode == qevercloud::EDAMErrorCode::AUTH_EXPIRED)
    {
        handleAuthExpiration();
        return;
    }
    else if (errorCode != 0) {
        Q_EMIT failure(errorDescription);
        return;
    }

    // NOTE: thumbnails for notes are downloaded separately and their download is optional;
    // for the sake of better error tolerance the failure to download thumbnails for particular notes
    // should not be considered the failure of the synchronization algorithm as a whole.
    //
    // For these reasons, even if the thumbnail downloading was set up for some particular note, we don't wait for it to finish
    // before adding the note to local storage or updating the note in the local storage; if the thumbnail is downloaded
    // successfully, the note would be updated one more time; otherwise, it just won't be updated

    const Notebook * pNotebook = Q_NULLPTR;

    if (shouldDownloadThumbnailsForNotes() && note.hasResources())
    {
        QNDEBUG(QStringLiteral("The added or updated note contains resources, need to download the thumbnails for it"));

        pNotebook = getNotebookPerNote(note);
        if (pNotebook)
        {
            bool res = setupNoteThumbnailDownloading(note, *pNotebook);
            if (Q_UNLIKELY(!res)) {
                QNDEBUG(QStringLiteral("Wasn't able to set up the note thumbnail downloading"));
            }
        }
        else
        {
            bool res = findNotebookForNoteThumbnailDownloading(note);
            if (Q_UNLIKELY(!res)) {
                QNDEBUG(QStringLiteral("Wasn't able to set up the search for the notebook of the note for which "
                                       "the thumbnail was meant to be downloaded"));
            }
        }
    }

    // NOTE: ink note images are also downloaded separately per each corresponding note's resource
    // and furthermore, the ink note images are not a part of integral note data type. For these reasons
    // and for better error tolerance the failure to download the ink note image is not considered a failure
    // of the synchronization procedure

    if (shouldDownloadInkNoteImages() && note.hasResources() && note.isInkNote())
    {
        QNDEBUG(QStringLiteral("The added or updated note is the ink note, need to download the ink note image for it"));

        if (!pNotebook) {
            pNotebook = getNotebookPerNote(note);
        }

        if (pNotebook)
        {
            bool res = setupInkNoteImageDownloadingForNote(note, *pNotebook);
            if (Q_UNLIKELY(!res)) {
                QNDEBUG(QStringLiteral("Wasn't able to set up the ink note images downloading"));
            }
        }
        else
        {
            bool res = findNotebookForInkNoteImageDownloading(note);
            if (Q_UNLIKELY(!res)) {
                QNDEBUG(QStringLiteral("Wasn't able to set up the search for the notebook of the note for which "
                                       "the ink note images were meant to be downloaded"));
            }
        }
    }

    checkAndIncrementNoteDownloadProgress(noteGuid);

    if (needToAddNote) {
        emitAddRequest(note);
        return;
    }

    QUuid updateNoteRequestId = QUuid::createUuid();
    Q_UNUSED(m_updateNoteRequestIds.insert(updateNoteRequestId));
    QNTRACE(QStringLiteral("Emitting the request to update note in local storage: request id = ")
            << updateNoteRequestId << QStringLiteral(", note; ") << note);
    Q_EMIT updateNote(note, /* update resources = */ true, /* update tags = */ true, updateNoteRequestId);
}

void RemoteToLocalSynchronizationManager::onGetResourceAsyncFinished(qint32 errorCode, qevercloud::Resource qecResource,
                                                                     qint32 rateLimitSeconds, ErrorString errorDescription)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onGetResourceAsyncFinished: error code = ")
            << errorCode << QStringLiteral(", rate limit seconds = ") << rateLimitSeconds
            << QStringLiteral(", error description: ") << errorDescription
            << QStringLiteral(", resource: ") << qecResource);

    if (Q_UNLIKELY(!qecResource.guid.isSet())) {
        errorDescription.setBase(QT_TR_NOOP("Internal error: just downloaded resource has no guid"));
        QNWARNING(errorDescription << QStringLiteral(", resource: ") << qecResource);
        Q_EMIT failure(errorDescription);
        return;
    }

    QString resourceGuid = qecResource.guid.ref();

    auto addIt = m_notesOwningResourcesPendingDownloadForAddingToLocalStorageByResourceGuid.find(resourceGuid);
    auto updateIt = ((addIt == m_notesOwningResourcesPendingDownloadForAddingToLocalStorageByResourceGuid.end())
                     ? m_resourcesPendingDownloadForUpdatingInLocalStorageWithNotesByResourceGuid.find(resourceGuid)
                     : m_resourcesPendingDownloadForUpdatingInLocalStorageWithNotesByResourceGuid.end());

    bool needToAddResource = (addIt != m_notesOwningResourcesPendingDownloadForAddingToLocalStorageByResourceGuid.end());
    bool needToUpdateResource = (updateIt != m_resourcesPendingDownloadForUpdatingInLocalStorageWithNotesByResourceGuid.end());

    Resource resource;
    Note note;

    if (needToAddResource) {
        note = addIt.value();
        Q_UNUSED(m_notesOwningResourcesPendingDownloadForAddingToLocalStorageByResourceGuid.erase(addIt))
    }
    else if (needToUpdateResource) {
        resource = updateIt.value().first;
        note = updateIt.value().second;
        Q_UNUSED(m_resourcesPendingDownloadForUpdatingInLocalStorageWithNotesByResourceGuid.erase(updateIt))
    }

    resource.qevercloudResource() = qecResource;
    resource.setDirty(false);

    if (Q_UNLIKELY(!needToAddResource && !needToUpdateResource)) {
        errorDescription.setBase(QT_TR_NOOP("Internal error: the downloaded resource was not expected"));
        QNWARNING(errorDescription << QStringLiteral(", resource: ") << resource);
        Q_EMIT failure(errorDescription);
        return;
    }

    if (errorCode == qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED)
    {
        if (rateLimitSeconds <= 0) {
            errorDescription.setBase(QT_TR_NOOP("QEverCloud or Evernote protocol error: caught RATE_LIMIT_REACHED "
                                                "exception but the number of seconds to wait is zero or negative"));
            errorDescription.details() = QString::number(rateLimitSeconds);
            Q_EMIT failure(errorDescription);
            return;
        }

        int timerId = startTimer(SEC_TO_MSEC(rateLimitSeconds));
        if (Q_UNLIKELY(timerId == 0)) {
            errorDescription.setBase(QT_TR_NOOP("Failed to start a timer to postpone the Evernote API call "
                                                "due to rate limit exceeding"));
            Q_EMIT failure(errorDescription);
            return;
        }

        if (needToAddResource) {
            m_resourcesToAddWithNotesPerAPICallPostponeTimerId[timerId] = std::pair<Resource,Note>(resource, note);
        }
        else if (needToUpdateResource) {
            m_resourcesToUpdateWithNotesPerAPICallPostponeTimerId[timerId] = std::pair<Resource,Note>(resource, note);
        }

        Q_EMIT rateLimitExceeded(rateLimitSeconds);
        return;
    }
    else if (errorCode == qevercloud::EDAMErrorCode::AUTH_EXPIRED)
    {
        handleAuthExpiration();
        return;
    }
    else if (errorCode != 0) {
        Q_EMIT failure(errorDescription);
        return;
    }

    checkAndIncrementResourceDownloadProgress(resourceGuid);

    if (needToAddResource)
    {
        QString resourceGuid = (resource.hasGuid() ? resource.guid() : QString());
        QString resourceLocalUid = resource.localUid();
        QPair<QString,QString> key(resourceGuid, resourceLocalUid);

        QUuid addResourceRequestId = QUuid::createUuid();
        Q_UNUSED(m_addResourceRequestIds.insert(addResourceRequestId));
        QNTRACE(QStringLiteral("Emitting the request to add resource to local storage: request id = ")
                << addResourceRequestId << QStringLiteral(", resource: ") << resource);
        Q_EMIT addResource(resource, addResourceRequestId);
    }
    else
    {
        QUuid updateResourceRequestId = QUuid::createUuid();
        Q_UNUSED(m_updateResourceRequestIds.insert(updateResourceRequestId))
        QNTRACE(QStringLiteral("Emitting the request to update resource: request id = ") << updateResourceRequestId
                << QStringLiteral(", resource: ") << resource);
        Q_EMIT updateResource(resource, updateResourceRequestId);
    }

    note.setDirty(true);
    QUuid markNoteDirtyRequestId = QUuid::createUuid();
    m_resourcesByMarkNoteOwningResourceDirtyRequestIds[markNoteDirtyRequestId] = resource;
    QNTRACE(QStringLiteral("Emitting the request to mark the resource owning note as the dirty one: request id = ")
            << markNoteDirtyRequestId << QStringLiteral(", resource: ") << resource);
    Q_EMIT updateNote(note, /* update resources = */ false, /* update tags = */ false, markNoteDirtyRequestId);
}

void RemoteToLocalSynchronizationManager::onNotebookSyncConflictResolverFinished(qevercloud::Notebook remoteNotebook)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onNotebookSyncConflictResolverFinished: ") << remoteNotebook);

    NotebookSyncConflictResolver * pResolver = qobject_cast<NotebookSyncConflictResolver*>(sender());
    if (pResolver) {
        pResolver->setParent(Q_NULLPTR);
        pResolver->deleteLater();
    }

    unregisterNotebookPendingAddOrUpdate(Notebook(remoteNotebook));

    checkNotebooksAndTagsSyncCompletionAndLaunchNotesSync();
    checkServerDataMergeCompletion();
}

void RemoteToLocalSynchronizationManager::onNotebookSyncConflictResolverFailure(qevercloud::Notebook remoteNotebook, ErrorString errorDescription)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onNotebookSyncConflictResolverFailure: error description = ")
            << errorDescription << QStringLiteral(", remote notebook: ") << remoteNotebook);

    NotebookSyncConflictResolver * pResolver = qobject_cast<NotebookSyncConflictResolver*>(sender());
    if (pResolver) {
        pResolver->setParent(Q_NULLPTR);
        pResolver->deleteLater();
    }

    Q_EMIT failure(errorDescription);
}

void RemoteToLocalSynchronizationManager::onTagSyncConflictResolverFinished(qevercloud::Tag remoteTag)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onTagSyncConflictResolverFinished: ") << remoteTag);

    TagSyncConflictResolver * pResolver = qobject_cast<TagSyncConflictResolver*>(sender());
    if (pResolver) {
        pResolver->setParent(Q_NULLPTR);
        pResolver->deleteLater();
    }

    unregisterTagPendingAddOrUpdate(Tag(remoteTag));
    syncNextTagPendingProcessing();
    checkNotebooksAndTagsSyncCompletionAndLaunchNotesSync();
    checkServerDataMergeCompletion();
}

void RemoteToLocalSynchronizationManager::onTagSyncConflictResolverFailure(qevercloud::Tag remoteTag, ErrorString errorDescription)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onTagSyncConflictResolverFailure: error description = ")
            << errorDescription << QStringLiteral(", remote tag: ") << remoteTag);

    TagSyncConflictResolver * pResolver = qobject_cast<TagSyncConflictResolver*>(sender());
    if (pResolver) {
        pResolver->setParent(Q_NULLPTR);
        pResolver->deleteLater();
    }

    Q_EMIT failure(errorDescription);
}

void RemoteToLocalSynchronizationManager::onSavedSearchSyncConflictResolverFinished(qevercloud::SavedSearch remoteSavedSearch)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onSavedSearchSyncConflictResolverFinished: ") << remoteSavedSearch);

    SavedSearchSyncConflictResolver * pResolver = qobject_cast<SavedSearchSyncConflictResolver*>(sender());
    if (pResolver) {
        pResolver->setParent(Q_NULLPTR);
        pResolver->deleteLater();
    }

    unregisterSavedSearchPendingAddOrUpdate(SavedSearch(remoteSavedSearch));
    checkServerDataMergeCompletion();
}

void RemoteToLocalSynchronizationManager::onSavedSearchSyncConflictResolverFailure(qevercloud::SavedSearch remoteSavedSearch, ErrorString errorDescription)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onSavedSearchSyncConflictResolverFailure: error description = ")
            << errorDescription << QStringLiteral(", remote saved search: ") << remoteSavedSearch);

    SavedSearchSyncConflictResolver * pResolver = qobject_cast<SavedSearchSyncConflictResolver*>(sender());
    if (pResolver) {
        pResolver->setParent(Q_NULLPTR);
        pResolver->deleteLater();
    }

    Q_EMIT failure(errorDescription);
}

void RemoteToLocalSynchronizationManager::onFullSyncStaleDataItemsExpungerFinished()
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onFullSyncStaleDataItemsExpungerFinished"));

    QString linkedNotebookGuid;

    FullSyncStaleDataItemsExpunger * pExpunger = qobject_cast<FullSyncStaleDataItemsExpunger*>(sender());
    if (pExpunger)
    {
        linkedNotebookGuid = pExpunger->linkedNotebookGuid();

        if (m_pFullSyncStaleDataItemsExpunger == pExpunger)
        {
            m_pFullSyncStaleDataItemsExpunger = Q_NULLPTR;
        }
        else
        {
            auto it = m_fullSyncStaleDataItemsExpungersByLinkedNotebookGuid.find(linkedNotebookGuid);
            if (it != m_fullSyncStaleDataItemsExpungersByLinkedNotebookGuid.end()) {
                Q_UNUSED(m_fullSyncStaleDataItemsExpungersByLinkedNotebookGuid.erase(it))
            }
        }

        junkFullSyncStaleDataItemsExpunger(*pExpunger);
    }

    if (linkedNotebookGuid.isEmpty())
    {
        QNDEBUG(QStringLiteral("Finished analyzing and expunging stuff from user's own account after the non-first full sync"));

        m_expungedFromServerToClient = true;
        startLinkedNotebooksSync();
    }
    else
    {
        if (!m_fullSyncStaleDataItemsExpungersByLinkedNotebookGuid.isEmpty()) {
            QNDEBUG(QStringLiteral("Still pending the finish of ") << m_fullSyncStaleDataItemsExpungersByLinkedNotebookGuid.size()
                    << QStringLiteral(" FullSyncStaleDataItemsExpungers for linked notebooks"));
            return;
        }

        QNDEBUG(QStringLiteral("All FullSyncStaleDataItemsExpungers for linked notebooks finished"));
        launchExpungingOfNotelessTagsFromLinkedNotebooks();
    }
}

void RemoteToLocalSynchronizationManager::onFullSyncStaleDataItemsExpungerFailure(ErrorString errorDescription)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onFullSyncStaleDataItemsExpungerFailure: ") << errorDescription);

    QString linkedNotebookGuid;

    FullSyncStaleDataItemsExpunger * pExpunger = qobject_cast<FullSyncStaleDataItemsExpunger*>(sender());
    if (pExpunger)
    {
        linkedNotebookGuid = pExpunger->linkedNotebookGuid();

        if (m_pFullSyncStaleDataItemsExpunger == pExpunger)
        {
            m_pFullSyncStaleDataItemsExpunger = Q_NULLPTR;
        }
        else
        {
            auto it = m_fullSyncStaleDataItemsExpungersByLinkedNotebookGuid.find(linkedNotebookGuid);
            if (it != m_fullSyncStaleDataItemsExpungersByLinkedNotebookGuid.end()) {
                Q_UNUSED(m_fullSyncStaleDataItemsExpungersByLinkedNotebookGuid.erase(it))
            }
        }

        junkFullSyncStaleDataItemsExpunger(*pExpunger);
    }

    QNWARNING(QStringLiteral("Failed to analyze and expunge stale stuff after the non-first full sync: ")
              << errorDescription << QStringLiteral("; linked notebook guid = ") << linkedNotebookGuid);
    Q_EMIT failure(errorDescription);
}

void RemoteToLocalSynchronizationManager::connectToLocalStorage()
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::connectToLocalStorage"));

    if (m_connectedToLocalStorage) {
        QNDEBUG(QStringLiteral("Already connected to the local storage"));
        return;
    }

    LocalStorageManagerAsync & localStorageManagerAsync = m_manager.localStorageManagerAsync();

    // Connect local signals with localStorageManagerAsync's slots
    QObject::connect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,addUser,User,QUuid),
                     &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onAddUserRequest,User,QUuid));
    QObject::connect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,updateUser,User,QUuid),
                     &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onUpdateUserRequest,User,QUuid));
    QObject::connect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,findUser,User,QUuid),
                     &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onFindUserRequest,User,QUuid));

    QObject::connect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,addNotebook,Notebook,QUuid),
                     &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onAddNotebookRequest,Notebook,QUuid));
    QObject::connect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,updateNotebook,Notebook,QUuid),
                     &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onUpdateNotebookRequest,Notebook,QUuid));
    QObject::connect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,findNotebook,Notebook,QUuid),
                     &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onFindNotebookRequest,Notebook,QUuid));
    QObject::connect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,expungeNotebook,Notebook,QUuid),
                     &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onExpungeNotebookRequest,Notebook,QUuid));

    QObject::connect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,addNote,Note,QUuid),
                     &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onAddNoteRequest,Note,QUuid));
    QObject::connect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,updateNote,Note,bool,bool,QUuid),
                     &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onUpdateNoteRequest,Note,bool,bool,QUuid));
    QObject::connect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,findNote,Note,bool,QUuid),
                     &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onFindNoteRequest,Note,bool,QUuid));
    QObject::connect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,expungeNote,Note,QUuid),
                     &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onExpungeNoteRequest,Note,QUuid));

    QObject::connect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,addTag,Tag,QUuid),
                     &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onAddTagRequest,Tag,QUuid));
    QObject::connect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,updateTag,Tag,QUuid),
                     &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onUpdateTagRequest,Tag,QUuid));
    QObject::connect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,findTag,Tag,QUuid),
                     &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onFindTagRequest,Tag,QUuid));
    QObject::connect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,expungeTag,Tag,QUuid),
                     &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onExpungeTagRequest,Tag,QUuid));

    QObject::connect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,expungeNotelessTagsFromLinkedNotebooks,QUuid),
                     &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onExpungeNotelessTagsFromLinkedNotebooksRequest,QUuid));

    QObject::connect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,addResource,Resource,QUuid),
                     &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onAddResourceRequest,Resource,QUuid));
    QObject::connect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,updateResource,Resource,QUuid),
                     &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onUpdateResourceRequest,Resource,QUuid));
    QObject::connect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,findResource,Resource,bool,QUuid),
                     &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onFindResourceRequest,Resource,bool,QUuid));

    QObject::connect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,addLinkedNotebook,LinkedNotebook,QUuid),
                     &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onAddLinkedNotebookRequest,LinkedNotebook,QUuid));
    QObject::connect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,updateLinkedNotebook,LinkedNotebook,QUuid),
                     &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onUpdateLinkedNotebookRequest,LinkedNotebook,QUuid));
    QObject::connect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,findLinkedNotebook,LinkedNotebook,QUuid),
                     &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onFindLinkedNotebookRequest,LinkedNotebook,QUuid));
    QObject::connect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,expungeLinkedNotebook,LinkedNotebook,QUuid),
                     &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onExpungeLinkedNotebookRequest,LinkedNotebook,QUuid));

    QObject::connect(this,
                     QNSIGNAL(RemoteToLocalSynchronizationManager,listAllLinkedNotebooks,size_t,size_t,
                              LocalStorageManager::ListLinkedNotebooksOrder::type,LocalStorageManager::OrderDirection::type,QUuid),
                     &localStorageManagerAsync,
                     QNSLOT(LocalStorageManagerAsync,onListAllLinkedNotebooksRequest,size_t,size_t,
                            LocalStorageManager::ListLinkedNotebooksOrder::type,LocalStorageManager::OrderDirection::type,QUuid));

    QObject::connect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,addSavedSearch,SavedSearch,QUuid),
                     &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onAddSavedSearchRequest,SavedSearch,QUuid));
    QObject::connect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,updateSavedSearch,SavedSearch,QUuid),
                     &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onUpdateSavedSearchRequest,SavedSearch,QUuid));
    QObject::connect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,findSavedSearch,SavedSearch,QUuid),
                     &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onFindSavedSearchRequest,SavedSearch,QUuid));
    QObject::connect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,expungeSavedSearch,SavedSearch,QUuid),
                     &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onExpungeSavedSearchRequest,SavedSearch,QUuid));

    // Connect localStorageManagerAsync's signals to local slots
    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findUserComplete,User,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onFindUserCompleted,User,QUuid));
    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findUserFailed,User,ErrorString,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onFindUserFailed,User,ErrorString,QUuid));

    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findNotebookComplete,Notebook,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onFindNotebookCompleted,Notebook,QUuid));
    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findNotebookFailed,Notebook,ErrorString,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onFindNotebookFailed,Notebook,ErrorString,QUuid));

    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findNoteComplete,Note,bool,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onFindNoteCompleted,Note,bool,QUuid));
    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findNoteFailed,Note,bool,ErrorString,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onFindNoteFailed,Note,bool,ErrorString,QUuid));

    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findTagComplete,Tag,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onFindTagCompleted,Tag,QUuid));
    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findTagFailed,Tag,ErrorString,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onFindTagFailed,Tag,ErrorString,QUuid));

    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findLinkedNotebookComplete,LinkedNotebook,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onFindLinkedNotebookCompleted,LinkedNotebook,QUuid));
    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findLinkedNotebookFailed,LinkedNotebook,ErrorString,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onFindLinkedNotebookFailed,LinkedNotebook,ErrorString,QUuid));

    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findSavedSearchComplete,SavedSearch,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onFindSavedSearchCompleted,SavedSearch,QUuid));
    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findSavedSearchFailed,SavedSearch,ErrorString,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onFindSavedSearchFailed,SavedSearch,ErrorString,QUuid));

    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findResourceComplete,Resource,bool,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onFindResourceCompleted,Resource,bool,QUuid));
    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findResourceFailed,Resource,bool,ErrorString,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onFindResourceFailed,Resource,bool,ErrorString,QUuid));

    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addTagComplete,Tag,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onAddTagCompleted,Tag,QUuid));
    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addTagFailed,Tag,ErrorString,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onAddTagFailed,Tag,ErrorString,QUuid));

    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateTagComplete,Tag,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onUpdateTagCompleted,Tag,QUuid));
    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateTagFailed,Tag,ErrorString,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onUpdateTagFailed,Tag,ErrorString,QUuid));

    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeTagComplete,Tag,QStringList,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onExpungeTagCompleted,Tag,QStringList,QUuid));
    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeTagFailed,Tag,ErrorString,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onExpungeTagFailed,Tag,ErrorString,QUuid));

    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeNotelessTagsFromLinkedNotebooksComplete,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onExpungeNotelessTagsFromLinkedNotebooksCompleted,QUuid));
    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeNotelessTagsFromLinkedNotebooksFailed,ErrorString,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onExpungeNotelessTagsFromLinkedNotebooksFailed,ErrorString,QUuid));

    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addUserComplete,User,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onAddUserCompleted,User,QUuid));
    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addUserFailed,User,ErrorString,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onAddUserFailed,User,ErrorString,QUuid));

    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateUserComplete,User,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onUpdateUserCompleted,User,QUuid));
    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateUserFailed,User,ErrorString,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onUpdateUserFailed,User,ErrorString,QUuid));

    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addSavedSearchComplete,SavedSearch,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onAddSavedSearchCompleted,SavedSearch,QUuid));
    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addSavedSearchFailed,SavedSearch,ErrorString,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onAddSavedSearchFailed,SavedSearch,ErrorString,QUuid));

    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateSavedSearchComplete,SavedSearch,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onUpdateSavedSearchCompleted,SavedSearch,QUuid));
    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateSavedSearchFailed,SavedSearch,ErrorString,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onUpdateSavedSearchFailed,SavedSearch,ErrorString,QUuid));

    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeSavedSearchComplete,SavedSearch,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onExpungeSavedSearchCompleted,SavedSearch,QUuid));
    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeSavedSearchFailed,SavedSearch,ErrorString,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onExpungeSavedSearchFailed,SavedSearch,ErrorString,QUuid));

    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addLinkedNotebookComplete,LinkedNotebook,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onAddLinkedNotebookCompleted,LinkedNotebook,QUuid));
    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addLinkedNotebookFailed,LinkedNotebook,ErrorString,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onAddLinkedNotebookFailed,LinkedNotebook,ErrorString,QUuid));

    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateLinkedNotebookComplete,LinkedNotebook,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onUpdateLinkedNotebookCompleted,LinkedNotebook,QUuid));
    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateLinkedNotebookFailed,LinkedNotebook,ErrorString,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onUpdateLinkedNotebookFailed,LinkedNotebook,ErrorString,QUuid));

    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeLinkedNotebookComplete,LinkedNotebook,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onExpungeLinkedNotebookCompleted,LinkedNotebook,QUuid));
    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeLinkedNotebookFailed,LinkedNotebook,ErrorString,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onExpungeLinkedNotebookFailed,LinkedNotebook,ErrorString,QUuid));

    QObject::connect(&localStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,listAllLinkedNotebooksComplete,size_t,size_t,
                              LocalStorageManager::ListLinkedNotebooksOrder::type,LocalStorageManager::OrderDirection::type,QList<LinkedNotebook>,QUuid),
                     this,
                     QNSLOT(RemoteToLocalSynchronizationManager,onListAllLinkedNotebooksCompleted,size_t,size_t,
                            LocalStorageManager::ListLinkedNotebooksOrder::type,LocalStorageManager::OrderDirection::type,QList<LinkedNotebook>,QUuid));
    QObject::connect(&localStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,listAllLinkedNotebooksFailed,size_t,size_t,
                              LocalStorageManager::ListLinkedNotebooksOrder::type,LocalStorageManager::OrderDirection::type,ErrorString,QUuid),
                     this,
                     QNSLOT(RemoteToLocalSynchronizationManager,onListAllLinkedNotebooksFailed,size_t,size_t,
                            LocalStorageManager::ListLinkedNotebooksOrder::type,LocalStorageManager::OrderDirection::type,ErrorString,QUuid));

    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addNotebookComplete,Notebook,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onAddNotebookCompleted,Notebook,QUuid));
    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addNotebookFailed,Notebook,ErrorString,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onAddNotebookFailed,Notebook,ErrorString,QUuid));

    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateNotebookComplete,Notebook,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onUpdateNotebookCompleted,Notebook,QUuid));
    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateNotebookFailed,Notebook,ErrorString,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onUpdateNotebookFailed,Notebook,ErrorString,QUuid));

    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeNotebookComplete,Notebook,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onExpungeNotebookCompleted,Notebook,QUuid));
    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeNotebookFailed,Notebook,ErrorString,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onExpungeNotebookFailed,Notebook,ErrorString,QUuid));

    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addNoteComplete,Note,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onAddNoteCompleted,Note,QUuid));
    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addNoteFailed,Note,ErrorString,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onAddNoteFailed,Note,ErrorString,QUuid));

    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateNoteComplete,Note,bool,bool,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onUpdateNoteCompleted,Note,bool,bool,QUuid));
    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateNoteFailed,Note,bool,bool,ErrorString,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onUpdateNoteFailed,Note,bool,bool,ErrorString,QUuid));

    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeNoteComplete,Note,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onExpungeNoteCompleted,Note,QUuid));
    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeNoteFailed,Note,ErrorString,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onExpungeNoteFailed,Note,ErrorString,QUuid));

    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addResourceComplete,Resource,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onAddResourceCompleted,Resource,QUuid));
    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addResourceFailed,Resource,ErrorString,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onAddResourceFailed,Resource,ErrorString,QUuid));

    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateResourceComplete,Resource,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onUpdateResourceCompleted,Resource,QUuid));
    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateResourceFailed,Resource,ErrorString,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onUpdateResourceFailed,Resource,ErrorString,QUuid));

    m_connectedToLocalStorage = true;
}

void RemoteToLocalSynchronizationManager::disconnectFromLocalStorage()
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::disconnectFromLocalStorage"));

    if (!m_connectedToLocalStorage) {
        QNDEBUG(QStringLiteral("Not connected to local storage at the moment"));
        return;
    }

    LocalStorageManagerAsync & localStorageManagerAsync = m_manager.localStorageManagerAsync();

    // Disconnect local signals from localStorageManagerThread's slots
    QObject::disconnect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,addUser,User,QUuid),
                        &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onAddUserRequest,User,QUuid));
    QObject::disconnect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,updateUser,User,QUuid),
                        &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onUpdateUserRequest,User,QUuid));
    QObject::disconnect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,findUser,User,QUuid),
                        &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onFindUserRequest,User,QUuid));

    QObject::disconnect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,addNotebook,Notebook,QUuid),
                        &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onAddNotebookRequest,Notebook,QUuid));
    QObject::disconnect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,updateNotebook,Notebook,QUuid),
                        &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onUpdateNotebookRequest,Notebook,QUuid));
    QObject::disconnect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,findNotebook,Notebook,QUuid),
                        &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onFindNotebookRequest,Notebook,QUuid));
    QObject::disconnect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,expungeNotebook,Notebook,QUuid),
                        &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onExpungeNotebookRequest,Notebook,QUuid));

    QObject::disconnect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,addNote,Note,QUuid),
                        &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onAddNoteRequest,Note,QUuid));
    QObject::disconnect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,updateNote,Note,bool,bool,QUuid),
                        &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onUpdateNoteRequest,Note,bool,bool,QUuid));
    QObject::disconnect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,findNote,Note,bool,QUuid),
                        &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onFindNoteRequest,Note,bool,QUuid));
    QObject::disconnect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,expungeNote,Note,QUuid),
                        &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onExpungeNoteRequest,Note,QUuid));

    QObject::disconnect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,addTag,Tag,QUuid),
                        &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onAddTagRequest,Tag,QUuid));
    QObject::disconnect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,updateTag,Tag,QUuid),
                        &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onUpdateTagRequest,Tag,QUuid));
    QObject::disconnect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,findTag,Tag,QUuid),
                        &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onFindTagRequest,Tag,QUuid));
    QObject::disconnect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,expungeTag,Tag,QUuid),
                        &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onExpungeTagRequest,Tag,QUuid));
    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeNotelessTagsFromLinkedNotebooksComplete,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onExpungeNotelessTagsFromLinkedNotebooksCompleted,QUuid));
    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeNotelessTagsFromLinkedNotebooksFailed,ErrorString,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onExpungeNotelessTagsFromLinkedNotebooksFailed,ErrorString,QUuid));

    QObject::disconnect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,addResource,Resource,QUuid),
                        &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onAddResourceRequest,Resource,QUuid));
    QObject::disconnect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,updateResource,Resource,QUuid),
                        &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onUpdateResourceRequest,Resource,QUuid));
    QObject::disconnect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,findResource,Resource,bool,QUuid),
                        &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onFindResourceRequest,Resource,bool,QUuid));

    QObject::disconnect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,addLinkedNotebook,LinkedNotebook,QUuid),
                        &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onAddLinkedNotebookRequest,LinkedNotebook,QUuid));
    QObject::disconnect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,updateLinkedNotebook,LinkedNotebook,QUuid),
                        &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onUpdateLinkedNotebookRequest,LinkedNotebook,QUuid));
    QObject::disconnect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,findLinkedNotebook,LinkedNotebook,QUuid),
                        &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onFindLinkedNotebookRequest,LinkedNotebook,QUuid));
    QObject::disconnect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,expungeLinkedNotebook,LinkedNotebook,QUuid),
                        &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onExpungeLinkedNotebookRequest,LinkedNotebook,QUuid));

    QObject::disconnect(this,
                        QNSIGNAL(RemoteToLocalSynchronizationManager,listAllLinkedNotebooks,size_t,size_t,
                                 LocalStorageManager::ListLinkedNotebooksOrder::type,LocalStorageManager::OrderDirection::type,QUuid),
                        &localStorageManagerAsync,
                        QNSLOT(LocalStorageManagerAsync,onListAllLinkedNotebooksRequest,size_t,size_t,
                               LocalStorageManager::ListLinkedNotebooksOrder::type,LocalStorageManager::OrderDirection::type,QUuid));

    QObject::disconnect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,addSavedSearch,SavedSearch,QUuid),
                        &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onAddSavedSearchRequest,SavedSearch,QUuid));
    QObject::disconnect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,updateSavedSearch,SavedSearch,QUuid),
                        &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onUpdateSavedSearchRequest,SavedSearch,QUuid));
    QObject::disconnect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,findSavedSearch,SavedSearch,QUuid),
                        &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onFindSavedSearchRequest,SavedSearch,QUuid));
    QObject::disconnect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,expungeSavedSearch,SavedSearch,QUuid),
                        &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onExpungeSavedSearchRequest,SavedSearch,QUuid));

    // Disconnect localStorageManagerThread's signals to local slots
    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findUserComplete,User,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onFindUserCompleted,User,QUuid));
    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findUserFailed,User,ErrorString,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onFindUserFailed,User,ErrorString,QUuid));
    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findNotebookComplete,Notebook,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onFindNotebookCompleted,Notebook,QUuid));
    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findNotebookFailed,Notebook,ErrorString,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onFindNotebookFailed,Notebook,ErrorString,QUuid));

    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findNoteComplete,Note,bool,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onFindNoteCompleted,Note,bool,QUuid));
    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findNoteFailed,Note,bool,ErrorString,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onFindNoteFailed,Note,bool,ErrorString,QUuid));

    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findTagComplete,Tag,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onFindTagCompleted,Tag,QUuid));
    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findTagFailed,Tag,ErrorString,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onFindTagFailed,Tag,ErrorString,QUuid));

    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findLinkedNotebookComplete,LinkedNotebook,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onFindLinkedNotebookCompleted,LinkedNotebook,QUuid));
    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findLinkedNotebookFailed,LinkedNotebook,ErrorString,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onFindLinkedNotebookFailed,LinkedNotebook,ErrorString,QUuid));

    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findSavedSearchComplete,SavedSearch,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onFindSavedSearchCompleted,SavedSearch,QUuid));
    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findSavedSearchFailed,SavedSearch,ErrorString,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onFindSavedSearchFailed,SavedSearch,ErrorString,QUuid));

    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findResourceComplete,Resource,bool,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onFindResourceCompleted,Resource,bool,QUuid));
    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findResourceFailed,Resource,bool,ErrorString,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onFindResourceFailed,Resource,bool,ErrorString,QUuid));

    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addTagComplete,Tag,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onAddTagCompleted,Tag,QUuid));
    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addTagFailed,Tag,ErrorString,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onAddTagFailed,Tag,ErrorString,QUuid));

    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateTagComplete,Tag,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onUpdateTagCompleted,Tag,QUuid));
    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateTagFailed,Tag,ErrorString,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onUpdateTagFailed,Tag,ErrorString,QUuid));

    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeTagComplete,Tag,QStringList,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onExpungeTagCompleted,Tag,QStringList,QUuid));
    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeTagFailed,Tag,ErrorString,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onExpungeTagFailed,Tag,ErrorString,QUuid));

    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addSavedSearchComplete,SavedSearch,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onAddSavedSearchCompleted,SavedSearch,QUuid));
    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addSavedSearchFailed,SavedSearch,ErrorString,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onAddSavedSearchFailed,SavedSearch,ErrorString,QUuid));

    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateSavedSearchComplete,SavedSearch,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onUpdateSavedSearchCompleted,SavedSearch,QUuid));
    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateSavedSearchFailed,SavedSearch,ErrorString,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onUpdateSavedSearchFailed,SavedSearch,ErrorString,QUuid));

    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeSavedSearchComplete,SavedSearch,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onExpungeSavedSearchCompleted,SavedSearch,QUuid));
    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeSavedSearchFailed,SavedSearch,ErrorString,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onExpungeSavedSearchFailed,SavedSearch,ErrorString,QUuid));
    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addUserComplete,User,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onAddUserCompleted,User,QUuid));
    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addUserFailed,User,ErrorString,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onAddUserFailed,User,ErrorString,QUuid));
    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateUserComplete,User,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onUpdateUserCompleted,User,QUuid));
    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateUserFailed,User,ErrorString,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onUpdateUserFailed,User,ErrorString,QUuid));
    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addLinkedNotebookComplete,LinkedNotebook,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onAddLinkedNotebookCompleted,LinkedNotebook,QUuid));
    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addLinkedNotebookFailed,LinkedNotebook,ErrorString,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onAddLinkedNotebookFailed,LinkedNotebook,ErrorString,QUuid));

    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateLinkedNotebookComplete,LinkedNotebook,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onUpdateLinkedNotebookCompleted,LinkedNotebook,QUuid));
    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateLinkedNotebookFailed,LinkedNotebook,ErrorString,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onUpdateLinkedNotebookFailed,LinkedNotebook,ErrorString,QUuid));

    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeLinkedNotebookComplete,LinkedNotebook,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onExpungeLinkedNotebookCompleted,LinkedNotebook,QUuid));
    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeLinkedNotebookFailed,LinkedNotebook,ErrorString,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onExpungeLinkedNotebookFailed,LinkedNotebook,ErrorString,QUuid));

    QObject::disconnect(&localStorageManagerAsync,
                        QNSIGNAL(LocalStorageManagerAsync,listAllLinkedNotebooksComplete,size_t,size_t,
                                 LocalStorageManager::ListLinkedNotebooksOrder::type,LocalStorageManager::OrderDirection::type,QList<LinkedNotebook>,QUuid),
                        this,
                        QNSLOT(RemoteToLocalSynchronizationManager,onListAllLinkedNotebooksCompleted,size_t,size_t,
                               LocalStorageManager::ListLinkedNotebooksOrder::type,LocalStorageManager::OrderDirection::type,QList<LinkedNotebook>,QUuid));
    QObject::disconnect(&localStorageManagerAsync,
                        QNSIGNAL(LocalStorageManagerAsync,listAllLinkedNotebooksFailed,size_t,size_t,
                                 LocalStorageManager::ListLinkedNotebooksOrder::type,LocalStorageManager::OrderDirection::type,ErrorString,QUuid),
                        this,
                        QNSLOT(RemoteToLocalSynchronizationManager,onListAllLinkedNotebooksFailed,size_t,size_t,
                               LocalStorageManager::ListLinkedNotebooksOrder::type,LocalStorageManager::OrderDirection::type,ErrorString,QUuid));

    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addNotebookComplete,Notebook,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onAddNotebookCompleted,Notebook,QUuid));
    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addNotebookFailed,Notebook,ErrorString,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onAddNotebookFailed,Notebook,ErrorString,QUuid));

    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateNotebookComplete,Notebook,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onUpdateNotebookCompleted,Notebook,QUuid));
    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateNotebookFailed,Notebook,ErrorString,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onUpdateNotebookFailed,Notebook,ErrorString,QUuid));

    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeNotebookComplete,Notebook,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onExpungeNotebookCompleted,Notebook,QUuid));
    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeNotebookFailed,Notebook,ErrorString,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onExpungeNotebookFailed,Notebook,ErrorString,QUuid));

    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateNoteComplete,Note,bool,bool,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onUpdateNoteCompleted,Note,bool,bool,QUuid));
    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateNoteFailed,Note,bool,bool,ErrorString,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onUpdateNoteFailed,Note,bool,bool,ErrorString,QUuid));

    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addNoteComplete,Note,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onAddNoteCompleted,Note,QUuid));
    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addNoteFailed,Note,ErrorString,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onAddNoteFailed,Note,ErrorString,QUuid));

    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeNoteComplete,Note,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onExpungeNoteCompleted,Note,QUuid));
    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeNoteFailed,Note,ErrorString,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onExpungeNoteFailed,Note,ErrorString,QUuid));

    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addResourceComplete,Resource,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onAddResourceCompleted,Resource,QUuid));
    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addResourceFailed,Resource,ErrorString,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onAddResourceFailed,Resource,ErrorString,QUuid));

    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateResourceComplete,Resource,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onUpdateResourceCompleted,Resource,QUuid));
    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateResourceFailed,Resource,ErrorString,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onUpdateResourceFailed,Resource,ErrorString,QUuid));

    m_connectedToLocalStorage = false;

    // With the disconnect from local storage the list of previously received linked notebooks (if any) + new additions/updates becomes invalidated
    m_allLinkedNotebooksListed = false;
}

void RemoteToLocalSynchronizationManager::resetCurrentSyncState()
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::resetCurrentSyncState"));

    m_lastUpdateCount = 0;
    m_lastSyncTime = 0;
    m_lastUpdateCountByLinkedNotebookGuid.clear();
    m_lastSyncTimeByLinkedNotebookGuid.clear();
    m_linkedNotebookGuidsForWhichFullSyncWasPerformed.clear();
    m_linkedNotebookGuidsOnceFullySynced.clear();

    m_gotLastSyncParameters = false;
}

QString RemoteToLocalSynchronizationManager::defaultInkNoteImageStoragePath() const
{
    return applicationPersistentStoragePath() + QStringLiteral("/inkNoteImages");
}

void RemoteToLocalSynchronizationManager::launchSync()
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::launchSync"));

    if (m_authenticationToken.isEmpty()) {
        m_pendingAuthenticationTokenAndShardId = true;
        Q_EMIT requestAuthenticationToken();
        return;
    }

    if (m_onceSyncDone && (m_lastSyncMode == SyncMode::FullSync)) {
        QNDEBUG(QStringLiteral("Performing full sync even though it has been performed at some moment in the past; "
                               "collecting synced guids for full sync stale data items expunger"));
        collectSyncedGuidsForFullSyncStaleDataItemsExpunger();
    }

    m_pendingTagsSyncStart = true;
    m_pendingLinkedNotebooksSyncStart = true;
    m_pendingNotebooksSyncStart = true;

    launchSavedSearchSync();
    launchLinkedNotebookSync();

    launchTagsSync();
    launchNotebookSync();

    if (!m_tags.isEmpty() || !m_notebooks.isEmpty()) {
        // NOTE: the sync of notes and, if need be, individual resouces would be launched asynchronously when the
        // notebooks and tags are synced
        return;
    }

    QNDEBUG(QStringLiteral("The local lists of tags and notebooks waiting for adding/updating are empty, "
                           "checking if there are notes to process"));

    launchNotesSync(ContentSource::UserAccount);
    if (!m_notes.isEmpty() || notesSyncInProgress()) {
        QNDEBUG(QStringLiteral("Synchronizing notes"));
        // NOTE: the sync of individual resources as well as expunging of various data items will be launched asynchronously
        // if current sync is incremental after the notes are synced
        return;
    }

    QNDEBUG(QStringLiteral("The local list of notes waiting for adding/updating is empty"));

    if (m_lastSyncMode != SyncMode::IncrementalSync) {
        QNDEBUG(QStringLiteral("Running full sync => no sync for individual resources or expunging stuff is needed"));
        return;
    }

    if (!resourcesSyncInProgress())
    {
        launchResourcesSync();

        if (!m_resources.isEmpty() || resourcesSyncInProgress()) {
            QNDEBUG(QStringLiteral("Resources sync in progress"));
            return;
        }
    }

    // If we got here and there's something to expunge, do it
    expungeFromServerToClient();

    // Just in case need to check if the sync chunk we received contained no actual data elements and hence there's nothing to sync
    checkServerDataMergeCompletion();
}

bool RemoteToLocalSynchronizationManager::checkProtocolVersion(ErrorString & errorDescription)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::checkProtocolVersion"));

    if (m_edamProtocolVersionChecked) {
        QNDEBUG(QStringLiteral("Already checked the protocol version, skipping it"));
        return true;
    }

    QString clientName = clientNameForProtocolVersionCheck();
    qint16 edamProtocolVersionMajor = qevercloud::EDAM_VERSION_MAJOR;
    qint16 edamProtocolVersionMinor = qevercloud::EDAM_VERSION_MINOR;
    bool protocolVersionChecked = m_manager.userStore().checkVersion(clientName, edamProtocolVersionMajor,
                                                                     edamProtocolVersionMinor, errorDescription);
    if (Q_UNLIKELY(!protocolVersionChecked))
    {
        if (!errorDescription.isEmpty())
        {
            ErrorString fullErrorDescription(QT_TR_NOOP("EDAM protocol version check failed"));
            fullErrorDescription.additionalBases().append(errorDescription.base());
            fullErrorDescription.additionalBases().append(errorDescription.additionalBases());
            fullErrorDescription.details() = errorDescription.details();
            errorDescription = fullErrorDescription;
        }
        else
        {
            errorDescription.setBase(QT_TR_NOOP("Evernote service reports the currently used protocol version "
                                                "can no longer be used for the communication with it"));
            errorDescription.details() = QString::number(edamProtocolVersionMajor);
            errorDescription.details() += QStringLiteral(".");
            errorDescription.details() += QString::number(edamProtocolVersionMinor);
        }

        QNWARNING(errorDescription);
        return false;
    }

    m_edamProtocolVersionChecked = true;
    QNDEBUG(QStringLiteral("Successfully checked the protocol version"));
    return true;
}

bool RemoteToLocalSynchronizationManager::syncUserImpl(const bool waitIfRateLimitReached, ErrorString & errorDescription,
                                                       const bool writeUserDataToLocalStorage)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::syncUserImpl: wait if rate limit reached = ")
            << (waitIfRateLimitReached ? QStringLiteral("true") : QStringLiteral("false"))
            << QStringLiteral(", write user data to local storage = ")
            << (writeUserDataToLocalStorage ? QStringLiteral("true") : QStringLiteral("false")));

    if (m_user.hasId() && m_user.hasServiceLevel()) {
        QNDEBUG(QStringLiteral("User id and service level are set, that means the user info has already been synchronized once "
                               "during the current session, won't do it again"));
        return true;
    }

    qint32 rateLimitSeconds = 0;
    qint32 errorCode = m_manager.userStore().getUser(m_user, errorDescription, rateLimitSeconds);
    if (errorCode == qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED)
    {
        if (rateLimitSeconds <= 0) {
            errorDescription.setBase(QT_TR_NOOP("Rate limit reached but the number of seconds to wait is incorrect"));
            errorDescription.details() = QString::number(rateLimitSeconds);
            QNWARNING(errorDescription);
            return false;
        }

        QNDEBUG(QStringLiteral("Rate limit exceeded, need to wait for ") << rateLimitSeconds << QStringLiteral(" seconds"));
        if (waitIfRateLimitReached)
        {
            int timerId = startTimer(SEC_TO_MSEC(rateLimitSeconds));
            if (Q_UNLIKELY(timerId == 0)) {
                ErrorString errorMessage(QT_TR_NOOP("Failed to start a timer to postpone the Evernote API call "
                                                    "due to rate limit exceeding"));
                errorMessage.additionalBases().append(errorDescription.base());
                errorMessage.additionalBases().append(errorDescription.additionalBases());
                errorMessage.details() = errorDescription.details();
                errorDescription = errorMessage;
                QNDEBUG(errorDescription);
                return false;
            }

            m_syncUserPostponeTimerId = timerId;
            Q_EMIT rateLimitExceeded(rateLimitSeconds);
        }

        return false;
    }
    else if (errorCode == qevercloud::EDAMErrorCode::AUTH_EXPIRED)
    {
        ErrorString errorMessage(QT_TR_NOOP("unexpected AUTH_EXPIRED error when trying to download "
                                            "the latest information about the current user"));
        errorMessage.additionalBases().append(errorDescription.base());
        errorMessage.additionalBases().append(errorDescription.additionalBases());
        errorMessage.details() = errorDescription.details();
        errorDescription = errorMessage;
        QNINFO(errorDescription);
        return false;
    }
    else if (errorCode != 0)
    {
        ErrorString errorMessage(QT_TR_NOOP("Failed to download the latest user info"));
        errorMessage.additionalBases().append(errorDescription.base());
        errorMessage.additionalBases().append(errorDescription.additionalBases());
        errorMessage.details() = errorDescription.details();
        errorDescription = errorMessage;
        QNINFO(errorDescription);
        return false;
    }

    if (m_user.hasAccountLimits()) {
        m_accountLimits = m_user.accountLimits();
        writeAccountLimitsToAppSettings();
    }

    if (!writeUserDataToLocalStorage) {
        return true;
    }

    launchWritingUserDataToLocalStorage();
    return true;
}

void RemoteToLocalSynchronizationManager::launchWritingUserDataToLocalStorage()
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::launchWritingUserDataToLocalStorage"));

    if (m_onceAddedOrUpdatedUserInLocalStorage) {
        QNDEBUG(QStringLiteral("Already added or updated the user data in the local storage, no need to do that again"));
        return;
    }

    connectToLocalStorage();

    // See if this user's entry already exists in the local storage or not
    m_findUserRequestId = QUuid::createUuid();
    QNTRACE(QStringLiteral("Emitting request to find user in the local storage database: request id = ")
            << m_findUserRequestId << QStringLiteral(", user = ") << m_user);
    Q_EMIT findUser(m_user, m_findUserRequestId);
}

bool RemoteToLocalSynchronizationManager::checkAndSyncAccountLimits(const bool waitIfRateLimitReached, ErrorString & errorDescription)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::checkAndSyncAccountLimits: wait if rate limit reached = ")
            << (waitIfRateLimitReached ? QStringLiteral("true") : QStringLiteral("false")));

    if (Q_UNLIKELY(!m_user.hasId())) {
        ErrorString error(QT_TR_NOOP("Detected the attempt to synchronize the account limits before the user id was set"));
        QNWARNING(error);
        Q_EMIT failure(error);
        return false;
    }

    ApplicationSettings appSettings(account(), SYNCHRONIZATION_PERSISTENCE_NAME);
    const QString keyGroup = ACCOUNT_LIMITS_KEY_GROUP + QString::number(m_user.id()) + QStringLiteral("/");

    QVariant accountLimitsLastSyncTime = appSettings.value(keyGroup + ACCOUNT_LIMITS_LAST_SYNC_TIME_KEY);
    if (!accountLimitsLastSyncTime.isNull())
    {
        QNTRACE(QStringLiteral("Found non-null last sync time for account limits: ") << accountLimitsLastSyncTime);

        bool conversionResult = false;
        qint64 timestamp = accountLimitsLastSyncTime.toLongLong(&conversionResult);
        if (conversionResult)
        {
            QNTRACE(QStringLiteral("Successfully read last sync time for account limits: ")
                    << printableDateTimeFromTimestamp(timestamp));
            qint64 currentTimestamp = QDateTime::currentMSecsSinceEpoch();
            qint64 diff = currentTimestamp - timestamp;
            if ((diff > 0) && (diff < THIRTY_DAYS_IN_MSEC)) {
                QNTRACE("The cached account limits appear to be still valid");
                readSavedAccountLimits();
                return true;
            }
        }
    }

    return syncAccountLimits(waitIfRateLimitReached, errorDescription);
}

bool RemoteToLocalSynchronizationManager::syncAccountLimits(const bool waitIfRateLimitReached, ErrorString & errorDescription)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::syncAccountLimits: wait if rate limit reached = ")
            << (waitIfRateLimitReached ? QStringLiteral("true") : QStringLiteral("false")));

    if (Q_UNLIKELY(!m_user.hasServiceLevel())) {
        errorDescription.setBase(QT_TR_NOOP("No Evernote service level was found for the current user"));
        QNDEBUG(errorDescription);
        return false;
    }

    qint32 rateLimitSeconds = 0;
    qint32 errorCode = m_manager.userStore().getAccountLimits(m_user.serviceLevel(), m_accountLimits, errorDescription, rateLimitSeconds);
    if (errorCode == qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED)
    {
        if (rateLimitSeconds <= 0) {
            errorDescription.setBase(QT_TR_NOOP("Rate limit reached but the number of seconds to wait is incorrect"));
            errorDescription.details() = QString::number(rateLimitSeconds);
            QNWARNING(errorDescription);
            return false;
        }

        QNDEBUG(QStringLiteral("Rate limit exceeded, need to wait for ") << rateLimitSeconds << QStringLiteral(" seconds"));
        if (waitIfRateLimitReached)
        {
            int timerId = startTimer(SEC_TO_MSEC(rateLimitSeconds));
            if (Q_UNLIKELY(timerId == 0)) {
                ErrorString errorMessage(QT_TR_NOOP("Failed to start a timer to postpone the Evernote API call "
                                                    "due to rate limit exceeding"));
                errorMessage.additionalBases().append(errorDescription.base());
                errorMessage.additionalBases().append(errorDescription.additionalBases());
                errorMessage.details() = errorDescription.details();
                errorDescription = errorMessage;
                QNWARNING(errorDescription);
                return false;
            }

            m_syncAccountLimitsPostponeTimerId = timerId;

            Q_EMIT rateLimitExceeded(rateLimitSeconds);
        }

        return false;
    }
    else if (errorCode == qevercloud::EDAMErrorCode::AUTH_EXPIRED)
    {
        ErrorString errorMessage(QT_TR_NOOP("unexpected AUTH_EXPIRED error when trying to sync the current user's account limits"));
        errorMessage.additionalBases().append(errorDescription.base());
        errorMessage.additionalBases().append(errorDescription.additionalBases());
        errorMessage.details() = errorDescription.details();
        errorDescription = errorMessage;
        QNWARNING(errorDescription);
        return false;
    }
    else if (errorCode != 0)
    {
        ErrorString errorMessage(QT_TR_NOOP("Failed to get the account limits for the current user"));
        errorMessage.additionalBases().append(errorDescription.base());
        errorMessage.additionalBases().append(errorDescription.additionalBases());
        errorMessage.details() = errorDescription.details();
        errorDescription = errorMessage;
        QNWARNING(errorDescription);
        return false;
    }

    writeAccountLimitsToAppSettings();
    return true;
}

void RemoteToLocalSynchronizationManager::readSavedAccountLimits()
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::readSavedAccountLimits"));

    if (Q_UNLIKELY(!m_user.hasId())) {
        ErrorString error(QT_TR_NOOP("Detected the attempt to read the saved account limits before the user id was set"));
        QNWARNING(error);
        Q_EMIT failure(error);
        return;
    }

    m_accountLimits = qevercloud::AccountLimits();

    ApplicationSettings appSettings(account(), SYNCHRONIZATION_PERSISTENCE_NAME);
    const QString keyGroup = ACCOUNT_LIMITS_KEY_GROUP + QString::number(m_user.id()) + QStringLiteral("/");

    QVariant userMailLimitDaily = appSettings.value(keyGroup + ACCOUNT_LIMITS_USER_MAIL_LIMIT_DAILY_KEY);
    if (!userMailLimitDaily.isNull())
    {
        QNTRACE(QStringLiteral("Found non-null user mail limit daily account limit: ") << userMailLimitDaily);
        bool conversionResult = false;
        qint32 value = userMailLimitDaily.toInt(&conversionResult);
        if (conversionResult) {
            m_accountLimits.userMailLimitDaily = value;
        }
        else {
            QNWARNING(QStringLiteral("Failed to convert user mail limit daily account limit to qint32: ") << userMailLimitDaily);
        }
    }

    QVariant noteSizeMax = appSettings.value(keyGroup + ACCOUNT_LIMITS_NOTE_SIZE_MAX_KEY);
    if (!noteSizeMax.isNull())
    {
        QNTRACE(QStringLiteral("Found non-null note size max: ") << noteSizeMax);
        bool conversionResult = false;
        qint64 value = noteSizeMax.toLongLong(&conversionResult);
        if (conversionResult) {
            m_accountLimits.noteSizeMax = value;
        }
        else {
            QNWARNING(QStringLiteral("Failed to convert note size max account limit to qint64: ") << noteSizeMax);
        }
    }

    QVariant resourceSizeMax = appSettings.value(keyGroup + ACCOUNT_LIMITS_RESOURCE_SIZE_MAX_KEY);
    if (!resourceSizeMax.isNull())
    {
        QNTRACE(QStringLiteral("Found non-null resource size max: ") << resourceSizeMax);
        bool conversionResult = false;
        qint64 value = resourceSizeMax.toLongLong(&conversionResult);
        if (conversionResult) {
            m_accountLimits.resourceSizeMax = value;
        }
        else {
            QNWARNING(QStringLiteral("Failed to convert resource size max account limit to qint64: ") << resourceSizeMax);
        }
    }

    QVariant userLinkedNotebookMax = appSettings.value(keyGroup + ACCOUNT_LIMITS_USER_LINKED_NOTEBOOK_MAX_KEY);
    if (!userLinkedNotebookMax.isNull())
    {
        QNTRACE(QStringLiteral("Found non-null user linked notebook max: ") << userLinkedNotebookMax);
        bool conversionResult = false;
        qint32 value = userLinkedNotebookMax.toInt(&conversionResult);
        if (conversionResult) {
            m_accountLimits.userLinkedNotebookMax = value;
        }
        else {
            QNWARNING(QStringLiteral("Failed to convert user linked notebook max account limit to qint32: ") << userLinkedNotebookMax);
        }
    }

    QVariant uploadLimit = appSettings.value(keyGroup + ACCOUNT_LIMITS_UPLOAD_LIMIT_KEY);
    if (!uploadLimit.isNull())
    {
        QNTRACE(QStringLiteral("Found non-null upload limit: ") << uploadLimit);
        bool conversionResult = false;
        qint64 value = uploadLimit.toLongLong(&conversionResult);
        if (conversionResult) {
            m_accountLimits.uploadLimit = value;
        }
        else {
            QNWARNING(QStringLiteral("Failed to convert upload limit to qint64: ") << uploadLimit);
        }
    }

    QVariant userNoteCountMax = appSettings.value(keyGroup + ACCOUNT_LIMITS_USER_NOTE_COUNT_MAX_KEY);
    if (!userNoteCountMax.isNull())
    {
        QNTRACE(QStringLiteral("Found non-null user note count max: ") << userNoteCountMax);
        bool conversionResult = false;
        qint32 value = userNoteCountMax.toInt(&conversionResult);
        if (conversionResult) {
            m_accountLimits.userNoteCountMax = value;
        }
        else {
            QNWARNING(QStringLiteral("Failed to convert user note count max to qint32: ") << userNoteCountMax);
        }
    }

    QVariant userNotebookCountMax = appSettings.value(keyGroup + ACCOUNT_LIMITS_USER_NOTEBOOK_COUNT_MAX_KEY);
    if (!userNotebookCountMax.isNull())
    {
        QNTRACE(QStringLiteral("Found non-null user notebook count max: ") << userNotebookCountMax);
        bool conversionResult = false;
        qint32 value = userNotebookCountMax.toInt(&conversionResult);
        if (conversionResult) {
            m_accountLimits.userNotebookCountMax = value;
        }
        else {
            QNWARNING(QStringLiteral("Failed to convert user notebook count max to qint32: ") << userNotebookCountMax);
        }
    }

    QVariant userTagCountMax = appSettings.value(keyGroup + ACCOUNT_LIMITS_USER_TAG_COUNT_MAX_KEY);
    if (!userTagCountMax.isNull())
    {
        QNTRACE(QStringLiteral("Found non-null user tag count max: ") << userTagCountMax);
        bool conversionResult = false;
        qint32 value = userTagCountMax.toInt(&conversionResult);
        if (conversionResult) {
            m_accountLimits.userTagCountMax = value;
        }
        else {
            QNWARNING(QStringLiteral("Failed to convert user tag count max to qint32: ") << userTagCountMax);
        }
    }

    QVariant noteTagCountMax = appSettings.value(keyGroup + ACCOUNT_LIMITS_NOTE_TAG_COUNT_MAX_KEY);
    if (!noteTagCountMax.isNull())
    {
        QNTRACE(QStringLiteral("Found non-null note tag cont max: ") << noteTagCountMax);
        bool conversionResult = false;
        qint32 value = noteTagCountMax.toInt(&conversionResult);
        if (conversionResult) {
            m_accountLimits.noteTagCountMax = value;
        }
        else {
            QNWARNING(QStringLiteral("Failed to convert note tag count max to qint32: ") << noteTagCountMax);
        }
    }

    QVariant userSavedSearchesMax = appSettings.value(keyGroup + ACCOUNT_LIMITS_USER_SAVED_SEARCH_COUNT_MAX_KEY);
    if (!userSavedSearchesMax.isNull())
    {
        QNTRACE(QStringLiteral("Found non-null user saved search max: ") << userSavedSearchesMax);
        bool conversionResult = false;
        qint32 value = userSavedSearchesMax.toInt(&conversionResult);
        if (conversionResult) {
            m_accountLimits.userSavedSearchesMax = value;
        }
        else {
            QNWARNING(QStringLiteral("Failed to convert user saved search max to qint32: ") << userSavedSearchesMax);
        }
    }

    QVariant noteResourceCountMax = appSettings.value(keyGroup + ACCOUNT_LIMITS_NOTE_RESOURCE_COUNT_MAX_KEY);
    if (!noteResourceCountMax.isNull())
    {
        QNTRACE(QStringLiteral("Found non-null note resource count max: ") << noteResourceCountMax);
        bool conversionResult = false;
        qint32 value = noteResourceCountMax.toInt(&conversionResult);
        if (conversionResult) {
            m_accountLimits.noteResourceCountMax = value;
        }
        else {
            QNWARNING(QStringLiteral("Failed to convert note resource count max to qint32: ") << noteResourceCountMax);
        }
    }

    QNTRACE("Read account limits from application settings: " << m_accountLimits);
}

void RemoteToLocalSynchronizationManager::writeAccountLimitsToAppSettings()
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::writeAccountLimitsToAppSettings"));

    if (Q_UNLIKELY(!m_user.hasId())) {
        ErrorString error(QT_TR_NOOP("Detected the attempt to save the account limits to app settings before the user id was set"));
        QNWARNING(error);
        Q_EMIT failure(error);
        return;
    }

    ApplicationSettings appSettings(account(), SYNCHRONIZATION_PERSISTENCE_NAME);
    const QString keyGroup = ACCOUNT_LIMITS_KEY_GROUP + QString::number(m_user.id()) + QStringLiteral("/");

    appSettings.setValue(keyGroup + ACCOUNT_LIMITS_USER_MAIL_LIMIT_DAILY_KEY,
                         (m_accountLimits.userMailLimitDaily.isSet()
                          ? QVariant(m_accountLimits.userMailLimitDaily.ref())
                          : QVariant()));

    appSettings.setValue(keyGroup + ACCOUNT_LIMITS_NOTE_SIZE_MAX_KEY,
                         (m_accountLimits.noteSizeMax.isSet()
                          ? QVariant(m_accountLimits.noteSizeMax.ref())
                          : QVariant()));

    appSettings.setValue(keyGroup + ACCOUNT_LIMITS_RESOURCE_SIZE_MAX_KEY,
                         (m_accountLimits.resourceSizeMax.isSet()
                          ? QVariant(m_accountLimits.resourceSizeMax.ref())
                          : QVariant()));

    appSettings.setValue(keyGroup + ACCOUNT_LIMITS_USER_LINKED_NOTEBOOK_MAX_KEY,
                         (m_accountLimits.userLinkedNotebookMax.isSet()
                          ? QVariant(m_accountLimits.userLinkedNotebookMax.ref())
                          : QVariant()));

    appSettings.setValue(keyGroup + ACCOUNT_LIMITS_UPLOAD_LIMIT_KEY,
                         (m_accountLimits.uploadLimit.isSet()
                          ? QVariant(m_accountLimits.uploadLimit.ref())
                          : QVariant()));

    appSettings.setValue(keyGroup + ACCOUNT_LIMITS_USER_NOTE_COUNT_MAX_KEY,
                         (m_accountLimits.userNoteCountMax.isSet()
                          ? QVariant(m_accountLimits.userNoteCountMax.ref())
                          : QVariant()));

    appSettings.setValue(keyGroup + ACCOUNT_LIMITS_USER_NOTEBOOK_COUNT_MAX_KEY,
                         (m_accountLimits.userNotebookCountMax.isSet()
                          ? QVariant(m_accountLimits.userNotebookCountMax.ref())
                          : QVariant()));

    appSettings.setValue(keyGroup + ACCOUNT_LIMITS_USER_TAG_COUNT_MAX_KEY,
                         (m_accountLimits.userTagCountMax.isSet()
                          ? QVariant(m_accountLimits.userTagCountMax.ref())
                          : QVariant()));

    appSettings.setValue(keyGroup + ACCOUNT_LIMITS_NOTE_TAG_COUNT_MAX_KEY,
                         (m_accountLimits.noteTagCountMax.isSet()
                          ? QVariant(m_accountLimits.noteTagCountMax.ref())
                          : QVariant()));

    appSettings.setValue(keyGroup + ACCOUNT_LIMITS_USER_SAVED_SEARCH_COUNT_MAX_KEY,
                         (m_accountLimits.userSavedSearchesMax.isSet()
                          ? QVariant(m_accountLimits.userSavedSearchesMax.ref())
                          : QVariant()));

    appSettings.setValue(keyGroup + ACCOUNT_LIMITS_NOTE_RESOURCE_COUNT_MAX_KEY,
                         (m_accountLimits.noteResourceCountMax.isSet()
                          ? QVariant(m_accountLimits.noteResourceCountMax.ref())
                          : QVariant()));

    appSettings.setValue(keyGroup + ACCOUNT_LIMITS_LAST_SYNC_TIME_KEY, QVariant(QDateTime::currentMSecsSinceEpoch()));
}

template <class ContainerType, class ElementType>
void RemoteToLocalSynchronizationManager::launchDataElementSyncCommon(const ContentSource::type contentSource, ContainerType & container,
                                                                      QList<QString> & expungedElements)
{
    bool syncingUserAccountData = (contentSource == ContentSource::UserAccount);
    QNTRACE(QStringLiteral("syncingUserAccountData = ") << (syncingUserAccountData ? QStringLiteral("true") : QStringLiteral("false")));

    const auto & syncChunks = (syncingUserAccountData ? m_syncChunks : m_linkedNotebookSyncChunks);

    container.clear();
    int numSyncChunks = syncChunks.size();
    QNTRACE(QStringLiteral("Num sync chunks = ") << numSyncChunks);

    for(int i = 0; i < numSyncChunks; ++i) {
        const qevercloud::SyncChunk & syncChunk = syncChunks[i];
        appendDataElementsFromSyncChunkToContainer<ContainerType>(syncChunk, container);
        extractExpungedElementsFromSyncChunk<ElementType>(syncChunk, expungedElements);
    }
}

template <class ContainerType, class ElementType>
void RemoteToLocalSynchronizationManager::launchDataElementSync(const ContentSource::type contentSource, const QString & typeName,
                                                                ContainerType & container, QList<QString> & expungedElements)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::launchDataElementSync: ") << typeName);

    launchDataElementSyncCommon<ContainerType, ElementType>(contentSource, container, expungedElements);

    if (container.isEmpty()) {
        QNDEBUG(QStringLiteral("No new or updated data items within the container"));
        return;
    }

    int numElements = container.size();

    if (typeName == QStringLiteral("Note")) {
        m_originalNumberOfNotes = static_cast<quint32>(std::max(numElements, 0));
        m_numNotesDownloaded = static_cast<quint32>(0);
    }
    else if (typeName == QStringLiteral("Resource")) {
        m_originalNumberOfResources = static_cast<quint32>(std::max(numElements, 0));
        m_numResourcesDownloaded = static_cast<quint32>(0);
    }

    for(int i = 0; i < numElements; ++i)
    {
        const typename ContainerType::value_type & element = container[i];
        if (!element.guid.isSet()) {
            SET_CANT_FIND_BY_GUID_ERROR();
            Q_EMIT failure(errorDescription);
            return;
        }

        emitFindByGuidRequest(element);
    }
}

template <>
void RemoteToLocalSynchronizationManager::launchDataElementSync<RemoteToLocalSynchronizationManager::TagsList, Tag>(const ContentSource::type contentSource, const QString & typeName,
                                                                                                                    RemoteToLocalSynchronizationManager::TagsList & container, QList<QString> & expungedElements)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::launchDataElementSync: ") << typeName);

    launchDataElementSyncCommon<RemoteToLocalSynchronizationManager::TagsList, Tag>(contentSource, container, expungedElements);

    if (container.isEmpty()) {
        QNDEBUG(QStringLiteral("No data items within the container"));
        return;
    }

    if (!sortTagsByParentChildRelations(container)) {
        return;
    }

    m_tagsPendingProcessing = m_tags;

    // NOTE: the whole point behind the explicit specialization of this template method for tags
    // is due to the parent-child relations between them: parent tags need to be added to the local storage
    // before their children, otherwise the local database would have a constraint failure; by now the tags
    // are already sorted by parent-child relations but they need to be processed one by one

    syncNextTagPendingProcessing();
}

void RemoteToLocalSynchronizationManager::launchTagsSync()
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::launchTagsSync"));
    m_pendingTagsSyncStart = false;
    launchDataElementSync<TagsList, Tag>(ContentSource::UserAccount, QStringLiteral("Tag"), m_tags, m_expungedTags);
}

void RemoteToLocalSynchronizationManager::launchSavedSearchSync()
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::launchSavedSearchSync"));
    launchDataElementSync<SavedSearchesList, SavedSearch>(ContentSource::UserAccount, QStringLiteral("Saved search"),
                                                          m_savedSearches, m_expungedSavedSearches);
}

void RemoteToLocalSynchronizationManager::launchLinkedNotebookSync()
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::launchLinkedNotebookSync"));
    m_pendingLinkedNotebooksSyncStart = false;
    launchDataElementSync<LinkedNotebooksList, LinkedNotebook>(ContentSource::UserAccount, QStringLiteral("Linked notebook"),
                                                               m_linkedNotebooks, m_expungedLinkedNotebooks);
}

void RemoteToLocalSynchronizationManager::launchNotebookSync()
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::launchNotebookSync"));
    m_pendingNotebooksSyncStart = false;
    launchDataElementSync<NotebooksList, Notebook>(ContentSource::UserAccount, QStringLiteral("Notebook"), m_notebooks, m_expungedNotebooks);
}

void RemoteToLocalSynchronizationManager::collectSyncedGuidsForFullSyncStaleDataItemsExpunger()
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::collectSyncedGuidsForFullSyncStaleDataItemsExpunger"));

    m_fullSyncStaleDataItemsSyncedGuids.m_syncedNotebookGuids.clear();
    m_fullSyncStaleDataItemsSyncedGuids.m_syncedTagGuids.clear();
    m_fullSyncStaleDataItemsSyncedGuids.m_syncedNoteGuids.clear();
    m_fullSyncStaleDataItemsSyncedGuids.m_syncedSavedSearchGuids.clear();

    m_fullSyncStaleDataItemsSyncedGuids.m_syncedNotebookGuids.reserve(m_notebooks.size());
    for(auto it = m_notebooks.constBegin(), end = m_notebooks.constEnd(); it != end; ++it)
    {
        const qevercloud::Notebook & notebook = *it;
        if (notebook.guid.isSet()) {
            Q_UNUSED(m_fullSyncStaleDataItemsSyncedGuids.m_syncedNotebookGuids.insert(notebook.guid.ref()))
        }
    }

    m_fullSyncStaleDataItemsSyncedGuids.m_syncedTagGuids.reserve(m_tags.size());
    for(auto it = m_tags.constBegin(), end = m_tags.constEnd(); it != end; ++it)
    {
        const qevercloud::Tag & tag = *it;
        if (tag.guid.isSet()) {
            Q_UNUSED(m_fullSyncStaleDataItemsSyncedGuids.m_syncedTagGuids.insert(tag.guid.ref()))
        }
    }

    m_fullSyncStaleDataItemsSyncedGuids.m_syncedNoteGuids.reserve(m_notes.size());
    for(auto it = m_notes.constBegin(), end = m_notes.constEnd(); it != end; ++it)
    {
        const qevercloud::Note & note = *it;
        if (note.guid.isSet()) {
            Q_UNUSED(m_fullSyncStaleDataItemsSyncedGuids.m_syncedNoteGuids.insert(note.guid.ref()))
        }
    }

    m_fullSyncStaleDataItemsSyncedGuids.m_syncedSavedSearchGuids.reserve(m_savedSearches.size());
    for(auto it = m_savedSearches.constBegin(), end = m_savedSearches.constEnd(); it != end; ++it)
    {
        const qevercloud::SavedSearch & savedSearch = *it;
        if (savedSearch.guid.isSet()) {
            Q_UNUSED(m_fullSyncStaleDataItemsSyncedGuids.m_syncedSavedSearchGuids.insert(savedSearch.guid.ref()))
        }
    }
}

void RemoteToLocalSynchronizationManager::launchFullSyncStaleDataItemsExpunger()
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::launchFullSyncStaleDataItemsExpunger"));

    if (m_pFullSyncStaleDataItemsExpunger) {
        junkFullSyncStaleDataItemsExpunger(*m_pFullSyncStaleDataItemsExpunger);
        m_pFullSyncStaleDataItemsExpunger = Q_NULLPTR;
    }

    m_pFullSyncStaleDataItemsExpunger = new FullSyncStaleDataItemsExpunger(m_manager.localStorageManagerAsync(),
                                                                           m_notebookSyncCache, m_tagSyncCache,
                                                                           m_savedSearchSyncCache,
                                                                           m_fullSyncStaleDataItemsSyncedGuids,
                                                                           QString(), this);
    QObject::connect(m_pFullSyncStaleDataItemsExpunger, QNSIGNAL(FullSyncStaleDataItemsExpunger,finished),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onFullSyncStaleDataItemsExpungerFinished));
    QObject::connect(m_pFullSyncStaleDataItemsExpunger, QNSIGNAL(FullSyncStaleDataItemsExpunger,failure,ErrorString),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onFullSyncStaleDataItemsExpungerFailure,ErrorString));
    QNDEBUG(QStringLiteral("Starting FullSyncStaleDataItemsExpunger for user's own content"));
    m_pFullSyncStaleDataItemsExpunger->start();
}

bool RemoteToLocalSynchronizationManager::launchFullSyncStaleDataItemsExpungersForLinkedNotebooks()
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::launchFullSyncStaleDataItemsExpungersForLinkedNotebooks"));

    bool foundLinkedNotebookEligibleForFullSyncStaleDataItemsExpunging = false;

    for(auto it = m_allLinkedNotebooks.constBegin(), end = m_allLinkedNotebooks.constEnd(); it != end; ++it)
    {
        const LinkedNotebook & linkedNotebook = *it;
        if (Q_UNLIKELY(!linkedNotebook.hasGuid())) {
            QNWARNING(QStringLiteral("Skipping linked notebook without guid: ") << linkedNotebook);
            continue;
        }

        const QString & linkedNotebookGuid = linkedNotebook.guid();
        QNTRACE(QStringLiteral("Examining linked notebook with guid ") << linkedNotebookGuid);

        auto fullSyncIt = m_linkedNotebookGuidsForWhichFullSyncWasPerformed.find(linkedNotebookGuid);
        if (fullSyncIt == m_linkedNotebookGuidsForWhichFullSyncWasPerformed.end()) {
            QNTRACE(QStringLiteral("It doesn't appear that full sync was performed for linked notebook with guid ")
                    << linkedNotebookGuid << QStringLiteral(" in the past"));
            continue;
        }

        auto onceFullSyncIt = m_linkedNotebookGuidsOnceFullySynced.find(linkedNotebookGuid);
        if (onceFullSyncIt == m_linkedNotebookGuidsOnceFullySynced.end()) {
            QNTRACE(QStringLiteral("It appears the full sync was performed for the first time for linked notebook with guid ")
                    << linkedNotebookGuid);
            continue;
        }

        QNDEBUG(QStringLiteral("The contents of a linked notebook with guid ") << linkedNotebookGuid
                << QStringLiteral(" were fully synced after being fully synced in the past, need to seek for stale data items and expunge them"));
        foundLinkedNotebookEligibleForFullSyncStaleDataItemsExpunging = true;

        FullSyncStaleDataItemsExpunger::SyncedGuids syncedGuids;

        for(auto nit = m_linkedNotebookGuidsByNotebookGuids.constBegin(), nend = m_linkedNotebookGuidsByNotebookGuids.constEnd(); nit != nend; ++nit)
        {
            const QString & currentLinkedNotebookGuid = nit.value();
            if (currentLinkedNotebookGuid != linkedNotebookGuid) {
                continue;
            }

            const QString & notebookGuid = nit.key();
            Q_UNUSED(syncedGuids.m_syncedNotebookGuids.insert(notebookGuid))

            for(auto noteIt = m_notes.constBegin(), noteEnd = m_notes.constEnd(); noteIt != noteEnd; ++noteIt)
            {
                const qevercloud::Note & note = *noteIt;
                if (note.guid.isSet() && note.notebookGuid.isSet() && (note.notebookGuid.ref() == notebookGuid)) {
                    Q_UNUSED(syncedGuids.m_syncedNoteGuids.insert(note.guid.ref()))
                }
            }
        }

        for(auto tit = m_linkedNotebookGuidsByTagGuids.constBegin(), tend = m_linkedNotebookGuidsByTagGuids.constEnd(); tit != tend; ++tit)
        {
            const QString & currentLinkedNotebookGuid = tit.value();
            if (currentLinkedNotebookGuid != linkedNotebookGuid) {
                continue;
            }

            const QString & tagGuid = tit.key();
            Q_UNUSED(syncedGuids.m_syncedTagGuids.insert(tagGuid))
        }

        auto notebookSyncCacheIt = m_notebookSyncCachesByLinkedNotebookGuids.find(linkedNotebookGuid);
        if (notebookSyncCacheIt == m_notebookSyncCachesByLinkedNotebookGuids.end()) {
            NotebookSyncCache * pNotebookSyncCache = new NotebookSyncCache(m_manager.localStorageManagerAsync(), linkedNotebookGuid, this);
            notebookSyncCacheIt = m_notebookSyncCachesByLinkedNotebookGuids.insert(linkedNotebookGuid, pNotebookSyncCache);
        }

        auto tagSyncCacheIt = m_tagSyncCachesByLinkedNotebookGuids.find(linkedNotebookGuid);
        if (tagSyncCacheIt == m_tagSyncCachesByLinkedNotebookGuids.end()) {
            TagSyncCache * pTagSyncCache = new TagSyncCache(m_manager.localStorageManagerAsync(), linkedNotebookGuid, this);
            tagSyncCacheIt = m_tagSyncCachesByLinkedNotebookGuids.insert(linkedNotebookGuid, pTagSyncCache);
        }

        FullSyncStaleDataItemsExpunger * pExpunger = new FullSyncStaleDataItemsExpunger(m_manager.localStorageManagerAsync(),
                                                                                        *notebookSyncCacheIt.value(),
                                                                                        *tagSyncCacheIt.value(),
                                                                                        m_savedSearchSyncCache, syncedGuids,
                                                                                        linkedNotebookGuid, this);
        m_fullSyncStaleDataItemsExpungersByLinkedNotebookGuid[linkedNotebookGuid] = pExpunger;
        QObject::connect(pExpunger, QNSIGNAL(FullSyncStaleDataItemsExpunger,finished),
                         this, QNSLOT(RemoteToLocalSynchronizationManager,onFullSyncStaleDataItemsExpungerFinished));
        QObject::connect(pExpunger, QNSIGNAL(FullSyncStaleDataItemsExpunger,failure,ErrorString),
                         this, QNSLOT(RemoteToLocalSynchronizationManager,onFullSyncStaleDataItemsExpungerFailure,ErrorString));
        QNDEBUG(QStringLiteral("Starting FullSyncStaleDataItemsExpunger for the content from linked notebook with guid ")
                << linkedNotebookGuid);
        pExpunger->start();
    }

    return foundLinkedNotebookEligibleForFullSyncStaleDataItemsExpunging;
}

void RemoteToLocalSynchronizationManager::launchExpungingOfNotelessTagsFromLinkedNotebooks()
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::launchExpungingOfNotelessTagsFromLinkedNotebooks"));

    m_expungeNotelessTagsRequestId = QUuid::createUuid();
    QNTRACE(QStringLiteral("Emitting the request to expunge noteless tags from linked notebooks: ")
            << m_expungeNotelessTagsRequestId);
    Q_EMIT expungeNotelessTagsFromLinkedNotebooks(m_expungeNotelessTagsRequestId);
}

bool RemoteToLocalSynchronizationManager::syncingLinkedNotebooksContent() const
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::syncingLinkedNotebooksContent: last sync mode = ")
            << m_lastSyncMode << QStringLiteral(", full note contents downloaded = ")
            << (m_fullNoteContentsDownloaded ? QStringLiteral("true") : QStringLiteral("false"))
            << QStringLiteral(", expunged from server to client = ")
            << (m_expungedFromServerToClient ? QStringLiteral("true") : QStringLiteral("false")));

    if (m_lastSyncMode == SyncMode::FullSync) {
        return m_fullNoteContentsDownloaded;
    }

    return m_expungedFromServerToClient;
}

void RemoteToLocalSynchronizationManager::checkAndIncrementNoteDownloadProgress(const QString & noteGuid)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::checkAndIncrementNoteDownloadProgress: note guid = ") << noteGuid);

    if (m_originalNumberOfNotes == 0) {
        QNDEBUG(QStringLiteral("No notes to download"));
        return;
    }

    if (m_resourceGuidsPendingInkNoteImageDownloadPerNoteGuid.contains(noteGuid)) {
        QNDEBUG(QStringLiteral("Found still pending ink note image download(s) for this note guid, won't increment the note download progress"));
        return;
    }

    if (m_notesPendingThumbnailDownloadByGuid.contains(noteGuid)) {
        QNDEBUG(QStringLiteral("Found still pending note thumbnail download for this note guid, won't increment the note download progress"));
        return;
    }

    if (Q_UNLIKELY(m_numNotesDownloaded == m_originalNumberOfNotes)) {
        QNWARNING(QStringLiteral("The count of downloaded notes (") << m_numNotesDownloaded
                  << QStringLiteral(") is already equal to the original number of notes (")
                  << m_originalNumberOfNotes << QStringLiteral("), won't increment it further"));
        return;
    }

    ++m_numNotesDownloaded;
    QNTRACE(QStringLiteral("Incremented the number of downloaded notes to ")
            << m_numNotesDownloaded << QStringLiteral(", the total number of notes to download = ")
            << m_originalNumberOfNotes);

    if (syncingLinkedNotebooksContent()) {
        Q_EMIT linkedNotebooksNotesDownloadProgress(m_numNotesDownloaded, m_originalNumberOfNotes);
    }
    else {
        Q_EMIT notesDownloadProgress(m_numNotesDownloaded, m_originalNumberOfNotes);
    }
}

void RemoteToLocalSynchronizationManager::checkAndIncrementResourceDownloadProgress(const QString & resourceGuid)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::checkAndIncrementResourceDownloadProgress: resource guid = ")
            << resourceGuid);

    if (m_originalNumberOfResources == 0) {
        QNDEBUG(QStringLiteral("No resources to download"));
        return;
    }

    if (Q_UNLIKELY(m_numResourcesDownloaded == m_originalNumberOfResources)) {
        QNWARNING(QStringLiteral("The count of downloaded resources (") << m_numResourcesDownloaded
                  << QStringLiteral(") is already equal to the original number of resources (")
                  << m_originalNumberOfResources << QStringLiteral("(, won't increment it further"));
        return;
    }

    ++m_numResourcesDownloaded;
    QNTRACE(QStringLiteral("Incremented the number of downloaded resources to ")
            << m_numResourcesDownloaded << QStringLiteral(", the total number of resources to download = ")
            << m_originalNumberOfResources);

    if (syncingLinkedNotebooksContent()) {
        Q_EMIT linkedNotebooksResourcesDownloadProgress(m_numResourcesDownloaded, m_originalNumberOfResources);
    }
    else {
        Q_EMIT resourcesDownloadProgress(m_numResourcesDownloaded, m_originalNumberOfResources);
    }
}

bool RemoteToLocalSynchronizationManager::notebooksSyncInProgress() const
{
    if (!m_pendingNotebooksSyncStart &&
        (!m_notebooks.isEmpty() ||
         !m_notebooksPendingAddOrUpdate.isEmpty() ||
         !m_findNotebookByGuidRequestIds.isEmpty() ||
         !m_findNotebookByNameRequestIds.isEmpty() ||
         !m_addNotebookRequestIds.isEmpty() ||
         !m_updateNotebookRequestIds.isEmpty() ||
         !m_expungeNotebookRequestIds.isEmpty()))
    {
        QNDEBUG(QStringLiteral("Notebooks sync is in progress: there are ")
                << m_notebooks.size() << QStringLiteral(" notebooks pending processing and/or ")
                << m_notebooksPendingAddOrUpdate.size() << QStringLiteral(" notebooks pending add or update within the local storage: pending ")
                << m_addNotebookRequestIds.size() << QStringLiteral(" add notebook requests and/or ")
                << m_updateNotebookRequestIds.size() << QStringLiteral(" update notebook request ids; ")
                << m_findNotebookByGuidRequestIds.size() << QStringLiteral(" find notebook by guid requests and/or ")
                << m_findNotebookByNameRequestIds.size() << QStringLiteral(" find notebook by name requests and/or ")
                << m_expungeNotebookRequestIds.size() << QStringLiteral(" expunge notebook requests"));
        return true;
    }

    QList<NotebookSyncConflictResolver*> notebookSyncConflictResolvers = findChildren<NotebookSyncConflictResolver*>();
    return !notebookSyncConflictResolvers.isEmpty();
}

bool RemoteToLocalSynchronizationManager::tagsSyncInProgress() const
{
    if (!m_pendingTagsSyncStart &&
        (!m_tagsPendingProcessing.isEmpty() ||
         !m_tagsPendingAddOrUpdate.isEmpty() ||
         !m_findTagByGuidRequestIds.isEmpty() ||
         !m_findTagByNameRequestIds.isEmpty() ||
         !m_addTagRequestIds.isEmpty() ||
         !m_updateTagRequestIds.isEmpty() ||
         !m_expungeTagRequestIds.isEmpty()))
    {
        return true;
    }

    QList<TagSyncConflictResolver*> tagSyncConflictResolvers = findChildren<TagSyncConflictResolver*>();
    return !tagSyncConflictResolvers.isEmpty();
}

bool RemoteToLocalSynchronizationManager::notesSyncInProgress() const
{
    return (!m_notesPendingAddOrUpdate.isEmpty() ||
            !m_findNoteByGuidRequestIds.isEmpty() ||
            !m_addNoteRequestIds.isEmpty() ||
            !m_updateNoteRequestIds.isEmpty() ||
            !m_expungeNoteRequestIds.isEmpty() ||
            !m_notesToAddPerAPICallPostponeTimerId.isEmpty() ||
            !m_notesToUpdatePerAPICallPostponeTimerId.isEmpty() ||
            !m_guidsOfNotesPendingDownloadForAddingToLocalStorage.isEmpty() ||
            !m_notesPendingDownloadForUpdatingInLocalStorageByGuid.isEmpty() ||
            !m_notesPendingInkNoteImagesDownloadByFindNotebookRequestId.isEmpty() ||
            !m_notesPendingThumbnailDownloadByFindNotebookRequestId.isEmpty() ||
            !m_notesPendingThumbnailDownloadByGuid.isEmpty() ||
            !m_updateNoteWithThumbnailRequestIds.isEmpty());
}

bool RemoteToLocalSynchronizationManager::resourcesSyncInProgress() const
{
    return (!m_resourcesPendingAddOrUpdate.isEmpty() ||
            !m_findResourceByGuidRequestIds.isEmpty() ||
            !m_addResourceRequestIds.isEmpty() ||
            !m_updateResourceRequestIds.isEmpty() ||
            !m_resourcesByMarkNoteOwningResourceDirtyRequestIds.isEmpty() ||
            !m_resourcesWithFindRequestIdsPerFindNoteRequestId.isEmpty() ||
            !m_inkNoteResourceDataPerFindNotebookRequestId.isEmpty() ||
            !m_notesOwningResourcesPendingDownloadForAddingToLocalStorageByResourceGuid.isEmpty() ||
            !m_resourcesPendingDownloadForUpdatingInLocalStorageWithNotesByResourceGuid.isEmpty() ||
            !m_resourcesToAddWithNotesPerAPICallPostponeTimerId.isEmpty() ||
            !m_resourcesToUpdateWithNotesPerAPICallPostponeTimerId.isEmpty() ||
            !m_postponedConflictingResourceDataPerAPICallPostponeTimerId.isEmpty());
}

QTextStream & operator<<(QTextStream & strm, const RemoteToLocalSynchronizationManager::ContentSource::type & obj)
{
    switch(obj)
    {
    case RemoteToLocalSynchronizationManager::ContentSource::UserAccount:
        strm << QStringLiteral("UserAccount");
        break;
    case RemoteToLocalSynchronizationManager::ContentSource::LinkedNotebook:
        strm << QStringLiteral("LinkedNotebook");
        break;
    default:
        strm << QStringLiteral("Unknown");
        break;
    }

    return strm;
}

void RemoteToLocalSynchronizationManager::checkNotebooksAndTagsSyncCompletionAndLaunchNotesSync()
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::checkNotebooksAndTagsSyncCompletionAndLaunchNotesSync"));

    if (!m_pendingNotebooksSyncStart && !notebooksSyncInProgress() &&
        !m_pendingTagsSyncStart && !tagsSyncInProgress())
    {
        launchNotesSync(syncingLinkedNotebooksContent()
                        ? ContentSource::LinkedNotebook
                        : ContentSource::UserAccount);
    }
}

void RemoteToLocalSynchronizationManager::launchNotesSync(const ContentSource::type & contentSource)
{
    launchDataElementSync<NotesList, Note>(contentSource, QStringLiteral("Note"), m_notes, m_expungedNotes);
}

void RemoteToLocalSynchronizationManager::checkNotesSyncCompletionAndLaunchResourcesSync()
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::checkNotesSyncCompletionAndLaunchResourcesSync"));

    if (m_lastSyncMode != SyncMode::IncrementalSync)
    {
        /**
         * NOTE: during the full sync the individual resources are not synced,
         * instead the full note contents including the resources are synced.
         *
         * That works both for the content from user's own account and for the stuff
         * from linked notebooks: the sync of linked notebooks' content might be
         * full while the last sync of user's own content is incremental
         * but in this case there won't be resources within the synch chunk
         * downloaded for that linked notebook so there's no real problem with us
         * not getting inside this if block when syncing stuff from the linked
         * notebooks
         */
        return;
    }

    if (!notesSyncInProgress() && !resourcesSyncInProgress()) {
        launchResourcesSync();
    }
}

void RemoteToLocalSynchronizationManager::launchResourcesSync()
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::launchResourcesSync"));
    QList<QString> dummyList;
    launchDataElementSync<ResourcesList, Resource>(ContentSource::UserAccount, QStringLiteral("Resource"), m_resources, dummyList);
}

void RemoteToLocalSynchronizationManager::checkLinkedNotebooksSyncAndLaunchLinkedNotebookContentSync()
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::checkLinkedNotebooksSyncAndLaunchLinkedNotebookContentSync"));

    if (m_updateLinkedNotebookRequestIds.isEmpty() && m_addLinkedNotebookRequestIds.isEmpty()) {
        // All remote linked notebooks were already updated in the local storage or added there
        startLinkedNotebooksSync();
    }
}

void RemoteToLocalSynchronizationManager::checkLinkedNotebooksNotebooksAndTagsSyncAndLaunchLinkedNotebookNotesSync()
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::checkLinkedNotebooksNotebooksAndTagsSyncAndLaunchLinkedNotebookNotesSync"));

    if (m_updateNotebookRequestIds.isEmpty() && m_addNotebookRequestIds.isEmpty() &&
        m_updateTagRequestIds.isEmpty() && m_addTagRequestIds.isEmpty())
    {
        // All remote notebooks and tags from linked notebooks were already either updated in the local storage or added there
        launchLinkedNotebooksNotesSync();
    }
}

void RemoteToLocalSynchronizationManager::launchLinkedNotebooksContentsSync()
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::launchLinkedNotebooksContentsSync"));

    m_pendingTagsSyncStart = true;
    m_pendingNotebooksSyncStart = true;

    launchLinkedNotebooksTagsSync();
    launchLinkedNotebooksNotebooksSync();

    // NOTE: we might have received the only sync chunk without the actual data elements, need to check for such case
    // and leave if there's nothing worth processing within the sync
    checkServerDataMergeCompletion();
}

template <>
bool RemoteToLocalSynchronizationManager::mapContainerElementsWithLinkedNotebookGuid<RemoteToLocalSynchronizationManager::TagsList>(const QString & linkedNotebookGuid,
                                                                                                                                    const RemoteToLocalSynchronizationManager::TagsList & tags)
{
    const int numTags = tags.size();
    for(int i = 0; i < numTags; ++i)
    {
        const qevercloud::Tag & tag = tags[i];
        if (!tag.guid.isSet())
        {
            ErrorString error(QT_TR_NOOP("Detected the attempt to map the linked notebook guid to a tag without guid"));
            if (tag.name.isSet()) {
                error.details() = tag.name.ref();
            }

            QNWARNING(error << QStringLiteral(", tag: ") << tag);
            Q_EMIT failure(error);
            return false;
        }

        m_linkedNotebookGuidsByTagGuids[tag.guid.ref()] = linkedNotebookGuid;
    }

    return true;
}

template <>
bool RemoteToLocalSynchronizationManager::mapContainerElementsWithLinkedNotebookGuid<RemoteToLocalSynchronizationManager::NotebooksList>(const QString & linkedNotebookGuid,
                                                                                                                                         const RemoteToLocalSynchronizationManager::NotebooksList & notebooks)
{
    const int numNotebooks = notebooks.size();
    for(int i = 0; i < numNotebooks; ++i)
    {
        const qevercloud::Notebook & notebook = notebooks[i];
        if (!notebook.guid.isSet())
        {
            ErrorString error(QT_TR_NOOP("Detected the attempt to map the linked notebook guid to a notebook without guid"));
            if (notebook.name.isSet()) {
                error.details() = notebook.name.ref();
            }

            QNWARNING(error << QStringLiteral(", notebook: ") << notebook);
            Q_EMIT failure(error);
            return false;
        }

        m_linkedNotebookGuidsByNotebookGuids[notebook.guid.ref()] = linkedNotebookGuid;
    }

    return true;
}

template <>
void RemoteToLocalSynchronizationManager::unmapContainerElementsFromLinkedNotebookGuid<qevercloud::Tag>(const QList<QString> & tagGuids)
{
    typedef QList<QString>::const_iterator CIter;
    CIter tagGuidsEnd = tagGuids.end();
    for(CIter it = tagGuids.begin(); it != tagGuidsEnd; ++it)
    {
        const QString & guid = *it;

        auto mapIt = m_linkedNotebookGuidsByTagGuids.find(guid);
        if (mapIt == m_linkedNotebookGuidsByTagGuids.end()) {
            continue;
        }

        Q_UNUSED(m_linkedNotebookGuidsByTagGuids.erase(mapIt));
    }
}

template <>
void RemoteToLocalSynchronizationManager::unmapContainerElementsFromLinkedNotebookGuid<qevercloud::Notebook>(const QList<QString> & notebookGuids)
{
    typedef QList<QString>::const_iterator CIter;
    CIter notebookGuidsEnd = notebookGuids.end();
    for(CIter it = notebookGuids.begin(); it != notebookGuidsEnd; ++it)
    {
        const QString & guid = *it;

        auto mapIt = m_linkedNotebookGuidsByNotebookGuids.find(guid);
        if (mapIt == m_linkedNotebookGuidsByNotebookGuids.end()) {
            continue;
        }

        Q_UNUSED(m_linkedNotebookGuidsByNotebookGuids.erase(mapIt));
    }
}

void RemoteToLocalSynchronizationManager::startLinkedNotebooksSync()
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::startLinkedNotebooksSync"));

    if (!m_allLinkedNotebooksListed) {
        requestAllLinkedNotebooks();
        return;
    }

    const int numAllLinkedNotebooks = m_allLinkedNotebooks.size();
    if (numAllLinkedNotebooks == 0) {
        QNDEBUG(QStringLiteral("No linked notebooks are present within the account, can finish the synchronization right away"));
        m_linkedNotebooksSyncChunksDownloaded = true;
        finalize();
        return;
    }

    if (!checkAndRequestAuthenticationTokensForLinkedNotebooks()) {
        return;
    }

    if (!downloadLinkedNotebooksSyncChunks()) {
        return;
    }

    launchLinkedNotebooksContentsSync();
}

bool RemoteToLocalSynchronizationManager::checkAndRequestAuthenticationTokensForLinkedNotebooks()
{
    const int numAllLinkedNotebooks = m_allLinkedNotebooks.size();
    for(int i = 0; i < numAllLinkedNotebooks; ++i)
    {
        const LinkedNotebook & linkedNotebook = m_allLinkedNotebooks[i];
        if (!linkedNotebook.hasGuid())
        {
            ErrorString error(QT_TR_NOOP("Internal error: found a linked notebook without guid"));
            if (linkedNotebook.hasUsername()) {
                error.details() = linkedNotebook.username();
            }

            QNWARNING(error << QStringLiteral(", linked notebook: ") << linkedNotebook);
            Q_EMIT failure(error);
            return false;
        }

        if (!m_authenticationTokensAndShardIdsByLinkedNotebookGuid.contains(linkedNotebook.guid())) {
            QNDEBUG(QStringLiteral("Authentication token for linked notebook with guid ") << linkedNotebook.guid()
                    << QStringLiteral(" was not found; will request authentication tokens for all linked notebooks at once"));
            requestAuthenticationTokensForAllLinkedNotebooks();
            return false;
        }

        auto it = m_authenticationTokenExpirationTimesByLinkedNotebookGuid.find(linkedNotebook.guid());
        if (it == m_authenticationTokenExpirationTimesByLinkedNotebookGuid.end())
        {
            ErrorString error(QT_TR_NOOP("Can't find the cached expiration time of linked notebook's authentication token"));
            if (linkedNotebook.hasUsername()) {
                error.details() = linkedNotebook.username();
            }

            QNWARNING(error << QStringLiteral(", linked notebook: ") << linkedNotebook);
            Q_EMIT failure(error);
            return false;
        }

        const qevercloud::Timestamp & expirationTime = it.value();
        const qevercloud::Timestamp currentTime = QDateTime::currentMSecsSinceEpoch();
        if ((expirationTime - currentTime) < HALF_AN_HOUR_IN_MSEC) {
            QNDEBUG(QStringLiteral("Authentication token for linked notebook with guid ") << linkedNotebook.guid()
                    << QStringLiteral(" is too close to expiration: its expiration time is ")
                    << printableDateTimeFromTimestamp(expirationTime) << QStringLiteral(", current time is ")
                    << printableDateTimeFromTimestamp(currentTime)
                    << QStringLiteral("; will request new authentication tokens for all linked notebooks"));

            requestAuthenticationTokensForAllLinkedNotebooks();
            return false;
        }
    }

    QNDEBUG(QStringLiteral("Got authentication tokens for all linked notebooks, can proceed with their synchronization"));

    return true;
}

void RemoteToLocalSynchronizationManager::requestAuthenticationTokensForAllLinkedNotebooks()
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::requestAuthenticationTokensForAllLinkedNotebooks"));

    QVector<LinkedNotebookAuthData> linkedNotebookAuthData;
    const int numAllLinkedNotebooks = m_allLinkedNotebooks.size();
    linkedNotebookAuthData.reserve(numAllLinkedNotebooks);

    for(int j = 0; j < numAllLinkedNotebooks; ++j)
    {
        const LinkedNotebook & currentLinkedNotebook = m_allLinkedNotebooks[j];

        if (!currentLinkedNotebook.hasGuid())
        {
            ErrorString error(QT_TR_NOOP("Internal error: found linked notebook without guid"));
            if (currentLinkedNotebook.hasUsername()) {
                error.details() = currentLinkedNotebook.username();
            }

            QNWARNING(error << QStringLiteral(", linked notebook: ") << currentLinkedNotebook);
            Q_EMIT failure(error);
            return;
        }

        if (!currentLinkedNotebook.hasShardId())
        {
            ErrorString error(QT_TR_NOOP("Internal error: found linked notebook without shard id"));
            if (currentLinkedNotebook.hasUsername()) {
                error.details() = currentLinkedNotebook.username();
            }

            QNWARNING(error << QStringLiteral(", linked notebook: ") << currentLinkedNotebook);
            Q_EMIT failure(error);
            return;
        }

        if (!currentLinkedNotebook.hasSharedNotebookGlobalId() && !currentLinkedNotebook.hasUri())
        {
            ErrorString error(QT_TR_NOOP("Internal error: found linked notebook without either shared notebook global id or uri"));
            if (currentLinkedNotebook.hasUsername()) {
                error.details() = currentLinkedNotebook.username();
            }

            QNWARNING(error << QStringLiteral(", linked notebook: ") << currentLinkedNotebook);
            Q_EMIT failure(error);
            return;
        }

        if (!currentLinkedNotebook.hasNoteStoreUrl())
        {
            ErrorString error(QT_TR_NOOP("Internal error: found linked notebook without note store URL"));
            if (currentLinkedNotebook.hasUsername()) {
                error.details() = currentLinkedNotebook.username();
            }

            QNWARNING(error << QStringLiteral(", linked notebook: ") << currentLinkedNotebook);
            Q_EMIT failure(error);
            return;
        }

        linkedNotebookAuthData << LinkedNotebookAuthData(currentLinkedNotebook.guid(), currentLinkedNotebook.shardId(),
                                                         (currentLinkedNotebook.hasSharedNotebookGlobalId()
                                                          ? currentLinkedNotebook.sharedNotebookGlobalId()
                                                          : QString()),
                                                         (currentLinkedNotebook.hasUri()
                                                          ? currentLinkedNotebook.uri()
                                                          : QString()),
                                                         currentLinkedNotebook.noteStoreUrl());
    }

    m_pendingAuthenticationTokensForLinkedNotebooks = true;
    Q_EMIT requestAuthenticationTokensForLinkedNotebooks(linkedNotebookAuthData);
}

void RemoteToLocalSynchronizationManager::requestAllLinkedNotebooks()
{
    QNDEBUG("RemoteToLocalSynchronizationManager::requestAllLinkedNotebooks");

    size_t limit = 0, offset = 0;
    LocalStorageManager::ListLinkedNotebooksOrder::type order = LocalStorageManager::ListLinkedNotebooksOrder::NoOrder;
    LocalStorageManager::OrderDirection::type orderDirection = LocalStorageManager::OrderDirection::Ascending;

    m_listAllLinkedNotebooksRequestId = QUuid::createUuid();
    QNTRACE(QStringLiteral("Emitting the request to list linked notebooks: request id = ") << m_listAllLinkedNotebooksRequestId);
    Q_EMIT listAllLinkedNotebooks(limit, offset, order, orderDirection, m_listAllLinkedNotebooksRequestId);
}

void RemoteToLocalSynchronizationManager::getLinkedNotebookSyncState(const LinkedNotebook & linkedNotebook,
                                                                     const QString & authToken, qevercloud::SyncState & syncState,
                                                                     bool & asyncWait, bool & error)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::getLinkedNotebookSyncState"));

    asyncWait = false;
    error = false;
    ErrorString errorDescription;

    if (Q_UNLIKELY(!linkedNotebook.hasGuid())) {
        errorDescription.setBase(QT_TR_NOOP("Linked notebook has no guid"));
        Q_EMIT failure(errorDescription);
        error = true;
        return;
    }

    NoteStore * pNoteStore = m_manager.noteStoreForLinkedNotebook(linkedNotebook);
    if (Q_UNLIKELY(!pNoteStore)) {
        errorDescription.setBase(QT_TR_NOOP("Can't find or create note store for the linked notebook"));
        Q_EMIT failure(errorDescription);
        error = true;
        return;
    }

    if (Q_UNLIKELY(pNoteStore->noteStoreUrl().isEmpty())) {
        errorDescription.setBase(QT_TR_NOOP("Internal error: empty note store url for the linked notebook's note store"));
        Q_EMIT failure(errorDescription);
        error = true;
        return;
    }

    qint32 rateLimitSeconds = 0;
    qint32 errorCode = pNoteStore->getLinkedNotebookSyncState(linkedNotebook.qevercloudLinkedNotebook(), authToken,
                                                              syncState, errorDescription, rateLimitSeconds);
    if (errorCode == qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED)
    {
        if (rateLimitSeconds <= 0) {
            errorDescription.setBase(QT_TR_NOOP("Rate limit reached but the number of seconds to wait is incorrect"));
            errorDescription.details() = QString::number(rateLimitSeconds);
            Q_EMIT failure(errorDescription);
            error = true;
            return;
        }

        int timerId = startTimer(SEC_TO_MSEC(rateLimitSeconds));
        if (Q_UNLIKELY(timerId == 0)) {
            ErrorString errorMessage(QT_TR_NOOP("Failed to start a timer to postpone the Evernote API call "
                                                "due to rate limit exceeding"));
            errorMessage.additionalBases().append(errorDescription.base());
            errorMessage.additionalBases().append(errorDescription.additionalBases());
            errorMessage.details() = errorDescription.details();
            Q_EMIT failure(errorMessage);
            error = true;
            return;
        }

        m_getLinkedNotebookSyncStateBeforeStartAPICallPostponeTimerId = timerId;

        QNDEBUG(QStringLiteral("Rate limit exceeded, need to wait for ") << rateLimitSeconds << QStringLiteral(" seconds"));
        Q_EMIT rateLimitExceeded(rateLimitSeconds);
        asyncWait = true;
        return;
    }
    else if (errorCode == qevercloud::EDAMErrorCode::AUTH_EXPIRED)
    {
        ErrorString errorMessage(QT_TR_NOOP("Unexpected AUTH_EXPIRED error when trying to get the linked notebook sync state"));
        errorMessage.additionalBases().append(errorDescription.base());
        errorMessage.additionalBases().append(errorDescription.additionalBases());
        errorMessage.details() = errorDescription.details();
        Q_EMIT failure(errorMessage);
        error = true;
        return;
    }
    else if (errorCode != 0)
    {
        ErrorString errorMessage(QT_TR_NOOP("Failed to get the linked notebook sync state"));
        errorMessage.additionalBases().append(errorDescription.base());
        errorMessage.additionalBases().append(errorDescription.additionalBases());
        errorMessage.details() = errorDescription.details();
        Q_EMIT failure(errorMessage);
        error = true;
        return;
    }
}

bool RemoteToLocalSynchronizationManager::downloadLinkedNotebooksSyncChunks()
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::downloadLinkedNotebooksSyncChunks"));

    qevercloud::SyncChunk * pSyncChunk = Q_NULLPTR;

    const int numAllLinkedNotebooks = m_allLinkedNotebooks.size();
    for(int i = 0; i < numAllLinkedNotebooks; ++i)
    {
        const LinkedNotebook & linkedNotebook = m_allLinkedNotebooks[i];
        if (!linkedNotebook.hasGuid())
        {
            ErrorString error(QT_TR_NOOP("Internal error: found linked notebook without guid when "
                                         "trying to download the linked notebook sync chunks"));
            if (linkedNotebook.hasUsername()) {
                error.details() = linkedNotebook.username();
            }

            QNWARNING(error << QStringLiteral(": ") << linkedNotebook);
            Q_EMIT failure(error);
            return false;
        }

        const QString & linkedNotebookGuid = linkedNotebook.guid();

        bool fullSyncOnly = false;

        auto lastSyncTimeIt = m_lastSyncTimeByLinkedNotebookGuid.find(linkedNotebookGuid);
        if (lastSyncTimeIt == m_lastSyncTimeByLinkedNotebookGuid.end()) {
            lastSyncTimeIt = m_lastSyncTimeByLinkedNotebookGuid.insert(linkedNotebookGuid, 0);
        }
        qevercloud::Timestamp lastSyncTime = lastSyncTimeIt.value();

        auto lastUpdateCountIt = m_lastUpdateCountByLinkedNotebookGuid.find(linkedNotebookGuid);
        if (lastUpdateCountIt == m_lastUpdateCountByLinkedNotebookGuid.end()) {
            lastUpdateCountIt = m_lastUpdateCountByLinkedNotebookGuid.insert(linkedNotebookGuid, 0);
        }
        qint32 lastUpdateCount = lastUpdateCountIt.value();

        auto syncChunksDownloadedFlagIt = m_linkedNotebookGuidsForWhichSyncChunksWereDownloaded.find(linkedNotebookGuid);
        if (syncChunksDownloadedFlagIt != m_linkedNotebookGuidsForWhichSyncChunksWereDownloaded.end()) {
            QNDEBUG(QStringLiteral("Sync chunks were already downloaded for linked notebook with guid ") << linkedNotebookGuid);
            continue;
        }

        qint32 afterUsn = lastUpdateCount;

        if (m_onceSyncDone || (afterUsn != 0))
        {
            auto syncStateIter = m_syncStatesByLinkedNotebookGuid.find(linkedNotebookGuid);
            if (syncStateIter == m_syncStatesByLinkedNotebookGuid.end())
            {
                QNTRACE(QStringLiteral("Found no cached sync state for linked notebook guid ") << linkedNotebookGuid
                        << QStringLiteral(", will try to receive it from the remote service"));

                qevercloud::SyncState syncState;
                bool error = false;
                bool asyncWait = false;
                getLinkedNotebookSyncState(linkedNotebook, m_authenticationToken, syncState, asyncWait, error);
                if (asyncWait || error) {
                    QNTRACE(QStringLiteral("Async wait = ") << (asyncWait ? QStringLiteral("true") : QStringLiteral("false"))
                            << QStringLiteral(", error = ") << (error ? QStringLiteral("true") : QStringLiteral("false")));
                    return false;
                }

                syncStateIter = m_syncStatesByLinkedNotebookGuid.insert(linkedNotebookGuid, syncState);
            }

            const qevercloud::SyncState & syncState = syncStateIter.value();
            QNDEBUG(QStringLiteral("Sync state: ") << syncState
                    << QStringLiteral("\nLast sync time = ") << printableDateTimeFromTimestamp(lastSyncTime)
                    << QStringLiteral(", last update count = ") << lastUpdateCount);

            if (syncState.fullSyncBefore > lastSyncTime)
            {
                QNDEBUG(QStringLiteral("Linked notebook sync state says the time has come to do the full sync"));
                afterUsn = 0;
                fullSyncOnly = true;
            }
            else if (syncState.updateCount == lastUpdateCount)
            {
                QNDEBUG(QStringLiteral("Server has no updates for data in this linked notebook, continuing with the next one"));
                Q_UNUSED(m_linkedNotebookGuidsForWhichSyncChunksWereDownloaded.insert(linkedNotebookGuid));
                continue;
            }
        }

        NoteStore * pNoteStore = m_manager.noteStoreForLinkedNotebook(linkedNotebook);
        if (Q_UNLIKELY(!pNoteStore)) {
            ErrorString error(QT_TR_NOOP("Can't find or create note store for the linked notebook"));
            Q_EMIT failure(error);
            return false;
        }

        if (Q_UNLIKELY(pNoteStore->noteStoreUrl().isEmpty())) {
            ErrorString errorDescription(QT_TR_NOOP("Internal error: empty note store url for the linked notebook's note store"));
            Q_EMIT failure(errorDescription);
            return false;
        }

        while(!pSyncChunk || (pSyncChunk->chunkHighUSN < pSyncChunk->updateCount))
        {
            if (pSyncChunk) {
                afterUsn = pSyncChunk->chunkHighUSN;
                QNTRACE(QStringLiteral("Updated afterUSN for linked notebook to sync chunk's high USN: ")
                        << pSyncChunk->chunkHighUSN);
            }

            m_linkedNotebookSyncChunks.push_back(qevercloud::SyncChunk());
            pSyncChunk = &(m_linkedNotebookSyncChunks.back());

            ErrorString errorDescription;
            qint32 rateLimitSeconds = 0;
            qint32 errorCode = pNoteStore->getLinkedNotebookSyncChunk(linkedNotebook.qevercloudLinkedNotebook(),
                                                                      afterUsn, m_maxSyncChunksPerOneDownload,
                                                                      m_authenticationToken, fullSyncOnly, *pSyncChunk,
                                                                      errorDescription, rateLimitSeconds);
            if (errorCode == qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED)
            {
                if (rateLimitSeconds <= 0) {
                    errorDescription.setBase(QT_TR_NOOP("Rate limit reached but the number of seconds to wait is incorrect"));
                    errorDescription.details() = QString::number(rateLimitSeconds);
                    Q_EMIT failure(errorDescription);
                    return false;
                }

                m_linkedNotebookSyncChunks.pop_back();

                int timerId = startTimer(SEC_TO_MSEC(rateLimitSeconds));
                if (Q_UNLIKELY(timerId == 0)) {
                    ErrorString errorMessage(QT_TR_NOOP("Failed to start a timer to postpone the Evernote API call "
                                                        "due to rate limit exceeding"));
                    errorMessage.additionalBases().append(errorDescription.base());
                    errorMessage.additionalBases().append(errorDescription.additionalBases());
                    errorMessage.details() = errorDescription.details();
                    Q_EMIT failure(errorMessage);
                    return false;
                }

                m_downloadLinkedNotebookSyncChunkAPICallPostponeTimerId = timerId;

                QNDEBUG(QStringLiteral("Rate limit exceeded, need to wait for ") << rateLimitSeconds << QStringLiteral(" seconds"));
                Q_EMIT rateLimitExceeded(rateLimitSeconds);
                return false;
            }
            else if (errorCode == qevercloud::EDAMErrorCode::AUTH_EXPIRED)
            {
                ErrorString errorMessage(QT_TR_NOOP("Unexpected AUTH_EXPIRED error when trying to download "
                                                    "the linked notebook sync chunks"));
                errorMessage.additionalBases().append(errorDescription.base());
                errorMessage.additionalBases().append(errorDescription.additionalBases());
                errorMessage.details() = errorDescription.details();
                QNDEBUG(errorMessage);
                Q_EMIT failure(errorMessage);
                return false;
            }
            else if (errorCode != 0)
            {
                ErrorString errorMessage(QT_TR_NOOP("Failed to download the sync chunks for linked notebooks content"));
                errorMessage.additionalBases().append(errorDescription.base());
                errorMessage.additionalBases().append(errorDescription.additionalBases());
                errorMessage.details() = errorDescription.details();
                QNDEBUG(errorMessage);
                Q_EMIT failure(errorMessage);
                return false;
            }

            QNDEBUG(QStringLiteral("Received sync chunk: ") << *pSyncChunk);

            lastSyncTime = std::max(pSyncChunk->currentTime, lastSyncTime);
            lastUpdateCount = std::max(pSyncChunk->updateCount, lastUpdateCount);

            QNTRACE(QStringLiteral("Linked notebook's sync chunk current time: ") << printableDateTimeFromTimestamp(pSyncChunk->currentTime)
                    << QStringLiteral(", last sync time = ") << printableDateTimeFromTimestamp(lastSyncTime)
                    << QStringLiteral(", sync chunk update count = ") << pSyncChunk->updateCount
                    << QStringLiteral(", last update count = ") << lastUpdateCount);

            if (pSyncChunk->tags.isSet())
            {
                bool res = mapContainerElementsWithLinkedNotebookGuid<TagsList>(linkedNotebookGuid, pSyncChunk->tags.ref());
                if (!res) {
                    return false;
                }
            }

            if (pSyncChunk->notebooks.isSet())
            {
                bool res = mapContainerElementsWithLinkedNotebookGuid<NotebooksList>(linkedNotebookGuid, pSyncChunk->notebooks.ref());
                if (!res) {
                    return false;
                }
            }

            if (pSyncChunk->expungedTags.isSet()) {
                unmapContainerElementsFromLinkedNotebookGuid<qevercloud::Tag>(pSyncChunk->expungedTags.ref());
            }

            if (pSyncChunk->expungedNotebooks.isSet()) {
                unmapContainerElementsFromLinkedNotebookGuid<qevercloud::Notebook>(pSyncChunk->expungedNotebooks.ref());
            }
        }

        lastSyncTimeIt.value() = lastSyncTime;
        lastUpdateCountIt.value() = lastUpdateCount;

        Q_UNUSED(m_linkedNotebookGuidsForWhichSyncChunksWereDownloaded.insert(linkedNotebook.guid()));

        if (fullSyncOnly) {
            Q_UNUSED(m_linkedNotebookGuidsForWhichFullSyncWasPerformed.insert(linkedNotebook.guid()))
        }
    }

    QNDEBUG(QStringLiteral("Done. Processing content pointed to by linked notebooks from buffered sync chunks"));

    m_syncStatesByLinkedNotebookGuid.clear();   // don't need this anymore, it only served the purpose of preventing multiple get sync state calls for the same linked notebook

    m_linkedNotebooksSyncChunksDownloaded = true;
    Q_EMIT linkedNotebooksSyncChunksDownloaded();

    return true;
}

void RemoteToLocalSynchronizationManager::launchLinkedNotebooksTagsSync()
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::launchLinkedNotebooksTagsSync"));
    m_pendingTagsSyncStart = false;
    QList<QString> dummyList;
    launchDataElementSync<TagsList, Tag>(ContentSource::LinkedNotebook, QStringLiteral("Tag"), m_tags, dummyList);
}

void RemoteToLocalSynchronizationManager::launchLinkedNotebooksNotebooksSync()
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::launchLinkedNotebooksNotebooksSync"));

    m_pendingNotebooksSyncStart = false;

    QList<QString> dummyList;
    launchDataElementSync<NotebooksList, Notebook>(ContentSource::LinkedNotebook, QStringLiteral("Notebook"), m_notebooks, dummyList);
}

void RemoteToLocalSynchronizationManager::launchLinkedNotebooksNotesSync()
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::launchLinkedNotebooksNotesSync"));
    launchDataElementSync<NotesList, Note>(ContentSource::LinkedNotebook, QStringLiteral("Note"), m_notes, m_expungedNotes);
}

void RemoteToLocalSynchronizationManager::checkServerDataMergeCompletion()
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::checkServerDataMergeCompletion"));

    // Need to check whether we are still waiting for the response from some add or update request
    bool tagsReady = !m_pendingTagsSyncStart && m_tagsPendingProcessing.isEmpty() && m_tagsPendingAddOrUpdate.isEmpty() &&
                     m_findTagByGuidRequestIds.isEmpty() &&
                     m_findTagByNameRequestIds.isEmpty() && m_updateTagRequestIds.isEmpty() &&
                     m_addTagRequestIds.isEmpty();
    if (!tagsReady) {
        QNDEBUG(QStringLiteral("Tags are not ready, pending tags sync start = ")
                << (m_pendingTagsSyncStart ? QStringLiteral("true") : QStringLiteral("false"))
                << QStringLiteral("; there are ") << m_tagsPendingProcessing.size()
                << QStringLiteral(" tags pending processing and/or ") << m_tagsPendingAddOrUpdate.size()
                << QStringLiteral(" tags pending add or update within the local storage: pending response for ")
                << m_updateTagRequestIds.size() << QStringLiteral(" tag update requests and/or ")
                << m_addTagRequestIds.size() << QStringLiteral(" tag add requests and/or ")
                << m_findTagByGuidRequestIds.size() << QStringLiteral(" find tag by guid requests and/or ")
                << m_findTagByNameRequestIds.size() << QStringLiteral(" find tag by name requests"));
        return;
    }

    bool searchesReady = m_savedSearches.isEmpty() && m_savedSearchesPendingAddOrUpdate.isEmpty() &&
                         m_findSavedSearchByGuidRequestIds.isEmpty() && m_findSavedSearchByNameRequestIds.isEmpty() &&
                         m_updateSavedSearchRequestIds.isEmpty() && m_addSavedSearchRequestIds.isEmpty();
    if (!searchesReady) {
        QNDEBUG(QStringLiteral("Saved searches are not ready, there are ") << m_savedSearches.size()
                << QStringLiteral(" saved searches pending processing and/or ") << m_savedSearchesPendingAddOrUpdate.size()
                << QStringLiteral(" saved searches pending add or update within the local storage: pending response for ") << m_updateSavedSearchRequestIds.size()
                << QStringLiteral(" saved search update requests and/or ") << m_addSavedSearchRequestIds.size()
                << QStringLiteral(" saved search add requests and/or ") << m_findSavedSearchByGuidRequestIds.size()
                << QStringLiteral(" find saved search by guid requests and/or ") << m_findSavedSearchByNameRequestIds.size()
                << QStringLiteral(" find saved search by name requests"));
        return;
    }

    bool linkedNotebooksReady = !m_pendingLinkedNotebooksSyncStart &&
                                m_linkedNotebooks.isEmpty() &&
                                m_linkedNotebooksPendingAddOrUpdate.isEmpty() &&
                                m_findLinkedNotebookRequestIds.isEmpty() && m_updateLinkedNotebookRequestIds.isEmpty() &&
                                m_addLinkedNotebookRequestIds.isEmpty();
    if (!linkedNotebooksReady) {
        QNDEBUG(QStringLiteral("Linked notebooks are not ready, pending linked notebooks sync start = ")
                << (m_pendingLinkedNotebooksSyncStart ? QStringLiteral("true") : QStringLiteral("false"))
                << QStringLiteral("; there are ") << m_linkedNotebooks.size()
                << QStringLiteral(" linked notebooks pending processing and/or ") << m_linkedNotebooksPendingAddOrUpdate.size()
                << QStringLiteral(" linked notebooks pending add or update within the local storage: pending response for ")
                << m_updateLinkedNotebookRequestIds.size()
                << QStringLiteral(" linked notebook update requests and/or ") << m_addLinkedNotebookRequestIds.size()
                << QStringLiteral(" linked notebook add requests and/or ") << m_findLinkedNotebookRequestIds.size()
                << QStringLiteral(" find linked notebook requests"));
        return;
    }

    bool notebooksReady = !m_pendingNotebooksSyncStart &&
                          m_notebooks.isEmpty() &&
                          m_notebooksPendingAddOrUpdate.isEmpty() &&
                          m_findNotebookByGuidRequestIds.isEmpty() && m_findNotebookByNameRequestIds.isEmpty() &&
                          m_updateNotebookRequestIds.isEmpty() && m_addNotebookRequestIds.isEmpty();
    if (!notebooksReady) {
        QNDEBUG(QStringLiteral("Notebooks are not ready, pending notebooks sync start = ")
                << (m_pendingNotebooksSyncStart ? QStringLiteral("true") : QStringLiteral("false"))
                << QStringLiteral("; there are ") << m_notebooks.size()
                << QStringLiteral(" notebooks pending processing and/or ") << m_notebooksPendingAddOrUpdate.size()
                << QStringLiteral(" notebooks pending add or update within the local storage: pending response for ")
                << m_updateNotebookRequestIds.size()
                << QStringLiteral(" notebook update requests and/or ") << m_addNotebookRequestIds.size()
                << QStringLiteral(" notebook add requests and/or ") << m_findNotebookByGuidRequestIds.size()
                << QStringLiteral(" find notebook by guid requests and/or ") << m_findNotebookByNameRequestIds.size()
                << QStringLiteral(" find notebook by name requests"));
        return;
    }

    bool notesReady = m_notes.isEmpty() && m_notesPendingAddOrUpdate.isEmpty() && m_findNoteByGuidRequestIds.isEmpty() &&
                      m_updateNoteRequestIds.isEmpty() && m_addNoteRequestIds.isEmpty() &&
                      m_guidsOfNotesPendingDownloadForAddingToLocalStorage.isEmpty() &&
                      m_notesPendingDownloadForUpdatingInLocalStorageByGuid.isEmpty() &&
                      m_notesToAddPerAPICallPostponeTimerId.isEmpty() && m_notesToUpdatePerAPICallPostponeTimerId.isEmpty() &&
                      m_resourceGuidsPendingInkNoteImageDownloadPerNoteGuid.isEmpty() &&
                      m_notesPendingInkNoteImagesDownloadByFindNotebookRequestId.isEmpty() &&
                      m_notesPendingThumbnailDownloadByFindNotebookRequestId.isEmpty() &&
                      m_notesPendingThumbnailDownloadByGuid.isEmpty() &&
                      m_updateNoteWithThumbnailRequestIds.isEmpty();
    if (!notesReady)
    {
        QNDEBUG(QStringLiteral("Notes are not ready, there are ") << m_notes.size()
                << QStringLiteral(" notes pending processing and/or ") << m_notesPendingAddOrUpdate.size()
                << QStringLiteral(" notes pending add or update within the local storage: pending response for ") << m_updateNoteRequestIds.size()
                << QStringLiteral(" note update requests and/or ") << m_addNoteRequestIds.size()
                << QStringLiteral(" note add requests and/or ") << m_findNoteByGuidRequestIds.size()
                << QStringLiteral(" find note by guid requests and/or ") << m_guidsOfNotesPendingDownloadForAddingToLocalStorage.size()
                << QStringLiteral(" async full new note data downloads and/or ") << m_notesPendingDownloadForUpdatingInLocalStorageByGuid.size()
                << QStringLiteral(" async full existing note data downloads; also, there are ") << m_notesToAddPerAPICallPostponeTimerId.size()
                << QStringLiteral(" postponed note add requests and/or ") << m_notesToUpdatePerAPICallPostponeTimerId.size()
                << QStringLiteral(" postponed note update requests and/or ") << m_resourceGuidsPendingInkNoteImageDownloadPerNoteGuid.size()
                << QStringLiteral(" note resources pending ink note image download processing and/or ") << m_notesPendingInkNoteImagesDownloadByFindNotebookRequestId.size()
                << QStringLiteral(" find notebook requests for ink note image download processing and/or ") << m_notesPendingThumbnailDownloadByFindNotebookRequestId.size()
                << QStringLiteral(" find notebook requests for note thumbnail download processing and/or ") << m_notesPendingThumbnailDownloadByGuid.size()
                << QStringLiteral(" note thumbnail downloads and/or ") << m_updateNoteWithThumbnailRequestIds.size()
                << QStringLiteral(" update note with downloaded thumbnails requests"));
        return;
    }

    if (m_lastSyncMode == SyncMode::IncrementalSync)
    {
        bool resourcesReady = m_resources.isEmpty() && m_resourcesPendingAddOrUpdate.isEmpty() &&
                              m_findResourceByGuidRequestIds.isEmpty() && m_updateResourceRequestIds.isEmpty() &&
                              m_resourcesByMarkNoteOwningResourceDirtyRequestIds.isEmpty() &&
                              m_addResourceRequestIds.isEmpty() && m_resourcesWithFindRequestIdsPerFindNoteRequestId.isEmpty() &&
                              m_inkNoteResourceDataPerFindNotebookRequestId.isEmpty() &&
                              m_notesOwningResourcesPendingDownloadForAddingToLocalStorageByResourceGuid.isEmpty() &&
                              m_resourcesPendingDownloadForUpdatingInLocalStorageWithNotesByResourceGuid.isEmpty() &&
                              m_resourcesToAddWithNotesPerAPICallPostponeTimerId.isEmpty() &&
                              m_resourcesToUpdateWithNotesPerAPICallPostponeTimerId.isEmpty() &&
                              m_postponedConflictingResourceDataPerAPICallPostponeTimerId.isEmpty();
        if (!resourcesReady)
        {
            QNDEBUG(QStringLiteral("Resources are not ready, there are ") << m_resources.size()
                    << QStringLiteral(" resources pending processing and/or ") << m_resourcesPendingAddOrUpdate.size()
                    << QStringLiteral(" resources pending add or update within the local storage: pending response for ") << m_updateResourceRequestIds.size()
                    << QStringLiteral(" resource update requests and/or ") << m_resourcesByMarkNoteOwningResourceDirtyRequestIds.size()
                    << QStringLiteral(" mark note owning resource as dirty requests and/or ") << m_addResourceRequestIds.size()
                    << QStringLiteral(" resource add requests and/or ") << m_resourcesWithFindRequestIdsPerFindNoteRequestId.size()
                    << QStringLiteral(" find resource by guid requests and/or ") << m_findResourceByGuidRequestIds.size()
                    << QStringLiteral(" resource find note requests and/or ") << m_inkNoteResourceDataPerFindNotebookRequestId.size()
                    << QStringLiteral(" resource find notebook for ink note image download processing and/or ")
                    << m_notesOwningResourcesPendingDownloadForAddingToLocalStorageByResourceGuid.size()
                    << QStringLiteral(" async full new resource data downloads and/or ")
                    << m_resourcesPendingDownloadForUpdatingInLocalStorageWithNotesByResourceGuid.size()
                    << QStringLiteral(" async full existing resource data downloads and/or ") << m_resourcesToAddWithNotesPerAPICallPostponeTimerId.size()
                    << QStringLiteral(" postponed resource add requests and/or ") << m_resourcesToUpdateWithNotesPerAPICallPostponeTimerId.size()
                    << QStringLiteral(" postponed resource update requests and/or ") << m_postponedConflictingResourceDataPerAPICallPostponeTimerId.size()
                    << QStringLiteral(" postponed resource conflict resolutions"));
            return;
        }
    }

    // Also need to check if we are still waiting for some sync conflict resolvers to finish

    QList<NotebookSyncConflictResolver*> notebookSyncConflictResolvers = findChildren<NotebookSyncConflictResolver*>();
    if (!notebookSyncConflictResolvers.isEmpty()) {
        QNDEBUG(QStringLiteral("Still have ") << notebookSyncConflictResolvers.size()
                << QStringLiteral(" pending notebook sync conflict resolutions"));
        return;
    }

    QList<TagSyncConflictResolver*> tagSyncConflictResolvers = findChildren<TagSyncConflictResolver*>();
    if (!tagSyncConflictResolvers.isEmpty()) {
        QNDEBUG(QStringLiteral("Still have ") << tagSyncConflictResolvers.size()
                << QStringLiteral(" pending tag sync conflict resolutions"));
        return;
    }

    QList<SavedSearchSyncConflictResolver*> savedSearchSyncConflictResolvers = findChildren<SavedSearchSyncConflictResolver*>();
    if (!savedSearchSyncConflictResolvers.isEmpty()) {
        QNDEBUG(QStringLiteral("Still have ") << savedSearchSyncConflictResolvers.size()
                << QStringLiteral(" pending saved search sync conflict resolutions"));
        return;
    }

    if (syncingLinkedNotebooksContent())
    {
        QNDEBUG(QStringLiteral("Synchronized the whole contents from linked notebooks"));

        if (!m_expungedNotes.isEmpty()) {
            expungeNotes();
            return;
        }

        if (launchFullSyncStaleDataItemsExpungersForLinkedNotebooks()) {
            return;
        }

        launchExpungingOfNotelessTagsFromLinkedNotebooks();
    }
    else
    {
        QNDEBUG(QStringLiteral("Synchronized the whole contents from user's account"));

        m_fullNoteContentsDownloaded = true;

        Q_EMIT synchronizedContentFromUsersOwnAccount(m_lastUpdateCount, m_lastSyncTime);

        if (m_lastSyncMode == SyncMode::FullSync)
        {
            if (m_onceSyncDone) {
                QNDEBUG(QStringLiteral("Performed full sync even though it has been performed at some moment in the past; "
                                       "need to check for stale data items left within the local storage and expunge them"));
                launchFullSyncStaleDataItemsExpunger();
                return;
            }

            m_expungedFromServerToClient = true;
        }

        if (m_expungedFromServerToClient) {
            startLinkedNotebooksSync();
            return;
        }

        expungeFromServerToClient();
    }
}

void RemoteToLocalSynchronizationManager::finalize()
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::finalize: last update count = ")
            << m_lastUpdateCount << QStringLiteral(", last sync time = ") << printableDateTimeFromTimestamp(m_lastSyncTime));

    if (QuentierIsLogLevelActive(LogLevel::TraceLevel))
    {
        QNTRACE(QStringLiteral("Last update counts by linked notebook guids: "));
        for(auto it = m_lastUpdateCountByLinkedNotebookGuid.constBegin(),
            end = m_lastUpdateCountByLinkedNotebookGuid.constEnd(); it != end; ++it)
        {
            QNTRACE(QStringLiteral("guid = ") << it.key() << QStringLiteral(", last update count = ") << it.value());
        }

        QNTRACE(QStringLiteral("Last sync times by linked notebook guids: "));
        for(auto it = m_lastSyncTimeByLinkedNotebookGuid.constBegin(),
            end = m_lastSyncTimeByLinkedNotebookGuid.constEnd(); it != end; ++it)
        {
            QNTRACE(QStringLiteral("guid = ") << it.key() << QStringLiteral(", last sync time = ") << printableDateTimeFromTimestamp(it.value()));
        }
    }

    m_onceSyncDone = true;
    Q_EMIT finished(m_lastUpdateCount, m_lastSyncTime, m_lastUpdateCountByLinkedNotebookGuid, m_lastSyncTimeByLinkedNotebookGuid);
    clear();
    disconnectFromLocalStorage();
    m_active = false;
}

void RemoteToLocalSynchronizationManager::clear()
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::clear"));

    disconnectFromLocalStorage();

    // NOTE: not clearing m_host: it can be reused in later syncs

    m_lastUsnOnStart = -1;
    m_lastSyncChunksDownloadedUsn = -1;

    m_syncChunksDownloaded = false;
    m_fullNoteContentsDownloaded = false;
    m_expungedFromServerToClient = false;
    m_linkedNotebooksSyncChunksDownloaded = false;

    m_active = false;

    // NOTE: not clearing m_edamProtocolVersionChecked flag: it can be reused in later syncs

    m_syncChunks.clear();
    m_linkedNotebookSyncChunks.clear();
    m_linkedNotebookGuidsForWhichSyncChunksWereDownloaded.clear();

    // NOTE: not clearing m_accountLimits: it can be reused in later syncs

    m_tags.clear();
    m_tagsPendingProcessing.clear();
    m_tagsPendingAddOrUpdate.clear();
    m_expungedTags.clear();
    m_findTagByNameRequestIds.clear();
    m_findTagByGuidRequestIds.clear();
    m_addTagRequestIds.clear();
    m_updateTagRequestIds.clear();
    m_expungeTagRequestIds.clear();
    m_pendingTagsSyncStart = false;

    QList<TagSyncConflictResolver*> tagSyncConflictResolvers = findChildren<TagSyncConflictResolver*>();
    for(auto it = tagSyncConflictResolvers.begin(), end = tagSyncConflictResolvers.end(); it != end; ++it)
    {
        TagSyncConflictResolver * pResolver = *it;
        if (Q_UNLIKELY(!pResolver)) {
            continue;
        }

        pResolver->disconnect();
        pResolver->setParent(Q_NULLPTR);
        pResolver->deleteLater();
    }

    m_tagSyncCache.clear();

    for(auto it = m_tagSyncCachesByLinkedNotebookGuids.begin(),
        end = m_tagSyncCachesByLinkedNotebookGuids.end(); it != end; ++it)
    {
        TagSyncCache * pCache = *it;
        if (Q_UNLIKELY(!pCache)) {
            continue;
        }

        pCache->disconnect();
        pCache->setParent(Q_NULLPTR);
        pCache->deleteLater();
    }

    m_tagSyncCachesByLinkedNotebookGuids.clear();

    m_linkedNotebookGuidsByTagGuids.clear();
    m_expungeNotelessTagsRequestId = QUuid();

    m_savedSearches.clear();
    m_savedSearchesPendingAddOrUpdate.clear();
    m_expungedSavedSearches.clear();
    m_findSavedSearchByNameRequestIds.clear();
    m_findSavedSearchByGuidRequestIds.clear();
    m_addSavedSearchRequestIds.clear();
    m_updateSavedSearchRequestIds.clear();
    m_expungeSavedSearchRequestIds.clear();

    QList<SavedSearchSyncConflictResolver*> savedSearchSyncConflictResolvers = findChildren<SavedSearchSyncConflictResolver*>();
    for(auto it = savedSearchSyncConflictResolvers.begin(), end = savedSearchSyncConflictResolvers.end(); it != end; ++it)
    {
        SavedSearchSyncConflictResolver * pResolver = *it;
        if (Q_UNLIKELY(!pResolver)) {
            continue;
        }

        pResolver->disconnect();
        pResolver->setParent(Q_NULLPTR);
        pResolver->deleteLater();
    }

    m_savedSearchSyncCache.clear();

    m_linkedNotebooks.clear();
    m_linkedNotebooksPendingAddOrUpdate.clear();
    m_expungedLinkedNotebooks.clear();
    m_findLinkedNotebookRequestIds.clear();
    m_addLinkedNotebookRequestIds.clear();
    m_updateLinkedNotebookRequestIds.clear();
    m_expungeLinkedNotebookRequestIds.clear();
    m_pendingLinkedNotebooksSyncStart = false;

    m_allLinkedNotebooks.clear();
    m_listAllLinkedNotebooksRequestId = QUuid();
    m_allLinkedNotebooksListed = false;

    // NOTE: not clearing authentication token, shard id + auth token's expiration time:
    // this information can be reused in later syncs

    m_pendingAuthenticationTokenAndShardId = false;

    // NOTE: not clearing m_user: this information can be reused in subsequent syncs

    m_findUserRequestId = QUuid();
    m_addOrUpdateUserRequestId = QUuid();
    m_onceAddedOrUpdatedUserInLocalStorage = false;

    // NOTE: not clearing auth tokens, shard ids and auth tokens' expiration times for linked notebooks:
    // this information can be reused in later syncs

    m_pendingAuthenticationTokensForLinkedNotebooks = false;

    m_syncStatesByLinkedNotebookGuid.clear();

    // NOTE: not clearing last synchronized USNs, sync times and update counts by linked notebook guid:
    // this information can be reused in subsequent syncs

    m_notebooks.clear();
    m_notebooksPendingAddOrUpdate.clear();
    m_expungedNotebooks.clear();
    m_findNotebookByNameRequestIds.clear();
    m_findNotebookByGuidRequestIds.clear();
    m_addNotebookRequestIds.clear();
    m_updateNotebookRequestIds.clear();
    m_expungeNotebookRequestIds.clear();
    m_pendingNotebooksSyncStart = false;

    QList<NotebookSyncConflictResolver*> notebookSyncConflictResolvers = findChildren<NotebookSyncConflictResolver*>();
    for(auto it = notebookSyncConflictResolvers.begin(), end = notebookSyncConflictResolvers.end(); it != end; ++it)
    {
        NotebookSyncConflictResolver * pResolver = *it;
        if (Q_UNLIKELY(!pResolver)) {
            continue;
        }

        pResolver->disconnect();
        pResolver->setParent(Q_NULLPTR);
        pResolver->deleteLater();
    }

    m_notebookSyncCache.clear();

    for(auto it = m_notebookSyncCachesByLinkedNotebookGuids.begin(),
        end = m_notebookSyncCachesByLinkedNotebookGuids.end(); it != end; ++it)
    {
        NotebookSyncCache * pCache = *it;
        if (Q_UNLIKELY(!pCache)) {
            continue;
        }

        pCache->disconnect();
        pCache->setParent(Q_NULLPTR);
        pCache->deleteLater();
    }

    m_notebookSyncCachesByLinkedNotebookGuids.clear();

    m_linkedNotebookGuidsByNotebookGuids.clear();

    m_notes.clear();
    m_originalNumberOfNotes = 0;
    m_numNotesDownloaded = 0;
    m_expungedNotes.clear();
    m_findNoteByGuidRequestIds.clear();
    m_addNoteRequestIds.clear();
    m_updateNoteRequestIds.clear();
    m_expungeNoteRequestIds.clear();
    m_guidsOfProcessedNonExpungedNotes.clear();

    m_notesWithFindRequestIdsPerFindNotebookRequestId.clear();
    m_notebooksPerNoteIds.clear();

    m_resources.clear();
    m_resourcesPendingAddOrUpdate.clear();
    m_originalNumberOfResources = 0;
    m_numResourcesDownloaded = 0;
    m_findResourceByGuidRequestIds.clear();
    m_addResourceRequestIds.clear();
    m_updateResourceRequestIds.clear();
    m_resourcesByMarkNoteOwningResourceDirtyRequestIds.clear();

    m_resourcesWithFindRequestIdsPerFindNoteRequestId.clear();
    m_inkNoteResourceDataPerFindNotebookRequestId.clear();
    m_resourceGuidsPendingInkNoteImageDownloadPerNoteGuid.clear();

    m_notesPendingInkNoteImagesDownloadByFindNotebookRequestId.clear();
    m_notesPendingThumbnailDownloadByFindNotebookRequestId.clear();
    m_notesPendingThumbnailDownloadByGuid.clear();
    m_updateNoteWithThumbnailRequestIds.clear();

    m_resourceFoundFlagPerFindResourceRequestId.clear();

    m_localUidsOfElementsAlreadyAttemptedToFindByName.clear();

    m_guidsOfNotesPendingDownloadForAddingToLocalStorage.clear();
    m_notesPendingDownloadForUpdatingInLocalStorageByGuid.clear();

    m_notesOwningResourcesPendingDownloadForAddingToLocalStorageByResourceGuid.clear();
    m_resourcesPendingDownloadForUpdatingInLocalStorageWithNotesByResourceGuid.clear();

    m_fullSyncStaleDataItemsSyncedGuids.m_syncedNotebookGuids.clear();
    m_fullSyncStaleDataItemsSyncedGuids.m_syncedTagGuids.clear();
    m_fullSyncStaleDataItemsSyncedGuids.m_syncedNoteGuids.clear();
    m_fullSyncStaleDataItemsSyncedGuids.m_syncedSavedSearchGuids.clear();

    if (m_pFullSyncStaleDataItemsExpunger) {
        junkFullSyncStaleDataItemsExpunger(*m_pFullSyncStaleDataItemsExpunger);
        m_pFullSyncStaleDataItemsExpunger = Q_NULLPTR;
    }

    for(auto it = m_fullSyncStaleDataItemsExpungersByLinkedNotebookGuid.begin(),
        end = m_fullSyncStaleDataItemsExpungersByLinkedNotebookGuid.end(); it != end; ++it)
    {
        FullSyncStaleDataItemsExpunger * pExpunger = it.value();
        if (pExpunger) {
            junkFullSyncStaleDataItemsExpunger(*pExpunger);
        }
    }

    m_fullSyncStaleDataItemsExpungersByLinkedNotebookGuid.clear();

    auto notesToAddPerAPICallPostponeTimerIdEnd = m_notesToAddPerAPICallPostponeTimerId.end();
    for(auto it = m_notesToAddPerAPICallPostponeTimerId.begin(); it != notesToAddPerAPICallPostponeTimerIdEnd; ++it) {
        int key = it.key();
        killTimer(key);
    }
    m_notesToAddPerAPICallPostponeTimerId.clear();

    auto notesToUpdateAndToAddLaterPerAPICallPostponeTimerIdEnd = m_notesToUpdatePerAPICallPostponeTimerId.end();
    for(auto it = m_notesToUpdatePerAPICallPostponeTimerId.begin(); it != notesToUpdateAndToAddLaterPerAPICallPostponeTimerIdEnd; ++it) {
        int key = it.key();
        killTimer(key);
    }
    m_notesToUpdatePerAPICallPostponeTimerId.clear();

    auto resourcesToAddWithNotesPerAPICallPostponeTimerIdEnd = m_resourcesToAddWithNotesPerAPICallPostponeTimerId.end();
    for(auto it = m_resourcesToAddWithNotesPerAPICallPostponeTimerId.begin();
        it != resourcesToAddWithNotesPerAPICallPostponeTimerIdEnd; ++it)
    {
        int key = it.key();
        killTimer(key);
    }
    m_resourcesToAddWithNotesPerAPICallPostponeTimerId.clear();

    auto resourcesToUpdateWithNotesPerAPICallPostponeTimerIdEnd = m_resourcesToUpdateWithNotesPerAPICallPostponeTimerId.end();
    for(auto it = m_resourcesToUpdateWithNotesPerAPICallPostponeTimerId.begin();
        it != resourcesToUpdateWithNotesPerAPICallPostponeTimerIdEnd; ++it)
    {
        int key = it.key();
        killTimer(key);
    }
    m_resourcesToUpdateWithNotesPerAPICallPostponeTimerId.clear();

    auto postponedConflictingResourceDataPerAPICallPostponeTimerIdEnd = m_postponedConflictingResourceDataPerAPICallPostponeTimerId.end();
    for(auto it = m_postponedConflictingResourceDataPerAPICallPostponeTimerId.begin();
        it != postponedConflictingResourceDataPerAPICallPostponeTimerIdEnd; ++it)
    {
        int key = it.key();
        killTimer(key);
    }
    m_postponedConflictingResourceDataPerAPICallPostponeTimerId.clear();

    auto afterUsnForSyncChunkPerAPICallPostponeTimerIdEnd = m_afterUsnForSyncChunkPerAPICallPostponeTimerId.end();
    for(auto it = m_afterUsnForSyncChunkPerAPICallPostponeTimerId.begin(); it != afterUsnForSyncChunkPerAPICallPostponeTimerIdEnd; ++it) {
        int key = it.key();
        killTimer(key);
    }
    m_afterUsnForSyncChunkPerAPICallPostponeTimerId.clear();

    if (m_getLinkedNotebookSyncStateBeforeStartAPICallPostponeTimerId != 0) {
        killTimer(m_getLinkedNotebookSyncStateBeforeStartAPICallPostponeTimerId);
        m_getLinkedNotebookSyncStateBeforeStartAPICallPostponeTimerId = 0;
    }

    if (m_downloadLinkedNotebookSyncChunkAPICallPostponeTimerId != 0) {
        killTimer(m_downloadLinkedNotebookSyncChunkAPICallPostponeTimerId);
        m_downloadLinkedNotebookSyncChunkAPICallPostponeTimerId = 0;
    }

    if (m_getSyncStateBeforeStartAPICallPostponeTimerId != 0) {
        killTimer(m_getSyncStateBeforeStartAPICallPostponeTimerId);
        m_getSyncStateBeforeStartAPICallPostponeTimerId = 0;
    }

    if (m_syncUserPostponeTimerId != 0) {
        killTimer(m_syncUserPostponeTimerId);
        m_syncUserPostponeTimerId = 0;
    }

    if (m_syncAccountLimitsPostponeTimerId != 0) {
        killTimer(m_syncAccountLimitsPostponeTimerId);
        m_syncAccountLimitsPostponeTimerId = 0;
    }

    // NOTE: not clearing m_gotLastSyncParameters: this information can be reused in subsequent syncs

    QList<NoteThumbnailDownloader*> noteThumbnailDownloaders = findChildren<NoteThumbnailDownloader*>();
    for(auto it = noteThumbnailDownloaders.begin(), end = noteThumbnailDownloaders.end(); it != end; ++it)
    {
        NoteThumbnailDownloader * pDownloader = *it;
        if (Q_UNLIKELY(!pDownloader)) {
            continue;
        }

        QObject::disconnect(pDownloader, QNSIGNAL(NoteThumbnailDownloader,finished,bool,QString,QByteArray,ErrorString),
                            this, QNSLOT(RemoteToLocalSynchronizationManager,onNoteThumbnailDownloadingFinished,bool,QString,QByteArray,ErrorString));
        pDownloader->setParent(Q_NULLPTR);
        pDownloader->deleteLater();
    }

    QList<InkNoteImageDownloader*> inkNoteImagesDownloaders = findChildren<InkNoteImageDownloader*>();
    for(auto it = inkNoteImagesDownloaders.begin(), end = inkNoteImagesDownloaders.end(); it != end; ++it)
    {
        InkNoteImageDownloader * pDownloader = *it;
        if (Q_UNLIKELY(!pDownloader)) {
            continue;
        }

        QObject::disconnect(pDownloader, QNSIGNAL(InkNoteImageDownloader,finished,bool,QString,QString,ErrorString),
                            this, QNSLOT(RemoteToLocalSynchronizationManager,onInkNoteImageDownloadFinished,bool,QString,QString,ErrorString));
        pDownloader->setParent(Q_NULLPTR);
        pDownloader->deleteLater();
    }
}

void RemoteToLocalSynchronizationManager::clearAll()
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::clearAll"));

    clear();

    m_host.clear();
    m_edamProtocolVersionChecked = false;
    m_accountLimits = qevercloud::AccountLimits();

    m_authenticationToken.clear();
    m_shardId.clear();
    m_authenticationTokenExpirationTime = 0;

    m_user.clear();

    m_authenticationTokensAndShardIdsByLinkedNotebookGuid.clear();
    m_authenticationTokenExpirationTimesByLinkedNotebookGuid.clear();

    m_lastSyncTimeByLinkedNotebookGuid.clear();
    m_lastUpdateCountByLinkedNotebookGuid.clear();
    m_linkedNotebookGuidsForWhichFullSyncWasPerformed.clear();
    m_linkedNotebookGuidsOnceFullySynced.clear();

    m_gotLastSyncParameters = false;
}

void RemoteToLocalSynchronizationManager::handleLinkedNotebookAdded(const LinkedNotebook & linkedNotebook)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::handleLinkedNotebookAdded: linked notebook = ") << linkedNotebook);

    unregisterLinkedNotebookPendingAddOrUpdate(linkedNotebook);

    if (!m_allLinkedNotebooksListed) {
        return;
    }

    if (!linkedNotebook.hasGuid()) {
        QNWARNING(QStringLiteral("Detected the addition of linked notebook without guid to local storage!"));
        return;
    }

    auto it = std::find_if(m_allLinkedNotebooks.begin(), m_allLinkedNotebooks.end(),
                           CompareItemByGuid<LinkedNotebook>(linkedNotebook.guid()));
    if (it != m_allLinkedNotebooks.end()) {
        QNINFO(QStringLiteral("Detected the addition of linked notebook to local storage, however such linked notebook is "
                              "already present within the list of all linked notebooks received previously from local storage"));
        *it = linkedNotebook;
        return;
    }

    m_allLinkedNotebooks << linkedNotebook;
}

void RemoteToLocalSynchronizationManager::handleLinkedNotebookUpdated(const LinkedNotebook & linkedNotebook)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::handleLinkedNotebookUpdated: linked notebook = ") << linkedNotebook);

    unregisterLinkedNotebookPendingAddOrUpdate(linkedNotebook);

    if (!m_allLinkedNotebooksListed) {
        return;
    }

    if (!linkedNotebook.hasGuid()) {
        QNWARNING(QStringLiteral("Detected the updated linked notebook without guid in local storage!"));
        return;
    }

    auto it = std::find_if(m_allLinkedNotebooks.begin(), m_allLinkedNotebooks.end(),
                           CompareItemByGuid<LinkedNotebook>(linkedNotebook.guid()));
    if (it == m_allLinkedNotebooks.end()) {
        QNINFO(QStringLiteral("Detected the update of linked notebook to local storage, however such linked notebook is "
                              "not present within the list of all linked notebooks received previously from local storage"));
        m_allLinkedNotebooks << linkedNotebook;
        return;
    }

    *it = linkedNotebook;
}

void RemoteToLocalSynchronizationManager::timerEvent(QTimerEvent * pEvent)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::timerEvent"));

    if (!pEvent) {
        ErrorString errorDescription(QT_TR_NOOP("Qt error: detected null pointer to QTimerEvent"));
        QNWARNING(errorDescription);
        Q_EMIT failure(errorDescription);
        return;
    }

    int timerId = pEvent->timerId();
    killTimer(timerId);
    QNDEBUG(QStringLiteral("Killed timer with id ") << timerId);

    auto noteToAddIt = m_notesToAddPerAPICallPostponeTimerId.find(timerId);
    if (noteToAddIt != m_notesToAddPerAPICallPostponeTimerId.end()) {
        Note note = noteToAddIt.value();
        Q_UNUSED(m_notesToAddPerAPICallPostponeTimerId.erase(noteToAddIt));
        getFullNoteDataAsyncAndAddToLocalStorage(note);
        return;
    }

    auto noteToUpdateIt = m_notesToUpdatePerAPICallPostponeTimerId.find(timerId);
    if (noteToUpdateIt != m_notesToUpdatePerAPICallPostponeTimerId.end()) {
        Note noteToUpdate = noteToUpdateIt.value();
        Q_UNUSED(m_notesToUpdatePerAPICallPostponeTimerId.erase(noteToUpdateIt));
        registerNotePendingAddOrUpdate(noteToUpdate);
        getFullNoteDataAsyncAndUpdateInLocalStorage(noteToUpdate);
        return;
    }

    auto resourceToAddIt = m_resourcesToAddWithNotesPerAPICallPostponeTimerId.find(timerId);
    if (resourceToAddIt != m_resourcesToAddWithNotesPerAPICallPostponeTimerId.end()) {
        std::pair<Resource,Note> pair = resourceToAddIt.value();
        Q_UNUSED(m_resourcesToAddWithNotesPerAPICallPostponeTimerId.erase(resourceToAddIt))
        getFullResourceDataAsyncAndAddToLocalStorage(pair.first, pair.second);
        return;
    }

    auto resourceToUpdateIt = m_resourcesToUpdateWithNotesPerAPICallPostponeTimerId.find(timerId);
    if (resourceToUpdateIt != m_resourcesToUpdateWithNotesPerAPICallPostponeTimerId.end()) {
        std::pair<Resource,Note> pair = resourceToAddIt.value();
        Q_UNUSED(m_resourcesToAddWithNotesPerAPICallPostponeTimerId.erase(resourceToAddIt))
        getFullResourceDataAsyncAndUpdateInLocalStorage(pair.first, pair.second);
        return;
    }

    auto conflictingResourceDataIt = m_postponedConflictingResourceDataPerAPICallPostponeTimerId.find(timerId);
    if (conflictingResourceDataIt != m_postponedConflictingResourceDataPerAPICallPostponeTimerId.end()) {
        PostponedConflictingResourceData data = conflictingResourceDataIt.value();
        Q_UNUSED(m_postponedConflictingResourceDataPerAPICallPostponeTimerId.erase(conflictingResourceDataIt))
        processResourceConflictAsNoteConflict(data.m_remoteNote, data.m_localConflictingNote, data.m_remoteNoteResourceWithoutFullData);
        return;
    }

    auto afterUsnIt = m_afterUsnForSyncChunkPerAPICallPostponeTimerId.find(timerId);
    if (afterUsnIt != m_afterUsnForSyncChunkPerAPICallPostponeTimerId.end()) {
        qint32 afterUsn = afterUsnIt.value();
        Q_UNUSED(m_afterUsnForSyncChunkPerAPICallPostponeTimerId.erase(afterUsnIt));
        downloadSyncChunksAndLaunchSync(afterUsn);
        return;
    }

    if (m_getLinkedNotebookSyncStateBeforeStartAPICallPostponeTimerId == timerId) {
        m_getLinkedNotebookSyncStateBeforeStartAPICallPostponeTimerId = 0;
        startLinkedNotebooksSync();
        return;
    }

    if (m_downloadLinkedNotebookSyncChunkAPICallPostponeTimerId == timerId) {
        m_downloadLinkedNotebookSyncChunkAPICallPostponeTimerId = 0;
        startLinkedNotebooksSync();
        return;
    }

    if (m_getSyncStateBeforeStartAPICallPostponeTimerId == timerId) {
        m_getSyncStateBeforeStartAPICallPostponeTimerId = 0;
        start(m_lastUsnOnStart);
        return;
    }

    if (m_syncUserPostponeTimerId == timerId) {
        m_syncUserPostponeTimerId = 0;
        start(m_lastUsnOnStart);
        return;
    }

    if (m_syncAccountLimitsPostponeTimerId == timerId) {
        m_syncAccountLimitsPostponeTimerId = 0;
        start(m_lastUsnOnStart);
        return;
    }
}

void RemoteToLocalSynchronizationManager::getFullNoteDataAsync(const Note & note)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::getFullNoteDataAsync: ") << note);

    if (!note.hasGuid()) {
        ErrorString errorDescription(QT_TR_NOOP("Detected the attempt to get full note's data for a note without guid"));
        APPEND_NOTE_DETAILS(errorDescription, note)
        QNWARNING(errorDescription << QStringLiteral(": ") << note);
        Q_EMIT failure(errorDescription);
        return;
    }

    if (!note.hasNotebookGuid()) {
        ErrorString errorDescription(QT_TR_NOOP("Detected the attempt to get full note's data for a note without notebook guid"));
        APPEND_NOTE_DETAILS(errorDescription, note)
        QNWARNING(errorDescription << QStringLiteral(": ") << note);
        Q_EMIT failure(errorDescription);
        return;
    }

    QString authToken;
    ErrorString errorDescription;
    NoteStore * pNoteStore = noteStoreForNote(note, authToken, errorDescription);
    if (Q_UNLIKELY(!pNoteStore)) {
        Q_EMIT failure(errorDescription);
        return;
    }

    bool withContent = true;
    bool withResourceData = true;
    bool withResourceRecognition = true;
    bool withResourceAlternateData = true;
    bool withSharedNotes = true;
    bool withNoteAppDataValues = true;
    bool withResourceAppDataValues = true;
    bool withNoteLimits = syncingLinkedNotebooksContent();
    errorDescription.clear();
    bool res = pNoteStore->getNoteAsync(withContent, withResourceData, withResourceRecognition,
                                        withResourceAlternateData, withSharedNotes,
                                        withNoteAppDataValues, withResourceAppDataValues,
                                        withNoteLimits, note.guid(), authToken, errorDescription);
    if (!res) {
        APPEND_NOTE_DETAILS(errorDescription, note)
        QNWARNING(errorDescription << QStringLiteral(", note: ") << note);
        Q_EMIT failure(errorDescription);
    }
}

void RemoteToLocalSynchronizationManager::getFullNoteDataAsyncAndAddToLocalStorage(const Note & note)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::getFullNoteDataAsyncAndAddToLocalStorage: ") << note);

    if (Q_UNLIKELY(!note.hasGuid()))
    {
        ErrorString errorDescription(QT_TR_NOOP("Internal error: the synced note to be added "
                                                "to the local storage has no guid"));
        APPEND_NOTE_DETAILS(errorDescription, note)

        QNWARNING(errorDescription << QStringLiteral(", note: ") << note);
        Q_EMIT failure(errorDescription);
        return;
    }

    QString noteGuid = note.guid();

    auto it = m_guidsOfNotesPendingDownloadForAddingToLocalStorage.find(noteGuid);
    if (Q_UNLIKELY(it != m_guidsOfNotesPendingDownloadForAddingToLocalStorage.end())) {
        QNDEBUG(QStringLiteral("Note with guid ") << noteGuid << QStringLiteral(" is already being downloaded"));
        return;
    }

    QNTRACE(QStringLiteral("Adding note guid into the list of those pending download for adding to the local storage: ")
            << noteGuid);
    Q_UNUSED(m_guidsOfNotesPendingDownloadForAddingToLocalStorage.insert(noteGuid))

    getFullNoteDataAsync(note);
}

void RemoteToLocalSynchronizationManager::getFullNoteDataAsyncAndUpdateInLocalStorage(const Note & note)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::getFullNoteDataAsyncAndUpdateInLocalStorage: ")
            << note);

    if (Q_UNLIKELY(!note.hasGuid()))
    {
        ErrorString errorDescription(QT_TR_NOOP("Internal error: the synced note to be updated "
                                                "in the local storage has no guid"));
        APPEND_NOTE_DETAILS(errorDescription, note)

        QNWARNING(errorDescription << QStringLiteral(", note: ") << note);
        Q_EMIT failure(errorDescription);
        return;
    }

    QString noteGuid = note.guid();

    auto it = m_notesPendingDownloadForUpdatingInLocalStorageByGuid.find(noteGuid);
    if (Q_UNLIKELY(it != m_notesPendingDownloadForUpdatingInLocalStorageByGuid.end())) {
        QNDEBUG(QStringLiteral("Note with guid ") << noteGuid << QStringLiteral(" is already being downloaded"));
        return;
    }

    QNTRACE(QStringLiteral("Adding note guid into the list of those pending download for update in the local storage: ")
            << noteGuid);
    m_notesPendingDownloadForUpdatingInLocalStorageByGuid[noteGuid] = note;

    getFullNoteDataAsync(note);
}

void RemoteToLocalSynchronizationManager::getFullResourceDataAsync(const Resource & resource,
                                                                   const Note & resourceOwningNote)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::getFullResourceDataAsync: resource = ") << resource
            << QStringLiteral("\nResource owning note: ") << resourceOwningNote);

    if (!resource.hasGuid()) {
        ErrorString errorDescription(QT_TR_NOOP("Detected the attempt to get full resource's data for a resource without guid"));
        QNWARNING(errorDescription << QStringLiteral("; resource: ") << resource
                  << QStringLiteral("\nResource owning note: ") << resourceOwningNote);
        Q_EMIT failure(errorDescription);
        return;
    }

    if (!resourceOwningNote.hasNotebookGuid()) {
        ErrorString errorDescription(QT_TR_NOOP("Detected the attempt to get full resource's data for a resource which owning note "
                                                "has no notebook guid set"));
        APPEND_NOTE_DETAILS(errorDescription, resourceOwningNote)
        QNWARNING(errorDescription << QStringLiteral("; resource: ") << resource
                  << QStringLiteral("\nResource owning note: ") << resourceOwningNote);
        Q_EMIT failure(errorDescription);
        return;
    }

    // Need to find out which note store is required - the one for user's own
    // account or the one for the stuff from some linked notebook

    QString authToken;
    NoteStore * pNoteStore = Q_NULLPTR;
    auto linkedNotebookGuidIt = m_linkedNotebookGuidsByNotebookGuids.find(resourceOwningNote.notebookGuid());
    if (linkedNotebookGuidIt == m_linkedNotebookGuidsByNotebookGuids.end())
    {
        QNDEBUG(QStringLiteral("Found no linked notebook corresponding to notebook guid ") << resourceOwningNote.notebookGuid()
                << QStringLiteral(", using the note store for the user's own account"));
        pNoteStore = &(m_manager.noteStore());
        authToken = m_authenticationToken;
    }
    else
    {
        const QString & linkedNotebookGuid = linkedNotebookGuidIt.value();

        auto authTokenIt = m_authenticationTokensAndShardIdsByLinkedNotebookGuid.find(linkedNotebookGuid);
        if (Q_UNLIKELY(authTokenIt == m_authenticationTokensAndShardIdsByLinkedNotebookGuid.end())) {
            ErrorString errorDescription(QT_TR_NOOP("Can't find the authentication token corresponding to the linked notebook"));
            APPEND_NOTE_DETAILS(errorDescription, resourceOwningNote)
            QNWARNING(errorDescription << QStringLiteral("; resource: ") << resource
                      << QStringLiteral("\nResource owning note: ") << resourceOwningNote);
            Q_EMIT failure(errorDescription);
            return;
        }

        authToken = authTokenIt.value().first;
        const QString & linkedNotebookShardId = authTokenIt.value().second;

        QString linkedNotebookNoteStoreUrl;
        for(auto it = m_allLinkedNotebooks.constBegin(), end = m_allLinkedNotebooks.constEnd(); it != end; ++it)
        {
            if (it->hasGuid() && (it->guid() == linkedNotebookGuid) && it->hasNoteStoreUrl()) {
                linkedNotebookNoteStoreUrl = it->noteStoreUrl();
                break;
            }
        }

        if (linkedNotebookNoteStoreUrl.isEmpty()) {
            ErrorString errorDescription(QT_TR_NOOP("Can't find the note store URL corresponding to the linked notebook"));
            APPEND_NOTE_DETAILS(errorDescription, resourceOwningNote)
            QNWARNING(errorDescription << QStringLiteral("; resource: ") << resource
                      << QStringLiteral("\nResource owning note: ") << resourceOwningNote);
            Q_EMIT failure(errorDescription);
            return;
        }

        LinkedNotebook linkedNotebook;
        linkedNotebook.setGuid(linkedNotebookGuid);
        linkedNotebook.setShardId(linkedNotebookShardId);
        linkedNotebook.setNoteStoreUrl(linkedNotebookNoteStoreUrl);
        pNoteStore = m_manager.noteStoreForLinkedNotebook(linkedNotebook);
        if (Q_UNLIKELY(!pNoteStore)) {
            ErrorString errorDescription(QT_TR_NOOP("Can't find or create note store for "));
            APPEND_NOTE_DETAILS(errorDescription, resourceOwningNote)
            QNWARNING(errorDescription << QStringLiteral("; resource: ") << resource
                      << QStringLiteral("\nResource owning note: ") << resourceOwningNote);
            Q_EMIT failure(errorDescription);
            return;
        }

        if (Q_UNLIKELY(pNoteStore->noteStoreUrl().isEmpty())) {
            ErrorString errorDescription(QT_TR_NOOP("Internal error: empty note store url for the linked notebook's note store"));
            APPEND_NOTE_DETAILS(errorDescription, resourceOwningNote)
            QNWARNING(errorDescription << QStringLiteral("; resource: ") << resource
                      << QStringLiteral("\nResource owning note: ") << resourceOwningNote);
            Q_EMIT failure(errorDescription);
            return;
        }

        QObject::connect(pNoteStore, QNSIGNAL(NoteStore,getResourceAsyncFinished,qint32,qevercloud::Resource,qint32,ErrorString),
                         this, QNSLOT(RemoteToLocalSynchronizationManager,onGetResourceAsyncFinished,qint32,qevercloud::Note,qint32,ErrorString),
                         Qt::ConnectionType(Qt::AutoConnection | Qt::UniqueConnection));

        QNDEBUG(QStringLiteral("Using NoteStore corresponding to linked notebook with guid ")
                << linkedNotebookGuid << QStringLiteral(", note store url = ") << pNoteStore->noteStoreUrl());
    }

    ErrorString errorDescription;
    bool res = pNoteStore->getResourceAsync(/* with data body = */ true, /* with recognition data body = */ true,
                                            /* with alternate data body = */ true, /* with attributes = */ true,
                                            resource.guid(), authToken, errorDescription);
    if (!res) {
        APPEND_NOTE_DETAILS(errorDescription, resourceOwningNote);
        QNWARNING(errorDescription << QStringLiteral("; resource: ") << resource
                  << QStringLiteral("\nResource owning note: ") << resourceOwningNote);
        Q_EMIT failure(errorDescription);
    }
}

void RemoteToLocalSynchronizationManager::getFullResourceDataAsyncAndAddToLocalStorage(const Resource & resource,
                                                                                       const Note & resourceOwningNote)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::getFullResourceDataAsyncAndAddToLocalStorage: resource = ")
            << resource << QStringLiteral("\nResource owning note: ") << resourceOwningNote);

    if (Q_UNLIKELY(!resource.hasGuid()))
    {
        ErrorString errorDescription(QT_TR_NOOP("Internal error: the synced resource to be added "
                                                "to the local storage has no guid"));
        QNWARNING(errorDescription << QStringLiteral(", resource: ") << resource
                  << QStringLiteral("\nResource owning note: ") << resourceOwningNote);
        Q_EMIT failure(errorDescription);
        return;
    }

    QString resourceGuid = resource.guid();

    auto it = m_notesOwningResourcesPendingDownloadForAddingToLocalStorageByResourceGuid.find(resourceGuid);
    if (Q_UNLIKELY(it != m_notesOwningResourcesPendingDownloadForAddingToLocalStorageByResourceGuid.end())) {
        QNDEBUG(QStringLiteral("Resource with guid ") << resourceGuid << QStringLiteral(" is already being downloaded"));
        return;
    }

    QNTRACE(QStringLiteral("Adding resource guid into the list of those pending download for adding to the local storage: ")
            << resourceGuid);
    m_notesOwningResourcesPendingDownloadForAddingToLocalStorageByResourceGuid[resourceGuid] = resourceOwningNote;

    getFullResourceDataAsync(resource, resourceOwningNote);
}

void RemoteToLocalSynchronizationManager::getFullResourceDataAsyncAndUpdateInLocalStorage(const Resource & resource,
                                                                                          const Note & resourceOwningNote)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::getFullResourceDataAsyncAndUpdateInLocalStorage: resource = ")
            << resource << QStringLiteral("\nResource owning note: ") << resourceOwningNote);

    if (Q_UNLIKELY(!resource.hasGuid()))
    {
        ErrorString errorDescription(QT_TR_NOOP("Internal error: the synced resource to be updated "
                                                "in the local storage has no guid"));
        QNWARNING(errorDescription << QStringLiteral(", resource: ") << resource
                  << QStringLiteral("\nResource owning note: ") << resourceOwningNote);
        Q_EMIT failure(errorDescription);
        return;
    }

    QString resourceGuid = resource.guid();

    auto it = m_resourcesPendingDownloadForUpdatingInLocalStorageWithNotesByResourceGuid.find(resourceGuid);
    if (Q_UNLIKELY(it != m_resourcesPendingDownloadForUpdatingInLocalStorageWithNotesByResourceGuid.end())) {
        QNDEBUG(QStringLiteral("Resource with guid ") << resourceGuid << QStringLiteral(" is already being downloaded"));
        return;
    }

    QNTRACE(QStringLiteral("Adding resource guid into the list of those pending download for update in the local storage: ")
            << resourceGuid);
    m_resourcesPendingDownloadForUpdatingInLocalStorageWithNotesByResourceGuid[resourceGuid] = std::pair<Resource,Note>(resource, resourceOwningNote);

    getFullResourceDataAsync(resource, resourceOwningNote);
}

void RemoteToLocalSynchronizationManager::downloadSyncChunksAndLaunchSync(qint32 afterUsn)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::downloadSyncChunksAndLaunchSync: after USN = ") << afterUsn);

    NoteStore & noteStore = m_manager.noteStore();
    qevercloud::SyncChunk * pSyncChunk = Q_NULLPTR;

    while(!pSyncChunk || (pSyncChunk->chunkHighUSN < pSyncChunk->updateCount))
    {
        if (pSyncChunk) {
            afterUsn = pSyncChunk->chunkHighUSN;
            QNTRACE(QStringLiteral("Updated after USN to sync chunk's high USN: ") << pSyncChunk->chunkHighUSN);
        }

        m_syncChunks.push_back(qevercloud::SyncChunk());
        pSyncChunk = &(m_syncChunks.back());

        qevercloud::SyncChunkFilter filter;
        filter.includeNotebooks = true;
        filter.includeNotes = true;
        filter.includeTags = true;
        filter.includeSearches = true;
        filter.includeNoteResources = true;
        filter.includeNoteAttributes = true;
        filter.includeNoteApplicationDataFullMap = true;
        filter.includeNoteResourceApplicationDataFullMap = true;
        filter.includeLinkedNotebooks = true;

        if (m_lastSyncMode == SyncMode::IncrementalSync) {
            filter.includeExpunged = true;
            filter.includeResources = true;
        }

        ErrorString errorDescription;
        qint32 rateLimitSeconds = 0;
        qint32 errorCode = noteStore.getSyncChunk(afterUsn, m_maxSyncChunksPerOneDownload, filter,
                                                  *pSyncChunk, errorDescription, rateLimitSeconds);
        if (errorCode == qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED)
        {
            if (rateLimitSeconds <= 0) {
                errorDescription.setBase(QT_TR_NOOP("Rate limit reached but the number of seconds to wait is incorrect"));
                errorDescription.details() = QString::number(rateLimitSeconds);
                QNWARNING(errorDescription);
                Q_EMIT failure(errorDescription);
                return;
            }

            m_syncChunks.pop_back();

            int timerId = startTimer(SEC_TO_MSEC(rateLimitSeconds));
            if (Q_UNLIKELY(timerId == 0)) {
                ErrorString errorDescription(QT_TR_NOOP("Failed to start a timer to postpone the Evernote API call "
                                                        "due to rate limit exceeding"));
                QNWARNING(errorDescription);
                Q_EMIT failure(errorDescription);
                return;
            }

            m_afterUsnForSyncChunkPerAPICallPostponeTimerId[timerId] = afterUsn;
            Q_EMIT rateLimitExceeded(rateLimitSeconds);
            return;
        }
        else if (errorCode == qevercloud::EDAMErrorCode::AUTH_EXPIRED)
        {
            handleAuthExpiration();
            return;
        }
        else if (errorCode != 0)
        {
            ErrorString errorMessage(QT_TR_NOOP("Failed to download the sync chunks"));
            errorMessage.additionalBases().append(errorDescription.base());
            errorMessage.additionalBases().append(errorDescription.additionalBases());
            errorMessage.details() = errorDescription.details();
            Q_EMIT failure(errorMessage);
            return;
        }

        QNDEBUG(QStringLiteral("Received sync chunk: ") << *pSyncChunk);

        m_lastSyncTime = std::max(pSyncChunk->currentTime, m_lastSyncTime);
        m_lastUpdateCount = std::max(pSyncChunk->updateCount, m_lastUpdateCount);

        QNTRACE(QStringLiteral("Sync chunk current time: ") << printableDateTimeFromTimestamp(pSyncChunk->currentTime)
                << QStringLiteral(", last sync time = ") << printableDateTimeFromTimestamp(m_lastSyncTime)
                << QStringLiteral(", sync chunk update count = ") << pSyncChunk->updateCount << QStringLiteral(", last update count = ")
                << m_lastUpdateCount);
    }

    QNDEBUG(QStringLiteral("Done. Processing tags, saved searches, linked notebooks and notebooks from buffered sync chunks"));

    m_lastSyncChunksDownloadedUsn = afterUsn;
    m_syncChunksDownloaded = true;
    Q_EMIT syncChunksDownloaded();

    launchSync();
}

const Notebook * RemoteToLocalSynchronizationManager::getNotebookPerNote(const Note & note) const
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::getNotebookPerNote: note = ") << note);

    QString noteGuid = (note.hasGuid() ? note.guid() : QString());
    QString noteLocalUid = note.localUid();

    QPair<QString,QString> key(noteGuid, noteLocalUid);
    QHash<QPair<QString,QString>,Notebook>::const_iterator cit = m_notebooksPerNoteIds.find(key);
    if (cit == m_notebooksPerNoteIds.end()) {
        return Q_NULLPTR;
    }
    else {
        return &(cit.value());
    }
}

void RemoteToLocalSynchronizationManager::handleAuthExpiration()
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::handleAuthExpiration"));

    if (m_pendingAuthenticationTokenAndShardId) {
        QNDEBUG(QStringLiteral("Already pending the authentication token and shard id"));
        return;
    }

    m_pendingAuthenticationTokenAndShardId = true;
    Q_EMIT requestAuthenticationToken();
}

bool RemoteToLocalSynchronizationManager::checkUserAccountSyncState(bool & asyncWait, bool & error, qint32 & afterUsn)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::checkUserAccountSyncState"));

    asyncWait = false;
    error = false;

    ErrorString errorDescription;
    qint32 rateLimitSeconds = 0;
    qevercloud::SyncState state;
    qint32 errorCode = m_manager.noteStore().getSyncState(state, errorDescription, rateLimitSeconds);
    if (errorCode == qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED)
    {
        if (rateLimitSeconds <= 0) {
            errorDescription.setBase(QT_TR_NOOP("Rate limit reached but the number of seconds to wait is incorrect"));
            errorDescription.details() = QString::number(rateLimitSeconds);
            Q_EMIT failure(errorDescription);
            error = true;
            return false;
        }

        m_getSyncStateBeforeStartAPICallPostponeTimerId = startTimer(SEC_TO_MSEC(rateLimitSeconds));
        if (Q_UNLIKELY(m_getSyncStateBeforeStartAPICallPostponeTimerId == 0)) {
            errorDescription.setBase(QT_TR_NOOP("Failed to start a timer to postpone "
                                                "the Evernote API call due to rate limit exceeding"));
            Q_EMIT failure(errorDescription);
            error = true;
        }
        else {
            asyncWait = true;
        }

        return false;
    }
    else if (errorCode == qevercloud::EDAMErrorCode::AUTH_EXPIRED)
    {
        handleAuthExpiration();
        asyncWait = true;
        return false;
    }
    else if (errorCode != 0)
    {
        Q_EMIT failure(errorDescription);
        error = true;
        return false;
    }

    QNDEBUG(QStringLiteral("Sync state: ") << state
            << QStringLiteral("\nLast sync time = ") << printableDateTimeFromTimestamp(m_lastSyncTime)
            << QStringLiteral("; last update count = ") << m_lastUpdateCount);

    if (state.fullSyncBefore > m_lastSyncTime)
    {
        QNDEBUG(QStringLiteral("Sync state says the time has come to do the full sync"));
        afterUsn = 0;
        m_lastSyncMode = SyncMode::FullSync;
    }
    else if (state.updateCount == m_lastUpdateCount)
    {
        QNDEBUG(QStringLiteral("Server has no updates for user's data since the last sync"));
        return false;
    }

    return true;
}

bool RemoteToLocalSynchronizationManager::checkLinkedNotebooksSyncStates(bool & asyncWait, bool & error)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::checkLinkedNotebooksSyncStates"));

    asyncWait = false;
    error = false;

    if (!m_allLinkedNotebooksListed) {
        QNTRACE(QStringLiteral("The list of all linked notebooks was not obtained from local storage yet, need to wait for it to happen"));
        requestAllLinkedNotebooks();
        asyncWait = true;
        return false;
    }

    if (m_allLinkedNotebooks.isEmpty()) {
        QNTRACE(QStringLiteral("The list of all linked notebooks is empty, nothing to check sync states for"));
        return false;
    }

    if (m_pendingAuthenticationTokensForLinkedNotebooks) {
        QNTRACE(QStringLiteral("Pending authentication tokens for linked notebook flag is set, "
                               "need to wait for auth tokens"));
        asyncWait = true;
        return false;
    }

    const int numAllLinkedNotebooks = m_allLinkedNotebooks.size();
    for(int i = 0; i < numAllLinkedNotebooks; ++i)
    {
        const LinkedNotebook & linkedNotebook = m_allLinkedNotebooks[i];
        if (!linkedNotebook.hasGuid()) {
            ErrorString errorMessage(QT_TR_NOOP("Internal error: found a linked notebook without guid"));
            Q_EMIT failure(errorMessage);
            QNWARNING(errorMessage << QStringLiteral(", linked notebook: ") << linkedNotebook);
            error = true;
            return false;
        }

        const QString & linkedNotebookGuid = linkedNotebook.guid();

        auto lastUpdateCountIt = m_lastUpdateCountByLinkedNotebookGuid.find(linkedNotebookGuid);
        if (lastUpdateCountIt == m_lastUpdateCountByLinkedNotebookGuid.end()) {
            lastUpdateCountIt = m_lastUpdateCountByLinkedNotebookGuid.insert(linkedNotebookGuid, 0);
        }
        qint32 lastUpdateCount = lastUpdateCountIt.value();

        qevercloud::SyncState state;
        getLinkedNotebookSyncState(linkedNotebook, m_authenticationToken, state, asyncWait, error);
        if (asyncWait || error) {
            return false;
        }

        QNDEBUG(QStringLiteral("Sync state: ") << state
                << QStringLiteral("\nLast update count = ") << lastUpdateCount);

        if (state.updateCount == lastUpdateCount) {
            QNTRACE(QStringLiteral("Evernote service has no updates for linked notebook with guid ") << linkedNotebookGuid);
            continue;
        }
        else {
            QNTRACE(QStringLiteral("Detected mismatch in update counts for linked notebook with guid ") << linkedNotebookGuid
                    << QStringLiteral(": last update count = ") << lastUpdateCount << QStringLiteral(", sync state's update count: ")
                    << state.updateCount);
            return true;
        }
    }

    QNTRACE(QStringLiteral("Checked sync states for all linked notebooks, found no updates from Evernote service"));
    return false;
}

void RemoteToLocalSynchronizationManager::authenticationInfoForNotebook(const Notebook & notebook, QString & authToken,
                                                                        QString & shardId, bool & isPublic) const
{
    isPublic = notebook.hasPublished() && notebook.isPublished();

    if (notebook.hasLinkedNotebookGuid())
    {
        auto it = m_authenticationTokensAndShardIdsByLinkedNotebookGuid.find(notebook.linkedNotebookGuid());
        if (Q_UNLIKELY(it == m_authenticationTokensAndShardIdsByLinkedNotebookGuid.end())) {
            QNWARNING(QStringLiteral("Can't download an ink note image: no authentication token and shard id for linked notebook: ")
                      << notebook);
            return;
        }

        authToken = it.value().first;
        shardId = it.value().second;
    }
    else
    {
        authToken = m_authenticationToken;
        shardId = m_shardId;
    }
}

bool RemoteToLocalSynchronizationManager::findNotebookForInkNoteImageDownloading(const Note & note)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::findNotebookForInkNoteImageDownloading: note local uid = ")
            << note.localUid() << QStringLiteral(", note guid = ") << (note.hasGuid() ? note.guid() : QStringLiteral("<empty>")));

    if (Q_UNLIKELY(!note.hasGuid())) {
        QNWARNING(QStringLiteral("Can't find notebook for ink note image downloading: note has no guid: ") << note);
        return false;
    }

    if (Q_UNLIKELY(!note.hasResources())) {
        QNWARNING(QStringLiteral("Can't find notebook for ink note image downloading: note has no resources: ") << note);
        return false;
    }

    if (Q_UNLIKELY(!note.isInkNote())) {
        QNWARNING(QStringLiteral("Can't find notebook for ink note image downloading: note is not an ink note: ") << note);
        return false;
    }

    if (Q_UNLIKELY(!note.hasNotebookLocalUid() && !note.hasNotebookGuid())) {
        QNWARNING(QStringLiteral("Can't find notebook for ink note image downloading: the note has neither notebook local uid "
                                 "nor notebook guid: ") << note);
        return false;
    }

    Notebook dummyNotebook;
    if (note.hasNotebookLocalUid()) {
        dummyNotebook.setLocalUid(note.notebookLocalUid());
    }
    else {
        dummyNotebook.setLocalUid(QString());
        dummyNotebook.setGuid(note.notebookGuid());
    }

    QUuid findNotebookForInkNoteImageDownloadRequestId = QUuid::createUuid();
    m_notesPendingInkNoteImagesDownloadByFindNotebookRequestId[findNotebookForInkNoteImageDownloadRequestId] = note;

    QString noteGuid = note.guid();

    // NOTE: technically, here we don't start downloading the ink note image yet; but it is necessary
    // to insert the resource guids per note guid into the container right here in order to prevent
    // multiple ink note image downloads for the same note during the sync process
    const QList<Resource> resources = note.resources();
    for(auto it = resources.constBegin(), end = resources.constEnd(); it != end; ++it)
    {
        const Resource & resource = *it;

        if (resource.hasGuid() && resource.hasMime() && resource.hasWidth() && resource.hasHeight() &&
            (resource.mime() == QStringLiteral("vnd.evernote.ink")))
        {
            if (!m_resourceGuidsPendingInkNoteImageDownloadPerNoteGuid.contains(note.guid(), resource.guid())) {
                m_resourceGuidsPendingInkNoteImageDownloadPerNoteGuid.insert(note.guid(), resource.guid());
            }
        }
    }

    QNTRACE(QStringLiteral("Emitting the request to find a notebook for the ink note images download setup: ")
            << findNotebookForInkNoteImageDownloadRequestId << QStringLiteral(", note guid = ")
            << noteGuid << QStringLiteral(", notebook: ") << dummyNotebook);
    Q_EMIT findNotebook(dummyNotebook, findNotebookForInkNoteImageDownloadRequestId);

    return true;
}

void RemoteToLocalSynchronizationManager::setupInkNoteImageDownloading(const QString & resourceGuid, const int resourceHeight,
                                                                       const int resourceWidth, const QString & noteGuid,
                                                                       const Notebook & notebook)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::setupInkNoteImageDownloading: resource guid = ")
            << resourceGuid << QStringLiteral(", resource height = ") << resourceHeight << QStringLiteral(", resource width = ")
            << resourceWidth << QStringLiteral(", note guid = ") << noteGuid << QStringLiteral(", notebook: ") << notebook);

    QString authToken, shardId;
    bool isPublicNotebook = false;
    authenticationInfoForNotebook(notebook, authToken, shardId, isPublicNotebook);

    if (!m_resourceGuidsPendingInkNoteImageDownloadPerNoteGuid.contains(noteGuid, resourceGuid)) {
        m_resourceGuidsPendingInkNoteImageDownloadPerNoteGuid.insert(noteGuid, resourceGuid);
    }

    QString storageFolderPath = inkNoteImagesStoragePath();

    InkNoteImageDownloader * pDownloader = new InkNoteImageDownloader(m_host, resourceGuid, noteGuid, authToken,
                                                                      shardId, resourceHeight, resourceWidth,
                                                                      /* from public linked notebook = */
                                                                      isPublicNotebook, storageFolderPath, this);
    QObject::connect(pDownloader, QNSIGNAL(InkNoteImageDownloader,finished,bool,QString,QString,ErrorString),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onInkNoteImageDownloadFinished,bool,QString,QString,ErrorString));

    // WARNING: it seems it's not possible to run ink note image downloading
    // in a different thread, the error like this might appear: QObject: Cannot
    // create children for a parent that is in a different thread.
    // (Parent is QNetworkAccessManager(0x499b900), parent's thread is QThread(0x1b535b0), current thread is QThread(0x42ed270)

    // QThreadPool::globalInstance()->start(pDownloader);

    pDownloader->run();
}

bool RemoteToLocalSynchronizationManager::setupInkNoteImageDownloadingForNote(const Note & note, const Notebook & notebook)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::setupInkNoteImageDownloadingForNote: note local uid = ")
            << note.localUid() << QStringLiteral(", note guid = ") << (note.hasGuid() ? note.guid() : QStringLiteral("<empty>"))
            << QStringLiteral(", notebook = ") << notebook);

    if (Q_UNLIKELY(!note.hasGuid())) {
        QNWARNING(QStringLiteral("Can't set up the ink note images downloading: the note has no guid: ") << note);
        return false;
    }

    if (Q_UNLIKELY(!note.hasResources())) {
        QNWARNING(QStringLiteral("Can't set up the ink note images downloading: the note has no resources: ") << note);
        return false;
    }

    if (Q_UNLIKELY(!note.isInkNote())) {
        QNWARNING(QStringLiteral("Can't set up the ink note images downloading: the note is not an ink note: ") << note);
        return false;
    }

    const QList<Resource> resources = note.resources();
    for(auto it = resources.constBegin(), end = resources.constEnd(); it != end; ++it)
    {
        const Resource & resource = *it;

        if (resource.hasGuid() && resource.hasMime() && resource.hasWidth() && resource.hasHeight() &&
            (resource.mime() == QStringLiteral("application/vnd.evernote.ink")))
        {
            setupInkNoteImageDownloading(resource.guid(), resource.height(), resource.width(), note.guid(), notebook);
        }
    }

    return true;
}

bool RemoteToLocalSynchronizationManager::findNotebookForNoteThumbnailDownloading(const Note & note)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::findNotebookForNoteThumbnailDownloading: note local uid = ")
            << note.localUid() << QStringLiteral(", note guid = ") << (note.hasGuid() ? note.guid() : QStringLiteral("<empty>")));

    if (Q_UNLIKELY(!note.hasGuid())) {
        QNWARNING(QStringLiteral("Can't find notebook for note thumbnail downloading: note has no guid: ") << note);
        return false;
    }

    if (Q_UNLIKELY(!note.hasNotebookLocalUid() && !note.hasNotebookGuid())) {
        QNWARNING(QStringLiteral("Can't find notebook for note thumbnail downloading: the note has neither notebook local uid "
                                 "nor notebook guid: ") << note);
        return false;
    }

    Notebook dummyNotebook;
    if (note.hasNotebookLocalUid()) {
        dummyNotebook.setLocalUid(note.notebookLocalUid());
    }
    else {
        dummyNotebook.setLocalUid(QString());
        dummyNotebook.setGuid(note.notebookGuid());
    }

    QUuid findNotebookForNoteThumbnailDownloadRequestId = QUuid::createUuid();
    m_notesPendingThumbnailDownloadByFindNotebookRequestId[findNotebookForNoteThumbnailDownloadRequestId] = note;

    QString noteGuid = note.guid();

    // NOTE: technically, here we don't start downloading the thumbnail yet; but it is necessary
    // to insert the note into the container right here in order to prevent multiple thumbnail downloads
    // for the same note during the sync process
    m_notesPendingThumbnailDownloadByGuid[noteGuid] = note;

    QNTRACE(QStringLiteral("Emitting the request to find a notebook for the note thumbnail download setup: ")
            << findNotebookForNoteThumbnailDownloadRequestId << QStringLiteral(", note guid = ")
            << noteGuid << QStringLiteral(", notebook: ") << dummyNotebook);
    Q_EMIT findNotebook(dummyNotebook, findNotebookForNoteThumbnailDownloadRequestId);

    return true;
}

bool RemoteToLocalSynchronizationManager::setupNoteThumbnailDownloading(const Note & note, const Notebook & notebook)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::setupNoteThumbnailDownloading: note guid = ")
            << (note.hasGuid() ? note.guid() : QStringLiteral("<empty>")) << QStringLiteral(", notebook: ") << notebook);

    if (Q_UNLIKELY(!note.hasGuid())) {
        QNWARNING(QStringLiteral("Can't setup downloading the thumbnail: note has no guid: ") << note);
        return false;
    }

    const QString & noteGuid = note.guid();
    m_notesPendingThumbnailDownloadByGuid[noteGuid] = note;

    QString authToken, shardId;
    bool isPublicNotebook = false;
    authenticationInfoForNotebook(notebook, authToken, shardId, isPublicNotebook);

    NoteThumbnailDownloader * pDownloader = new NoteThumbnailDownloader(m_host, noteGuid, authToken, shardId,
                                                                        /* from public linked notebook = */
                                                                        isPublicNotebook, this);
    QObject::connect(pDownloader, QNSIGNAL(NoteThumbnailDownloader,finished,bool,QString,QByteArray,ErrorString),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onNoteThumbnailDownloadingFinished,bool,QString,QByteArray,ErrorString));
    pDownloader->start();
    return true;
}

QString RemoteToLocalSynchronizationManager::clientNameForProtocolVersionCheck() const
{
    QString clientName = QCoreApplication::applicationName();
    clientName += QStringLiteral("/");
    clientName += QCoreApplication::applicationVersion();
    clientName += QStringLiteral("; ");

    SysInfo sysInfo;
    QString platformName = sysInfo.platformName();
    clientName += platformName;

    return clientName;
}

Note RemoteToLocalSynchronizationManager::createConflictingNote(const Note & originalNote) const
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::createConflictingNote: original note local uid = ")
            << originalNote.localUid());

    Note conflictingNote(originalNote);
    conflictingNote.setLocalUid(UidGenerator::Generate());
    conflictingNote.setGuid(QString());
    conflictingNote.setUpdateSequenceNumber(-1);
    conflictingNote.setDirty(true);
    conflictingNote.setLocal(false);

    if (originalNote.hasGuid())
    {
        qevercloud::NoteAttributes & attributes = conflictingNote.noteAttributes();
        if (!attributes.conflictSourceNoteGuid.isSet()) {
            attributes.conflictSourceNoteGuid = originalNote.guid();
        }
    }

    if (conflictingNote.hasResources())
    {
        // Need to update the conflicting note's resources:
        // 1) give each of them new local uid + unset guid
        // 2) make each of them point to the conflicting note

        QList<Resource> resources = conflictingNote.resources();

        for(auto it = resources.begin(), end = resources.end(); it != end; ++it)
        {
            Resource & resource = *it;
            resource.setLocalUid(UidGenerator::Generate());
            resource.setGuid(QString());
            resource.setDirty(true);
            resource.setLocal(false);
            resource.setNoteGuid(QString());
            resource.setNoteLocalUid(conflictingNote.localUid());
        }

        conflictingNote.setResources(resources);
    }

    qint64 currentTimestamp = QDateTime::currentMSecsSinceEpoch();
    conflictingNote.setCreationTimestamp(currentTimestamp);
    conflictingNote.setModificationTimestamp(currentTimestamp);

    QString conflictingNoteTitle;
    if (conflictingNote.hasTitle())
    {
        conflictingNoteTitle = conflictingNote.title() + QStringLiteral(" - ") + tr("conflicting");
    }
    else
    {
        QString previewText = conflictingNote.plainText();
        if (!previewText.isEmpty()) {
            previewText.truncate(12);
            conflictingNoteTitle = previewText + QStringLiteral("... - ") + tr("conflicting");
        }
        else {
            conflictingNoteTitle = tr("Conflicting note");
        }
    }

    conflictingNote.setTitle(conflictingNoteTitle);
    return conflictingNote;
}

qint32 RemoteToLocalSynchronizationManager::nonProcessedItemsSmallestUsn(const QString & linkedNotebookGuid) const
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::nonProcessedItemsSmallestUsn: linked notebook guid = ")
            << linkedNotebookGuid);

    qint32 smallestUsn = -1;

#define PROCESS_CONTAINER(container, linkedNotebookMapping) \
    for(auto it = container.constBegin(), end = container.constEnd(); it != end; ++it) \
    { \
        const auto & item = *it; \
        if (Q_UNLIKELY(!item.updateSequenceNum.isSet())) { \
            QNWARNING(QStringLiteral("Skipping item with empty update sequence number: ") << item); \
            continue; \
        } \
        \
        if (!linkedNotebookGuid.isEmpty()) \
        { \
            if (Q_UNLIKELY(!item.guid.isSet())) { \
                QNWARNING(QStringLiteral("Skipping item without guid: ") << item); \
                continue; \
            } \
            \
            auto linkedNotebookGuidIt = linkedNotebookMapping.find(item.guid.ref()); \
            if (linkedNotebookGuidIt == linkedNotebookMapping.end()) { \
                QNTRACE(QStringLiteral("Skipping item without linked notebook mapping: ") << item); \
                continue; \
            } \
            \
            if (linkedNotebookGuidIt.value() != linkedNotebookGuid) { \
                QNTRACE(QStringLiteral("Skipping item corresponding to another linked notebook (") \
                        << linkedNotebookGuidIt.value() << QStringLiteral("): ") << item); \
                continue; \
            } \
        } \
        \
        if ((smallestUsn < 0) || (item.updateSequenceNum.ref() < smallestUsn)) { \
            smallestUsn = item.updateSequenceNum.ref(); \
        } \
    }

    QHash<QString,QString> dummyHash;

    PROCESS_CONTAINER(m_tags, m_linkedNotebookGuidsByTagGuids)
    PROCESS_CONTAINER(m_tagsPendingAddOrUpdate, m_linkedNotebookGuidsByTagGuids)
    PROCESS_CONTAINER(m_savedSearches, dummyHash)
    PROCESS_CONTAINER(m_savedSearchesPendingAddOrUpdate, dummyHash)
    PROCESS_CONTAINER(m_linkedNotebooks, dummyHash)
    PROCESS_CONTAINER(m_linkedNotebooksPendingAddOrUpdate, dummyHash)
    PROCESS_CONTAINER(m_notebooks, m_linkedNotebookGuidsByNotebookGuids)
    PROCESS_CONTAINER(m_notebooksPendingAddOrUpdate, m_linkedNotebookGuidsByNotebookGuids)

    bool syncingNotebooks = m_pendingNotebooksSyncStart || notebooksSyncInProgress();
    bool syncingTags = m_pendingTagsSyncStart || tagsSyncInProgress();

    if (linkedNotebookGuid.isEmpty())
    {
        if (syncingNotebooks || syncingTags)
        {
            // That means the sync of notes hasn't started yet so we need to check notes from sync chunks
            for(auto it = m_syncChunks.constBegin(), end = m_syncChunks.constEnd(); it != end; ++it)
            {
                const qevercloud::SyncChunk & syncChunk = *it;
                if (!syncChunk.notes.isSet()) {
                    continue;
                }

                PROCESS_CONTAINER(syncChunk.notes.ref(), dummyHash)
            }
        }
        else
        {
            PROCESS_CONTAINER(m_notes, dummyHash)
            PROCESS_CONTAINER(m_notesPendingAddOrUpdate, dummyHash)
        }

        if (syncingNotebooks || syncingTags || notesSyncInProgress())
        {
            // That means the sync of resources hasn't started yet so we need to check resources from sync chunks
            for(auto it = m_syncChunks.constBegin(), end = m_syncChunks.constEnd(); it != end; ++it)
            {
                const qevercloud::SyncChunk & syncChunk = *it;
                if (!syncChunk.resources.isSet()) {
                    continue;
                }

                PROCESS_CONTAINER(syncChunk.resources.ref(), dummyHash)
            }
        }
        else
        {
            PROCESS_CONTAINER(m_resources, dummyHash)
            PROCESS_CONTAINER(m_resourcesPendingAddOrUpdate, dummyHash)
        }
    }
    else
    {
        // The mapping between linked notebook guids and note guids is implicit, through notebook guid;
        // need to make it explicit here in order to reuse the macro
        QHash<QString,QString> linkedNotebookGuidsByNoteGuids;

        // The mapping between linked notebook guids and resource guids is implicit, now through note guid;
        // need to make it explicit here in order to reuse the macro
        QHash<QString,QString> linkedNotebookGuidsByResourceGuids;

        bool syncingNotes = notesSyncInProgress();
        if (syncingNotebooks || syncingTags || syncingNotes)
        {
            NotesList localNotesList;
            if (syncingNotebooks || syncingTags)
            {
                // That means the sync of notes hasn't started yet so the notes are still within the sync chunks
                for(auto it = m_linkedNotebookSyncChunks.constBegin(), end = m_linkedNotebookSyncChunks.constEnd(); it != end; ++it)
                {
                    const qevercloud::SyncChunk & syncChunk = *it;
                    if (!syncChunk.notes.isSet()) {
                        continue;
                    }

                    localNotesList << syncChunk.notes.ref();
                }
            }
            else
            {
                // The sync of notes has already started so the pending notes are in either of two containers
                localNotesList << m_notes;
                localNotesList << m_notesPendingAddOrUpdate;
            }

            for(auto noteIt = localNotesList.constBegin(), notesEnd = localNotesList.constEnd(); noteIt != notesEnd; ++noteIt)
            {
                const qevercloud::Note & note = *noteIt;
                if (Q_UNLIKELY(!note.guid.isSet())) {
                    QNWARNING(QStringLiteral("Skipping note without guid: ") << note);
                    continue;
                }

                if (!note.notebookGuid.isSet()) {
                    QNWARNING(QStringLiteral("Skipping note without notebook guid: ") << note);
                    continue;
                }

                auto linkedNotebookGuidIt = m_linkedNotebookGuidsByNotebookGuids.find(note.notebookGuid.ref());
                if (linkedNotebookGuidIt == m_linkedNotebookGuidsByNotebookGuids.end()) {
                    QNTRACE(QStringLiteral("Skipping note without linked notebook mapping: ") << note);
                    continue;
                }

                linkedNotebookGuidsByNoteGuids[note.guid.ref()] = linkedNotebookGuidIt.value();
            }

            ResourcesList localResourcesList;
            if (syncingNotes)
            {
                // That means the sync of resources hasn't started yet so the resources are still within the sync chunks
                for(auto it = m_linkedNotebookSyncChunks.constBegin(), end = m_linkedNotebookSyncChunks.constEnd(); it != end; ++it)
                {
                    const qevercloud::SyncChunk & syncChunk = *it;
                    if (!syncChunk.resources.isSet()) {
                        continue;
                    }

                    localResourcesList << syncChunk.resources.ref();
                }
            }
            else
            {
                // The sync of resources has already started so the pending resources are in either of two containers
                localResourcesList << m_resources;
                localResourcesList << m_resourcesPendingAddOrUpdate;
            }

            for(auto resourceIt = localResourcesList.constBegin(), resourcesEnd = localResourcesList.constEnd();
                resourceIt != resourcesEnd; ++resourceIt)
            {
                const qevercloud::Resource & resource = *resourceIt;
                if (Q_UNLIKELY(!resource.guid.isSet())) {
                    QNWARNING(QStringLiteral("Skipping resource without guid: ") << resource);
                    continue;
                }

                if (Q_UNLIKELY(!resource.noteGuid.isSet())) {
                    QNWARNING(QStringLiteral("Skipping resource without note guid: ") << resource);
                    continue;
                }

                auto linkedNotebookGuidIt = linkedNotebookGuidsByNoteGuids.find(resource.noteGuid.ref());
                if (linkedNotebookGuidIt == linkedNotebookGuidsByNoteGuids.end()) {
                    QNTRACE(QStringLiteral("Skipping resource without linked notebook mapping: ") << resource);
                    continue;
                }

                linkedNotebookGuidsByResourceGuids[resource.guid.ref()] = linkedNotebookGuidIt.value();
            }
        }

        if (syncingNotebooks || syncingTags)
        {
            // That means the sync of notes hasn't started yet so we need to check notes from sync chunks
            for(auto it = m_linkedNotebookSyncChunks.constBegin(), end = m_linkedNotebookSyncChunks.constEnd(); it != end; ++it)
            {
                const qevercloud::SyncChunk & syncChunk = *it;
                if (!syncChunk.notes.isSet()) {
                    continue;
                }

                PROCESS_CONTAINER(syncChunk.notes.ref(), linkedNotebookGuidsByNoteGuids)
            }
        }
        else
        {
            PROCESS_CONTAINER(m_notes, linkedNotebookGuidsByNoteGuids)
            PROCESS_CONTAINER(m_notesPendingAddOrUpdate, linkedNotebookGuidsByNoteGuids)
        }

        if (syncingNotebooks || syncingTags || syncingNotes)
        {
            // That means the sync of resources hasn't started yet so we need to check resources from sync chunks
            for(auto it = m_linkedNotebookSyncChunks.constBegin(), end = m_linkedNotebookSyncChunks.constEnd(); it != end; ++it)
            {
                const qevercloud::SyncChunk & syncChunk = *it;
                if (!syncChunk.resources.isSet()) {
                    continue;
                }

                PROCESS_CONTAINER(syncChunk.resources.ref(), linkedNotebookGuidsByResourceGuids)
            }
        }
        else
        {
            PROCESS_CONTAINER(m_resources, linkedNotebookGuidsByResourceGuids)
            PROCESS_CONTAINER(m_resourcesPendingAddOrUpdate, linkedNotebookGuidsByResourceGuids)
        }
    }

#undef PROCESS_CONTAINER

    return smallestUsn;
}

void RemoteToLocalSynchronizationManager::registerTagPendingAddOrUpdate(const Tag & tag)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::registerTagPendingAddOrUpdate: ") << tag);

    if (!tag.hasGuid()) {
        return;
    }

    auto it = std::find_if(m_tagsPendingAddOrUpdate.begin(), m_tagsPendingAddOrUpdate.end(),
                           CompareItemByGuid<TagsList::value_type>(tag.guid()));
    if (it == m_tagsPendingAddOrUpdate.end()) {
        m_tagsPendingAddOrUpdate << tag.qevercloudTag();
    }
}

void RemoteToLocalSynchronizationManager::registerSavedSearchPendingAddOrUpdate(const SavedSearch & search)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::registerSavedSearchPendingAddOrUpdate: ") << search);

    if (!search.hasGuid()) {
        return;
    }

    auto it = std::find_if(m_savedSearchesPendingAddOrUpdate.begin(), m_savedSearchesPendingAddOrUpdate.end(),
                           CompareItemByGuid<SavedSearchesList::value_type>(search.guid()));
    if (it == m_savedSearchesPendingAddOrUpdate.end()) {
        m_savedSearchesPendingAddOrUpdate << search.qevercloudSavedSearch();
    }
}

void RemoteToLocalSynchronizationManager::registerLinkedNotebookPendingAddOrUpdate(const LinkedNotebook & linkedNotebook)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::registerLinkedNotebookPendingAddOrUpdate: ") << linkedNotebook);

    if (!linkedNotebook.hasGuid()) {
        return;
    }

    auto it = std::find_if(m_linkedNotebooksPendingAddOrUpdate.begin(), m_linkedNotebooksPendingAddOrUpdate.end(),
                           CompareItemByGuid<LinkedNotebooksList::value_type>(linkedNotebook.guid()));
    if (it == m_linkedNotebooksPendingAddOrUpdate.end()) {
        m_linkedNotebooksPendingAddOrUpdate << linkedNotebook.qevercloudLinkedNotebook();
    }
}

void RemoteToLocalSynchronizationManager::registerNotebookPendingAddOrUpdate(const Notebook & notebook)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::registerNotebookPendingAddOrUpdate: ") << notebook);

    if (!notebook.hasGuid()) {
        return;
    }

    auto it = std::find_if(m_notebooksPendingAddOrUpdate.begin(), m_notebooksPendingAddOrUpdate.end(),
                           CompareItemByGuid<NotebooksList::value_type>(notebook.guid()));
    if (it == m_notebooksPendingAddOrUpdate.end()) {
        m_notebooksPendingAddOrUpdate << notebook.qevercloudNotebook();
    }
}

void RemoteToLocalSynchronizationManager::registerNotePendingAddOrUpdate(const Note & note)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::registerNotePendingAddOrUpdate: ") << note);

    if (!note.hasGuid()) {
        return;
    }

    auto it = std::find_if(m_notesPendingAddOrUpdate.begin(), m_notesPendingAddOrUpdate.end(),
                           CompareItemByGuid<NotesList::value_type>(note.guid()));
    if (it == m_notesPendingAddOrUpdate.end()) {
        m_notesPendingAddOrUpdate << note.qevercloudNote();
    }
}

void RemoteToLocalSynchronizationManager::registerResourcePendingAddOrUpdate(const Resource & resource)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::registerResourcePendingAddOrUpdate: ") << resource);

    if (!resource.hasGuid()) {
        return;
    }

    auto it = std::find_if(m_resourcesPendingAddOrUpdate.begin(), m_resourcesPendingAddOrUpdate.end(),
                           CompareItemByGuid<ResourcesList::value_type>(resource.guid()));
    if (it == m_resourcesPendingAddOrUpdate.end()) {
        m_resourcesPendingAddOrUpdate << resource.qevercloudResource();
    }
}

void RemoteToLocalSynchronizationManager::unregisterTagPendingAddOrUpdate(const Tag & tag)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::unregisterTagPendingAddOrUpdate: ") << tag);

    if (!tag.hasGuid()) {
        return;
    }

    auto it = std::find_if(m_tagsPendingAddOrUpdate.begin(), m_tagsPendingAddOrUpdate.end(),
                           CompareItemByGuid<TagsList::value_type>(tag.guid()));
    if (it != m_tagsPendingAddOrUpdate.end()) {
        Q_UNUSED(m_tagsPendingAddOrUpdate.erase(it))
    }
}

void RemoteToLocalSynchronizationManager::unregisterSavedSearchPendingAddOrUpdate(const SavedSearch & search)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::unregisterSavedSearchPendingAddOrUpdate: ") << search);

    if (!search.hasGuid()) {
        return;
    }

    auto it = std::find_if(m_savedSearchesPendingAddOrUpdate.begin(), m_savedSearchesPendingAddOrUpdate.end(),
                           CompareItemByGuid<SavedSearchesList::value_type>(search.guid()));
    if (it != m_savedSearchesPendingAddOrUpdate.end()) {
        Q_UNUSED(m_savedSearchesPendingAddOrUpdate.erase(it))
    }
}

void RemoteToLocalSynchronizationManager::unregisterLinkedNotebookPendingAddOrUpdate(const LinkedNotebook & linkedNotebook)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::unregisterLinkedNotebookPendingAddOrUpdate: ") << linkedNotebook);

    if (!linkedNotebook.hasGuid()) {
        return;
    }

    auto it = std::find_if(m_linkedNotebooksPendingAddOrUpdate.begin(), m_linkedNotebooksPendingAddOrUpdate.end(),
                           CompareItemByGuid<LinkedNotebooksList::value_type>(linkedNotebook.guid()));
    if (it != m_linkedNotebooksPendingAddOrUpdate.end()) {
        Q_UNUSED(m_linkedNotebooksPendingAddOrUpdate.erase(it))
    }
}

void RemoteToLocalSynchronizationManager::unregisterNotebookPendingAddOrUpdate(const Notebook & notebook)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::unregisterNotebookPendingAddOrUpdate: ") << notebook);

    if (!notebook.hasGuid()) {
        return;
    }

    auto it = std::find_if(m_notebooksPendingAddOrUpdate.begin(), m_notebooksPendingAddOrUpdate.end(),
                           CompareItemByGuid<NotebooksList::value_type>(notebook.guid()));
    if (it != m_notebooksPendingAddOrUpdate.end()) {
        Q_UNUSED(m_notebooksPendingAddOrUpdate.erase(it))
    }
}

void RemoteToLocalSynchronizationManager::unregisterNotePendingAddOrUpdate(const Note & note)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::unregisterNotePendingAddOrUpdate: ") << note);

    if (!note.hasGuid()) {
        return;
    }

    auto it = std::find_if(m_notesPendingAddOrUpdate.begin(), m_notesPendingAddOrUpdate.end(),
                           CompareItemByGuid<NotesList::value_type>(note.guid()));
    if (it != m_notesPendingAddOrUpdate.end()) {
        Q_UNUSED(m_notesPendingAddOrUpdate.erase(it))
    }
}

void RemoteToLocalSynchronizationManager::unregisterResourcePendingAddOrUpdate(const Resource & resource)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::unregisterResourcePendingAddOrUpdate: ") << resource);

    if (!resource.hasGuid()) {
        return;
    }

    auto it = std::find_if(m_resourcesPendingAddOrUpdate.begin(), m_resourcesPendingAddOrUpdate.end(),
                           CompareItemByGuid<ResourcesList::value_type>(resource.guid()));
    if (it != m_resourcesPendingAddOrUpdate.end()) {
        Q_UNUSED(m_resourcesPendingAddOrUpdate.erase(it))
    }
}

void RemoteToLocalSynchronizationManager::overrideLocalNoteWithRemoteNote(Note & localNote, const qevercloud::Note & remoteNote) const
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::overrideLocalNoteWithRemoteNote: local note = ")
            << localNote << QStringLiteral("\nRemote note: ") << remoteNote);

    // Need to clear out the tag local uids from the local note so that the local storage uses tag guids list
    // from the remote note instead
    localNote.setTagLocalUids(QStringList());

    // NOTE: dealing with resources is tricky: need to not screw up the local uids of note's resources
    QList<Resource> resources;
    if (localNote.hasResources()) {
        resources = localNote.resources();
    }

    localNote.qevercloudNote() = remoteNote;
    localNote.setDirty(false);
    localNote.setLocal(false);

    QList<qevercloud::Resource> updatedResources;
    if (remoteNote.resources.isSet()) {
        updatedResources = remoteNote.resources.ref();
    }

    QList<Resource> amendedResources;
    amendedResources.reserve(updatedResources.size());

    // First update those resources which were within the local note already
    for(auto it = resources.begin(), end = resources.end(); it != end; ++it)
    {
        Resource & resource = *it;
        if (!resource.hasGuid()) {
            continue;
        }

        bool foundResource = false;
        for(auto uit = updatedResources.constBegin(), uend = updatedResources.constEnd(); uit != uend; ++uit)
        {
            const qevercloud::Resource & updatedResource = *uit;
            if (!updatedResource.guid.isSet()) {
                continue;
            }

            if (updatedResource.guid.ref() == resource.guid()) {
                resource.qevercloudResource() = updatedResource;
                // NOTE: need to not forget to reset the dirty flag since we are
                // resetting the state of the local resource here
                resource.setDirty(false);
                resource.setLocal(false);
                foundResource = true;
                break;
            }
        }

        if (foundResource) {
            amendedResources << resource;
        }
    }

    // Then account for new resources
    for(auto uit = updatedResources.constBegin(), uend = updatedResources.constEnd(); uit != uend; ++uit)
    {
        const qevercloud::Resource & updatedResource = *uit;
        if (Q_UNLIKELY(!updatedResource.guid.isSet())) {
            QNWARNING(QStringLiteral("Skipping resource from remote note without guid: ") << updatedResource);
            continue;
        }

        const Resource * pExistingResource = Q_NULLPTR;
        for(auto it = resources.constBegin(), end = resources.constEnd(); it != end; ++it)
        {
            const Resource & resource = *it;
            if (resource.hasGuid() && (resource.guid() == updatedResource.guid.ref())) {
                pExistingResource = &resource;
                break;
            }
        }

        if (pExistingResource) {
            continue;
        }

        Resource newResource;
        newResource.qevercloudResource() = updatedResource;
        newResource.setDirty(false);
        newResource.setLocal(false);
        newResource.setNoteLocalUid(localNote.localUid());
        amendedResources << newResource;
    }

    localNote.setResources(amendedResources);
    QNTRACE(QStringLiteral("Local note after overriding: ") << localNote);
}

void RemoteToLocalSynchronizationManager::processResourceConflictAsNoteConflict(Note & remoteNote, const Note & localConflictingNote,
                                                                                Resource & remoteNoteResource)
{
    QString authToken;
    ErrorString errorDescription;
    NoteStore * pNoteStore = noteStoreForNote(remoteNote, authToken, errorDescription);
    if (Q_UNLIKELY(!pNoteStore)) {
        Q_EMIT failure(errorDescription);
        return;
    }

    bool withDataBody = true;
    bool withRecognitionDataBody = true;
    bool withAlternateDataBody = true;
    bool withAttributes = true;
    errorDescription.clear();
    qint32 rateLimitSeconds = 0;
    qint32 errorCode = pNoteStore->getResource(withDataBody, withRecognitionDataBody, withAlternateDataBody,
                                               withAttributes, authToken, remoteNoteResource, errorDescription,
                                               rateLimitSeconds);
    if (errorCode == qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED)
    {
        if (Q_UNLIKELY(rateLimitSeconds <= 0)) {
            errorDescription.setBase(QT_TR_NOOP("Rate limit reached but the number of seconds to wait is incorrect"));
            errorDescription.details() = QString::number(rateLimitSeconds);
            Q_EMIT failure(errorDescription);
            return;
        }

        int timerId = startTimer(SEC_TO_MSEC(rateLimitSeconds));
        if (Q_UNLIKELY(timerId == 0)) {
            errorDescription.setBase(QT_TR_NOOP("Failed to start a timer to postpone the Evernote API call "
                                                "due to rate limit exceeding"));
            Q_EMIT failure(errorDescription);
            return;
        }

        PostponedConflictingResourceData data;
        data.m_remoteNote = remoteNote;
        data.m_localConflictingNote = localConflictingNote;
        data.m_remoteNoteResourceWithoutFullData = remoteNoteResource;
        m_postponedConflictingResourceDataPerAPICallPostponeTimerId[timerId] = data;

        Q_EMIT rateLimitExceeded(rateLimitSeconds);
        return;
    }
    else if (errorCode == qevercloud::EDAMErrorCode::AUTH_EXPIRED)
    {
        handleAuthExpiration();
        return;
    }
    else if (errorCode != 0)
    {
        ErrorString errorMessage(QT_TR_NOOP("Failed to download full resource data"));
        errorMessage.additionalBases().append(errorDescription.base());
        errorMessage.additionalBases().append(errorDescription.additionalBases());
        errorMessage.details() = errorDescription.details();
        Q_EMIT failure(errorMessage);
        return;
    }

    bool hasResources = remoteNote.hasResources();
    QList<Resource> resources;
    if (hasResources) {
        resources = remoteNote.resources();
    }

    int numResources = resources.size();
    int resourceIndex = -1;
    for(int i = 0; i < numResources; ++i)
    {
        const Resource & existingResource = resources[i];
        if (existingResource.hasGuid() && (existingResource.guid() == remoteNoteResource.guid())) {
            resourceIndex = i;
            break;
        }
    }

    if (resourceIndex < 0)
    {
        remoteNote.addResource(remoteNoteResource);
    }
    else
    {
        QList<Resource> noteResources = remoteNote.resources();
        noteResources[resourceIndex] = remoteNoteResource;
        remoteNote.setResources(noteResources);
    }

    // Update remote note
    registerNotePendingAddOrUpdate(remoteNote);
    QUuid updateNoteRequestId = QUuid::createUuid();
    Q_UNUSED(m_updateNoteRequestIds.insert(updateNoteRequestId));
    QNTRACE(QStringLiteral("Emitting the request to update the remote note in local storage: request id = ")
            << updateNoteRequestId << QStringLiteral(", note; ") << remoteNote);
    Q_EMIT updateNote(remoteNote, /* update resources = */ true, /* update tags = */ true, updateNoteRequestId);

    // Add local conflicting note
    emitAddRequest(localConflictingNote);
}

void RemoteToLocalSynchronizationManager::syncNextTagPendingProcessing()
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::syncNextTagPendingProcessing"));

    if (m_tagsPendingProcessing.isEmpty()) {
        QNDEBUG(QStringLiteral("No tags pending for processing, nothing more to sync"));
        return;
    }

    qevercloud::Tag frontTag = m_tagsPendingProcessing.takeFirst();
    emitFindByGuidRequest(frontTag);
}

void RemoteToLocalSynchronizationManager::junkFullSyncStaleDataItemsExpunger(FullSyncStaleDataItemsExpunger & expunger)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::junkFullSyncStaleDataItemsExpunger: linked notebook guid = ")
            << expunger.linkedNotebookGuid());

    QObject::disconnect(&expunger, QNSIGNAL(FullSyncStaleDataItemsExpunger,finished),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onFullSyncStaleDataItemsExpungerFinished));
    QObject::disconnect(&expunger, QNSIGNAL(FullSyncStaleDataItemsExpunger,failure,ErrorString),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onFullSyncStaleDataItemsExpungerFailure,ErrorString));
    expunger.setParent(Q_NULLPTR);
    expunger.deleteLater();
}

NoteStore * RemoteToLocalSynchronizationManager::noteStoreForNote(const Note & note, QString & authToken, ErrorString & errorDescription) const
{
    authToken.resize(0);

    if (!note.hasGuid()) {
        errorDescription.setBase(QT_TR_NOOP("Detected the attempt to get full note's data for a note without guid"));
        APPEND_NOTE_DETAILS(errorDescription, note)
        QNWARNING(errorDescription << QStringLiteral(": ") << note);
        return Q_NULLPTR;
    }

    if (!note.hasNotebookGuid()) {
        errorDescription.setBase(QT_TR_NOOP("Detected the attempt to get full note's data for a note without notebook guid"));
        APPEND_NOTE_DETAILS(errorDescription, note)
        QNWARNING(errorDescription << QStringLiteral(": ") << note);
        return Q_NULLPTR;
    }

    // Need to find out which note store is required - the one for user's own
    // account or the one for the stuff from some linked notebook

    NoteStore * pNoteStore = Q_NULLPTR;
    auto linkedNotebookGuidIt = m_linkedNotebookGuidsByNotebookGuids.find(note.notebookGuid());
    if (linkedNotebookGuidIt == m_linkedNotebookGuidsByNotebookGuids.end()) {
        QNDEBUG(QStringLiteral("Found no linked notebook corresponding to notebook guid ") << note.notebookGuid()
                << QStringLiteral(", using the note store for the user's own account"));
        pNoteStore = &(m_manager.noteStore());
        authToken = m_authenticationToken;
        return pNoteStore;
    }

    const QString & linkedNotebookGuid = linkedNotebookGuidIt.value();

    auto authTokenIt = m_authenticationTokensAndShardIdsByLinkedNotebookGuid.find(linkedNotebookGuid);
    if (Q_UNLIKELY(authTokenIt == m_authenticationTokensAndShardIdsByLinkedNotebookGuid.end())) {
        errorDescription.setBase(QT_TR_NOOP("Can't find the authentication token corresponding to the linked notebook"));
        APPEND_NOTE_DETAILS(errorDescription, note)
        QNWARNING(errorDescription << QStringLiteral(": ") << note);
        return Q_NULLPTR;
    }

    authToken = authTokenIt.value().first;
    const QString & linkedNotebookShardId = authTokenIt.value().second;

    QString linkedNotebookNoteStoreUrl;
    for(auto it = m_allLinkedNotebooks.constBegin(), end = m_allLinkedNotebooks.constEnd(); it != end; ++it)
    {
        if (it->hasGuid() && (it->guid() == linkedNotebookGuid) && it->hasNoteStoreUrl()) {
            linkedNotebookNoteStoreUrl = it->noteStoreUrl();
            break;
        }
    }

    if (linkedNotebookNoteStoreUrl.isEmpty()) {
        errorDescription.setBase(QT_TR_NOOP("Can't find the note store URL corresponding to the linked notebook"));
        APPEND_NOTE_DETAILS(errorDescription, note)
        QNWARNING(errorDescription << QStringLiteral(": ") << note);
        return Q_NULLPTR;
    }

    LinkedNotebook linkedNotebook;
    linkedNotebook.setGuid(linkedNotebookGuid);
    linkedNotebook.setShardId(linkedNotebookShardId);
    linkedNotebook.setNoteStoreUrl(linkedNotebookNoteStoreUrl);
    pNoteStore = m_manager.noteStoreForLinkedNotebook(linkedNotebook);
    if (Q_UNLIKELY(!pNoteStore)) {
        errorDescription.setBase(QT_TR_NOOP("Can't find or create note store for "));
        APPEND_NOTE_DETAILS(errorDescription, note)
        QNWARNING(errorDescription << QStringLiteral(": ") << note);
        return Q_NULLPTR;
    }

    if (Q_UNLIKELY(pNoteStore->noteStoreUrl().isEmpty())) {
        errorDescription.setBase(QT_TR_NOOP("Internal error: empty note store url for the linked notebook's note store"));
        APPEND_NOTE_DETAILS(errorDescription, note)
        QNWARNING(errorDescription << QStringLiteral(": ") << note);
        return Q_NULLPTR;
    }

    QObject::connect(pNoteStore, QNSIGNAL(NoteStore,getNoteAsyncFinished,qint32,qevercloud::Note,qint32,ErrorString),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onGetNoteAsyncFinished,qint32,qevercloud::Note,qint32,ErrorString),
                     Qt::ConnectionType(Qt::AutoConnection | Qt::UniqueConnection));

    QNDEBUG(QStringLiteral("Using NoteStore corresponding to linked notebook with guid ")
            << linkedNotebookGuid << QStringLiteral(", note store url = ") << pNoteStore->noteStoreUrl());
    return pNoteStore;
}

QTextStream & operator<<(QTextStream & strm, const RemoteToLocalSynchronizationManager::SyncMode::type & obj)
{
    switch(obj)
    {
    case RemoteToLocalSynchronizationManager::SyncMode::FullSync:
        strm << QStringLiteral("FullSync");
        break;
    case RemoteToLocalSynchronizationManager::SyncMode::IncrementalSync:
        strm << QStringLiteral("IncrementalSync");
        break;
    default:
        strm << QStringLiteral("<unknown>");
        break;
    }

    return strm;
}

template <>
void RemoteToLocalSynchronizationManager::appendDataElementsFromSyncChunkToContainer<RemoteToLocalSynchronizationManager::TagsList>(const qevercloud::SyncChunk & syncChunk,
                                                                                                                                    RemoteToLocalSynchronizationManager::TagsList & container)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::appendDataElementsFromSyncChunkToContainer: tags"));

    if (syncChunk.tags.isSet())
    {
        const auto & tags = syncChunk.tags.ref();
        QNDEBUG(QStringLiteral("Appending ") << tags.size() << QStringLiteral(" tags"));
        container.append(tags);

        for(auto it = m_expungedTags.begin(); it != m_expungedTags.end(); )
        {
            auto tagIt = std::find_if(tags.constBegin(), tags.constEnd(), CompareItemByGuid<qevercloud::Tag>(*it));
            if (tagIt == tags.constEnd()) {
                ++it;
            }
            else {
                it = m_expungedTags.erase(it);
            }
        }
    }

    if (syncChunk.expungedTags.isSet())
    {
        const auto & expungedTags = syncChunk.expungedTags.ref();
        QNDEBUG(QStringLiteral("Processing ") << expungedTags.size() << QStringLiteral(" expunged tags"));

        const auto expungedTagsEnd = expungedTags.end();
        for(auto eit = expungedTags.begin(); eit != expungedTagsEnd; ++eit)
        {
            TagsList::iterator it = std::find_if(container.begin(), container.end(),
                                                 CompareItemByGuid<qevercloud::Tag>(*eit));
            if (it != container.end()) {
                Q_UNUSED(container.erase(it));
            }

            for(auto iit = container.begin(), iend = container.end(); iit != iend; ++iit)
            {
                qevercloud::Tag & tag = *iit;
                if (tag.parentGuid.isSet() && (tag.parentGuid.ref() == *eit)) {
                    tag.parentGuid.clear();
                }
            }
        }
    }
}

template <>
void RemoteToLocalSynchronizationManager::appendDataElementsFromSyncChunkToContainer<RemoteToLocalSynchronizationManager::SavedSearchesList>(const qevercloud::SyncChunk & syncChunk,
                                                                                                                                             RemoteToLocalSynchronizationManager::SavedSearchesList & container)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::appendDataElementsFromSyncChunkToContainer: saved searches"));

    if (syncChunk.searches.isSet())
    {
        const auto & savedSearches = syncChunk.searches.ref();
        QNDEBUG(QStringLiteral("Appending ") << savedSearches.size() << QStringLiteral(" saved searches"));
        container.append(savedSearches);

        for(auto it = m_expungedSavedSearches.begin(); it != m_expungedSavedSearches.end(); )
        {
            auto searchIt = std::find_if(savedSearches.constBegin(), savedSearches.constEnd(),
                                         CompareItemByGuid<qevercloud::SavedSearch>(*it));
            if (searchIt == savedSearches.constEnd()) {
                ++it;
            }
            else {
                it = m_expungedSavedSearches.erase(it);
            }
        }
    }

    if (syncChunk.expungedSearches.isSet())
    {
        const auto & expungedSearches = syncChunk.expungedSearches.ref();
        QNDEBUG(QStringLiteral("Processing ") << expungedSearches.size() << QStringLiteral(" expunged saved searches"));

        const auto expungedSearchesEnd = expungedSearches.end();
        for(auto eit = expungedSearches.begin(); eit != expungedSearchesEnd; ++eit)
        {
            SavedSearchesList::iterator it = std::find_if(container.begin(), container.end(),
                                                          CompareItemByGuid<qevercloud::SavedSearch>(*eit));
            if (it != container.end()) {
                Q_UNUSED(container.erase(it));
            }
        }
    }
}

template <>
void RemoteToLocalSynchronizationManager::appendDataElementsFromSyncChunkToContainer<RemoteToLocalSynchronizationManager::LinkedNotebooksList>(const qevercloud::SyncChunk & syncChunk,
                                                                                                                                               RemoteToLocalSynchronizationManager::LinkedNotebooksList & container)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::appendDataElementsFromSyncChunkToContainer: linked notebooks"));

    if (syncChunk.linkedNotebooks.isSet())
    {
        const auto & linkedNotebooks = syncChunk.linkedNotebooks.ref();
        QNDEBUG(QStringLiteral("Appending ") << linkedNotebooks.size() << QStringLiteral(" linked notebooks"));
        container.append(linkedNotebooks);

        for(auto it = m_expungedLinkedNotebooks.begin(); it != m_expungedLinkedNotebooks.end(); )
        {
            auto linkedNotebookIt = std::find_if(linkedNotebooks.constBegin(), linkedNotebooks.constEnd(),
                                                 CompareItemByGuid<qevercloud::LinkedNotebook>(*it));
            if (linkedNotebookIt == linkedNotebooks.constEnd()) {
                ++it;
            }
            else {
                it = m_expungedLinkedNotebooks.erase(it);
            }
        }
    }

    if (syncChunk.expungedLinkedNotebooks.isSet())
    {
        const auto & expungedLinkedNotebooks = syncChunk.expungedLinkedNotebooks.ref();
        QNDEBUG(QStringLiteral("Processing ") << expungedLinkedNotebooks.size() << QStringLiteral(" expunged linked notebooks"));

        const auto expungedLinkedNotebooksEnd = expungedLinkedNotebooks.end();
        for(auto eit = expungedLinkedNotebooks.begin(); eit != expungedLinkedNotebooksEnd; ++eit)
        {
            LinkedNotebooksList::iterator it = std::find_if(container.begin(), container.end(),
                                                            CompareItemByGuid<qevercloud::LinkedNotebook>(*eit));
            if (it != container.end()) {
                Q_UNUSED(container.erase(it));
            }
        }
    }
}

template <>
void RemoteToLocalSynchronizationManager::appendDataElementsFromSyncChunkToContainer<RemoteToLocalSynchronizationManager::NotebooksList>(const qevercloud::SyncChunk & syncChunk,
                                                                                                                                         RemoteToLocalSynchronizationManager::NotebooksList & container)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::appendDataElementsFromSyncChunkToContainer: notebooks"));

    if (syncChunk.notebooks.isSet())
    {
        const auto & notebooks = syncChunk.notebooks.ref();
        QNDEBUG(QStringLiteral("Appending ") << notebooks.size() << QStringLiteral(" notebooks"));
        container.append(notebooks);

        for(auto it = m_expungedNotebooks.begin(); it != m_expungedNotebooks.end(); )
        {
            auto notebookIt = std::find_if(notebooks.constBegin(), notebooks.constEnd(),
                                           CompareItemByGuid<qevercloud::Notebook>(*it));
            if (notebookIt == notebooks.constEnd()) {
                ++it;
            }
            else {
                it = m_expungedNotebooks.erase(it);
            }
        }
    }

    if (syncChunk.expungedNotebooks.isSet())
    {
        const auto & expungedNotebooks = syncChunk.expungedNotebooks.ref();
        QNDEBUG(QStringLiteral("Processing ") << expungedNotebooks.size() << QStringLiteral(" expunged notebooks"));

        const auto expungedNotebooksEnd = expungedNotebooks.end();
        for(auto eit = expungedNotebooks.begin(); eit != expungedNotebooksEnd; ++eit)
        {
            NotebooksList::iterator it = std::find_if(container.begin(), container.end(),
                                                      CompareItemByGuid<qevercloud::Notebook>(*eit));
            if (it != container.end()) {
                Q_UNUSED(container.erase(it));
            }
        }
    }
}

template <>
void RemoteToLocalSynchronizationManager::appendDataElementsFromSyncChunkToContainer<RemoteToLocalSynchronizationManager::NotesList>(const qevercloud::SyncChunk & syncChunk,
                                                                                                                                     RemoteToLocalSynchronizationManager::NotesList & container)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::appendDataElementsFromSyncChunkToContainer: notes"));

    if (syncChunk.notes.isSet())
    {
        const auto & syncChunkNotes = syncChunk.notes.ref();
        QNDEBUG(QStringLiteral("Appending ") << syncChunkNotes.size() << QStringLiteral(" notes"));
        container.append(syncChunkNotes);

        for(auto it = m_expungedNotes.begin(); it != m_expungedNotes.end(); )
        {
            auto noteIt = std::find_if(syncChunkNotes.constBegin(), syncChunkNotes.constEnd(),
                                       CompareItemByGuid<qevercloud::Note>(*it));
            if (noteIt == syncChunkNotes.constEnd()) {
                ++it;
            }
            else {
                it = m_expungedNotes.erase(it);
            }
        }
    }

    if (syncChunk.expungedNotes.isSet())
    {
        const auto & expungedNotes = syncChunk.expungedNotes.ref();
        QNDEBUG(QStringLiteral("Processing ") << expungedNotes.size() << QStringLiteral(" expunged notes"));

        const auto expungedNotesEnd = expungedNotes.end();
        for(auto eit = expungedNotes.begin(); eit != expungedNotesEnd; ++eit)
        {
            NotesList::iterator it = std::find_if(container.begin(), container.end(),
                                                  CompareItemByGuid<qevercloud::Note>(*eit));
            if (it != container.end()) {
                Q_UNUSED(container.erase(it));
            }
        }
    }

    if (syncChunk.expungedNotebooks.isSet())
    {
        const auto & expungedNotebooks = syncChunk.expungedNotebooks.ref();
        QNDEBUG(QStringLiteral("Processing ") << expungedNotebooks.size() << QStringLiteral(" expunged notebooks"));

        const auto expungedNotebooksEnd = expungedNotebooks.end();
        for(auto eit = expungedNotebooks.begin(); eit != expungedNotebooksEnd; ++eit)
        {
            const QString & expungedNotebookGuid = *eit;

            for(auto it = container.begin(); it != container.end();)
            {
                qevercloud::Note & note = *it;
                if (note.notebookGuid.isSet() && (note.notebookGuid.ref() == expungedNotebookGuid)) {
                    it = container.erase(it);
                }
                else {
                    ++it;
                }
            }
        }
    }
}

template <>
void RemoteToLocalSynchronizationManager::appendDataElementsFromSyncChunkToContainer<RemoteToLocalSynchronizationManager::ResourcesList>(const qevercloud::SyncChunk & syncChunk,
                                                                                                                                         RemoteToLocalSynchronizationManager::ResourcesList & container)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::appendDataElementsFromSyncChunkToContainer: resources"));

    if (!syncChunk.resources.isSet()) {
        return;
    }

    const QList<qevercloud::Resource> & resources = syncChunk.resources.ref();
    QNDEBUG(QStringLiteral("Appending ") << resources.size() << QStringLiteral(" resources"));

    // Need to filter out those resources which belong to the notes which will be downloaded
    // along with their whole content, resources included or to the notes which have already been downloaded
    QList<qevercloud::Resource> filteredResources;
    filteredResources.reserve(resources.size());

    for(auto it = resources.constBegin(), end = resources.constEnd(); it != end; ++it)
    {
        const qevercloud::Resource & resource = *it;
        if (Q_UNLIKELY(!resource.noteGuid.isSet())) {
            QNWARNING(QStringLiteral("Skipping resource without note guid: ") << resource);
            continue;
        }

        QNTRACE(QStringLiteral("Checking whether resource belongs to a note pending downloading or already downloaded one: ")
                << resource);

        auto ngit = m_guidsOfProcessedNonExpungedNotes.find(resource.noteGuid.ref());
        if (ngit != m_guidsOfProcessedNonExpungedNotes.end()) {
            QNTRACE(QStringLiteral("Skipping resource as it belongs to the note which while content has already been downloaded: ")
                    << resource);
            continue;
        }

        bool foundNote = false;
        for(auto nit = m_notes.constBegin(), nend = m_notes.constEnd(); nit != nend; ++nit)
        {
            const qevercloud::Note & note = *nit;
            QNTRACE(QStringLiteral("Checking note: ") << note);

            if (Q_UNLIKELY(!note.guid.isSet())) {
                continue;
            }

            if (note.guid.ref() == resource.noteGuid.ref()) {
                QNTRACE(QStringLiteral("Resource belongs to a note pending downloading: ") << note);
                foundNote = true;
                break;
            }
        }

        if (foundNote) {
            QNTRACE(QStringLiteral("Skipping resource as it belongs to the note which while content would be downloaded "
                                   "a bit later: ") << resource);
            continue;
        }

        QNTRACE(QStringLiteral("Appending the resource which does not belong to any note pending downloading"));
        filteredResources << resource;
    }

    QNTRACE(QStringLiteral("Will append " ) << filteredResources.size() << QStringLiteral(" resources to the container"));
    container.append(filteredResources);
}

template <class ContainerType, class ElementType>
typename ContainerType::iterator RemoteToLocalSynchronizationManager::findItemByName(ContainerType & container,
                                                                                     const ElementType & element,
                                                                                     const QString & typeName)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::findItemByName<") << typeName << QStringLiteral(">"));

    // Attempt to find this data element by name within the list of elements waiting for processing;
    // first simply try the front element from the list to avoid the costly lookup
    if (!element.hasName()) {
        SET_CANT_FIND_BY_NAME_ERROR();
        Q_EMIT failure(errorDescription);
        return container.end();
    }

    if (container.isEmpty()) {
        SET_EMPTY_PENDING_LIST_ERROR();
        Q_EMIT failure(errorDescription);
        return container.end();
    }

    // Try the front element first, in most cases it should be it
    const auto & frontItem = container.front();
    typename ContainerType::iterator it = container.begin();
    if (!frontItem.name.isSet() || (frontItem.name.ref() != element.name()))
    {
        it = std::find_if(container.begin(), container.end(),
                          CompareItemByName<typename ContainerType::value_type>(element.name()));
        if (it == container.end()) {
            SET_CANT_FIND_IN_PENDING_LIST_ERROR();
            Q_EMIT failure(errorDescription);
            return container.end();
        }
    }

    return it;
}

template<>
RemoteToLocalSynchronizationManager::NotesList::iterator RemoteToLocalSynchronizationManager::findItemByName<RemoteToLocalSynchronizationManager::NotesList, Note>(NotesList & container,
                                                                                                                                                                   const Note & element,
                                                                                                                                                                   const QString & typeName)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::findItemByName<") << typeName << QStringLiteral(">"));

    // Attempt to find this data element by name within the list of elements waiting for processing;
    // first simply try the front element from the list to avoid the costly lookup
    if (!element.hasTitle()) {
        SET_CANT_FIND_BY_NAME_ERROR();
        Q_EMIT failure(errorDescription);
        return container.end();
    }

    if (container.isEmpty()) {
        SET_EMPTY_PENDING_LIST_ERROR();
        Q_EMIT failure(errorDescription);
        return container.end();
    }

    // Try the front element first, in most cases it should be it
    const auto & frontItem = container.front();
    NotesList::iterator it = container.begin();
    if (!frontItem.title.isSet() || (frontItem.title.ref() != element.title()))
    {
        it = std::find_if(container.begin(), container.end(),
                          CompareItemByName<qevercloud::Note>(element.title()));
        if (it == container.end()) {
            SET_CANT_FIND_IN_PENDING_LIST_ERROR();
            Q_EMIT failure(errorDescription);
            return container.end();
        }
    }

    return it;
}

template <class ContainerType, class ElementType>
typename ContainerType::iterator RemoteToLocalSynchronizationManager::findItemByGuid(ContainerType & container,
                                                                                     const ElementType & element,
                                                                                     const QString & typeName)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::findItemByGuid<") << typeName << QStringLiteral(">"));

    // Attempt to find this data element by guid within the list of elements waiting for processing;
    // first simply try the front element from the list to avoid the costly lookup
    if (!element.hasGuid()) {
        SET_CANT_FIND_BY_GUID_ERROR();
        Q_EMIT failure(errorDescription);
        return container.end();
    }

    if (container.isEmpty()) {
        SET_EMPTY_PENDING_LIST_ERROR();
        Q_EMIT failure(errorDescription);
        return container.end();
    }

    // Try the front element first, in most cases it should be it
    const auto & frontItem = container.front();
    typename ContainerType::iterator it = container.begin();
    if (!frontItem.guid.isSet() || (frontItem.guid.ref() != element.guid()))
    {
        it = std::find_if(container.begin(), container.end(),
                          CompareItemByGuid<typename ContainerType::value_type>(element.guid()));
        if (it == container.end()) {
            SET_CANT_FIND_IN_PENDING_LIST_ERROR();
            Q_EMIT failure(errorDescription);
            return container.end();
        }
    }

    return it;
}

template <class T>
bool RemoteToLocalSynchronizationManager::CompareItemByName<T>::operator()(const T & item) const
{
    if (item.name.isSet()) {
        return (m_name.toUpper() == item.name.ref().toUpper());
    }
    else {
        return false;
    }
}

template <>
bool RemoteToLocalSynchronizationManager::CompareItemByName<qevercloud::Note>::operator()(const qevercloud::Note & item) const
{
    if (item.title.isSet()) {
        return (m_name.toUpper() == item.title->toUpper());
    }
    else {
        return false;
    }
}

template <class T>
bool RemoteToLocalSynchronizationManager::CompareItemByGuid<T>::operator()(const T & item) const
{
    if (item.guid.isSet()) {
        return (m_guid == item.guid.ref());
    }
    else {
        return false;
    }
}

template <>
bool RemoteToLocalSynchronizationManager::CompareItemByGuid<LinkedNotebook>::operator()(const LinkedNotebook & item) const
{
    if (item.hasGuid()) {
        return (m_guid == item.guid());
    }
    else {
        return false;
    }
}

template <class ElementType>
void RemoteToLocalSynchronizationManager::extractExpungedElementsFromSyncChunk(const qevercloud::SyncChunk & syncChunk, QList<QString> & expungedElementGuids)
{
    Q_UNUSED(syncChunk);
    Q_UNUSED(expungedElementGuids);
    // do nothing by default
}

template <>
void RemoteToLocalSynchronizationManager::extractExpungedElementsFromSyncChunk<Tag>(const qevercloud::SyncChunk & syncChunk,
                                                                                    QList<QString> & expungedElementGuids)
{
    if (syncChunk.expungedTags.isSet()) {
        expungedElementGuids = syncChunk.expungedTags.ref();
    }
}

template <>
void RemoteToLocalSynchronizationManager::extractExpungedElementsFromSyncChunk<SavedSearch>(const qevercloud::SyncChunk & syncChunk,
                                                                                            QList<QString> & expungedElementGuids)
{
    if (syncChunk.expungedSearches.isSet()) {
        expungedElementGuids = syncChunk.expungedSearches.ref();
    }
}

template <>
void RemoteToLocalSynchronizationManager::extractExpungedElementsFromSyncChunk<Notebook>(const qevercloud::SyncChunk & syncChunk,
                                                                                         QList<QString> & expungedElementGuids)
{
    if (syncChunk.expungedNotebooks.isSet()) {
        expungedElementGuids = syncChunk.expungedNotebooks.ref();
    }
}

template <>
void RemoteToLocalSynchronizationManager::extractExpungedElementsFromSyncChunk<Note>(const qevercloud::SyncChunk & syncChunk,
                                                                                     QList<QString> & expungedElementGuids)
{
    if (syncChunk.expungedNotes.isSet()) {
        expungedElementGuids = syncChunk.expungedNotes.ref();
    }
}

template <>
void RemoteToLocalSynchronizationManager::extractExpungedElementsFromSyncChunk<LinkedNotebook>(const qevercloud::SyncChunk & syncChunk,
                                                                                               QList<QString> & expungedElementGuids)
{
    if (syncChunk.expungedLinkedNotebooks.isSet()) {
        expungedElementGuids = syncChunk.expungedLinkedNotebooks.ref();
    }
}

template <>
void RemoteToLocalSynchronizationManager::emitFindByNameRequest<Tag>(const Tag & tag)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::emitFindByNameRequest<Tag>: ") << tag);

    if (!tag.hasName()) {
        ErrorString errorDescription(QT_TR_NOOP("Detected tag from remote storage which needs "
                                                "to be searched by name in the local storage but "
                                                "it has no name set"));
        QNWARNING(errorDescription << QStringLiteral(": ") << tag);
        Q_EMIT failure(errorDescription);
        return;
    }

    QUuid findElementRequestId = QUuid::createUuid();
    Q_UNUSED(m_findTagByNameRequestIds.insert(findElementRequestId));
    QNTRACE(QStringLiteral("Emitting the request to find tag in the local storage: request id = ")
            << findElementRequestId << QStringLiteral(", tag: ") << tag);
    Q_EMIT findTag(tag, findElementRequestId);
}

template <>
void RemoteToLocalSynchronizationManager::emitFindByNameRequest<SavedSearch>(const SavedSearch & search)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::emitFindByNameRequest<SavedSearch>: ") << search);

    if (!search.hasName()) {
        ErrorString errorDescription(QT_TR_NOOP("Detected saved search from remote storage which needs to be "
                                                "searched by name in the local storage but it has no name set"));
        QNWARNING(errorDescription << QStringLiteral(": ") << search);
        Q_EMIT failure(errorDescription);
        return;
    }

    QUuid findElementRequestId = QUuid::createUuid();
    Q_UNUSED(m_findSavedSearchByNameRequestIds.insert(findElementRequestId));
    QNTRACE(QStringLiteral("Emitting the request to find saved search in the local storage: request id = ")
            << findElementRequestId << QStringLiteral(", saved search: ") << search);
    Q_EMIT findSavedSearch(search, findElementRequestId);
}

template <>
void RemoteToLocalSynchronizationManager::emitFindByNameRequest<Notebook>(const Notebook & notebook)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::emitFindByNameRequest<Notebook>: ") << notebook);

    if (!notebook.hasName()) {
        ErrorString errorDescription(QT_TR_NOOP("Detected notebook from remote storage which needs to be "
                                                "searched by name in the local storage but it has no name set"));
        QNWARNING(errorDescription << QStringLiteral(": ") << notebook);
        Q_EMIT failure(errorDescription);
        return;
    }

    QUuid findElementRequestId = QUuid::createUuid();
    Q_UNUSED(m_findNotebookByNameRequestIds.insert(findElementRequestId));
    QNTRACE(QStringLiteral("Emitting the request to find notebook in the local storage by name: request id = ")
            << findElementRequestId << QStringLiteral(", notebook: ") << notebook);
    Q_EMIT findNotebook(notebook, findElementRequestId);
}

template <class ContainerType, class ElementType>
bool RemoteToLocalSynchronizationManager::onFoundDuplicateByName(ElementType element, const QUuid & requestId,
                                                                 const QString & typeName, ContainerType & container,
                                                                 ContainerType & pendingItemsContainer,
                                                                 QSet<QUuid> & findElementRequestIds)
{
    QSet<QUuid>::iterator rit = findElementRequestIds.find(requestId);
    if (rit == findElementRequestIds.end()) {
        return false;
    }

    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onFoundDuplicateByName<") << typeName << QStringLiteral(">: ")
            << typeName << QStringLiteral(" = ") << element << QStringLiteral(", requestId  = ") << requestId);

    Q_UNUSED(findElementRequestIds.erase(rit));

    typename ContainerType::iterator it = findItemByName(container, element, typeName);
    if (it == container.end()) {
        return true;
    }

    // The element exists both in the client and in the server
    typedef typename ContainerType::value_type RemoteElementType;
    const RemoteElementType & remoteElement = *it;

    if (!remoteElement.updateSequenceNum.isSet()) {
        ErrorString errorDescription(QT_TR_NOOP("Found a data item without the update sequence number within the sync chunk"));
        SET_ITEM_TYPE_TO_ERROR();
        QNWARNING(errorDescription << QStringLiteral(": ") << remoteElement);
        Q_EMIT failure(errorDescription);
        return true;
    }

    if (!remoteElement.guid.isSet()) {
        ErrorString errorDescription(QT_TR_NOOP("Found a data item without guid within the sync chunk"));
        SET_ITEM_TYPE_TO_ERROR();
        QNWARNING(errorDescription << QStringLiteral(": ") << remoteElement);
        Q_EMIT failure(errorDescription);
        return true;
    }

    resolveSyncConflict(remoteElement, element);

    typename ContainerType::iterator pendingItemIt = std::find_if(pendingItemsContainer.begin(),
                                                                  pendingItemsContainer.end(),
                                                                  CompareItemByGuid<typename ContainerType::value_type>(remoteElement.guid.ref()));
    if (pendingItemIt == pendingItemsContainer.end()) {
        pendingItemsContainer << remoteElement;
    }

    Q_UNUSED(container.erase(it))
    return true;
}

template <class ElementType, class ContainerType>
bool RemoteToLocalSynchronizationManager::onFoundDuplicateByGuid(ElementType element, const QUuid & requestId,
                                                                 const QString & typeName, ContainerType & container,
                                                                 ContainerType & pendingItemsContainer,
                                                                 QSet<QUuid> & findByGuidRequestIds,
                                                                 const bool removeItemFromOriginalContainer)
{
    typename QSet<QUuid>::iterator rit = findByGuidRequestIds.find(requestId);
    if (rit == findByGuidRequestIds.end()) {
        return false;
    }

    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onFoundDuplicateByGuid<") << typeName << QStringLiteral(">: ")
            << typeName << QStringLiteral(" = ") << element << QStringLiteral(", requestId = ") << requestId);

    Q_UNUSED(findByGuidRequestIds.erase(rit));

    typename ContainerType::iterator it = findItemByGuid(container, element, typeName);
    if (it == container.end()) {
        ErrorString errorDescription(QT_TR_NOOP("Could not find the remote item by guid when reported "
                                                "of duplicate by guid in the local storage"));
        SET_ITEM_TYPE_TO_ERROR();
        QNWARNING(errorDescription << QStringLiteral(": ") << element);
        Q_EMIT failure(errorDescription);
        return true;
    }

    typedef typename ContainerType::value_type RemoteElementType;
    const RemoteElementType & remoteElement = *it;
    if (!remoteElement.updateSequenceNum.isSet()) {
        ErrorString errorDescription(QT_TR_NOOP("Found a remote data item without the update sequence number"));
        SET_ITEM_TYPE_TO_ERROR();
        QNWARNING(errorDescription << QStringLiteral(": ") << remoteElement);
        Q_EMIT failure(errorDescription);
        return true;
    }

    resolveSyncConflict(remoteElement, element);

    typename ContainerType::iterator pendingItemIt = std::find_if(pendingItemsContainer.begin(),
                                                                  pendingItemsContainer.end(),
                                                                  CompareItemByGuid<typename ContainerType::value_type>(remoteElement.guid.ref()));
    if (pendingItemIt == pendingItemsContainer.end()) {
        pendingItemsContainer << remoteElement;
    }

    if (removeItemFromOriginalContainer) {
        Q_UNUSED(container.erase(it))
    }

    return true;
}

template <class ContainerType, class ElementType>
bool RemoteToLocalSynchronizationManager::onNoDuplicateByGuid(ElementType element, const QUuid & requestId,
                                                              const ErrorString & errorDescription,
                                                              const QString & typeName, ContainerType & container,
                                                              QSet<QUuid> & findElementRequestIds)
{
    QSet<QUuid>::iterator rit = findElementRequestIds.find(requestId);
    if (rit == findElementRequestIds.end()) {
        return false;
    }

    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onNoDuplicateByGuid<") << typeName << QStringLiteral(">: ")
            << element << QStringLiteral(", errorDescription = ") << errorDescription << QStringLiteral(", requestId = ")
            << requestId);

    Q_UNUSED(findElementRequestIds.erase(rit));

    typename ContainerType::iterator it = findItemByGuid(container, element, typeName);
    if (it == container.end()) {
        return true;
    }

    // This element wasn't found in the local storage by guid, need to check whether
    // the element with similar name exists
    ElementType elementToFindByName(*it);
    elementToFindByName.unsetLocalUid();
    checkAndAddLinkedNotebookBinding(elementToFindByName);
    elementToFindByName.setGuid(QString());
    emitFindByNameRequest(elementToFindByName);

    return true;
}

template <class ContainerType, class ElementType>
bool RemoteToLocalSynchronizationManager::onNoDuplicateByName(ElementType element, const QUuid & requestId,
                                                              const ErrorString & errorDescription,
                                                              const QString & typeName, ContainerType & container,
                                                              QSet<QUuid> & findElementRequestIds)
{
    QSet<QUuid>::iterator rit = findElementRequestIds.find(requestId);
    if (rit == findElementRequestIds.end()) {
        return false;
    }

    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onNoDuplicateByName<") << typeName << QStringLiteral(">: ")
            << element << QStringLiteral(", errorDescription = ") << errorDescription << QStringLiteral(", requestId = ")
            << requestId);

    Q_UNUSED(findElementRequestIds.erase(rit));

    typename ContainerType::iterator it = findItemByName(container, element, typeName);
    if (it == container.end()) {
        return true;
    }

    if (Q_UNLIKELY(!it->guid.isSet())) {
        ErrorString error(QT_TR_NOOP("Internal error: found data item without guid within those from the downloaded sync chunks"));
        QNWARNING(error << QStringLiteral(": ") << *it);
        Q_EMIT failure(error);
        return true;
    }

    // This element wasn't found in the local storage by guid or name ==> it's new from remote storage, adding it
    ElementType newElement(*it);
    setNonLocalAndNonDirty(newElement);
    checkAndAddLinkedNotebookBinding(newElement);

    emitAddRequest(newElement);

    // also removing the element from the list of ones waiting for processing
    Q_UNUSED(container.erase(it));

    return true;
}

template <>
void RemoteToLocalSynchronizationManager::resolveSyncConflict(const qevercloud::Notebook & remoteNotebook,
                                                              const Notebook & localConflict)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::resolveSyncConflict<Notebook>: remote notebook = ")
            << remoteNotebook << QStringLiteral("\nLocal conflicting notebook: ") << localConflict);

    if (Q_UNLIKELY(!remoteNotebook.guid.isSet())) {
        ErrorString error(QT_TR_NOOP("Can't resolve the conflict between remote and local notebooks: the remote notebook has no guid"));
        QNWARNING(error << QStringLiteral(", remote notebook: ") << remoteNotebook);
        Q_EMIT failure(error);
        return;
    }

    QList<NotebookSyncConflictResolver*> notebookSyncConflictResolvers = findChildren<NotebookSyncConflictResolver*>();

    for(auto it = notebookSyncConflictResolvers.constBegin(),
        end = notebookSyncConflictResolvers.constEnd(); it != end; ++it)
    {
        const NotebookSyncConflictResolver * pResolver = *it;
        if (Q_UNLIKELY(!pResolver)) {
            QNWARNING(QStringLiteral("Skipping the null pointer to notebook sync conflict resolver"));
            continue;
        }

        const qevercloud::Notebook & resolverRemoteNotebook = pResolver->remoteNotebook();
        if (Q_UNLIKELY(!resolverRemoteNotebook.guid.isSet())) {
            QNWARNING(QStringLiteral("Skipping the resolver with remote notebook containing no guid: ") << resolverRemoteNotebook);
            continue;
        }

        if (resolverRemoteNotebook.guid.ref() != remoteNotebook.guid.ref()) {
            QNTRACE(QStringLiteral("Skipping the existing notebook sync conflict resolver processing remote notebook with another guid: ")
                    << resolverRemoteNotebook);
            continue;
        }

        const Notebook & resolverLocalConflict = pResolver->localConflict();
        if (resolverLocalConflict.localUid() != localConflict.localUid()) {
            QNTRACE(QStringLiteral("Skipping the existing notebook sync conflict resolver processing local conflict with another local uid: ")
                    << resolverLocalConflict);
            continue;
        }

        QNDEBUG(QStringLiteral("Found existing notebook sync conflict resolver for this pair of remote and local notebooks"));
        return;
    }

    NotebookSyncCache * pCache = Q_NULLPTR;
    if (localConflict.hasLinkedNotebookGuid())
    {
        const QString & linkedNotebookGuid = localConflict.linkedNotebookGuid();
        auto it = m_notebookSyncCachesByLinkedNotebookGuids.find(linkedNotebookGuid);
        if (it == m_notebookSyncCachesByLinkedNotebookGuids.end()) {
            pCache = new NotebookSyncCache(m_manager.localStorageManagerAsync(),
                                           linkedNotebookGuid, this);
            it = m_notebookSyncCachesByLinkedNotebookGuids.insert(linkedNotebookGuid, pCache);
        }

        pCache = it.value();
    }
    else
    {
        pCache = &m_notebookSyncCache;
    }

    NotebookSyncConflictResolver * pResolver = new NotebookSyncConflictResolver(remoteNotebook, localConflict, *pCache,
                                                                                m_manager.localStorageManagerAsync(), this);
    QObject::connect(pResolver, QNSIGNAL(NotebookSyncConflictResolver,finished,qevercloud::Notebook),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onNotebookSyncConflictResolverFinished,qevercloud::Notebook));
    QObject::connect(pResolver, QNSIGNAL(NotebookSyncConflictResolver,failure,qevercloud::Notebook,ErrorString),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onNotebookSyncConflictResolverFailure,qevercloud::Notebook,ErrorString));
    pResolver->start();
}

template <>
void RemoteToLocalSynchronizationManager::resolveSyncConflict(const qevercloud::Tag & remoteTag,
                                                              const Tag & localConflict)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::resolveSyncConflict<Tag>: remote tag = ")
            << remoteTag << QStringLiteral("\nLocal conflicting tag: ") << localConflict);

    if (Q_UNLIKELY(!remoteTag.guid.isSet())) {
        ErrorString error(QT_TR_NOOP("Can't resolve the conflict between remote and local tags: the remote tag has no guid"));
        QNWARNING(error << QStringLiteral(", remote tag: ") << remoteTag);
        Q_EMIT failure(error);
        return;
    }

    QList<TagSyncConflictResolver*> tagSyncConflictResolvers = findChildren<TagSyncConflictResolver*>();

    for(auto it = tagSyncConflictResolvers.constBegin(),
        end = tagSyncConflictResolvers.constEnd(); it != end; ++it)
    {
        const TagSyncConflictResolver * pResolver = *it;
        if (Q_UNLIKELY(!pResolver)) {
            QNWARNING(QStringLiteral("Skipping the null pointer to tag sync conflict resolver"));
            continue;
        }

        const qevercloud::Tag & resolverRemoteTag = pResolver->remoteTag();
        if (Q_UNLIKELY(!resolverRemoteTag.guid.isSet())) {
            QNWARNING(QStringLiteral("Skipping the resolver with remote tag containing no guid: ") << resolverRemoteTag);
            continue;
        }

        if (resolverRemoteTag.guid.ref() != remoteTag.guid.ref()) {
            QNTRACE(QStringLiteral("Skipping the existing tag sync conflict resolver processing remote tag with another guid: ")
                    << resolverRemoteTag);
            continue;
        }

        const Tag & resolverLocalConflict = pResolver->localConflict();
        if (resolverLocalConflict.localUid() != localConflict.localUid()) {
            QNTRACE(QStringLiteral("Skipping the existing tag sync conflict resolver processing local conflict with another local uid: ")
                    << resolverLocalConflict);
            continue;
        }

        QNDEBUG(QStringLiteral("Found existing tag sync conflict resolver for this pair of remote and local tags"));
        return;
    }

    TagSyncCache * pCache = Q_NULLPTR;
    if (localConflict.hasLinkedNotebookGuid())
    {
        const QString & linkedNotebookGuid = localConflict.linkedNotebookGuid();
        auto it = m_tagSyncCachesByLinkedNotebookGuids.find(linkedNotebookGuid);
        if (it == m_tagSyncCachesByLinkedNotebookGuids.end()) {
            pCache = new TagSyncCache(m_manager.localStorageManagerAsync(), linkedNotebookGuid, this);
            it = m_tagSyncCachesByLinkedNotebookGuids.insert(linkedNotebookGuid, pCache);
        }

        pCache = it.value();
    }
    else
    {
        pCache = &m_tagSyncCache;
    }

    TagSyncConflictResolver * pResolver = new TagSyncConflictResolver(remoteTag, localConflict, *pCache,
                                                                      m_manager.localStorageManagerAsync(), this);
    QObject::connect(pResolver, QNSIGNAL(TagSyncConflictResolver,finished,qevercloud::Tag),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onTagSyncConflictResolverFinished,qevercloud::Tag));
    QObject::connect(pResolver, QNSIGNAL(TagSyncConflictResolver,failure,qevercloud::Tag,ErrorString),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onTagSyncConflictResolverFailure,qevercloud::Tag,ErrorString));
    pResolver->start();
}

template <>
void RemoteToLocalSynchronizationManager::resolveSyncConflict(const qevercloud::SavedSearch & remoteSavedSearch,
                                                              const SavedSearch & localConflict)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::resolveSyncConflict<SavedSearch>: remote saved search = ")
            << remoteSavedSearch << QStringLiteral("\nLocal conflicting saved search: ") << localConflict);

    if (Q_UNLIKELY(!remoteSavedSearch.guid.isSet())) {
        ErrorString error(QT_TR_NOOP("Can't resolve the conflict between remote and local saved searches: the remote saved search has no guid"));
        QNWARNING(error << QStringLiteral(", remote saved search: ") << remoteSavedSearch);
        Q_EMIT failure(error);
        return;
    }

    QList<SavedSearchSyncConflictResolver*> savedSearchSyncConflictResolvers = findChildren<SavedSearchSyncConflictResolver*>();

    for(auto it = savedSearchSyncConflictResolvers.constBegin(),
        end = savedSearchSyncConflictResolvers.constEnd(); it != end; ++it)
    {
        const SavedSearchSyncConflictResolver * pResolver = *it;
        if (Q_UNLIKELY(!pResolver)) {
            QNWARNING(QStringLiteral("Skipping the null pointer to saved search sync conflict resolver"));
            continue;
        }

        const qevercloud::SavedSearch & resolverRemoteSavedSearch = pResolver->remoteSavedSearch();
        if (Q_UNLIKELY(!resolverRemoteSavedSearch.guid.isSet())) {
            QNWARNING(QStringLiteral("Skipping the existing saved search sync conflict resolver processing remote saved search with another guid: ")
                      << resolverRemoteSavedSearch);
            continue;
        }

        if (resolverRemoteSavedSearch.guid.ref() != remoteSavedSearch.guid.ref()) {
            QNTRACE(QStringLiteral("Skipping the existing saved search sync conflict resolver processing remote saved search with another guid: ")
                    << resolverRemoteSavedSearch);
            continue;
        }

        const SavedSearch & resolverLocalConflict = pResolver->localConflict();
        if (resolverLocalConflict.localUid() != localConflict.localUid()) {
            QNTRACE(QStringLiteral("Skipping the existing saved search sync conflict resolver processing local conflict with another local uid: ")
                    << resolverLocalConflict);
            continue;
        }

        QNDEBUG(QStringLiteral("Found existing saved search conflict resolver for this pair of remote and local saved searches"));
        return;
    }

    SavedSearchSyncConflictResolver * pResolver = new SavedSearchSyncConflictResolver(remoteSavedSearch, localConflict,
                                                                                      m_savedSearchSyncCache,
                                                                                      m_manager.localStorageManagerAsync(), this);
    QObject::connect(pResolver, QNSIGNAL(SavedSearchSyncConflictResolver,finished,qevercloud::SavedSearch),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onSavedSearchSyncConflictResolverFinished,qevercloud::SavedSearch));
    QObject::connect(pResolver, QNSIGNAL(SavedSearchSyncConflictResolver,failure,qevercloud::SavedSearch,ErrorString),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onSavedSearchSyncConflictResolverFailure,qevercloud::SavedSearch,ErrorString));
    pResolver->start();
}

template <>
void RemoteToLocalSynchronizationManager::resolveSyncConflict(const qevercloud::Note & remoteNote,
                                                              const Note & localConflict)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::resolveSyncConflict<Note>: remote note = ")
            << remoteNote << QStringLiteral("\nLocal conflicting note: ") << localConflict);

    if (Q_UNLIKELY(!remoteNote.guid.isSet())) {
        ErrorString errorDescription(QT_TR_NOOP("Found a remote note without guid set"));
        QNWARNING(errorDescription << QStringLiteral(", note: ") << remoteNote);
        Q_EMIT failure(errorDescription);
        return;
    }

    if (Q_UNLIKELY(!remoteNote.updateSequenceNum.isSet())) {
        ErrorString errorDescription(QT_TR_NOOP("Found a remote note without update sequence number set"));
        QNWARNING(errorDescription << QStringLiteral(", note: ") << remoteNote);
        Q_EMIT failure(errorDescription);
        return;
    }

    bool shouldCreateConflictingNote = true;
    if (localConflict.hasGuid() && (localConflict.guid() == remoteNote.guid.ref()))
    {
        QNDEBUG(QStringLiteral("The notes match by guid"));

        if (!localConflict.isDirty()) {
            QNDEBUG(QStringLiteral("The local note is not dirty, can just override it with remote changes"));
            shouldCreateConflictingNote = false;
        }
        else if (localConflict.hasUpdateSequenceNumber() && (localConflict.updateSequenceNumber() == remoteNote.updateSequenceNum.ref())) {
            QNDEBUG(QStringLiteral("The notes match by update sequence number but the local note is dirty => local note should override the remote changes"));
            return;
        }
    }

    // NOTE: it is necessary to copy the updated note from the local conflict
    // in order to preserve stuff like e.g. resource local uids which otherwise
    // would be different from those currently stored within the local storage
    Note updatedNote(localConflict);
    overrideLocalNoteWithRemoteNote(updatedNote, remoteNote);

    registerNotePendingAddOrUpdate(updatedNote);
    getFullNoteDataAsyncAndUpdateInLocalStorage(updatedNote);

    if (shouldCreateConflictingNote) {
        Note conflictingNote = createConflictingNote(localConflict);
        emitAddRequest(conflictingNote);
    }
}

template <>
void RemoteToLocalSynchronizationManager::resolveSyncConflict(const qevercloud::LinkedNotebook & remoteLinkedNotebook,
                                                              const LinkedNotebook & localConflict)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::resolveSyncConflict<LinkedNotebook>: remote linked notebook = ")
            << remoteLinkedNotebook << QStringLiteral("\nLocal conflicting linked notebook: ") << localConflict);

    // NOTE: since linked notebook is just a pointer to a notebook in another user's account, it makes little sense
    // to even attempt to resolve any potential conflict in favor of local changes - the remote changes should always
    // win

    LinkedNotebook linkedNotebook(localConflict);
    linkedNotebook.qevercloudLinkedNotebook() = remoteLinkedNotebook;
    linkedNotebook.setDirty(false);

    registerLinkedNotebookPendingAddOrUpdate(linkedNotebook);

    QUuid updateLinkedNotebookRequestId = QUuid::createUuid();
    Q_UNUSED(m_updateLinkedNotebookRequestIds.insert(updateLinkedNotebookRequestId));
    QNTRACE(QStringLiteral("Emitting the request to update linked notebook: request id = ") << updateLinkedNotebookRequestId
            << QStringLiteral(", linked notebook: ") << linkedNotebook);
    Q_EMIT updateLinkedNotebook(linkedNotebook, updateLinkedNotebookRequestId);
}

bool RemoteToLocalSynchronizationManager::sortTagsByParentChildRelations(TagsList & tagList)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::sortTagsByParentChildRelations"));

    ErrorString errorDescription;
    bool res = ::quentier::sortTagsByParentChildRelations(tagList, errorDescription);
    if (!res) {
        QNWARNING(errorDescription);
        Q_EMIT failure(errorDescription);
        return false;
    }

    return true;
}

QTextStream & RemoteToLocalSynchronizationManager::PostponedConflictingResourceData::print(QTextStream & strm) const
{
    strm << QStringLiteral("PostponedConflictingResourceData: {\n  Remote note:\n")
         << m_remoteNote << QStringLiteral("\n\n  Local conflicting note:\n")
         << m_localConflictingNote << QStringLiteral("\n\n  Remote note's resource without full data:\n")
         << m_remoteNoteResourceWithoutFullData << QStringLiteral("\n};\n");
    return strm;
}

} // namespace quentier
