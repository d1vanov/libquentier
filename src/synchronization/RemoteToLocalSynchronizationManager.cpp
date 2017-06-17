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
#include "SynchronizationPersistenceName.h"
#include "InkNoteImageDownloader.h"
#include "NoteThumbnailDownloader.h"
#include <quentier/utility/Utility.h>
#include <quentier/local_storage/LocalStorageManagerAsync.h>
#include <quentier/types/Resource.h>
#include <quentier/utility/QuentierCheckPtr.h>
#include <quentier/utility/ApplicationSettings.h>
#include <quentier/utility/StandardPaths.h>
#include <quentier/utility/SysInfo.h>
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

namespace quentier {

RemoteToLocalSynchronizationManager::RemoteToLocalSynchronizationManager(LocalStorageManagerAsync & localStorageManagerAsync,
                                                                         const QString & host, QSharedPointer<qevercloud::NoteStore> pNoteStore,
                                                                         QSharedPointer<qevercloud::UserStore> pUserStore,
                                                                         QObject * parent) :
    QObject(parent),
    m_localStorageManagerAsync(localStorageManagerAsync),
    m_connectedToLocalStorage(false),
    m_host(host),
    m_noteStore(pNoteStore),
    m_userStore(pUserStore),
    m_maxSyncChunkEntries(50),
    m_lastSyncMode(SyncMode::FullSync),
    m_lastSyncTime(0),
    m_lastUpdateCount(0),
    m_onceSyncDone(false),
    m_lastUsnOnStart(-1),
    m_lastSyncChunksDownloadedUsn(-1),
    m_syncChunksDownloaded(false),
    m_fullNoteContentsDownloaded(false),
    m_expungedFromServerToClient(false),
    m_linkedNotebooksSyncChunksDownloaded(false),
    m_active(false),
    m_paused(false),
    m_requestedToStop(false),
    m_edamProtocolVersionChecked(false),
    m_syncChunks(),
    m_linkedNotebookSyncChunks(),
    m_linkedNotebookGuidsForWhichSyncChunksWereDownloaded(),
    m_accountLimits(),
    m_tags(),
    m_expungedTags(),
    m_tagsToAddPerRequestId(),
    m_findTagByNameRequestIds(),
    m_findTagByGuidRequestIds(),
    m_addTagRequestIds(),
    m_updateTagRequestIds(),
    m_expungeTagRequestIds(),
    m_tagSyncConflictResolutionCache(m_localStorageManagerAsync),
    m_linkedNotebookGuidsByTagGuids(),
    m_expungeNotelessTagsRequestId(),
    m_savedSearches(),
    m_expungedSavedSearches(),
    m_savedSearchesToAddPerRequestId(),
    m_findSavedSearchByNameRequestIds(),
    m_findSavedSearchByGuidRequestIds(),
    m_addSavedSearchRequestIds(),
    m_updateSavedSearchRequestIds(),
    m_expungeSavedSearchRequestIds(),
    m_savedSearchSyncConflictResolutionCache(m_localStorageManagerAsync),
    m_linkedNotebooks(),
    m_expungedLinkedNotebooks(),
    m_findLinkedNotebookRequestIds(),
    m_addLinkedNotebookRequestIds(),
    m_updateLinkedNotebookRequestIds(),
    m_expungeLinkedNotebookRequestIds(),
    m_allLinkedNotebooks(),
    m_listAllLinkedNotebooksRequestId(),
    m_allLinkedNotebooksListed(false),
    m_listAllLinkedNotebooksContext(ListLinkedNotebooksContext::StartLinkedNotebooksSync),
    m_collectHighUsnRequested(false),
    m_accountHighUsnRequestId(),
    m_accountHighUsn(-1),
    m_accountHighUsnForLinkedNotebookRequestIds(),
    m_accountHighUsnForLinkedNotebooksByLinkedNotebookGuid(),
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
    m_lastSynchronizedUsnByLinkedNotebookGuid(),
    m_lastSyncTimeByLinkedNotebookGuid(),
    m_lastUpdateCountByLinkedNotebookGuid(),
    m_notebooks(),
    m_expungedNotebooks(),
    m_notebooksToAddPerRequestId(),
    m_findNotebookByNameRequestIds(),
    m_findNotebookByGuidRequestIds(),
    m_addNotebookRequestIds(),
    m_updateNotebookRequestIds(),
    m_expungeNotebookRequestIds(),
    m_notebookSyncConflictResolutionCache(m_localStorageManagerAsync),
    m_linkedNotebookGuidsByNotebookGuids(),
    m_notes(),
    m_originalNumberOfNotes(0),
    m_numNotesDownloaded(0),
    m_expungedNotes(),
    m_findNoteByGuidRequestIds(),
    m_addNoteRequestIds(),
    m_updateNoteRequestIds(),
    m_expungeNoteRequestIds(),
    m_notesWithFindRequestIdsPerFindNotebookRequestId(),
    m_notebooksPerNoteGuids(),
    m_resources(),
    m_findResourceByGuidRequestIds(),
    m_addResourceRequestIds(),
    m_updateResourceRequestIds(),
    m_resourcesWithFindRequestIdsPerFindNoteRequestId(),
    m_inkNoteResourceDataPerFindNotebookRequestId(),
    m_resourceGuidsPendingInkNoteImageDownloadPerNoteGuid(),
    m_notesPendingInkNoteImagesDownloadByFindNotebookRequestId(),
    m_notesPendingThumbnailDownloadByFindNotebookRequestId(),
    m_notesPendingThumbnailDownloadByGuid(),
    m_resourceFoundFlagPerFindResourceRequestId(),
    m_localUidsOfElementsAlreadyAttemptedToFindByName(),
    m_guidsOfNotesPendingDownloadForAddingToLocalStorage(),
    m_guidsOfNotesPendingDownloadForUpdatingInLocalStorage(),
    m_notesToAddPerAPICallPostponeTimerId(),
    m_notesToUpdatePerAPICallPostponeTimerId(),
    m_afterUsnForSyncChunkPerAPICallPostponeTimerId(),
    m_getLinkedNotebookSyncStateBeforeStartAPICallPostponeTimerId(),
    m_downloadLinkedNotebookSyncChunkAPICallPostponeTimerId(),
    m_getSyncStateBeforeStartAPICallPostponeTimerId(0),
    m_syncUserPostponeTimerId(0),
    m_syncAccountLimitsPostponeTimerId(0),
    m_gotLastSyncParameters(false)
{}

bool RemoteToLocalSynchronizationManager::active() const
{
    return m_active;
}

void RemoteToLocalSynchronizationManager::setAccount(const Account & account)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::setAccount"));

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

    Account account(name, Account::Type::Evernote, userId, accountEnType, m_host);
    account.setEvernoteAccountLimits(m_accountLimits);
    return account;
}

bool RemoteToLocalSynchronizationManager::syncUser(const qevercloud::UserID userId, ErrorString & errorDescription,
                                                   const QString & authToken, const bool writeUserDataToLocalStorage)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::syncUser: user id = ") << userId
            << QStringLiteral(", write user data to local storage = ")
            << (writeUserDataToLocalStorage ? QStringLiteral("true") : QStringLiteral("false")));

    m_user = User();
    m_user.setId(userId);

    if (!authToken.isEmpty()) {
        m_noteStore.setAuthenticationToken(authToken);
        m_userStore.setAuthenticationToken(authToken);
    }

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

    if (!m_connectedToLocalStorage) {
        createConnections();
    }

    if (m_paused) {
        resume();   // NOTE: resume can call this method so it's necessary to return
        return;
    }

    if (!m_gotLastSyncParameters) {
        emit requestLastSyncParameters();
        return;
    }

    clear();
    m_active = true;

    ErrorString errorDescription;

    // Checking the protocol version first
    if (!checkProtocolVersion(errorDescription)) {
        emit failure(errorDescription);
        return;
    }

    bool waitIfRateLimitReached = true;

    // Retrieving the latest user info then, to figure out the service level and stuff like that
    if (!syncUserImpl(waitIfRateLimitReached, errorDescription))
    {
        if (m_syncUserPostponeTimerId == 0) {
            // Not a "rate limit exceeded" error
            emit failure(errorDescription);
        }

        return;
    }

    if (!checkAndSyncAccountLimits(waitIfRateLimitReached, errorDescription))
    {
        if (m_syncAccountLimitsPostponeTimerId == 0) {
            // Not a "rate limit exceeded" error
            emit failure(errorDescription);

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
            res = checkLinkedNotebooksSyncStates(asyncWait, error);
            if (asyncWait || error) {
                return;
            }

            if (!res) {
                QNTRACE(QStringLiteral("The service has no updates for any of linked notebooks"));
                finalize();
            }
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

    m_requestedToStop = true;
    m_active = false;
    emit stopped();
}

void RemoteToLocalSynchronizationManager::pause()
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::pause"));
    m_paused = true;
    m_active = false;
    emit paused(/* pending authentication = */ false);
}

void RemoteToLocalSynchronizationManager::resume()
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::resume"));

    if (!m_connectedToLocalStorage) {
        createConnections();
    }

    if (m_paused)
    {
        m_active = true;
        m_paused = false;

        if (m_syncChunksDownloaded && (m_lastSyncChunksDownloadedUsn >= m_lastUsnOnStart))
        {
            QNDEBUG(QStringLiteral("last USN for which sync chunks were downloaded is ") << m_lastSyncChunksDownloadedUsn
                    << QStringLiteral(", there's no need to download the sync chunks again, launching the sync procedure"));

            if (m_fullNoteContentsDownloaded)
            {
                QNDEBUG(QStringLiteral("Full note's contents have already been downloaded meaning that the sync for "
                                       "tags, saved searches, notebooks and notes from user's account is over"));

                if (m_linkedNotebooksSyncChunksDownloaded) {
                    QNDEBUG(QStringLiteral("sync chunks for linked notebooks were already downloaded, there's no need to "
                                           "do it again, will launch the sync for linked notebooks"));
                    launchLinkedNotebooksContentsSync();
                }
                else {
                    startLinkedNotebooksSync();
                }
            }
            else
            {
                launchSync();
            }
        }
        else
        {
            start(m_lastUsnOnStart);
        }
    }
}

template <>
void RemoteToLocalSynchronizationManager::emitAddRequest<Tag>(const Tag & tag)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::emitAddRequest<Tag>: ") << tag);

    QUuid addTagRequestId = QUuid::createUuid();
    Q_UNUSED(m_addTagRequestIds.insert(addTagRequestId));
    QNTRACE(QStringLiteral("Emitting the request to add tag to local storage: request id = ")
            << addTagRequestId << QStringLiteral(", tag: ") << tag);
    emit addTag(tag, addTagRequestId);
}

template <>
void RemoteToLocalSynchronizationManager::emitAddRequest<SavedSearch>(const SavedSearch & search)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::emitAddRequest<SavedSearch>: ") << search);

    QUuid addSavedSearchRequestId = QUuid::createUuid();
    Q_UNUSED(m_addSavedSearchRequestIds.insert(addSavedSearchRequestId));
    QNTRACE(QStringLiteral("Emitting the request to add saved search to local storage: request id = ")
            << addSavedSearchRequestId << QStringLiteral(", saved search: ") << search);
    emit addSavedSearch(search, addSavedSearchRequestId);
}

template <>
void RemoteToLocalSynchronizationManager::emitAddRequest<LinkedNotebook>(const LinkedNotebook & linkedNotebook)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::emitAddRequest<LinkedNotebook>: ") << linkedNotebook);

    QUuid addLinkedNotebookRequestId = QUuid::createUuid();
    Q_UNUSED(m_addLinkedNotebookRequestIds.insert(addLinkedNotebookRequestId));
    QNTRACE(QStringLiteral("Emitting the request to add linked notebook to local storage: request id = ")
            << addLinkedNotebookRequestId << QStringLiteral(", linked notebook: ") << linkedNotebook);
    emit addLinkedNotebook(linkedNotebook, addLinkedNotebookRequestId);
}

template <>
void RemoteToLocalSynchronizationManager::emitAddRequest<Notebook>(const Notebook & notebook)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::emitAddRequest<Notebook>: ") << notebook);

    QUuid addNotebookRequestId = QUuid::createUuid();
    Q_UNUSED(m_addNotebookRequestIds.insert(addNotebookRequestId));
    QNTRACE(QStringLiteral("Emitting the request to add notebook to local storage: request id = ")
            << addNotebookRequestId << QStringLiteral(", notebook: ") << notebook);
    emit addNotebook(notebook, addNotebookRequestId);
}

template <>
void RemoteToLocalSynchronizationManager::emitAddRequest<Note>(const Note & note)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::emitAddRequest<Note>: ") << note);

    QUuid addNoteRequestId = QUuid::createUuid();
    Q_UNUSED(m_addNoteRequestIds.insert(addNoteRequestId));
    QNTRACE(QStringLiteral("Emitting the request to add note to local storage: request id = ")
            << addNoteRequestId << QStringLiteral(", note: ") << note);
    emit addNote(note, addNoteRequestId);
}

template <>
void RemoteToLocalSynchronizationManager::emitAddRequest<Resource>(const Resource & resource)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::emitAddRequest<Resource>: ") << resource);

    QString resourceGuid = (resource.hasGuid() ? resource.guid() : QString());
    QString resourceLocalUid = resource.localUid();
    QPair<QString,QString> key(resourceGuid, resourceLocalUid);

    QUuid addResourceRequestId = QUuid::createUuid();
    Q_UNUSED(m_addResourceRequestIds.insert(addResourceRequestId));
    QNTRACE(QStringLiteral("Emitting the request to add resource to local storage: request id = ")
            << addResourceRequestId << QStringLiteral(", resource: ") << resource);
    emit addResource(resource, addResourceRequestId);
}

template <>
void RemoteToLocalSynchronizationManager::emitUpdateRequest<Tag>(const Tag & tag,
                                                                 const Tag * tagToAddLater)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::emitUpdateRequest<Tag>: tag = ")
            << tag << QStringLiteral(", tagToAddLater = ") << (tagToAddLater ? tagToAddLater->toString() : QStringLiteral("<null>")));

    QUuid updateTagRequestId = QUuid::createUuid();
    Q_UNUSED(m_updateTagRequestIds.insert(updateTagRequestId));

    if (tagToAddLater) {
        m_tagsToAddPerRequestId[updateTagRequestId] = *tagToAddLater;
    }

    emit updateTag(tag, updateTagRequestId);
}

template <>
void RemoteToLocalSynchronizationManager::emitUpdateRequest<SavedSearch>(const SavedSearch & search,
                                                                         const SavedSearch * searchToAddLater)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::emitUpdateRequest<SavedSearch>: search = ")
            << search << QStringLiteral(", searchToAddLater = ") << (searchToAddLater ? searchToAddLater->toString() : QStringLiteral("<null>")));

    QUuid updateSavedSearchRequestId = QUuid::createUuid();
    Q_UNUSED(m_updateSavedSearchRequestIds.insert(updateSavedSearchRequestId));

    if (searchToAddLater) {
        m_savedSearchesToAddPerRequestId[updateSavedSearchRequestId] = *searchToAddLater;
    }

    emit updateSavedSearch(search, updateSavedSearchRequestId);
}

template <>
void RemoteToLocalSynchronizationManager::emitUpdateRequest<LinkedNotebook>(const LinkedNotebook & linkedNotebook,
                                                                            const LinkedNotebook *)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::emitUpdateRequest<LinkedNotebook>: linked notebook = ")
            << linkedNotebook);

    QUuid updateLinkedNotebookRequestId = QUuid::createUuid();
    Q_UNUSED(m_updateLinkedNotebookRequestIds.insert(updateLinkedNotebookRequestId));
    emit updateLinkedNotebook(linkedNotebook, updateLinkedNotebookRequestId);
}

template <>
void RemoteToLocalSynchronizationManager::emitUpdateRequest<Notebook>(const Notebook & notebook,
                                                                      const Notebook * notebookToAddLater)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::emitUpdateRequest<Notebook>: notebook = ")
            << notebook << QStringLiteral(", notebook to add later = ") << (notebookToAddLater ? notebookToAddLater->toString() : QStringLiteral("<null>")));

    QUuid updateNotebookRequestId = QUuid::createUuid();
    Q_UNUSED(m_updateNotebookRequestIds.insert(updateNotebookRequestId));

    if (notebookToAddLater) {
        m_notebooksToAddPerRequestId[updateNotebookRequestId] = *notebookToAddLater;
    }

    emit updateNotebook(notebook, updateNotebookRequestId);
}

template <>
void RemoteToLocalSynchronizationManager::emitUpdateRequest<Note>(const Note & note, const Note * pNoteToAdd)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::emitUpdateRequest<Note>: note = ") << note
            << QStringLiteral("\nNote to add: ") << (pNoteToAdd ? pNoteToAdd->toString() : QStringLiteral("<null>")));

    getFullNoteDataAsyncAndUpdateInLocalStorage(note);

    if (pNoteToAdd) {
        emitAddRequest(*pNoteToAdd);
    }
}

#define CHECK_PAUSED() \
    if (m_paused && !m_requestedToStop) { \
        QNDEBUG(QStringLiteral("RemoteTolocalSynchronizationManager is being paused, returning without any actions")); \
        return; \
    }

#define CHECK_STOPPED() \
    if (m_requestedToStop && !hasPendingRequests()) { \
        QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager is requested to stop and has no pending requests, " \
                               "finishing the synchronization")); \
        finalize(); \
        return; \
    }

void RemoteToLocalSynchronizationManager::onFindUserCompleted(User user, QUuid requestId)
{
    CHECK_PAUSED();

    if (requestId != m_findUserRequestId) {
        return;
    }

    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onFindUserCompleted: user = ") << user
            << QStringLiteral("\nRequest id = ") << requestId);

    m_user = user;
    m_findUserRequestId = QUuid();

    CHECK_STOPPED();

    // Updating the user info as user was found in the local storage
    m_addOrUpdateUserRequestId = QUuid::createUuid();
    QNTRACE(QStringLiteral("Emitting the request to update user in the local storage database: request id = ")
            << m_addOrUpdateUserRequestId << QStringLiteral(", user = ") << m_user);
    emit updateUser(m_user, m_addOrUpdateUserRequestId);
}

void RemoteToLocalSynchronizationManager::onFindUserFailed(User user, ErrorString errorDescription, QUuid requestId)
{
    CHECK_PAUSED();

    if (requestId != m_findUserRequestId) {
        return;
    }

    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onFindUserFailed: user = ") << user
            << QStringLiteral("\nError description = ") << errorDescription << QStringLiteral(", request id = ")
            << requestId);

    m_findUserRequestId = QUuid();
    CHECK_STOPPED();

    // Adding the user info as user was not found in the local storage
    m_addOrUpdateUserRequestId = QUuid::createUuid();
    QNTRACE(QStringLiteral("Emitting the request to add user to the local storage database: request id = ")
            << m_addOrUpdateUserRequestId << QStringLiteral(", user = ") << m_user);
    emit addUser(m_user, m_addOrUpdateUserRequestId);
}

void RemoteToLocalSynchronizationManager::onFindNotebookCompleted(Notebook notebook, QUuid requestId)
{
    QNTRACE(QStringLiteral("RemoteToLocalSynchronizationManager::onFindNotebookCompleted: request id = ")
            << requestId << QStringLiteral(", notebook: ") << notebook);

    CHECK_PAUSED();

    bool foundByGuid = onFoundDuplicateByGuid(notebook, requestId, QStringLiteral("Notebook"),
                                              m_notebooks, m_findNotebookByGuidRequestIds);
    if (foundByGuid) {
        CHECK_STOPPED();
        return;
    }

    bool foundByName = onFoundDuplicateByName(notebook, requestId, QStringLiteral("Notebook"),
                                              m_notebooks, m_findNotebookByNameRequestIds);
    if (foundByName) {
        CHECK_STOPPED();
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

        m_notebooksPerNoteGuids[key] = notebook;

        Q_UNUSED(onFoundDuplicateByGuid(note, findNoteRequestId, QStringLiteral("Note"), m_notes, m_findNoteByGuidRequestIds));
        CHECK_STOPPED();
        return;
    }

    auto iit = m_inkNoteResourceDataPerFindNotebookRequestId.find(requestId);
    if (iit != m_inkNoteResourceDataPerFindNotebookRequestId.end())
    {
        QNDEBUG(QStringLiteral("Found notebook for ink note image downloading for note resource"));

        InkNoteResourceData resourceData = iit.value();
        Q_UNUSED(m_inkNoteResourceDataPerFindNotebookRequestId.erase(iit))

        CHECK_STOPPED();

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

        CHECK_STOPPED();

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

        CHECK_STOPPED();

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

    CHECK_PAUSED();

    bool failedToFindByGuid = onNoDuplicateByGuid(notebook, requestId, errorDescription, QStringLiteral("Notebook"),
                                                  m_notebooks, m_findNotebookByGuidRequestIds);
    if (failedToFindByGuid) {
        CHECK_STOPPED();
        return;
    }

    bool failedToFindByName = onNoDuplicateByName(notebook, requestId, errorDescription, QStringLiteral("Notebook"),
                                                  m_notebooks, m_findNotebookByNameRequestIds);
    if (failedToFindByName) {
        CHECK_STOPPED();
        return;
    }

    NoteDataPerFindNotebookRequestId::iterator rit = m_notesWithFindRequestIdsPerFindNotebookRequestId.find(requestId);
    if (rit != m_notesWithFindRequestIdsPerFindNotebookRequestId.end())
    {
        ErrorString errorDescription(QT_TR_NOOP("Failed to find the notebook for one of synchronized notes"));
        QNWARNING(errorDescription << QStringLiteral(": ") << notebook);
        emit failure(errorDescription);
        return;
    }

    auto iit = m_inkNoteResourceDataPerFindNotebookRequestId.find(requestId);
    if (iit != m_inkNoteResourceDataPerFindNotebookRequestId.end())
    {
        Q_UNUSED(m_inkNoteResourceDataPerFindNotebookRequestId.erase(iit))
        QNWARNING(QStringLiteral("Can't find the notebook for the purpose of setting up the ink note image downloading"));
        CHECK_STOPPED();
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
        CHECK_STOPPED();

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
        CHECK_STOPPED();

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
    CHECK_PAUSED();

    Q_UNUSED(withResourceBinaryData);

    QSet<QUuid>::iterator it = m_findNoteByGuidRequestIds.find(requestId);
    if (it != m_findNoteByGuidRequestIds.end())
    {
        QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onFindNoteCompleted: note = ")
                << note << QStringLiteral(", requestId = ") << requestId);

        // NOTE: erase is required for proper work of the macro; the request would be re-inserted below if macro doesn't return from the method
        Q_UNUSED(m_findNoteByGuidRequestIds.erase(it));

        CHECK_STOPPED();

        // Need to find Notebook corresponding to the note in order to proceed
        if (!note.hasNotebookGuid())
        {
            ErrorString errorDescription(QT_TR_NOOP("Found duplicate note in the local storage which doesn't have "
                                                    "a notebook guid"));
            APPEND_NOTE_DETAILS(errorDescription, note)

            QNWARNING(errorDescription << QStringLiteral(": ") << note);
            emit failure(errorDescription);
            return;
        }

        QUuid findNotebookPerNoteRequestId = QUuid::createUuid();
        m_notesWithFindRequestIdsPerFindNotebookRequestId[findNotebookPerNoteRequestId] =
                QPair<Note,QUuid>(note, requestId);

        Notebook notebookToFind;
        notebookToFind.unsetLocalUid();
        notebookToFind.setGuid(note.notebookGuid());

        Q_UNUSED(m_findNoteByGuidRequestIds.insert(requestId));

        emit findNotebook(notebookToFind, findNotebookPerNoteRequestId);
        return;
    }

    ResourceDataPerFindNoteRequestId::iterator rit = m_resourcesWithFindRequestIdsPerFindNoteRequestId.find(requestId);
    if (rit != m_resourcesWithFindRequestIdsPerFindNoteRequestId.end())
    {
        QPair<Resource,QUuid> resourceWithFindRequestId = rit.value();

        Q_UNUSED(m_resourcesWithFindRequestIdsPerFindNoteRequestId.erase(rit))

        CHECK_STOPPED();

        if (Q_UNLIKELY(!note.hasGuid())) {
            ErrorString errorDescription(QT_TR_NOOP("Found the note necessary for the resource synchronization "
                                                    "but it doesn't have a guid"));
            APPEND_NOTE_DETAILS(errorDescription, note)

            QNWARNING(errorDescription << QStringLiteral(": ") << note);
            emit failure(errorDescription);
            return;
        }

        const Notebook * pNotebook = getNotebookPerNote(note);

        const Resource & resource = resourceWithFindRequestId.first;
        const QUuid & findResourceRequestId = resourceWithFindRequestId.second;

        if (shouldDownloadThumbnailsForNotes())
        {
            auto noteThumbnailDownloadIt = m_notesPendingThumbnailDownloadByGuid.find(note.guid());
            if (noteThumbnailDownloadIt == m_notesPendingThumbnailDownloadByGuid.end())
            {
                QNDEBUG(QStringLiteral("Need to downloading the thumbnail for the note with added or updated resource"));

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
                emit findNotebook(dummyNotebook, findNotebookForInkNoteSetupRequestId);
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
            emitAddRequest(resource);
            return;
        }

        if (!resource.isDirty()) {
            QNDEBUG(QStringLiteral("Found duplicate resource in local storage which is not marked dirty => "
                                   "overriding it with the version received from the remote storage"));
            QUuid updateResourceRequestId = QUuid::createUuid();
            Q_UNUSED(m_updateResourceRequestIds.insert(updateResourceRequestId))
            emit updateResource(resource, updateResourceRequestId);
            return;
        }

        QNDEBUG(QStringLiteral("Found duplicate resource in local storage which is marked dirty => "
                               "will treat it as a conflict of notes"));

        Note conflictingNote = createConflictingNote(note);

        Note updatedNote(note);
        bool hasResources = updatedNote.hasResources();
        QList<Resource> resources;
        if (hasResources) {
            resources = updatedNote.resources();
        }

        int numResources = resources.size();
        int resourceIndex = -1;
        for(int i = 0; i < numResources; ++i)
        {
            const Resource & existingResource = resources[i];
            if (existingResource.hasGuid() && (existingResource.guid() == resource.guid())) {
                resourceIndex = i;
                break;
            }
        }

        if (resourceIndex < 0)
        {
            updatedNote.addResource(resource);
        }
        else
        {
            QList<Resource> noteResources = updatedNote.resources();
            noteResources[resourceIndex] = resource;
            updatedNote.setResources(noteResources);
        }

        emitUpdateRequest(updatedNote, &conflictingNote);
    }
}

void RemoteToLocalSynchronizationManager::onFindNoteFailed(Note note, bool withResourceBinaryData,
                                                           ErrorString errorDescription, QUuid requestId)
{
    CHECK_PAUSED();

    Q_UNUSED(withResourceBinaryData)
    Q_UNUSED(errorDescription)

    QSet<QUuid>::iterator it = m_findNoteByGuidRequestIds.find(requestId);
    if (it != m_findNoteByGuidRequestIds.end())
    {
        QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onFindNoteFailed: note = ") << note
                << QStringLiteral(", requestId = ") << requestId);

        Q_UNUSED(m_findNoteByGuidRequestIds.erase(it));

        CHECK_STOPPED();

        auto it = findItemByGuid(m_notes, note, QStringLiteral("Note"));
        if (it == m_notes.end()) {
            return;
        }

        // Removing the note from the list of notes waiting for processing
        Q_UNUSED(m_notes.erase(it));

        getFullNoteDataAsyncAndAddToLocalStorage(note);
        return;
    }

    ResourceDataPerFindNoteRequestId::iterator rit = m_resourcesWithFindRequestIdsPerFindNoteRequestId.find(requestId);
    if (rit != m_resourcesWithFindRequestIdsPerFindNoteRequestId.end())
    {
        Q_UNUSED(m_resourcesWithFindRequestIdsPerFindNoteRequestId.erase(rit));

        CHECK_STOPPED();

        ErrorString errorDescription(QT_TR_NOOP("Can't find a note containing the synchronized resource in the local storage"));
        APPEND_NOTE_DETAILS(errorDescription, note)

        QNWARNING(errorDescription << QStringLiteral(", note attempted to be found: ") << note);
        emit failure(errorDescription);
        return;
    }
}

void RemoteToLocalSynchronizationManager::onFindTagCompleted(Tag tag, QUuid requestId)
{
    CHECK_PAUSED();

    bool foundByGuid = onFoundDuplicateByGuid(tag, requestId, QStringLiteral("Tag"), m_tags, m_findTagByGuidRequestIds);
    if (foundByGuid) {
        CHECK_STOPPED();
        return;
    }

    bool foundByName = onFoundDuplicateByName(tag, requestId, QStringLiteral("Tag"), m_tags, m_findTagByNameRequestIds);
    if (foundByName) {
        CHECK_STOPPED();
        return;
    }
}

void RemoteToLocalSynchronizationManager::onFindTagFailed(Tag tag, ErrorString errorDescription, QUuid requestId)
{
    CHECK_PAUSED();

    bool failedToFindByGuid = onNoDuplicateByGuid(tag, requestId, errorDescription, QStringLiteral("Tag"),
                                                  m_tags, m_findTagByGuidRequestIds);
    if (failedToFindByGuid) {
        CHECK_STOPPED();
        return;
    }

    bool failedToFindByName = onNoDuplicateByName(tag, requestId, errorDescription, QStringLiteral("Tag"),
                                                  m_tags, m_findTagByNameRequestIds);
    if (failedToFindByName) {
        CHECK_STOPPED();
        return;
    }
}

void RemoteToLocalSynchronizationManager::onFindResourceCompleted(Resource resource,
                                                                  bool withResourceBinaryData, QUuid requestId)
{
    CHECK_PAUSED();

    Q_UNUSED(withResourceBinaryData)

    QSet<QUuid>::iterator rit = m_findResourceByGuidRequestIds.find(requestId);
    if (rit != m_findResourceByGuidRequestIds.end())
    {
        QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onFindResourceCompleted: resource = ")
                << resource << QStringLiteral(", requestId = ") << requestId);

        Q_UNUSED(m_findResourceByGuidRequestIds.erase(rit))

        CHECK_STOPPED();

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
            emit failure(errorDescription);
            return;
        }

        Q_UNUSED(m_resourceFoundFlagPerFindResourceRequestId.insert(requestId))

        QUuid findNotePerResourceRequestId = QUuid::createUuid();
        m_resourcesWithFindRequestIdsPerFindNoteRequestId[findNotePerResourceRequestId] =
            QPair<Resource,QUuid>(resource, requestId);

        Note noteToFind;
        noteToFind.unsetLocalUid();
        noteToFind.setGuid(resource.noteGuid());

        emit findNote(noteToFind, /* with resource binary data = */ true, findNotePerResourceRequestId);
        return;
    }
}

void RemoteToLocalSynchronizationManager::onFindResourceFailed(Resource resource,
                                                               bool withResourceBinaryData,
                                                               ErrorString errorDescription, QUuid requestId)
{
    CHECK_PAUSED();

    Q_UNUSED(withResourceBinaryData)

    QSet<QUuid>::iterator rit = m_findResourceByGuidRequestIds.find(requestId);
    if (rit != m_findResourceByGuidRequestIds.end())
    {
        QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onFindResourceFailed: resource = ")
                << resource << QStringLiteral(", requestId = ") << requestId);

        Q_UNUSED(m_findResourceByGuidRequestIds.erase(rit))

        CHECK_STOPPED();

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
            emit failure(errorDescription);
            return;
        }

        QUuid findNotePerResourceRequestId = QUuid::createUuid();
        m_resourcesWithFindRequestIdsPerFindNoteRequestId[findNotePerResourceRequestId] =
            QPair<Resource,QUuid>(resource, requestId);

        Note noteToFind;
        noteToFind.unsetLocalUid();
        noteToFind.setGuid(resource.noteGuid());

        emit findNote(noteToFind, /* with resource binary data = */ false, findNotePerResourceRequestId);
        return;
    }
}

void RemoteToLocalSynchronizationManager::onFindLinkedNotebookCompleted(LinkedNotebook linkedNotebook,
                                                                        QUuid requestId)
{
    CHECK_PAUSED();

    Q_UNUSED(onFoundDuplicateByGuid(linkedNotebook, requestId, QStringLiteral("LinkedNotebook"),
                                    m_linkedNotebooks, m_findLinkedNotebookRequestIds));

    CHECK_STOPPED();
}

void RemoteToLocalSynchronizationManager::onFindLinkedNotebookFailed(LinkedNotebook linkedNotebook,
                                                                     ErrorString errorDescription, QUuid requestId)
{
    CHECK_PAUSED();

    QSet<QUuid>::iterator rit = m_findLinkedNotebookRequestIds.find(requestId);
    if (rit == m_findLinkedNotebookRequestIds.end()) {
        return;
    }

    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onFindLinkedNotebookFailed: ")
            << linkedNotebook << QStringLiteral(", errorDescription = ") << errorDescription
            << QStringLiteral(", requestId = ") << requestId);

    Q_UNUSED(m_findLinkedNotebookRequestIds.erase(rit));

    CHECK_STOPPED();

    LinkedNotebooksList::iterator it = findItemByGuid(m_linkedNotebooks, linkedNotebook, QStringLiteral("LinkedNotebook"));
    if (it == m_linkedNotebooks.end()) {
        return;
    }

    // This linked notebook was not found in the local storage by guid, adding it there
    emitAddRequest(linkedNotebook);
}

void RemoteToLocalSynchronizationManager::onFindSavedSearchCompleted(SavedSearch savedSearch, QUuid requestId)
{
    CHECK_PAUSED();

    bool foundByGuid = onFoundDuplicateByGuid(savedSearch, requestId, QStringLiteral("SavedSearch"),
                                              m_savedSearches, m_findSavedSearchByGuidRequestIds);
    if (foundByGuid) {
        CHECK_STOPPED();
        return;
    }

    bool foundByName = onFoundDuplicateByName(savedSearch, requestId, QStringLiteral("SavedSearch"),
                                              m_savedSearches, m_findSavedSearchByNameRequestIds);
    if (foundByName) {
        CHECK_STOPPED();
        return;
    }
}

void RemoteToLocalSynchronizationManager::onFindSavedSearchFailed(SavedSearch savedSearch,
                                                                  ErrorString errorDescription, QUuid requestId)
{
    CHECK_PAUSED();

    bool failedToFindByGuid = onNoDuplicateByGuid(savedSearch, requestId, errorDescription, QStringLiteral("SavedSearch"),
                                                  m_savedSearches, m_findSavedSearchByGuidRequestIds);
    if (failedToFindByGuid) {
        CHECK_STOPPED();
        return;
    }

    bool failedToFindByName = onNoDuplicateByName(savedSearch, requestId, errorDescription, QStringLiteral("SavedSearch"),
                                                  m_savedSearches, m_findSavedSearchByNameRequestIds);
    if (failedToFindByName) {
        CHECK_STOPPED();
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

        CHECK_PAUSED();
        CHECK_STOPPED();

        performPostAddOrUpdateChecks<ElementType>();

        checkServerDataMergeCompletion();
    }
}

void RemoteToLocalSynchronizationManager::onAddUserCompleted(User user, QUuid requestId)
{
    CHECK_PAUSED();

    if (requestId != m_addOrUpdateUserRequestId) {
        return;
    }

    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onAddUserCompleted: user = ") << user
            << QStringLiteral("\nRequest id = ") << requestId);

    m_addOrUpdateUserRequestId = QUuid();
    m_onceAddedOrUpdatedUserInLocalStorage = true;
    CHECK_STOPPED();
}

void RemoteToLocalSynchronizationManager::onAddUserFailed(User user, ErrorString errorDescription, QUuid requestId)
{
    CHECK_PAUSED();

    if (requestId != m_addOrUpdateUserRequestId) {
        return;
    }

    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onAddUserFailed: ") << user
            << QStringLiteral("\nRequest id = ") << requestId);

    ErrorString error(QT_TR_NOOP("Failed to add the user data fetched from the remote database to the local storage"));
    error.additionalBases().append(errorDescription.base());
    error.additionalBases().append(errorDescription.additionalBases());
    error.details() = errorDescription.details();
    emit failure(error);

    m_addOrUpdateUserRequestId = QUuid();
    CHECK_STOPPED();
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
                  << QStringLiteral(">: ") << typeName << QStringLiteral(" = ") << element << QStringLiteral(", error description = ")
                  << errorDescription << QStringLiteral(", requestId = ") << requestId);

        Q_UNUSED(addElementRequestIds.erase(it));

        ErrorString error(QT_TR_NOOP("Failed to add the data item fetched from the remote database to the local storage"));
        error.additionalBases().append(errorDescription.base());
        error.additionalBases().append(errorDescription.additionalBases());
        error.details() = errorDescription.details();
        QNWARNING(error);
        emit failure(error);
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
    CHECK_PAUSED();

    if (requestId != m_addOrUpdateUserRequestId)
    {
        if (user.hasId() && m_user.hasId() && (user.id() == m_user.id())) {
            QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onUpdateUserCompleted: external update of current user, request id = ")
                    << requestId);
            m_user = user;
            CHECK_STOPPED();
        }

        return;
    }

    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onUpdateUserCompleted: user = ") << user
            << QStringLiteral("\nRequest id = ") << requestId);

    m_addOrUpdateUserRequestId = QUuid();
    m_onceAddedOrUpdatedUserInLocalStorage = true;
    CHECK_STOPPED();
}

void RemoteToLocalSynchronizationManager::onUpdateUserFailed(User user, ErrorString errorDescription, QUuid requestId)
{
    CHECK_PAUSED();

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
    emit failure(error);

    m_addOrUpdateUserRequestId = QUuid();
    CHECK_STOPPED();
}

template <class ElementType, class ElementsToAddByUuid>
void RemoteToLocalSynchronizationManager::onUpdateDataElementCompleted(const ElementType & element,
                                                                       const QUuid & requestId, const QString & typeName,
                                                                       QSet<QUuid> & updateElementRequestIds,
                                                                       ElementsToAddByUuid & elementsToAddByRenameRequestId)
{
    QSet<QUuid>::iterator rit = updateElementRequestIds.find(requestId);
    if (rit == updateElementRequestIds.end()) {
        return;
    }

    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizartionManager::onUpdateDataElementCompleted<") << typeName
            << QStringLiteral(">: ") << typeName << QStringLiteral(" = ") << element << QStringLiteral(", requestId = ") << requestId);

    Q_UNUSED(updateElementRequestIds.erase(rit));

    typename ElementsToAddByUuid::iterator addIt = elementsToAddByRenameRequestId.find(requestId);
    if (addIt != elementsToAddByRenameRequestId.end())
    {
        ElementType & elementToAdd = addIt.value();
        const QString localUid = elementToAdd.localUid();
        if (localUid.isEmpty()) {
            ErrorString errorDescription(QT_TR_NOOP("Internal error: detected a data item with empty local uid in the local storage"));
            errorDescription.details() = QStringLiteral("item type = ");
            errorDescription.details() += typeName;
            QNWARNING(errorDescription << QStringLiteral(": ") << elementToAdd);
            emit failure(errorDescription);
            return;
        }

        QSet<QString>::iterator git = m_localUidsOfElementsAlreadyAttemptedToFindByName.find(localUid);
        if (git == m_localUidsOfElementsAlreadyAttemptedToFindByName.end())
        {
            QNDEBUG(QStringLiteral("Checking whether ") << typeName << QStringLiteral(" with the same name already exists in local storage"));
            Q_UNUSED(m_localUidsOfElementsAlreadyAttemptedToFindByName.insert(localUid));

            elementToAdd.unsetLocalUid();
            elementToAdd.setGuid(QStringLiteral(""));
            emitFindByNameRequest(elementToAdd);
        }
        else
        {
            QNDEBUG(QStringLiteral("Adding ") << typeName << QStringLiteral(" to local storage"));
            emitAddRequest(elementToAdd);
        }

        Q_UNUSED(elementsToAddByRenameRequestId.erase(addIt));
    }
    else {
        CHECK_PAUSED();
        CHECK_STOPPED();
        performPostAddOrUpdateChecks<ElementType>();
    }

    checkServerDataMergeCompletion();
}

void RemoteToLocalSynchronizationManager::onUpdateTagCompleted(Tag tag, QUuid requestId)
{
    onUpdateDataElementCompleted(tag, requestId, QStringLiteral("Tag"), m_updateTagRequestIds, m_tagsToAddPerRequestId);
}

void RemoteToLocalSynchronizationManager::onUpdateSavedSearchCompleted(SavedSearch search, QUuid requestId)
{
    onUpdateDataElementCompleted(search, requestId, QStringLiteral("SavedSearch"), m_updateSavedSearchRequestIds,
                                 m_savedSearchesToAddPerRequestId);
}

template <class ElementType, class ElementsToAddByUuid>
void RemoteToLocalSynchronizationManager::onUpdateDataElementFailed(const ElementType & element, const QUuid & requestId,
                                                                    const ErrorString & errorDescription, const QString & typeName,
                                                                    QSet<QUuid> & updateElementRequestIds,
                                                                    ElementsToAddByUuid & elementsToAddByRenameRequestId)
{
    QSet<QUuid>::iterator it = updateElementRequestIds.find(requestId);
    if (it == updateElementRequestIds.end()) {
        return;
    }

    QNWARNING(QStringLiteral("RemoteToLocalSynchronizationManager::onUpdateDataElementFailed<") << typeName
              << QStringLiteral(">: ") << typeName << QStringLiteral(" = ") << element << QStringLiteral(", errorDescription = ")
              << errorDescription << QStringLiteral(", requestId = ") << requestId);

    Q_UNUSED(updateElementRequestIds.erase(it));

    ErrorString error;
    typename ElementsToAddByUuid::iterator addIt = elementsToAddByRenameRequestId.find(requestId);
    if (addIt != elementsToAddByRenameRequestId.end()) {
        Q_UNUSED(elementsToAddByRenameRequestId.erase(addIt));
        error.setBase(QT_TR_NOOP("Can't rename the local dirty duplicate item in the local storage"));
    }
    else {
        error.setBase(QT_TR_NOOP("Can't update the item in the local storage"));
    }

    error.additionalBases().append(errorDescription.base());
    error.additionalBases().append(errorDescription.additionalBases());
    error.details() = errorDescription.details();
    emit failure(error);
}

void RemoteToLocalSynchronizationManager::onUpdateTagFailed(Tag tag, ErrorString errorDescription, QUuid requestId)
{
    onUpdateDataElementFailed(tag, requestId, errorDescription, QStringLiteral("Tag"), m_updateTagRequestIds,
                              m_tagsToAddPerRequestId);
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
    emit failure(error);
}

void RemoteToLocalSynchronizationManager::onUpdateSavedSearchFailed(SavedSearch search, ErrorString errorDescription,
                                                                    QUuid requestId)
{
    onUpdateDataElementFailed(search, requestId, errorDescription, QStringLiteral("SavedSearch"), m_updateSavedSearchRequestIds,
                              m_savedSearchesToAddPerRequestId);
}

void RemoteToLocalSynchronizationManager::onExpungeSavedSearchCompleted(SavedSearch search, QUuid requestId)
{
    onExpungeDataElementCompleted(search, requestId, QStringLiteral("SavedSearch"), m_expungeSavedSearchRequestIds);
}

void RemoteToLocalSynchronizationManager::onExpungeSavedSearchFailed(SavedSearch search, ErrorString errorDescription, QUuid requestId)
{
    onExpungeDataElementFailed(search, requestId, errorDescription, QStringLiteral("SavedSearch"), m_expungeSavedSearchRequestIds);
}

template <class ElementType>
void RemoteToLocalSynchronizationManager::performPostAddOrUpdateChecks()
{
    // do nothing
}

template <>
void RemoteToLocalSynchronizationManager::performPostAddOrUpdateChecks<Tag>()
{
    checkNotebooksAndTagsSyncAndLaunchNotesSync();

    if (m_addTagRequestIds.empty() && m_updateTagRequestIds.empty()) {
        expungeTags();
    }
}

template <>
void RemoteToLocalSynchronizationManager::performPostAddOrUpdateChecks<Notebook>()
{
    checkNotebooksAndTagsSyncAndLaunchNotesSync();
}

template <>
void RemoteToLocalSynchronizationManager::performPostAddOrUpdateChecks<Note>()
{
    checkNotesSyncAndLaunchResourcesSync();

    if (m_addNoteRequestIds.empty() && m_updateNoteRequestIds.empty() &&
        m_notesToAddPerAPICallPostponeTimerId.empty() && m_notesToUpdatePerAPICallPostponeTimerId.empty())
    {
        if (!m_resources.empty()) {
            return;
        }

        if (!m_expungedNotes.empty()) {
            expungeNotes();
        }
        else if (!m_expungedNotebooks.empty()) {
            expungeNotebooks();
        }
        else {
            expungeLinkedNotebooks();
        }
    }
}

template <>
void RemoteToLocalSynchronizationManager::performPostAddOrUpdateChecks<Resource>()
{
    if (m_addResourceRequestIds.empty() && m_updateResourceRequestIds.empty() &&
        m_resourcesWithFindRequestIdsPerFindNoteRequestId.empty() &&
        m_inkNoteResourceDataPerFindNotebookRequestId.empty() &&
        m_resourceGuidsPendingInkNoteImageDownloadPerNoteGuid.empty() &&
        m_notesPendingInkNoteImagesDownloadByFindNotebookRequestId.empty() &&
        m_notesPendingThumbnailDownloadByFindNotebookRequestId.empty() &&
        m_notesPendingThumbnailDownloadByGuid.empty())
    {
        if (!m_expungedNotes.empty()) {
            expungeNotes();
        }
        else if (!m_expungedNotebooks.empty()) {
            expungeNotebooks();
        }
        else {
            expungeLinkedNotebooks();
        }
    }
}

template <>
void RemoteToLocalSynchronizationManager::performPostAddOrUpdateChecks<SavedSearch>()
{
    if (m_addSavedSearchRequestIds.empty() && m_updateSavedSearchRequestIds.empty()) {
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
    if (m_expungedTags.empty()) {
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
        emit expungeTag(tagToExpunge, expungeTagRequestId);
    }

    m_expungedTags.clear();
}

void RemoteToLocalSynchronizationManager::expungeSavedSearches()
{
    if (m_expungedSavedSearches.empty()) {
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
        emit expungeSavedSearch(searchToExpunge, expungeSavedSearchRequestId);
    }

    m_expungedSavedSearches.clear();
}

void RemoteToLocalSynchronizationManager::expungeLinkedNotebooks()
{
    if (m_expungedLinkedNotebooks.empty()) {
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
        emit expungeLinkedNotebook(linkedNotebookToExpunge, expungeLinkedNotebookRequestId);
    }

    m_expungedLinkedNotebooks.clear();
}

void RemoteToLocalSynchronizationManager::expungeNotebooks()
{
    if (m_expungedNotebooks.empty()) {
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
        emit expungeNotebook(notebookToExpunge, expungeNotebookRequestId);
    }

    m_expungedNotebooks.clear();
}

void RemoteToLocalSynchronizationManager::expungeNotes()
{
    if (m_expungedNotes.empty()) {
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
        emit expungeNote(noteToExpunge, expungeNoteRequestId);
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
    if (!m_expungedNotes.isEmpty()) {
        expungeNotes();
        return;
    }

    if (!m_expungedNotebooks.isEmpty()) {
        expungeNotebooks();
        return;
    }

    expungeSavedSearches();
    expungeTags();
    expungeLinkedNotebooks();

    checkExpungesCompletion();
}

void RemoteToLocalSynchronizationManager::checkExpungesCompletion()
{
    if (m_expungedTags.isEmpty() && m_expungeTagRequestIds.isEmpty() &&
        m_expungedNotebooks.isEmpty() && m_expungeNotebookRequestIds.isEmpty() &&
        m_expungedSavedSearches.isEmpty() && m_expungeSavedSearchRequestIds.isEmpty() &&
        m_expungedLinkedNotebooks.isEmpty() && m_expungeLinkedNotebookRequestIds.isEmpty() &&
        m_expungedNotes.isEmpty() && m_expungeNoteRequestIds.isEmpty())
    {
        if (syncingLinkedNotebooksContent())
        {
            m_expungeNotelessTagsRequestId = QUuid::createUuid();
            emit expungeNotelessTagsFromLinkedNotebooks(m_expungeNotelessTagsRequestId);
        }
        else
        {
            m_expungedFromServerToClient = true;
            emit expungedFromServerToClient();

            startLinkedNotebooksSync();
        }
    }
}

template <>
void RemoteToLocalSynchronizationManager::emitFindByGuidRequest<Tag>(const QString & guid)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::emitFindByGuidRequest<Tag>: guid = ") << guid);

    Tag tag;
    tag.unsetLocalUid();
    tag.setGuid(guid);

    QUuid requestId = QUuid::createUuid();
    Q_UNUSED(m_findTagByGuidRequestIds.insert(requestId));
    QNTRACE(QStringLiteral("Emitting the request to find tag in the local storage: request id = ") << requestId
            << QStringLiteral(", tag: ") << tag);
    emit findTag(tag, requestId);
}

template <>
void RemoteToLocalSynchronizationManager::emitFindByGuidRequest<SavedSearch>(const QString & guid)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::emitFindByGuidRequest<SavedSearch>: guid = ") << guid);

    SavedSearch search;
    search.unsetLocalUid();
    search.setGuid(guid);

    QUuid requestId = QUuid::createUuid();
    Q_UNUSED(m_findSavedSearchByGuidRequestIds.insert(requestId));
    QNTRACE(QStringLiteral("Emitting the request to find saved search in the local storage: request id = ") << requestId
            << QStringLiteral(", saved search: ") << search);
    emit findSavedSearch(search, requestId);
}

template <>
void RemoteToLocalSynchronizationManager::emitFindByGuidRequest<Notebook>(const QString & guid)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::emitFindByGuidRequest<Notebook>: guid = ") << guid);

    Notebook notebook;
    notebook.unsetLocalUid();
    notebook.setGuid(guid);

    QUuid requestId = QUuid::createUuid();
    Q_UNUSED(m_findNotebookByGuidRequestIds.insert(requestId));
    QNTRACE(QStringLiteral("Emitting the request to find notebook in the local storage: request id = ") << requestId
            << QStringLiteral(", notebook: ") << notebook);
    emit findNotebook(notebook, requestId);
}

template <>
void RemoteToLocalSynchronizationManager::emitFindByGuidRequest<LinkedNotebook>(const QString & guid)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::emitFindByGuidRequest<LinkedNotebook>: guid = ") << guid);

    LinkedNotebook linkedNotebook;
    linkedNotebook.setGuid(guid);

    QUuid requestId = QUuid::createUuid();
    Q_UNUSED(m_findLinkedNotebookRequestIds.insert(requestId));
    QNTRACE(QStringLiteral("Emitting the request to find linked notebook in the local storage: request id = ") << requestId
            << QStringLiteral(", linked notebook: ") << linkedNotebook);
    emit findLinkedNotebook(linkedNotebook, requestId);
}

template <>
void RemoteToLocalSynchronizationManager::emitFindByGuidRequest<Note>(const QString & guid)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::emitFindByGuidRequest<Note>: guid = ") << guid);

    Note note;
    note.unsetLocalUid();
    note.setGuid(guid);

    QUuid requestId = QUuid::createUuid();
    Q_UNUSED(m_findNoteByGuidRequestIds.insert(requestId));
    bool withResourceDataOption = false;
    QNTRACE(QStringLiteral("Emiting the request to find note in the local storage: request id = ") << requestId
            << QStringLiteral(", note: ") << note);
    emit findNote(note, withResourceDataOption, requestId);
}

template <>
void RemoteToLocalSynchronizationManager::emitFindByGuidRequest<Resource>(const QString & guid)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::emitFindByGuidRequest<Resource>: guid = ") << guid);

    Resource resource;
    resource.unsetLocalUid();
    resource.setGuid(guid);

    QUuid requestId = QUuid::createUuid();
    Q_UNUSED(m_findResourceByGuidRequestIds.insert(requestId));
    bool withBinaryData = false;
    QNTRACE(QStringLiteral("Emitting the request to find resource in the local storage: request id = ") << requestId
            << QStringLiteral(", resource: ") << resource);
    emit findResource(resource, withBinaryData, requestId);
}

void RemoteToLocalSynchronizationManager::onAddLinkedNotebookCompleted(LinkedNotebook linkedNotebook, QUuid requestId)
{
    handleLinkedNotebookAdded(linkedNotebook);
    onAddDataElementCompleted(linkedNotebook, requestId, QStringLiteral("LinkedNotebook"), m_addLinkedNotebookRequestIds);
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

    QSet<QUuid>::iterator it = m_updateLinkedNotebookRequestIds.find(requestId);
    if (it != m_updateLinkedNotebookRequestIds.end())
    {
        QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onUpdateLinkedNotebookCompleted: linkedNotebook = ")
                << linkedNotebook << QStringLiteral(", requestId = ") << requestId);

        Q_UNUSED(m_updateLinkedNotebookRequestIds.erase(it));

        CHECK_PAUSED();
        CHECK_STOPPED();

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
        emit failure(error);
    }
}

void RemoteToLocalSynchronizationManager::onExpungeLinkedNotebookCompleted(LinkedNotebook linkedNotebook, QUuid requestId)
{
    onExpungeDataElementCompleted(linkedNotebook, requestId, QStringLiteral("Linked notebook"), m_expungeLinkedNotebookRequestIds);
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
    CHECK_PAUSED();
    CHECK_STOPPED();

    if (requestId != m_listAllLinkedNotebooksRequestId) {
        return;
    }

    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onListAllLinkedNotebooksCompleted: limit = ") << limit
            << QStringLiteral(", offset = ") << offset << QStringLiteral(", order = ") << order
            << QStringLiteral(", order direction = ") << orderDirection << QStringLiteral(", requestId = ") << requestId);

    m_allLinkedNotebooks = linkedNotebooks;
    m_allLinkedNotebooksListed = true;

    if (m_listAllLinkedNotebooksContext == ListLinkedNotebooksContext::CollectLinkedNotebooksHighUsns) {
        collectHighUpdateSequenceNumbers();
        m_listAllLinkedNotebooksContext = ListLinkedNotebooksContext::StartLinkedNotebooksSync;
    }
    else {
        startLinkedNotebooksSync();
    }
}

void RemoteToLocalSynchronizationManager::onListAllLinkedNotebooksFailed(size_t limit, size_t offset,
                                                                         LocalStorageManager::ListLinkedNotebooksOrder::type order,
                                                                         LocalStorageManager::OrderDirection::type orderDirection,
                                                                         ErrorString errorDescription, QUuid requestId)
{
    CHECK_PAUSED();
    CHECK_STOPPED();

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

    if (m_listAllLinkedNotebooksContext == ListLinkedNotebooksContext::StartLinkedNotebooksSync) {
        emit failure(error);
    }
    else {
        m_collectHighUsnRequested = false;
        m_listAllLinkedNotebooksContext = ListLinkedNotebooksContext::StartLinkedNotebooksSync;
        emit failedToCollectHighUpdateSequenceNumbers(error);
    }
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
    onUpdateDataElementCompleted(notebook, requestId, QStringLiteral("Notebook"), m_updateNotebookRequestIds,
                                 m_notebooksToAddPerRequestId);
}

void RemoteToLocalSynchronizationManager::onUpdateNotebookFailed(Notebook notebook, ErrorString errorDescription,
                                                                 QUuid requestId)
{
    onUpdateDataElementFailed(notebook, requestId, errorDescription, QStringLiteral("Notebook"),
                              m_updateNotebookRequestIds, m_notebooksToAddPerRequestId);
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
        emit failure(error);

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
    auto it = m_updateResourceRequestIds.find(requestId);
    if (it != m_updateResourceRequestIds.end())
    {
        QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onUpdateResourceCompleted: resource = ")
                << resource << QStringLiteral("\nrequestId = ") << requestId);

        Q_UNUSED(m_updateResourceRequestIds.erase(it));

        checkServerDataMergeCompletion();
    }
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
        emit failure(error);
    }
}

void RemoteToLocalSynchronizationManager::onAccountHighUsnCompleted(qint32 usn, QString linkedNotebookGuid, QUuid requestId)
{
    CHECK_PAUSED()
    CHECK_STOPPED()

    if (Q_UNLIKELY(!m_collectHighUsnRequested)) {
        return;
    }

    auto it = m_accountHighUsnForLinkedNotebookRequestIds.end();
    if (requestId != m_accountHighUsnRequestId)
    {
        it = m_accountHighUsnForLinkedNotebookRequestIds.find(requestId);
        if (it == m_accountHighUsnForLinkedNotebookRequestIds.end()) {
            return;
        }
    }

    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onAccountHighUsnCompleted: USN = ")
            << usn << QStringLiteral(", linked notebook guid = ") << linkedNotebookGuid
            << QStringLiteral(", request id = ") << requestId);

    if (requestId == m_accountHighUsnRequestId) {
        m_accountHighUsn = usn;
        m_accountHighUsnRequestId = QUuid();
    }
    else {
        m_accountHighUsnForLinkedNotebooksByLinkedNotebookGuid[linkedNotebookGuid] = usn;
        Q_UNUSED(m_accountHighUsnForLinkedNotebookRequestIds.erase(it))
    }

    checkHighUsnCollectingCompletion();
}

void RemoteToLocalSynchronizationManager::onAccountHighUsnFailed(QString linkedNotebookGuid, ErrorString errorDescription, QUuid requestId)
{
    CHECK_PAUSED()
    CHECK_STOPPED()

    if (Q_UNLIKELY(!m_collectHighUsnRequested)) {
        return;
    }

    auto it = m_accountHighUsnForLinkedNotebookRequestIds.end();
    if (requestId != m_accountHighUsnRequestId)
    {
        it = m_accountHighUsnForLinkedNotebookRequestIds.find(requestId);
        if (it == m_accountHighUsnForLinkedNotebookRequestIds.end()) {
            return;
        }
    }

    QNWARNING(QStringLiteral("RemoteToLocalSynchronizationManager::onAccountHighUsnFailed: linked notebook guid = ")
              << linkedNotebookGuid << QStringLiteral(", error description: ") << errorDescription
              << QStringLiteral("; request id = ") << requestId);

    emit failedToCollectHighUpdateSequenceNumbers(errorDescription);

    m_accountHighUsn = -1;
    m_accountHighUsnRequestId = QUuid();
    m_accountHighUsnForLinkedNotebooksByLinkedNotebookGuid.clear();
    m_accountHighUsnForLinkedNotebookRequestIds.clear();
    m_collectHighUsnRequested = false;
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

    Q_UNUSED(m_resourceGuidsPendingInkNoteImageDownloadPerNoteGuid.remove(noteGuid, resourceGuid))

    checkAndIncrementNoteDownloadProgress(noteGuid);
    checkServerDataMergeCompletion();
}

void RemoteToLocalSynchronizationManager::onNoteThumbnailDownloadingFinished(bool status, QString noteGuid,
                                                                             QByteArray downloadedThumbnailImageData,
                                                                             ErrorString errorDescription)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onNoteThumbnailDownloadingFinished: status = ")
            << (status ? QStringLiteral("true") : QStringLiteral("false")) << QStringLiteral(", note guid = ")
            << noteGuid << QStringLiteral(", error description = ") << errorDescription);

    if (!status)
    {
        QNWARNING(errorDescription);
        checkAndIncrementNoteDownloadProgress(noteGuid);
        checkServerDataMergeCompletion();
        return;
    }

    auto it = m_notesPendingThumbnailDownloadByGuid.find(noteGuid);
    if (Q_UNLIKELY(it == m_notesPendingThumbnailDownloadByGuid.end()))
    {
        QNWARNING(QStringLiteral("Received note thumbnail downloaded event for note which seemingly was not pending it: note guid = ")
                  << noteGuid);

        checkAndIncrementNoteDownloadProgress(noteGuid);
        checkServerDataMergeCompletion();
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
    emit updateNote(note, /* update resources = */ false, /* update tags = */ false, updateNoteRequestId);
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

    if (m_paused) {
        resume();
    }
    else {
        launchSync();
    }
}

void RemoteToLocalSynchronizationManager::onAuthenticationTokensForLinkedNotebooksReceived(QHash<QString, QPair<QString,QString> > authenticationTokensAndShardIdsByLinkedNotebookGuid,
                                                                                           QHash<QString, qevercloud::Timestamp> authenticationTokenExpirationTimesByLinkedNotebookGuid)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onAuthenticationTokensForLinkedNotebooksReceived:"));

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

void RemoteToLocalSynchronizationManager::collectHighUpdateSequenceNumbers()
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::collectHighUpdateSequenceNumbers"));

    m_collectHighUsnRequested = true;
    connectToLocalStorage();

    if (!m_allLinkedNotebooksListed) {
        requestAllLinkedNotebooks(ListLinkedNotebooksContext::CollectLinkedNotebooksHighUsns);
        return;
    }

    m_accountHighUsn = -1;
    m_accountHighUsnForLinkedNotebooksByLinkedNotebookGuid.clear();
    m_accountHighUsnRequestId = QUuid();
    m_accountHighUsnForLinkedNotebookRequestIds.clear();

    m_accountHighUsnRequestId = QUuid::createUuid();
    QNTRACE(QStringLiteral("Emitting the request to get user's own account's high update sequence number: request id = ")
            << m_accountHighUsnRequestId);
    emit requestAccountHighUsn(QString(), m_accountHighUsnRequestId);

    for(auto it = m_linkedNotebooks.constBegin(), end = m_linkedNotebooks.constEnd(); it != end; ++it)
    {
        LinkedNotebook linkedNotebook(*it);
        if (Q_UNLIKELY(!linkedNotebook.hasGuid())) {
            QNWARNING(QStringLiteral("Detected a linked notebook without guid: ") << linkedNotebook);
            continue;
        }

        QUuid requestId = QUuid::createUuid();
        Q_UNUSED(m_accountHighUsnForLinkedNotebookRequestIds.insert(requestId))
        QNTRACE(QStringLiteral("Emitting the request to get high update sequence number for a linked notebook: request id = ")
                << requestId << QStringLiteral(", linked notebook: ") << linkedNotebook);
        emit requestAccountHighUsn(linkedNotebook.guid(), requestId);
    }
}

void RemoteToLocalSynchronizationManager::onGetNoteAsyncFinished(qint32 errorCode, Note note, qint32 rateLimitSeconds,
                                                                 ErrorString errorDescription)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onGetNoteAsyncFinished: error code = ")
            << errorCode << QStringLiteral(", rate limit seconds = ") << rateLimitSeconds
            << QStringLiteral(", error description: ") << errorDescription
            << QStringLiteral(", note: ") << note);

    if (Q_UNLIKELY(!note.hasGuid())) {
        errorDescription.setBase(QT_TR_NOOP("Internal error: just downloaded note has no guid"));
        QNWARNING(errorDescription << QStringLiteral(", note: ") << note);
        emit failure(errorDescription);
        return;
    }

    QString noteGuid = note.guid();

    auto addIt = m_guidsOfNotesPendingDownloadForAddingToLocalStorage.find(noteGuid);
    auto updateIt = ((addIt == m_guidsOfNotesPendingDownloadForAddingToLocalStorage.end())
                     ? m_guidsOfNotesPendingDownloadForUpdatingInLocalStorage.find(noteGuid)
                     : m_guidsOfNotesPendingDownloadForUpdatingInLocalStorage.end());

    bool needToAddNote = (addIt != m_guidsOfNotesPendingDownloadForAddingToLocalStorage.end());
    bool needToUpdateNote = (updateIt != m_guidsOfNotesPendingDownloadForUpdatingInLocalStorage.end());

    if (needToAddNote) {
        Q_UNUSED(m_guidsOfNotesPendingDownloadForAddingToLocalStorage.erase(addIt))
    }
    else if (needToUpdateNote) {
        Q_UNUSED(m_guidsOfNotesPendingDownloadForUpdatingInLocalStorage.erase(updateIt))
    }

    if (Q_UNLIKELY(!needToAddNote && !needToUpdateNote)) {
        errorDescription.setBase(QT_TR_NOOP("Internal error: the downloaded note was not expected"));
        QNWARNING(errorDescription << QStringLiteral(", note: ") << note);
        emit failure(errorDescription);
        return;
    }

    if (errorCode == qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED)
    {
        if (rateLimitSeconds <= 0) {
            errorDescription.setBase(QT_TR_NOOP("QEverCloud or Evernote protocol error: caught RATE_LIMIT_REACHED "
                                                "exception but the number of seconds to wait is zero or negative"));
            errorDescription.details() = QString::number(rateLimitSeconds);
            emit failure(errorDescription);
            return;
        }

        int timerId = startTimer(SEC_TO_MSEC(rateLimitSeconds));
        if (Q_UNLIKELY(timerId == 0)) {
            ErrorString errorDescription(QT_TR_NOOP("Failed to start a timer to postpone the Evernote API call "
                                                    "due to rate limit exceeding"));
            emit failure(errorDescription);
            return;
        }

        if (needToAddNote) {
            m_notesToAddPerAPICallPostponeTimerId[timerId] = note;
        }
        else {
            m_notesToUpdatePerAPICallPostponeTimerId[timerId] = note;
        }

        emit rateLimitExceeded(rateLimitSeconds);
        return;
    }
    else if (errorCode == qevercloud::EDAMErrorCode::AUTH_EXPIRED)
    {
        handleAuthExpiration();
        return;
    }
    else if (errorCode != 0) {
        emit failure(errorDescription);
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
    emit updateNote(note, /* update resources = */ true, /* update tags = */ true, updateNoteRequestId);
}

void RemoteToLocalSynchronizationManager::onNotebookSyncConflictResolverFinished(qevercloud::Notebook remoteNotebook)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onNotebookSyncConflictResolverFinished: ") << remoteNotebook);

    NotebookSyncConflictResolver * pResolver = qobject_cast<NotebookSyncConflictResolver*>(sender());
    if (pResolver) {
        pResolver->setParent(Q_NULLPTR);
        pResolver->deleteLater();
    }

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

    emit failure(errorDescription);
}

void RemoteToLocalSynchronizationManager::onTagSyncConflictResolverFinished(qevercloud::Tag remoteTag)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onTagSyncConflictResolverFinished: ") << remoteTag);

    TagSyncConflictResolver * pResolver = qobject_cast<TagSyncConflictResolver*>(sender());
    if (pResolver) {
        pResolver->setParent(Q_NULLPTR);
        pResolver->deleteLater();
    }

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

    emit failure(errorDescription);
}

void RemoteToLocalSynchronizationManager::onSavedSearchSyncConflictResolverFinished(qevercloud::SavedSearch remoteSavedSearch)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::onSavedSearchSyncConflictResolverFinished: ") << remoteSavedSearch);

    SavedSearchSyncConflictResolver * pResolver = qobject_cast<SavedSearchSyncConflictResolver*>(sender());
    if (pResolver) {
        pResolver->setParent(Q_NULLPTR);
        pResolver->deleteLater();
    }

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

    emit failure(errorDescription);
}

void RemoteToLocalSynchronizationManager::createConnections()
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::createConnections"));

    QObject::connect(&m_noteStore, QNSIGNAL(NoteStore,getNoteAsyncFinished,qint32,Note,qint32,ErrorString),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onGetNoteAsyncFinished,qint32,Note,qint32,ErrorString));

    connectToLocalStorage();
}

void RemoteToLocalSynchronizationManager::connectToLocalStorage()
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::connectToLocalStorage"));

    if (m_connectedToLocalStorage) {
        QNDEBUG(QStringLiteral("Already connected to the local storage"));
        return;
    }

    // Connect local signals with localStorageManagerAsync's slots
    QObject::connect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,addUser,User,QUuid),
                     &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onAddUserRequest,User,QUuid));
    QObject::connect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,updateUser,User,QUuid),
                     &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onUpdateUserRequest,User,QUuid));
    QObject::connect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,findUser,User,QUuid),
                     &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onFindUserRequest,User,QUuid));

    QObject::connect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,addNotebook,Notebook,QUuid),
                     &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onAddNotebookRequest,Notebook,QUuid));
    QObject::connect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,updateNotebook,Notebook,QUuid),
                     &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onUpdateNotebookRequest,Notebook,QUuid));
    QObject::connect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,findNotebook,Notebook,QUuid),
                     &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onFindNotebookRequest,Notebook,QUuid));
    QObject::connect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,expungeNotebook,Notebook,QUuid),
                     &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onExpungeNotebookRequest,Notebook,QUuid));

    QObject::connect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,addNote,Note,QUuid),
                     &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onAddNoteRequest,Note,QUuid));
    QObject::connect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,updateNote,Note,bool,bool,QUuid),
                     &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onUpdateNoteRequest,Note,bool,bool,QUuid));
    QObject::connect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,findNote,Note,bool,QUuid),
                     &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onFindNoteRequest,Note,bool,QUuid));
    QObject::connect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,expungeNote,Note,QUuid),
                     &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onExpungeNoteRequest,Note,QUuid));

    QObject::connect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,addTag,Tag,QUuid),
                     &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onAddTagRequest,Tag,QUuid));
    QObject::connect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,updateTag,Tag,QUuid),
                     &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onUpdateTagRequest,Tag,QUuid));
    QObject::connect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,findTag,Tag,QUuid),
                     &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onFindTagRequest,Tag,QUuid));
    QObject::connect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,expungeTag,Tag,QUuid),
                     &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onExpungeTagRequest,Tag,QUuid));

    QObject::connect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,expungeNotelessTagsFromLinkedNotebooks,QUuid),
                     &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onExpungeNotelessTagsFromLinkedNotebooksRequest,QUuid));

    QObject::connect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,addResource,Resource,QUuid),
                     &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onAddResourceRequest,Resource,QUuid));
    QObject::connect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,updateResource,Resource,QUuid),
                     &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onUpdateResourceRequest,Resource,QUuid));
    QObject::connect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,findResource,Resource,bool,QUuid),
                     &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onFindResourceRequest,Resource,bool,QUuid));

    QObject::connect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,addLinkedNotebook,LinkedNotebook,QUuid),
                     &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onAddLinkedNotebookRequest,LinkedNotebook,QUuid));
    QObject::connect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,updateLinkedNotebook,LinkedNotebook,QUuid),
                     &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onUpdateLinkedNotebookRequest,LinkedNotebook,QUuid));
    QObject::connect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,findLinkedNotebook,LinkedNotebook,QUuid),
                     &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onFindLinkedNotebookRequest,LinkedNotebook,QUuid));
    QObject::connect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,expungeLinkedNotebook,LinkedNotebook,QUuid),
                     &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onExpungeLinkedNotebookRequest,LinkedNotebook,QUuid));

    QObject::connect(this,
                     QNSIGNAL(RemoteToLocalSynchronizationManager,listAllLinkedNotebooks,size_t,size_t,
                              LocalStorageManager::ListLinkedNotebooksOrder::type,LocalStorageManager::OrderDirection::type,QUuid),
                     &m_localStorageManagerAsync,
                     QNSLOT(LocalStorageManagerAsync,onListAllLinkedNotebooksRequest,size_t,size_t,
                            LocalStorageManager::ListLinkedNotebooksOrder::type,LocalStorageManager::OrderDirection::type,QUuid));

    QObject::connect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,addSavedSearch,SavedSearch,QUuid),
                     &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onAddSavedSearchRequest,SavedSearch,QUuid));
    QObject::connect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,updateSavedSearch,SavedSearch,QUuid),
                     &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onUpdateSavedSearchRequest,SavedSearch,QUuid));
    QObject::connect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,findSavedSearch,SavedSearch,QUuid),
                     &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onFindSavedSearchRequest,SavedSearch,QUuid));
    QObject::connect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,expungeSavedSearch,SavedSearch,QUuid),
                     &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onExpungeSavedSearch,SavedSearch,QUuid));
    QObject::connect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,requestAccountHighUsn,QString,QUuid),
                     &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onAccountHighUsnRequest,QString,QUuid));

    // Connect localStorageManagerAsync's signals to local slots
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findUserComplete,User,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onFindUserCompleted,User,QUuid));
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findUserFailed,User,ErrorString,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onFindUserFailed,User,ErrorString,QUuid));

    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findNotebookComplete,Notebook,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onFindNotebookCompleted,Notebook,QUuid));
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findNotebookFailed,Notebook,ErrorString,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onFindNotebookFailed,Notebook,ErrorString,QUuid));

    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findNoteComplete,Note,bool,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onFindNoteCompleted,Note,bool,QUuid));
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findNoteFailed,Note,bool,ErrorString,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onFindNoteFailed,Note,bool,ErrorString,QUuid));

    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findTagComplete,Tag,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onFindTagCompleted,Tag,QUuid));
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findTagFailed,Tag,ErrorString,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onFindTagFailed,Tag,ErrorString,QUuid));

    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findLinkedNotebookComplete,LinkedNotebook,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onFindLinkedNotebookCompleted,LinkedNotebook,QUuid));
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findLinkedNotebookFailed,LinkedNotebook,ErrorString,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onFindLinkedNotebookFailed,LinkedNotebook,ErrorString,QUuid));

    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findSavedSearchComplete,SavedSearch,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onFindSavedSearchCompleted,SavedSearch,QUuid));
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findSavedSearchFailed,SavedSearch,ErrorString,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onFindSavedSearchFailed,SavedSearch,ErrorString,QUuid));

    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findResourceComplete,Resource,bool,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onFindResourceCompleted,Resource,bool,QUuid));
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findResourceFailed,Resource,bool,ErrorString,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onFindResourceFailed,Resource,bool,ErrorString,QUuid));

    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addTagComplete,Tag,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onAddTagCompleted,Tag,QUuid));
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addTagFailed,Tag,ErrorString,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onAddTagFailed,Tag,ErrorString,QUuid));

    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateTagComplete,Tag,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onUpdateTagCompleted,Tag,QUuid));
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateTagFailed,Tag,ErrorString,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onUpdateTagFailed,Tag,ErrorString,QUuid));

    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeTagComplete,Tag,QStringList,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onExpungeTagCompleted,Tag,QStringList,QUuid));
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeTagFailed,Tag,ErrorString,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onExpungeTagFailed,Tag,ErrorString,QUuid));

    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeNotelessTagsFromLinkedNotebooksComplete,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onExpungeNotelessTagsFromLinkedNotebooksCompleted,QUuid));
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeNotelessTagsFromLinkedNotebooksFailed,ErrorString,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onExpungeNotelessTagsFromLinkedNotebooksFailed,ErrorString,QUuid));

    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addUserComplete,User,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onAddUserCompleted,User,QUuid));
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addUserFailed,User,ErrorString,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onAddUserFailed,User,ErrorString,QUuid));

    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateUserComplete,User,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onUpdateUserCompleted,User,QUuid));
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateUserFailed,User,ErrorString,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onUpdateUserFailed,User,ErrorString,QUuid));

    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addSavedSearchComplete,SavedSearch,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onAddSavedSearchCompleted,SavedSearch,QUuid));
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addSavedSearchFailed,SavedSearch,ErrorString,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onAddSavedSearchFailed,SavedSearch,ErrorString,QUuid));

    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateSavedSearchComplete,SavedSearch,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onUpdateSavedSearchCompleted,SavedSearch,QUuid));
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateSavedSearchFailed,SavedSearch,ErrorString,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onUpdateSavedSearchFailed,SavedSearch,ErrorString,QUuid));

    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeSavedSearchComplete,SavedSearch,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onExpungeSavedSearchCompleted,SavedSearch,QUuid));
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeSavedSearchFailed,SavedSearch,ErrorString,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onExpungeSavedSearchFailed,SavedSearch,ErrorString,QUuid));

    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addLinkedNotebookComplete,LinkedNotebook,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onAddLinkedNotebookCompleted,LinkedNotebook,QUuid));
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addLinkedNotebookFailed,LinkedNotebook,ErrorString,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onAddLinkedNotebookFailed,LinkedNotebook,ErrorString,QUuid));

    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateLinkedNotebookComplete,LinkedNotebook,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onUpdateLinkedNotebookCompleted,LinkedNotebook,QUuid));
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateLinkedNotebookFailed,LinkedNotebook,ErrorString,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onUpdateLinkedNotebookFailed,LinkedNotebook,ErrorString,QUuid));

    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeLinkedNotebookComplete,LinkedNotebook,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onExpungeLinkedNotebookCompleted,LinkedNotebook,QUuid));
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeLinkedNotebookFailed,LinkedNotebook,ErrorString,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onExpungeLinkedNotebookFailed,LinkedNotebook,ErrorString,QUuid));

    QObject::connect(&m_localStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,listAllLinkedNotebooksComplete,size_t,size_t,
                              LocalStorageManager::ListLinkedNotebooksOrder::type,LocalStorageManager::OrderDirection::type,QList<LinkedNotebook>,QUuid),
                     this,
                     QNSLOT(RemoteToLocalSynchronizationManager,onListAllLinkedNotebooksCompleted,size_t,size_t,
                            LocalStorageManager::ListLinkedNotebooksOrder::type,LocalStorageManager::OrderDirection::type,QList<LinkedNotebook>,QUuid));
    QObject::connect(&m_localStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,listAllLinkedNotebooksFailed,size_t,size_t,
                              LocalStorageManager::ListLinkedNotebooksOrder::type,LocalStorageManager::OrderDirection::type,ErrorString,QUuid),
                     this,
                     QNSLOT(RemoteToLocalSynchronizationManager,onListAllLinkedNotebooksFailed,size_t,size_t,
                            LocalStorageManager::ListLinkedNotebooksOrder::type,LocalStorageManager::OrderDirection::type,ErrorString,QUuid));

    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addNotebookComplete,Notebook,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onAddNotebookCompleted,Notebook,QUuid));
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addNotebookFailed,Notebook,ErrorString,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onAddNotebookFailed,Notebook,ErrorString,QUuid));

    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateNotebookComplete,Notebook,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onUpdateNotebookCompleted,Notebook,QUuid));
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateNotebookFailed,Notebook,ErrorString,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onUpdateNotebookFailed,Notebook,ErrorString,QUuid));

    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeNotebookComplete,Notebook,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onExpungeNotebookCompleted,Notebook,QUuid));
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeNotebookFailed,Notebook,ErrorString,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onExpungeNotebookFailed,Notebook,ErrorString,QUuid));

    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addNoteComplete,Note,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onAddNoteCompleted,Note,QUuid));
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addNoteFailed,Note,ErrorString,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onAddNoteFailed,Note,ErrorString,QUuid));

    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateNoteComplete,Note,bool,bool,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onUpdateNoteCompleted,Note,bool,bool,QUuid));
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateNoteFailed,Note,bool,bool,ErrorString,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onUpdateNoteFailed,Note,bool,bool,ErrorString,QUuid));

    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeNoteComplete,Note,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onExpungeNoteCompleted,Note,QUuid));
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeNoteFailed,Note,ErrorString,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onExpungeNoteFailed,Note,ErrorString,QUuid));

    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addResourceComplete,Resource,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onAddResourceCompleted,Resource,QUuid));
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addResourceFailed,Resource,ErrorString,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onAddResourceFailed,Resource,ErrorString,QUuid));

    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateResourceComplete,Resource,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onUpdateResourceCompleted,Resource,QUuid));
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateResourceFailed,Resource,ErrorString,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onUpdateResourceFailed,Resource,ErrorString,QUuid));

    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,accountHighUsnComplete,qint32,QString,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onAccountHighUsnCompleted,qint32,QString,QUuid));
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,accountHighUsnFailed,QString,ErrorString,QUuid),
                     this, QNSLOT(RemoteToLocalSynchronizationManager,onAccountHighUsnFailed,QString,ErrorString,QUuid));

    m_connectedToLocalStorage = true;
}

void RemoteToLocalSynchronizationManager::disconnectFromLocalStorage()
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::disconnectFromLocalStorage"));

    if (!m_connectedToLocalStorage) {
        QNDEBUG(QStringLiteral("Not connected to local storage at the moment"));
        return;
    }

    // Disconnect local signals from localStorageManagerThread's slots
    QObject::disconnect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,addUser,User,QUuid),
                        &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onAddUserRequest,User,QUuid));
    QObject::disconnect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,updateUser,User,QUuid),
                        &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onUpdateUserRequest,User,QUuid));
    QObject::disconnect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,findUser,User,QUuid),
                        &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onFindUserRequest,User,QUuid));

    QObject::disconnect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,addNotebook,Notebook,QUuid),
                        &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onAddNotebookRequest,Notebook,QUuid));
    QObject::disconnect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,updateNotebook,Notebook,QUuid),
                        &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onUpdateNotebookRequest,Notebook,QUuid));
    QObject::disconnect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,findNotebook,Notebook,QUuid),
                        &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onFindNotebookRequest,Notebook,QUuid));
    QObject::disconnect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,expungeNotebook,Notebook,QUuid),
                        &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onExpungeNotebookRequest,Notebook,QUuid));

    QObject::disconnect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,addNote,Note,QUuid),
                        &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onAddNoteRequest,Note,QUuid));
    QObject::disconnect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,updateNote,Note,bool,bool,QUuid),
                        &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onUpdateNoteRequest,Note,bool,bool,QUuid));
    QObject::disconnect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,findNote,Note,bool,QUuid),
                        &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onFindNoteRequest,Note,bool,QUuid));
    QObject::disconnect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,expungeNote,Note,QUuid),
                        &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onExpungeNoteRequest,Note,QUuid));

    QObject::disconnect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,addTag,Tag,QUuid),
                        &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onAddTagRequest,Tag,QUuid));
    QObject::disconnect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,updateTag,Tag,QUuid),
                        &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onUpdateTagRequest,Tag,QUuid));
    QObject::disconnect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,findTag,Tag,QUuid),
                        &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onFindTagRequest,Tag,QUuid));
    QObject::disconnect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,expungeTag,Tag,QUuid),
                        &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onExpungeTagRequest,Tag,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeNotelessTagsFromLinkedNotebooksComplete,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onExpungeNotelessTagsFromLinkedNotebooksCompleted,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeNotelessTagsFromLinkedNotebooksFailed,ErrorString,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onExpungeNotelessTagsFromLinkedNotebooksFailed,ErrorString,QUuid));

    QObject::disconnect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,addResource,Resource,QUuid),
                        &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onAddResourceRequest,Resource,QUuid));
    QObject::disconnect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,updateResource,Resource,QUuid),
                        &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onUpdateResourceRequest,Resource,QUuid));
    QObject::disconnect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,findResource,Resource,bool,QUuid),
                        &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onFindResourceRequest,Resource,bool,QUuid));

    QObject::disconnect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,addLinkedNotebook,LinkedNotebook,QUuid),
                        &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onAddLinkedNotebookRequest,LinkedNotebook,QUuid));
    QObject::disconnect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,updateLinkedNotebook,LinkedNotebook,QUuid),
                        &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onUpdateLinkedNotebookRequest,LinkedNotebook,QUuid));
    QObject::disconnect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,findLinkedNotebook,LinkedNotebook,QUuid),
                        &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onFindLinkedNotebookRequest,LinkedNotebook,QUuid));
    QObject::disconnect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,expungeLinkedNotebook,LinkedNotebook,QUuid),
                        &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onExpungeLinkedNotebookRequest,LinkedNotebook,QUuid));

    QObject::disconnect(this,
                        QNSIGNAL(RemoteToLocalSynchronizationManager,listAllLinkedNotebooks,size_t,size_t,
                                 LocalStorageManager::ListLinkedNotebooksOrder::type,LocalStorageManager::OrderDirection::type,QUuid),
                        &m_localStorageManagerAsync,
                        QNSLOT(LocalStorageManagerAsync,onListAllLinkedNotebooksRequest,size_t,size_t,
                               LocalStorageManager::ListLinkedNotebooksOrder::type,LocalStorageManager::OrderDirection::type,QUuid));

    QObject::disconnect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,addSavedSearch,SavedSearch,QUuid),
                        &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onAddSavedSearchRequest,SavedSearch,QUuid));
    QObject::disconnect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,updateSavedSearch,SavedSearch,QUuid),
                        &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onUpdateSavedSearchRequest,SavedSearch,QUuid));
    QObject::disconnect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,findSavedSearch,SavedSearch,QUuid),
                        &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onFindSavedSearchRequest,SavedSearch,QUuid));
    QObject::disconnect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,expungeSavedSearch,SavedSearch,QUuid),
                        &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onExpungeSavedSearch,SavedSearch,QUuid));
    QObject::disconnect(this, QNSIGNAL(RemoteToLocalSynchronizationManager,requestAccountHighUsn,QString,QUuid),
                        &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onAccountHighUsnRequest,QString,QUuid));

    // Disconnect localStorageManagerThread's signals to local slots
    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findUserComplete,User,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onFindUserCompleted,User,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findUserFailed,User,ErrorString,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onFindUserFailed,User,ErrorString,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findNotebookComplete,Notebook,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onFindNotebookCompleted,Notebook,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findNotebookFailed,Notebook,ErrorString,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onFindNotebookFailed,Notebook,ErrorString,QUuid));

    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findNoteComplete,Note,bool,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onFindNoteCompleted,Note,bool,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findNoteFailed,Note,bool,ErrorString,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onFindNoteFailed,Note,bool,ErrorString,QUuid));

    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findTagComplete,Tag,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onFindTagCompleted,Tag,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findTagFailed,Tag,ErrorString,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onFindTagFailed,Tag,ErrorString,QUuid));

    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findLinkedNotebookComplete,LinkedNotebook,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onFindLinkedNotebookCompleted,LinkedNotebook,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findLinkedNotebookFailed,LinkedNotebook,ErrorString,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onFindLinkedNotebookFailed,LinkedNotebook,ErrorString,QUuid));

    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findSavedSearchComplete,SavedSearch,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onFindSavedSearchCompleted,SavedSearch,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findSavedSearchFailed,SavedSearch,ErrorString,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onFindSavedSearchFailed,SavedSearch,ErrorString,QUuid));

    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findResourceComplete,Resource,bool,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onFindResourceCompleted,Resource,bool,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findResourceFailed,Resource,bool,ErrorString,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onFindResourceFailed,Resource,bool,ErrorString,QUuid));

    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addTagComplete,Tag,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onAddTagCompleted,Tag,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addTagFailed,Tag,ErrorString,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onAddTagFailed,Tag,ErrorString,QUuid));

    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateTagComplete,Tag,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onUpdateTagCompleted,Tag,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateTagFailed,Tag,ErrorString,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onUpdateTagFailed,Tag,ErrorString,QUuid));

    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeTagComplete,Tag,QStringList,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onExpungeTagCompleted,Tag,QStringList,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeTagFailed,Tag,ErrorString,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onExpungeTagFailed,Tag,ErrorString,QUuid));

    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addSavedSearchComplete,SavedSearch,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onAddSavedSearchCompleted,SavedSearch,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addSavedSearchFailed,SavedSearch,ErrorString,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onAddSavedSearchFailed,SavedSearch,ErrorString,QUuid));

    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateSavedSearchComplete,SavedSearch,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onUpdateSavedSearchCompleted,SavedSearch,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateSavedSearchFailed,SavedSearch,ErrorString,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onUpdateSavedSearchFailed,SavedSearch,ErrorString,QUuid));

    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeSavedSearchComplete,SavedSearch,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onExpungeSavedSearchCompleted,SavedSearch,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeSavedSearchFailed,SavedSearch,ErrorString,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onExpungeSavedSearchFailed,SavedSearch,ErrorString,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addUserComplete,User,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onAddUserCompleted,User,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addUserFailed,User,ErrorString,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onAddUserFailed,User,ErrorString,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateUserComplete,User,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onUpdateUserCompleted,User,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateUserFailed,User,ErrorString,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onUpdateUserFailed,User,ErrorString,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addLinkedNotebookComplete,LinkedNotebook,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onAddLinkedNotebookCompleted,LinkedNotebook,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addLinkedNotebookFailed,LinkedNotebook,ErrorString,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onAddLinkedNotebookFailed,LinkedNotebook,ErrorString,QUuid));

    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateLinkedNotebookComplete,LinkedNotebook,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onUpdateLinkedNotebookCompleted,LinkedNotebook,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateLinkedNotebookFailed,LinkedNotebook,ErrorString,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onUpdateLinkedNotebookFailed,LinkedNotebook,ErrorString,QUuid));

    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeLinkedNotebookComplete,LinkedNotebook,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onExpungeLinkedNotebookCompleted,LinkedNotebook,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeLinkedNotebookFailed,LinkedNotebook,ErrorString,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onExpungeLinkedNotebookFailed,LinkedNotebook,ErrorString,QUuid));

    QObject::disconnect(&m_localStorageManagerAsync,
                        QNSIGNAL(LocalStorageManagerAsync,listAllLinkedNotebooksComplete,size_t,size_t,
                                 LocalStorageManager::ListLinkedNotebooksOrder::type,LocalStorageManager::OrderDirection::type,QList<LinkedNotebook>,QUuid),
                        this,
                        QNSLOT(RemoteToLocalSynchronizationManager,onListAllLinkedNotebooksCompleted,size_t,size_t,
                               LocalStorageManager::ListLinkedNotebooksOrder::type,LocalStorageManager::OrderDirection::type,QList<LinkedNotebook>,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync,
                        QNSIGNAL(LocalStorageManagerAsync,listAllLinkedNotebooksFailed,size_t,size_t,
                                 LocalStorageManager::ListLinkedNotebooksOrder::type,LocalStorageManager::OrderDirection::type,ErrorString,QUuid),
                        this,
                        QNSLOT(RemoteToLocalSynchronizationManager,onListAllLinkedNotebooksFailed,size_t,size_t,
                               LocalStorageManager::ListLinkedNotebooksOrder::type,LocalStorageManager::OrderDirection::type,ErrorString,QUuid));

    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addNotebookComplete,Notebook,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onAddNotebookCompleted,Notebook,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addNotebookFailed,Notebook,ErrorString,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onAddNotebookFailed,Notebook,ErrorString,QUuid));

    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateNotebookComplete,Notebook,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onUpdateNotebookCompleted,Notebook,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateNotebookFailed,Notebook,ErrorString,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onUpdateNotebookFailed,Notebook,ErrorString,QUuid));

    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeNotebookComplete,Notebook,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onExpungeNotebookCompleted,Notebook,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeNotebookFailed,Notebook,ErrorString,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onExpungeNotebookFailed,Notebook,ErrorString,QUuid));

    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateNoteComplete,Note,bool,bool,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onUpdateNoteCompleted,Note,bool,bool,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateNoteFailed,Note,bool,bool,ErrorString,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onUpdateNoteFailed,Note,bool,bool,ErrorString,QUuid));

    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addNoteComplete,Note,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onAddNoteCompleted,Note,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addNoteFailed,Note,ErrorString,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onAddNoteFailed,Note,ErrorString,QUuid));

    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeNoteComplete,Note,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onExpungeNoteCompleted,Note,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeNoteFailed,Note,ErrorString,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onExpungeNoteFailed,Note,ErrorString,QUuid));

    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addResourceComplete,Resource,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onAddResourceCompleted,Resource,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addResourceFailed,Resource,ErrorString,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onAddResourceFailed,Resource,ErrorString,QUuid));

    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateResourceComplete,Resource,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onUpdateResourceCompleted,Resource,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateResourceFailed,Resource,ErrorString,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onUpdateResourceFailed,Resource,ErrorString,QUuid));

    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,accountHighUsnComplete,qint32,QString,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onAccountHighUsnCompleted,qint32,QString,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,accountHighUsnFailed,QString,ErrorString,QUuid),
                        this, QNSLOT(RemoteToLocalSynchronizationManager,onAccountHighUsnFailed,QString,ErrorString,QUuid));

    m_connectedToLocalStorage = false;

    // With the disconnect from local storage the list of previously received linked notebooks (if any) + new additions/updates becomes invalidated
    m_allLinkedNotebooksListed = false;
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
        emit requestAuthenticationToken();
        return;
    }

    launchSavedSearchSync();
    launchLinkedNotebookSync();

    launchTagsSync();
    launchNotebookSync();

    if (!m_tags.empty() || !m_notebooks.empty()) {
        // NOTE: the sync of notes and, if need be, individual resouces would be launched asynchronously when the
        // notebooks and tags are synced
        return;
    }

    QNDEBUG(QStringLiteral("The local lists of tags and notebooks waiting for processing are empty, "
                           "checking if there are notes to process"));

    launchNotesSync();
    if (!m_notes.empty())
    {
        QNDEBUG(QStringLiteral("Synchronizing notes"));
        // NOTE: the sync of individual resources would be launched asynchronously (if current sync is incremental)
        // when the notes are synced
        return;
    }

    QNDEBUG(QStringLiteral("The local list of notes waiting for processing is empty"));

    if (m_lastSyncMode != SyncMode::IncrementalSync) {
        QNDEBUG(QStringLiteral("Running full sync => no sync for individual resources is needed"));
        return;
    }

    launchResourcesSync();

    // NOTE: we might have received the only sync chunk without the actual data elements, need to check for such case
    // and leave if there's nothing worth processing within the sync
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
    bool protocolVersionChecked = m_userStore.checkVersion(clientName, edamProtocolVersionMajor, edamProtocolVersionMinor,
                                                           errorDescription);
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
    qint32 errorCode = m_userStore.getUser(m_user, errorDescription, rateLimitSeconds);
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
            emit rateLimitExceeded(rateLimitSeconds);
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

    if (!m_connectedToLocalStorage) {
        createConnections();
    }

    // See if this user's entry already exists in the local storage or not
    m_findUserRequestId = QUuid::createUuid();
    QNTRACE(QStringLiteral("Emitting request to find user in the local storage database: request id = ")
            << m_findUserRequestId << QStringLiteral(", user = ") << m_user);
    emit findUser(m_user, m_findUserRequestId);
}

bool RemoteToLocalSynchronizationManager::checkAndSyncAccountLimits(const bool waitIfRateLimitReached, ErrorString & errorDescription)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::checkAndSyncAccountLimits: wait if rate limit reached = ")
            << (waitIfRateLimitReached ? QStringLiteral("true") : QStringLiteral("false")));

    if (Q_UNLIKELY(!m_user.hasId())) {
        ErrorString error(QT_TR_NOOP("Detected the attempt to synchronize the account limits before the user id was set"));
        QNWARNING(error);
        emit failure(error);
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
    qint32 errorCode = m_userStore.getAccountLimits(m_user.serviceLevel(), m_accountLimits, errorDescription, rateLimitSeconds);
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

            emit rateLimitExceeded(rateLimitSeconds);
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
        emit failure(error);
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
        emit failure(error);
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

void RemoteToLocalSynchronizationManager::launchTagsSync()
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::launchTagsSync"));
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
    launchDataElementSync<LinkedNotebooksList, LinkedNotebook>(ContentSource::UserAccount, QStringLiteral("Linked notebook"),
                                                               m_linkedNotebooks, m_expungedLinkedNotebooks);
}

void RemoteToLocalSynchronizationManager::launchNotebookSync()
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::launchNotebookSync"));
    launchDataElementSync<NotebooksList, Notebook>(ContentSource::UserAccount, QStringLiteral("Notebook"), m_notebooks, m_expungedNotebooks);
}

bool RemoteToLocalSynchronizationManager::syncingLinkedNotebooksContent() const
{
    if (m_lastSyncMode == SyncMode::FullSync) {
        return m_fullNoteContentsDownloaded;
    }

    return m_expungedFromServerToClient;
}

void RemoteToLocalSynchronizationManager::checkAndIncrementNoteDownloadProgress(const QString & noteGuid)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::checkAndIncrementNoteDownloadProgress: note guid = ") << noteGuid);

    if (m_resourceGuidsPendingInkNoteImageDownloadPerNoteGuid.contains(noteGuid)) {
        QNDEBUG(QStringLiteral("Found still pending ink note image download(s) for this note guid, won't increment the note download progress"));
        return;
    }

    if (m_notesPendingThumbnailDownloadByGuid.contains(noteGuid)) {
        QNDEBUG(QStringLiteral("Found still pending note thumbnail download for this note guid, won't increment the note download progress"));
        return;
    }

    ++m_numNotesDownloaded;
    QNTRACE(QStringLiteral("Incremented the number of downloaded notes to ")
            << m_numNotesDownloaded << QStringLiteral(", the total number of notes to download = ")
            << m_originalNumberOfNotes);

    if (syncingLinkedNotebooksContent()) {
        emit linkedNotebooksNotesDownloadProgress(m_numNotesDownloaded, m_originalNumberOfNotes);
    }
    else {
        emit notesDownloadProgress(m_numNotesDownloaded, m_originalNumberOfNotes);
    }
}

void RemoteToLocalSynchronizationManager::checkHighUsnCollectingCompletion()
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::checkHighUsnCollectingCompletion"));

    if (!m_collectHighUsnRequested) {
        QNDEBUG(QStringLiteral("Not collecting high USNs at the moment"));
        return;
    }

    if (!m_accountHighUsnRequestId.isNull()) {
        QNDEBUG(QStringLiteral("Still pending the user's own account high USN"));
        return;
    }

    if (!m_accountHighUsnForLinkedNotebookRequestIds.empty()) {
        QNDEBUG(QStringLiteral("Still pending ") << m_accountHighUsnForLinkedNotebookRequestIds.size()
                << QStringLiteral(" high USN requests for linked notebooks"));
        return;
    }

    QNDEBUG(QStringLiteral("Finished collecting high USNs: user's own high USN = ") << m_accountHighUsn);

    if (QuentierIsLogLevelActive(LogLevel::TraceLevel))
    {
        QTextStream strm;
        strm << QStringLiteral("High USNs for linked notebooks:\n");
        for(auto it = m_accountHighUsnForLinkedNotebooksByLinkedNotebookGuid.constBegin(),
            end = m_accountHighUsnForLinkedNotebooksByLinkedNotebookGuid.constEnd(); it != end; ++it)
        {
            strm << QStringLiteral("[") << it.key() << QStringLiteral("]: ") << it.value() << QStringLiteral("\n");
        }

        QNTRACE(strm.readAll());
    }

    emit collectedHighUpdateSequenceNumbers(m_accountHighUsn, m_accountHighUsnForLinkedNotebooksByLinkedNotebookGuid);

    m_accountHighUsn = -1;
    m_accountHighUsnForLinkedNotebooksByLinkedNotebookGuid.clear();
    m_collectHighUsnRequested = false;
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

void RemoteToLocalSynchronizationManager::checkNotebooksAndTagsSyncAndLaunchNotesSync()
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::checkNotebooksAndTagsSyncAndLaunchNotesSync"));

    if (m_updateNotebookRequestIds.empty() && m_addNotebookRequestIds.empty() &&
        m_updateTagRequestIds.empty() && m_addTagRequestIds.empty())
    {
        // All remote notebooks and tags were already either updated in the local storage or added there
        launchNotesSync();
    }
}

void RemoteToLocalSynchronizationManager::launchNotesSync()
{
    launchDataElementSync<NotesList, Note>(ContentSource::UserAccount, QStringLiteral("Note"), m_notes, m_expungedNotes);
}

void RemoteToLocalSynchronizationManager::checkNotesSyncAndLaunchResourcesSync()
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::checkNotesSyncAndLaunchResourcesSync"));

    if (m_lastSyncMode != SyncMode::IncrementalSync) {
        return;
    }

    if (m_updateNoteRequestIds.empty() && m_addNoteRequestIds.empty() && m_notesToAddPerAPICallPostponeTimerId.empty() &&
        m_notesToUpdatePerAPICallPostponeTimerId.empty())
    {
        // All remote notes were already either updated in the local storage or added there
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

    if (m_updateLinkedNotebookRequestIds.empty() && m_addLinkedNotebookRequestIds.empty()) {
        // All remote linked notebooks were already updated in the local storage or added there
        startLinkedNotebooksSync();
    }
}

void RemoteToLocalSynchronizationManager::checkLinkedNotebooksNotebooksAndTagsSyncAndLaunchLinkedNotebookNotesSync()
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::checkLinkedNotebooksNotebooksAndTagsSyncAndLaunchLinkedNotebookNotesSync"));

    if (m_updateNotebookRequestIds.empty() && m_addNotebookRequestIds.empty() &&
        m_updateTagRequestIds.empty() && m_addTagRequestIds.empty())
    {
        // All remote notebooks and tags from linked notebooks were already either updated in the local storage or added there
        launchLinkedNotebooksNotesSync();
    }
}

void RemoteToLocalSynchronizationManager::launchLinkedNotebooksContentsSync()
{
    launchLinkedNotebooksTagsSync();
    launchLinkedNotebooksNotebooksSync();
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
            emit failure(error);
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
            emit failure(error);
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
        requestAllLinkedNotebooks(ListLinkedNotebooksContext::StartLinkedNotebooksSync);
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
            emit failure(error);
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
            emit failure(error);
            return false;
        }

        const qevercloud::Timestamp & expirationTime = it.value();
        const qevercloud::Timestamp currentTime = QDateTime::currentMSecsSinceEpoch();
        if (currentTime - expirationTime < SIX_HOURS_IN_MSEC) {
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

    QVector<QPair<QString,QString> > linkedNotebookGuidsAndSharedNotebookGlobalIds;
    const int numAllLinkedNotebooks = m_allLinkedNotebooks.size();
    linkedNotebookGuidsAndSharedNotebookGlobalIds.reserve(numAllLinkedNotebooks);

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
            emit failure(error);
            return;
        }

        if (!currentLinkedNotebook.hasSharedNotebookGlobalId())
        {
            ErrorString error(QT_TR_NOOP("Internal error: found linked notebook without shared notebook global id"));
            if (currentLinkedNotebook.hasUsername()) {
                error.details() = currentLinkedNotebook.username();
            }

            QNWARNING(error << QStringLiteral(", linked notebook: ") << currentLinkedNotebook);
            emit failure(error);
            return;
        }

        linkedNotebookGuidsAndSharedNotebookGlobalIds << QPair<QString,QString>(currentLinkedNotebook.guid(),
                                                                                currentLinkedNotebook.sharedNotebookGlobalId());
    }

    emit requestAuthenticationTokensForLinkedNotebooks(linkedNotebookGuidsAndSharedNotebookGlobalIds);
    m_pendingAuthenticationTokensForLinkedNotebooks = true;
}

void RemoteToLocalSynchronizationManager::requestAllLinkedNotebooks(const ListLinkedNotebooksContext::type context)
{
    QNDEBUG("RemoteToLocalSynchronizationManager::requestAllLinkedNotebooks: context = " << context);

    m_listAllLinkedNotebooksContext = context;

    size_t limit = 0, offset = 0;
    LocalStorageManager::ListLinkedNotebooksOrder::type order = LocalStorageManager::ListLinkedNotebooksOrder::NoOrder;
    LocalStorageManager::OrderDirection::type orderDirection = LocalStorageManager::OrderDirection::Ascending;

    m_listAllLinkedNotebooksRequestId = QUuid::createUuid();
    QNTRACE(QStringLiteral("Emitting the request to list linked notebooks: request id = ") << m_listAllLinkedNotebooksRequestId);
    emit listAllLinkedNotebooks(limit, offset, order, orderDirection, m_listAllLinkedNotebooksRequestId);
}

void RemoteToLocalSynchronizationManager::getLinkedNotebookSyncState(const LinkedNotebook & linkedNotebook,
                                                                     const QString & authToken, qevercloud::SyncState & syncState,
                                                                     bool & asyncWait, bool & error)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::getLinkedNotebookSyncState"));

    asyncWait = false;
    error = false;

    ErrorString errorDescription;
    qint32 rateLimitSeconds = 0;
    qint32 errorCode = m_noteStore.getLinkedNotebookSyncState(linkedNotebook.qevercloudLinkedNotebook(), authToken,
                                                              syncState, errorDescription, rateLimitSeconds);
    if (errorCode == qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED)
    {
        if (rateLimitSeconds <= 0) {
            errorDescription.setBase(QT_TR_NOOP("Rate limit reached but the number of seconds to wait is incorrect"));
            errorDescription.details() = QString::number(rateLimitSeconds);
            emit failure(errorDescription);
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
            emit failure(errorMessage);
            error = true;
            return;
        }

        m_getLinkedNotebookSyncStateBeforeStartAPICallPostponeTimerId = timerId;

        QNDEBUG(QStringLiteral("Rate limit exceeded, need to wait for ") << rateLimitSeconds << QStringLiteral(" seconds"));
        emit rateLimitExceeded(rateLimitSeconds);
        asyncWait = true;
        return;
    }
    else if (errorCode == qevercloud::EDAMErrorCode::AUTH_EXPIRED)
    {
        ErrorString errorMessage(QT_TR_NOOP("Unexpected AUTH_EXPIRED error when trying to get the linked notebook sync state"));
        errorMessage.additionalBases().append(errorDescription.base());
        errorMessage.additionalBases().append(errorDescription.additionalBases());
        errorMessage.details() = errorDescription.details();
        emit failure(errorMessage);
        error = true;
        return;
    }
    else if (errorCode != 0)
    {
        ErrorString errorMessage(QT_TR_NOOP("Failed to get the linked notebook sync state"));
        errorMessage.additionalBases().append(errorDescription.base());
        errorMessage.additionalBases().append(errorDescription.additionalBases());
        errorMessage.details() = errorDescription.details();
        emit failure(errorMessage);
        error = true;
        return;
    }
}

bool RemoteToLocalSynchronizationManager::downloadLinkedNotebooksSyncChunks()
{
    qevercloud::SyncChunk * pSyncChunk = Q_NULLPTR;

    QNDEBUG(QStringLiteral("Downloading linked notebook sync chunks:"));

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
            emit failure(error);
            return false;
        }

        const QString & linkedNotebookGuid = linkedNotebook.guid();

        bool fullSyncOnly = false;

        auto lastSynchronizedUsnIt = m_lastSynchronizedUsnByLinkedNotebookGuid.find(linkedNotebookGuid);
        if (lastSynchronizedUsnIt == m_lastSynchronizedUsnByLinkedNotebookGuid.end()) {
            lastSynchronizedUsnIt = m_lastSynchronizedUsnByLinkedNotebookGuid.insert(linkedNotebookGuid, 0);
            fullSyncOnly = true;
        }
        qint32 afterUsn = lastSynchronizedUsnIt.value();

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

        auto it = m_authenticationTokensAndShardIdsByLinkedNotebookGuid.find(linkedNotebookGuid);
        if (it == m_authenticationTokensAndShardIdsByLinkedNotebookGuid.end())
        {
            ErrorString error(QT_TR_NOOP("can't find the authentication token for one of linked notebooks"));
            if (linkedNotebook.hasUsername()) {
                error.details() = linkedNotebook.username();
            }

            QNWARNING(error << QStringLiteral(": ") << linkedNotebook);
            emit failure(error);
            return false;
        }

        const QString & authToken = it.value().first;

        if (m_onceSyncDone || (afterUsn != 0))
        {
            auto syncStateIter = m_syncStatesByLinkedNotebookGuid.find(linkedNotebookGuid);
            if (syncStateIter == m_syncStatesByLinkedNotebookGuid.end())
            {
                qevercloud::SyncState syncState;
                bool error = false;
                bool asyncWait = false;
                getLinkedNotebookSyncState(linkedNotebook, authToken, syncState, asyncWait, error);
                if (asyncWait || error) {
                    return false;
                }

                syncStateIter = m_syncStatesByLinkedNotebookGuid.insert(linkedNotebookGuid, syncState);
            }

            const qevercloud::SyncState & syncState = syncStateIter.value();

            if (syncState.fullSyncBefore > lastSyncTime)
            {
                QNDEBUG(QStringLiteral("Linked notebook sync state says the time has come to do the full sync"));
                afterUsn = 0;
                if (!m_onceSyncDone) {
                    fullSyncOnly = true;
                }
                m_lastSyncMode = SyncMode::FullSync;
            }
            else if (syncState.updateCount == lastUpdateCount)
            {
                QNDEBUG(QStringLiteral("Server has no updates for data in this linked notebook, continuing with the next one"));
                Q_UNUSED(m_linkedNotebookGuidsForWhichSyncChunksWereDownloaded.insert(linkedNotebookGuid));
                continue;
            }
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
            qint32 errorCode = m_noteStore.getLinkedNotebookSyncChunk(linkedNotebook.qevercloudLinkedNotebook(),
                                                                      afterUsn, m_maxSyncChunkEntries,
                                                                      authToken, fullSyncOnly, *pSyncChunk,
                                                                      errorDescription, rateLimitSeconds);
            if (errorCode == qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED)
            {
                if (rateLimitSeconds <= 0) {
                    errorDescription.setBase(QT_TR_NOOP("Rate limit reached but the number of seconds to wait is incorrect"));
                    errorDescription.details() = QString::number(rateLimitSeconds);
                    emit failure(errorDescription);
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
                    emit failure(errorMessage);
                    return false;
                }

                m_downloadLinkedNotebookSyncChunkAPICallPostponeTimerId = timerId;

                QNDEBUG(QStringLiteral("Rate limit exceeded, need to wait for ") << rateLimitSeconds << QStringLiteral(" seconds"));
                emit rateLimitExceeded(rateLimitSeconds);
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
                emit failure(errorMessage);
                return false;
            }
            else if (errorCode != 0)
            {
                ErrorString errorMessage(QT_TR_NOOP("Failed to download the sync chunks for linked notebooks content"));
                errorMessage.additionalBases().append(errorDescription.base());
                errorMessage.additionalBases().append(errorDescription.additionalBases());
                errorMessage.details() = errorDescription.details();
                QNDEBUG(errorMessage);
                emit failure(errorMessage);
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

        lastSynchronizedUsnIt.value() = afterUsn;
        lastSyncTimeIt.value() = lastSyncTime;
        lastUpdateCountIt.value() = lastUpdateCount;
        Q_UNUSED(m_linkedNotebookGuidsForWhichSyncChunksWereDownloaded.insert(linkedNotebook.guid()));
    }

    QNDEBUG(QStringLiteral("Done. Processing content pointed to by linked notebooks from buffered sync chunks"));

    m_syncStatesByLinkedNotebookGuid.clear();   // don't need this anymore, it only served the purpose of preventing multiple get sync state calls for the same linked notebook

    m_linkedNotebooksSyncChunksDownloaded = true;
    emit linkedNotebooksSyncChunksDownloaded();

    return true;
}

void RemoteToLocalSynchronizationManager::launchLinkedNotebooksTagsSync()
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::launchLinkedNotebooksTagsSync"));
    QList<QString> dummyList;
    launchDataElementSync<TagsList, Tag>(ContentSource::LinkedNotebook, QStringLiteral("Tag"), m_tags, dummyList);
}

void RemoteToLocalSynchronizationManager::launchLinkedNotebooksNotebooksSync()
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::launchLinkedNotebooksNotebooksSync"));
    QList<QString> dummyList;
    launchDataElementSync<NotebooksList, Notebook>(ContentSource::LinkedNotebook, QStringLiteral("Notebook"), m_notebooks, dummyList);
}

void RemoteToLocalSynchronizationManager::launchLinkedNotebooksNotesSync()
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::launchLinkedNotebooksNotesSync"));
    launchDataElementSync<NotesList, Note>(ContentSource::LinkedNotebook, QStringLiteral("Note"), m_notes, m_expungedNotes);
}

bool RemoteToLocalSynchronizationManager::hasPendingRequests() const
{
    return !(m_findTagByNameRequestIds.empty() &&
             m_findTagByGuidRequestIds.empty() &&
             m_addTagRequestIds.empty() &&
             m_updateTagRequestIds.empty() &&
             m_expungeTagRequestIds.empty() &&
             m_expungeNotelessTagsRequestId.isNull() &&
             m_findSavedSearchByNameRequestIds.empty() &&
             m_findSavedSearchByGuidRequestIds.empty() &&
             m_addSavedSearchRequestIds.empty() &&
             m_updateSavedSearchRequestIds.empty() &&
             m_expungeSavedSearchRequestIds.empty() &&
             m_findLinkedNotebookRequestIds.empty() &&
             m_addLinkedNotebookRequestIds.empty() &&
             m_updateLinkedNotebookRequestIds.empty() &&
             m_expungeLinkedNotebookRequestIds.empty() &&
             m_findNotebookByNameRequestIds.empty() &&
             m_findNotebookByGuidRequestIds.empty() &&
             m_addNotebookRequestIds.empty() &&
             m_updateNotebookRequestIds.empty() &&
             m_expungeNotebookRequestIds.empty() &&
             m_findNoteByGuidRequestIds.empty() &&
             m_addNoteRequestIds.empty() &&
             m_updateNoteRequestIds.empty() &&
             m_expungeNoteRequestIds.empty() &&
             m_findResourceByGuidRequestIds.empty() &&
             m_addResourceRequestIds.empty() &&
             m_updateResourceRequestIds.empty());
}

void RemoteToLocalSynchronizationManager::checkServerDataMergeCompletion()
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::checkServerDataMergeCompletion"));

    // Need to check whether we are still waiting for the response from some add or update request
    bool tagsReady = m_findTagByGuidRequestIds.empty() && m_findTagByNameRequestIds.empty() &&
                     m_updateTagRequestIds.empty() && m_addTagRequestIds.empty();
    if (!tagsReady) {
        QNDEBUG(QStringLiteral("Tags are not ready, pending response for ") << m_updateTagRequestIds.size()
                << QStringLiteral(" tag update requests and/or ") << m_addTagRequestIds.size() << QStringLiteral(" tag add requests and/or ")
                << m_findTagByGuidRequestIds.size() << QStringLiteral(" find tag by guid requests and/or ")
                << m_findTagByNameRequestIds.size() << QStringLiteral(" find tag by name requests"));
        return;
    }

    bool searchesReady = m_findSavedSearchByGuidRequestIds.empty() && m_findSavedSearchByNameRequestIds.empty() &&
                         m_updateSavedSearchRequestIds.empty() && m_addSavedSearchRequestIds.empty();
    if (!searchesReady) {
        QNDEBUG(QStringLiteral("Saved searches are not ready, pending response for ") << m_updateSavedSearchRequestIds.size()
                << QStringLiteral(" saved search update requests and/or ") << m_addSavedSearchRequestIds.size()
                << QStringLiteral(" saved search add requests and/or ") << m_findSavedSearchByGuidRequestIds.size()
                << QStringLiteral(" find saved search by guid requests and/or ") << m_findSavedSearchByNameRequestIds.size()
                << QStringLiteral(" find saved search by name requests"));
        return;
    }

    bool linkedNotebooksReady = m_findLinkedNotebookRequestIds.empty() && m_updateLinkedNotebookRequestIds.empty() &&
                                m_addLinkedNotebookRequestIds.empty();
    if (!linkedNotebooksReady) {
        QNDEBUG(QStringLiteral("Linked notebooks are not ready, pending response for ") << m_updateLinkedNotebookRequestIds.size()
                << QStringLiteral(" linked notebook update requests and/or ") << m_addLinkedNotebookRequestIds.size()
                << QStringLiteral(" linked notebook add requests and/or ") << m_findLinkedNotebookRequestIds.size()
                << QStringLiteral(" find linked notebook requests"));
        return;
    }

    bool notebooksReady = m_findNotebookByGuidRequestIds.empty() && m_findNotebookByNameRequestIds.empty() &&
                          m_updateNotebookRequestIds.empty() && m_addNotebookRequestIds.empty();
    if (!notebooksReady) {
        QNDEBUG(QStringLiteral("Notebooks are not ready, pending response for ") << m_updateNotebookRequestIds.size()
                << QStringLiteral(" notebook update requests and/or ") << m_addNotebookRequestIds.size()
                << QStringLiteral(" notebook add requests and/or ") << m_findNotebookByGuidRequestIds.size()
                << QStringLiteral(" find notebook by guid requests and/or ") << m_findNotebookByNameRequestIds.size()
                << QStringLiteral(" find notebook by name requests"));
        return;
    }

    bool notesReady = m_findNoteByGuidRequestIds.empty() && m_updateNoteRequestIds.empty() && m_addNoteRequestIds.empty() &&
                      m_guidsOfNotesPendingDownloadForAddingToLocalStorage.empty() &&
                      m_guidsOfNotesPendingDownloadForUpdatingInLocalStorage.empty() &&
                      m_notesToAddPerAPICallPostponeTimerId.empty() && m_notesToUpdatePerAPICallPostponeTimerId.empty() &&
                      m_resourceGuidsPendingInkNoteImageDownloadPerNoteGuid.empty() &&
                      m_notesPendingInkNoteImagesDownloadByFindNotebookRequestId.empty() &&
                      m_notesPendingThumbnailDownloadByFindNotebookRequestId.empty() &&
                      m_notesPendingThumbnailDownloadByGuid.empty() &&
                      m_updateNoteWithThumbnailRequestIds.empty();
    if (!notesReady)
    {
        QNDEBUG(QStringLiteral("Notes are not ready, pending response for ") << m_updateNoteRequestIds.size()
                << QStringLiteral(" note update requests and/or ") << m_addNoteRequestIds.size()
                << QStringLiteral(" note add requests and/or ") << m_findNoteByGuidRequestIds.size()
                << QStringLiteral(" find note by guid requests and/or ") << m_guidsOfNotesPendingDownloadForAddingToLocalStorage.size()
                << QStringLiteral(" async full new note data downloads and/or ") << m_guidsOfNotesPendingDownloadForUpdatingInLocalStorage.size()
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
        bool resourcesReady = m_findResourceByGuidRequestIds.empty() && m_updateResourceRequestIds.empty() &&
                              m_addResourceRequestIds.empty() && m_resourcesWithFindRequestIdsPerFindNoteRequestId.empty() &&
                              m_inkNoteResourceDataPerFindNotebookRequestId.empty() &&
                              m_resourceGuidsPendingInkNoteImageDownloadPerNoteGuid.empty();
        if (!resourcesReady)
        {
            QNDEBUG(QStringLiteral("Resources are not ready, pending response for ") << m_updateResourceRequestIds.size()
                    << QStringLiteral(" resource update requests and/or ") << m_addResourceRequestIds.size()
                    << QStringLiteral(" resource add requests and/or ") << m_resourcesWithFindRequestIdsPerFindNoteRequestId.size()
                    << QStringLiteral(" find resource by guid requests and/or ") << m_findResourceByGuidRequestIds.size()
                    << QStringLiteral(" resource find note requests and/or ") << m_inkNoteResourceDataPerFindNotebookRequestId.size()
                    << QStringLiteral(" resource find notebook for ink note image download processing and/or ") << m_resourceGuidsPendingInkNoteImageDownloadPerNoteGuid.size()
                    << QStringLiteral(" pending ink note image downloads"));
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

        m_expungeNotelessTagsRequestId = QUuid::createUuid();
        QNTRACE(QStringLiteral("Emitting the request to expunge noteless tags from linked notebooks: ")
                << m_expungeNotelessTagsRequestId);
        emit expungeNotelessTagsFromLinkedNotebooks(m_expungeNotelessTagsRequestId);
    }
    else
    {
        QNDEBUG(QStringLiteral("Synchronized the whole contents from user's account"));

        m_fullNoteContentsDownloaded = true;

        if (m_lastSyncMode == SyncMode::FullSync) {
            startLinkedNotebooksSync();
            return;
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
    emit finished(m_lastUpdateCount, m_lastSyncTime, m_lastUpdateCountByLinkedNotebookGuid, m_lastSyncTimeByLinkedNotebookGuid);
    clear();
    disconnectFromLocalStorage();
    m_active = false;
}

void RemoteToLocalSynchronizationManager::clear()
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::clear"));

    // NOTE: not clearing authentication tokens and shard ids; it is intentional,
    // this information can be reused in subsequent syncs

    m_lastSyncChunksDownloadedUsn = -1;
    m_syncChunksDownloaded = false;
    m_fullNoteContentsDownloaded = false;
    m_expungedFromServerToClient = false;
    m_linkedNotebooksSyncChunksDownloaded = false;

    m_active = false;
    m_paused = false;
    m_requestedToStop = false;

    m_syncChunks.clear();
    m_linkedNotebookSyncChunks.clear();
    m_linkedNotebookGuidsForWhichSyncChunksWereDownloaded.clear();

    m_tags.clear();
    m_expungedTags.clear();
    m_tagsToAddPerRequestId.clear();
    m_findTagByNameRequestIds.clear();
    m_findTagByGuidRequestIds.clear();
    m_addTagRequestIds.clear();
    m_updateTagRequestIds.clear();
    m_expungeTagRequestIds.clear();

    m_linkedNotebookGuidsByTagGuids.clear();
    m_expungeNotelessTagsRequestId = QUuid();

    m_savedSearches.clear();
    m_expungedSavedSearches.clear();
    m_savedSearchesToAddPerRequestId.clear();
    m_findSavedSearchByNameRequestIds.clear();
    m_findSavedSearchByGuidRequestIds.clear();
    m_addSavedSearchRequestIds.clear();
    m_updateSavedSearchRequestIds.clear();

    m_linkedNotebooks.clear();
    m_expungedLinkedNotebooks.clear();
    m_findLinkedNotebookRequestIds.clear();
    m_addLinkedNotebookRequestIds.clear();
    m_updateLinkedNotebookRequestIds.clear();
    m_expungeSavedSearchRequestIds.clear();

    m_allLinkedNotebooks.clear();
    m_listAllLinkedNotebooksRequestId = QUuid();
    m_allLinkedNotebooksListed = false;
    m_listAllLinkedNotebooksContext = ListLinkedNotebooksContext::StartLinkedNotebooksSync;

    m_collectHighUsnRequested = false;
    m_accountHighUsnRequestId = QUuid();
    m_accountHighUsn = -1;
    m_accountHighUsnForLinkedNotebookRequestIds.clear();
    m_accountHighUsnForLinkedNotebooksByLinkedNotebookGuid.clear();

    m_pendingAuthenticationTokensForLinkedNotebooks = false;

    m_findUserRequestId = QUuid();
    m_addOrUpdateUserRequestId = QUuid();
    m_onceAddedOrUpdatedUserInLocalStorage = false;

    m_syncStatesByLinkedNotebookGuid.clear();
    // NOTE: not clearing last synchronized usns by linked notebook guid; it is intentional,
    // this information can be reused in subsequent syncs

    m_notebooks.clear();
    m_expungedNotebooks.clear();
    m_notebooksToAddPerRequestId.clear();
    m_findNotebookByNameRequestIds.clear();
    m_findNotebookByGuidRequestIds.clear();
    m_addNotebookRequestIds.clear();
    m_updateNotebookRequestIds.clear();
    m_expungeNotebookRequestIds.clear();

    m_linkedNotebookGuidsByNotebookGuids.clear();

    m_notes.clear();
    m_expungedNotes.clear();
    m_findNoteByGuidRequestIds.clear();
    m_addNoteRequestIds.clear();
    m_updateNoteRequestIds.clear();
    m_expungeNoteRequestIds.clear();

    m_notesWithFindRequestIdsPerFindNotebookRequestId.clear();
    m_notebooksPerNoteGuids.clear();

    m_resources.clear();
    m_findResourceByGuidRequestIds.clear();
    m_addResourceRequestIds.clear();
    m_updateResourceRequestIds.clear();
    m_resourcesWithFindRequestIdsPerFindNoteRequestId.clear();
    m_inkNoteResourceDataPerFindNotebookRequestId.clear();
    m_resourceGuidsPendingInkNoteImageDownloadPerNoteGuid.clear();
    m_notesPendingInkNoteImagesDownloadByFindNotebookRequestId.clear();
    m_notesPendingThumbnailDownloadByFindNotebookRequestId.clear();
    m_notesPendingThumbnailDownloadByGuid.clear();
    m_resourceFoundFlagPerFindResourceRequestId.clear();

    m_localUidsOfElementsAlreadyAttemptedToFindByName.clear();

    m_guidsOfNotesPendingDownloadForAddingToLocalStorage.clear();
    m_guidsOfNotesPendingDownloadForUpdatingInLocalStorage.clear();

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

    auto afterUsnForSyncChunkPerAPICallPostponeTimerIdEnd = m_afterUsnForSyncChunkPerAPICallPostponeTimerId.end();
    for(auto it = m_afterUsnForSyncChunkPerAPICallPostponeTimerId.begin(); it != afterUsnForSyncChunkPerAPICallPostponeTimerIdEnd; ++it) {
        int key = it.key();
        killTimer(key);
    }
    m_afterUsnForSyncChunkPerAPICallPostponeTimerId.clear();

    if (m_getSyncStateBeforeStartAPICallPostponeTimerId != 0) {
        killTimer(m_getSyncStateBeforeStartAPICallPostponeTimerId);
        m_getSyncStateBeforeStartAPICallPostponeTimerId = 0;
    }

    if (m_getLinkedNotebookSyncStateBeforeStartAPICallPostponeTimerId != 0) {
        killTimer(m_getLinkedNotebookSyncStateBeforeStartAPICallPostponeTimerId);
        m_getLinkedNotebookSyncStateBeforeStartAPICallPostponeTimerId = 0;
    }

    if (m_downloadLinkedNotebookSyncChunkAPICallPostponeTimerId != 0) {
        killTimer(m_downloadLinkedNotebookSyncChunkAPICallPostponeTimerId);
        m_downloadLinkedNotebookSyncChunkAPICallPostponeTimerId = 0;
    }
}

void RemoteToLocalSynchronizationManager::handleLinkedNotebookAdded(const LinkedNotebook & linkedNotebook)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::handleLinkedNotebookAdded: linked notebook = ") << linkedNotebook);

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
        emit failure(errorDescription);
        return;
    }

    int timerId = pEvent->timerId();
    killTimer(timerId);
    QNDEBUG(QStringLiteral("Killed timer with id ") << timerId);

    if (m_paused)
    {
        QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager is being paused. Won't try to download full Note data "
                               "but will return the notes waiting to be downloaded into the common list of notes"));

        const int numNotesToAdd = m_notesToAddPerAPICallPostponeTimerId.size();
        const int numNotesToUpdate = m_notesToUpdatePerAPICallPostponeTimerId.size();
        const int numNotesInCommonList = m_notes.size();

        m_notes.reserve(std::max(numNotesToAdd + numNotesToUpdate + numNotesInCommonList, 0));

        typedef QHash<int,Note>::const_iterator CIter;

        CIter notesToAddEnd = m_notesToAddPerAPICallPostponeTimerId.constEnd();
        for(CIter it = m_notesToAddPerAPICallPostponeTimerId.constBegin(); it != notesToAddEnd; ++it) {
            m_notes.push_back(it.value().qevercloudNote());
        }
        m_notesToAddPerAPICallPostponeTimerId.clear();

        CIter notesToUpdateEnd = m_notesToUpdatePerAPICallPostponeTimerId.constEnd();
        for(CIter it = m_notesToUpdatePerAPICallPostponeTimerId.constBegin(); it != notesToUpdateEnd; ++it) {
            m_notes.push_back(it.value().qevercloudNote());
        }
        m_notesToUpdatePerAPICallPostponeTimerId.clear();

        m_afterUsnForSyncChunkPerAPICallPostponeTimerId.clear();
        m_getLinkedNotebookSyncStateBeforeStartAPICallPostponeTimerId = 0;
        m_downloadLinkedNotebookSyncChunkAPICallPostponeTimerId = 0;

        return;
    }

    auto noteToAddIt = m_notesToAddPerAPICallPostponeTimerId.find(timerId);
    if (noteToAddIt != m_notesToAddPerAPICallPostponeTimerId.end())
    {
        Note note = noteToAddIt.value();
        Q_UNUSED(m_notesToAddPerAPICallPostponeTimerId.erase(noteToAddIt));

        getFullNoteDataAsyncAndAddToLocalStorage(note);
        return;
    }

    auto noteToUpdateIt = m_notesToUpdatePerAPICallPostponeTimerId.find(timerId);
    if (noteToUpdateIt != m_notesToUpdatePerAPICallPostponeTimerId.end())
    {
        Note noteToUpdate = noteToUpdateIt.value();
        Q_UNUSED(m_notesToUpdatePerAPICallPostponeTimerId.erase(noteToUpdateIt));

        // NOTE: workarounding the stupidity of MSVC 2013
        emitUpdateRequest(noteToUpdate, static_cast<const Note*>(Q_NULLPTR));
        return;
    }

    auto afterUsnIt = m_afterUsnForSyncChunkPerAPICallPostponeTimerId.find(timerId);
    if (afterUsnIt != m_afterUsnForSyncChunkPerAPICallPostponeTimerId.end())
    {
        qint32 afterUsn = afterUsnIt.value();

        Q_UNUSED(m_afterUsnForSyncChunkPerAPICallPostponeTimerId.erase(afterUsnIt));

        downloadSyncChunksAndLaunchSync(afterUsn);
        return;
    }

    if (timerId == m_getLinkedNotebookSyncStateBeforeStartAPICallPostponeTimerId) {
        m_getLinkedNotebookSyncStateBeforeStartAPICallPostponeTimerId = 0;
        startLinkedNotebooksSync();
        return;
    }

    if (timerId == m_downloadLinkedNotebookSyncChunkAPICallPostponeTimerId) {
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

    ErrorString errorDescription;
    bool res = m_noteStore.getNoteAsync(withContent, withResourceData, withResourceRecognition,
                                        withResourceAlternateData, withSharedNotes,
                                        withNoteAppDataValues, withResourceAppDataValues,
                                        withNoteLimits, note.guid(), errorDescription);
    if (!res) {
        APPEND_NOTE_DETAILS(errorDescription, note)
        QNWARNING(errorDescription << QStringLiteral(", note: ") << note);
        emit failure(errorDescription);
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
        emit failure(errorDescription);
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
        emit failure(errorDescription);
        return;
    }

    QString noteGuid = note.guid();

    auto it = m_guidsOfNotesPendingDownloadForUpdatingInLocalStorage.find(noteGuid);
    if (Q_UNLIKELY(it != m_guidsOfNotesPendingDownloadForUpdatingInLocalStorage.end())) {
        QNDEBUG(QStringLiteral("Note with guid ") << noteGuid << QStringLiteral(" is already being downloaded"));
        return;
    }

    QNTRACE(QStringLiteral("Adding note guid into the list of those pending download for update in the local storage: ")
            << noteGuid);
    Q_UNUSED(m_guidsOfNotesPendingDownloadForUpdatingInLocalStorage.insert(noteGuid))

    getFullNoteDataAsync(note);
}

void RemoteToLocalSynchronizationManager::downloadSyncChunksAndLaunchSync(qint32 afterUsn)
{
    qevercloud::SyncChunk * pSyncChunk = Q_NULLPTR;

    QNDEBUG(QStringLiteral("Downloading sync chunks:"));

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
        qint32 errorCode = m_noteStore.getSyncChunk(afterUsn, m_maxSyncChunkEntries, filter,
                                                    *pSyncChunk, errorDescription, rateLimitSeconds);
        if (errorCode == qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED)
        {
            if (rateLimitSeconds <= 0) {
                errorDescription.setBase(QT_TR_NOOP("Rate limit reached but the number of seconds to wait is incorrect"));
                errorDescription.details() = QString::number(rateLimitSeconds);
                emit failure(errorDescription);
                return;
            }

            m_syncChunks.pop_back();

            int timerId = startTimer(SEC_TO_MSEC(rateLimitSeconds));
            if (Q_UNLIKELY(timerId == 0)) {
                ErrorString errorDescription(QT_TR_NOOP("Failed to start a timer to postpone the Evernote API call "
                                                        "due to rate limit exceeding"));
                emit failure(errorDescription);
                return;
            }

            m_afterUsnForSyncChunkPerAPICallPostponeTimerId[timerId] = afterUsn;
            emit rateLimitExceeded(rateLimitSeconds);
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
            emit failure(errorMessage);
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
    emit syncChunksDownloaded();

    launchSync();
}

const Notebook * RemoteToLocalSynchronizationManager::getNotebookPerNote(const Note & note) const
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::getNotebookPerNote: note = ") << note);

    QString noteGuid = (note.hasGuid() ? note.guid() : QString());
    QString noteLocalUid = note.localUid();

    QPair<QString,QString> key(noteGuid, noteLocalUid);
    QHash<QPair<QString,QString>,Notebook>::const_iterator cit = m_notebooksPerNoteGuids.find(key);
    if (cit == m_notebooksPerNoteGuids.end()) {
        return Q_NULLPTR;
    }
    else {
        return &(cit.value());
    }
}

void RemoteToLocalSynchronizationManager::handleAuthExpiration()
{
    QNINFO(QStringLiteral("Got AUTH_EXPIRED error, pausing the sync and requesting a new authentication token"));

    m_paused = true;
    emit paused(/* pending authentication = */ true);

    m_pendingAuthenticationTokenAndShardId = true;
    emit requestAuthenticationToken();
}

bool RemoteToLocalSynchronizationManager::checkUserAccountSyncState(bool & asyncWait, bool & error, qint32 & afterUsn)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::checkUsersAccountSyncState"));

    asyncWait = false;
    error = false;

    ErrorString errorDescription;
    qint32 rateLimitSeconds = 0;
    qevercloud::SyncState state;
    qint32 errorCode = m_noteStore.getSyncState(state, errorDescription, rateLimitSeconds);
    if (errorCode == qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED)
    {
        if (rateLimitSeconds <= 0) {
            errorDescription.setBase(QT_TR_NOOP("Rate limit reached but the number of seconds to wait is incorrect"));
            errorDescription.details() = QString::number(rateLimitSeconds);
            emit failure(errorDescription);
            error = true;
            return false;
        }

        m_getSyncStateBeforeStartAPICallPostponeTimerId = startTimer(SEC_TO_MSEC(rateLimitSeconds));
        if (Q_UNLIKELY(m_getSyncStateBeforeStartAPICallPostponeTimerId == 0)) {
            errorDescription.setBase(QT_TR_NOOP("Failed to start a timer to postpone "
                                                "the Evernote API call due to rate limit exceeding"));
            emit failure(errorDescription);
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
        emit failure(errorDescription);
        error = true;
        return false;
    }

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

    if (m_allLinkedNotebooks.empty()) {
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
            emit failure(errorMessage);
            QNWARNING(errorMessage << QStringLiteral(", linked notebook: ") << linkedNotebook);
            error = true;
            return false;
        }

        const QString & linkedNotebookGuid = linkedNotebook.guid();

        auto it = m_authenticationTokensAndShardIdsByLinkedNotebookGuid.find(linkedNotebookGuid);
        if (it == m_authenticationTokensAndShardIdsByLinkedNotebookGuid.end()) {
            QNINFO(QStringLiteral("Detected missing auth token for one of linked notebooks. Will request auth tokens "
                                  "for all linked notebooks now. Linked notebook without auth token: ") << linkedNotebook);
            requestAuthenticationTokensForAllLinkedNotebooks();
            asyncWait = true;
            return false;
        }

        const QString & authToken = it.value().first;

        auto lastUpdateCountIt = m_lastUpdateCountByLinkedNotebookGuid.find(linkedNotebookGuid);
        if (lastUpdateCountIt == m_lastUpdateCountByLinkedNotebookGuid.end()) {
            lastUpdateCountIt = m_lastUpdateCountByLinkedNotebookGuid.insert(linkedNotebookGuid, 0);
        }
        qint32 lastUpdateCount = lastUpdateCountIt.value();

        qevercloud::SyncState state;
        getLinkedNotebookSyncState(linkedNotebook, authToken, state, asyncWait, error);
        if (asyncWait || error) {
            return false;
        }

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
    emit findNotebook(dummyNotebook, findNotebookForInkNoteImageDownloadRequestId);

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
    emit findNotebook(dummyNotebook, findNotebookForNoteThumbnailDownloadRequestId);

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
    // WARNING: it seems it's not possible to run note thumbnails downloading
    // in a different thread, the error like this might appear: QObject: Cannot
    // create children for a parent that is in a different thread.
    // (Parent is QNetworkAccessManager(0x499b900), parent's thread is QThread(0x1b535b0), current thread is QThread(0x42ed270)

    // QThreadPool::globalInstance()->start(pDownloader);

    pDownloader->run();
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

    if (syncChunk.tags.isSet()) {
        const auto & tags = syncChunk.tags.ref();
        container.append(tags);
    }

    if (syncChunk.expungedTags.isSet())
    {
        const auto & expungedTags = syncChunk.expungedTags.ref();
        const auto expungedTagsEnd = expungedTags.end();
        for(auto eit = expungedTags.begin(); eit != expungedTagsEnd; ++eit)
        {
            const QString & tagGuid = *eit;
            if (Q_UNLIKELY(tagGuid.isEmpty())) {
                QNWARNING(QStringLiteral("Found empty expunged tag guid within the sync chunk"));
                continue;
            }

            TagsList::iterator it = std::find_if(container.begin(), container.end(),
                                                 CompareItemByGuid<qevercloud::Tag>(tagGuid));
            if (it == container.end()) {
                continue;
            }

            Q_UNUSED(container.erase(it));
        }
    }
}

template <>
void RemoteToLocalSynchronizationManager::appendDataElementsFromSyncChunkToContainer<RemoteToLocalSynchronizationManager::SavedSearchesList>(const qevercloud::SyncChunk & syncChunk,
                                                                                                                                             RemoteToLocalSynchronizationManager::SavedSearchesList & container)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::appendDataElementsFromSyncChunkToContainer: saved searches"));

    if (syncChunk.searches.isSet()) {
        container.append(syncChunk.searches.ref());
    }

    if (syncChunk.expungedSearches.isSet())
    {
        const auto & expungedSearches = syncChunk.expungedSearches.ref();
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

    if (syncChunk.linkedNotebooks.isSet()) {
        container.append(syncChunk.linkedNotebooks.ref());
    }

    if (syncChunk.expungedLinkedNotebooks.isSet())
    {
        const auto & expungedLinkedNotebooks = syncChunk.expungedLinkedNotebooks.ref();
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

    if (syncChunk.notebooks.isSet()) {
        container.append(syncChunk.notebooks.ref());
    }

    if (syncChunk.expungedNotebooks.isSet())
    {
        const auto & expungedNotebooks = syncChunk.expungedNotebooks.ref();
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
        container.append(syncChunk.notes.ref());

        if (!m_expungedNotes.isEmpty())
        {
            const auto & syncChunkNotes = syncChunk.notes.ref();
            auto syncChunkNotesEnd = syncChunkNotes.constEnd();
            for(auto it = syncChunkNotes.constBegin(); it != syncChunkNotesEnd; ++it)
            {
                const qevercloud::Note & note = *it;
                if (!note.guid.isSet()) {
                    continue;
                }

                QList<QString>::iterator eit = std::find(m_expungedNotes.begin(), m_expungedNotes.end(), note.guid.ref());
                if (eit != m_expungedNotes.end()) {
                    QNINFO(QStringLiteral("Note with guid ") << note.guid.ref() << QStringLiteral(" and title ")
                           << (note.title.isSet() ? note.title.ref() : QStringLiteral("<no title>"))
                           << QStringLiteral(" is present both within the sync chunk's notes and in the list of "
                                             "previously collected expunged notes. That most probably means that the note "
                                             "belongs to the shared notebook and it was moved from one shared notebook "
                                             "to another one; removing the note from the list of notes waiting to be expunged"));
                    Q_UNUSED(m_expungedNotes.erase(eit));
                }
            }
        }
    }


    if (syncChunk.expungedNotes.isSet())
    {
        const auto & expungedNotes = syncChunk.expungedNotes.ref();
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

    if (syncChunk.resources.isSet()) {
        const auto & resources = syncChunk.resources.ref();
        container.append(resources);
    }
}

#define SET_ITEM_TYPE_TO_ERROR() \
    errorDescription.details() = QStringLiteral("item type = "); \
    errorDescription.details() += typeName

#define SET_CANT_FIND_BY_NAME_ERROR() \
    ErrorString errorDescription(QT_TR_NOOP("Found a data item with empty name in the local storage")); \
    SET_ITEM_TYPE_TO_ERROR(); \
    QNWARNING(errorDescription << QStringLiteral(": ") << element)

#define SET_CANT_FIND_BY_GUID_ERROR() \
    ErrorString errorDescription(QT_TR_NOOP("Found a data item with empty guid in the local storage")); \
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
        emit failure(errorDescription);
        return container.end();
    }

    if (container.empty()) {
        SET_EMPTY_PENDING_LIST_ERROR();
        emit failure(errorDescription);
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
            emit failure(errorDescription);
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
        emit failure(errorDescription);
        return container.end();
    }

    if (container.empty()) {
        SET_EMPTY_PENDING_LIST_ERROR();
        emit failure(errorDescription);
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
            emit failure(errorDescription);
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
        emit failure(errorDescription);
        return container.end();
    }

    if (container.empty()) {
        SET_EMPTY_PENDING_LIST_ERROR();
        emit failure(errorDescription);
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
            emit failure(errorDescription);
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

template <class ContainerType, class ElementType>
void RemoteToLocalSynchronizationManager::launchDataElementSync(const ContentSource::type contentSource, const QString & typeName,
                                                                ContainerType & container, QList<QString> & expungedElements)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::launchDataElementSync: ") << typeName);

    bool syncingUserAccountData = (contentSource == ContentSource::UserAccount);
    const auto & syncChunks = (syncingUserAccountData ? m_syncChunks : m_linkedNotebookSyncChunks);

    container.clear();
    int numSyncChunks = syncChunks.size();
    for(int i = 0; i < numSyncChunks; ++i) {
        const qevercloud::SyncChunk & syncChunk = syncChunks[i];
        appendDataElementsFromSyncChunkToContainer<ContainerType>(syncChunk, container);
        extractExpungedElementsFromSyncChunk<ElementType>(syncChunk, expungedElements);
    }

    if (container.empty()) {
        return;
    }

    int numElements = container.size();

    if (typeName == QStringLiteral("Note")) {
        m_originalNumberOfNotes = static_cast<quint32>(std::max(numElements, 0));
        m_numNotesDownloaded = static_cast<quint32>(0);
    }

    for(int i = 0; i < numElements; ++i)
    {
        const typename ContainerType::value_type & element = container[i];
        if (!element.guid.isSet()) {
            SET_CANT_FIND_BY_GUID_ERROR();
            emit failure(errorDescription);
            return;
        }

        emitFindByGuidRequest<ElementType>(element.guid.ref());
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
        emit failure(errorDescription);
        return;
    }

    QUuid findElementRequestId = QUuid::createUuid();
    Q_UNUSED(m_findTagByNameRequestIds.insert(findElementRequestId));
    QNTRACE(QStringLiteral("Emitting the request to find tag in the local storage: request id = ")
            << findElementRequestId << QStringLiteral(", tag: ") << tag);
    emit findTag(tag, findElementRequestId);
}

template <>
void RemoteToLocalSynchronizationManager::emitFindByNameRequest<SavedSearch>(const SavedSearch & search)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::emitFindByNameRequest<SavedSearch>: ") << search);

    if (!search.hasName()) {
        ErrorString errorDescription(QT_TR_NOOP("Detected saved search from remote storage which needs to be "
                                                "searched by name in the local storage but it has no name set"));
        QNWARNING(errorDescription << QStringLiteral(": ") << search);
        emit failure(errorDescription);
    }

    QUuid findElementRequestId = QUuid::createUuid();
    Q_UNUSED(m_findSavedSearchByNameRequestIds.insert(findElementRequestId));
    QNTRACE(QStringLiteral("Emitting the request to find saved search in the local storage: request id = ")
            << findElementRequestId << QStringLiteral(", saved search: ") << search);
    emit findSavedSearch(search, findElementRequestId);
}

template <>
void RemoteToLocalSynchronizationManager::emitFindByNameRequest<Notebook>(const Notebook & notebook)
{
    QNDEBUG(QStringLiteral("RemoteToLocalSynchronizationManager::emitFindByNameRequest<Notebook>: ") << notebook);

    if (!notebook.hasName()) {
        ErrorString errorDescription(QT_TR_NOOP("Detected notebook from remote storage which needs to be "
                                                "searched by name in the local storage but it has no name set"));
        QNWARNING(errorDescription << QStringLiteral(": ") << notebook);
        emit failure(errorDescription);
    }

    QUuid findElementRequestId = QUuid::createUuid();
    Q_UNUSED(m_findNotebookByNameRequestIds.insert(findElementRequestId));
    QNTRACE(QStringLiteral("Emitting the request to find notebook in the local storage by name: request id = ")
            << findElementRequestId << QStringLiteral(", notebook: ") << notebook);
    emit findNotebook(notebook, findElementRequestId);
}

template <class ContainerType, class ElementType>
bool RemoteToLocalSynchronizationManager::onFoundDuplicateByName(ElementType element, const QUuid & requestId,
                                                                 const QString & typeName, ContainerType & container,
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
        emit failure(errorDescription);
        return true;
    }

    resolveSyncConflict(remoteElement, element);

    Q_UNUSED(container.erase(it))
    return true;
}

template <class ElementType, class ContainerType>
bool RemoteToLocalSynchronizationManager::onFoundDuplicateByGuid(ElementType element, const QUuid & requestId,
                                                                 const QString & typeName, ContainerType & container,
                                                                 QSet<QUuid> & findByGuidRequestIds)
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
        emit failure(errorDescription);
        return true;
    }

    typedef typename ContainerType::value_type RemoteElementType;
    const RemoteElementType & remoteElement = *it;
    if (!remoteElement.updateSequenceNum.isSet()) {
        ErrorString errorDescription(QT_TR_NOOP("Found a remote data item without the update sequence number"));
        SET_ITEM_TYPE_TO_ERROR();
        QNWARNING(errorDescription << QStringLiteral(": ") << remoteElement);
        emit failure(errorDescription);
        return true;
    }

    resolveSyncConflict(remoteElement, element);

    Q_UNUSED(container.erase(it))
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

    // This element wasn't found in the local storage by guid or name ==> it's new from remote storage, adding it
    ElementType newElement(*it);
    setNonLocalAndNonDirty(newElement);
    checkAndAddLinkedNotebookBinding(element, newElement);

    emitAddRequest(newElement);

    // also removing the element from the list of ones waiting for processing
    Q_UNUSED(container.erase(it));

    return true;
}

template <class ElementType>
void RemoteToLocalSynchronizationManager::checkAndAddLinkedNotebookBinding(const ElementType & sourceElement,
                                                                           ElementType & targetElement)
{
    Q_UNUSED(sourceElement);
    Q_UNUSED(targetElement);
    // Do nothing in default instantiation, only tags and notebooks need to be processed specifically
}

template <>
void RemoteToLocalSynchronizationManager::checkAndAddLinkedNotebookBinding<Notebook>(const Notebook & sourceNotebook,
                                                                                     Notebook & targetNotebook)
{
    if (!sourceNotebook.hasGuid()) {
        return;
    }

    auto it = m_linkedNotebookGuidsByNotebookGuids.find(sourceNotebook.guid());
    if (it == m_linkedNotebookGuidsByNotebookGuids.end()) {
        return;
    }

    targetNotebook.setLinkedNotebookGuid(it.value());
}

template <>
void RemoteToLocalSynchronizationManager::checkAndAddLinkedNotebookBinding<Tag>(const Tag & sourceTag, Tag & targetTag)
{
    if (!sourceTag.hasGuid()) {
        return;
    }

    auto it = m_linkedNotebookGuidsByTagGuids.find(sourceTag.guid());
    if (it == m_linkedNotebookGuidsByTagGuids.end()) {
        return;
    }

    targetTag.setLinkedNotebookGuid(it.value());
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
        emit failure(error);
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

    NotebookSyncConflictResolver * pResolver = new NotebookSyncConflictResolver(remoteNotebook, localConflict,
                                                                                m_notebookSyncConflictResolutionCache,
                                                                                m_localStorageManagerAsync, this);
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
        emit failure(error);
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

    TagSyncConflictResolver * pResolver = new TagSyncConflictResolver(remoteTag, localConflict,
                                                                      m_tagSyncConflictResolutionCache,
                                                                      m_localStorageManagerAsync, this);
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
        emit failure(error);
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
                                                                                      m_savedSearchSyncConflictResolutionCache,
                                                                                      m_localStorageManagerAsync, this);
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

    Note conflictingNote = createConflictingNote(localConflict);

    Note updatedNote(remoteNote);
    updatedNote.unsetLocalUid();

    emitUpdateRequest(updatedNote, &conflictingNote);
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

    emitUpdateRequest(linkedNotebook);
}

} // namespace quentier
