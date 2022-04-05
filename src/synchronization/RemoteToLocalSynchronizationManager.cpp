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

#include "RemoteToLocalSynchronizationManager.h"

#include "InkNoteImageDownloader.h"
#include "NoteSyncConflictResolver.h"
#include "NoteThumbnailDownloader.h"
#include "qglobal.h"

#include <quentier/local_storage/LocalStorageManagerAsync.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/types/Resource.h>
#include <quentier/utility/ApplicationSettings.h>
#include <quentier/utility/Compat.h>
#include <quentier/utility/DateTime.h>
#include <quentier/utility/QuentierCheckPtr.h>
#include <quentier/utility/StandardPaths.h>
#include <quentier/utility/SysInfo.h>
#include <quentier/utility/TagSortByParentChildRelations.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QThreadPool>
#include <QTimerEvent>

#include <algorithm>
#include <set>

#define ACCOUNT_LIMITS_KEY_GROUP          QStringLiteral("AccountLimits/")
#define ACCOUNT_LIMITS_LAST_SYNC_TIME_KEY QStringLiteral("last_sync_time")
#define ACCOUNT_LIMITS_SERVICE_LEVEL_KEY  QStringLiteral("service_level")

#define ACCOUNT_LIMITS_USER_MAIL_LIMIT_DAILY_KEY                               \
    QStringLiteral("user_mail_limit_daily")

#define ACCOUNT_LIMITS_NOTE_SIZE_MAX_KEY     QStringLiteral("note_size_max")
#define ACCOUNT_LIMITS_RESOURCE_SIZE_MAX_KEY QStringLiteral("resource_size_max")

#define ACCOUNT_LIMITS_USER_LINKED_NOTEBOOK_MAX_KEY                            \
    QStringLiteral("user_linked_notebook_max")

#define ACCOUNT_LIMITS_UPLOAD_LIMIT_KEY QStringLiteral("upload_limit")

#define ACCOUNT_LIMITS_USER_NOTE_COUNT_MAX_KEY                                 \
    QStringLiteral("user_note_count_max")

#define ACCOUNT_LIMITS_USER_NOTEBOOK_COUNT_MAX_KEY                             \
    QStringLiteral("user_notebook_count_max")

#define ACCOUNT_LIMITS_USER_TAG_COUNT_MAX_KEY                                  \
    QStringLiteral("user_tag_count_max")

#define ACCOUNT_LIMITS_NOTE_TAG_COUNT_MAX_KEY                                  \
    QStringLiteral("note_tag_count_max")

#define ACCOUNT_LIMITS_USER_SAVED_SEARCH_COUNT_MAX_KEY                         \
    QStringLiteral("user_saved_search_count_max")

#define ACCOUNT_LIMITS_NOTE_RESOURCE_COUNT_MAX_KEY                             \
    QStringLiteral("note_resource_count_max")

#define SYNC_SETTINGS_KEY_GROUP QStringLiteral("SynchronizationSettings")

#define SHOULD_DOWNLOAD_NOTE_THUMBNAILS QStringLiteral("DownloadNoteThumbnails")
#define SHOULD_DOWNLOAD_INK_NOTE_IMAGES QStringLiteral("DownloadInkNoteImages")

#define INK_NOTE_IMAGES_STORAGE_PATH_KEY                                       \
    QStringLiteral("InkNoteImagesStoragePath")

#define THIRTY_DAYS_IN_MSEC (2592000000)

#define SET_ITEM_TYPE_TO_ERROR()                                               \
    errorDescription.appendBase(QT_TRANSLATE_NOOP(                             \
        "RemoteToLocalSynchronizationManager", "item type is"));               \
    errorDescription.details() += typeName

#define SET_CANT_FIND_BY_GUID_ERROR()                                          \
    ErrorString errorDescription(QT_TRANSLATE_NOOP(                            \
        "RemoteToLocalSynchronizationManager",                                 \
        "Internal error: can't find data item from sync chunks "               \
        "by guid: data item has no guid"));                                    \
    SET_ITEM_TYPE_TO_ERROR();                                                  \
    QNWARNING(                                                                 \
        "synchronization:remote_to_local",                                     \
        errorDescription << ": " << element)

#define SET_EMPTY_PENDING_LIST_ERROR()                                         \
    ErrorString errorDescription(QT_TRANSLATE_NOOP(                            \
        "RemoteToLocalSynchronizationManager",                                 \
        "Detected attempt to find a data item within the list of remote "      \
        "items "                                                               \
        "waiting for processing but that list is empty"));                     \
    QNWARNING(                                                                 \
        "synchronization:remote_to_local",                                     \
        errorDescription << ": " << element)

#define SET_CANT_FIND_IN_PENDING_LIST_ERROR()                                  \
    ErrorString errorDescription(QT_TRANSLATE_NOOP(                            \
        "RemoteToLocalSynchronizationManager",                                 \
        "Can't find the data item within the list of remote elements waiting " \
        "for processing"));                                                    \
    SET_ITEM_TYPE_TO_ERROR();                                                  \
    QNWARNING(                                                                 \
        "synchronization:remote_to_local",                                     \
        errorDescription << ": " << element)

namespace quentier {

namespace {

////////////////////////////////////////////////////////////////////////////////

QString dumpTagsContainer(const TagsContainer & tagsContainer)
{
    const auto & tagIndexByName = tagsContainer.get<ByName>();

    QString tagsDump;
    QTextStream strm(&tagsDump);
    strm << "Tags parsed from sync chunks:\n";

    for (const auto & tag: tagIndexByName) {
        strm << "    guid = "
             << (tag.guid.isSet() ? tag.guid.ref()
                                  : QStringLiteral("<not set>"))
             << ", name = "
             << (tag.name.isSet() ? tag.name.ref()
                                  : QStringLiteral("<not set>"))
             << "\n";
    }

    strm.flush();
    return tagsDump;
}

QString dumpLinkedNotebookGuidsByTagGuids(
    const QHash<QString, QString> & linkedNotebookGuidsByTagGuids)
{
    QString info;
    QTextStream strm(&info);

    strm << "Linked notebook guids by tag guids:\n";

    for (const auto it: qevercloud::toRange(linkedNotebookGuidsByTagGuids)) {
        strm << "    " << it.key() << " -> " << it.value() << "\n";
    }

    strm.flush();
    return info;
}

} // namespace

////////////////////////////////////////////////////////////////////////////////

class NoteSyncConflictResolverManager :
    public NoteSyncConflictResolver::IManager
{
public:
    NoteSyncConflictResolverManager(
        RemoteToLocalSynchronizationManager & manager);

    virtual LocalStorageManagerAsync & localStorageManagerAsync() override;

    virtual INoteStore * noteStoreForNote(
        const Note & note, QString & authToken,
        ErrorString & errorDescription) override;

    virtual bool syncingLinkedNotebooksContent() const override;

private:
    RemoteToLocalSynchronizationManager & m_manager;
};

RemoteToLocalSynchronizationManager::RemoteToLocalSynchronizationManager(
    IManager & manager, const QString & host, QObject * parent) :
    QObject(parent),
    m_manager(manager), m_host(host),
    m_tagSyncCache(m_manager.localStorageManagerAsync(), QLatin1String("")),
    m_savedSearchSyncCache(m_manager.localStorageManagerAsync()),
    m_notebookSyncCache(
        m_manager.localStorageManagerAsync(), QLatin1String("")),
    m_pNoteSyncConflictResolverManager(
        new NoteSyncConflictResolverManager(*this))
{}

RemoteToLocalSynchronizationManager::~RemoteToLocalSynchronizationManager() {}

bool RemoteToLocalSynchronizationManager::active() const
{
    return m_active;
}

void RemoteToLocalSynchronizationManager::setAccount(const Account & account)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::setAccount: " << account.name());

    if (m_user.hasId() && (m_user.id() != account.id())) {
        QNDEBUG(
            "synchronization:remote_to_local",
            "Switching to a different "
                << "user, clearing the current state");
        clearAll();
    }

    m_user.setId(account.id());
    m_user.setName(account.name());
    m_user.setUsername(account.name());

    auto accountEnType = account.evernoteAccountType();
    switch (accountEnType) {
    case Account::EvernoteAccountType::Plus:
        m_user.setServiceLevel(
            static_cast<qint8>(qevercloud::ServiceLevel::PLUS));
        break;
    case Account::EvernoteAccountType::Premium:
        m_user.setServiceLevel(
            static_cast<qint8>(qevercloud::ServiceLevel::PREMIUM));
        break;
    case Account::EvernoteAccountType::Business:
        m_user.setServiceLevel(
            static_cast<qint8>(qevercloud::ServiceLevel::BUSINESS));
        break;
    default:
        m_user.setServiceLevel(
            static_cast<qint8>(qevercloud::ServiceLevel::BASIC));
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
    QString name;
    if (m_user.hasName()) {
        name = m_user.name();
    }

    if (name.isEmpty() && m_user.hasUsername()) {
        name = m_user.username();
    }

    auto accountEnType = Account::EvernoteAccountType::Free;
    if (m_user.hasServiceLevel()) {
        switch (m_user.serviceLevel()) {
        case qevercloud::ServiceLevel::PLUS:
            accountEnType = Account::EvernoteAccountType::Plus;
            break;
        case qevercloud::ServiceLevel::PREMIUM:
            accountEnType = Account::EvernoteAccountType::Premium;
            break;
        case qevercloud::ServiceLevel::BUSINESS:
            accountEnType = Account::EvernoteAccountType::Business;
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

    Account account(
        name, Account::Type::Evernote, userId, accountEnType, m_host, shardId);

    account.setEvernoteAccountLimits(m_accountLimits);
    return account;
}

bool RemoteToLocalSynchronizationManager::syncUser(
    const qevercloud::UserID userId, ErrorString & errorDescription,
    const bool writeUserDataToLocalStorage)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::syncUser: user id = "
            << userId << ", write user data to local storage = "
            << (writeUserDataToLocalStorage ? "true" : "false"));

    m_user = User();
    m_user.setId(userId);

    // Checking the protocol version first
    if (!checkProtocolVersion(errorDescription)) {
        QNDEBUG(
            "synchronization:remote_to_local",
            "Protocol version check "
                << "failed: " << errorDescription);
        return false;
    }

    bool waitIfRateLimitReached = false;

    // Retrieving the latest user info then, to figure out the service level
    // and stuff like that
    if (!syncUserImpl(
            waitIfRateLimitReached, errorDescription,
            writeUserDataToLocalStorage))
    {
        QNDEBUG(
            "synchronization:remote_to_local",
            "Syncing the user has "
                << "failed: " << errorDescription);
        return false;
    }

    if (!checkAndSyncAccountLimits(waitIfRateLimitReached, errorDescription)) {
        QNDEBUG(
            "synchronization:remote_to_local",
            "Syncing the user's account "
                << "limits has failed: " << errorDescription);
        return false;
    }

    QNTRACE(
        "synchronization:remote_to_local",
        "Synchronized user data: " << m_user);
    return true;
}

const User & RemoteToLocalSynchronizationManager::user() const
{
    return m_user;
}

bool RemoteToLocalSynchronizationManager::shouldDownloadThumbnailsForNotes()
    const
{
    ApplicationSettings appSettings(
        account(), SYNCHRONIZATION_PERSISTENCE_NAME);

    appSettings.beginGroup(SYNC_SETTINGS_KEY_GROUP);
    bool res =
        (appSettings.contains(SHOULD_DOWNLOAD_NOTE_THUMBNAILS)
             ? appSettings.value(SHOULD_DOWNLOAD_NOTE_THUMBNAILS).toBool()
             : false);
    appSettings.endGroup();
    return res;
}

bool RemoteToLocalSynchronizationManager::shouldDownloadInkNoteImages() const
{
    ApplicationSettings appSettings(
        account(), SYNCHRONIZATION_PERSISTENCE_NAME);

    appSettings.beginGroup(SYNC_SETTINGS_KEY_GROUP);
    bool res =
        (appSettings.contains(SHOULD_DOWNLOAD_INK_NOTE_IMAGES)
             ? appSettings.value(SHOULD_DOWNLOAD_INK_NOTE_IMAGES).toBool()
             : false);
    appSettings.endGroup();
    return res;
}

QString RemoteToLocalSynchronizationManager::inkNoteImagesStoragePath() const
{
    ApplicationSettings appSettings(
        account(), SYNCHRONIZATION_PERSISTENCE_NAME);

    appSettings.beginGroup(SYNC_SETTINGS_KEY_GROUP);
    QString path =
        (appSettings.contains(INK_NOTE_IMAGES_STORAGE_PATH_KEY)
             ? appSettings.value(INK_NOTE_IMAGES_STORAGE_PATH_KEY).toString()
             : defaultInkNoteImageStoragePath());
    appSettings.endGroup();
    return path;
}

void RemoteToLocalSynchronizationManager::start(qint32 afterUsn)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::start: afterUsn = " << afterUsn);

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

    // Retrieving the latest user info then, to figure out the service level
    // and stuff like that
    if (!syncUserImpl(waitIfRateLimitReached, errorDescription)) {
        if (m_syncUserPostponeTimerId == 0) {
            // Not a "rate limit exceeded" error
            Q_EMIT failure(errorDescription);
        }

        return;
    }

    if (!checkAndSyncAccountLimits(waitIfRateLimitReached, errorDescription)) {
        if (m_syncAccountLimitsPostponeTimerId == 0) {
            // Not a "rate limit exceeded" error
            Q_EMIT failure(errorDescription);
        }

        return;
    }

    m_lastSyncMode =
        ((afterUsn == 0) ? SyncMode::FullSync : SyncMode::IncrementalSync);

    if (m_onceSyncDone || (afterUsn != 0)) {
        bool asyncWait = false;
        bool error = false;

        // check the sync state of user's own account, this may produce
        // the asynchronous chain of events or some error
        bool res = checkUserAccountSyncState(asyncWait, error, afterUsn);
        if (error || asyncWait) {
            return;
        }

        if (!res) {
            QNTRACE(
                "synchronization:remote_to_local",
                "The service has no "
                    << "updates for user's own account, need to check for "
                       "updates "
                    << "from linked notebooks");

            m_fullNoteContentsDownloaded = true;
            Q_EMIT synchronizedContentFromUsersOwnAccount(
                m_lastUpdateCount, m_lastSyncTime);

            m_expungedFromServerToClient = true;

            res = checkLinkedNotebooksSyncStates(asyncWait, error);
            if (asyncWait || error) {
                return;
            }

            if (!res) {
                QNTRACE(
                    "synchronization:remote_to_local",
                    "The service has no "
                        << "updates for any of linked notebooks");
                finalize();
            }

            startLinkedNotebooksSync();
            return;
        }
        /**
         * Otherwise the sync of all linked notebooks from user's account would
         * start after the sync of user's account
         * (Because the sync of user's account can bring in the new linked
         * notebooks or remove any of them)
         */
    }

    downloadSyncChunksAndLaunchSync(afterUsn);
}

void RemoteToLocalSynchronizationManager::stop()
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::stop");

    if (!m_active) {
        QNDEBUG("synchronization:remote_to_local", "Already stopped");
        return;
    }

    clear();
    resetCurrentSyncState();

    Q_EMIT stopped();
}

template <>
void RemoteToLocalSynchronizationManager::emitAddRequest<Tag>(const Tag & tag)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::emitAddRequest<Tag>: " << tag);

    registerTagPendingAddOrUpdate(tag);

    QUuid addTagRequestId = QUuid::createUuid();
    Q_UNUSED(m_addTagRequestIds.insert(addTagRequestId));
    QNTRACE(
        "synchronization:remote_to_local",
        "Emitting the request to add "
            << "tag to local storage: request id = " << addTagRequestId
            << ", tag: " << tag);

    Q_EMIT addTag(tag, addTagRequestId);
}

template <>
void RemoteToLocalSynchronizationManager::emitAddRequest<SavedSearch>(
    const SavedSearch & search)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::emitAddRequest<SavedSearch>: "
            << search);

    registerSavedSearchPendingAddOrUpdate(search);

    QUuid addSavedSearchRequestId = QUuid::createUuid();
    Q_UNUSED(m_addSavedSearchRequestIds.insert(addSavedSearchRequestId));
    QNTRACE(
        "synchronization:remote_to_local",
        "Emitting the request to add "
            << "saved search to local storage: request id = "
            << addSavedSearchRequestId << ", saved search: " << search);

    Q_EMIT addSavedSearch(search, addSavedSearchRequestId);
}

template <>
void RemoteToLocalSynchronizationManager::emitAddRequest<LinkedNotebook>(
    const LinkedNotebook & linkedNotebook)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::emitAddRequest<LinkedNotebook>: "
            << linkedNotebook);

    registerLinkedNotebookPendingAddOrUpdate(linkedNotebook);

    QUuid addLinkedNotebookRequestId = QUuid::createUuid();
    Q_UNUSED(m_addLinkedNotebookRequestIds.insert(addLinkedNotebookRequestId));
    QNTRACE(
        "synchronization:remote_to_local",
        "Emitting the request to add "
            << "linked notebook to local storage: request id = "
            << addLinkedNotebookRequestId
            << ", linked notebook: " << linkedNotebook);

    Q_EMIT addLinkedNotebook(linkedNotebook, addLinkedNotebookRequestId);
}

template <>
void RemoteToLocalSynchronizationManager::emitAddRequest<Notebook>(
    const Notebook & notebook)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::emitAddRequest<Notebook>: "
            << notebook);

    registerNotebookPendingAddOrUpdate(notebook);

    QUuid addNotebookRequestId = QUuid::createUuid();
    Q_UNUSED(m_addNotebookRequestIds.insert(addNotebookRequestId));
    QNTRACE(
        "synchronization:remote_to_local",
        "Emitting the request to add "
            << "notebook to local storage: request id = "
            << addNotebookRequestId << ", notebook: " << notebook);

    Q_EMIT addNotebook(notebook, addNotebookRequestId);
}

template <>
void RemoteToLocalSynchronizationManager::emitAddRequest<Note>(
    const Note & note)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::emitAddRequest<Note>: " << note);

    registerNotePendingAddOrUpdate(note);

    QUuid addNoteRequestId = QUuid::createUuid();
    Q_UNUSED(m_addNoteRequestIds.insert(addNoteRequestId));
    QNTRACE(
        "synchronization:remote_to_local",
        "Emitting the request to add "
            << "note to the local storage: request id = " << addNoteRequestId
            << ", note: " << note);

    Q_EMIT addNote(note, addNoteRequestId);
}

void RemoteToLocalSynchronizationManager::onFindUserCompleted(
    User user, QUuid requestId)
{
    if (requestId != m_findUserRequestId) {
        return;
    }

    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::onFindUserCompleted: user = "
            << user << "\nRequest id = " << requestId);

    m_user = user;
    m_findUserRequestId = QUuid();

    // Updating the user info as user was found in the local storage
    m_addOrUpdateUserRequestId = QUuid::createUuid();
    QNTRACE(
        "synchronization:remote_to_local",
        "Emitting the request to update "
            << "user in the local storage database: request id = "
            << m_addOrUpdateUserRequestId << ", user = " << m_user);

    Q_EMIT updateUser(m_user, m_addOrUpdateUserRequestId);
}

void RemoteToLocalSynchronizationManager::onFindUserFailed(
    User user, ErrorString errorDescription, QUuid requestId)
{
    if (requestId != m_findUserRequestId) {
        return;
    }

    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::onFindUserFailed: user = "
            << user << "\nError description = " << errorDescription
            << ", request id = " << requestId);

    m_findUserRequestId = QUuid();

    // Adding the user info as user was not found in the local storage
    m_addOrUpdateUserRequestId = QUuid::createUuid();
    QNTRACE(
        "synchronization:remote_to_local",
        "Emitting the request to add "
            << "user to the local storage database: request id = "
            << m_addOrUpdateUserRequestId << ", user = " << m_user);

    Q_EMIT addUser(m_user, m_addOrUpdateUserRequestId);
}

void RemoteToLocalSynchronizationManager::onFindNotebookCompleted(
    Notebook notebook, QUuid requestId)
{
    QNTRACE(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::onFindNotebookCompleted: "
            << "request id = " << requestId << ", notebook: " << notebook);

    bool foundByGuid = onFoundDuplicateByGuid(
        notebook, requestId, QStringLiteral("Notebook"), m_notebooks,
        m_notebooksPendingAddOrUpdate, m_findNotebookByGuidRequestIds);

    if (foundByGuid) {
        return;
    }

    bool foundByName = onFoundDuplicateByName(
        notebook, requestId, QStringLiteral("Notebook"), m_notebooks,
        m_notebooksPendingAddOrUpdate, m_findNotebookByNameRequestIds);

    if (foundByName) {
        return;
    }

    auto rit =
        m_notesWithFindRequestIdsPerFindNotebookRequestId.find(requestId);

    if (rit != m_notesWithFindRequestIdsPerFindNotebookRequestId.end()) {
        QNDEBUG(
            "synchronization:remote_to_local",
            "Found notebook needed for "
                << "note synchronization");

        const auto & noteWithFindRequestId = rit.value();
        const Note & note = noteWithFindRequestId.first;
        const QUuid & findNoteRequestId = noteWithFindRequestId.second;

        QString noteGuid = (note.hasGuid() ? note.guid() : QString());
        QString noteLocalUid = note.localUid();

        std::pair<QString, QString> key(noteGuid, noteLocalUid);

        // NOTE: notebook for notes is only required for its pair of guid +
        // local uid, it shouldn't prohibit the creation or update of notes
        // during the synchronization procedure
        notebook.setCanCreateNotes(true);
        notebook.setCanUpdateNotes(true);

        m_notebooksPerNoteIds[key] = notebook;

        Q_UNUSED(onFoundDuplicateByGuid(
            note, findNoteRequestId, QStringLiteral("Note"), m_notes,
            m_notesPendingAddOrUpdate, m_findNoteByGuidRequestIds));

        Q_UNUSED(m_notesWithFindRequestIdsPerFindNotebookRequestId.erase(rit))
        return;
    }

    auto iit = m_inkNoteResourceDataPerFindNotebookRequestId.find(requestId);
    if (iit != m_inkNoteResourceDataPerFindNotebookRequestId.end()) {
        QNDEBUG(
            "synchronization:remote_to_local",
            "Found notebook for ink "
                << "note image downloading for note resource");

        InkNoteResourceData resourceData = iit.value();
        Q_UNUSED(m_inkNoteResourceDataPerFindNotebookRequestId.erase(iit))

        auto git =
            m_resourceGuidsPendingFindNotebookForInkNoteImageDownloadPerNoteGuid
                .find(resourceData.m_noteGuid, resourceData.m_resourceGuid);

        if (git !=
            m_resourceGuidsPendingFindNotebookForInkNoteImageDownloadPerNoteGuid
                .end())
        {
            Q_UNUSED(
                m_resourceGuidsPendingFindNotebookForInkNoteImageDownloadPerNoteGuid
                    .erase(git))
        }

        setupInkNoteImageDownloading(
            resourceData.m_resourceGuid, resourceData.m_resourceHeight,
            resourceData.m_resourceWidth, resourceData.m_noteGuid, notebook);
        return;
    }

    auto iit_note =
        m_notesPendingInkNoteImagesDownloadByFindNotebookRequestId.find(
            requestId);
    if (iit_note !=
        m_notesPendingInkNoteImagesDownloadByFindNotebookRequestId.end())
    {
        QNDEBUG(
            "synchronization:remote_to_local",
            "Found notebook for ink "
                << "note images downloading for note");

        Note note = iit_note.value();
        Q_UNUSED(
            m_notesPendingInkNoteImagesDownloadByFindNotebookRequestId.erase(
                iit_note))

        bool res = setupInkNoteImageDownloadingForNote(note, notebook);
        if (Q_UNLIKELY(!res)) {
            QNDEBUG(
                "synchronization:remote_to_local",
                "Wasn't able to set up "
                    << "the ink note image downloading for note: " << note
                    << "\nNotebook: " << notebook);

            // NOTE: treat it as a recoverable failure, just ignore it and
            // consider the note properly downloaded
            checkAndIncrementNoteDownloadProgress(
                note.hasGuid() ? note.guid() : QString());

            checkServerDataMergeCompletion();
        }

        return;
    }

    auto thumbnailIt =
        m_notesPendingThumbnailDownloadByFindNotebookRequestId.find(requestId);

    if (thumbnailIt !=
        m_notesPendingThumbnailDownloadByFindNotebookRequestId.end()) {
        QNDEBUG(
            "synchronization:remote_to_local",
            "Found note for note "
                << "thumbnail downloading");

        Note note = thumbnailIt.value();
        Q_UNUSED(m_notesPendingThumbnailDownloadByFindNotebookRequestId.erase(
            thumbnailIt))

        bool res = setupNoteThumbnailDownloading(note, notebook);
        if (Q_UNLIKELY(!res)) {
            QNDEBUG(
                "synchronization:remote_to_local",
                "Wasn't able to set up "
                    << "the thumbnail downloading for note: " << note
                    << "\nNotebook: " << notebook);

            // NOTE: treat it as a recoverable failure, just ignore it and
            // consider the note properly downloaded
            checkAndIncrementNoteDownloadProgress(
                note.hasGuid() ? note.guid() : QString());

            checkServerDataMergeCompletion();
        }

        return;
    }
}

void RemoteToLocalSynchronizationManager::onFindNotebookFailed(
    Notebook notebook, ErrorString errorDescription, QUuid requestId)
{
    QNTRACE(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::onFindNotebookFailed: "
            << "request id = " << requestId << ", error description: "
            << errorDescription << ", notebook: " << notebook);

    bool failedToFindByGuid = onNoDuplicateByGuid(
        notebook, requestId, errorDescription, QStringLiteral("Notebook"),
        m_notebooks, m_findNotebookByGuidRequestIds);

    if (failedToFindByGuid) {
        return;
    }

    bool failedToFindByName = onNoDuplicateByName(
        notebook, requestId, errorDescription, QStringLiteral("Notebook"),
        m_notebooks, m_findNotebookByNameRequestIds);

    if (failedToFindByName) {
        return;
    }

    auto rit =
        m_notesWithFindRequestIdsPerFindNotebookRequestId.find(requestId);
    if (rit != m_notesWithFindRequestIdsPerFindNotebookRequestId.end()) {
        ErrorString errorDescription(
            QT_TR_NOOP("Failed to find the notebook for "
                       "one of synchronized notes"));
        QNWARNING(
            "synchronization:remote_to_local",
            errorDescription << ": " << notebook);
        Q_EMIT failure(errorDescription);
        return;
    }

    auto iit = m_inkNoteResourceDataPerFindNotebookRequestId.find(requestId);
    if (iit != m_inkNoteResourceDataPerFindNotebookRequestId.end()) {
        InkNoteResourceData resourceData = iit.value();
        Q_UNUSED(m_inkNoteResourceDataPerFindNotebookRequestId.erase(iit))

        auto git =
            m_resourceGuidsPendingFindNotebookForInkNoteImageDownloadPerNoteGuid
                .find(resourceData.m_noteGuid, resourceData.m_resourceGuid);

        if (git !=
            m_resourceGuidsPendingFindNotebookForInkNoteImageDownloadPerNoteGuid
                .end())
        {
            Q_UNUSED(
                m_resourceGuidsPendingFindNotebookForInkNoteImageDownloadPerNoteGuid
                    .erase(git))
        }

        QNWARNING(
            "synchronization:remote_to_local",
            "Can't find the notebook "
                << "for the purpose of setting up the ink note image "
                   "downloading");

        checkAndIncrementResourceDownloadProgress(resourceData.m_resourceGuid);

        /**
         * NOTE: handle the failure to download the ink note image as a
         * recoverable error i.e. consider the resource successfully downloaded
         * anyway - hence, need to check if that was the last resource pending
         * its downloading events sequence
         */
        checkServerDataMergeCompletion();

        return;
    }

    auto iit_note =
        m_notesPendingInkNoteImagesDownloadByFindNotebookRequestId.find(
            requestId);

    if (iit_note !=
        m_notesPendingInkNoteImagesDownloadByFindNotebookRequestId.end())
    {
        Note note = iit_note.value();
        Q_UNUSED(
            m_notesPendingInkNoteImagesDownloadByFindNotebookRequestId.erase(
                iit_note))

        if (note.hasGuid()) {
            // We might already have this note's resources mapped by note's guid
            // as "pending ink note image download", need to remove this mapping
            Q_UNUSED(
                m_resourceGuidsPendingInkNoteImageDownloadPerNoteGuid.remove(
                    note.guid()))
        }

        QNWARNING(
            "synchronization:remote_to_local",
            "Can't find the notebook "
                << "for the purpose of setting up the ink note image "
                   "downloading");

        /**
         * NOTE: incrementing note download progress here because we haven't
         * incremented it on the receipt of full note data before setting up
         * the ink note image downloading
         */
        checkAndIncrementNoteDownloadProgress(
            note.hasGuid() ? note.guid() : QString());

        /**
         * NOTE: handle the failure to download the ink note image as
         * a recoverable error i.e. consider the note successfully downloaded
         * anyway - hence, need to check if that was the last note pending its
         * downloading events sequence
         */
        checkServerDataMergeCompletion();

        return;
    }

    auto thumbnailIt =
        m_notesPendingThumbnailDownloadByFindNotebookRequestId.find(requestId);

    if (thumbnailIt !=
        m_notesPendingThumbnailDownloadByFindNotebookRequestId.end()) {
        Note note = thumbnailIt.value();
        Q_UNUSED(m_notesPendingThumbnailDownloadByFindNotebookRequestId.erase(
            thumbnailIt))

        if (note.hasGuid()) {
            // We might already have this note within those "pending
            // the thumbnail download", need to remove it from there
            auto dit = m_notesPendingThumbnailDownloadByGuid.find(note.guid());
            if (dit != m_notesPendingThumbnailDownloadByGuid.end()) {
                Q_UNUSED(m_notesPendingThumbnailDownloadByGuid.erase(dit))
            }
        }

        QNWARNING(
            "synchronization:remote_to_local",
            "Can't find the notebook "
                << "for the purpose of setting up the note thumbnail "
                   "downloading");

        /**
         * NOTE: incrementing note download progress here because we haven't
         * incremented it on the receipt of full note data before setting up
         * the thumbnails downloading
         */
        checkAndIncrementNoteDownloadProgress(
            note.hasGuid() ? note.guid() : QString());

        /**
         * NOTE: handle the failure to download the note thumbnail as
         * a recoverable error i.e. consider the note successfully downloaded
         * anyway - hence, need to check if that was the last note pending its
         * downloading events sequence
         */
        checkServerDataMergeCompletion();

        return;
    }
}

void RemoteToLocalSynchronizationManager::onFindNoteCompleted(
    Note note, LocalStorageManager::GetNoteOptions options, QUuid requestId)
{
    Q_UNUSED(options)

    auto it = m_findNoteByGuidRequestIds.find(requestId);
    if (it != m_findNoteByGuidRequestIds.end()) {
        QNDEBUG(
            "synchronization:remote_to_local",
            "RemoteToLocalSynchronizationManager::onFindNoteCompleted: "
                << "requestId = " << requestId);

        QNTRACE("synchronization:remote_to_local", "Note = " << note);

        // NOTE: erase is required for proper work of the macro; the request
        // would be re-inserted below if macro doesn't return from the method
        Q_UNUSED(m_findNoteByGuidRequestIds.erase(it));

        // Need to find Notebook corresponding to the note in order to proceed
        if (Q_UNLIKELY(!note.hasNotebookGuid())) {
            ErrorString errorDescription(
                QT_TR_NOOP("Found duplicate note in the local storage which "
                           "doesn't have a notebook guid"));
            APPEND_NOTE_DETAILS(errorDescription, note)

            QNWARNING(
                "synchronization:remote_to_local",
                errorDescription << ": " << note);
            Q_EMIT failure(errorDescription);
            return;
        }

        QUuid findNotebookPerNoteRequestId = QUuid::createUuid();

        m_notesWithFindRequestIdsPerFindNotebookRequestId
            [findNotebookPerNoteRequestId] = std::make_pair(note, requestId);

        Notebook notebookToFind;
        notebookToFind.unsetLocalUid();
        notebookToFind.setGuid(note.notebookGuid());

        Q_UNUSED(m_findNoteByGuidRequestIds.insert(requestId));

        Q_EMIT findNotebook(notebookToFind, findNotebookPerNoteRequestId);
        return;
    }

    auto rit = m_resourcesByFindNoteRequestIds.find(requestId);
    if (rit != m_resourcesByFindNoteRequestIds.end()) {
        Resource resource = rit.value();
        Q_UNUSED(m_resourcesByFindNoteRequestIds.erase(rit))

        if (Q_UNLIKELY(!note.hasGuid())) {
            ErrorString errorDescription(
                QT_TR_NOOP("Found the note necessary for the resource "
                           "synchronization but it doesn't have a guid"));
            APPEND_NOTE_DETAILS(errorDescription, note)
            QNWARNING(
                "synchronization:remote_to_local",
                errorDescription << ": " << note);
            Q_EMIT failure(errorDescription);
            return;
        }

        const Notebook * pNotebook = getNotebookPerNote(note);

        if (shouldDownloadThumbnailsForNotes()) {
            auto noteThumbnailDownloadIt =
                m_notesPendingThumbnailDownloadByGuid.find(note.guid());

            if (noteThumbnailDownloadIt ==
                m_notesPendingThumbnailDownloadByGuid.end()) {
                QNDEBUG(
                    "synchronization:remote_to_local",
                    "Need to download "
                        << "the thumbnail for the note with added or updated "
                        << "resource");

                // NOTE: don't care whether was capable to start downloading
                // the note thumnail, if not, this error is simply ignored
                if (pNotebook) {
                    Q_UNUSED(setupNoteThumbnailDownloading(note, *pNotebook))
                }
                else {
                    Q_UNUSED(findNotebookForNoteThumbnailDownloading(note))
                }
            }
        }

        if (resource.hasMime() && resource.hasWidth() && resource.hasHeight() &&
            (resource.mime() == QStringLiteral("application/vnd.evernote.ink")))
        {
            QNDEBUG(
                "synchronization:remote_to_local",
                "The resource appears "
                    << "to be the one for the ink note, need to download the "
                       "image "
                    << "for it; but first need to understand whether the note "
                    << "owning the resource is from the current "
                    << "user's account or from some linked notebook");

            if (pNotebook) {
                setupInkNoteImageDownloading(
                    resource.guid(), resource.height(), resource.width(),
                    note.guid(), *pNotebook);
            }
            else if (note.hasNotebookLocalUid() || note.hasNotebookGuid()) {
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

                Q_UNUSED(
                    m_resourceGuidsPendingFindNotebookForInkNoteImageDownloadPerNoteGuid
                        .insert(note.guid(), resource.guid()))

                QUuid findNotebookForInkNoteSetupRequestId =
                    QUuid::createUuid();

                m_inkNoteResourceDataPerFindNotebookRequestId
                    [findNotebookForInkNoteSetupRequestId] = resourceData;

                QNTRACE(
                    "synchronization:remote_to_local",
                    "Emitting "
                        << "the request to find a notebook for the ink note "
                           "image "
                        << "download resolution: "
                        << findNotebookForInkNoteSetupRequestId
                        << ", resource guid = " << resourceData.m_resourceGuid
                        << ", resource height = "
                        << resourceData.m_resourceHeight
                        << ", resource width = " << resourceData.m_resourceWidth
                        << ", note guid = " << note.guid()
                        << ", notebook: " << dummyNotebook);

                Q_EMIT findNotebook(
                    dummyNotebook, findNotebookForInkNoteSetupRequestId);
            }
            else {
                QNWARNING(
                    "synchronization:remote_to_local",
                    "Can't download "
                        << "the ink note image: note has neither notebook "
                           "local "
                        << "uid nor notebook guid: " << note);
            }
        }

        auto resourceFoundIt =
            m_guidsOfResourcesFoundWithinTheLocalStorage.find(resource.guid());
        if (resourceFoundIt ==
            m_guidsOfResourcesFoundWithinTheLocalStorage.end()) {
            QNDEBUG(
                "synchronization:remote_to_local",
                "Duplicate of "
                    << "synchronized resource was not found in the local "
                       "storage "
                    << "database! Attempting to add it to the local storage");

            registerResourcePendingAddOrUpdate(resource);
            getFullResourceDataAsyncAndAddToLocalStorage(resource, note);
            return;
        }

        if (!resource.isDirty()) {
            QNDEBUG(
                "synchronization:remote_to_local",
                "Found duplicate "
                    << "resource in local storage which is not marked dirty => "
                    << "overriding it with the version received from Evernote");

            registerResourcePendingAddOrUpdate(resource);
            getFullResourceDataAsyncAndUpdateInLocalStorage(resource, note);
            return;
        }

        QNDEBUG(
            "synchronization:remote_to_local",
            "Found duplicate resource "
                << "in the local storage which is marked dirty => will treat "
                   "it as "
                << "a conflict of notes");

        Note conflictingNote = createConflictingNote(note);

        Note updatedNote(note);
        updatedNote.setDirty(false);
        updatedNote.setLocal(false);

        processResourceConflictAsNoteConflict(
            updatedNote, conflictingNote, resource);
    }
}

void RemoteToLocalSynchronizationManager::onFindNoteFailed(
    Note note, LocalStorageManager::GetNoteOptions options,
    ErrorString errorDescription, QUuid requestId)
{
    Q_UNUSED(options)
    Q_UNUSED(errorDescription)

    auto it = m_findNoteByGuidRequestIds.find(requestId);
    if (it != m_findNoteByGuidRequestIds.end()) {
        QNDEBUG(
            "synchronization:remote_to_local",
            "RemoteToLocalSynchronizationManager::onFindNoteFailed: note = "
                << note << ", requestId = " << requestId);

        Q_UNUSED(m_findNoteByGuidRequestIds.erase(it));

        auto it = findItemByGuid(m_notes, note, QStringLiteral("Note"));
        if (it == m_notes.end()) {
            return;
        }

        note = *it;

        // Removing the note from the list of notes waiting for processing
        // but also remembering it for further reference
        Q_UNUSED(m_notes.erase(it));

        Q_UNUSED(m_guidsOfProcessedNonExpungedNotes.insert(note.guid()))

        getFullNoteDataAsyncAndAddToLocalStorage(note);
        return;
    }

    auto rit = m_resourcesByFindNoteRequestIds.find(requestId);
    if (rit != m_resourcesByFindNoteRequestIds.end()) {
        QNDEBUG(
            "synchronization:remote_to_local",
            "RemoteToLocalSynchronizationManager::onFindNoteFailed: note = "
                << note << ", requestId = " << requestId);

        Q_UNUSED(m_resourcesByFindNoteRequestIds.erase(rit));

        ErrorString errorDescription(
            QT_TR_NOOP("Can't find note containing the synchronized resource "
                       "in the local storage"));
        APPEND_NOTE_DETAILS(errorDescription, note)

        QNWARNING(
            "synchronization:remote_to_local",
            errorDescription << ", note attempted to be found: " << note);
        Q_EMIT failure(errorDescription);
        return;
    }
}

void RemoteToLocalSynchronizationManager::onFindTagCompleted(
    Tag tag, QUuid requestId)
{
    bool foundByGuid = onFoundDuplicateByGuid(
        tag, requestId, QStringLiteral("Tag"), m_tags, m_tagsPendingAddOrUpdate,
        m_findTagByGuidRequestIds);

    if (foundByGuid) {
        return;
    }

    Q_UNUSED(onFoundDuplicateByName(
        tag, requestId, QStringLiteral("Tag"), m_tags, m_tagsPendingAddOrUpdate,
        m_findTagByNameRequestIds))
}

void RemoteToLocalSynchronizationManager::onFindTagFailed(
    Tag tag, ErrorString errorDescription, QUuid requestId)
{
    bool failedToFindByGuid = onNoDuplicateByGuid(
        tag, requestId, errorDescription, QStringLiteral("Tag"), m_tags,
        m_findTagByGuidRequestIds);

    if (failedToFindByGuid) {
        return;
    }

    Q_UNUSED(onNoDuplicateByName(
        tag, requestId, errorDescription, QStringLiteral("Tag"), m_tags,
        m_findTagByNameRequestIds))
}

void RemoteToLocalSynchronizationManager::onFindResourceCompleted(
    Resource resource, LocalStorageManager::GetResourceOptions options,
    QUuid requestId)
{
    Q_UNUSED(options)

    auto rit = m_findResourceByGuidRequestIds.find(requestId);
    if (rit != m_findResourceByGuidRequestIds.end()) {
        QNDEBUG(
            "synchronization:remote_to_local",
            "RemoteToLocalSynchronizationManager::onFindResourceCompleted: "
                << "resource = " << resource << ", requestId = " << requestId);

        Q_UNUSED(m_findResourceByGuidRequestIds.erase(rit))

        auto it =
            findItemByGuid(m_resources, resource, QStringLiteral("Resource"));
        if (it == m_resources.end()) {
            return;
        }

        // Override blank resource object (it contains only guid) with
        // the actual updated resource from the container
        resource.qevercloudResource() = *it;

        // Removing the resource from the list of resources waiting for
        // processing
        Q_UNUSED(m_resources.erase(it))

        // need to find the note owning the resource to proceed
        if (!resource.hasNoteGuid()) {
            ErrorString errorDescription(
                QT_TR_NOOP("Found duplicate resource in the local storage "
                           "which doesn't have a note guid"));
            QNWARNING(
                "synchronization:remote_to_local",
                errorDescription << ": " << resource);
            Q_EMIT failure(errorDescription);
            return;
        }

        Q_UNUSED(m_guidsOfResourcesFoundWithinTheLocalStorage.insert(
            resource.guid()))

        QUuid findNotePerResourceRequestId = QUuid::createUuid();
        m_resourcesByFindNoteRequestIds[findNotePerResourceRequestId] =
            resource;

        Note noteToFind;
        noteToFind.unsetLocalUid();
        noteToFind.setGuid(resource.noteGuid());

        QNTRACE(
            "synchronization:remote_to_local",
            "Emitting the request to "
                << "find resource's note by guid: request id = "
                << findNotePerResourceRequestId << ", note: " << noteToFind);

        LocalStorageManager::GetNoteOptions options(
            LocalStorageManager::GetNoteOption::WithResourceMetadata |
            LocalStorageManager::GetNoteOption::WithResourceBinaryData);
        Q_EMIT findNote(noteToFind, options, findNotePerResourceRequestId);
        return;
    }
}

void RemoteToLocalSynchronizationManager::onFindResourceFailed(
    Resource resource, LocalStorageManager::GetResourceOptions options,
    ErrorString errorDescription, QUuid requestId)
{
    Q_UNUSED(options)

    auto rit = m_findResourceByGuidRequestIds.find(requestId);
    if (rit != m_findResourceByGuidRequestIds.end()) {
        QNDEBUG(
            "synchronization:remote_to_local",
            "RemoteToLocalSynchronizationManager::onFindResourceFailed: "
                << "resource = " << resource << ", requestId = " << requestId);

        Q_UNUSED(m_findResourceByGuidRequestIds.erase(rit))

        auto it =
            findItemByGuid(m_resources, resource, QStringLiteral("Resource"));

        if (it == m_resources.end()) {
            return;
        }

        // Override blank resource object (it contains only guid) with
        // the actual updated resource from the container
        resource.qevercloudResource() = *it;

        // Removing the resource from the list of resources waiting for
        // processing
        Q_UNUSED(m_resources.erase(it))

        // need to find the note owning the resource to proceed
        if (!resource.hasNoteGuid()) {
            errorDescription.setBase(
                QT_TR_NOOP("Detected resource which doesn't "
                           "have note guid set"));
            QNWARNING(
                "synchronization:remote_to_local",
                errorDescription << ": " << resource);
            Q_EMIT failure(errorDescription);
            return;
        }

        QUuid findNotePerResourceRequestId = QUuid::createUuid();
        m_resourcesByFindNoteRequestIds[findNotePerResourceRequestId] =
            resource;

        Note noteToFind;
        noteToFind.unsetLocalUid();
        noteToFind.setGuid(resource.noteGuid());

        QNTRACE(
            "synchronization:remote_to_local",
            "Emitting the request to "
                << "find the resource's note by guid: request id = "
                << findNotePerResourceRequestId << ", note: " << noteToFind);

        LocalStorageManager::GetNoteOptions options =
            LocalStorageManager::GetNoteOption::WithResourceMetadata;
        Q_EMIT findNote(noteToFind, options, findNotePerResourceRequestId);
        return;
    }
}

void RemoteToLocalSynchronizationManager::onFindLinkedNotebookCompleted(
    LinkedNotebook linkedNotebook, QUuid requestId)
{
    Q_UNUSED(onFoundDuplicateByGuid(
        linkedNotebook, requestId, QStringLiteral("LinkedNotebook"),
        m_linkedNotebooks, m_linkedNotebooksPendingAddOrUpdate,
        m_findLinkedNotebookRequestIds))
}

void RemoteToLocalSynchronizationManager::onFindLinkedNotebookFailed(
    LinkedNotebook linkedNotebook, ErrorString errorDescription,
    QUuid requestId)
{
    auto rit = m_findLinkedNotebookRequestIds.find(requestId);
    if (rit == m_findLinkedNotebookRequestIds.end()) {
        return;
    }

    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::onFindLinkedNotebookFailed: "
            << linkedNotebook << ", errorDescription = " << errorDescription
            << ", requestId = " << requestId);

    Q_UNUSED(m_findLinkedNotebookRequestIds.erase(rit));

    auto it = findItemByGuid(
        m_linkedNotebooks, linkedNotebook, QStringLiteral("LinkedNotebook"));
    if (it == m_linkedNotebooks.end()) {
        return;
    }

    Q_UNUSED(m_linkedNotebooks.erase(it))

    // This linked notebook was not found in the local storage by guid, adding
    // it there
    emitAddRequest(linkedNotebook);
}

void RemoteToLocalSynchronizationManager::onFindSavedSearchCompleted(
    SavedSearch savedSearch, QUuid requestId)
{
    bool foundByGuid = onFoundDuplicateByGuid(
        savedSearch, requestId, QStringLiteral("SavedSearch"), m_savedSearches,
        m_savedSearchesPendingAddOrUpdate, m_findSavedSearchByGuidRequestIds);

    if (foundByGuid) {
        return;
    }

    Q_UNUSED(onFoundDuplicateByName(
        savedSearch, requestId, QStringLiteral("SavedSearch"), m_savedSearches,
        m_savedSearchesPendingAddOrUpdate, m_findSavedSearchByNameRequestIds))
}

void RemoteToLocalSynchronizationManager::onFindSavedSearchFailed(
    SavedSearch savedSearch, ErrorString errorDescription, QUuid requestId)
{
    bool failedToFindByGuid = onNoDuplicateByGuid(
        savedSearch, requestId, errorDescription, QStringLiteral("SavedSearch"),
        m_savedSearches, m_findSavedSearchByGuidRequestIds);

    if (failedToFindByGuid) {
        return;
    }

    Q_UNUSED(onNoDuplicateByName(
        savedSearch, requestId, errorDescription, QStringLiteral("SavedSearch"),
        m_savedSearches, m_findSavedSearchByNameRequestIds))
}

template <class ElementType>
void RemoteToLocalSynchronizationManager::onAddDataElementCompleted(
    const ElementType & element, const QUuid & requestId,
    const QString & typeName, QSet<QUuid> & addElementRequestIds)
{
    auto it = addElementRequestIds.find(requestId);
    if (it != addElementRequestIds.end()) {
        QNDEBUG(
            "synchronization:remote_to_local",
            "RemoteToLocalSynchronizationManager::"
                << "onAddDataElementCompleted<" << typeName << ">: " << typeName
                << " = " << element << ", requestId = " << requestId);

        Q_UNUSED(addElementRequestIds.erase(it));
        performPostAddOrUpdateChecks<ElementType>(element);
        checkServerDataMergeCompletion();
    }
}

void RemoteToLocalSynchronizationManager::onAddUserCompleted(
    User user, QUuid requestId)
{
    if (requestId != m_addOrUpdateUserRequestId) {
        return;
    }

    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::onAddUserCompleted: user = "
            << user << "\nRequest id = " << requestId);

    m_addOrUpdateUserRequestId = QUuid();
    m_onceAddedOrUpdatedUserInLocalStorage = true;
}

void RemoteToLocalSynchronizationManager::onAddUserFailed(
    User user, ErrorString errorDescription, QUuid requestId)
{
    if (requestId != m_addOrUpdateUserRequestId) {
        return;
    }

    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::onAddUserFailed: "
            << user << "\nRequest id = " << requestId);

    ErrorString error(
        QT_TR_NOOP("Failed to add the user data fetched from "
                   "the remote database to the local storage"));
    error.additionalBases().append(errorDescription.base());
    error.additionalBases().append(errorDescription.additionalBases());
    error.details() = errorDescription.details();
    Q_EMIT failure(error);

    m_addOrUpdateUserRequestId = QUuid();
}

void RemoteToLocalSynchronizationManager::onAddTagCompleted(
    Tag tag, QUuid requestId)
{
    onAddDataElementCompleted(
        tag, requestId, QStringLiteral("Tag"), m_addTagRequestIds);
}

void RemoteToLocalSynchronizationManager::onAddSavedSearchCompleted(
    SavedSearch search, QUuid requestId)
{
    onAddDataElementCompleted(
        search, requestId, QStringLiteral("SavedSearch"),
        m_addSavedSearchRequestIds);
}

template <class ElementType>
void RemoteToLocalSynchronizationManager::onAddDataElementFailed(
    const ElementType & element, const QUuid & requestId,
    const ErrorString & errorDescription, const QString & typeName,
    QSet<QUuid> & addElementRequestIds)
{
    auto it = addElementRequestIds.find(requestId);
    if (it != addElementRequestIds.end()) {
        QNWARNING(
            "synchronization:remote_to_local",
            "RemoteToLocalSynchronizationManager::onAddDataElementFailed<"
                << typeName << ">: " << typeName << " = " << element
                << "\nError description = " << errorDescription
                << ", requestId = " << requestId);

        Q_UNUSED(addElementRequestIds.erase(it));

        ErrorString error(QT_TRANSLATE_NOOP(
            "RemoteToLocalSynchronizationManager",
            "Failed to add the data item fetched "
            "from the remote database to the local storage"));
        error.additionalBases().append(errorDescription.base());
        error.additionalBases().append(errorDescription.additionalBases());
        error.details() = errorDescription.details();
        QNWARNING("synchronization:remote_to_local", error);
        Q_EMIT failure(error);
    }
}

void RemoteToLocalSynchronizationManager::onAddTagFailed(
    Tag tag, ErrorString errorDescription, QUuid requestId)
{
    onAddDataElementFailed(
        tag, requestId, errorDescription, QStringLiteral("Tag"),
        m_addTagRequestIds);
}

void RemoteToLocalSynchronizationManager::onAddSavedSearchFailed(
    SavedSearch search, ErrorString errorDescription, QUuid requestId)
{
    onAddDataElementFailed(
        search, requestId, errorDescription, QStringLiteral("SavedSearch"),
        m_addSavedSearchRequestIds);
}

void RemoteToLocalSynchronizationManager::onUpdateUserCompleted(
    User user, QUuid requestId)
{
    if (requestId != m_addOrUpdateUserRequestId) {
        if (user.hasId() && m_user.hasId() && (user.id() == m_user.id())) {
            QNDEBUG(
                "synchronization:remote_to_local",
                "RemoteToLocalSynchronizationManager::onUpdateUserCompleted: "
                    << "external update of current user, request id = "
                    << requestId);
            m_user = user;
        }

        return;
    }

    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::onUpdateUserCompleted: "
            << "user = " << user << "\nRequest id = " << requestId);

    m_addOrUpdateUserRequestId = QUuid();
    m_onceAddedOrUpdatedUserInLocalStorage = true;
}

void RemoteToLocalSynchronizationManager::onUpdateUserFailed(
    User user, ErrorString errorDescription, QUuid requestId)
{
    if (requestId != m_addOrUpdateUserRequestId) {
        return;
    }

    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::onUpdateUserFailed: "
            << "user = " << user << "\nError description = " << errorDescription
            << ", request id = " << requestId);

    ErrorString error(
        QT_TR_NOOP("Can't update the user data fetched from "
                   "the remote database in the local storage"));
    error.additionalBases().append(errorDescription.base());
    error.additionalBases().append(errorDescription.additionalBases());
    error.details() = errorDescription.details();
    Q_EMIT failure(error);

    m_addOrUpdateUserRequestId = QUuid();
}

template <class ElementType>
void RemoteToLocalSynchronizationManager::onUpdateDataElementCompleted(
    const ElementType & element, const QUuid & requestId,
    const QString & typeName, QSet<QUuid> & updateElementRequestIds)
{
    auto rit = updateElementRequestIds.find(requestId);
    if (rit == updateElementRequestIds.end()) {
        return;
    }

    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizartionManager::onUpdateDataElementCompleted<"
            << typeName << ">: " << typeName << " = " << element
            << ", requestId = " << requestId);

    Q_UNUSED(updateElementRequestIds.erase(rit));
    performPostAddOrUpdateChecks<ElementType>(element);
    checkServerDataMergeCompletion();
}

void RemoteToLocalSynchronizationManager::onUpdateTagCompleted(
    Tag tag, QUuid requestId)
{
    onUpdateDataElementCompleted(
        tag, requestId, QStringLiteral("Tag"), m_updateTagRequestIds);
}

void RemoteToLocalSynchronizationManager::onUpdateSavedSearchCompleted(
    SavedSearch search, QUuid requestId)
{
    onUpdateDataElementCompleted(
        search, requestId, QStringLiteral("SavedSearch"),
        m_updateSavedSearchRequestIds);
}

template <class ElementType>
void RemoteToLocalSynchronizationManager::onUpdateDataElementFailed(
    const ElementType & element, const QUuid & requestId,
    const ErrorString & errorDescription, const QString & typeName,
    QSet<QUuid> & updateElementRequestIds)
{
    auto it = updateElementRequestIds.find(requestId);
    if (it == updateElementRequestIds.end()) {
        return;
    }

    QNWARNING(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::onUpdateDataElementFailed<"
            << typeName << ">: " << typeName << " = " << element
            << ", errorDescription = " << errorDescription
            << ", requestId = " << requestId);

    Q_UNUSED(updateElementRequestIds.erase(it));

    ErrorString error(QT_TRANSLATE_NOOP(
        "RemoteToLocalSynchronizationManager",
        "Can't update the item in the local storage"));
    error.additionalBases().append(errorDescription.base());
    error.additionalBases().append(errorDescription.additionalBases());
    error.details() = errorDescription.details();
    Q_EMIT failure(error);
}

void RemoteToLocalSynchronizationManager::onUpdateTagFailed(
    Tag tag, ErrorString errorDescription, QUuid requestId)
{
    onUpdateDataElementFailed(
        tag, requestId, errorDescription, QStringLiteral("Tag"),
        m_updateTagRequestIds);
}

void RemoteToLocalSynchronizationManager::onExpungeTagCompleted(
    Tag tag, QStringList expungedChildTagLocalUids, QUuid requestId)
{
    Q_UNUSED(expungedChildTagLocalUids)

    onExpungeDataElementCompleted(
        tag, requestId, QStringLiteral("Tag"), m_expungeTagRequestIds);
}

void RemoteToLocalSynchronizationManager::onExpungeTagFailed(
    Tag tag, ErrorString errorDescription, QUuid requestId)
{
    onExpungeDataElementFailed(
        tag, requestId, errorDescription, QStringLiteral("Tag"),
        m_expungeTagRequestIds);
}

void RemoteToLocalSynchronizationManager::
    onExpungeNotelessTagsFromLinkedNotebooksCompleted(QUuid requestId)
{
    if (requestId == m_expungeNotelessTagsRequestId) {
        QNDEBUG(
            "synchronization:remote_to_local",
            "RemoteToLocalSynchronizationManager::"
                << "onExpungeNotelessTagsFromLinkedNotebooksCompleted");

        m_expungeNotelessTagsRequestId = QUuid();
        finalize();
        return;
    }
}

void RemoteToLocalSynchronizationManager::
    onExpungeNotelessTagsFromLinkedNotebooksFailed(
        ErrorString errorDescription, QUuid requestId)
{
    if (requestId != m_expungeNotelessTagsRequestId) {
        return;
    }

    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::"
            << "onExpungeNotelessTagsFromLinkedNotebooksFailed: "
            << errorDescription);

    m_expungeNotelessTagsRequestId = QUuid();

    ErrorString error(
        QT_TR_NOOP("Failed to expunge the noteless tags belonging "
                   "to linked notebooks from the local storage"));
    error.additionalBases().append(errorDescription.base());
    error.additionalBases().append(errorDescription.additionalBases());
    error.details() = errorDescription.details();
    Q_EMIT failure(error);
}

void RemoteToLocalSynchronizationManager::onUpdateSavedSearchFailed(
    SavedSearch search, ErrorString errorDescription, QUuid requestId)
{
    onUpdateDataElementFailed(
        search, requestId, errorDescription, QStringLiteral("SavedSearch"),
        m_updateSavedSearchRequestIds);
}

void RemoteToLocalSynchronizationManager::onExpungeSavedSearchCompleted(
    SavedSearch search, QUuid requestId)
{
    onExpungeDataElementCompleted(
        search, requestId, QStringLiteral("SavedSearch"),
        m_expungeSavedSearchRequestIds);
}

void RemoteToLocalSynchronizationManager::onExpungeSavedSearchFailed(
    SavedSearch search, ErrorString errorDescription, QUuid requestId)
{
    onExpungeDataElementFailed(
        search, requestId, errorDescription, QStringLiteral("SavedSearch"),
        m_expungeSavedSearchRequestIds);
}

template <>
void RemoteToLocalSynchronizationManager::performPostAddOrUpdateChecks<Tag>(
    const Tag & tag)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::performPostAddOrUpdateChecks"
            << "<Tag>: " << tag);

    unregisterTagPendingAddOrUpdate(tag);
    syncNextTagPendingProcessing();
    checkNotebooksAndTagsSyncCompletionAndLaunchNotesAndResourcesSync();
    checkServerDataMergeCompletion();
}

template <>
void RemoteToLocalSynchronizationManager::performPostAddOrUpdateChecks<
    Notebook>(const Notebook & notebook)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::performPostAddOrUpdateChecks"
            << "<Notebook>: " << notebook);

    unregisterNotebookPendingAddOrUpdate(notebook);
    checkNotebooksAndTagsSyncCompletionAndLaunchNotesAndResourcesSync();
    checkServerDataMergeCompletion();
}

template <>
void RemoteToLocalSynchronizationManager::performPostAddOrUpdateChecks<Note>(
    const Note & note)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::performPostAddOrUpdateChecks"
            << "<Note>: " << note);

    unregisterNotePendingAddOrUpdate(note);
    checkNotesSyncCompletionAndLaunchResourcesSync();
    checkServerDataMergeCompletion();
}

template <>
void RemoteToLocalSynchronizationManager::performPostAddOrUpdateChecks<
    Resource>(const Resource & resource)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::performPostAddOrUpdateChecks"
            << "<Resource>: " << resource);

    unregisterResourcePendingAddOrUpdate(resource);
    checkServerDataMergeCompletion();
}

template <>
void RemoteToLocalSynchronizationManager::performPostAddOrUpdateChecks<
    SavedSearch>(const SavedSearch & search)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::performPostAddOrUpdateChecks"
            << "<SavedSearch>: " << search);

    unregisterSavedSearchPendingAddOrUpdate(search);
    checkServerDataMergeCompletion();
}

template <class ElementType>
void RemoteToLocalSynchronizationManager::unsetLocalUid(ElementType & element)
{
    element.unsetLocalUid();
}

template <>
void RemoteToLocalSynchronizationManager::unsetLocalUid<LinkedNotebook>(
    LinkedNotebook &)
{
    // do nothing, local uid doesn't make any sense to linked notebook
}

template <class ElementType>
void RemoteToLocalSynchronizationManager::setNonLocalAndNonDirty(
    ElementType & element)
{
    element.setLocal(false);
    element.setDirty(false);
}

template <>
void RemoteToLocalSynchronizationManager::setNonLocalAndNonDirty<
    LinkedNotebook>(LinkedNotebook & linkedNotebook)
{
    linkedNotebook.setDirty(false);
}

template <class ElementType>
void RemoteToLocalSynchronizationManager::onExpungeDataElementCompleted(
    const ElementType & element, const QUuid & requestId,
    const QString & typeName, QSet<QUuid> & expungeElementRequestIds)
{
    auto it = expungeElementRequestIds.find(requestId);
    if (it == expungeElementRequestIds.end()) {
        return;
    }

    QNTRACE(
        "synchronization:remote_to_local",
        "Expunged " << typeName << " from local storage: " << element);

    Q_UNUSED(expungeElementRequestIds.erase(it))

    performPostExpungeChecks<ElementType>();
    checkExpungesCompletion();
}

template <class ElementType>
void RemoteToLocalSynchronizationManager::onExpungeDataElementFailed(
    const ElementType & element, const QUuid & requestId,
    const ErrorString & errorDescription, const QString & typeName,
    QSet<QUuid> & expungeElementRequestIds)
{
    Q_UNUSED(element)
    Q_UNUSED(typeName)

    auto it = expungeElementRequestIds.find(requestId);
    if (it == expungeElementRequestIds.end()) {
        return;
    }

    QNDEBUG(
        "synchronization:remote_to_local",
        "Failed to expunge "
            << typeName << " from the local storage; won't panic since most "
            << "likely the corresponding data element has never "
            << "existed in the local storage in the first place. "
            << "Error description: " << errorDescription);
    Q_UNUSED(expungeElementRequestIds.erase(it))

    performPostExpungeChecks<ElementType>();
    checkExpungesCompletion();
}

void RemoteToLocalSynchronizationManager::expungeTags()
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::expungeTags: "
            << m_expungedTags.size());

    if (m_expungedTags.isEmpty()) {
        return;
    }

    Tag tagToExpunge;
    tagToExpunge.unsetLocalUid();

    const int numExpungedTags = m_expungedTags.size();
    for (int i = 0; i < numExpungedTags; ++i) {
        const QString & expungedTagGuid = m_expungedTags[i];
        tagToExpunge.setGuid(expungedTagGuid);

        QUuid expungeTagRequestId = QUuid::createUuid();
        Q_UNUSED(m_expungeTagRequestIds.insert(expungeTagRequestId));

        QNTRACE(
            "synchronization:remote_to_local",
            "Emitting the request to "
                << "expunge tag: guid = " << expungedTagGuid
                << ", request id = " << expungeTagRequestId);
        Q_EMIT expungeTag(tagToExpunge, expungeTagRequestId);
    }

    m_expungedTags.clear();
}

void RemoteToLocalSynchronizationManager::expungeSavedSearches()
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::expungeSavedSearches: "
            << m_expungedSavedSearches.size());

    if (m_expungedSavedSearches.isEmpty()) {
        return;
    }

    SavedSearch searchToExpunge;
    searchToExpunge.unsetLocalUid();

    const int numExpungedSearches = m_expungedSavedSearches.size();
    for (int i = 0; i < numExpungedSearches; ++i) {
        const QString & expungedSavedSerchGuid = m_expungedSavedSearches[i];
        searchToExpunge.setGuid(expungedSavedSerchGuid);

        QUuid expungeSavedSearchRequestId = QUuid::createUuid();
        Q_UNUSED(
            m_expungeSavedSearchRequestIds.insert(expungeSavedSearchRequestId));

        QNTRACE(
            "synchronization:remote_to_local",
            "Emitting the request to "
                << "expunge saved search: guid = " << expungedSavedSerchGuid
                << ", request id = " << expungeSavedSearchRequestId);

        Q_EMIT expungeSavedSearch(searchToExpunge, expungeSavedSearchRequestId);
    }

    m_expungedSavedSearches.clear();
}

void RemoteToLocalSynchronizationManager::expungeLinkedNotebooks()
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::expungeLinkedNotebooks: "
            << m_expungedLinkedNotebooks.size());

    if (m_expungedLinkedNotebooks.isEmpty()) {
        return;
    }

    LinkedNotebook linkedNotebookToExpunge;

    const int numExpungedLinkedNotebooks = m_expungedLinkedNotebooks.size();
    for (int i = 0; i < numExpungedLinkedNotebooks; ++i) {
        const QString & expungedLinkedNotebookGuid =
            m_expungedLinkedNotebooks[i];
        linkedNotebookToExpunge.setGuid(expungedLinkedNotebookGuid);

        QUuid expungeLinkedNotebookRequestId = QUuid::createUuid();
        Q_UNUSED(m_expungeLinkedNotebookRequestIds.insert(
            expungeLinkedNotebookRequestId));

        QNTRACE(
            "synchronization:remote_to_local",
            "Emitting the request to "
                << "expunge linked notebook: guid = "
                << expungedLinkedNotebookGuid
                << ", request id = " << expungeLinkedNotebookRequestId);

        Q_EMIT expungeLinkedNotebook(
            linkedNotebookToExpunge, expungeLinkedNotebookRequestId);
    }

    m_expungedLinkedNotebooks.clear();
}

void RemoteToLocalSynchronizationManager::expungeNotebooks()
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::expungeNotebooks: "
            << m_expungedNotebooks.size());

    if (m_expungedNotebooks.isEmpty()) {
        return;
    }

    Notebook notebookToExpunge;
    notebookToExpunge.unsetLocalUid();

    const int numExpungedNotebooks = m_expungedNotebooks.size();
    for (int i = 0; i < numExpungedNotebooks; ++i) {
        const QString & expungedNotebookGuid = m_expungedNotebooks[i];
        notebookToExpunge.setGuid(expungedNotebookGuid);

        QUuid expungeNotebookRequestId = QUuid::createUuid();
        Q_UNUSED(m_expungeNotebookRequestIds.insert(expungeNotebookRequestId));

        QNTRACE(
            "synchronization:remote_to_local",
            "Emitting the request to "
                << "expunge notebook: notebook guid = " << expungedNotebookGuid
                << ", request id = " << expungeNotebookRequestId);

        Q_EMIT expungeNotebook(notebookToExpunge, expungeNotebookRequestId);
    }

    m_expungedNotebooks.clear();
}

void RemoteToLocalSynchronizationManager::expungeNotes()
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::expungeNotes: "
            << m_expungedNotes.size());

    if (m_expungedNotes.isEmpty()) {
        return;
    }

    Note noteToExpunge;
    noteToExpunge.unsetLocalUid();

    const int numExpungedNotes = m_expungedNotes.size();
    for (int i = 0; i < numExpungedNotes; ++i) {
        const QString & expungedNoteGuid = m_expungedNotes[i];
        noteToExpunge.setGuid(expungedNoteGuid);

        QUuid expungeNoteRequestId = QUuid::createUuid();
        Q_UNUSED(m_expungeNoteRequestIds.insert(expungeNoteRequestId));

        QNTRACE(
            "synchronization:remote_to_local",
            "Emitting the request to "
                << "expunge note: guid = " << expungedNoteGuid
                << ", request id = " << expungeNoteRequestId);

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
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::expungeFromServerToClient");

    expungeNotes();
    expungeNotebooks();
    expungeSavedSearches();
    expungeTags();
    expungeLinkedNotebooks();

    checkExpungesCompletion();
}

void RemoteToLocalSynchronizationManager::checkExpungesCompletion()
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::checkExpungesCompletion");

    if (m_expungedTags.isEmpty() && m_expungeTagRequestIds.isEmpty() &&
        m_expungedNotebooks.isEmpty() &&
        m_expungeNotebookRequestIds.isEmpty() &&
        m_expungedSavedSearches.isEmpty() &&
        m_expungeSavedSearchRequestIds.isEmpty() &&
        m_expungedLinkedNotebooks.isEmpty() &&
        m_expungeLinkedNotebookRequestIds.isEmpty() &&
        m_expungedNotes.isEmpty() && m_expungeNoteRequestIds.isEmpty())
    {
        QNDEBUG(
            "synchronization:remote_to_local", "No pending expunge requests");

        if (syncingLinkedNotebooksContent()) {
            m_expungeNotelessTagsRequestId = QUuid::createUuid();

            QNTRACE(
                "synchronization:remote_to_local",
                "Emitting the request "
                    << "to expunge noteless tags from local storage: request "
                       "id = "
                    << m_expungeNotelessTagsRequestId);

            Q_EMIT expungeNotelessTagsFromLinkedNotebooks(
                m_expungeNotelessTagsRequestId);
        }
        else if (!m_expungedFromServerToClient) {
            m_expungedFromServerToClient = true;
            Q_EMIT expungedFromServerToClient();

            startLinkedNotebooksSync();
        }
    }
    else {
        QNDEBUG(
            "synchronization:remote_to_local",
            "Expunges not complete yet: "
                << "still have " << m_expungedTags.size()
                << " tags pending expunging, " << m_expungeTagRequestIds.size()
                << " expunge tag requests, " << m_expungedNotebooks.size()
                << " notebooks pending expunging, "
                << m_expungeNotebookRequestIds.size()
                << " expunge notebook requests, "
                << m_expungedSavedSearches.size()
                << " saved searches pending expunging, "
                << m_expungeSavedSearchRequestIds.size()
                << " expunge saved search requests, "
                << m_expungedLinkedNotebooks.size()
                << " linked notebooks pending expunging, "
                << m_expungeLinkedNotebookRequestIds.size()
                << " expunge linked notebook requests, "
                << m_expungedNotes.size() << " notes pendinig expunging, "
                << m_expungeNoteRequestIds.size() << " expunge note requests");
    }
}

template <class ElementType>
QString RemoteToLocalSynchronizationManager::checkAndAddLinkedNotebookBinding(
    ElementType & element)
{
    Q_UNUSED(element);
    // Do nothing in default instantiation, only tags and notebooks need to be
    // processed specifically
    return QString();
}

template <>
QString
RemoteToLocalSynchronizationManager::checkAndAddLinkedNotebookBinding<Notebook>(
    Notebook & notebook)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::checkAndAddLinkedNotebookBinding"
            << "<Notebook>: " << notebook);

    if (!notebook.hasGuid()) {
        QNDEBUG("synchronization:remote_to_local", "The notebook has no guid");
        return QString();
    }

    auto it = m_linkedNotebookGuidsByNotebookGuids.find(notebook.guid());
    if (it == m_linkedNotebookGuidsByNotebookGuids.end()) {
        QNDEBUG(
            "synchronization:remote_to_local",
            "Found no linked notebook "
                << "guid for notebook guid " << notebook.guid());
        return QString();
    }

    notebook.setLinkedNotebookGuid(it.value());

    QNDEBUG(
        "synchronization:remote_to_local",
        "Set linked notebook guid " << it.value() << " to the notebook");

    // NOTE: the notebook coming from the linked notebook might be marked as
    // default and/or last used which might not make much sense in the context
    // of the user's own default and/or last used notebooks so removing these
    // two properties
    notebook.setLastUsed(false);
    notebook.setDefaultNotebook(false);

    return it.value();
}

template <>
QString
RemoteToLocalSynchronizationManager::checkAndAddLinkedNotebookBinding<Tag>(
    Tag & tag)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::checkAndAddLinkedNotebookBinding"
            << "<Tag>: " << tag);

    if (!tag.hasGuid()) {
        QNDEBUG("synchronization:remote_to_local", "The tag has no guid");
        return QString();
    }

    auto it = m_linkedNotebookGuidsByTagGuids.find(tag.guid());
    if (it == m_linkedNotebookGuidsByTagGuids.end()) {
        QNDEBUG(
            "synchronization:remote_to_local",
            "Found no linked notebook "
                << "guid for tag guid " << tag.guid());
        return QString();
    }

    tag.setLinkedNotebookGuid(it.value());

    QNDEBUG(
        "synchronization:remote_to_local",
        "Set linked notebook guid " << it.value() << " to the tag");

    return it.value();
}

template <>
void RemoteToLocalSynchronizationManager::emitFindByGuidRequest<
    qevercloud::Tag>(const qevercloud::Tag & qecTag)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::emitFindByGuidRequest<Tag>: "
            << "tag = " << qecTag);

    if (Q_UNLIKELY(!qecTag.guid.isSet())) {
        ErrorString errorDescription(QT_TRANSLATE_NOOP(
            "RemoteToLocalSynchronizationManager",
            "Internal error: detected attempt to find tag "
            "by guid using tag which doesn't have a guid"));
        QNWARNING(
            "synchronization:remote_to_local",
            errorDescription << ": " << qecTag);
        Q_EMIT failure(errorDescription);
        return;
    }

    Tag tag;
    tag.unsetLocalUid();
    tag.setGuid(qecTag.guid.ref());
    checkAndAddLinkedNotebookBinding(tag);

    QUuid requestId = QUuid::createUuid();
    Q_UNUSED(m_findTagByGuidRequestIds.insert(requestId))

    QNTRACE(
        "synchronization:remote_to_local",
        "Emitting the request to find "
            << "tag in the local storage: "
            << "request id = " << requestId << ", tag: " << tag);

    Q_EMIT findTag(tag, requestId);
}

template <>
void RemoteToLocalSynchronizationManager::emitFindByGuidRequest<
    qevercloud::SavedSearch>(const qevercloud::SavedSearch & qecSearch)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::emitFindByGuidRequest"
            << "<SavedSearch>: search = " << qecSearch);

    if (Q_UNLIKELY(!qecSearch.guid.isSet())) {
        ErrorString errorDescription(QT_TRANSLATE_NOOP(
            "RemoteToLocalSynchronizationManager",
            "Internal error: detected attempt to find saved "
            "search by guid using saved search which doesn't "
            "have a guid"));
        QNWARNING(
            "synchronization:remote_to_local",
            errorDescription << ": " << qecSearch);
        Q_EMIT failure(errorDescription);
        return;
    }

    SavedSearch search;
    search.unsetLocalUid();
    search.setGuid(qecSearch.guid.ref());

    QUuid requestId = QUuid::createUuid();
    Q_UNUSED(m_findSavedSearchByGuidRequestIds.insert(requestId));

    QNTRACE(
        "synchronization:remote_to_local",
        "Emitting the request to find "
            << "saved search in the local storage: request id = " << requestId
            << ", saved search: " << search);
    Q_EMIT findSavedSearch(search, requestId);
}

template <>
void RemoteToLocalSynchronizationManager::emitFindByGuidRequest<
    qevercloud::Notebook>(const qevercloud::Notebook & qecNotebook)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::emitFindByGuidRequest<Notebook>: "
            << "notebook = " << qecNotebook);

    if (Q_UNLIKELY(!qecNotebook.guid.isSet())) {
        ErrorString errorDescription(QT_TRANSLATE_NOOP(
            "RemoteToLocalSynchronizationManager",
            "Internal error: detected attempt to find notebook "
            "by guid using notebook which doesn't have a guid"));
        QNWARNING(
            "synchronization:remote_to_local",
            errorDescription << ": " << qecNotebook);
        Q_EMIT failure(errorDescription);
        return;
    }

    Notebook notebook;
    notebook.unsetLocalUid();
    notebook.setGuid(qecNotebook.guid.ref());
    checkAndAddLinkedNotebookBinding(notebook);

    QUuid requestId = QUuid::createUuid();
    Q_UNUSED(m_findNotebookByGuidRequestIds.insert(requestId));
    QNTRACE(
        "synchronization:remote_to_local",
        "Emitting the request to find "
            << "notebook in the local storage: request id = " << requestId
            << ", notebook: " << notebook);
    Q_EMIT findNotebook(notebook, requestId);
}

template <>
void RemoteToLocalSynchronizationManager::emitFindByGuidRequest<
    qevercloud::LinkedNotebook>(
    const qevercloud::LinkedNotebook & qecLinkedNotebook)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::emitFindByGuidRequest"
            << "<LinkedNotebook>: linked notebook = " << qecLinkedNotebook);

    if (Q_UNLIKELY(!qecLinkedNotebook.guid.isSet())) {
        ErrorString errorDescription(QT_TRANSLATE_NOOP(
            "RemoteToLocalSynchronizationManager",
            "Internal error: detected attempt to find linked "
            "notebook by guid using linked notebook which "
            "doesn't have a guid"));
        QNWARNING(
            "synchronization:remote_to_local",
            errorDescription << ": " << qecLinkedNotebook);
        Q_EMIT failure(errorDescription);
        return;
    }

    LinkedNotebook linkedNotebook(qecLinkedNotebook);

    QUuid requestId = QUuid::createUuid();
    Q_UNUSED(m_findLinkedNotebookRequestIds.insert(requestId))

    QNTRACE(
        "synchronization:remote_to_local",
        "Emitting the request to find "
            << "linked notebook in the local storage: request id = "
            << requestId << ", linked notebook: " << linkedNotebook);
    Q_EMIT findLinkedNotebook(linkedNotebook, requestId);
}

template <>
void RemoteToLocalSynchronizationManager::emitFindByGuidRequest<
    qevercloud::Note>(const qevercloud::Note & qecNote)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::emitFindByGuidRequest<Note>: "
            << "note = " << qecNote);

    if (Q_UNLIKELY(!qecNote.guid.isSet())) {
        ErrorString errorDescription(QT_TRANSLATE_NOOP(
            "RemoteToLocalSynchronizationManager",
            "Internal error: detected attempt to find note "
            "by guid using note which doesn't have a guid"));
        QNWARNING(
            "synchronization:remote_to_local",
            errorDescription << ": " << qecNote);
        Q_EMIT failure(errorDescription);
        return;
    }

    if (Q_UNLIKELY(!qecNote.notebookGuid.isSet())) {
        ErrorString errorDescription(QT_TRANSLATE_NOOP(
            "RemoteToLocalSynchronizationManager",
            "Internal error: the note from the Evernote "
            "service has no notebook guid"));
        QNWARNING(
            "synchronization:remote_to_local",
            errorDescription << ": " << qecNote);
        Q_EMIT failure(errorDescription);
        return;
    }

    Note note;
    note.unsetLocalUid();
    note.setGuid(qecNote.guid.ref());
    note.setNotebookGuid(qecNote.notebookGuid.ref());

    QUuid requestId = QUuid::createUuid();
    Q_UNUSED(m_findNoteByGuidRequestIds.insert(requestId))

    QNTRACE(
        "synchronization:remote_to_local",
        "Emitting the request to find "
            << "note in the local storage: request id = " << requestId
            << ", note: " << note);

    LocalStorageManager::GetNoteOptions options =
        LocalStorageManager::GetNoteOption::WithResourceMetadata;

    Q_EMIT findNote(note, options, requestId);
}

template <>
void RemoteToLocalSynchronizationManager::emitFindByGuidRequest<
    qevercloud::Resource>(const qevercloud::Resource & qecResource)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::emitFindByGuidRequest<Resource>: "
            << "resource = " << qecResource);

    if (Q_UNLIKELY(!qecResource.guid.isSet())) {
        ErrorString errorDescription(QT_TRANSLATE_NOOP(
            "RemoteToLocalSynchronizationManager",
            "Internal error: detected attempt to find "
            "resource by guid using resource which doesn't "
            "have a guid"));
        QNWARNING(
            "synchronization:remote_to_local",
            errorDescription << ": " << qecResource);
        Q_EMIT failure(errorDescription);
        return;
    }

    if (Q_UNLIKELY(!qecResource.noteGuid.isSet())) {
        ErrorString errorDescription(QT_TRANSLATE_NOOP(
            "RemoteToLocalSynchronizationManager",
            "Internal error: detected attempt to find "
            "resource by guid using resource which doesn't "
            "have a note guid"));
        QNWARNING(
            "synchronization:remote_to_local",
            errorDescription << ": " << qecResource);
        Q_EMIT failure(errorDescription);
        return;
    }

    Resource resource;

    /**
     * NOTE: this is very important! If the resource is not dirty, the failure
     * to find it in the local storage (i.e. when the resource is new) might
     * cause the sync conflict resulting in conflicts of notes
     */
    resource.setDirty(false);

    resource.setLocal(false);
    resource.unsetLocalUid();
    resource.setGuid(qecResource.guid.ref());
    resource.setNoteGuid(qecResource.noteGuid.ref());

    QUuid requestId = QUuid::createUuid();
    Q_UNUSED(m_findResourceByGuidRequestIds.insert(requestId));
    QNTRACE(
        "synchronization:remote_to_local",
        "Emitting the request to find "
            << "resource in the local storage: request id = " << requestId
            << ", resource: " << resource);

#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    LocalStorageManager::GetResourceOptions options;
#else
    LocalStorageManager::GetResourceOptions options(0);
#endif

    Q_EMIT findResource(resource, options, requestId);
}

void RemoteToLocalSynchronizationManager::onAddLinkedNotebookCompleted(
    LinkedNotebook linkedNotebook, QUuid requestId)
{
    handleLinkedNotebookAdded(linkedNotebook);

    auto it = m_addLinkedNotebookRequestIds.find(requestId);
    if (it != m_addLinkedNotebookRequestIds.end()) {
        QNDEBUG(
            "synchronization:remote_to_local",
            "RemoteToLocalSynchronizationManager::onAddLinkedNotebookCompleted:"
            " "
                << "linked notebook = " << linkedNotebook
                << ", request id = " << requestId);

        Q_UNUSED(m_addLinkedNotebookRequestIds.erase(it))
        checkServerDataMergeCompletion();
    }
}

void RemoteToLocalSynchronizationManager::onAddLinkedNotebookFailed(
    LinkedNotebook linkedNotebook, ErrorString errorDescription,
    QUuid requestId)
{
    onAddDataElementFailed(
        linkedNotebook, requestId, errorDescription,
        QStringLiteral("LinkedNotebook"), m_addLinkedNotebookRequestIds);
}

void RemoteToLocalSynchronizationManager::onUpdateLinkedNotebookCompleted(
    LinkedNotebook linkedNotebook, QUuid requestId)
{
    handleLinkedNotebookUpdated(linkedNotebook);

    auto it = m_updateLinkedNotebookRequestIds.find(requestId);
    if (it != m_updateLinkedNotebookRequestIds.end()) {
        QNDEBUG(
            "synchronization:remote_to_local",
            "RemoteToLocalSynchronizationManager"
                << "::onUpdateLinkedNotebookCompleted: linkedNotebook = "
                << linkedNotebook << ", requestId = " << requestId);

        Q_UNUSED(m_updateLinkedNotebookRequestIds.erase(it));
        checkServerDataMergeCompletion();
    }
}

void RemoteToLocalSynchronizationManager::onUpdateLinkedNotebookFailed(
    LinkedNotebook linkedNotebook, ErrorString errorDescription,
    QUuid requestId)
{
    auto it = m_updateLinkedNotebookRequestIds.find(requestId);
    if (it != m_updateLinkedNotebookRequestIds.end()) {
        QNDEBUG(
            "synchronization:remote_to_local",
            "RemoteToLocalSynchronizationManager"
                << "::onUpdateLinkedNotebookFailed: linkedNotebook = "
                << linkedNotebook << ", errorDescription = " << errorDescription
                << ", requestId = " << requestId);

        ErrorString error(QT_TR_NOOP(
            "Failed to update linked notebook in the local storage"));
        error.additionalBases().append(errorDescription.base());
        error.additionalBases().append(errorDescription.additionalBases());
        error.details() = errorDescription.details();
        Q_EMIT failure(error);
    }
}

void RemoteToLocalSynchronizationManager::onExpungeLinkedNotebookCompleted(
    LinkedNotebook linkedNotebook, QUuid requestId)
{
    onExpungeDataElementCompleted(
        linkedNotebook, requestId, QStringLiteral("Linked notebook"),
        m_expungeLinkedNotebookRequestIds);

    if (Q_UNLIKELY(!linkedNotebook.hasGuid())) {
        QNWARNING(
            "synchronization:remote_to_local",
            "Detected expunging of "
                << "a linked notebook without guid: " << linkedNotebook);
        return;
    }

    const QString & linkedNotebookGuid = linkedNotebook.guid();

    auto notebookSyncCacheIt =
        m_notebookSyncCachesByLinkedNotebookGuids.find(linkedNotebookGuid);

    if (notebookSyncCacheIt != m_notebookSyncCachesByLinkedNotebookGuids.end())
    {
        NotebookSyncCache * pNotebookSyncCache = notebookSyncCacheIt.value();
        if (pNotebookSyncCache) {
            pNotebookSyncCache->disconnect();
            pNotebookSyncCache->setParent(nullptr);
            pNotebookSyncCache->deleteLater();
        }

        Q_UNUSED(m_notebookSyncCachesByLinkedNotebookGuids.erase(
            notebookSyncCacheIt))
    }

    auto tagSyncCacheIt =
        m_tagSyncCachesByLinkedNotebookGuids.find(linkedNotebookGuid);

    if (tagSyncCacheIt != m_tagSyncCachesByLinkedNotebookGuids.end()) {
        TagSyncCache * pTagSyncCache = tagSyncCacheIt.value();
        if (pTagSyncCache) {
            pTagSyncCache->disconnect();
            pTagSyncCache->setParent(nullptr);
            pTagSyncCache->deleteLater();
        }

        Q_UNUSED(m_tagSyncCachesByLinkedNotebookGuids.erase(tagSyncCacheIt))
    }

    auto linkedNotebookGuidPendingTagSyncCacheIt =
        m_linkedNotebookGuidsPendingTagSyncCachesFill.find(linkedNotebookGuid);

    if (linkedNotebookGuidPendingTagSyncCacheIt !=
        m_linkedNotebookGuidsPendingTagSyncCachesFill.end())
    {
        Q_UNUSED(m_linkedNotebookGuidsPendingTagSyncCachesFill.erase(
            linkedNotebookGuidPendingTagSyncCacheIt))
    }
}

void RemoteToLocalSynchronizationManager::onExpungeLinkedNotebookFailed(
    LinkedNotebook linkedNotebook, ErrorString errorDescription,
    QUuid requestId)
{
    onExpungeDataElementFailed(
        linkedNotebook, requestId, errorDescription,
        QStringLiteral("Linked notebook"), m_expungeLinkedNotebookRequestIds);
}

void RemoteToLocalSynchronizationManager::onListAllLinkedNotebooksCompleted(
    size_t limit, size_t offset,
    LocalStorageManager::ListLinkedNotebooksOrder order,
    LocalStorageManager::OrderDirection orderDirection,
    QList<LinkedNotebook> linkedNotebooks, QUuid requestId)
{
    if (requestId != m_listAllLinkedNotebooksRequestId) {
        return;
    }

    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager"
            << "::onListAllLinkedNotebooksCompleted: limit = " << limit
            << ", offset = " << offset << ", order = " << order
            << ", order direction = " << orderDirection
            << ", requestId = " << requestId);

    m_listAllLinkedNotebooksRequestId = QUuid();
    m_allLinkedNotebooks = linkedNotebooks;
    m_allLinkedNotebooksListed = true;

    startLinkedNotebooksSync();
}

void RemoteToLocalSynchronizationManager::onListAllLinkedNotebooksFailed(
    size_t limit, size_t offset,
    LocalStorageManager::ListLinkedNotebooksOrder order,
    LocalStorageManager::OrderDirection orderDirection,
    ErrorString errorDescription, QUuid requestId)
{
    if (requestId != m_listAllLinkedNotebooksRequestId) {
        return;
    }

    QNWARNING(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::onListAllLinkedNotebooksFailed: "
            << "limit = " << limit << ", offset = " << offset
            << ", order = " << order << ", order direction = " << orderDirection
            << ", error description = " << errorDescription
            << "; request id = " << requestId);

    m_allLinkedNotebooksListed = false;

    ErrorString error(
        QT_TR_NOOP("Failed to list all linked notebooks from "
                   "the local storage"));
    error.additionalBases().append(errorDescription.base());
    error.additionalBases().append(errorDescription.additionalBases());
    error.details() = errorDescription.details();
    Q_EMIT failure(error);
}

void RemoteToLocalSynchronizationManager::onAddNotebookCompleted(
    Notebook notebook, QUuid requestId)
{
    onAddDataElementCompleted(
        notebook, requestId, QStringLiteral("Notebook"),
        m_addNotebookRequestIds);
}

void RemoteToLocalSynchronizationManager::onAddNotebookFailed(
    Notebook notebook, ErrorString errorDescription, QUuid requestId)
{
    onAddDataElementFailed(
        notebook, requestId, errorDescription, QStringLiteral("Notebook"),
        m_addNotebookRequestIds);
}

void RemoteToLocalSynchronizationManager::onUpdateNotebookCompleted(
    Notebook notebook, QUuid requestId)
{
    onUpdateDataElementCompleted(
        notebook, requestId, QStringLiteral("Notebook"),
        m_updateNotebookRequestIds);
}

void RemoteToLocalSynchronizationManager::onUpdateNotebookFailed(
    Notebook notebook, ErrorString errorDescription, QUuid requestId)
{
    onUpdateDataElementFailed(
        notebook, requestId, errorDescription, QStringLiteral("Notebook"),
        m_updateNotebookRequestIds);
}

void RemoteToLocalSynchronizationManager::onExpungeNotebookCompleted(
    Notebook notebook, QUuid requestId)
{
    onExpungeDataElementCompleted(
        notebook, requestId, QStringLiteral("Notebook"),
        m_expungeNotebookRequestIds);
}

void RemoteToLocalSynchronizationManager::onExpungeNotebookFailed(
    Notebook notebook, ErrorString errorDescription, QUuid requestId)
{
    onExpungeDataElementFailed(
        notebook, requestId, errorDescription, QStringLiteral("Notebook"),
        m_expungeNotebookRequestIds);
}

void RemoteToLocalSynchronizationManager::onAddNoteCompleted(
    Note note, QUuid requestId)
{
    onAddDataElementCompleted(
        note, requestId, QStringLiteral("Note"), m_addNoteRequestIds);
}

void RemoteToLocalSynchronizationManager::onAddNoteFailed(
    Note note, ErrorString errorDescription, QUuid requestId)
{
    onAddDataElementFailed(
        note, requestId, errorDescription, QStringLiteral("Note"),
        m_addNoteRequestIds);
}

void RemoteToLocalSynchronizationManager::onUpdateNoteCompleted(
    Note note, LocalStorageManager::UpdateNoteOptions options, QUuid requestId)
{
    Q_UNUSED(options)

    auto it = m_updateNoteRequestIds.find(requestId);
    if (it != m_updateNoteRequestIds.end()) {
        QNDEBUG(
            "synchronization:remote_to_local",
            "RemoteToLocalSynchronizationManager::onUpdateNoteCompleted: "
                << "note = " << note << "\nRequestId = " << requestId);

        Q_UNUSED(m_updateNoteRequestIds.erase(it));

        performPostAddOrUpdateChecks(note);
        checkServerDataMergeCompletion();
        return;
    }

    auto tit = m_updateNoteWithThumbnailRequestIds.find(requestId);
    if (tit != m_updateNoteWithThumbnailRequestIds.end()) {
        QNDEBUG(
            "synchronization:remote_to_local",
            "RemoteToLocalSynchronizationManager::onUpdateNoteCompleted: "
                << "note with updated thumbnail = " << note
                << "\nRequestId = " << requestId);

        Q_UNUSED(m_updateNoteWithThumbnailRequestIds.erase(tit))

        checkAndIncrementNoteDownloadProgress(
            note.hasGuid() ? note.guid() : QString());

        performPostAddOrUpdateChecks(note);
        checkServerDataMergeCompletion();
        return;
    }

    auto mdit =
        m_resourcesByMarkNoteOwningResourceDirtyRequestIds.find(requestId);

    if (mdit != m_resourcesByMarkNoteOwningResourceDirtyRequestIds.end()) {
        QNDEBUG(
            "synchronization:remote_to_local",
            "RemoteToLocalSynchronizationManager::onUpdateNoteCompleted: "
                << "note owning added or updated resource was marked as dirty: "
                << "request id = " << requestId << ", note: " << note);

        Resource resource = mdit.value();
        Q_UNUSED(m_resourcesByMarkNoteOwningResourceDirtyRequestIds.erase(mdit))
        performPostAddOrUpdateChecks(resource);
        checkServerDataMergeCompletion();
        return;
    }
}

void RemoteToLocalSynchronizationManager::onUpdateNoteFailed(
    Note note, LocalStorageManager::UpdateNoteOptions options,
    ErrorString errorDescription, QUuid requestId)
{
    Q_UNUSED(options)

    auto it = m_updateNoteRequestIds.find(requestId);
    if (it != m_updateNoteRequestIds.end()) {
        QNWARNING(
            "synchronization:remote_to_local",
            "RemoteToLocalSynchronizationManager::onUpdateNoteFailed: note = "
                << note << "\nErrorDescription = " << errorDescription
                << "\nRequestId = " << requestId);

        Q_UNUSED(m_updateNoteRequestIds.erase(it))

        ErrorString error(
            QT_TR_NOOP("Failed to update note in the local storage"));
        error.additionalBases().append(errorDescription.base());
        error.additionalBases().append(errorDescription.additionalBases());
        error.details() = errorDescription.details();
        Q_EMIT failure(error);
        return;
    }

    auto tit = m_updateNoteWithThumbnailRequestIds.find(requestId);
    if (tit != m_updateNoteWithThumbnailRequestIds.end()) {
        QNWARNING(
            "synchronization:remote_to_local",
            "RemoteToLocalSynchronizationManager::onUpdateNoteFailed: note "
                << "with thumbnail = " << note << "\nErrorDescription = "
                << errorDescription << "\nRequestId = " << requestId);

        Q_UNUSED(m_updateNoteWithThumbnailRequestIds.erase(tit))

        checkAndIncrementNoteDownloadProgress(
            note.hasGuid() ? note.guid() : QString());

        checkServerDataMergeCompletion();
        return;
    }

    auto mdit =
        m_resourcesByMarkNoteOwningResourceDirtyRequestIds.find(requestId);
    if (mdit != m_resourcesByMarkNoteOwningResourceDirtyRequestIds.end()) {
        QNWARNING(
            "synchronization:remote_to_local",
            "RemoteToLocalSynchronizationManager::onUpdateNoteFailed: "
                << "failed to mark the resource owning note as dirty: "
                << errorDescription << ", request id = " << requestId
                << ", note: " << note);

        Q_UNUSED(m_resourcesByMarkNoteOwningResourceDirtyRequestIds.erase(mdit))

        ErrorString error(
            QT_TR_NOOP("Failed to mark the resource owning note "
                       "dirty in the local storage"));
        error.additionalBases().append(errorDescription.base());
        error.additionalBases().append(errorDescription.additionalBases());
        error.details() = errorDescription.details();
        Q_EMIT failure(error);
        return;
    }
}

void RemoteToLocalSynchronizationManager::onExpungeNoteCompleted(
    Note note, QUuid requestId)
{
    onExpungeDataElementCompleted(
        note, requestId, QStringLiteral("Note"), m_expungeNoteRequestIds);
}

void RemoteToLocalSynchronizationManager::onExpungeNoteFailed(
    Note note, ErrorString errorDescription, QUuid requestId)
{
    onExpungeDataElementFailed(
        note, requestId, errorDescription, QStringLiteral("Note"),
        m_expungeNoteRequestIds);
}

void RemoteToLocalSynchronizationManager::onAddResourceCompleted(
    Resource resource, QUuid requestId)
{
    onAddDataElementCompleted(
        resource, requestId, QStringLiteral("Resource"),
        m_addResourceRequestIds);
}

void RemoteToLocalSynchronizationManager::onAddResourceFailed(
    Resource resource, ErrorString errorDescription, QUuid requestId)
{
    onAddDataElementFailed(
        resource, requestId, errorDescription, QStringLiteral("Resource"),
        m_addResourceRequestIds);
}

void RemoteToLocalSynchronizationManager::onUpdateResourceCompleted(
    Resource resource, QUuid requestId)
{
    onUpdateDataElementCompleted<Resource>(
        resource, requestId, QStringLiteral("Resource"),
        m_updateResourceRequestIds);
}

void RemoteToLocalSynchronizationManager::onUpdateResourceFailed(
    Resource resource, ErrorString errorDescription, QUuid requestId)
{
    auto it = m_updateResourceRequestIds.find(requestId);
    if (it != m_updateResourceRequestIds.end()) {
        QNDEBUG(
            "synchronization:remote_to_local",
            "RemoteToLocalSynchronizationManager::onUpdateResourceFailed: "
                << "resource = " << resource << "\nrequestId = " << requestId);

        ErrorString error(
            QT_TR_NOOP("Failed to update resource in the local storage"));
        error.additionalBases().append(errorDescription.base());
        error.additionalBases().append(errorDescription.additionalBases());
        error.details() = errorDescription.details();
        Q_EMIT failure(error);
    }
}

void RemoteToLocalSynchronizationManager::onInkNoteImageDownloadFinished(
    bool status, QString resourceGuid, QString noteGuid,
    ErrorString errorDescription)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::onInkNoteImageDownloadFinished: "
            << "status = " << (status ? "true" : "false")
            << ", resource guid = " << resourceGuid << ", note guid = "
            << noteGuid << ", error description = " << errorDescription);

    if (!status) {
        QNWARNING("synchronization:remote_to_local", errorDescription);
    }

    if (m_resourceGuidsPendingInkNoteImageDownloadPerNoteGuid.remove(
            noteGuid, resourceGuid))
    {
        checkAndIncrementNoteDownloadProgress(noteGuid);
        checkServerDataMergeCompletion();
    }
    else {
        QNDEBUG(
            "synchronization:remote_to_local",
            "No such combination of "
                << "note guid and resource guid was found pending ink note "
                   "image "
                << "download");
    }
}

void RemoteToLocalSynchronizationManager::onNoteThumbnailDownloadingFinished(
    bool status, QString noteGuid, QByteArray downloadedThumbnailImageData,
    ErrorString errorDescription)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::"
        "onNoteThumbnailDownloadingFinished: "
            << "status = " << (status ? "true" : "false") << ", note guid = "
            << noteGuid << ", error description = " << errorDescription);

    auto it = m_notesPendingThumbnailDownloadByGuid.find(noteGuid);
    if (Q_UNLIKELY(it == m_notesPendingThumbnailDownloadByGuid.end())) {
        QNDEBUG(
            "synchronization:remote_to_local",
            "Received note thumbnail downloaded event for note which was not "
                << "pending it; the slot invoking must be the stale one, "
                << "ignoring it");
        return;
    }

    Note note = it.value();
    Q_UNUSED(m_notesPendingThumbnailDownloadByGuid.erase(it))

    if (!status) {
        QNWARNING("synchronization:remote_to_local", errorDescription);
        checkAndIncrementNoteDownloadProgress(noteGuid);
        checkServerDataMergeCompletion();
        return;
    }

    note.setThumbnailData(downloadedThumbnailImageData);

    QUuid updateNoteRequestId = QUuid::createUuid();
    Q_UNUSED(m_updateNoteWithThumbnailRequestIds.insert(updateNoteRequestId))

    QNTRACE(
        "synchronization:remote_to_local",
        "Emitting the request to update "
            << "note with downloaded thumbnail: request id = "
            << updateNoteRequestId << ", note: " << note);

    Q_EMIT updateNote(
        note,
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
        LocalStorageManager::UpdateNoteOptions(),
#else
        LocalStorageManager::UpdateNoteOptions(0),
#endif
        updateNoteRequestId);
}

void RemoteToLocalSynchronizationManager::onAuthenticationInfoReceived(
    QString authToken, QString shardId, qevercloud::Timestamp expirationTime)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::onAuthenticationInfoReceived: "
            << "expiration time = "
            << printableDateTimeFromTimestamp(expirationTime));

    bool wasPending = m_pendingAuthenticationTokenAndShardId;

    // NOTE: we only need this authentication information to download
    // the thumbnails and ink note images
    m_authenticationToken = authToken;
    m_shardId = shardId;
    m_authenticationTokenExpirationTime = expirationTime;
    m_pendingAuthenticationTokenAndShardId = false;

    if (!wasPending) {
        return;
    }

    Q_EMIT authDataUpdated(authToken, shardId, expirationTime);
    launchSync();
}

void RemoteToLocalSynchronizationManager::
    onAuthenticationTokensForLinkedNotebooksReceived(
        QHash<QString, std::pair<QString, QString>>
            authenticationTokensAndShardIdsByLinkedNotebookGuid,
        QHash<QString, qevercloud::Timestamp>
            authenticationTokenExpirationTimesByLinkedNotebookGuid)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::"
            << "onAuthenticationTokensForLinkedNotebooksReceived");

    bool wasPending = m_pendingAuthenticationTokensForLinkedNotebooks;

    m_authenticationTokensAndShardIdsByLinkedNotebookGuid =
        authenticationTokensAndShardIdsByLinkedNotebookGuid;

    m_authenticationTokenExpirationTimesByLinkedNotebookGuid =
        authenticationTokenExpirationTimesByLinkedNotebookGuid;

    m_pendingAuthenticationTokensForLinkedNotebooks = false;

    if (!wasPending) {
        QNDEBUG(
            "synchronization:remote_to_local",
            "Authentication tokens for "
                << "linked notebooks were not requested");
        return;
    }

    Q_EMIT linkedNotebookAuthDataUpdated(
        authenticationTokensAndShardIdsByLinkedNotebookGuid,
        authenticationTokenExpirationTimesByLinkedNotebookGuid);

    startLinkedNotebooksSync();
}

void RemoteToLocalSynchronizationManager::onLastSyncParametersReceived(
    qint32 lastUpdateCount, qevercloud::Timestamp lastSyncTime,
    QHash<QString, qint32> lastUpdateCountByLinkedNotebookGuid,
    QHash<QString, qevercloud::Timestamp> lastSyncTimeByLinkedNotebookGuid)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::onLastSyncParametersReceived: "
            << "last update count = " << lastUpdateCount
            << ", last sync time = " << lastSyncTime
            << ", last update counts per linked notebook = "
            << lastUpdateCountByLinkedNotebookGuid
            << ", last sync time per linked notebook = "
            << lastSyncTimeByLinkedNotebookGuid);

    m_lastUpdateCount = lastUpdateCount;
    m_lastSyncTime = lastSyncTime;
    m_lastUpdateCountByLinkedNotebookGuid = lastUpdateCountByLinkedNotebookGuid;
    m_lastSyncTimeByLinkedNotebookGuid = lastSyncTimeByLinkedNotebookGuid;

    m_gotLastSyncParameters = true;

    if ((m_lastUpdateCount > 0) && (m_lastSyncTime > 0)) {
        m_onceSyncDone = true;
    }

    m_linkedNotebookGuidsOnceFullySynced.clear();
    for (auto it = m_lastSyncTimeByLinkedNotebookGuid.constBegin(),
              end = m_lastSyncTimeByLinkedNotebookGuid.constEnd();
         it != end; ++it)
    {
        const QString & linkedNotebookGuid = it.key();
        qevercloud::Timestamp lastSyncTime = it.value();
        if (lastSyncTime != 0) {
            Q_UNUSED(
                m_linkedNotebookGuidsOnceFullySynced.insert(linkedNotebookGuid))
        }
    }

    start(m_lastUsnOnStart);
}

void RemoteToLocalSynchronizationManager::setDownloadNoteThumbnails(
    const bool flag)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::setDownloadNoteThumbnails: "
            << "flag = " << (flag ? "true" : "false"));

    ApplicationSettings appSettings(
        account(), SYNCHRONIZATION_PERSISTENCE_NAME);

    appSettings.beginGroup(SYNC_SETTINGS_KEY_GROUP);
    appSettings.setValue(SHOULD_DOWNLOAD_NOTE_THUMBNAILS, flag);
    appSettings.endGroup();
}

void RemoteToLocalSynchronizationManager::setDownloadInkNoteImages(
    const bool flag)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::setDownloadInkNoteImages: flag = "
            << (flag ? "true" : "false"));

    ApplicationSettings appSettings(
        account(), SYNCHRONIZATION_PERSISTENCE_NAME);

    appSettings.beginGroup(SYNC_SETTINGS_KEY_GROUP);
    appSettings.setValue(SHOULD_DOWNLOAD_INK_NOTE_IMAGES, flag);
    appSettings.endGroup();
}

void RemoteToLocalSynchronizationManager::setInkNoteImagesStoragePath(
    const QString & path)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::setInkNoteImagesStoragePath: "
            << "path = " << path);

    QString actualPath = path;

    QFileInfo pathInfo(path);
    if (!pathInfo.exists()) {
        QDir pathDir(path);
        bool res = pathDir.mkpath(path);
        if (!res) {
            actualPath = defaultInkNoteImageStoragePath();
            QNWARNING(
                "synchronization:remote_to_local",
                "Could not create "
                    << "folder for ink note images storage: " << path
                    << ", fallback to using the default path " << actualPath);
        }
        else {
            QNDEBUG(
                "synchronization:remote_to_local",
                "Successfully created "
                    << "the folder for ink note images storage: "
                    << actualPath);
        }
    }
    else if (Q_UNLIKELY(!pathInfo.isDir())) {
        actualPath = defaultInkNoteImageStoragePath();
        QNWARNING(
            "synchronization:remote_to_local",
            "The specified ink note "
                << "images storage path is not a directory: " << path
                << ", fallback to using the default path " << actualPath);
    }
    else if (Q_UNLIKELY(!pathInfo.isWritable())) {
        actualPath = defaultInkNoteImageStoragePath();
        QNWARNING(
            "synchronization:remote_to_local",
            "The specified ink note "
                << "images storage path is not writable: " << path
                << ", fallback to using the default path " << actualPath);
    }

    ApplicationSettings appSettings(
        account(), SYNCHRONIZATION_PERSISTENCE_NAME);

    appSettings.beginGroup(SYNC_SETTINGS_KEY_GROUP);
    appSettings.setValue(INK_NOTE_IMAGES_STORAGE_PATH_KEY, actualPath);
    appSettings.endGroup();
}

void RemoteToLocalSynchronizationManager::collectNonProcessedItemsSmallestUsns(
    qint32 & usn, QHash<QString, qint32> & usnByLinkedNotebookGuid)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::"
        "collectNonProcessedItemsSmallestUsns");

    usn = -1;
    usnByLinkedNotebookGuid.clear();

    QNDEBUG(
        "synchronization:remote_to_local",
        "User own data sync chunks "
            << "downloaded = " << (m_syncChunksDownloaded ? "true" : "false")
            << ", all linked notebooks listed = "
            << (m_allLinkedNotebooksListed ? "true" : "false")
            << ", linked notebook sync chunks downloaded = "
            << (m_linkedNotebooksSyncChunksDownloaded ? "true" : "false"));

    if (m_syncChunksDownloaded && !syncingLinkedNotebooksContent()) {
        qint32 smallestUsn = findSmallestUsnOfNonSyncedItems();
        if (smallestUsn > 0) {
            QNDEBUG(
                "synchronization:remote_to_local",
                "Found the smallest USN "
                    << "of non-processed items within the user's own account: "
                    << smallestUsn);
            // NOTE: decrement this USN because that would give the USN
            // *after which* the next sync should start
            usn = smallestUsn - 1;
        }
    }

    if (m_allLinkedNotebooksListed && m_linkedNotebooksSyncChunksDownloaded) {
        for (const auto & linkedNotebook: qAsConst(m_allLinkedNotebooks)) {
            if (Q_UNLIKELY(!linkedNotebook.hasGuid())) {
                QNWARNING(
                    "synchronization:remote_to_local",
                    "Detected "
                        << "a linked notebook without guid: "
                        << linkedNotebook);
                continue;
            }

            qint32 smallestUsn =
                findSmallestUsnOfNonSyncedItems(linkedNotebook.guid());

            if (smallestUsn >= 0) {
                QNDEBUG(
                    "synchronization:remote_to_local",
                    "Found the smallest "
                        << "USN of non-processed items within linked notebook "
                           "with "
                        << "guid " << linkedNotebook.guid() << ": "
                        << smallestUsn);

                usnByLinkedNotebookGuid[linkedNotebook.guid()] =
                    (smallestUsn - 1);
                continue;
            }
        }
    }
}

void RemoteToLocalSynchronizationManager::onGetNoteAsyncFinished(
    qint32 errorCode, qevercloud::Note qecNote, qint32 rateLimitSeconds,
    ErrorString errorDescription)
{
    if (Q_UNLIKELY(!qecNote.guid.isSet())) {
        errorDescription.setBase(
            QT_TR_NOOP("Internal error: just downloaded note has no guid"));

        QNWARNING(
            "synchronization:remote_to_local",
            errorDescription << ", note: " << qecNote);
        Q_EMIT failure(errorDescription);
        return;
    }

    QString noteGuid = qecNote.guid.ref();

    auto addIt = m_notesPendingDownloadForAddingToLocalStorage.find(noteGuid);

    auto updateIt =
        ((addIt == m_notesPendingDownloadForAddingToLocalStorage.end())
             ? m_notesPendingDownloadForUpdatingInLocalStorageByGuid.find(
                   noteGuid)
             : m_notesPendingDownloadForUpdatingInLocalStorageByGuid.end());

    bool needToAddNote =
        (addIt != m_notesPendingDownloadForAddingToLocalStorage.end());

    bool needToUpdateNote =
        (updateIt !=
         m_notesPendingDownloadForUpdatingInLocalStorageByGuid.end());

    if (!needToAddNote && !needToUpdateNote) {
        // The download of this note was requested by someone else,
        // perhaps by one of NoteSyncConflictResolvers
        return;
    }

    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::onGetNoteAsyncFinished: "
            << "error code = " << errorCode << ", rate limit seconds = "
            << rateLimitSeconds << ", error description: " << errorDescription
            << ", note: " << qecNote);

    Note note;

    if (needToAddNote) {
        note = addIt.value();
        note.setDirty(false);
        note.setLocal(false);
        Q_UNUSED(m_notesPendingDownloadForAddingToLocalStorage.erase(addIt))
    }
    else if (needToUpdateNote) {
        note = updateIt.value();
        Q_UNUSED(m_notesPendingDownloadForUpdatingInLocalStorageByGuid.erase(
            updateIt))
    }

    if (errorCode ==
        static_cast<qint32>(qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED))
    {
        if (rateLimitSeconds < 0) {
            errorDescription.setBase(
                QT_TR_NOOP("QEverCloud or Evernote protocol error: caught "
                           "RATE_LIMIT_REACHED exception but the number of "
                           "seconds to wait is zero or negative"));
            errorDescription.details() = QString::number(rateLimitSeconds);
            Q_EMIT failure(errorDescription);
            return;
        }

        int timerId = startTimer(secondsToMilliseconds(rateLimitSeconds));
        if (Q_UNLIKELY(timerId == 0)) {
            errorDescription.setBase(
                QT_TR_NOOP("Failed to start a timer to postpone the Evernote "
                           "API call due to rate limit exceeding"));
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
    else if (
        errorCode ==
        static_cast<qint32>(qevercloud::EDAMErrorCode::AUTH_EXPIRED))
    {
        handleAuthExpiration();
        return;
    }
    else if (errorCode != 0) {
        Q_EMIT failure(errorDescription);
        return;
    }

    overrideLocalNoteWithRemoteNote(note, qecNote);

    /**
     * NOTE: thumbnails for notes are downloaded separately and their download
     * is optional; for the sake of better error tolerance the failure to
     * download thumbnails for particular notes should not be considered
     * the failure of the synchronization algorithm as a whole.
     *
     * For these reasons, even if the thumbnail downloading was set up for some
     * particular note, we don't wait for it to finish before adding the note
     * to local storage or updating the note in the local storage; if
     * the thumbnail is downloaded successfully, the note would be updated one
     * more time; otherwise, it just won't be updated
     */

    const Notebook * pNotebook = nullptr;

    /**
     * Since the downloaded note includes the whole content for each of their
     * resources, need to ensure this note's resources which might still be
     * present in the sync chunks are removed from there
     */
    removeNoteResourcesFromSyncChunks(note);

    if (shouldDownloadThumbnailsForNotes() && note.hasResources()) {
        QNDEBUG(
            "synchronization:remote_to_local",
            "The added or updated note "
                << "contains resources, need to download the thumbnails for "
                   "it");

        pNotebook = getNotebookPerNote(note);
        if (pNotebook) {
            bool res = setupNoteThumbnailDownloading(note, *pNotebook);
            if (Q_UNLIKELY(!res)) {
                QNDEBUG(
                    "synchronization:remote_to_local",
                    "Wasn't able to set "
                        << "up the note thumbnail downloading");
            }
        }
        else {
            bool res = findNotebookForNoteThumbnailDownloading(note);
            if (Q_UNLIKELY(!res)) {
                QNDEBUG(
                    "synchronization:remote_to_local",
                    "Wasn't able to set "
                        << "up the search for the notebook of the note for "
                           "which "
                        << "the thumbnail was meant to be downloaded");
            }
        }
    }

    /**
     * NOTE: ink note images are also downloaded separately per each
     * corresponding note's resource and, furthermore, the ink note images are
     * not a part of the integral note data type. For these reasons and for
     * better error tolerance the failure to download any ink note image is not
     * considered a failure of the synchronization procedure
     */

    if (shouldDownloadInkNoteImages() && note.hasResources() &&
        note.isInkNote()) {
        QNDEBUG(
            "synchronization:remote_to_local",
            "The added or updated note "
                << "is the ink note, need to download the ink note image for "
                   "it");

        if (!pNotebook) {
            pNotebook = getNotebookPerNote(note);
        }

        if (pNotebook) {
            bool res = setupInkNoteImageDownloadingForNote(note, *pNotebook);
            if (Q_UNLIKELY(!res)) {
                QNDEBUG(
                    "synchronization:remote_to_local",
                    "Wasn't able to set "
                        << "up the ink note images downloading");
            }
        }
        else {
            bool res = findNotebookForInkNoteImageDownloading(note);
            if (Q_UNLIKELY(!res)) {
                QNDEBUG(
                    "synchronization:remote_to_local",
                    "Wasn't able to set "
                        << "up the search for the notebook of the note for "
                           "which "
                        << "the ink note images were meant to be downloaded");
            }
        }
    }

    checkAndIncrementNoteDownloadProgress(noteGuid);

    if (needToAddNote) {
        emitAddRequest(note);
        return;
    }

    QUuid updateNoteRequestId = QUuid::createUuid();
    Q_UNUSED(m_updateNoteRequestIds.insert(updateNoteRequestId))

    LocalStorageManager::UpdateNoteOptions options(
        LocalStorageManager::UpdateNoteOption::UpdateResourceMetadata |
        LocalStorageManager::UpdateNoteOption::UpdateResourceBinaryData |
        LocalStorageManager::UpdateNoteOption::UpdateTags);

    QNTRACE(
        "synchronization:remote_to_local",
        "Emitting the request to update "
            << "note in local storage: request id = " << updateNoteRequestId
            << ", note; " << note);
    Q_EMIT updateNote(note, options, updateNoteRequestId);
}

void RemoteToLocalSynchronizationManager::onGetResourceAsyncFinished(
    qint32 errorCode, qevercloud::Resource qecResource, qint32 rateLimitSeconds,
    ErrorString errorDescription)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::onGetResourceAsyncFinished: "
            << "error code = " << errorCode << ", rate limit seconds = "
            << rateLimitSeconds << ", error description: " << errorDescription
            << ", resource: " << qecResource);

    if (Q_UNLIKELY(!qecResource.guid.isSet())) {
        errorDescription.setBase(
            QT_TR_NOOP("Internal error: just downloaded resource has no guid"));

        QNWARNING(
            "synchronization:remote_to_local",
            errorDescription << ", resource: " << qecResource);
        Q_EMIT failure(errorDescription);
        return;
    }

    QString resourceGuid = qecResource.guid.ref();

    auto addIt =
        m_resourcesPendingDownloadForAddingToLocalStorageWithNotesByResourceGuid
            .find(resourceGuid);

    auto updateIt =
        ((addIt ==
          m_resourcesPendingDownloadForAddingToLocalStorageWithNotesByResourceGuid
              .end())
             ? m_resourcesPendingDownloadForUpdatingInLocalStorageWithNotesByResourceGuid
                   .find(resourceGuid)
             : m_resourcesPendingDownloadForUpdatingInLocalStorageWithNotesByResourceGuid
                   .end());

    bool needToAddResource =
        (addIt !=
         m_resourcesPendingDownloadForAddingToLocalStorageWithNotesByResourceGuid
             .end());

    bool needToUpdateResource =
        (updateIt !=
         m_resourcesPendingDownloadForUpdatingInLocalStorageWithNotesByResourceGuid
             .end());

    Resource resource;
    Note note;

    if (needToAddResource) {
        resource = addIt.value().first;
        note = addIt.value().second;
        Q_UNUSED(
            m_resourcesPendingDownloadForAddingToLocalStorageWithNotesByResourceGuid
                .erase(addIt))
    }
    else if (needToUpdateResource) {
        resource = updateIt.value().first;
        note = updateIt.value().second;
        Q_UNUSED(
            m_resourcesPendingDownloadForUpdatingInLocalStorageWithNotesByResourceGuid
                .erase(updateIt))
    }

    if (Q_UNLIKELY(!needToAddResource && !needToUpdateResource)) {
        errorDescription.setBase(
            QT_TR_NOOP("Internal error: the downloaded resource was not "
                       "expected"));

        QNWARNING(
            "synchronization:remote_to_local",
            errorDescription << ", resource: " << resource);
        Q_EMIT failure(errorDescription);
        return;
    }

    if (errorCode ==
        static_cast<qint32>(qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED))
    {
        if (rateLimitSeconds < 0) {
            errorDescription.setBase(
                QT_TR_NOOP("QEverCloud or Evernote protocol error: caught "
                           "RATE_LIMIT_REACHED exception but the number of "
                           "seconds to wait is zero or negative"));
            errorDescription.details() = QString::number(rateLimitSeconds);
            Q_EMIT failure(errorDescription);
            return;
        }

        int timerId = startTimer(secondsToMilliseconds(rateLimitSeconds));
        if (Q_UNLIKELY(timerId == 0)) {
            errorDescription.setBase(
                QT_TR_NOOP("Failed to start a timer to postpone the Evernote "
                           "API call due to rate limit exceeding"));
            Q_EMIT failure(errorDescription);
            return;
        }

        if (needToAddResource) {
            m_resourcesToAddWithNotesPerAPICallPostponeTimerId[timerId] =
                std::make_pair(resource, note);
        }
        else if (needToUpdateResource) {
            m_resourcesToUpdateWithNotesPerAPICallPostponeTimerId[timerId] =
                std::make_pair(resource, note);
        }

        Q_EMIT rateLimitExceeded(rateLimitSeconds);
        return;
    }
    else if (
        errorCode ==
        static_cast<qint32>(qevercloud::EDAMErrorCode::AUTH_EXPIRED))
    {
        handleAuthExpiration();
        return;
    }
    else if (errorCode != 0) {
        Q_EMIT failure(errorDescription);
        return;
    }

    resource.qevercloudResource() = qecResource;
    resource.setDirty(false);

    checkAndIncrementResourceDownloadProgress(resourceGuid);

    if (needToAddResource) {
        QString resourceGuid =
            (resource.hasGuid() ? resource.guid() : QString());

        QString resourceLocalUid = resource.localUid();
        auto key = std::make_pair(resourceGuid, resourceLocalUid);

        QUuid addResourceRequestId = QUuid::createUuid();
        Q_UNUSED(m_addResourceRequestIds.insert(addResourceRequestId))

        QNTRACE(
            "synchronization:remote_to_local",
            "Emitting the request to "
                << "add resource to the local storage: request id = "
                << addResourceRequestId << ", resource: " << resource);
        Q_EMIT addResource(resource, addResourceRequestId);
    }
    else {
        QUuid updateResourceRequestId = QUuid::createUuid();
        Q_UNUSED(m_updateResourceRequestIds.insert(updateResourceRequestId))

        QNTRACE(
            "synchronization:remote_to_local",
            "Emitting the request to "
                << "update resource: request id = " << updateResourceRequestId
                << ", resource: " << resource);
        Q_EMIT updateResource(resource, updateResourceRequestId);
    }

    note.setDirty(true);
    QUuid markNoteDirtyRequestId = QUuid::createUuid();

    m_resourcesByMarkNoteOwningResourceDirtyRequestIds[markNoteDirtyRequestId] =
        resource;

    QNTRACE(
        "synchronization:remote_to_local",
        "Emitting the request to mark "
            << "the resource owning note as the dirty one: request id = "
            << markNoteDirtyRequestId << ", resource: " << resource
            << "\nNote: " << note);

    Q_EMIT updateNote(
        note,
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
        LocalStorageManager::UpdateNoteOptions(),
#else
        LocalStorageManager::UpdateNoteOptions(0),
#endif
        markNoteDirtyRequestId);
}

void RemoteToLocalSynchronizationManager::onTagSyncCacheFilled()
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::onTagSyncCacheFilled");

    auto * pTagSyncCache = qobject_cast<TagSyncCache *>(sender());
    if (Q_UNLIKELY(!pTagSyncCache)) {
        ErrorString errorDescription(
            QT_TR_NOOP("Internal error: can't cast "
                       "the slot invoker to TagSyncCache"));
        QNWARNING("synchronization:remote_to_local", errorDescription);
        Q_EMIT failure(errorDescription);
        return;
    }

    const QString & linkedNotebookGuid = pTagSyncCache->linkedNotebookGuid();
    auto it =
        m_linkedNotebookGuidsPendingTagSyncCachesFill.find(linkedNotebookGuid);
    if (Q_UNLIKELY(it == m_linkedNotebookGuidsPendingTagSyncCachesFill.end())) {
        ErrorString errorDescription(
            QT_TR_NOOP("Received TagSyncCache fill event "
                       "for unexpected linked notebook guid"));
        errorDescription.details() = linkedNotebookGuid;
        QNWARNING("synchronization:remote_to_local", errorDescription);
        Q_EMIT failure(errorDescription);
        return;
    }

    checkAndRemoveInaccessibleParentTagGuidsForTagsFromLinkedNotebook(
        linkedNotebookGuid, *pTagSyncCache);

    Q_UNUSED(m_linkedNotebookGuidsPendingTagSyncCachesFill.erase(it))
    if (m_linkedNotebookGuidsPendingTagSyncCachesFill.isEmpty()) {
        QNDEBUG(
            "synchronization:remote_to_local",
            "No more linked notebook "
                << "guids pending tag sync caches fill");
        startFeedingDownloadedTagsToLocalStorageOneByOne(m_tags);
    }
    else {
        QNDEBUG(
            "synchronization:remote_to_local",
            "Still have "
                << m_linkedNotebookGuidsPendingTagSyncCachesFill.size()
                << " linked notebook guids pending tag sync caches fill");
    }
}

void RemoteToLocalSynchronizationManager::onTagSyncCacheFailure(
    ErrorString errorDescription)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::onTagSyncCacheFailure: "
            << errorDescription);

    Q_EMIT failure(errorDescription);
}

void RemoteToLocalSynchronizationManager::
    onNotebookSyncConflictResolverFinished(qevercloud::Notebook remoteNotebook)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager"
            << "::onNotebookSyncConflictResolverFinished: " << remoteNotebook);

    auto * pResolver = qobject_cast<NotebookSyncConflictResolver *>(sender());
    if (pResolver) {
        pResolver->disconnect(this);
        pResolver->setParent(nullptr);
        pResolver->deleteLater();
    }

    unregisterNotebookPendingAddOrUpdate(Notebook(remoteNotebook));

    checkNotebooksAndTagsSyncCompletionAndLaunchNotesAndResourcesSync();
    checkServerDataMergeCompletion();
}

void RemoteToLocalSynchronizationManager::onNotebookSyncConflictResolverFailure(
    qevercloud::Notebook remoteNotebook, ErrorString errorDescription)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager"
            << "::onNotebookSyncConflictResolverFailure: error description = "
            << errorDescription << ", remote notebook: " << remoteNotebook);

    auto * pResolver = qobject_cast<NotebookSyncConflictResolver *>(sender());
    if (pResolver) {
        pResolver->disconnect(this);
        pResolver->setParent(nullptr);
        pResolver->deleteLater();
    }

    Q_EMIT failure(errorDescription);
}

void RemoteToLocalSynchronizationManager::onTagSyncConflictResolverFinished(
    qevercloud::Tag remoteTag)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager"
            << "::onTagSyncConflictResolverFinished: " << remoteTag);

    auto * pResolver = qobject_cast<TagSyncConflictResolver *>(sender());
    if (pResolver) {
        pResolver->disconnect(this);
        pResolver->setParent(nullptr);
        pResolver->deleteLater();
    }

    unregisterTagPendingAddOrUpdate(Tag(remoteTag));
    syncNextTagPendingProcessing();
    checkNotebooksAndTagsSyncCompletionAndLaunchNotesAndResourcesSync();
    checkServerDataMergeCompletion();
}

void RemoteToLocalSynchronizationManager::onTagSyncConflictResolverFailure(
    qevercloud::Tag remoteTag, ErrorString errorDescription)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::onTagSyncConflictResolverFailure:"
        " "
            << "error description = " << errorDescription
            << ", remote tag: " << remoteTag);

    auto * pResolver = qobject_cast<TagSyncConflictResolver *>(sender());
    if (pResolver) {
        pResolver->disconnect(this);
        pResolver->setParent(nullptr);
        pResolver->deleteLater();
    }

    Q_EMIT failure(errorDescription);
}

void RemoteToLocalSynchronizationManager::
    onSavedSearchSyncConflictResolverFinished(
        qevercloud::SavedSearch remoteSavedSearch)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager"
            << "::onSavedSearchSyncConflictResolverFinished: "
            << remoteSavedSearch);

    auto * pResolver =
        qobject_cast<SavedSearchSyncConflictResolver *>(sender());
    if (pResolver) {
        pResolver->disconnect(this);
        pResolver->setParent(nullptr);
        pResolver->deleteLater();
    }

    unregisterSavedSearchPendingAddOrUpdate(SavedSearch(remoteSavedSearch));
    checkServerDataMergeCompletion();
}

void RemoteToLocalSynchronizationManager::
    onSavedSearchSyncConflictResolverFailure(
        qevercloud::SavedSearch remoteSavedSearch, ErrorString errorDescription)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager"
            << "::onSavedSearchSyncConflictResolverFailure: "
            << "error description = " << errorDescription
            << ", remote saved search: " << remoteSavedSearch);

    auto * pResolver =
        qobject_cast<SavedSearchSyncConflictResolver *>(sender());
    if (pResolver) {
        pResolver->disconnect(this);
        pResolver->setParent(nullptr);
        pResolver->deleteLater();
    }

    Q_EMIT failure(errorDescription);
}

void RemoteToLocalSynchronizationManager::onNoteSyncConflictResolverFinished(
    qevercloud::Note note)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager"
            << "::onNoteSyncConflictResolverFinished: note guid = "
            << (note.guid.isSet() ? note.guid.ref()
                                  : QStringLiteral("<not set>")));

    auto * pResolver = qobject_cast<NoteSyncConflictResolver *>(sender());
    if (pResolver) {
        pResolver->disconnect(this);
        pResolver->setParent(nullptr);
        pResolver->deleteLater();
    }

    unregisterNotePendingAddOrUpdate(note);
    checkNotesSyncCompletionAndLaunchResourcesSync();
    checkServerDataMergeCompletion();
}

void RemoteToLocalSynchronizationManager::onNoteSyncConflictResolvedFailure(
    qevercloud::Note note, ErrorString errorDescription)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager"
            << "::onNoteSyncConflictResolvedFailure: note guid = "
            << (note.guid.isSet() ? note.guid.ref()
                                  : QStringLiteral("<not set>"))
            << ", error description = " << errorDescription);

    auto * pResolver = qobject_cast<NoteSyncConflictResolver *>(sender());
    if (pResolver) {
        pResolver->disconnect(this);
        pResolver->setParent(nullptr);
        pResolver->deleteLater();
    }

    Q_EMIT failure(errorDescription);
}

void RemoteToLocalSynchronizationManager::onNoteSyncConflictRateLimitExceeded(
    qint32 rateLimitSeconds)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager"
            << "::onNoteSyncConflictRateLimitExceeded: rate limit seconds = "
            << rateLimitSeconds);

    Q_EMIT rateLimitExceeded(rateLimitSeconds);
}

void RemoteToLocalSynchronizationManager::
    onNoteSyncConflictAuthenticationExpired()
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager"
            << "::onNoteSyncConflictAuthenticationExpired");

    auto * pResolver = qobject_cast<NoteSyncConflictResolver *>(sender());
    if (pResolver) {
        if (syncingLinkedNotebooksContent()) {
            QObject::connect(
                this,
                &RemoteToLocalSynchronizationManager::
                    linkedNotebookAuthDataUpdated,
                pResolver,
                &NoteSyncConflictResolver::onLinkedNotebooksAuthDataUpdated,
                Qt::ConnectionType(
                    Qt::UniqueConnection | Qt::QueuedConnection));
        }
        else {
            QObject::connect(
                this, &RemoteToLocalSynchronizationManager::authDataUpdated,
                pResolver, &NoteSyncConflictResolver::onAuthDataUpdated,
                Qt::ConnectionType(
                    Qt::UniqueConnection | Qt::QueuedConnection));
        }
    }

    handleAuthExpiration();
}

void RemoteToLocalSynchronizationManager::
    onFullSyncStaleDataItemsExpungerFinished()
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager"
            << "::onFullSyncStaleDataItemsExpungerFinished");

    QString linkedNotebookGuid;

    auto * pExpunger = qobject_cast<FullSyncStaleDataItemsExpunger *>(sender());
    if (pExpunger) {
        linkedNotebookGuid = pExpunger->linkedNotebookGuid();

        if (m_pFullSyncStaleDataItemsExpunger == pExpunger) {
            m_pFullSyncStaleDataItemsExpunger = nullptr;
        }
        else {
            auto it =
                m_fullSyncStaleDataItemsExpungersByLinkedNotebookGuid.find(
                    linkedNotebookGuid);

            if (it !=
                m_fullSyncStaleDataItemsExpungersByLinkedNotebookGuid.end()) {
                Q_UNUSED(
                    m_fullSyncStaleDataItemsExpungersByLinkedNotebookGuid.erase(
                        it))
            }
        }

        junkFullSyncStaleDataItemsExpunger(*pExpunger);
    }

    if (linkedNotebookGuid.isEmpty()) {
        QNDEBUG(
            "synchronization:remote_to_local",
            "Finished analyzing and "
                << "expunging stuff from user's own account after the "
                   "non-first "
                << "full sync");

        m_expungedFromServerToClient = true;
        startLinkedNotebooksSync();
    }
    else {
        if (!m_fullSyncStaleDataItemsExpungersByLinkedNotebookGuid.isEmpty()) {
            QNDEBUG(
                "synchronization:remote_to_local",
                "Still pending "
                    << m_fullSyncStaleDataItemsExpungersByLinkedNotebookGuid
                           .size()
                    << " FullSyncStaleDataItemsExpungers for linked notebooks");
            return;
        }

        QNDEBUG(
            "synchronization:remote_to_local",
            "All FullSyncStaleDataItemsExpungers for linked notebooks are "
                << "finished");

        launchExpungingOfNotelessTagsFromLinkedNotebooks();
    }
}

void RemoteToLocalSynchronizationManager::
    onFullSyncStaleDataItemsExpungerFailure(ErrorString errorDescription)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager"
            << "::onFullSyncStaleDataItemsExpungerFailure: "
            << errorDescription);

    QString linkedNotebookGuid;

    auto * pExpunger = qobject_cast<FullSyncStaleDataItemsExpunger *>(sender());
    if (pExpunger) {
        linkedNotebookGuid = pExpunger->linkedNotebookGuid();

        if (m_pFullSyncStaleDataItemsExpunger == pExpunger) {
            m_pFullSyncStaleDataItemsExpunger = nullptr;
        }
        else {
            auto it =
                m_fullSyncStaleDataItemsExpungersByLinkedNotebookGuid.find(
                    linkedNotebookGuid);
            if (it !=
                m_fullSyncStaleDataItemsExpungersByLinkedNotebookGuid.end()) {
                Q_UNUSED(
                    m_fullSyncStaleDataItemsExpungersByLinkedNotebookGuid.erase(
                        it))
            }
        }

        junkFullSyncStaleDataItemsExpunger(*pExpunger);
    }

    QNWARNING(
        "synchronization:remote_to_local",
        "Failed to analyze and "
            << "expunge stale stuff after the non-first full sync: "
            << errorDescription
            << "; linked notebook guid = " << linkedNotebookGuid);

    Q_EMIT failure(errorDescription);
}

void RemoteToLocalSynchronizationManager::connectToLocalStorage()
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::connectToLocalStorage");

    if (m_connectedToLocalStorage) {
        QNDEBUG(
            "synchronization:remote_to_local",
            "Already connected to "
                << "the local storage");
        return;
    }

    auto & localStorageManagerAsync = m_manager.localStorageManagerAsync();

    // Connect local signals with localStorageManagerAsync's slots
    QObject::connect(
        this, &RemoteToLocalSynchronizationManager::addUser,
        &localStorageManagerAsync, &LocalStorageManagerAsync::onAddUserRequest,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        this, &RemoteToLocalSynchronizationManager::updateUser,
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::onUpdateUserRequest,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        this, &RemoteToLocalSynchronizationManager::findUser,
        &localStorageManagerAsync, &LocalStorageManagerAsync::onFindUserRequest,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        this, &RemoteToLocalSynchronizationManager::addNotebook,
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::onAddNotebookRequest,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        this, &RemoteToLocalSynchronizationManager::updateNotebook,
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::onUpdateNotebookRequest,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        this, &RemoteToLocalSynchronizationManager::findNotebook,
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::onFindNotebookRequest,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        this, &RemoteToLocalSynchronizationManager::expungeNotebook,
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::onExpungeNotebookRequest,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        this, &RemoteToLocalSynchronizationManager::addNote,
        &localStorageManagerAsync, &LocalStorageManagerAsync::onAddNoteRequest,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        this, &RemoteToLocalSynchronizationManager::updateNote,
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::onUpdateNoteRequest,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        this, &RemoteToLocalSynchronizationManager::findNote,
        &localStorageManagerAsync, &LocalStorageManagerAsync::onFindNoteRequest,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        this, &RemoteToLocalSynchronizationManager::expungeNote,
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::onExpungeNoteRequest,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        this, &RemoteToLocalSynchronizationManager::addTag,
        &localStorageManagerAsync, &LocalStorageManagerAsync::onAddTagRequest,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        this, &RemoteToLocalSynchronizationManager::updateTag,
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::onUpdateTagRequest,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        this, &RemoteToLocalSynchronizationManager::findTag,
        &localStorageManagerAsync, &LocalStorageManagerAsync::onFindTagRequest,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        this, &RemoteToLocalSynchronizationManager::expungeTag,
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::onExpungeTagRequest,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        this,
        &RemoteToLocalSynchronizationManager::
            expungeNotelessTagsFromLinkedNotebooks,
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::
            onExpungeNotelessTagsFromLinkedNotebooksRequest,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        this, &RemoteToLocalSynchronizationManager::addResource,
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::onAddResourceRequest,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        this, &RemoteToLocalSynchronizationManager::updateResource,
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::onUpdateResourceRequest,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        this, &RemoteToLocalSynchronizationManager::findResource,
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::onFindResourceRequest,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        this, &RemoteToLocalSynchronizationManager::addLinkedNotebook,
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::onAddLinkedNotebookRequest,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        this, &RemoteToLocalSynchronizationManager::updateLinkedNotebook,
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::onUpdateLinkedNotebookRequest,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        this, &RemoteToLocalSynchronizationManager::findLinkedNotebook,
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::onFindLinkedNotebookRequest,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        this, &RemoteToLocalSynchronizationManager::expungeLinkedNotebook,
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::onExpungeLinkedNotebookRequest,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        this, &RemoteToLocalSynchronizationManager::listAllLinkedNotebooks,
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::onListAllLinkedNotebooksRequest,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        this, &RemoteToLocalSynchronizationManager::addSavedSearch,
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::onAddSavedSearchRequest,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        this, &RemoteToLocalSynchronizationManager::updateSavedSearch,
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::onUpdateSavedSearchRequest,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        this, &RemoteToLocalSynchronizationManager::findSavedSearch,
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::onFindSavedSearchRequest,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        this, &RemoteToLocalSynchronizationManager::expungeSavedSearch,
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::onExpungeSavedSearchRequest,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    // Connect localStorageManagerAsync's signals to local slots
    QObject::connect(
        &localStorageManagerAsync, &LocalStorageManagerAsync::findUserComplete,
        this, &RemoteToLocalSynchronizationManager::onFindUserCompleted,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &localStorageManagerAsync, &LocalStorageManagerAsync::findUserFailed,
        this, &RemoteToLocalSynchronizationManager::onFindUserFailed,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::findNotebookComplete, this,
        &RemoteToLocalSynchronizationManager::onFindNotebookCompleted,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::findNotebookFailed, this,
        &RemoteToLocalSynchronizationManager::onFindNotebookFailed,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &localStorageManagerAsync, &LocalStorageManagerAsync::findNoteComplete,
        this, &RemoteToLocalSynchronizationManager::onFindNoteCompleted,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &localStorageManagerAsync, &LocalStorageManagerAsync::findNoteFailed,
        this, &RemoteToLocalSynchronizationManager::onFindNoteFailed,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &localStorageManagerAsync, &LocalStorageManagerAsync::findTagComplete,
        this, &RemoteToLocalSynchronizationManager::onFindTagCompleted,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &localStorageManagerAsync, &LocalStorageManagerAsync::findTagFailed,
        this, &RemoteToLocalSynchronizationManager::onFindTagFailed,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::findLinkedNotebookComplete, this,
        &RemoteToLocalSynchronizationManager::onFindLinkedNotebookCompleted,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::findLinkedNotebookFailed, this,
        &RemoteToLocalSynchronizationManager::onFindLinkedNotebookFailed,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::findSavedSearchComplete, this,
        &RemoteToLocalSynchronizationManager::onFindSavedSearchCompleted,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::findSavedSearchFailed, this,
        &RemoteToLocalSynchronizationManager::onFindSavedSearchFailed,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::findResourceComplete, this,
        &RemoteToLocalSynchronizationManager::onFindResourceCompleted,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::findResourceFailed, this,
        &RemoteToLocalSynchronizationManager::onFindResourceFailed,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &localStorageManagerAsync, &LocalStorageManagerAsync::addTagComplete,
        this, &RemoteToLocalSynchronizationManager::onAddTagCompleted,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &localStorageManagerAsync, &LocalStorageManagerAsync::addTagFailed,
        this, &RemoteToLocalSynchronizationManager::onAddTagFailed,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &localStorageManagerAsync, &LocalStorageManagerAsync::updateTagComplete,
        this, &RemoteToLocalSynchronizationManager::onUpdateTagCompleted,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &localStorageManagerAsync, &LocalStorageManagerAsync::updateTagFailed,
        this, &RemoteToLocalSynchronizationManager::onUpdateTagFailed,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::expungeTagComplete, this,
        &RemoteToLocalSynchronizationManager::onExpungeTagCompleted,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &localStorageManagerAsync, &LocalStorageManagerAsync::expungeTagFailed,
        this, &RemoteToLocalSynchronizationManager::onExpungeTagFailed,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::
            expungeNotelessTagsFromLinkedNotebooksComplete,
        this,
        &RemoteToLocalSynchronizationManager::
            onExpungeNotelessTagsFromLinkedNotebooksCompleted,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::expungeNotelessTagsFromLinkedNotebooksFailed,
        this,
        &RemoteToLocalSynchronizationManager::
            onExpungeNotelessTagsFromLinkedNotebooksFailed,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &localStorageManagerAsync, &LocalStorageManagerAsync::addUserComplete,
        this, &RemoteToLocalSynchronizationManager::onAddUserCompleted,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &localStorageManagerAsync, &LocalStorageManagerAsync::addUserFailed,
        this, &RemoteToLocalSynchronizationManager::onAddUserFailed,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::updateUserComplete, this,
        &RemoteToLocalSynchronizationManager::onUpdateUserCompleted,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &localStorageManagerAsync, &LocalStorageManagerAsync::updateUserFailed,
        this, &RemoteToLocalSynchronizationManager::onUpdateUserFailed,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::addSavedSearchComplete, this,
        &RemoteToLocalSynchronizationManager::onAddSavedSearchCompleted,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::addSavedSearchFailed, this,
        &RemoteToLocalSynchronizationManager::onAddSavedSearchFailed,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::updateSavedSearchComplete, this,
        &RemoteToLocalSynchronizationManager::onUpdateSavedSearchCompleted,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::updateSavedSearchFailed, this,
        &RemoteToLocalSynchronizationManager::onUpdateSavedSearchFailed,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::expungeSavedSearchComplete, this,
        &RemoteToLocalSynchronizationManager::onExpungeSavedSearchCompleted,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::expungeSavedSearchFailed, this,
        &RemoteToLocalSynchronizationManager::onExpungeSavedSearchFailed,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::addLinkedNotebookComplete, this,
        &RemoteToLocalSynchronizationManager::onAddLinkedNotebookCompleted,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::addLinkedNotebookFailed, this,
        &RemoteToLocalSynchronizationManager::onAddLinkedNotebookFailed,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::updateLinkedNotebookComplete, this,
        &RemoteToLocalSynchronizationManager::onUpdateLinkedNotebookCompleted,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::updateLinkedNotebookFailed, this,
        &RemoteToLocalSynchronizationManager::onUpdateLinkedNotebookFailed,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::expungeLinkedNotebookComplete, this,
        &RemoteToLocalSynchronizationManager::onExpungeLinkedNotebookCompleted,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::expungeLinkedNotebookFailed, this,
        &RemoteToLocalSynchronizationManager::onExpungeLinkedNotebookFailed,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::listAllLinkedNotebooksComplete, this,
        &RemoteToLocalSynchronizationManager::onListAllLinkedNotebooksCompleted,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::listAllLinkedNotebooksFailed, this,
        &RemoteToLocalSynchronizationManager::onListAllLinkedNotebooksFailed,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::addNotebookComplete, this,
        &RemoteToLocalSynchronizationManager::onAddNotebookCompleted,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &localStorageManagerAsync, &LocalStorageManagerAsync::addNotebookFailed,
        this, &RemoteToLocalSynchronizationManager::onAddNotebookFailed,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::updateNotebookComplete, this,
        &RemoteToLocalSynchronizationManager::onUpdateNotebookCompleted,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::updateNotebookFailed, this,
        &RemoteToLocalSynchronizationManager::onUpdateNotebookFailed,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::expungeNotebookComplete, this,
        &RemoteToLocalSynchronizationManager::onExpungeNotebookCompleted,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::expungeNotebookFailed, this,
        &RemoteToLocalSynchronizationManager::onExpungeNotebookFailed,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &localStorageManagerAsync, &LocalStorageManagerAsync::addNoteComplete,
        this, &RemoteToLocalSynchronizationManager::onAddNoteCompleted,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &localStorageManagerAsync, &LocalStorageManagerAsync::addNoteFailed,
        this, &RemoteToLocalSynchronizationManager::onAddNoteFailed,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::updateNoteComplete, this,
        &RemoteToLocalSynchronizationManager::onUpdateNoteCompleted,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &localStorageManagerAsync, &LocalStorageManagerAsync::updateNoteFailed,
        this, &RemoteToLocalSynchronizationManager::onUpdateNoteFailed,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::expungeNoteComplete, this,
        &RemoteToLocalSynchronizationManager::onExpungeNoteCompleted,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &localStorageManagerAsync, &LocalStorageManagerAsync::expungeNoteFailed,
        this, &RemoteToLocalSynchronizationManager::onExpungeNoteFailed,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::addResourceComplete, this,
        &RemoteToLocalSynchronizationManager::onAddResourceCompleted,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &localStorageManagerAsync, &LocalStorageManagerAsync::addResourceFailed,
        this, &RemoteToLocalSynchronizationManager::onAddResourceFailed,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::updateResourceComplete, this,
        &RemoteToLocalSynchronizationManager::onUpdateResourceCompleted,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::updateResourceFailed, this,
        &RemoteToLocalSynchronizationManager::onUpdateResourceFailed,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    m_connectedToLocalStorage = true;
}

void RemoteToLocalSynchronizationManager::disconnectFromLocalStorage()
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::disconnectFromLocalStorage");

    if (!m_connectedToLocalStorage) {
        QNDEBUG(
            "synchronization:remote_to_local",
            "Not connected to "
                << "the local storage at the moment");
        return;
    }

    auto & localStorageManagerAsync = m_manager.localStorageManagerAsync();

    // Disconnect local signals from localStorageManagerAsync's slots
    QObject::disconnect(
        this, &RemoteToLocalSynchronizationManager::addUser,
        &localStorageManagerAsync, &LocalStorageManagerAsync::onAddUserRequest);

    QObject::disconnect(
        this, &RemoteToLocalSynchronizationManager::updateUser,
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::onUpdateUserRequest);

    QObject::disconnect(
        this, &RemoteToLocalSynchronizationManager::findUser,
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::onFindUserRequest);

    QObject::disconnect(
        this, &RemoteToLocalSynchronizationManager::addNotebook,
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::onAddNotebookRequest);

    QObject::disconnect(
        this, &RemoteToLocalSynchronizationManager::updateNotebook,
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::onUpdateNotebookRequest);

    QObject::disconnect(
        this, &RemoteToLocalSynchronizationManager::findNotebook,
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::onFindNotebookRequest);

    QObject::disconnect(
        this, &RemoteToLocalSynchronizationManager::expungeNotebook,
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::onExpungeNotebookRequest);

    QObject::disconnect(
        this, &RemoteToLocalSynchronizationManager::addNote,
        &localStorageManagerAsync, &LocalStorageManagerAsync::onAddNoteRequest);

    QObject::disconnect(
        this, &RemoteToLocalSynchronizationManager::updateNote,
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::onUpdateNoteRequest);

    QObject::disconnect(
        this, &RemoteToLocalSynchronizationManager::findNote,
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::onFindNoteRequest);

    QObject::disconnect(
        this, &RemoteToLocalSynchronizationManager::expungeNote,
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::onExpungeNoteRequest);

    QObject::disconnect(
        this, &RemoteToLocalSynchronizationManager::addTag,
        &localStorageManagerAsync, &LocalStorageManagerAsync::onAddTagRequest);

    QObject::disconnect(
        this, &RemoteToLocalSynchronizationManager::updateTag,
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::onUpdateTagRequest);

    QObject::disconnect(
        this, &RemoteToLocalSynchronizationManager::findTag,
        &localStorageManagerAsync, &LocalStorageManagerAsync::onFindTagRequest);

    QObject::disconnect(
        this, &RemoteToLocalSynchronizationManager::expungeTag,
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::onExpungeTagRequest);

    QObject::disconnect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::
            expungeNotelessTagsFromLinkedNotebooksComplete,
        this,
        &RemoteToLocalSynchronizationManager::
            onExpungeNotelessTagsFromLinkedNotebooksCompleted);

    QObject::disconnect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::expungeNotelessTagsFromLinkedNotebooksFailed,
        this,
        &RemoteToLocalSynchronizationManager::
            onExpungeNotelessTagsFromLinkedNotebooksFailed);

    QObject::disconnect(
        this, &RemoteToLocalSynchronizationManager::addResource,
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::onAddResourceRequest);

    QObject::disconnect(
        this, &RemoteToLocalSynchronizationManager::updateResource,
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::onUpdateResourceRequest);

    QObject::disconnect(
        this, &RemoteToLocalSynchronizationManager::findResource,
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::onFindResourceRequest);

    QObject::disconnect(
        this, &RemoteToLocalSynchronizationManager::addLinkedNotebook,
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::onAddLinkedNotebookRequest);

    QObject::disconnect(
        this, &RemoteToLocalSynchronizationManager::updateLinkedNotebook,
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::onUpdateLinkedNotebookRequest);

    QObject::disconnect(
        this, &RemoteToLocalSynchronizationManager::findLinkedNotebook,
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::onFindLinkedNotebookRequest);

    QObject::disconnect(
        this, &RemoteToLocalSynchronizationManager::expungeLinkedNotebook,
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::onExpungeLinkedNotebookRequest);

    QObject::disconnect(
        this, &RemoteToLocalSynchronizationManager::listAllLinkedNotebooks,
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::onListAllLinkedNotebooksRequest);

    QObject::disconnect(
        this, &RemoteToLocalSynchronizationManager::addSavedSearch,
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::onAddSavedSearchRequest);

    QObject::disconnect(
        this, &RemoteToLocalSynchronizationManager::updateSavedSearch,
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::onUpdateSavedSearchRequest);

    QObject::disconnect(
        this, &RemoteToLocalSynchronizationManager::findSavedSearch,
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::onFindSavedSearchRequest);

    QObject::disconnect(
        this, &RemoteToLocalSynchronizationManager::expungeSavedSearch,
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::onExpungeSavedSearchRequest);

    // Disconnect localStorageManagerAsync's signals to local slots
    QObject::disconnect(
        &localStorageManagerAsync, &LocalStorageManagerAsync::findUserComplete,
        this, &RemoteToLocalSynchronizationManager::onFindUserCompleted);

    QObject::disconnect(
        &localStorageManagerAsync, &LocalStorageManagerAsync::findUserFailed,
        this, &RemoteToLocalSynchronizationManager::onFindUserFailed);

    QObject::disconnect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::findNotebookComplete, this,
        &RemoteToLocalSynchronizationManager::onFindNotebookCompleted);

    QObject::disconnect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::findNotebookFailed, this,
        &RemoteToLocalSynchronizationManager::onFindNotebookFailed);

    QObject::disconnect(
        &localStorageManagerAsync, &LocalStorageManagerAsync::findNoteComplete,
        this, &RemoteToLocalSynchronizationManager::onFindNoteCompleted);

    QObject::disconnect(
        &localStorageManagerAsync, &LocalStorageManagerAsync::findNoteFailed,
        this, &RemoteToLocalSynchronizationManager::onFindNoteFailed);

    QObject::disconnect(
        &localStorageManagerAsync, &LocalStorageManagerAsync::findTagComplete,
        this, &RemoteToLocalSynchronizationManager::onFindTagCompleted);

    QObject::disconnect(
        &localStorageManagerAsync, &LocalStorageManagerAsync::findTagFailed,
        this, &RemoteToLocalSynchronizationManager::onFindTagFailed);

    QObject::disconnect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::findLinkedNotebookComplete, this,
        &RemoteToLocalSynchronizationManager::onFindLinkedNotebookCompleted);

    QObject::disconnect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::findLinkedNotebookFailed, this,
        &RemoteToLocalSynchronizationManager::onFindLinkedNotebookFailed);

    QObject::disconnect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::findSavedSearchComplete, this,
        &RemoteToLocalSynchronizationManager::onFindSavedSearchCompleted);

    QObject::disconnect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::findSavedSearchFailed, this,
        &RemoteToLocalSynchronizationManager::onFindSavedSearchFailed);

    QObject::disconnect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::findResourceComplete, this,
        &RemoteToLocalSynchronizationManager::onFindResourceCompleted);

    QObject::disconnect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::findResourceFailed, this,
        &RemoteToLocalSynchronizationManager::onFindResourceFailed);

    QObject::disconnect(
        &localStorageManagerAsync, &LocalStorageManagerAsync::addTagComplete,
        this, &RemoteToLocalSynchronizationManager::onAddTagCompleted);

    QObject::disconnect(
        &localStorageManagerAsync, &LocalStorageManagerAsync::addTagFailed,
        this, &RemoteToLocalSynchronizationManager::onAddTagFailed);

    QObject::disconnect(
        &localStorageManagerAsync, &LocalStorageManagerAsync::updateTagComplete,
        this, &RemoteToLocalSynchronizationManager::onUpdateTagCompleted);

    QObject::disconnect(
        &localStorageManagerAsync, &LocalStorageManagerAsync::updateTagFailed,
        this, &RemoteToLocalSynchronizationManager::onUpdateTagFailed);

    QObject::disconnect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::expungeTagComplete, this,
        &RemoteToLocalSynchronizationManager::onExpungeTagCompleted);

    QObject::disconnect(
        &localStorageManagerAsync, &LocalStorageManagerAsync::expungeTagFailed,
        this, &RemoteToLocalSynchronizationManager::onExpungeTagFailed);

    QObject::disconnect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::addSavedSearchComplete, this,
        &RemoteToLocalSynchronizationManager::onAddSavedSearchCompleted);

    QObject::disconnect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::addSavedSearchFailed, this,
        &RemoteToLocalSynchronizationManager::onAddSavedSearchFailed);

    QObject::disconnect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::updateSavedSearchComplete, this,
        &RemoteToLocalSynchronizationManager::onUpdateSavedSearchCompleted);

    QObject::disconnect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::updateSavedSearchFailed, this,
        &RemoteToLocalSynchronizationManager::onUpdateSavedSearchFailed);

    QObject::disconnect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::expungeSavedSearchComplete, this,
        &RemoteToLocalSynchronizationManager::onExpungeSavedSearchCompleted);

    QObject::disconnect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::expungeSavedSearchFailed, this,
        &RemoteToLocalSynchronizationManager::onExpungeSavedSearchFailed);

    QObject::disconnect(
        &localStorageManagerAsync, &LocalStorageManagerAsync::addUserComplete,
        this, &RemoteToLocalSynchronizationManager::onAddUserCompleted);

    QObject::disconnect(
        &localStorageManagerAsync, &LocalStorageManagerAsync::addUserFailed,
        this, &RemoteToLocalSynchronizationManager::onAddUserFailed);

    QObject::disconnect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::updateUserComplete, this,
        &RemoteToLocalSynchronizationManager::onUpdateUserCompleted);

    QObject::disconnect(
        &localStorageManagerAsync, &LocalStorageManagerAsync::updateUserFailed,
        this, &RemoteToLocalSynchronizationManager::onUpdateUserFailed);

    QObject::disconnect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::addLinkedNotebookComplete, this,
        &RemoteToLocalSynchronizationManager::onAddLinkedNotebookCompleted);

    QObject::disconnect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::addLinkedNotebookFailed, this,
        &RemoteToLocalSynchronizationManager::onAddLinkedNotebookFailed);

    QObject::disconnect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::updateLinkedNotebookComplete, this,
        &RemoteToLocalSynchronizationManager::onUpdateLinkedNotebookCompleted);

    QObject::disconnect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::updateLinkedNotebookFailed, this,
        &RemoteToLocalSynchronizationManager::onUpdateLinkedNotebookFailed);

    QObject::disconnect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::expungeLinkedNotebookComplete, this,
        &RemoteToLocalSynchronizationManager::onExpungeLinkedNotebookCompleted);

    QObject::disconnect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::expungeLinkedNotebookFailed, this,
        &RemoteToLocalSynchronizationManager::onExpungeLinkedNotebookFailed);

    QObject::disconnect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::listAllLinkedNotebooksComplete, this,
        &RemoteToLocalSynchronizationManager::
            onListAllLinkedNotebooksCompleted);

    QObject::disconnect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::listAllLinkedNotebooksFailed, this,
        &RemoteToLocalSynchronizationManager::onListAllLinkedNotebooksFailed);

    QObject::disconnect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::addNotebookComplete, this,
        &RemoteToLocalSynchronizationManager::onAddNotebookCompleted);

    QObject::disconnect(
        &localStorageManagerAsync, &LocalStorageManagerAsync::addNotebookFailed,
        this, &RemoteToLocalSynchronizationManager::onAddNotebookFailed);

    QObject::disconnect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::updateNotebookComplete, this,
        &RemoteToLocalSynchronizationManager::onUpdateNotebookCompleted);

    QObject::disconnect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::updateNotebookFailed, this,
        &RemoteToLocalSynchronizationManager::onUpdateNotebookFailed);

    QObject::disconnect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::expungeNotebookComplete, this,
        &RemoteToLocalSynchronizationManager::onExpungeNotebookCompleted);

    QObject::disconnect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::expungeNotebookFailed, this,
        &RemoteToLocalSynchronizationManager::onExpungeNotebookFailed);

    QObject::disconnect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::updateNoteComplete, this,
        &RemoteToLocalSynchronizationManager::onUpdateNoteCompleted);

    QObject::disconnect(
        &localStorageManagerAsync, &LocalStorageManagerAsync::updateNoteFailed,
        this, &RemoteToLocalSynchronizationManager::onUpdateNoteFailed);

    QObject::disconnect(
        &localStorageManagerAsync, &LocalStorageManagerAsync::addNoteComplete,
        this, &RemoteToLocalSynchronizationManager::onAddNoteCompleted);

    QObject::disconnect(
        &localStorageManagerAsync, &LocalStorageManagerAsync::addNoteFailed,
        this, &RemoteToLocalSynchronizationManager::onAddNoteFailed);

    QObject::disconnect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::expungeNoteComplete, this,
        &RemoteToLocalSynchronizationManager::onExpungeNoteCompleted);

    QObject::disconnect(
        &localStorageManagerAsync, &LocalStorageManagerAsync::expungeNoteFailed,
        this, &RemoteToLocalSynchronizationManager::onExpungeNoteFailed);

    QObject::disconnect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::addResourceComplete, this,
        &RemoteToLocalSynchronizationManager::onAddResourceCompleted);

    QObject::disconnect(
        &localStorageManagerAsync, &LocalStorageManagerAsync::addResourceFailed,
        this, &RemoteToLocalSynchronizationManager::onAddResourceFailed);

    QObject::disconnect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::updateResourceComplete, this,
        &RemoteToLocalSynchronizationManager::onUpdateResourceCompleted);

    QObject::disconnect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::updateResourceFailed, this,
        &RemoteToLocalSynchronizationManager::onUpdateResourceFailed);

    m_connectedToLocalStorage = false;

    // With the disconnect from local storage the list of previously received
    // linked notebooks (if any) + new additions/updates becomes invalidated
    m_allLinkedNotebooksListed = false;
}

void RemoteToLocalSynchronizationManager::resetCurrentSyncState()
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::resetCurrentSyncState");

    m_lastUpdateCount = 0;
    m_lastSyncTime = 0;
    m_lastUpdateCountByLinkedNotebookGuid.clear();
    m_lastSyncTimeByLinkedNotebookGuid.clear();
    m_linkedNotebookGuidsForWhichFullSyncWasPerformed.clear();
    m_linkedNotebookGuidsOnceFullySynced.clear();

    m_gotLastSyncParameters = false;
}

QString RemoteToLocalSynchronizationManager::defaultInkNoteImageStoragePath()
    const
{
    return applicationPersistentStoragePath() +
        QStringLiteral("/inkNoteImages");
}

void RemoteToLocalSynchronizationManager::launchSync()
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::launchSync");

    if (m_authenticationToken.isEmpty()) {
        m_pendingAuthenticationTokenAndShardId = true;
        Q_EMIT requestAuthenticationToken();
        return;
    }

    if (m_onceSyncDone && (m_lastSyncMode == SyncMode::FullSync)) {
        QNDEBUG(
            "synchronization:remote_to_local",
            "Performing full sync even "
                << "though it has been performed at some moment in the past; "
                << "collecting synced guids for full sync stale data items "
                << "expunger");
        collectSyncedGuidsForFullSyncStaleDataItemsExpunger();
    }

    m_pendingTagsSyncStart = true;
    m_pendingLinkedNotebooksSyncStart = true;
    m_pendingNotebooksSyncStart = true;

    launchSavedSearchSync();
    launchLinkedNotebookSync();

    launchTagsSync();
    launchNotebookSync();

    if (!m_tags.empty() || !m_notebooks.isEmpty()) {
        // NOTE: the sync of notes and, if need be, individual resouces
        // would be launched asynchronously when the notebooks and tags
        // are synced
        return;
    }

    QNDEBUG(
        "synchronization:remote_to_local",
        "The local lists of tags and "
            << "notebooks waiting for adding/updating are empty, checking if "
               "there "
            << "are notes to process");

    launchNotesSync(ContentSource::UserAccount);
    if (!m_notes.isEmpty() || notesSyncInProgress()) {
        QNDEBUG("synchronization:remote_to_local", "Synchronizing notes");
        /**
         * NOTE: the sync of individual resources as well as expunging of
         * various data items will be launched asynchronously
         * if current sync is incremental after the notes are synced
         */
        return;
    }

    QNDEBUG(
        "synchronization:remote_to_local",
        "The local list of notes "
            << "waiting for adding/updating is empty");

    if (m_lastSyncMode != SyncMode::IncrementalSync) {
        QNDEBUG(
            "synchronization:remote_to_local",
            "Running full sync => no "
                << "sync for individual resources or expunging stuff is "
                   "needed");
        return;
    }

    if (!resourcesSyncInProgress()) {
        launchResourcesSync(ContentSource::UserAccount);

        if (!m_resources.isEmpty() || resourcesSyncInProgress()) {
            QNDEBUG(
                "synchronization:remote_to_local",
                "Resources sync in "
                    << "progress");
            return;
        }
    }

    /**
     * If there's nothing to sync for user's own account, check if something
     * needs to be expunged, if yes, do it, otherwirse launch the linked
     * notebooks sync
     */
    checkServerDataMergeCompletion();
}

bool RemoteToLocalSynchronizationManager::checkProtocolVersion(
    ErrorString & errorDescription)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::checkProtocolVersion");

    if (m_edamProtocolVersionChecked) {
        QNDEBUG(
            "synchronization:remote_to_local",
            "Already checked "
                << "the protocol version, skipping it");
        return true;
    }

    QString clientName = clientNameForProtocolVersionCheck();
    qint16 edamProtocolVersionMajor = qevercloud::EDAM_VERSION_MAJOR;
    qint16 edamProtocolVersionMinor = qevercloud::EDAM_VERSION_MINOR;

    bool protocolVersionChecked = m_manager.userStore().checkVersion(
        clientName, edamProtocolVersionMajor, edamProtocolVersionMinor,
        errorDescription);

    if (Q_UNLIKELY(!protocolVersionChecked)) {
        if (!errorDescription.isEmpty()) {
            ErrorString fullErrorDescription(
                QT_TR_NOOP("EDAM protocol version check failed"));

            fullErrorDescription.additionalBases().append(
                errorDescription.base());

            fullErrorDescription.additionalBases().append(
                errorDescription.additionalBases());

            fullErrorDescription.details() = errorDescription.details();
            errorDescription = fullErrorDescription;
        }
        else {
            errorDescription.setBase(
                QT_TR_NOOP("Evernote service reports the currently used "
                           "protocol version can no longer be used for "
                           "the communication with it"));

            errorDescription.details() =
                QString::number(edamProtocolVersionMajor);

            errorDescription.details() += QStringLiteral(".");

            errorDescription.details() +=
                QString::number(edamProtocolVersionMinor);
        }

        QNWARNING("synchronization:remote_to_local", errorDescription);
        return false;
    }

    m_edamProtocolVersionChecked = true;

    QNDEBUG(
        "synchronization:remote_to_local",
        "Successfully checked the protocol version");

    return true;
}

bool RemoteToLocalSynchronizationManager::syncUserImpl(
    const bool waitIfRateLimitReached, ErrorString & errorDescription,
    const bool writeUserDataToLocalStorage)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::syncUserImpl: "
            << "wait if rate limit reached = "
            << (waitIfRateLimitReached ? "true" : "false")
            << ", write user data to local storage = "
            << (writeUserDataToLocalStorage ? "true" : "false"));

    if (m_user.hasId() && m_user.hasServiceLevel()) {
        QNDEBUG(
            "synchronization:remote_to_local",
            "User id and service level "
                << "are set, that means the user info has already been "
                << "synchronized once during the current session, won't do it "
                << "again");
        return true;
    }

    qint32 rateLimitSeconds = 0;
    qint32 errorCode = m_manager.userStore().getUser(
        m_user, errorDescription, rateLimitSeconds);
    if (errorCode ==
        static_cast<qint32>(qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED))
    {
        if (rateLimitSeconds < 0) {
            errorDescription.setBase(
                QT_TR_NOOP("Rate limit reached but the number "
                           "of seconds to wait is incorrect"));
            errorDescription.details() = QString::number(rateLimitSeconds);
            QNWARNING("synchronization:remote_to_local", errorDescription);
            return false;
        }

        QNDEBUG(
            "synchronization:remote_to_local",
            "Rate limit exceeded, need "
                << "to wait for " << rateLimitSeconds << " seconds");
        if (waitIfRateLimitReached) {
            int timerId = startTimer(secondsToMilliseconds(rateLimitSeconds));
            if (Q_UNLIKELY(timerId == 0)) {
                ErrorString errorMessage(QT_TR_NOOP(
                    "Failed to start a timer to postpone the "
                    "Evernote API call due to rate limit exceeding"));
                errorMessage.additionalBases().append(errorDescription.base());
                errorMessage.additionalBases().append(
                    errorDescription.additionalBases());
                errorMessage.details() = errorDescription.details();
                errorDescription = errorMessage;
                QNDEBUG("synchronization:remote_to_local", errorDescription);
                return false;
            }

            m_syncUserPostponeTimerId = timerId;
        }

        Q_EMIT rateLimitExceeded(rateLimitSeconds);
        return false;
    }
    else if (
        errorCode ==
        static_cast<qint32>(qevercloud::EDAMErrorCode::AUTH_EXPIRED))
    {
        ErrorString errorMessage(
            QT_TR_NOOP("unexpected AUTH_EXPIRED error when trying to download "
                       "the latest information about the current user"));
        errorMessage.additionalBases().append(errorDescription.base());
        errorMessage.additionalBases().append(
            errorDescription.additionalBases());
        errorMessage.details() = errorDescription.details();
        errorDescription = errorMessage;
        QNINFO("synchronization:remote_to_local", errorDescription);
        return false;
    }
    else if (errorCode != 0) {
        ErrorString errorMessage(
            QT_TR_NOOP("Failed to download the latest user info"));
        errorMessage.additionalBases().append(errorDescription.base());
        errorMessage.additionalBases().append(
            errorDescription.additionalBases());
        errorMessage.details() = errorDescription.details();
        errorDescription = errorMessage;
        QNINFO("synchronization:remote_to_local", errorDescription);
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
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager"
            << "::launchWritingUserDataToLocalStorage");

    if (m_onceAddedOrUpdatedUserInLocalStorage) {
        QNDEBUG(
            "synchronization:remote_to_local",
            "Already added or updated "
                << "the user data in the local storage, no need to do that "
                   "again");
        return;
    }

    connectToLocalStorage();

    // See if this user's entry already exists in the local storage or not
    m_findUserRequestId = QUuid::createUuid();
    QNTRACE(
        "synchronization:remote_to_local",
        "Emitting request to find user "
            << "in the local storage database: request id = "
            << m_findUserRequestId << ", user = " << m_user);
    Q_EMIT findUser(m_user, m_findUserRequestId);
}

bool RemoteToLocalSynchronizationManager::checkAndSyncAccountLimits(
    const bool waitIfRateLimitReached, ErrorString & errorDescription)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::checkAndSyncAccountLimits: wait "
            << "if rate limit reached = "
            << (waitIfRateLimitReached ? "true" : "false"));

    if (Q_UNLIKELY(!m_user.hasId())) {
        ErrorString error(
            QT_TR_NOOP("Detected the attempt to synchronize the account limits "
                       "before the user id was set"));
        QNWARNING("synchronization:remote_to_local", error);
        Q_EMIT failure(error);
        return false;
    }

    ApplicationSettings appSettings(
        account(), SYNCHRONIZATION_PERSISTENCE_NAME);

    const QString keyGroup = ACCOUNT_LIMITS_KEY_GROUP +
        QString::number(m_user.id()) + QStringLiteral("/");

    QVariant accountLimitsLastSyncTime =
        appSettings.value(keyGroup + ACCOUNT_LIMITS_LAST_SYNC_TIME_KEY);

    if (!accountLimitsLastSyncTime.isNull()) {
        QNTRACE(
            "synchronization:remote_to_local",
            "Found non-null last sync "
                << "time for account limits: " << accountLimitsLastSyncTime);

        bool conversionResult = false;

        qint64 timestamp =
            accountLimitsLastSyncTime.toLongLong(&conversionResult);

        if (conversionResult) {
            QNTRACE(
                "synchronization:remote_to_local",
                "Successfully read last "
                    << "sync time for account limits: "
                    << printableDateTimeFromTimestamp(timestamp));

            qint64 currentTimestamp = QDateTime::currentMSecsSinceEpoch();
            qint64 diff = currentTimestamp - timestamp;
            if ((diff > 0) && (diff < THIRTY_DAYS_IN_MSEC)) {
                QNTRACE(
                    "synchronization:remote_to_local",
                    "The cached account "
                        << "limits appear to be still valid");
                readSavedAccountLimits();
                return true;
            }
        }
    }

    return syncAccountLimits(waitIfRateLimitReached, errorDescription);
}

bool RemoteToLocalSynchronizationManager::syncAccountLimits(
    const bool waitIfRateLimitReached, ErrorString & errorDescription)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::syncAccountLimits: "
            << "wait if rate limit reached = "
            << (waitIfRateLimitReached ? "true" : "false"));

    if (Q_UNLIKELY(!m_user.hasServiceLevel())) {
        errorDescription.setBase(
            QT_TR_NOOP("No Evernote service level was found for the current "
                       "user"));
        QNDEBUG("synchronization:remote_to_local", errorDescription);
        return false;
    }

    qint32 rateLimitSeconds = 0;

    qint32 errorCode = m_manager.userStore().getAccountLimits(
        m_user.serviceLevel(), m_accountLimits, errorDescription,
        rateLimitSeconds);

    if (errorCode ==
        static_cast<qint32>(qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED))
    {
        if (rateLimitSeconds < 0) {
            errorDescription.setBase(
                QT_TR_NOOP("Rate limit reached but the number "
                           "of seconds to wait is incorrect"));
            errorDescription.details() = QString::number(rateLimitSeconds);
            QNWARNING("synchronization:remote_to_local", errorDescription);
            return false;
        }

        QNDEBUG(
            "synchronization:remote_to_local",
            "Rate limit exceeded, need "
                << "to wait for " << rateLimitSeconds << " seconds");

        if (waitIfRateLimitReached) {
            int timerId = startTimer(secondsToMilliseconds(rateLimitSeconds));
            if (Q_UNLIKELY(timerId == 0)) {
                ErrorString errorMessage(QT_TR_NOOP(
                    "Failed to start a timer to postpone the "
                    "Evernote API call due to rate limit exceeding"));
                errorMessage.additionalBases().append(errorDescription.base());
                errorMessage.additionalBases().append(
                    errorDescription.additionalBases());
                errorMessage.details() = errorDescription.details();
                errorDescription = errorMessage;
                QNWARNING("synchronization:remote_to_local", errorDescription);
                return false;
            }

            m_syncAccountLimitsPostponeTimerId = timerId;
        }

        Q_EMIT rateLimitExceeded(rateLimitSeconds);
        return false;
    }
    else if (
        errorCode ==
        static_cast<qint32>(qevercloud::EDAMErrorCode::AUTH_EXPIRED))
    {
        ErrorString errorMessage(
            QT_TR_NOOP("unexpected AUTH_EXPIRED error when trying to sync "
                       "the current user's account limits"));
        errorMessage.additionalBases().append(errorDescription.base());
        errorMessage.additionalBases().append(
            errorDescription.additionalBases());
        errorMessage.details() = errorDescription.details();
        errorDescription = errorMessage;
        QNWARNING("synchronization:remote_to_local", errorDescription);
        return false;
    }
    else if (errorCode != 0) {
        ErrorString errorMessage(QT_TR_NOOP(
            "Failed to get the account limits for the current user"));
        errorMessage.additionalBases().append(errorDescription.base());
        errorMessage.additionalBases().append(
            errorDescription.additionalBases());
        errorMessage.details() = errorDescription.details();
        errorDescription = errorMessage;
        QNWARNING("synchronization:remote_to_local", errorDescription);
        return false;
    }

    writeAccountLimitsToAppSettings();
    return true;
}

void RemoteToLocalSynchronizationManager::readSavedAccountLimits()
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::readSavedAccountLimits");

    if (Q_UNLIKELY(!m_user.hasId())) {
        ErrorString error(
            QT_TR_NOOP("Detected the attempt to read the saved "
                       "account limits before the user id was set"));
        QNWARNING("synchronization:remote_to_local", error);
        Q_EMIT failure(error);
        return;
    }

    m_accountLimits = qevercloud::AccountLimits();

    ApplicationSettings appSettings(
        account(), SYNCHRONIZATION_PERSISTENCE_NAME);

    const QString keyGroup = ACCOUNT_LIMITS_KEY_GROUP +
        QString::number(m_user.id()) + QStringLiteral("/");

    QVariant userMailLimitDaily =
        appSettings.value(keyGroup + ACCOUNT_LIMITS_USER_MAIL_LIMIT_DAILY_KEY);

    if (!userMailLimitDaily.isNull()) {
        QNTRACE(
            "synchronization:remote_to_local",
            "Found non-null user mail "
                << "limit daily account limit: " << userMailLimitDaily);

        bool conversionResult = false;
        qint32 value = userMailLimitDaily.toInt(&conversionResult);
        if (conversionResult) {
            m_accountLimits.userMailLimitDaily = value;
        }
        else {
            QNWARNING(
                "synchronization:remote_to_local",
                "Failed to convert "
                    << "user mail limit daily account limit to qint32: "
                    << userMailLimitDaily);
        }
    }

    QVariant noteSizeMax =
        appSettings.value(keyGroup + ACCOUNT_LIMITS_NOTE_SIZE_MAX_KEY);

    if (!noteSizeMax.isNull()) {
        QNTRACE(
            "synchronization:remote_to_local",
            "Found non-null note size "
                << "max: " << noteSizeMax);

        bool conversionResult = false;
        qint64 value = noteSizeMax.toLongLong(&conversionResult);
        if (conversionResult) {
            m_accountLimits.noteSizeMax = value;
        }
        else {
            QNWARNING(
                "synchronization:remote_to_local",
                "Failed to convert "
                    << "note size max account limit to qint64: "
                    << noteSizeMax);
        }
    }

    QVariant resourceSizeMax =
        appSettings.value(keyGroup + ACCOUNT_LIMITS_RESOURCE_SIZE_MAX_KEY);

    if (!resourceSizeMax.isNull()) {
        QNTRACE(
            "synchronization:remote_to_local",
            "Found non-null resource "
                << "size max: " << resourceSizeMax);

        bool conversionResult = false;
        qint64 value = resourceSizeMax.toLongLong(&conversionResult);
        if (conversionResult) {
            m_accountLimits.resourceSizeMax = value;
        }
        else {
            QNWARNING(
                "synchronization:remote_to_local",
                "Failed to convert "
                    << "resource size max account limit to qint64: "
                    << resourceSizeMax);
        }
    }

    QVariant userLinkedNotebookMax = appSettings.value(
        keyGroup + ACCOUNT_LIMITS_USER_LINKED_NOTEBOOK_MAX_KEY);

    if (!userLinkedNotebookMax.isNull()) {
        QNTRACE(
            "synchronization:remote_to_local",
            "Found non-null user linked "
                << "notebook max: " << userLinkedNotebookMax);

        bool conversionResult = false;
        qint32 value = userLinkedNotebookMax.toInt(&conversionResult);
        if (conversionResult) {
            m_accountLimits.userLinkedNotebookMax = value;
        }
        else {
            QNWARNING(
                "synchronization:remote_to_local",
                "Failed to convert "
                    << "user linked notebook max account limit to qint32: "
                    << userLinkedNotebookMax);
        }
    }

    QVariant uploadLimit =
        appSettings.value(keyGroup + ACCOUNT_LIMITS_UPLOAD_LIMIT_KEY);

    if (!uploadLimit.isNull()) {
        QNTRACE(
            "synchronization:remote_to_local",
            "Found non-null upload "
                << "limit: " << uploadLimit);

        bool conversionResult = false;
        qint64 value = uploadLimit.toLongLong(&conversionResult);
        if (conversionResult) {
            m_accountLimits.uploadLimit = value;
        }
        else {
            QNWARNING(
                "synchronization:remote_to_local",
                "Failed to convert "
                    << "upload limit to qint64: " << uploadLimit);
        }
    }

    QVariant userNoteCountMax =
        appSettings.value(keyGroup + ACCOUNT_LIMITS_USER_NOTE_COUNT_MAX_KEY);

    if (!userNoteCountMax.isNull()) {
        QNTRACE(
            "synchronization:remote_to_local",
            "Found non-null user note "
                << "count max: " << userNoteCountMax);

        bool conversionResult = false;
        qint32 value = userNoteCountMax.toInt(&conversionResult);
        if (conversionResult) {
            m_accountLimits.userNoteCountMax = value;
        }
        else {
            QNWARNING(
                "synchronization:remote_to_local",
                "Failed to convert "
                    << "user note count max to qint32: " << userNoteCountMax);
        }
    }

    QVariant userNotebookCountMax = appSettings.value(
        keyGroup + ACCOUNT_LIMITS_USER_NOTEBOOK_COUNT_MAX_KEY);

    if (!userNotebookCountMax.isNull()) {
        QNTRACE(
            "synchronization:remote_to_local",
            "Found non-null user "
                << "notebook count max: " << userNotebookCountMax);

        bool conversionResult = false;
        qint32 value = userNotebookCountMax.toInt(&conversionResult);
        if (conversionResult) {
            m_accountLimits.userNotebookCountMax = value;
        }
        else {
            QNWARNING(
                "synchronization:remote_to_local",
                "Failed to convert "
                    << "user notebook count max to qint32: "
                    << userNotebookCountMax);
        }
    }

    QVariant userTagCountMax =
        appSettings.value(keyGroup + ACCOUNT_LIMITS_USER_TAG_COUNT_MAX_KEY);

    if (!userTagCountMax.isNull()) {
        QNTRACE(
            "synchronization:remote_to_local",
            "Found non-null user tag "
                << "count max: " << userTagCountMax);

        bool conversionResult = false;
        qint32 value = userTagCountMax.toInt(&conversionResult);
        if (conversionResult) {
            m_accountLimits.userTagCountMax = value;
        }
        else {
            QNWARNING(
                "synchronization:remote_to_local",
                "Failed to convert "
                    << "user tag count max to qint32: " << userTagCountMax);
        }
    }

    QVariant noteTagCountMax =
        appSettings.value(keyGroup + ACCOUNT_LIMITS_NOTE_TAG_COUNT_MAX_KEY);

    if (!noteTagCountMax.isNull()) {
        QNTRACE(
            "synchronization:remote_to_local",
            "Found non-null note tag "
                << "cont max: " << noteTagCountMax);

        bool conversionResult = false;
        qint32 value = noteTagCountMax.toInt(&conversionResult);
        if (conversionResult) {
            m_accountLimits.noteTagCountMax = value;
        }
        else {
            QNWARNING(
                "synchronization:remote_to_local",
                "Failed to convert "
                    << "note tag count max to qint32: " << noteTagCountMax);
        }
    }

    QVariant userSavedSearchesMax = appSettings.value(
        keyGroup + ACCOUNT_LIMITS_USER_SAVED_SEARCH_COUNT_MAX_KEY);

    if (!userSavedSearchesMax.isNull()) {
        QNTRACE(
            "synchronization:remote_to_local",
            "Found non-null user saved "
                << "search max: " << userSavedSearchesMax);

        bool conversionResult = false;
        qint32 value = userSavedSearchesMax.toInt(&conversionResult);
        if (conversionResult) {
            m_accountLimits.userSavedSearchesMax = value;
        }
        else {
            QNWARNING(
                "synchronization:remote_to_local",
                "Failed to convert "
                    << "user saved search max to qint32: "
                    << userSavedSearchesMax);
        }
    }

    QVariant noteResourceCountMax = appSettings.value(
        keyGroup + ACCOUNT_LIMITS_NOTE_RESOURCE_COUNT_MAX_KEY);

    if (!noteResourceCountMax.isNull()) {
        QNTRACE(
            "synchronization:remote_to_local",
            "Found non-null note "
                << "resource count max: " << noteResourceCountMax);

        bool conversionResult = false;
        qint32 value = noteResourceCountMax.toInt(&conversionResult);
        if (conversionResult) {
            m_accountLimits.noteResourceCountMax = value;
        }
        else {
            QNWARNING(
                "synchronization:remote_to_local",
                "Failed to convert "
                    << "note resource count max to qint32: "
                    << noteResourceCountMax);
        }
    }

    QNTRACE(
        "synchronization:remote_to_local",
        "Read account limits from "
            << "application settings: " << m_accountLimits);
}

void RemoteToLocalSynchronizationManager::writeAccountLimitsToAppSettings()
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::writeAccountLimitsToAppSettings");

    if (Q_UNLIKELY(!m_user.hasId())) {
        ErrorString error(
            QT_TR_NOOP("Detected the attempt to save the account "
                       "limits to app settings before the user id was set"));
        QNWARNING("synchronization:remote_to_local", error);
        Q_EMIT failure(error);
        return;
    }

    ApplicationSettings appSettings(
        account(), SYNCHRONIZATION_PERSISTENCE_NAME);

    const QString keyGroup = ACCOUNT_LIMITS_KEY_GROUP +
        QString::number(m_user.id()) + QStringLiteral("/");

    appSettings.setValue(
        keyGroup + ACCOUNT_LIMITS_USER_MAIL_LIMIT_DAILY_KEY,
        (m_accountLimits.userMailLimitDaily.isSet()
             ? QVariant(m_accountLimits.userMailLimitDaily.ref())
             : QVariant()));

    appSettings.setValue(
        keyGroup + ACCOUNT_LIMITS_NOTE_SIZE_MAX_KEY,
        (m_accountLimits.noteSizeMax.isSet()
             ? QVariant(m_accountLimits.noteSizeMax.ref())
             : QVariant()));

    appSettings.setValue(
        keyGroup + ACCOUNT_LIMITS_RESOURCE_SIZE_MAX_KEY,
        (m_accountLimits.resourceSizeMax.isSet()
             ? QVariant(m_accountLimits.resourceSizeMax.ref())
             : QVariant()));

    appSettings.setValue(
        keyGroup + ACCOUNT_LIMITS_USER_LINKED_NOTEBOOK_MAX_KEY,
        (m_accountLimits.userLinkedNotebookMax.isSet()
             ? QVariant(m_accountLimits.userLinkedNotebookMax.ref())
             : QVariant()));

    appSettings.setValue(
        keyGroup + ACCOUNT_LIMITS_UPLOAD_LIMIT_KEY,
        (m_accountLimits.uploadLimit.isSet()
             ? QVariant(m_accountLimits.uploadLimit.ref())
             : QVariant()));

    appSettings.setValue(
        keyGroup + ACCOUNT_LIMITS_USER_NOTE_COUNT_MAX_KEY,
        (m_accountLimits.userNoteCountMax.isSet()
             ? QVariant(m_accountLimits.userNoteCountMax.ref())
             : QVariant()));

    appSettings.setValue(
        keyGroup + ACCOUNT_LIMITS_USER_NOTEBOOK_COUNT_MAX_KEY,
        (m_accountLimits.userNotebookCountMax.isSet()
             ? QVariant(m_accountLimits.userNotebookCountMax.ref())
             : QVariant()));

    appSettings.setValue(
        keyGroup + ACCOUNT_LIMITS_USER_TAG_COUNT_MAX_KEY,
        (m_accountLimits.userTagCountMax.isSet()
             ? QVariant(m_accountLimits.userTagCountMax.ref())
             : QVariant()));

    appSettings.setValue(
        keyGroup + ACCOUNT_LIMITS_NOTE_TAG_COUNT_MAX_KEY,
        (m_accountLimits.noteTagCountMax.isSet()
             ? QVariant(m_accountLimits.noteTagCountMax.ref())
             : QVariant()));

    appSettings.setValue(
        keyGroup + ACCOUNT_LIMITS_USER_SAVED_SEARCH_COUNT_MAX_KEY,
        (m_accountLimits.userSavedSearchesMax.isSet()
             ? QVariant(m_accountLimits.userSavedSearchesMax.ref())
             : QVariant()));

    appSettings.setValue(
        keyGroup + ACCOUNT_LIMITS_NOTE_RESOURCE_COUNT_MAX_KEY,
        (m_accountLimits.noteResourceCountMax.isSet()
             ? QVariant(m_accountLimits.noteResourceCountMax.ref())
             : QVariant()));

    appSettings.setValue(
        keyGroup + ACCOUNT_LIMITS_LAST_SYNC_TIME_KEY,
        QVariant(QDateTime::currentMSecsSinceEpoch()));
}

template <class ContainerType, class ElementType>
void RemoteToLocalSynchronizationManager::launchDataElementSyncCommon(
    const ContentSource contentSource, ContainerType & container,
    QList<QString> & expungedElements)
{
    bool syncingUserAccountData = (contentSource == ContentSource::UserAccount);

    QNTRACE(
        "synchronization:remote_to_local",
        "syncingUserAccountData = "
            << (syncingUserAccountData ? "true" : "false"));

    const auto & syncChunks =
        (syncingUserAccountData ? m_syncChunks : m_linkedNotebookSyncChunks);

    container.clear();
    int numSyncChunks = syncChunks.size();

    QNTRACE(
        "synchronization:remote_to_local",
        "Num sync chunks = " << numSyncChunks);

    for (int i = 0; i < numSyncChunks; ++i) {
        const auto & syncChunk = syncChunks[i];

        appendDataElementsFromSyncChunkToContainer<ContainerType>(
            syncChunk, container);

        extractExpungedElementsFromSyncChunk<ElementType>(
            syncChunk, expungedElements);
    }
}

template <class ContainerType, class ElementType>
void RemoteToLocalSynchronizationManager::launchDataElementSync(
    const ContentSource contentSource, const QString & typeName,
    ContainerType & container, QList<QString> & expungedElements)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::launchDataElementSync: "
            << typeName);

    launchDataElementSyncCommon<ContainerType, ElementType>(
        contentSource, container, expungedElements);

    if (container.isEmpty()) {
        QNDEBUG(
            "synchronization:remote_to_local",
            "No new or updated data "
                << "items within the container");
        return;
    }

    int numElements = container.size();

    if (typeName == QStringLiteral("Note")) {
        m_originalNumberOfNotes =
            static_cast<quint32>(std::max(numElements, 0));
        m_numNotesDownloaded = static_cast<quint32>(0);
    }
    else if (typeName == QStringLiteral("Resource")) {
        m_originalNumberOfResources =
            static_cast<quint32>(std::max(numElements, 0));
        m_numResourcesDownloaded = static_cast<quint32>(0);
    }

    for (auto it = container.begin(), end = container.end(); it != end; ++it) {
        const auto & element = *it;
        if (!element.guid.isSet()) {
            SET_CANT_FIND_BY_GUID_ERROR()
            Q_EMIT failure(errorDescription);
            return;
        }

        emitFindByGuidRequest(element);
    }
}

template <>
void RemoteToLocalSynchronizationManager::launchDataElementSync<
    TagsContainer, Tag>(
    const ContentSource contentSource, const QString & typeName,
    TagsContainer & container, QList<QString> & expungedElements)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::launchDataElementSync: "
            << typeName);

    launchDataElementSyncCommon<TagsContainer, Tag>(
        contentSource, container, expungedElements);

    if (container.empty()) {
        QNDEBUG(
            "synchronization:remote_to_local",
            "No data items within "
                << "the container");
        return;
    }

    if (syncingLinkedNotebooksContent()) {
        /**
         * NOTE: tags from linked notebooks can have parent tag guids referring
         * to tags from linked notebook's owner account; these parent tags might
         * be unaccessible because no notes from the currently linked notebook
         * are labeled with those parent tags; the local storage would reject
         * the attempts to insert tags without existing parents so need to
         * manually remove parent tag guids referring to inaccessible tags
         * from the tags being synced;
         *
         * First try to find all parent tags within the list of downloaded tags:
         * if that succeeds, there's no need to try finding the parent tags
         * within the local storage asynchronously
         */

        std::set<QString> guidsOfTagsWithMissingParentTag;
        const auto & tagIndexByGuid = container.get<ByGuid>();
        for (auto it = tagIndexByGuid.begin(), end = tagIndexByGuid.end();
             it != end; ++it)
        {
            const qevercloud::Tag & tag = *it;
            if (Q_UNLIKELY(!tag.guid.isSet())) {
                continue;
            }

            if (!tag.parentGuid.isSet()) {
                continue;
            }

            auto parentTagIt = tagIndexByGuid.find(tag.parentGuid.ref());
            if (parentTagIt != tagIndexByGuid.end()) {
                continue;
            }

            Q_UNUSED(guidsOfTagsWithMissingParentTag.insert(tag.guid.ref()))
            QNDEBUG(
                "synchronization:remote_to_local",
                "Detected tag which "
                    << "parent is not within the list of downloaded tags: "
                    << tag);
        }

        if (!guidsOfTagsWithMissingParentTag.empty()) {
            // Ok, let's fill the tag sync caches for all linked notebooks which
            // tags have parent tag guids referring to inaccessible parent tags
            std::set<QString> affectedLinkedNotebookGuids;
            for (auto it = guidsOfTagsWithMissingParentTag.begin(),
                      end = guidsOfTagsWithMissingParentTag.end();
                 it != end; ++it)
            {
                auto linkedNotebookGuidIt =
                    m_linkedNotebookGuidsByTagGuids.find(*it);

                if (linkedNotebookGuidIt !=
                    m_linkedNotebookGuidsByTagGuids.end()) {
                    auto insertResult = affectedLinkedNotebookGuids.insert(
                        linkedNotebookGuidIt.value());

                    if (insertResult.second) {
                        QNDEBUG(
                            "synchronization:remote_to_local",
                            "Guid of "
                                << "linked notebook for which TagSyncCache is "
                                << "required to ensure there are no "
                                   "inaccessible "
                                << "parent tags: "
                                << linkedNotebookGuidIt.value());
                    }
                }
            }

            for (auto it = affectedLinkedNotebookGuids.begin(),
                      end = affectedLinkedNotebookGuids.end();
                 it != end; ++it)
            {
                const QString & linkedNotebookGuid = *it;

                auto tagSyncCacheIt = m_tagSyncCachesByLinkedNotebookGuids.find(
                    linkedNotebookGuid);

                if (tagSyncCacheIt ==
                    m_tagSyncCachesByLinkedNotebookGuids.end()) {
                    auto * pTagSyncCache = new TagSyncCache(
                        m_manager.localStorageManagerAsync(),
                        linkedNotebookGuid, this);

                    tagSyncCacheIt =
                        m_tagSyncCachesByLinkedNotebookGuids.insert(
                            linkedNotebookGuid, pTagSyncCache);
                }

                TagSyncCache * pTagSyncCache = tagSyncCacheIt.value();
                if (pTagSyncCache->isFilled()) {
                    checkAndRemoveInaccessibleParentTagGuidsForTagsFromLinkedNotebook(
                        linkedNotebookGuid, *pTagSyncCache);
                }
                else {
                    Q_UNUSED(
                        m_linkedNotebookGuidsPendingTagSyncCachesFill.insert(
                            linkedNotebookGuid))

                    QObject::connect(
                        pTagSyncCache, &TagSyncCache::filled, this,
                        &RemoteToLocalSynchronizationManager::
                            onTagSyncCacheFilled,
                        Qt::ConnectionType(
                            Qt::UniqueConnection | Qt::QueuedConnection));

                    QObject::connect(
                        pTagSyncCache, &TagSyncCache::failure, this,
                        &RemoteToLocalSynchronizationManager::
                            onTagSyncCacheFailure,
                        Qt::ConnectionType(
                            Qt::UniqueConnection | Qt::QueuedConnection));

                    pTagSyncCache->fill();
                }
            }

            if (!m_linkedNotebookGuidsPendingTagSyncCachesFill.isEmpty()) {
                QNDEBUG(
                    "synchronization:remote_to_local",
                    "Pending "
                        << "TagSyncCaches filling for "
                        << m_linkedNotebookGuidsPendingTagSyncCachesFill.size()
                        << " linked notebook guids");
                return;
            }
        }
    }

    startFeedingDownloadedTagsToLocalStorageOneByOne(container);
}

void RemoteToLocalSynchronizationManager::launchTagsSync()
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::launchTagsSync");

    m_pendingTagsSyncStart = false;
    launchDataElementSync<TagsContainer, Tag>(
        ContentSource::UserAccount, QStringLiteral("Tag"), m_tags,
        m_expungedTags);
}

void RemoteToLocalSynchronizationManager::launchSavedSearchSync()
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::launchSavedSearchSync");

    launchDataElementSync<SavedSearchesList, SavedSearch>(
        ContentSource::UserAccount, QStringLiteral("Saved search"),
        m_savedSearches, m_expungedSavedSearches);
}

void RemoteToLocalSynchronizationManager::launchLinkedNotebookSync()
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::launchLinkedNotebookSync");

    m_pendingLinkedNotebooksSyncStart = false;
    launchDataElementSync<LinkedNotebooksList, LinkedNotebook>(
        ContentSource::UserAccount, QStringLiteral("Linked notebook"),
        m_linkedNotebooks, m_expungedLinkedNotebooks);
}

void RemoteToLocalSynchronizationManager::launchNotebookSync()
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::launchNotebookSync");

    m_pendingNotebooksSyncStart = false;
    launchDataElementSync<NotebooksList, Notebook>(
        ContentSource::UserAccount, QStringLiteral("Notebook"), m_notebooks,
        m_expungedNotebooks);
}

void RemoteToLocalSynchronizationManager::
    collectSyncedGuidsForFullSyncStaleDataItemsExpunger()
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager"
            << "::collectSyncedGuidsForFullSyncStaleDataItemsExpunger");

    m_fullSyncStaleDataItemsSyncedGuids.m_syncedNotebookGuids.clear();
    m_fullSyncStaleDataItemsSyncedGuids.m_syncedTagGuids.clear();
    m_fullSyncStaleDataItemsSyncedGuids.m_syncedNoteGuids.clear();
    m_fullSyncStaleDataItemsSyncedGuids.m_syncedSavedSearchGuids.clear();

    m_fullSyncStaleDataItemsSyncedGuids.m_syncedNotebookGuids.reserve(
        m_notebooks.size());

    for (const auto & notebook: ::qAsConst(m_notebooks)) {
        if (notebook.guid.isSet()) {
            Q_UNUSED(m_fullSyncStaleDataItemsSyncedGuids.m_syncedNotebookGuids
                         .insert(notebook.guid.ref()))
        }
    }

    m_fullSyncStaleDataItemsSyncedGuids.m_syncedTagGuids.reserve(
        static_cast<int>(m_tags.size()));

    for (const auto & tag: ::qAsConst(m_tags)) {
        if (tag.guid.isSet()) {
            Q_UNUSED(
                m_fullSyncStaleDataItemsSyncedGuids.m_syncedTagGuids.insert(
                    tag.guid.ref()))
        }
    }

    m_fullSyncStaleDataItemsSyncedGuids.m_syncedNoteGuids.reserve(
        m_notes.size());

    for (const auto & note: ::qAsConst(m_notes)) {
        if (note.guid.isSet()) {
            Q_UNUSED(
                m_fullSyncStaleDataItemsSyncedGuids.m_syncedNoteGuids.insert(
                    note.guid.ref()))
        }
    }

    m_fullSyncStaleDataItemsSyncedGuids.m_syncedSavedSearchGuids.reserve(
        m_savedSearches.size());

    for (const auto & savedSearch: ::qAsConst(m_savedSearches)) {
        if (savedSearch.guid.isSet()) {
            Q_UNUSED(
                m_fullSyncStaleDataItemsSyncedGuids.m_syncedSavedSearchGuids
                    .insert(savedSearch.guid.ref()))
        }
    }
}

void RemoteToLocalSynchronizationManager::launchFullSyncStaleDataItemsExpunger()
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager"
            << "::launchFullSyncStaleDataItemsExpunger");

    if (m_pFullSyncStaleDataItemsExpunger) {
        junkFullSyncStaleDataItemsExpunger(*m_pFullSyncStaleDataItemsExpunger);
        m_pFullSyncStaleDataItemsExpunger = nullptr;
    }

    m_pFullSyncStaleDataItemsExpunger = new FullSyncStaleDataItemsExpunger(
        m_manager.localStorageManagerAsync(), m_notebookSyncCache,
        m_tagSyncCache, m_savedSearchSyncCache,
        m_fullSyncStaleDataItemsSyncedGuids, QString(), this);

    QObject::connect(
        m_pFullSyncStaleDataItemsExpunger,
        &FullSyncStaleDataItemsExpunger::finished, this,
        &RemoteToLocalSynchronizationManager::
            onFullSyncStaleDataItemsExpungerFinished,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        m_pFullSyncStaleDataItemsExpunger,
        &FullSyncStaleDataItemsExpunger::failure, this,
        &RemoteToLocalSynchronizationManager::
            onFullSyncStaleDataItemsExpungerFailure,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QNDEBUG(
        "synchronization:remote_to_local",
        "Starting "
            << "FullSyncStaleDataItemsExpunger for user's own content");
    m_pFullSyncStaleDataItemsExpunger->start();
}

bool RemoteToLocalSynchronizationManager::
    launchFullSyncStaleDataItemsExpungersForLinkedNotebooks()
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager"
            << "::launchFullSyncStaleDataItemsExpungersForLinkedNotebooks");

    bool foundLinkedNotebookEligibleForFullSyncStaleDataItemsExpunging = false;

    for (const auto & linkedNotebook: ::qAsConst(m_allLinkedNotebooks)) {
        if (Q_UNLIKELY(!linkedNotebook.hasGuid())) {
            QNWARNING(
                "synchronization:remote_to_local",
                "Skipping linked "
                    << "notebook without guid: " << linkedNotebook);
            continue;
        }

        const QString & linkedNotebookGuid = linkedNotebook.guid();
        QNTRACE(
            "synchronization:remote_to_local",
            "Examining linked notebook "
                << "with guid " << linkedNotebookGuid);

        auto fullSyncIt =
            m_linkedNotebookGuidsForWhichFullSyncWasPerformed.find(
                linkedNotebookGuid);

        if (fullSyncIt ==
            m_linkedNotebookGuidsForWhichFullSyncWasPerformed.end()) {
            QNTRACE(
                "synchronization:remote_to_local",
                "It doesn't appear that "
                    << "full sync was performed for linked notebook with guid "
                    << linkedNotebookGuid << " in the past");
            continue;
        }

        auto onceFullSyncIt =
            m_linkedNotebookGuidsOnceFullySynced.find(linkedNotebookGuid);

        if (onceFullSyncIt == m_linkedNotebookGuidsOnceFullySynced.end()) {
            QNTRACE(
                "synchronization:remote_to_local",
                "It appears the full "
                    << "sync was performed for the first time for linked "
                       "notebook "
                    << "with guid " << linkedNotebookGuid);
            continue;
        }

        QNDEBUG(
            "synchronization:remote_to_local",
            "The contents of a linked "
                << "notebook with guid " << linkedNotebookGuid << " were fully "
                << "synced after being fully synced in the past, need to seek "
                   "for "
                << "stale data items and expunge them");
        foundLinkedNotebookEligibleForFullSyncStaleDataItemsExpunging = true;

        FullSyncStaleDataItemsExpunger::SyncedGuids syncedGuids;

        for (const auto nit: qevercloud::toRange(
                 qAsConst(m_linkedNotebookGuidsByNotebookGuids)))
        {
            const QString & currentLinkedNotebookGuid = nit.value();
            if (currentLinkedNotebookGuid != linkedNotebookGuid) {
                continue;
            }

            const QString & notebookGuid = nit.key();
            Q_UNUSED(syncedGuids.m_syncedNotebookGuids.insert(notebookGuid))

            for (const auto & note: ::qAsConst(m_notes)) {
                if (note.guid.isSet() && note.notebookGuid.isSet() &&
                    (note.notebookGuid.ref() == notebookGuid))
                {
                    Q_UNUSED(
                        syncedGuids.m_syncedNoteGuids.insert(note.guid.ref()))
                }
            }
        }

        for (const auto tit:
             qevercloud::toRange(::qAsConst(m_linkedNotebookGuidsByTagGuids)))
        {
            const QString & currentLinkedNotebookGuid = tit.value();
            if (currentLinkedNotebookGuid != linkedNotebookGuid) {
                continue;
            }

            const QString & tagGuid = tit.key();
            Q_UNUSED(syncedGuids.m_syncedTagGuids.insert(tagGuid))
        }

        auto notebookSyncCacheIt =
            m_notebookSyncCachesByLinkedNotebookGuids.find(linkedNotebookGuid);

        if (notebookSyncCacheIt ==
            m_notebookSyncCachesByLinkedNotebookGuids.end()) {
            auto * pNotebookSyncCache = new NotebookSyncCache(
                m_manager.localStorageManagerAsync(), linkedNotebookGuid, this);

            notebookSyncCacheIt =
                m_notebookSyncCachesByLinkedNotebookGuids.insert(
                    linkedNotebookGuid, pNotebookSyncCache);
        }

        auto tagSyncCacheIt =
            m_tagSyncCachesByLinkedNotebookGuids.find(linkedNotebookGuid);

        if (tagSyncCacheIt == m_tagSyncCachesByLinkedNotebookGuids.end()) {
            auto * pTagSyncCache = new TagSyncCache(
                m_manager.localStorageManagerAsync(), linkedNotebookGuid, this);

            tagSyncCacheIt = m_tagSyncCachesByLinkedNotebookGuids.insert(
                linkedNotebookGuid, pTagSyncCache);
        }

        auto * pExpunger = new FullSyncStaleDataItemsExpunger(
            m_manager.localStorageManagerAsync(), *notebookSyncCacheIt.value(),
            *tagSyncCacheIt.value(), m_savedSearchSyncCache, syncedGuids,
            linkedNotebookGuid, this);

        m_fullSyncStaleDataItemsExpungersByLinkedNotebookGuid
            [linkedNotebookGuid] = pExpunger;

        QObject::connect(
            pExpunger, &FullSyncStaleDataItemsExpunger::finished, this,
            &RemoteToLocalSynchronizationManager::
                onFullSyncStaleDataItemsExpungerFinished,
            Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

        QObject::connect(
            pExpunger, &FullSyncStaleDataItemsExpunger::failure, this,
            &RemoteToLocalSynchronizationManager::
                onFullSyncStaleDataItemsExpungerFailure,
            Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

        QNDEBUG(
            "synchronization:remote_to_local",
            "Starting "
                << "FullSyncStaleDataItemsExpunger for the content from linked "
                << "notebook with guid " << linkedNotebookGuid);
        pExpunger->start();
    }

    return foundLinkedNotebookEligibleForFullSyncStaleDataItemsExpunging;
}

void RemoteToLocalSynchronizationManager::
    launchExpungingOfNotelessTagsFromLinkedNotebooks()
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager"
            << "::launchExpungingOfNotelessTagsFromLinkedNotebooks");

    m_expungeNotelessTagsRequestId = QUuid::createUuid();

    QNTRACE(
        "synchronization:remote_to_local",
        "Emitting the request to "
            << "expunge noteless tags from linked notebooks: "
            << m_expungeNotelessTagsRequestId);

    Q_EMIT expungeNotelessTagsFromLinkedNotebooks(
        m_expungeNotelessTagsRequestId);
}

bool RemoteToLocalSynchronizationManager::syncingLinkedNotebooksContent() const
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::syncingLinkedNotebooksContent: "
            << "last sync mode = " << m_lastSyncMode
            << ", full note contents downloaded = "
            << (m_fullNoteContentsDownloaded ? "true" : "false")
            << ", expunged from server to client = "
            << (m_expungedFromServerToClient ? "true" : "false"));

    if (m_lastSyncMode == SyncMode::FullSync) {
        return m_fullNoteContentsDownloaded;
    }

    return m_expungedFromServerToClient;
}

void RemoteToLocalSynchronizationManager::checkAndIncrementNoteDownloadProgress(
    const QString & noteGuid)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager"
            << "::checkAndIncrementNoteDownloadProgress: note guid = "
            << noteGuid);

    if (m_originalNumberOfNotes == 0) {
        QNDEBUG("synchronization:remote_to_local", "No notes to download");
        return;
    }

    if (m_resourceGuidsPendingInkNoteImageDownloadPerNoteGuid.contains(
            noteGuid)) {
        QNDEBUG(
            "synchronization:remote_to_local",
            "Found still pending ink "
                << "note image download(s) for this note guid, won't increment "
                << "the note download progress");
        return;
    }

    if (m_notesPendingThumbnailDownloadByGuid.contains(noteGuid)) {
        QNDEBUG(
            "synchronization:remote_to_local",
            "Found still pending note "
                << "thumbnail download for this note guid, won't increment "
                << "the note download progress");
        return;
    }

    if (Q_UNLIKELY(m_numNotesDownloaded == m_originalNumberOfNotes)) {
        QNDEBUG(
            "synchronization:remote_to_local",
            "The count of downloaded "
                << "notes (" << m_numNotesDownloaded << ") is already equal to "
                << "the original number of notes (" << m_originalNumberOfNotes
                << "), won't increment it further");
        return;
    }

    ++m_numNotesDownloaded;

    QNTRACE(
        "synchronization:remote_to_local",
        "Incremented the number of "
            << "downloaded notes to " << m_numNotesDownloaded
            << ", the total number of notes to download = "
            << m_originalNumberOfNotes);

    if (syncingLinkedNotebooksContent()) {
        Q_EMIT linkedNotebooksNotesDownloadProgress(
            m_numNotesDownloaded, m_originalNumberOfNotes);
    }
    else {
        Q_EMIT notesDownloadProgress(
            m_numNotesDownloaded, m_originalNumberOfNotes);
    }
}

void RemoteToLocalSynchronizationManager::
    checkAndIncrementResourceDownloadProgress(const QString & resourceGuid)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager"
            << "::checkAndIncrementResourceDownloadProgress: resource guid = "
            << resourceGuid);

    if (m_originalNumberOfResources == 0) {
        QNDEBUG("synchronization:remote_to_local", "No resources to download");
        return;
    }

    for (
        const auto it: qevercloud::toRange(qAsConst(
            m_resourceGuidsPendingFindNotebookForInkNoteImageDownloadPerNoteGuid)))
    {
        if (it.value() == resourceGuid) {
            QNDEBUG(
                "synchronization:remote_to_local",
                "The resource is still "
                    << "pending finding notebook for ink note image "
                       "downloading");
            return;
        }
    }

    if (Q_UNLIKELY(m_numResourcesDownloaded == m_originalNumberOfResources)) {
        QNWARNING(
            "synchronization:remote_to_local",
            "The count of downloaded "
                << "resources (" << m_numResourcesDownloaded
                << ") is already equal to the original number "
                << "of resources (" << m_originalNumberOfResources
                << "(, won't increment it further");
        return;
    }

    ++m_numResourcesDownloaded;

    QNTRACE(
        "synchronization:remote_to_local",
        "Incremented the number of "
            << "downloaded resources to " << m_numResourcesDownloaded
            << ", the total number of resources to download = "
            << m_originalNumberOfResources);

    if (syncingLinkedNotebooksContent()) {
        Q_EMIT linkedNotebooksResourcesDownloadProgress(
            m_numResourcesDownloaded, m_originalNumberOfResources);
    }
    else {
        Q_EMIT resourcesDownloadProgress(
            m_numResourcesDownloaded, m_originalNumberOfResources);
    }
}

bool RemoteToLocalSynchronizationManager::notebooksSyncInProgress() const
{
    if (!m_pendingNotebooksSyncStart &&
        (!m_notebooks.isEmpty() || !m_notebooksPendingAddOrUpdate.isEmpty() ||
         !m_findNotebookByGuidRequestIds.isEmpty() ||
         !m_findNotebookByNameRequestIds.isEmpty() ||
         !m_addNotebookRequestIds.isEmpty() ||
         !m_updateNotebookRequestIds.isEmpty() ||
         !m_expungeNotebookRequestIds.isEmpty()))
    {
        QNDEBUG(
            "synchronization:remote_to_local",
            "Notebooks sync is in "
                << "progress: there are " << m_notebooks.size()
                << " notebooks pending processing and/or "
                << m_notebooksPendingAddOrUpdate.size()
                << " notebooks pending add or update within the local storage: "
                << "pending " << m_addNotebookRequestIds.size()
                << " add notebook requests and/or "
                << m_updateNotebookRequestIds.size()
                << " update notebook requests and/or "
                << m_findNotebookByGuidRequestIds.size()
                << " find notebook by guid requests and/or "
                << m_findNotebookByNameRequestIds.size()
                << " find notebook by name requests and/or "
                << m_expungeNotebookRequestIds.size()
                << " expunge notebook requests");
        return true;
    }

    auto notebookSyncConflictResolvers =
        findChildren<NotebookSyncConflictResolver *>();

    if (!notebookSyncConflictResolvers.isEmpty()) {
        QNDEBUG(
            "synchronization:remote_to_local",
            "Notebooks sync is in "
                << "progress: there are "
                << notebookSyncConflictResolvers.size()
                << " active notebook sync conflict resolvers");
        return true;
    }

    return false;
}

bool RemoteToLocalSynchronizationManager::tagsSyncInProgress() const
{
    if (!m_pendingTagsSyncStart &&
        (!m_tagsPendingProcessing.isEmpty() ||
         !m_tagsPendingAddOrUpdate.isEmpty() ||
         !m_findTagByGuidRequestIds.isEmpty() ||
         !m_findTagByNameRequestIds.isEmpty() ||
         !m_addTagRequestIds.isEmpty() || !m_updateTagRequestIds.isEmpty() ||
         !m_expungeTagRequestIds.isEmpty()))
    {
        QNDEBUG(
            "synchronization:remote_to_local",
            "Tags sync is in progress: "
                << "there are " << m_tagsPendingProcessing.size()
                << " tags pending processing and/or "
                << m_tagsPendingAddOrUpdate.size()
                << " tags pending add or update within "
                << "the local storage: pending " << m_addTagRequestIds.size()
                << " add tag requests and/or " << m_updateTagRequestIds.size()
                << " update tag requests and/or "
                << m_findTagByGuidRequestIds.size()
                << " find tag by guid requests and/or "
                << m_findTagByNameRequestIds.size()
                << " find tag by name requests and/or "
                << m_expungeTagRequestIds.size() << " expunge tag requests");
        return true;
    }

    auto tagSyncConflictResolvers = findChildren<TagSyncConflictResolver *>();

    if (!tagSyncConflictResolvers.isEmpty()) {
        QNDEBUG(
            "synchronization:remote_to_local",
            "Tags sync is in progress: "
                << "there are " << tagSyncConflictResolvers.size()
                << " active tag sync conflict resolvers");
        return true;
    }

    return false;
}

bool RemoteToLocalSynchronizationManager::notesSyncInProgress() const
{
    auto noteSyncConflictResolvers = findChildren<NoteSyncConflictResolver *>();

    if (!m_notesPendingAddOrUpdate.isEmpty() ||
        !m_findNoteByGuidRequestIds.isEmpty() ||
        !m_addNoteRequestIds.isEmpty() || !m_updateNoteRequestIds.isEmpty() ||
        !m_expungeNoteRequestIds.isEmpty() ||
        !m_notesToAddPerAPICallPostponeTimerId.isEmpty() ||
        !m_notesToUpdatePerAPICallPostponeTimerId.isEmpty() ||
        !m_notesPendingDownloadForAddingToLocalStorage.isEmpty() ||
        !m_notesPendingDownloadForUpdatingInLocalStorageByGuid.isEmpty() ||
        !m_notesPendingInkNoteImagesDownloadByFindNotebookRequestId.isEmpty() ||
        !m_notesPendingThumbnailDownloadByFindNotebookRequestId.isEmpty() ||
        !m_notesPendingThumbnailDownloadByGuid.isEmpty() ||
        !m_updateNoteWithThumbnailRequestIds.isEmpty() ||
        !noteSyncConflictResolvers.isEmpty())
    {
        QNDEBUG(
            "synchronization:remote_to_local",
            "Notes sync is in progress: "
                << "there are " << m_notesPendingAddOrUpdate.size()
                << " notes pending add or update within "
                << "the local storage: pending " << m_addNoteRequestIds.size()
                << " add note requests and/or " << m_updateNoteRequestIds.size()
                << " update note requests and/or "
                << m_findNoteByGuidRequestIds.size()
                << " find note by guid requests and/or "
                << m_notesToAddPerAPICallPostponeTimerId.size()
                << " notes pending addition due to rate API limits and/or "
                << m_notesToUpdatePerAPICallPostponeTimerId.size()
                << " notes pending update due to rate API limits and/or "
                << m_notesPendingDownloadForAddingToLocalStorage.size()
                << " notes pending download for adding to "
                << "the local storage and/or "
                << m_notesPendingDownloadForUpdatingInLocalStorageByGuid.size()
                << " notes pending download for updating "
                << "in the local stroage and/or "
                << m_notesPendingInkNoteImagesDownloadByFindNotebookRequestId
                       .size()
                << " notes pending ink note image download and/or "
                << (m_notesPendingThumbnailDownloadByFindNotebookRequestId
                        .size() +
                    m_notesPendingThumbnailDownloadByGuid.size())
                << " notes pending thumbnail download and/or "
                << m_updateNoteWithThumbnailRequestIds.size()
                << " update note with thumbnail requests and/or "
                << noteSyncConflictResolvers.size()
                << " note sync conflict resolvers");
        return true;
    }

    return false;
}

bool RemoteToLocalSynchronizationManager::resourcesSyncInProgress() const
{
    return !m_resourcesPendingAddOrUpdate.isEmpty() ||
        !m_findResourceByGuidRequestIds.isEmpty() ||
        !m_addResourceRequestIds.isEmpty() ||
        !m_updateResourceRequestIds.isEmpty() ||
        !m_resourcesByMarkNoteOwningResourceDirtyRequestIds.isEmpty() ||
        !m_resourcesByFindNoteRequestIds.isEmpty() ||
        !m_inkNoteResourceDataPerFindNotebookRequestId.isEmpty() ||
        !m_resourcesPendingDownloadForAddingToLocalStorageWithNotesByResourceGuid
             .isEmpty() ||
        !m_resourcesPendingDownloadForUpdatingInLocalStorageWithNotesByResourceGuid
             .isEmpty() ||
        !m_resourcesToAddWithNotesPerAPICallPostponeTimerId.isEmpty() ||
        !m_resourcesToUpdateWithNotesPerAPICallPostponeTimerId.isEmpty() ||
        !m_postponedConflictingResourceDataPerAPICallPostponeTimerId.isEmpty();
}

#define PRINT_CONTENT_SOURCE(StreamType)                                       \
    StreamType & operator<<(                                                   \
        StreamType & strm,                                                     \
        const RemoteToLocalSynchronizationManager::ContentSource & obj)        \
    {                                                                          \
        switch (obj) {                                                         \
        case RemoteToLocalSynchronizationManager::ContentSource::UserAccount:  \
            strm << "UserAccount";                                             \
            break;                                                             \
        case RemoteToLocalSynchronizationManager::ContentSource::              \
            LinkedNotebook:                                                    \
            strm << "LinkedNotebook";                                          \
            break;                                                             \
        default:                                                               \
            strm << "Unknown";                                                 \
            break;                                                             \
        }                                                                      \
                                                                               \
        return strm;                                                           \
    }                                                                          \
    // PRINT_CONTENT_SOURCE

PRINT_CONTENT_SOURCE(QTextStream)
PRINT_CONTENT_SOURCE(QDebug)

#undef PRINT_CONTENT_SOURCE

void RemoteToLocalSynchronizationManager::
    checkNotebooksAndTagsSyncCompletionAndLaunchNotesAndResourcesSync()
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager"
            << "::"
               "checkNotebooksAndTagsSyncCompletionAndLaunchNotesAndResourcesSy"
               "nc");

    if (m_pendingNotebooksSyncStart) {
        QNDEBUG(
            "synchronization:remote_to_local",
            "Still pending notebook "
                << "sync start");
        return;
    }

    if (m_pendingTagsSyncStart) {
        QNDEBUG(
            "synchronization:remote_to_local",
            "Still pending tags sync "
                << "start");
        return;
    }

    if (notebooksSyncInProgress() || tagsSyncInProgress()) {
        return;
    }

    ContentSource contentSource =
        (syncingLinkedNotebooksContent() ? ContentSource::LinkedNotebook
                                         : ContentSource::UserAccount);

    launchNotesSync(contentSource);

    if (notesSyncInProgress()) {
        return;
    }

    // If we got here, there are no notes to sync but there might be resources
    // to sync

    if (m_lastSyncMode != SyncMode::IncrementalSync) {
        /**
         * NOTE: during the full sync the individual resources are not synced,
         * instead the full note contents including the resources are synced.
         *
         * That works both for the content from user's own account and for
         * the stuff from linked notebooks: the sync of linked notebooks'
         * content might be full while the last sync of user's own content is
         * incremental but in this case there won't be resources within
         * the sync chunk downloaded for that linked notebook so there's no real
         * problem with us not getting inside this if block when syncing stuff
         * from the linked notebooks
         */
        QNDEBUG(
            "synchronization:remote_to_local",
            "The last sync mode is not "
                << "incremental, won't launch the sync of resources");
        return;
    }

    if (!resourcesSyncInProgress()) {
        launchResourcesSync(contentSource);
    }
}

void RemoteToLocalSynchronizationManager::launchNotesSync(
    const ContentSource & contentSource)
{
    launchDataElementSync<NotesList, Note>(
        contentSource, QStringLiteral("Note"), m_notes, m_expungedNotes);
}

void RemoteToLocalSynchronizationManager::
    checkNotesSyncCompletionAndLaunchResourcesSync()
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager"
            << "::checkNotesSyncCompletionAndLaunchResourcesSync");

    if (m_lastSyncMode != SyncMode::IncrementalSync) {
        /**
         * NOTE: during the full sync the individual resources are not synced,
         * instead the full note contents including the resources are synced.
         *
         * That works both for the content from user's own account and for
         * the stuff from linked notebooks: the sync of linked notebooks'
         * content might be full while the last sync of user's own content is
         * incremental but in this case there won't be resources within the
         * synch chunk downloaded for that linked notebook so there's no real
         * problem with us not getting inside this if block when syncing stuff
         * from the linked notebooks
         */
        QNDEBUG(
            "synchronization:remote_to_local",
            "Sync is not incremental, "
                << "won't launch resources sync");
        return;
    }

    if (!m_pendingNotebooksSyncStart && !notebooksSyncInProgress() &&
        !m_pendingTagsSyncStart && !tagsSyncInProgress() &&
        !notesSyncInProgress() && !resourcesSyncInProgress())
    {
        launchResourcesSync(
            syncingLinkedNotebooksContent() ? ContentSource::LinkedNotebook
                                            : ContentSource::UserAccount);
    }
}

void RemoteToLocalSynchronizationManager::launchResourcesSync(
    const ContentSource & contentSource)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::launchResourcesSync: "
            << "content source = " << contentSource);

    QList<QString> dummyList;
    launchDataElementSync<ResourcesList, Resource>(
        contentSource, QStringLiteral("Resource"), m_resources, dummyList);
}

void RemoteToLocalSynchronizationManager::
    checkLinkedNotebooksSyncAndLaunchLinkedNotebookContentSync()
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager"
            << "::checkLinkedNotebooksSyncAndLaunchLinkedNotebookContentSync");

    if (m_updateLinkedNotebookRequestIds.isEmpty() &&
        m_addLinkedNotebookRequestIds.isEmpty())
    {
        // All remote linked notebooks were already updated in the local storage
        // or added there
        startLinkedNotebooksSync();
    }
}

void RemoteToLocalSynchronizationManager::launchLinkedNotebooksContentsSync()
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::"
        "launchLinkedNotebooksContentsSync");

    m_pendingTagsSyncStart = true;
    m_pendingNotebooksSyncStart = true;

    launchLinkedNotebooksTagsSync();
    launchLinkedNotebooksNotebooksSync();

    checkNotebooksAndTagsSyncCompletionAndLaunchNotesAndResourcesSync();

    // NOTE: we might have received the only sync chunk without the actual data
    // elements, need to check for such case and leave if there's nothing worth
    // processing within the sync
    checkServerDataMergeCompletion();
}

template <>
bool RemoteToLocalSynchronizationManager::
    mapContainerElementsWithLinkedNotebookGuid<
        RemoteToLocalSynchronizationManager::TagsList>(
        const QString & linkedNotebookGuid,
        const RemoteToLocalSynchronizationManager::TagsList & tags)
{
    for (const auto & tag: ::qAsConst(tags)) {
        if (!tag.guid.isSet()) {
            ErrorString error(QT_TRANSLATE_NOOP(
                "RemoteToLocalSynchronizationManager",
                "Detected the attempt to map "
                "the linked notebook guid to "
                "a tag without guid"));
            if (tag.name.isSet()) {
                error.details() = tag.name.ref();
            }

            QNWARNING(
                "synchronization:remote_to_local", error << ", tag: " << tag);
            Q_EMIT failure(error);
            return false;
        }

        m_linkedNotebookGuidsByTagGuids[tag.guid.ref()] = linkedNotebookGuid;
    }

    return true;
}

template <>
bool RemoteToLocalSynchronizationManager::
    mapContainerElementsWithLinkedNotebookGuid<
        RemoteToLocalSynchronizationManager::NotebooksList>(
        const QString & linkedNotebookGuid,
        const RemoteToLocalSynchronizationManager::NotebooksList & notebooks)
{
    for (const auto & notebook: ::qAsConst(notebooks)) {
        if (!notebook.guid.isSet()) {
            ErrorString error(QT_TRANSLATE_NOOP(
                "RemoteToLocalSynchronizationManager",
                "Detected the attempt to map "
                "the linked notebook guid to "
                "a notebook without guid"));
            if (notebook.name.isSet()) {
                error.details() = notebook.name.ref();
            }

            QNWARNING(
                "synchronization:remote_to_local",
                error << ", notebook: " << notebook);
            Q_EMIT failure(error);
            return false;
        }

        m_linkedNotebookGuidsByNotebookGuids[notebook.guid.ref()] =
            linkedNotebookGuid;
    }

    return true;
}

template <>
bool RemoteToLocalSynchronizationManager::
    mapContainerElementsWithLinkedNotebookGuid<
        RemoteToLocalSynchronizationManager::NotesList>(
        const QString & linkedNotebookGuid,
        const RemoteToLocalSynchronizationManager::NotesList & notes)
{
    for (const auto & note: ::qAsConst(notes)) {
        if (!note.notebookGuid.isSet()) {
            ErrorString error(QT_TRANSLATE_NOOP(
                "RemoteToLocalSynchronizationManager",
                "Can't map note to a linked notebook: "
                "note has no notebook guid"));
            if (note.title.isSet()) {
                error.details() = note.title.ref();
            }

            QNWARNING(
                "synchronization:remote_to_local", error << ", note; " << note);
            Q_EMIT failure(error);
            return false;
        }

        m_linkedNotebookGuidsByNotebookGuids[note.notebookGuid.ref()] =
            linkedNotebookGuid;
    }

    return true;
}

template <>
bool RemoteToLocalSynchronizationManager::
    mapContainerElementsWithLinkedNotebookGuid<
        RemoteToLocalSynchronizationManager::ResourcesList>(
        const QString & linkedNotebookGuid,
        const RemoteToLocalSynchronizationManager::ResourcesList & resources)
{
    for (const auto & resource: ::qAsConst(resources)) {
        if (!resource.guid.isSet()) {
            ErrorString error(QT_TRANSLATE_NOOP(
                "RemoteToLocalSynchronizationManager",
                "Can't map resource to a linked "
                "notebook: resource has no guid"));
            QNWARNING(
                "synchronization:remote_to_local",
                error << ", resource: " << resource);
            Q_EMIT failure(error);
            return false;
        }

        m_linkedNotebookGuidsByResourceGuids[resource.guid.ref()] =
            linkedNotebookGuid;
    }

    return true;
}

template <>
void RemoteToLocalSynchronizationManager::
    unmapContainerElementsFromLinkedNotebookGuid<qevercloud::Tag>(
        const QList<QString> & tagGuids)
{
    for (const auto & guid: ::qAsConst(tagGuids)) {
        auto mapIt = m_linkedNotebookGuidsByTagGuids.find(guid);
        if (mapIt == m_linkedNotebookGuidsByTagGuids.end()) {
            continue;
        }

        Q_UNUSED(m_linkedNotebookGuidsByTagGuids.erase(mapIt));
    }
}

template <>
void RemoteToLocalSynchronizationManager::
    unmapContainerElementsFromLinkedNotebookGuid<qevercloud::Notebook>(
        const QList<QString> & notebookGuids)
{
    for (const auto & guid: ::qAsConst(notebookGuids)) {
        auto mapIt = m_linkedNotebookGuidsByNotebookGuids.find(guid);
        if (mapIt == m_linkedNotebookGuidsByNotebookGuids.end()) {
            continue;
        }

        Q_UNUSED(m_linkedNotebookGuidsByNotebookGuids.erase(mapIt));
    }
}

void RemoteToLocalSynchronizationManager::startLinkedNotebooksSync()
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::startLinkedNotebooksSync");

    if (!m_allLinkedNotebooksListed) {
        requestAllLinkedNotebooks();
        return;
    }

    const int numAllLinkedNotebooks = m_allLinkedNotebooks.size();
    if (numAllLinkedNotebooks == 0) {
        QNDEBUG(
            "synchronization:remote_to_local",
            "No linked notebooks are "
                << "present within the account, can finish the synchronization "
                << "right away");

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

bool RemoteToLocalSynchronizationManager::
    checkAndRequestAuthenticationTokensForLinkedNotebooks()
{
    const int numAllLinkedNotebooks = m_allLinkedNotebooks.size();
    for (int i = 0; i < numAllLinkedNotebooks; ++i) {
        const LinkedNotebook & linkedNotebook = m_allLinkedNotebooks[i];
        if (!linkedNotebook.hasGuid()) {
            ErrorString error(
                QT_TR_NOOP("Internal error: found a linked notebook "
                           "without guid"));

            if (linkedNotebook.hasUsername()) {
                error.details() = linkedNotebook.username();
            }

            QNWARNING(
                "synchronization:remote_to_local",
                error << ", linked notebook: " << linkedNotebook);

            Q_EMIT failure(error);
            return false;
        }

        if (!m_authenticationTokensAndShardIdsByLinkedNotebookGuid.contains(
                linkedNotebook.guid()))
        {
            QNDEBUG(
                "synchronization:remote_to_local",
                "Authentication token "
                    << "for linked notebook with guid " << linkedNotebook.guid()
                    << " was not found; will "
                    << "request authentication tokens for all linked "
                    << "notebooks at once");

            requestAuthenticationTokensForAllLinkedNotebooks();
            return false;
        }

        auto it = m_authenticationTokenExpirationTimesByLinkedNotebookGuid.find(
            linkedNotebook.guid());

        if (it ==
            m_authenticationTokenExpirationTimesByLinkedNotebookGuid.end()) {
            ErrorString error(
                QT_TR_NOOP("Can't find the cached expiration time "
                           "of linked notebook's authentication token"));
            if (linkedNotebook.hasUsername()) {
                error.details() = linkedNotebook.username();
            }

            QNWARNING(
                "synchronization:remote_to_local",
                error << ", linked notebook: " << linkedNotebook);

            Q_EMIT failure(error);
            return false;
        }

        const qevercloud::Timestamp & expirationTime = it.value();
        const qevercloud::Timestamp currentTime =
            QDateTime::currentMSecsSinceEpoch();
        if ((expirationTime - currentTime) < HALF_AN_HOUR_IN_MSEC) {
            QNDEBUG(
                "synchronization:remote_to_local",
                "Authentication token "
                    << "for linked notebook with guid " << linkedNotebook.guid()
                    << " is too close to expiration: its expiration time is "
                    << printableDateTimeFromTimestamp(expirationTime)
                    << ", current time is "
                    << printableDateTimeFromTimestamp(currentTime)
                    << "; will request new authentication tokens "
                    << "for all linked notebooks");

            requestAuthenticationTokensForAllLinkedNotebooks();
            return false;
        }
    }

    QNDEBUG(
        "synchronization:remote_to_local",
        "Got authentication tokens for "
            << "all linked notebooks, can proceed with their synchronization");

    return true;
}

void RemoteToLocalSynchronizationManager::
    requestAuthenticationTokensForAllLinkedNotebooks()
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager"
            << "::requestAuthenticationTokensForAllLinkedNotebooks");

    QVector<LinkedNotebookAuthData> linkedNotebookAuthData;
    const int numAllLinkedNotebooks = m_allLinkedNotebooks.size();
    linkedNotebookAuthData.reserve(numAllLinkedNotebooks);

    for (int j = 0; j < numAllLinkedNotebooks; ++j) {
        const LinkedNotebook & currentLinkedNotebook = m_allLinkedNotebooks[j];

        if (!currentLinkedNotebook.hasGuid()) {
            ErrorString error(QT_TR_NOOP(
                "Internal error: found linked notebook without guid"));
            if (currentLinkedNotebook.hasUsername()) {
                error.details() = currentLinkedNotebook.username();
            }

            QNWARNING(
                "synchronization:remote_to_local",
                error << ", linked notebook: " << currentLinkedNotebook);

            Q_EMIT failure(error);
            return;
        }

        if (!currentLinkedNotebook.hasShardId()) {
            ErrorString error(
                QT_TR_NOOP("Internal error: found linked notebook "
                           "without shard id"));
            if (currentLinkedNotebook.hasUsername()) {
                error.details() = currentLinkedNotebook.username();
            }

            QNWARNING(
                "synchronization:remote_to_local",
                error << ", linked notebook: " << currentLinkedNotebook);

            Q_EMIT failure(error);
            return;
        }

        if (!currentLinkedNotebook.hasSharedNotebookGlobalId() &&
            !currentLinkedNotebook.hasUri())
        {
            ErrorString error(
                QT_TR_NOOP("Internal error: found linked notebook without "
                           "either shared notebook global id or uri"));
            if (currentLinkedNotebook.hasUsername()) {
                error.details() = currentLinkedNotebook.username();
            }

            QNWARNING(
                "synchronization:remote_to_local",
                error << ", linked notebook: " << currentLinkedNotebook);

            Q_EMIT failure(error);
            return;
        }

        if (!currentLinkedNotebook.hasNoteStoreUrl()) {
            ErrorString error(
                QT_TR_NOOP("Internal error: found linked notebook "
                           "without note store URL"));
            if (currentLinkedNotebook.hasUsername()) {
                error.details() = currentLinkedNotebook.username();
            }

            QNWARNING(
                "synchronization:remote_to_local",
                error << ", linked notebook: " << currentLinkedNotebook);

            Q_EMIT failure(error);
            return;
        }

        linkedNotebookAuthData << LinkedNotebookAuthData(
            currentLinkedNotebook.guid(), currentLinkedNotebook.shardId(),
            (currentLinkedNotebook.hasSharedNotebookGlobalId()
                 ? currentLinkedNotebook.sharedNotebookGlobalId()
                 : QString()),
            (currentLinkedNotebook.hasUri() ? currentLinkedNotebook.uri()
                                            : QString()),
            currentLinkedNotebook.noteStoreUrl());
    }

    m_pendingAuthenticationTokensForLinkedNotebooks = true;
    Q_EMIT requestAuthenticationTokensForLinkedNotebooks(
        linkedNotebookAuthData);
}

void RemoteToLocalSynchronizationManager::requestAllLinkedNotebooks()
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::requestAllLinkedNotebooks");

    size_t limit = 0, offset = 0;

    LocalStorageManager::ListLinkedNotebooksOrder order =
        LocalStorageManager::ListLinkedNotebooksOrder::NoOrder;

    LocalStorageManager::OrderDirection orderDirection =
        LocalStorageManager::OrderDirection::Ascending;

    m_listAllLinkedNotebooksRequestId = QUuid::createUuid();

    QNTRACE(
        "synchronization:remote_to_local",
        "Emitting the request to list "
            << "linked notebooks: request id = "
            << m_listAllLinkedNotebooksRequestId);

    Q_EMIT listAllLinkedNotebooks(
        limit, offset, order, orderDirection,
        m_listAllLinkedNotebooksRequestId);
}

void RemoteToLocalSynchronizationManager::getLinkedNotebookSyncState(
    const LinkedNotebook & linkedNotebook, const QString & authToken,
    qevercloud::SyncState & syncState, bool & asyncWait, bool & error)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::getLinkedNotebookSyncState");

    asyncWait = false;
    error = false;
    ErrorString errorDescription;

    if (Q_UNLIKELY(!linkedNotebook.hasGuid())) {
        errorDescription.setBase(QT_TR_NOOP("Linked notebook has no guid"));
        Q_EMIT failure(errorDescription);
        error = true;
        return;
    }

    auto * pNoteStore = m_manager.noteStoreForLinkedNotebook(linkedNotebook);
    if (Q_UNLIKELY(!pNoteStore)) {
        errorDescription.setBase(
            QT_TR_NOOP("Can't find or create note store "
                       "for the linked notebook"));
        Q_EMIT failure(errorDescription);
        error = true;
        return;
    }

    if (Q_UNLIKELY(pNoteStore->noteStoreUrl().isEmpty())) {
        errorDescription.setBase(
            QT_TR_NOOP("Internal error: empty note store url "
                       "for the linked notebook's note store"));
        Q_EMIT failure(errorDescription);
        error = true;
        return;
    }

    qint32 rateLimitSeconds = 0;
    qint32 errorCode = pNoteStore->getLinkedNotebookSyncState(
        linkedNotebook.qevercloudLinkedNotebook(), authToken, syncState,
        errorDescription, rateLimitSeconds);

    if (errorCode ==
        static_cast<qint32>(qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED))
    {
        if (rateLimitSeconds < 0) {
            errorDescription.setBase(
                QT_TR_NOOP("Rate limit reached but the number "
                           "of seconds to wait is incorrect"));
            errorDescription.details() = QString::number(rateLimitSeconds);
            Q_EMIT failure(errorDescription);
            error = true;
            return;
        }

        int timerId = startTimer(secondsToMilliseconds(rateLimitSeconds));
        if (Q_UNLIKELY(timerId == 0)) {
            ErrorString errorMessage(
                QT_TR_NOOP("Failed to start a timer to postpone the Evernote "
                           "API call due to rate limit exceeding"));
            errorMessage.additionalBases().append(errorDescription.base());
            errorMessage.additionalBases().append(
                errorDescription.additionalBases());
            errorMessage.details() = errorDescription.details();
            Q_EMIT failure(errorMessage);
            error = true;
            return;
        }

        m_getLinkedNotebookSyncStateBeforeStartAPICallPostponeTimerId = timerId;

        QNDEBUG(
            "synchronization:remote_to_local",
            "Rate limit exceeded, need "
                << "to wait for " << rateLimitSeconds << " seconds");
        Q_EMIT rateLimitExceeded(rateLimitSeconds);
        asyncWait = true;
        return;
    }
    else if (
        errorCode ==
        static_cast<qint32>(qevercloud::EDAMErrorCode::AUTH_EXPIRED))
    {
        ErrorString errorMessage(
            QT_TR_NOOP("Unexpected AUTH_EXPIRED error when trying to get "
                       "the linked notebook sync state"));
        errorMessage.additionalBases().append(errorDescription.base());
        errorMessage.additionalBases().append(
            errorDescription.additionalBases());
        errorMessage.details() = errorDescription.details();
        Q_EMIT failure(errorMessage);
        error = true;
        return;
    }
    else if (errorCode != 0) {
        ErrorString errorMessage(
            QT_TR_NOOP("Failed to get linked notebook sync state"));
        errorMessage.additionalBases().append(errorDescription.base());
        errorMessage.additionalBases().append(
            errorDescription.additionalBases());
        errorMessage.details() = errorDescription.details();
        Q_EMIT failure(errorMessage);
        error = true;
        return;
    }
}

bool RemoteToLocalSynchronizationManager::downloadLinkedNotebooksSyncChunks()
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager"
            << "::downloadLinkedNotebooksSyncChunks");

    qevercloud::SyncChunk * pSyncChunk = nullptr;

    const int numAllLinkedNotebooks = m_allLinkedNotebooks.size();
    for (int i = 0; i < numAllLinkedNotebooks; ++i) {
        const LinkedNotebook & linkedNotebook = m_allLinkedNotebooks[i];
        if (!linkedNotebook.hasGuid()) {
            ErrorString error(
                QT_TR_NOOP("Internal error: found linked notebook without guid "
                           "when trying to download the linked notebook sync "
                           "chunks"));
            if (linkedNotebook.hasUsername()) {
                error.details() = linkedNotebook.username();
            }

            QNWARNING(
                "synchronization:remote_to_local",
                error << ": " << linkedNotebook);
            Q_EMIT failure(error);
            return false;
        }

        const QString & linkedNotebookGuid = linkedNotebook.guid();
        bool fullSyncOnly = false;

        auto lastSyncTimeIt =
            m_lastSyncTimeByLinkedNotebookGuid.find(linkedNotebookGuid);

        if (lastSyncTimeIt == m_lastSyncTimeByLinkedNotebookGuid.end()) {
            lastSyncTimeIt = m_lastSyncTimeByLinkedNotebookGuid.insert(
                linkedNotebookGuid, 0);
        }

        qevercloud::Timestamp lastSyncTime = lastSyncTimeIt.value();

        auto lastUpdateCountIt =
            m_lastUpdateCountByLinkedNotebookGuid.find(linkedNotebookGuid);

        if (lastUpdateCountIt == m_lastUpdateCountByLinkedNotebookGuid.end()) {
            lastUpdateCountIt = m_lastUpdateCountByLinkedNotebookGuid.insert(
                linkedNotebookGuid, 0);
        }

        qint32 lastUpdateCount = lastUpdateCountIt.value();

        auto syncChunksDownloadedFlagIt =
            m_linkedNotebookGuidsForWhichSyncChunksWereDownloaded.find(
                linkedNotebookGuid);

        if (syncChunksDownloadedFlagIt !=
            m_linkedNotebookGuidsForWhichSyncChunksWereDownloaded.end())
        {
            QNDEBUG(
                "synchronization:remote_to_local",
                "Sync chunks were "
                    << "already downloaded for the linked notebook with guid "
                    << linkedNotebookGuid);
            continue;
        }

        qint32 afterUsn = lastUpdateCount;
        qint32 lastPreviousUsn = std::max(lastUpdateCount, 0);

        QNDEBUG(
            "synchronization:remote_to_local",
            "Last previous USN for "
                << "current linked notebook = " << lastPreviousUsn
                << " (linked notebook guid = " << linkedNotebookGuid << ")");

        if (m_onceSyncDone || (afterUsn != 0)) {
            auto syncStateIter =
                m_syncStatesByLinkedNotebookGuid.find(linkedNotebookGuid);

            if (syncStateIter == m_syncStatesByLinkedNotebookGuid.end()) {
                QNTRACE(
                    "synchronization:remote_to_local",
                    "Found no cached "
                        << "sync state for linked notebook guid "
                        << linkedNotebookGuid
                        << ", will try to receive it from "
                        << "the remote service");

                qevercloud::SyncState syncState;
                bool error = false;
                bool asyncWait = false;

                getLinkedNotebookSyncState(
                    linkedNotebook, m_authenticationToken, syncState, asyncWait,
                    error);

                if (asyncWait || error) {
                    QNTRACE(
                        "synchronization:remote_to_local",
                        "Async wait = " << (asyncWait ? "true" : "false")
                                        << ", error = "
                                        << (error ? "true" : "false"));
                    return false;
                }

                syncStateIter = m_syncStatesByLinkedNotebookGuid.insert(
                    linkedNotebookGuid, syncState);
            }

            const auto & syncState = syncStateIter.value();

            QNDEBUG(
                "synchronization:remote_to_local",
                "Sync state: " << syncState << "\nLast sync time = "
                               << printableDateTimeFromTimestamp(lastSyncTime)
                               << ", last update count = " << lastUpdateCount);

            if (syncState.fullSyncBefore > lastSyncTime) {
                QNDEBUG(
                    "synchronization:remote_to_local",
                    "Linked notebook "
                        << "sync state says the time has come to do the full "
                           "sync");
                afterUsn = 0;
                fullSyncOnly = true;
            }
            else if (syncState.updateCount == lastUpdateCount) {
                QNDEBUG(
                    "synchronization:remote_to_local",
                    "Server has no "
                        << "updates for data in this linked notebook, "
                           "continuing "
                        << "with the next one");

                Q_UNUSED(m_linkedNotebookGuidsForWhichSyncChunksWereDownloaded
                             .insert(linkedNotebookGuid));
                continue;
            }
        }

        auto * pNoteStore =
            m_manager.noteStoreForLinkedNotebook(linkedNotebook);
        if (Q_UNLIKELY(!pNoteStore)) {
            ErrorString error(
                QT_TR_NOOP("Can't find or create note store for "
                           "the linked notebook"));
            Q_EMIT failure(error);
            return false;
        }

        if (Q_UNLIKELY(pNoteStore->noteStoreUrl().isEmpty())) {
            ErrorString errorDescription(
                QT_TR_NOOP("Internal error: empty note store url for "
                           "the linked notebook's note store"));
            Q_EMIT failure(errorDescription);
            return false;
        }

        while (!pSyncChunk ||
               (pSyncChunk->chunkHighUSN < pSyncChunk->updateCount)) {
            if (pSyncChunk) {
                afterUsn = pSyncChunk->chunkHighUSN;
                QNTRACE(
                    "synchronization:remote_to_local",
                    "Updated afterUSN "
                        << "for linked notebook to sync chunk's high USN: "
                        << pSyncChunk->chunkHighUSN);
            }

            m_linkedNotebookSyncChunks.push_back(qevercloud::SyncChunk());
            pSyncChunk = &(m_linkedNotebookSyncChunks.back());

            ErrorString errorDescription;
            qint32 rateLimitSeconds = 0;

            qint32 errorCode = pNoteStore->getLinkedNotebookSyncChunk(
                linkedNotebook.qevercloudLinkedNotebook(), afterUsn,
                m_maxSyncChunksPerOneDownload, m_authenticationToken,
                fullSyncOnly, *pSyncChunk, errorDescription, rateLimitSeconds);

            if (errorCode ==
                static_cast<qint32>(
                    qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED))
            {
                if (rateLimitSeconds < 0) {
                    errorDescription.setBase(
                        QT_TR_NOOP("Rate limit reached but the number of "
                                   "seconds to wait is incorrect"));
                    errorDescription.details() =
                        QString::number(rateLimitSeconds);
                    Q_EMIT failure(errorDescription);
                    return false;
                }

                m_linkedNotebookSyncChunks.pop_back();

                int timerId =
                    startTimer(secondsToMilliseconds(rateLimitSeconds));
                if (Q_UNLIKELY(timerId == 0)) {
                    ErrorString errorMessage(
                        QT_TR_NOOP("Failed to start a timer to postpone "
                                   "the Evernote API call due to rate limit "
                                   "exceeding"));
                    errorMessage.additionalBases().append(
                        errorDescription.base());
                    errorMessage.additionalBases().append(
                        errorDescription.additionalBases());
                    errorMessage.details() = errorDescription.details();
                    Q_EMIT failure(errorMessage);
                    return false;
                }

                m_downloadLinkedNotebookSyncChunkAPICallPostponeTimerId =
                    timerId;

                QNDEBUG(
                    "synchronization:remote_to_local",
                    "Rate limit "
                        << "exceeded, need to wait for " << rateLimitSeconds
                        << " seconds");
                Q_EMIT rateLimitExceeded(rateLimitSeconds);
                return false;
            }
            else if (
                errorCode ==
                static_cast<qint32>(qevercloud::EDAMErrorCode::AUTH_EXPIRED))
            {
                ErrorString errorMessage(
                    QT_TR_NOOP("Unexpected AUTH_EXPIRED error when trying to "
                               "download the linked notebook sync chunks"));
                errorMessage.additionalBases().append(errorDescription.base());
                errorMessage.additionalBases().append(
                    errorDescription.additionalBases());
                errorMessage.details() = errorDescription.details();
                QNDEBUG("synchronization:remote_to_local", errorMessage);
                Q_EMIT failure(errorMessage);
                return false;
            }
            else if (errorCode != 0) {
                ErrorString errorMessage(
                    QT_TR_NOOP("Failed to download the sync chunks for linked "
                               "notebooks content"));
                errorMessage.additionalBases().append(errorDescription.base());
                errorMessage.additionalBases().append(
                    errorDescription.additionalBases());
                errorMessage.details() = errorDescription.details();
                QNDEBUG("synchronization:remote_to_local", errorMessage);
                Q_EMIT failure(errorMessage);
                return false;
            }

            QNDEBUG(
                "synchronization:remote_to_local",
                "Received sync chunk: " << *pSyncChunk);

            lastSyncTime = std::max(pSyncChunk->currentTime, lastSyncTime);
            lastUpdateCount =
                std::max(pSyncChunk->updateCount, lastUpdateCount);

            QNTRACE(
                "synchronization:remote_to_local",
                "Linked notebook's sync "
                    << "chunk current time: "
                    << printableDateTimeFromTimestamp(pSyncChunk->currentTime)
                    << ", last sync time = "
                    << printableDateTimeFromTimestamp(lastSyncTime)
                    << ", sync chunk update count = " << pSyncChunk->updateCount
                    << ", last update count = " << lastUpdateCount);

            Q_EMIT linkedNotebookSyncChunksDownloadProgress(
                pSyncChunk->chunkHighUSN, pSyncChunk->updateCount,
                lastPreviousUsn, linkedNotebook);

            if (pSyncChunk->tags.isSet()) {
                bool res = mapContainerElementsWithLinkedNotebookGuid<TagsList>(
                    linkedNotebookGuid, pSyncChunk->tags.ref());
                if (!res) {
                    return false;
                }
            }

            if (pSyncChunk->notebooks.isSet()) {
                bool res =
                    mapContainerElementsWithLinkedNotebookGuid<NotebooksList>(
                        linkedNotebookGuid, pSyncChunk->notebooks.ref());
                if (!res) {
                    return false;
                }
            }

            if (pSyncChunk->notes.isSet()) {
                bool res =
                    mapContainerElementsWithLinkedNotebookGuid<NotesList>(
                        linkedNotebookGuid, pSyncChunk->notes.ref());
                if (!res) {
                    return false;
                }
            }

            if (pSyncChunk->resources.isSet()) {
                bool res =
                    mapContainerElementsWithLinkedNotebookGuid<ResourcesList>(
                        linkedNotebookGuid, pSyncChunk->resources.ref());
                if (!res) {
                    return false;
                }
            }

            if (pSyncChunk->expungedTags.isSet()) {
                unmapContainerElementsFromLinkedNotebookGuid<qevercloud::Tag>(
                    pSyncChunk->expungedTags.ref());
            }

            if (pSyncChunk->expungedNotebooks.isSet()) {
                unmapContainerElementsFromLinkedNotebookGuid<
                    qevercloud::Notebook>(pSyncChunk->expungedNotebooks.ref());
            }
        }

        lastSyncTimeIt.value() = lastSyncTime;
        lastUpdateCountIt.value() = lastUpdateCount;

        Q_UNUSED(m_linkedNotebookGuidsForWhichSyncChunksWereDownloaded.insert(
            linkedNotebook.guid()));

        if (fullSyncOnly) {
            Q_UNUSED(m_linkedNotebookGuidsForWhichFullSyncWasPerformed.insert(
                linkedNotebook.guid()))
        }

        pSyncChunk = nullptr;
    }

    QNDEBUG(
        "synchronization:remote_to_local",
        "Done. Processing content "
            << "pointed to by linked notebooks from buffered sync chunks");

    // don't need this anymore, it only served the purpose of preventing
    // multiple get sync state calls for the same linked notebook
    m_syncStatesByLinkedNotebookGuid.clear();

    m_linkedNotebooksSyncChunksDownloaded = true;
    Q_EMIT linkedNotebooksSyncChunksDownloaded();

    return true;
}

void RemoteToLocalSynchronizationManager::launchLinkedNotebooksTagsSync()
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager"
            << "::launchLinkedNotebooksTagsSync");

    m_pendingTagsSyncStart = false;
    QList<QString> dummyList;
    launchDataElementSync<TagsContainer, Tag>(
        ContentSource::LinkedNotebook, QStringLiteral("Tag"), m_tags,
        dummyList);
}

void RemoteToLocalSynchronizationManager::launchLinkedNotebooksNotebooksSync()
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager"
            << "::launchLinkedNotebooksNotebooksSync");

    m_pendingNotebooksSyncStart = false;

    QList<QString> dummyList;
    launchDataElementSync<NotebooksList, Notebook>(
        ContentSource::LinkedNotebook, QStringLiteral("Notebook"), m_notebooks,
        dummyList);
}

void RemoteToLocalSynchronizationManager::checkServerDataMergeCompletion()
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager"
            << "::checkServerDataMergeCompletion");

    // Need to check whether we are still waiting for the response
    // from some add or update request
    bool tagsReady = !m_pendingTagsSyncStart &&
        m_tagsPendingProcessing.isEmpty() &&
        m_tagsPendingAddOrUpdate.isEmpty() &&
        m_findTagByGuidRequestIds.isEmpty() &&
        m_findTagByNameRequestIds.isEmpty() &&
        m_updateTagRequestIds.isEmpty() && m_addTagRequestIds.isEmpty();

    if (!tagsReady) {
        QNDEBUG(
            "synchronization:remote_to_local",
            "Tags are not ready, "
                << "pending tags sync start = "
                << (m_pendingTagsSyncStart ? "true" : "false") << "; there are "
                << m_tagsPendingProcessing.size()
                << " tags pending processing and/or "
                << m_tagsPendingAddOrUpdate.size()
                << " tags pending add or update within "
                << "the local storage: pending response for "
                << m_updateTagRequestIds.size()
                << " tag update requests and/or " << m_addTagRequestIds.size()
                << " tag add requests and/or "
                << m_findTagByGuidRequestIds.size()
                << " find tag by guid requests and/or "
                << m_findTagByNameRequestIds.size()
                << " find tag by name requests");
        return;
    }

    bool searchesReady = m_savedSearches.isEmpty() &&
        m_savedSearchesPendingAddOrUpdate.isEmpty() &&
        m_findSavedSearchByGuidRequestIds.isEmpty() &&
        m_findSavedSearchByNameRequestIds.isEmpty() &&
        m_updateSavedSearchRequestIds.isEmpty() &&
        m_addSavedSearchRequestIds.isEmpty();

    if (!searchesReady) {
        QNDEBUG(
            "synchronization:remote_to_local",
            "Saved searches are not "
                << "ready, there are " << m_savedSearches.size()
                << " saved searches pending processing and/or "
                << m_savedSearchesPendingAddOrUpdate.size()
                << " saved searches pending add or update within "
                << "the local storage: pending response for "
                << m_updateSavedSearchRequestIds.size()
                << " saved search update requests and/or "
                << m_addSavedSearchRequestIds.size()
                << " saved search add requests and/or "
                << m_findSavedSearchByGuidRequestIds.size()
                << " find saved search by guid requests and/or "
                << m_findSavedSearchByNameRequestIds.size()
                << " find saved search by name requests");
        return;
    }

    bool linkedNotebooksReady = !m_pendingLinkedNotebooksSyncStart &&
        m_linkedNotebooks.isEmpty() &&
        m_linkedNotebooksPendingAddOrUpdate.isEmpty() &&
        m_findLinkedNotebookRequestIds.isEmpty() &&
        m_updateLinkedNotebookRequestIds.isEmpty() &&
        m_addLinkedNotebookRequestIds.isEmpty();

    if (!linkedNotebooksReady) {
        QNDEBUG(
            "synchronization:remote_to_local",
            "Linked notebooks are not "
                << "ready, pending linked notebooks sync start = "
                << (m_pendingLinkedNotebooksSyncStart ? "true" : "false")
                << "; there are " << m_linkedNotebooks.size()
                << " linked notebooks pending processing and/or "
                << m_linkedNotebooksPendingAddOrUpdate.size()
                << " linked notebooks pending add or update within "
                << "the local storage: pending response for "
                << m_updateLinkedNotebookRequestIds.size()
                << " linked notebook update requests and/or "
                << m_addLinkedNotebookRequestIds.size()
                << " linked notebook add requests and/or "
                << m_findLinkedNotebookRequestIds.size()
                << " find linked notebook requests");
        return;
    }

    bool notebooksReady = !m_pendingNotebooksSyncStart &&
        m_notebooks.isEmpty() && m_notebooksPendingAddOrUpdate.isEmpty() &&
        m_findNotebookByGuidRequestIds.isEmpty() &&
        m_findNotebookByNameRequestIds.isEmpty() &&
        m_updateNotebookRequestIds.isEmpty() &&
        m_addNotebookRequestIds.isEmpty();

    if (!notebooksReady) {
        QNDEBUG(
            "synchronization:remote_to_local",
            "Notebooks are not ready, "
                << "pending notebooks sync start = "
                << (m_pendingNotebooksSyncStart ? "true" : "false")
                << "; there are " << m_notebooks.size()
                << " notebooks pending processing and/or "
                << m_notebooksPendingAddOrUpdate.size()
                << " notebooks pending add or update within "
                << "the local storage: pending response for "
                << m_updateNotebookRequestIds.size()
                << " notebook update requests and/or "
                << m_addNotebookRequestIds.size()
                << " notebook add requests and/or "
                << m_findNotebookByGuidRequestIds.size()
                << " find notebook by guid requests and/or "
                << m_findNotebookByNameRequestIds.size()
                << " find notebook by name requests");
        return;
    }

    bool notesReady = m_notes.isEmpty() &&
        m_notesPendingAddOrUpdate.isEmpty() &&
        m_findNoteByGuidRequestIds.isEmpty() &&
        m_updateNoteRequestIds.isEmpty() && m_addNoteRequestIds.isEmpty() &&
        m_notesPendingDownloadForAddingToLocalStorage.isEmpty() &&
        m_notesPendingDownloadForUpdatingInLocalStorageByGuid.isEmpty() &&
        m_notesToAddPerAPICallPostponeTimerId.isEmpty() &&
        m_notesToUpdatePerAPICallPostponeTimerId.isEmpty() &&
        m_resourceGuidsPendingInkNoteImageDownloadPerNoteGuid.isEmpty() &&
        m_notesPendingInkNoteImagesDownloadByFindNotebookRequestId.isEmpty() &&
        m_notesPendingThumbnailDownloadByFindNotebookRequestId.isEmpty() &&
        m_notesPendingThumbnailDownloadByGuid.isEmpty() &&
        m_updateNoteWithThumbnailRequestIds.isEmpty();

    if (!notesReady) {
        QNDEBUG(
            "synchronization:remote_to_local",
            "Notes are not ready, "
                << "there are " << m_notes.size()
                << " notes pending processing and/or "
                << m_notesPendingAddOrUpdate.size()
                << " notes pending add or update within "
                << "the local storage: pending response for "
                << m_updateNoteRequestIds.size()
                << " note update requests and/or " << m_addNoteRequestIds.size()
                << " note add requests and/or "
                << m_findNoteByGuidRequestIds.size()
                << " find note by guid requests and/or "
                << m_notesPendingDownloadForAddingToLocalStorage.size()
                << " async full new note data downloads and/or "
                << m_notesPendingDownloadForUpdatingInLocalStorageByGuid.size()
                << " async full existing note data downloads; also, there are "
                << m_notesToAddPerAPICallPostponeTimerId.size()
                << " postponed note add requests and/or "
                << m_notesToUpdatePerAPICallPostponeTimerId.size()
                << " postponed note update requests and/or "
                << m_resourceGuidsPendingInkNoteImageDownloadPerNoteGuid.size()
                << " note resources pending ink note image download "
                << "processing and/or "
                << m_notesPendingInkNoteImagesDownloadByFindNotebookRequestId
                       .size()
                << " find notebook requests for ink note image "
                << "download processing and/or "
                << m_notesPendingThumbnailDownloadByFindNotebookRequestId.size()
                << " find notebook requests for note thumbnail "
                << "download processing and/or "
                << m_notesPendingThumbnailDownloadByGuid.size()
                << " note thumbnail downloads and/or "
                << m_updateNoteWithThumbnailRequestIds.size()
                << " update note with downloaded thumbnails requests");
        return;
    }

    if (m_lastSyncMode == SyncMode::IncrementalSync) {
        bool resourcesReady = m_resources.isEmpty() &&
            m_resourcesPendingAddOrUpdate.isEmpty() &&
            m_findResourceByGuidRequestIds.isEmpty() &&
            m_updateResourceRequestIds.isEmpty() &&
            m_resourcesByMarkNoteOwningResourceDirtyRequestIds.isEmpty() &&
            m_addResourceRequestIds.isEmpty() &&
            m_resourcesByFindNoteRequestIds.isEmpty() &&
            m_inkNoteResourceDataPerFindNotebookRequestId.isEmpty() &&
            m_resourcesPendingDownloadForAddingToLocalStorageWithNotesByResourceGuid
                .isEmpty() &&
            m_resourcesPendingDownloadForUpdatingInLocalStorageWithNotesByResourceGuid
                .isEmpty() &&
            m_resourcesToAddWithNotesPerAPICallPostponeTimerId.isEmpty() &&
            m_resourcesToUpdateWithNotesPerAPICallPostponeTimerId.isEmpty() &&
            m_postponedConflictingResourceDataPerAPICallPostponeTimerId
                .isEmpty();

        if (!resourcesReady) {
            QNDEBUG(
                "synchronization:remote_to_local",
                "Resources are not "
                    << "ready, there are " << m_resources.size()
                    << " resources pending processing and/or "
                    << m_resourcesPendingAddOrUpdate.size()
                    << " resources pending add or update within "
                    << "the local storage: pending response for "
                    << m_updateResourceRequestIds.size()
                    << " resource update requests and/or "
                    << m_resourcesByMarkNoteOwningResourceDirtyRequestIds.size()
                    << " mark note owning resource as dirty requests and/or "
                    << m_addResourceRequestIds.size()
                    << " resource add requests and/or "
                    << m_resourcesByFindNoteRequestIds.size()
                    << " find note for resource requests and/or "
                    << m_findResourceByGuidRequestIds.size()
                    << " find resource requests and/or "
                    << m_inkNoteResourceDataPerFindNotebookRequestId.size()
                    << " resource find notebook for ink note image "
                    << "download processing and/or "
                    << m_resourcesPendingDownloadForAddingToLocalStorageWithNotesByResourceGuid
                           .size()
                    << " async full new resource data downloads and/or "
                    << m_resourcesPendingDownloadForUpdatingInLocalStorageWithNotesByResourceGuid
                           .size()
                    << " async full existing resource data downloads and/or "
                    << m_resourcesToAddWithNotesPerAPICallPostponeTimerId.size()
                    << " postponed resource add requests and/or "
                    << m_resourcesToUpdateWithNotesPerAPICallPostponeTimerId
                           .size()
                    << " postponed resource update requests and/or "
                    << m_postponedConflictingResourceDataPerAPICallPostponeTimerId
                           .size()
                    << " postponed resource conflict resolutions");
            return;
        }
    }

    // Also need to check if we are still waiting for some sync conflict
    // resolvers to finish

    auto notebookSyncConflictResolvers =
        findChildren<NotebookSyncConflictResolver *>();

    if (!notebookSyncConflictResolvers.isEmpty()) {
        QNDEBUG(
            "synchronization:remote_to_local",
            "Still have " << notebookSyncConflictResolvers.size()
                          << " pending notebook sync conflict resolutions");
        return;
    }

    auto tagSyncConflictResolvers = findChildren<TagSyncConflictResolver *>();

    if (!tagSyncConflictResolvers.isEmpty()) {
        QNDEBUG(
            "synchronization:remote_to_local",
            "Still have " << tagSyncConflictResolvers.size()
                          << " pending tag sync conflict resolutions");
        return;
    }

    auto savedSearchSyncConflictResolvers =
        findChildren<SavedSearchSyncConflictResolver *>();

    if (!savedSearchSyncConflictResolvers.isEmpty()) {
        QNDEBUG(
            "synchronization:remote_to_local",
            "Still have " << savedSearchSyncConflictResolvers.size()
                          << " pending saved search sync conflict resolutions");
        return;
    }

    auto noteSyncConflictResolvers = findChildren<NoteSyncConflictResolver *>();
    if (!noteSyncConflictResolvers.isEmpty()) {
        QNDEBUG(
            "synchronization:remote_to_local",
            "Still have " << noteSyncConflictResolvers.size()
                          << " pending note sync conflict resolutions");
        return;
    }

    if (syncingLinkedNotebooksContent()) {
        if (!m_listAllLinkedNotebooksRequestId.isNull()) {
            QNDEBUG(
                "synchronization:remote_to_local",
                "Pending list of all "
                    << "linked notebooks to actually start the linked "
                       "notebooks "
                    << "sync");
            return;
        }

        if (!m_linkedNotebookGuidsPendingTagSyncCachesFill.isEmpty()) {
            QNDEBUG(
                "synchronization:remote_to_local",
                "Pending TagSyncCache "
                    << "fill for some linked notebooks to actually start the "
                       "sync "
                    << "of tags from linked notebooks");
            return;
        }

        QNDEBUG(
            "synchronization:remote_to_local",
            "Synchronized the whole "
                << "contents from linked notebooks");

        if (!m_expungedNotes.isEmpty()) {
            expungeNotes();
            return;
        }

        if (launchFullSyncStaleDataItemsExpungersForLinkedNotebooks()) {
            return;
        }

        launchExpungingOfNotelessTagsFromLinkedNotebooks();
    }
    else {
        QNDEBUG(
            "synchronization:remote_to_local",
            "Synchronized the whole "
                << "contents from user's account");

        m_fullNoteContentsDownloaded = true;

        Q_EMIT synchronizedContentFromUsersOwnAccount(
            m_lastUpdateCount, m_lastSyncTime);

        if (m_lastSyncMode == SyncMode::FullSync) {
            if (m_onceSyncDone) {
                QNDEBUG(
                    "synchronization:remote_to_local",
                    "Performed full "
                        << "sync even though it has been performed at some "
                           "moment "
                        << "in the past; need to check for stale data items "
                           "left "
                        << "within the local storage and expunge them");
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
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::finalize: "
            << "last update count = " << m_lastUpdateCount
            << ", last sync time = "
            << printableDateTimeFromTimestamp(m_lastSyncTime));

    if (QuentierIsLogLevelActive(LogLevel::Trace)) {
        QNTRACE(
            "synchronization:remote_to_local",
            "Last update counts by "
                << "linked notebook guids: ");

        for (const auto it: qevercloud::toRange(
                 qAsConst(m_lastUpdateCountByLinkedNotebookGuid)))
        {
            QNTRACE(
                "synchronization:remote_to_local",
                "guid = " << it.key()
                          << ", last update count = " << it.value());
        }

        QNTRACE(
            "synchronization:remote_to_local",
            "Last sync times by linked "
                << "notebook guids: ");

        for (const auto it:
             qevercloud::toRange(qAsConst(m_lastSyncTimeByLinkedNotebookGuid)))
        {
            QNTRACE(
                "synchronization:remote_to_local",
                "guid = " << it.key() << ", last sync time = "
                          << printableDateTimeFromTimestamp(it.value()));
        }
    }

    m_onceSyncDone = true;

    Q_EMIT finished(
        m_lastUpdateCount, m_lastSyncTime,
        m_lastUpdateCountByLinkedNotebookGuid,
        m_lastSyncTimeByLinkedNotebookGuid);

    clear();
    disconnectFromLocalStorage();
    m_active = false;
}

void RemoteToLocalSynchronizationManager::clear()
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::clear");

    disconnectFromLocalStorage();

    // NOTE: not clearing m_host: it can be reused in later syncs

    m_lastUsnOnStart = -1;
    m_lastSyncChunksDownloadedUsn = -1;

    m_syncChunksDownloaded = false;
    m_fullNoteContentsDownloaded = false;
    m_expungedFromServerToClient = false;
    m_linkedNotebooksSyncChunksDownloaded = false;

    m_active = false;

    // NOTE: not clearing m_edamProtocolVersionChecked flag: it can be reused
    // in later syncs

    m_syncChunks.clear();
    m_linkedNotebookSyncChunks.clear();
    m_linkedNotebookGuidsForWhichSyncChunksWereDownloaded.clear();

    // NOTE: not clearing m_accountLimits: it can be reused in later syncs

    m_tags.clear();
    m_tagsPendingProcessing.clear();
    m_tagsPendingAddOrUpdate.clear();
    m_expungedTags.clear();
    m_findTagByNameRequestIds.clear();
    m_linkedNotebookGuidsByFindTagByNameRequestIds.clear();
    m_findTagByGuidRequestIds.clear();
    m_addTagRequestIds.clear();
    m_updateTagRequestIds.clear();
    m_expungeTagRequestIds.clear();
    m_pendingTagsSyncStart = false;

    auto tagSyncConflictResolvers = findChildren<TagSyncConflictResolver *>();
    for (auto * pResolver: ::qAsConst(tagSyncConflictResolvers)) {
        if (Q_UNLIKELY(!pResolver)) {
            continue;
        }

        pResolver->disconnect();
        pResolver->setParent(nullptr);
        pResolver->deleteLater();
    }

    m_tagSyncCache.clear();

    for (auto * pCache: ::qAsConst(m_tagSyncCachesByLinkedNotebookGuids)) {
        if (Q_UNLIKELY(!pCache)) {
            continue;
        }

        pCache->disconnect();
        pCache->setParent(nullptr);
        pCache->deleteLater();
    }

    m_tagSyncCachesByLinkedNotebookGuids.clear();
    m_linkedNotebookGuidsPendingTagSyncCachesFill.clear();

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

    auto savedSearchSyncConflictResolvers =
        findChildren<SavedSearchSyncConflictResolver *>();

    for (auto * pResolver: qAsConst(savedSearchSyncConflictResolvers)) {
        if (Q_UNLIKELY(!pResolver)) {
            continue;
        }

        pResolver->disconnect();
        pResolver->setParent(nullptr);
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

    // NOTE: not clearing authentication token, shard id + auth token's
    // expiration time: this information can be reused in later syncs

    m_pendingAuthenticationTokenAndShardId = false;

    // NOTE: not clearing m_user: this information can be reused in subsequent
    // syncs

    m_findUserRequestId = QUuid();
    m_addOrUpdateUserRequestId = QUuid();
    m_onceAddedOrUpdatedUserInLocalStorage = false;

    // NOTE: not clearing auth tokens, shard ids and auth tokens' expiration
    // times for linked notebooks: this information can be reused in later syncs

    m_pendingAuthenticationTokensForLinkedNotebooks = false;

    m_syncStatesByLinkedNotebookGuid.clear();

    // NOTE: not clearing last synchronized USNs, sync times and update counts
    // by linked notebook guid: this information can be reused in subsequent
    // syncs

    m_notebooks.clear();
    m_notebooksPendingAddOrUpdate.clear();
    m_expungedNotebooks.clear();
    m_findNotebookByNameRequestIds.clear();
    m_linkedNotebookGuidsByFindNotebookByNameRequestIds.clear();
    m_findNotebookByGuidRequestIds.clear();
    m_addNotebookRequestIds.clear();
    m_updateNotebookRequestIds.clear();
    m_expungeNotebookRequestIds.clear();
    m_pendingNotebooksSyncStart = false;

    auto notebookSyncConflictResolvers =
        findChildren<NotebookSyncConflictResolver *>();

    for (auto * pResolver: qAsConst(notebookSyncConflictResolvers)) {
        if (Q_UNLIKELY(!pResolver)) {
            continue;
        }

        pResolver->disconnect();
        pResolver->setParent(nullptr);
        pResolver->deleteLater();
    }

    m_notebookSyncCache.clear();

    for (auto * pCache: ::qAsConst(m_notebookSyncCachesByLinkedNotebookGuids)) {
        if (Q_UNLIKELY(!pCache)) {
            continue;
        }

        pCache->disconnect();
        pCache->setParent(nullptr);
        pCache->deleteLater();
    }

    m_notebookSyncCachesByLinkedNotebookGuids.clear();

    m_linkedNotebookGuidsByNotebookGuids.clear();
    m_linkedNotebookGuidsByResourceGuids.clear();

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

    m_resourcesByFindNoteRequestIds.clear();
    m_inkNoteResourceDataPerFindNotebookRequestId.clear();
    m_resourceGuidsPendingInkNoteImageDownloadPerNoteGuid.clear();
    m_resourceGuidsPendingFindNotebookForInkNoteImageDownloadPerNoteGuid
        .clear();

    m_notesPendingInkNoteImagesDownloadByFindNotebookRequestId.clear();
    m_notesPendingThumbnailDownloadByFindNotebookRequestId.clear();
    m_notesPendingThumbnailDownloadByGuid.clear();
    m_updateNoteWithThumbnailRequestIds.clear();

    m_guidsOfResourcesFoundWithinTheLocalStorage.clear();
    m_localUidsOfElementsAlreadyAttemptedToFindByName.clear();

    m_notesPendingDownloadForAddingToLocalStorage.clear();
    m_notesPendingDownloadForUpdatingInLocalStorageByGuid.clear();

    m_resourcesPendingDownloadForAddingToLocalStorageWithNotesByResourceGuid
        .clear();
    m_resourcesPendingDownloadForUpdatingInLocalStorageWithNotesByResourceGuid
        .clear();

    m_fullSyncStaleDataItemsSyncedGuids.m_syncedNotebookGuids.clear();
    m_fullSyncStaleDataItemsSyncedGuids.m_syncedTagGuids.clear();
    m_fullSyncStaleDataItemsSyncedGuids.m_syncedNoteGuids.clear();
    m_fullSyncStaleDataItemsSyncedGuids.m_syncedSavedSearchGuids.clear();

    if (m_pFullSyncStaleDataItemsExpunger) {
        junkFullSyncStaleDataItemsExpunger(*m_pFullSyncStaleDataItemsExpunger);
        m_pFullSyncStaleDataItemsExpunger = nullptr;
    }

    for (auto * pExpunger:
         ::qAsConst(m_fullSyncStaleDataItemsExpungersByLinkedNotebookGuid))
    {
        if (pExpunger) {
            junkFullSyncStaleDataItemsExpunger(*pExpunger);
        }
    }

    m_fullSyncStaleDataItemsExpungersByLinkedNotebookGuid.clear();

    for (const auto it:
         qevercloud::toRange(m_notesToAddPerAPICallPostponeTimerId)) {
        int key = it.key();
        killTimer(key);
    }
    m_notesToAddPerAPICallPostponeTimerId.clear();

    for (const auto it:
         qevercloud::toRange(m_notesToUpdatePerAPICallPostponeTimerId))
    {
        int key = it.key();
        killTimer(key);
    }
    m_notesToUpdatePerAPICallPostponeTimerId.clear();

    for (const auto it: qevercloud::toRange(
             m_resourcesToAddWithNotesPerAPICallPostponeTimerId))
    {
        int key = it.key();
        killTimer(key);
    }
    m_resourcesToAddWithNotesPerAPICallPostponeTimerId.clear();

    for (const auto it: qevercloud::toRange(
             m_resourcesToUpdateWithNotesPerAPICallPostponeTimerId))
    {
        int key = it.key();
        killTimer(key);
    }
    m_resourcesToUpdateWithNotesPerAPICallPostponeTimerId.clear();

    for (const auto it: qevercloud::toRange(
             m_postponedConflictingResourceDataPerAPICallPostponeTimerId))
    {
        int key = it.key();
        killTimer(key);
    }
    m_postponedConflictingResourceDataPerAPICallPostponeTimerId.clear();

    for (const auto it:
         qevercloud::toRange(m_afterUsnForSyncChunkPerAPICallPostponeTimerId))
    {
        int key = it.key();
        killTimer(key);
    }
    m_afterUsnForSyncChunkPerAPICallPostponeTimerId.clear();

    if (m_getLinkedNotebookSyncStateBeforeStartAPICallPostponeTimerId != 0) {
        killTimer(
            m_getLinkedNotebookSyncStateBeforeStartAPICallPostponeTimerId);
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

    // NOTE: not clearing m_gotLastSyncParameters: this information can be
    // reused in subsequent syncs

    auto noteThumbnailDownloaders = findChildren<NoteThumbnailDownloader *>();
    for (auto * pDownloader: qAsConst(noteThumbnailDownloaders)) {
        if (Q_UNLIKELY(!pDownloader)) {
            continue;
        }

        QObject::disconnect(
            pDownloader, &NoteThumbnailDownloader::finished, this,
            &RemoteToLocalSynchronizationManager::
                onNoteThumbnailDownloadingFinished);

        pDownloader->setParent(nullptr);
        pDownloader->deleteLater();
    }

    auto inkNoteImagesDownloaders = findChildren<InkNoteImageDownloader *>();
    for (auto * pDownloader: qAsConst(inkNoteImagesDownloaders)) {
        if (Q_UNLIKELY(!pDownloader)) {
            continue;
        }

        QObject::disconnect(
            pDownloader, &InkNoteImageDownloader::finished, this,
            &RemoteToLocalSynchronizationManager::
                onInkNoteImageDownloadFinished);

        pDownloader->setParent(nullptr);
        pDownloader->deleteLater();
    }
}

void RemoteToLocalSynchronizationManager::clearAll()
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::clearAll");

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

void RemoteToLocalSynchronizationManager::handleLinkedNotebookAdded(
    const LinkedNotebook & linkedNotebook)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::handleLinkedNotebookAdded: "
            << "linked notebook = " << linkedNotebook);

    unregisterLinkedNotebookPendingAddOrUpdate(linkedNotebook);

    if (!m_allLinkedNotebooksListed) {
        return;
    }

    if (!linkedNotebook.hasGuid()) {
        QNWARNING(
            "synchronization:remote_to_local",
            "Detected the addition of "
                << "linked notebook without guid to local storage!");
        return;
    }

    auto it = std::find_if(
        m_allLinkedNotebooks.begin(), m_allLinkedNotebooks.end(),
        CompareItemByGuid<LinkedNotebook>(linkedNotebook.guid()));

    if (it != m_allLinkedNotebooks.end()) {
        QNINFO(
            "synchronization:remote_to_local",
            "Detected the addition of "
                << "linked notebook to the local storage, however such linked "
                << "notebook is already present within the list of all linked "
                << "notebooks received previously from local storage");
        *it = linkedNotebook;
        return;
    }

    m_allLinkedNotebooks << linkedNotebook;
}

void RemoteToLocalSynchronizationManager::handleLinkedNotebookUpdated(
    const LinkedNotebook & linkedNotebook)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::handleLinkedNotebookUpdated: "
            << "linked notebook = " << linkedNotebook);

    unregisterLinkedNotebookPendingAddOrUpdate(linkedNotebook);

    if (!m_allLinkedNotebooksListed) {
        return;
    }

    if (!linkedNotebook.hasGuid()) {
        QNWARNING(
            "synchronization:remote_to_local",
            "Detected the updated "
                << "linked notebook without guid in local storage!");
        return;
    }

    auto it = std::find_if(
        m_allLinkedNotebooks.begin(), m_allLinkedNotebooks.end(),
        CompareItemByGuid<LinkedNotebook>(linkedNotebook.guid()));

    if (it == m_allLinkedNotebooks.end()) {
        QNINFO(
            "synchronization:remote_to_local",
            "Detected the update of "
                << "linked notebook to the local storage, however such linked "
                << "notebook is not present within the list of all linked "
                << "notebooks received previously from local storage");
        m_allLinkedNotebooks << linkedNotebook;
        return;
    }

    *it = linkedNotebook;
}

void RemoteToLocalSynchronizationManager::timerEvent(QTimerEvent * pEvent)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::timerEvent");

    if (Q_UNLIKELY(!pEvent)) {
        ErrorString errorDescription(
            QT_TR_NOOP("Qt error: detected null pointer to QTimerEvent"));
        QNWARNING("synchronization:remote_to_local", errorDescription);
        Q_EMIT failure(errorDescription);
        return;
    }

    int timerId = pEvent->timerId();
    killTimer(timerId);

    QNDEBUG(
        "synchronization:remote_to_local", "Killed timer with id " << timerId);

    auto noteToAddIt = m_notesToAddPerAPICallPostponeTimerId.find(timerId);
    if (noteToAddIt != m_notesToAddPerAPICallPostponeTimerId.end()) {
        Note note = noteToAddIt.value();
        Q_UNUSED(m_notesToAddPerAPICallPostponeTimerId.erase(noteToAddIt));
        getFullNoteDataAsyncAndAddToLocalStorage(note);
        return;
    }

    auto noteToUpdateIt =
        m_notesToUpdatePerAPICallPostponeTimerId.find(timerId);
    if (noteToUpdateIt != m_notesToUpdatePerAPICallPostponeTimerId.end()) {
        Note noteToUpdate = noteToUpdateIt.value();
        Q_UNUSED(
            m_notesToUpdatePerAPICallPostponeTimerId.erase(noteToUpdateIt));
        registerNotePendingAddOrUpdate(noteToUpdate);
        getFullNoteDataAsyncAndUpdateInLocalStorage(noteToUpdate);
        return;
    }

    auto resourceToAddIt =
        m_resourcesToAddWithNotesPerAPICallPostponeTimerId.find(timerId);
    if (resourceToAddIt !=
        m_resourcesToAddWithNotesPerAPICallPostponeTimerId.end()) {
        std::pair<Resource, Note> pair = resourceToAddIt.value();
        Q_UNUSED(m_resourcesToAddWithNotesPerAPICallPostponeTimerId.erase(
            resourceToAddIt))
        getFullResourceDataAsyncAndAddToLocalStorage(pair.first, pair.second);
        return;
    }

    auto resourceToUpdateIt =
        m_resourcesToUpdateWithNotesPerAPICallPostponeTimerId.find(timerId);
    if (resourceToUpdateIt !=
        m_resourcesToUpdateWithNotesPerAPICallPostponeTimerId.end())
    {
        std::pair<Resource, Note> pair = resourceToUpdateIt.value();
        Q_UNUSED(m_resourcesToUpdateWithNotesPerAPICallPostponeTimerId.erase(
            resourceToUpdateIt))
        getFullResourceDataAsyncAndUpdateInLocalStorage(
            pair.first, pair.second);
        return;
    }

    auto conflictingResourceDataIt =
        m_postponedConflictingResourceDataPerAPICallPostponeTimerId.find(
            timerId);
    if (conflictingResourceDataIt !=
        m_postponedConflictingResourceDataPerAPICallPostponeTimerId.end())
    {
        PostponedConflictingResourceData data =
            conflictingResourceDataIt.value();
        Q_UNUSED(
            m_postponedConflictingResourceDataPerAPICallPostponeTimerId.erase(
                conflictingResourceDataIt))
        processResourceConflictAsNoteConflict(
            data.m_remoteNote, data.m_localConflictingNote,
            data.m_remoteNoteResourceWithoutFullData);
        return;
    }

    auto afterUsnIt =
        m_afterUsnForSyncChunkPerAPICallPostponeTimerId.find(timerId);
    if (afterUsnIt != m_afterUsnForSyncChunkPerAPICallPostponeTimerId.end()) {
        qint32 afterUsn = afterUsnIt.value();
        Q_UNUSED(
            m_afterUsnForSyncChunkPerAPICallPostponeTimerId.erase(afterUsnIt));
        downloadSyncChunksAndLaunchSync(afterUsn);
        return;
    }

    if (m_getLinkedNotebookSyncStateBeforeStartAPICallPostponeTimerId ==
        timerId) {
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

void RemoteToLocalSynchronizationManager::getFullNoteDataAsync(
    const Note & note)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::getFullNoteDataAsync: " << note);

    if (!note.hasGuid()) {
        ErrorString errorDescription(
            QT_TR_NOOP("Detected the attempt to get full "
                       "note's data for a note without guid"));
        APPEND_NOTE_DETAILS(errorDescription, note)
        QNWARNING(
            "synchronization:remote_to_local",
            errorDescription << ": " << note);
        Q_EMIT failure(errorDescription);
        return;
    }

    if (!note.hasNotebookGuid() && syncingLinkedNotebooksContent()) {
        ErrorString errorDescription(
            QT_TR_NOOP("Detected the attempt to get full note's data for "
                       "a note without notebook guid while syncing linked "
                       "notebooks content"));
        APPEND_NOTE_DETAILS(errorDescription, note)
        QNWARNING(
            "synchronization:remote_to_local",
            errorDescription << ": " << note);
        Q_EMIT failure(errorDescription);
        return;
    }

    QString authToken;
    ErrorString errorDescription;
    auto * pNoteStore = noteStoreForNote(note, authToken, errorDescription);
    if (Q_UNLIKELY(!pNoteStore)) {
        QNWARNING("synchronization:remote_to_local", errorDescription);
        Q_EMIT failure(errorDescription);
        return;
    }

    if (authToken.isEmpty()) {
        /**
         * Empty authentication tokens should correspond to public linked
         * notebooks; the official Evernote documentation
         * (dev.evernote.com/media/pdf/edam-sync.pdf) says in this case
         * the authentication token is not required, however, that is a lie,
         * with empty authentication token EDAMUserException is thrown with
         * PERMISSION_DENIED error code; instead for public notebooks
         * the authentication token from the primary account should be used
         */
        QNDEBUG(
            "synchronization:remote_to_local",
            "No auth token for public "
                << "linked notebook, will use the account's default auth "
                   "token");
        authToken = m_authenticationToken;
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

    bool res = pNoteStore->getNoteAsync(
        withContent, withResourceData, withResourceRecognition,
        withResourceAlternateData, withSharedNotes, withNoteAppDataValues,
        withResourceAppDataValues, withNoteLimits, note.guid(), authToken,
        errorDescription);

    if (!res) {
        APPEND_NOTE_DETAILS(errorDescription, note)
        QNWARNING(
            "synchronization:remote_to_local",
            errorDescription << ", note: " << note);
        Q_EMIT failure(errorDescription);
    }
}

void RemoteToLocalSynchronizationManager::
    getFullNoteDataAsyncAndAddToLocalStorage(const Note & note)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager"
            << "::getFullNoteDataAsyncAndAddToLocalStorage: " << note);

    if (Q_UNLIKELY(!note.hasGuid())) {
        ErrorString errorDescription(
            QT_TR_NOOP("Internal error: the synced note to be added to "
                       "the local storage has no guid"));
        APPEND_NOTE_DETAILS(errorDescription, note)

        QNWARNING(
            "synchronization:remote_to_local",
            errorDescription << ", note: " << note);
        Q_EMIT failure(errorDescription);
        return;
    }

    QString noteGuid = note.guid();

    auto it = m_notesPendingDownloadForAddingToLocalStorage.find(noteGuid);
    if (Q_UNLIKELY(it != m_notesPendingDownloadForAddingToLocalStorage.end())) {
        QNDEBUG(
            "synchronization:remote_to_local",
            "Note with guid " << noteGuid << " is already being downloaded");
        return;
    }

    QNTRACE(
        "synchronization:remote_to_local",
        "Adding note into the list of "
            << "those pending download for adding to the local storage: "
            << note.qevercloudNote());

    m_notesPendingDownloadForAddingToLocalStorage[noteGuid] =
        note.qevercloudNote();

    getFullNoteDataAsync(note);
}

void RemoteToLocalSynchronizationManager::
    getFullNoteDataAsyncAndUpdateInLocalStorage(const Note & note)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager"
            << "::getFullNoteDataAsyncAndUpdateInLocalStorage: " << note);

    if (Q_UNLIKELY(!note.hasGuid())) {
        ErrorString errorDescription(
            QT_TR_NOOP("Internal error: the synced note "
                       "to be updated in the local storage "
                       "has no guid"));
        APPEND_NOTE_DETAILS(errorDescription, note)
        QNWARNING(
            "synchronization:remote_to_local",
            errorDescription << ", note: " << note);
        Q_EMIT failure(errorDescription);
        return;
    }

    QString noteGuid = note.guid();

    auto it =
        m_notesPendingDownloadForUpdatingInLocalStorageByGuid.find(noteGuid);

    if (Q_UNLIKELY(
            it != m_notesPendingDownloadForUpdatingInLocalStorageByGuid.end()))
    {
        QNDEBUG(
            "synchronization:remote_to_local",
            "Note with guid " << noteGuid << " is already being downloaded");
        return;
    }

    QNTRACE(
        "synchronization:remote_to_local",
        "Adding note guid into the list "
            << "of those pending download for update in the local storage: "
            << noteGuid);

    m_notesPendingDownloadForUpdatingInLocalStorageByGuid[noteGuid] = note;
    getFullNoteDataAsync(note);
}

void RemoteToLocalSynchronizationManager::getFullResourceDataAsync(
    const Resource & resource, const Note & resourceOwningNote)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::getFullResourceDataAsync: "
            << "resource = " << resource
            << "\nResource owning note: " << resourceOwningNote);

    if (!resource.hasGuid()) {
        ErrorString errorDescription(
            QT_TR_NOOP("Detected the attempt to get full resource's data for "
                       "a resource without guid"));
        APPEND_NOTE_DETAILS(errorDescription, resourceOwningNote);
        QNWARNING(
            "synchronization:remote_to_local",
            errorDescription
                << "\nResource: " << resource
                << "\nResource owning note: " << resourceOwningNote);
        Q_EMIT failure(errorDescription);
        return;
    }

    // Need to find out which note store is required - the one for user's own
    // account or the one for the stuff from some linked notebook

    QString authToken;
    INoteStore * pNoteStore = nullptr;

    auto linkedNotebookGuidIt =
        m_linkedNotebookGuidsByResourceGuids.find(resource.guid());

    if (linkedNotebookGuidIt == m_linkedNotebookGuidsByResourceGuids.end()) {
        QNDEBUG(
            "synchronization:remote_to_local",
            "Found no linked notebook "
                << "corresponding to the resource with guid " << resource.guid()
                << ", using the note store for the user's own account");
        pNoteStore = &(m_manager.noteStore());
        connectToUserOwnNoteStore(pNoteStore);
        authToken = m_authenticationToken;
    }
    else {
        const QString & linkedNotebookGuid = linkedNotebookGuidIt.value();

        auto authTokenIt =
            m_authenticationTokensAndShardIdsByLinkedNotebookGuid.find(
                linkedNotebookGuid);

        if (Q_UNLIKELY(
                authTokenIt ==
                m_authenticationTokensAndShardIdsByLinkedNotebookGuid.end()))
        {
            ErrorString errorDescription(
                QT_TR_NOOP("Can't find the authentication token corresponding "
                           "to the linked notebook"));
            APPEND_NOTE_DETAILS(errorDescription, resourceOwningNote)
            QNWARNING(
                "synchronization:remote_to_local",
                errorDescription
                    << "; resource: " << resource
                    << "\nResource owning note: " << resourceOwningNote);
            Q_EMIT failure(errorDescription);
            return;
        }

        authToken = authTokenIt.value().first;
        const QString & linkedNotebookShardId = authTokenIt.value().second;

        QString linkedNotebookNoteStoreUrl;
        for (const auto & linkedNotebook: qAsConst(m_allLinkedNotebooks)) {
            if (linkedNotebook.hasGuid() &&
                (linkedNotebook.guid() == linkedNotebookGuid) &&
                linkedNotebook.hasNoteStoreUrl())
            {
                linkedNotebookNoteStoreUrl = linkedNotebook.noteStoreUrl();
                break;
            }
        }

        if (linkedNotebookNoteStoreUrl.isEmpty()) {
            ErrorString errorDescription(
                QT_TR_NOOP("Can't find the note store URL corresponding to "
                           "the linked notebook"));
            APPEND_NOTE_DETAILS(errorDescription, resourceOwningNote)
            QNWARNING(
                "synchronization:remote_to_local",
                errorDescription
                    << "; resource: " << resource
                    << "\nResource owning note: " << resourceOwningNote);
            Q_EMIT failure(errorDescription);
            return;
        }

        LinkedNotebook linkedNotebook;
        linkedNotebook.setGuid(linkedNotebookGuid);
        linkedNotebook.setShardId(linkedNotebookShardId);
        linkedNotebook.setNoteStoreUrl(linkedNotebookNoteStoreUrl);
        pNoteStore = m_manager.noteStoreForLinkedNotebook(linkedNotebook);
        if (Q_UNLIKELY(!pNoteStore)) {
            ErrorString errorDescription(
                QT_TR_NOOP("Can't find or create note store for "));
            APPEND_NOTE_DETAILS(errorDescription, resourceOwningNote)
            QNWARNING(
                "synchronization:remote_to_local",
                errorDescription
                    << "; resource: " << resource
                    << "\nResource owning note: " << resourceOwningNote);
            Q_EMIT failure(errorDescription);
            return;
        }

        if (Q_UNLIKELY(pNoteStore->noteStoreUrl().isEmpty())) {
            ErrorString errorDescription(
                QT_TR_NOOP("Internal error: empty note store url for "
                           "the linked notebook's note store"));
            APPEND_NOTE_DETAILS(errorDescription, resourceOwningNote)
            QNWARNING(
                "synchronization:remote_to_local",
                errorDescription
                    << "; resource: " << resource
                    << "\nResource owning note: " << resourceOwningNote);
            Q_EMIT failure(errorDescription);
            return;
        }

        QObject::connect(
            pNoteStore, &INoteStore::getResourceAsyncFinished, this,
            &RemoteToLocalSynchronizationManager::onGetResourceAsyncFinished,
            Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

        QNDEBUG(
            "synchronization:remote_to_local",
            "Using INoteStore "
                << "corresponding to the linked notebook with guid "
                << linkedNotebookGuid
                << ", note store url = " << pNoteStore->noteStoreUrl());
    }

    ErrorString errorDescription;

    bool res = pNoteStore->getResourceAsync(
        /* with data body = */ true,
        /* with recognition data body = */ true,
        /* with alternate data body = */ true,
        /* with attributes = */ true, resource.guid(), authToken,
        errorDescription);

    if (!res) {
        APPEND_NOTE_DETAILS(errorDescription, resourceOwningNote);
        QNWARNING(
            "synchronization:remote_to_local",
            errorDescription
                << "; resource: " << resource
                << "\nResource owning note: " << resourceOwningNote);
        Q_EMIT failure(errorDescription);
    }
}

void RemoteToLocalSynchronizationManager::
    getFullResourceDataAsyncAndAddToLocalStorage(
        const Resource & resource, const Note & resourceOwningNote)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager"
            << "::getFullResourceDataAsyncAndAddToLocalStorage: resource = "
            << resource << "\nResource owning note: " << resourceOwningNote);

    if (Q_UNLIKELY(!resource.hasGuid())) {
        ErrorString errorDescription(
            QT_TR_NOOP("Internal error: the synced resource to be added to "
                       "the local storage has no guid"));
        QNWARNING(
            "synchronization:remote_to_local",
            errorDescription
                << ", resource: " << resource
                << "\nResource owning note: " << resourceOwningNote);
        Q_EMIT failure(errorDescription);
        return;
    }

    QString resourceGuid = resource.guid();

    auto it =
        m_resourcesPendingDownloadForAddingToLocalStorageWithNotesByResourceGuid
            .find(resourceGuid);

    if (Q_UNLIKELY(
            it !=
            m_resourcesPendingDownloadForAddingToLocalStorageWithNotesByResourceGuid
                .end()))
    {
        QNDEBUG(
            "synchronization:remote_to_local",
            "Resource with guid " << resourceGuid
                                  << " is already being downloaded");
        return;
    }

    QNTRACE(
        "synchronization:remote_to_local",
        "Adding resource guid into "
            << "the list of those pending download for adding to the local "
            << "storage: " << resourceGuid);

    m_resourcesPendingDownloadForAddingToLocalStorageWithNotesByResourceGuid
        [resourceGuid] = std::make_pair(resource, resourceOwningNote);

    getFullResourceDataAsync(resource, resourceOwningNote);
}

void RemoteToLocalSynchronizationManager::
    getFullResourceDataAsyncAndUpdateInLocalStorage(
        const Resource & resource, const Note & resourceOwningNote)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager"
            << "::getFullResourceDataAsyncAndUpdateInLocalStorage: resource = "
            << resource << "\nResource owning note: " << resourceOwningNote);

    if (Q_UNLIKELY(!resource.hasGuid())) {
        ErrorString errorDescription(
            QT_TR_NOOP("Internal error: the synced resource to be updated "
                       "in the local storage has no guid"));
        QNWARNING(
            "synchronization:remote_to_local",
            errorDescription
                << ", resource: " << resource
                << "\nResource owning note: " << resourceOwningNote);
        Q_EMIT failure(errorDescription);
        return;
    }

    QString resourceGuid = resource.guid();

    auto it =
        m_resourcesPendingDownloadForUpdatingInLocalStorageWithNotesByResourceGuid
            .find(resourceGuid);

    if (Q_UNLIKELY(
            it !=
            m_resourcesPendingDownloadForUpdatingInLocalStorageWithNotesByResourceGuid
                .end()))
    {
        QNDEBUG(
            "synchronization:remote_to_local",
            "Resource with guid " << resourceGuid
                                  << " is already being downloaded");
        return;
    }

    QNTRACE(
        "synchronization:remote_to_local",
        "Adding resource guid into "
            << "the list of those pending download for update in the local "
            << "storage: " << resourceGuid);

    m_resourcesPendingDownloadForUpdatingInLocalStorageWithNotesByResourceGuid
        [resourceGuid] = std::make_pair(resource, resourceOwningNote);

    getFullResourceDataAsync(resource, resourceOwningNote);
}

void RemoteToLocalSynchronizationManager::downloadSyncChunksAndLaunchSync(
    qint32 afterUsn)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::downloadSyncChunksAndLaunchSync: "
            << "after USN = " << afterUsn);

    auto & noteStore = m_manager.noteStore();
    qevercloud::SyncChunk * pSyncChunk = nullptr;

    qint32 lastPreviousUsn = std::max(m_lastUpdateCount, 0);
    QNDEBUG(
        "synchronization:remote_to_local",
        "Last previous USN: " << lastPreviousUsn);

    while (!pSyncChunk || (pSyncChunk->chunkHighUSN < pSyncChunk->updateCount))
    {
        if (pSyncChunk) {
            afterUsn = pSyncChunk->chunkHighUSN;
            QNTRACE(
                "synchronization:remote_to_local",
                "Updated after USN to "
                    << "sync chunk's high USN: " << pSyncChunk->chunkHighUSN);
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

        qint32 errorCode = noteStore.getSyncChunk(
            afterUsn, m_maxSyncChunksPerOneDownload, filter, *pSyncChunk,
            errorDescription, rateLimitSeconds);

        if (errorCode ==
            static_cast<qint32>(qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED))
        {
            if (rateLimitSeconds < 0) {
                errorDescription.setBase(QT_TR_NOOP(
                    "Rate limit reached but the number of seconds to "
                    "wait is incorrect"));
                errorDescription.details() = QString::number(rateLimitSeconds);
                QNWARNING("synchronization:remote_to_local", errorDescription);
                Q_EMIT failure(errorDescription);
                return;
            }

            m_syncChunks.pop_back();

            int timerId = startTimer(secondsToMilliseconds(rateLimitSeconds));
            if (Q_UNLIKELY(timerId == 0)) {
                ErrorString errorDescription(QT_TR_NOOP(
                    "Failed to start a timer to postpone the Evernote "
                    "API call due to rate limit exceeding"));
                QNWARNING("synchronization:remote_to_local", errorDescription);
                Q_EMIT failure(errorDescription);
                return;
            }

            m_afterUsnForSyncChunkPerAPICallPostponeTimerId[timerId] = afterUsn;
            Q_EMIT rateLimitExceeded(rateLimitSeconds);
            return;
        }
        else if (
            errorCode ==
            static_cast<qint32>(qevercloud::EDAMErrorCode::AUTH_EXPIRED))
        {
            handleAuthExpiration();
            return;
        }
        else if (errorCode != 0) {
            ErrorString errorMessage(
                QT_TR_NOOP("Failed to download the sync chunks"));

            errorMessage.additionalBases().append(errorDescription.base());

            errorMessage.additionalBases().append(
                errorDescription.additionalBases());

            errorMessage.details() = errorDescription.details();
            Q_EMIT failure(errorMessage);
            return;
        }

        QNDEBUG(
            "synchronization:remote_to_local",
            "Received sync chunk: " << *pSyncChunk);

        m_lastSyncTime = std::max(pSyncChunk->currentTime, m_lastSyncTime);
        m_lastUpdateCount =
            std::max(pSyncChunk->updateCount, m_lastUpdateCount);

        QNTRACE(
            "synchronization:remote_to_local",
            "Sync chunk current time: "
                << printableDateTimeFromTimestamp(pSyncChunk->currentTime)
                << ", last sync time = "
                << printableDateTimeFromTimestamp(m_lastSyncTime)
                << ", sync chunk high USN = " << pSyncChunk->chunkHighUSN
                << ", sync chunk update count = " << pSyncChunk->updateCount
                << ", last update count = " << m_lastUpdateCount);

        Q_EMIT syncChunksDownloadProgress(
            pSyncChunk->chunkHighUSN, pSyncChunk->updateCount, lastPreviousUsn);
    }

    QNDEBUG(
        "synchronization:remote_to_local",
        "Done. Processing tags, saved "
            << "searches, linked notebooks and notebooks from buffered sync "
               "chunks");

    m_lastSyncChunksDownloadedUsn = afterUsn;
    m_syncChunksDownloaded = true;
    Q_EMIT syncChunksDownloaded();

    launchSync();
}

const Notebook * RemoteToLocalSynchronizationManager::getNotebookPerNote(
    const Note & note) const
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::getNotebookPerNote: note = "
            << note);

    QString noteGuid = (note.hasGuid() ? note.guid() : QString());
    QString noteLocalUid = note.localUid();

    auto key = std::make_pair(noteGuid, noteLocalUid);
    auto it = m_notebooksPerNoteIds.find(key);
    if (it == m_notebooksPerNoteIds.end()) {
        return nullptr;
    }
    else {
        return &(it.value());
    }
}

void RemoteToLocalSynchronizationManager::handleAuthExpiration()
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::handleAuthExpiration");

    if (syncingLinkedNotebooksContent()) {
        if (m_pendingAuthenticationTokensForLinkedNotebooks) {
            QNDEBUG(
                "synchronization:remote_to_local",
                "Already pending "
                    << "authentication tokens for linked notebooks");
            return;
        }

        requestAuthenticationTokensForAllLinkedNotebooks();
    }
    else {
        if (m_pendingAuthenticationTokenAndShardId) {
            QNDEBUG(
                "synchronization:remote_to_local",
                "Already pending "
                    << "the authentication token and shard id");
            return;
        }

        m_pendingAuthenticationTokenAndShardId = true;
        Q_EMIT requestAuthenticationToken();
    }
}

bool RemoteToLocalSynchronizationManager::checkUserAccountSyncState(
    bool & asyncWait, bool & error, qint32 & afterUsn)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::checkUserAccountSyncState");

    asyncWait = false;
    error = false;

    ErrorString errorDescription;
    qint32 rateLimitSeconds = 0;
    qevercloud::SyncState state;

    qint32 errorCode = m_manager.noteStore().getSyncState(
        state, errorDescription, rateLimitSeconds);

    if (errorCode ==
        static_cast<qint32>(qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED))
    {
        if (rateLimitSeconds < 0) {
            errorDescription.setBase(
                QT_TR_NOOP("Rate limit reached but the number "
                           "of seconds to wait is incorrect"));
            errorDescription.details() = QString::number(rateLimitSeconds);
            Q_EMIT failure(errorDescription);
            error = true;
            return false;
        }

        m_getSyncStateBeforeStartAPICallPostponeTimerId =
            startTimer(secondsToMilliseconds(rateLimitSeconds));
        if (Q_UNLIKELY(m_getSyncStateBeforeStartAPICallPostponeTimerId == 0)) {
            errorDescription.setBase(
                QT_TR_NOOP("Failed to start a timer to postpone the Evernote "
                           "API call due to rate limit exceeding"));
            Q_EMIT failure(errorDescription);
            error = true;
        }
        else {
            asyncWait = true;
        }

        Q_EMIT rateLimitExceeded(rateLimitSeconds);
        return false;
    }
    else if (
        errorCode ==
        static_cast<qint32>(qevercloud::EDAMErrorCode::AUTH_EXPIRED))
    {
        handleAuthExpiration();
        asyncWait = true;
        return false;
    }
    else if (errorCode != 0) {
        Q_EMIT failure(errorDescription);
        error = true;
        return false;
    }

    QNDEBUG(
        "synchronization:remote_to_local",
        "Sync state: " << state << "\nLast sync time = "
                       << printableDateTimeFromTimestamp(m_lastSyncTime)
                       << "; last update count = " << m_lastUpdateCount);

    if (state.fullSyncBefore > m_lastSyncTime) {
        QNDEBUG(
            "synchronization:remote_to_local",
            "Sync state says the time "
                << "has come to do the full sync");
        afterUsn = 0;
        m_lastSyncMode = SyncMode::FullSync;
    }
    else if (state.updateCount == m_lastUpdateCount) {
        QNDEBUG(
            "synchronization:remote_to_local",
            "Server has no updates for "
                << "user's data since the last sync");
        return false;
    }

    return true;
}

bool RemoteToLocalSynchronizationManager::checkLinkedNotebooksSyncStates(
    bool & asyncWait, bool & error)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::checkLinkedNotebooksSyncStates");

    asyncWait = false;
    error = false;

    if (!m_allLinkedNotebooksListed) {
        QNTRACE(
            "synchronization:remote_to_local",
            "The list of all linked "
                << "notebooks was not obtained from the local storage yet, "
                   "need to "
                << "wait for it to happen");

        requestAllLinkedNotebooks();
        asyncWait = true;
        return false;
    }

    if (m_allLinkedNotebooks.isEmpty()) {
        QNTRACE(
            "synchronization:remote_to_local",
            "The list of all linked "
                << "notebooks is empty, nothing to check sync states for");
        return false;
    }

    if (m_pendingAuthenticationTokensForLinkedNotebooks) {
        QNTRACE(
            "synchronization:remote_to_local",
            "Pending authentication "
                << "tokens for linked notebook flag is set, need to wait for "
                   "auth "
                << "tokens");
        asyncWait = true;
        return false;
    }

    const int numAllLinkedNotebooks = m_allLinkedNotebooks.size();
    for (int i = 0; i < numAllLinkedNotebooks; ++i) {
        const LinkedNotebook & linkedNotebook = m_allLinkedNotebooks[i];
        if (!linkedNotebook.hasGuid()) {
            ErrorString errorMessage(
                QT_TR_NOOP("Internal error: found a linked notebook without "
                           "guid"));
            Q_EMIT failure(errorMessage);
            QNWARNING(
                "synchronization:remote_to_local",
                errorMessage << ", linked notebook: " << linkedNotebook);
            error = true;
            return false;
        }

        const QString & linkedNotebookGuid = linkedNotebook.guid();

        auto lastUpdateCountIt =
            m_lastUpdateCountByLinkedNotebookGuid.find(linkedNotebookGuid);

        if (lastUpdateCountIt == m_lastUpdateCountByLinkedNotebookGuid.end()) {
            lastUpdateCountIt = m_lastUpdateCountByLinkedNotebookGuid.insert(
                linkedNotebookGuid, 0);
        }

        qint32 lastUpdateCount = lastUpdateCountIt.value();
        qevercloud::SyncState state;

        getLinkedNotebookSyncState(
            linkedNotebook, m_authenticationToken, state, asyncWait, error);

        if (asyncWait || error) {
            return false;
        }

        QNDEBUG(
            "synchronization:remote_to_local",
            "Sync state: " << state
                           << "\nLast update count = " << lastUpdateCount);

        if (state.updateCount == lastUpdateCount) {
            QNTRACE(
                "synchronization:remote_to_local",
                "Evernote service has "
                    << "no updates for the linked notebook with guid "
                    << linkedNotebookGuid);
            continue;
        }
        else {
            QNTRACE(
                "synchronization:remote_to_local",
                "Detected mismatch in "
                    << "update counts for the linked notebook with guid "
                    << linkedNotebookGuid
                    << ": last update count = " << lastUpdateCount
                    << ", sync state's update count: " << state.updateCount);
            return true;
        }
    }

    QNTRACE(
        "synchronization:remote_to_local",
        "Checked sync states for all "
            << "linked notebooks, found no updates from Evernote service");
    return false;
}

void RemoteToLocalSynchronizationManager::authenticationInfoForNotebook(
    const Notebook & notebook, QString & authToken, QString & shardId,
    bool & isPublic) const
{
    isPublic = notebook.hasPublished() && notebook.isPublished();

    if (notebook.hasLinkedNotebookGuid()) {
        auto it = m_authenticationTokensAndShardIdsByLinkedNotebookGuid.find(
            notebook.linkedNotebookGuid());
        if (Q_UNLIKELY(
                it ==
                m_authenticationTokensAndShardIdsByLinkedNotebookGuid.end()))
        {
            QNWARNING(
                "synchronization:remote_to_local",
                "Can't download an "
                    << "ink note image: no authentication token and shard id "
                       "for "
                    << "linked notebook: " << notebook);
            return;
        }

        authToken = it.value().first;
        shardId = it.value().second;
    }
    else {
        authToken = m_authenticationToken;
        shardId = m_shardId;
    }
}

bool RemoteToLocalSynchronizationManager::
    findNotebookForInkNoteImageDownloading(const Note & note)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager"
            << "::findNotebookForInkNoteImageDownloading: note local uid = "
            << note.localUid() << ", note guid = "
            << (note.hasGuid() ? note.guid() : QStringLiteral("<empty>")));

    if (Q_UNLIKELY(!note.hasGuid())) {
        QNWARNING(
            "synchronization:remote_to_local",
            "Can't find notebook for "
                << "ink note image downloading: note has no guid: " << note);
        return false;
    }

    if (Q_UNLIKELY(!note.hasResources())) {
        QNWARNING(
            "synchronization:remote_to_local",
            "Can't find notebook for "
                << "ink note image downloading: note has no resources: "
                << note);
        return false;
    }

    if (Q_UNLIKELY(!note.isInkNote())) {
        QNWARNING(
            "synchronization:remote_to_local",
            "Can't find notebook for "
                << "ink note image downloading: note is not an ink note: "
                << note);
        return false;
    }

    if (Q_UNLIKELY(!note.hasNotebookLocalUid() && !note.hasNotebookGuid())) {
        QNWARNING(
            "synchronization:remote_to_local",
            "Can't find notebook for "
                << "ink note image downloading: the note has neither notebook "
                << "local uid nor notebook guid: " << note);
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

    QUuid requestId = QUuid::createUuid();

    m_notesPendingInkNoteImagesDownloadByFindNotebookRequestId[requestId] =
        note;

    QString noteGuid = note.guid();

    /**
     * NOTE: technically, here we don't start downloading the ink note image
     * yet; but it is necessary to insert the resource guids per note guid into
     * the container right here in order to prevent multiple ink note image
     * downloads for the same note during the sync process
     */
    const auto resources = note.resources();
    for (auto it = resources.constBegin(), end = resources.constEnd();
         it != end; ++it)
    {
        const auto & resource = *it;

        if (resource.hasGuid() && resource.hasMime() && resource.hasWidth() &&
            resource.hasHeight() &&
            (resource.mime() == QStringLiteral("vnd.evernote.ink")))
        {
            const QString & noteGuid = note.guid();
            const QString & resGuid = resource.guid();

            bool res =
                m_resourceGuidsPendingInkNoteImageDownloadPerNoteGuid.contains(
                    noteGuid, resGuid);

            if (!res) {
                m_resourceGuidsPendingInkNoteImageDownloadPerNoteGuid.insert(
                    noteGuid, resGuid);
            }
        }
    }

    QNTRACE(
        "synchronization:remote_to_local",
        "Emitting the request to find "
            << "a notebook for the ink note images download setup: "
            << requestId << ", note guid = " << noteGuid
            << ", notebook: " << dummyNotebook);
    Q_EMIT findNotebook(dummyNotebook, requestId);

    return true;
}

void RemoteToLocalSynchronizationManager::setupInkNoteImageDownloading(
    const QString & resourceGuid, const int resourceHeight,
    const int resourceWidth, const QString & noteGuid,
    const Notebook & notebook)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::setupInkNoteImageDownloading: "
            << "resource guid = " << resourceGuid << ", resource height = "
            << resourceHeight << ", resource width = " << resourceWidth
            << ", note guid = " << noteGuid << ", notebook: " << notebook);

    QString authToken, shardId;
    bool isPublicNotebook = false;

    authenticationInfoForNotebook(
        notebook, authToken, shardId, isPublicNotebook);

    if (m_resourceGuidsPendingInkNoteImageDownloadPerNoteGuid.contains(
            noteGuid, resourceGuid))
    {
        QNDEBUG(
            "synchronization:remote_to_local",
            "Already downloading "
                << "the ink note image for note guid " << noteGuid
                << " and resource guid " << resourceGuid);
        return;
    }

    Q_UNUSED(m_resourceGuidsPendingInkNoteImageDownloadPerNoteGuid.insert(
        noteGuid, resourceGuid))

    QString storageFolderPath = inkNoteImagesStoragePath();

    auto * pDownloader = new InkNoteImageDownloader(
        m_host, resourceGuid, noteGuid, authToken, shardId, resourceHeight,
        resourceWidth,
        /* from public linked notebook = */ isPublicNotebook, storageFolderPath,
        this);

    QObject::connect(
        pDownloader, &InkNoteImageDownloader::finished, this,
        &RemoteToLocalSynchronizationManager::onInkNoteImageDownloadFinished,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    /**
     * WARNING: it seems it's not possible to run ink note image downloading
     * in a different thread, the error like this might appear: QObject: Cannot
     * create children for a parent that is in a different thread.
     * (Parent is QNetworkAccessManager(0x499b900), parent's thread is
     * QThread(0x1b535b0), current thread is QThread(0x42ed270)
     */
    // QThreadPool::globalInstance()->start(pDownloader);

    pDownloader->run();
}

bool RemoteToLocalSynchronizationManager::setupInkNoteImageDownloadingForNote(
    const Note & note, const Notebook & notebook)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager"
            << "::setupInkNoteImageDownloadingForNote: note local uid = "
            << note.localUid() << ", note guid = "
            << (note.hasGuid() ? note.guid() : QStringLiteral("<empty>"))
            << ", notebook = " << notebook);

    if (Q_UNLIKELY(!note.hasGuid())) {
        QNWARNING(
            "synchronization:remote_to_local",
            "Can't set up the ink "
                << "note images downloading: the note has no guid: " << note);
        return false;
    }

    if (Q_UNLIKELY(!note.hasResources())) {
        QNWARNING(
            "synchronization:remote_to_local",
            "Can't set up the ink "
                << "note images downloading: the note has no resources: "
                << note);
        return false;
    }

    if (Q_UNLIKELY(!note.isInkNote())) {
        QNWARNING(
            "synchronization:remote_to_local",
            "Can't set up the ink "
                << "note images downloading: the note is not an ink note: "
                << note);
        return false;
    }

    auto resources = note.resources();
    for (const auto & resource: ::qAsConst(resources)) {
        if (resource.hasGuid() && resource.hasMime() && resource.hasWidth() &&
            resource.hasHeight() &&
            (resource.mime() == QStringLiteral("application/vnd.evernote.ink")))
        {
            setupInkNoteImageDownloading(
                resource.guid(), resource.height(), resource.width(),
                note.guid(), notebook);
        }
    }

    return true;
}

bool RemoteToLocalSynchronizationManager::
    findNotebookForNoteThumbnailDownloading(const Note & note)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager"
            << "::findNotebookForNoteThumbnailDownloading: note local uid = "
            << note.localUid() << ", note guid = "
            << (note.hasGuid() ? note.guid() : QStringLiteral("<empty>")));

    if (Q_UNLIKELY(!note.hasGuid())) {
        QNWARNING(
            "synchronization:remote_to_local",
            "Can't find notebook for "
                << "note thumbnail downloading: note has no guid: " << note);
        return false;
    }

    if (Q_UNLIKELY(!note.hasNotebookLocalUid() && !note.hasNotebookGuid())) {
        QNWARNING(
            "synchronization:remote_to_local",
            "Can't find notebook for "
                << "note thumbnail downloading: the note has neither notebook "
                << "local uid nor notebook guid: " << note);
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

    QUuid requestId = QUuid::createUuid();
    m_notesPendingThumbnailDownloadByFindNotebookRequestId[requestId] = note;

    QString noteGuid = note.guid();

    /**
     * NOTE: technically, here we don't start downloading the thumbnail yet;
     * but it is necessary to insert the note into the container right here in
     * order to prevent multiple thumbnail downloads for the same note during
     * the sync process
     */
    m_notesPendingThumbnailDownloadByGuid[noteGuid] = note;

    QNTRACE(
        "synchronization:remote_to_local",
        "Emitting the request to find "
            << "a notebook for the note thumbnail download setup: " << requestId
            << ", note guid = " << noteGuid << ", notebook: " << dummyNotebook);
    Q_EMIT findNotebook(dummyNotebook, requestId);

    return true;
}

bool RemoteToLocalSynchronizationManager::setupNoteThumbnailDownloading(
    const Note & note, const Notebook & notebook)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::setupNoteThumbnailDownloading: "
            << "note guid = "
            << (note.hasGuid() ? note.guid() : QStringLiteral("<empty>"))
            << ", notebook: " << notebook);

    if (Q_UNLIKELY(!note.hasGuid())) {
        QNWARNING(
            "synchronization:remote_to_local",
            "Can't setup downloading "
                << "the thumbnail: note has no guid: " << note);
        return false;
    }

    const QString & noteGuid = note.guid();
    m_notesPendingThumbnailDownloadByGuid[noteGuid] = note;

    QString authToken, shardId;
    bool isPublicNotebook = false;

    authenticationInfoForNotebook(
        notebook, authToken, shardId, isPublicNotebook);

    auto * pDownloader = new NoteThumbnailDownloader(
        m_host, noteGuid, authToken, shardId, isPublicNotebook, this);

    QObject::connect(
        pDownloader, &NoteThumbnailDownloader::finished, this,
        &RemoteToLocalSynchronizationManager::
            onNoteThumbnailDownloadingFinished,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    pDownloader->start();
    return true;
}

void RemoteToLocalSynchronizationManager::launchNoteSyncConflictResolver(
    const Note & localConflict, const qevercloud::Note & remoteNote)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::launchNoteSyncConflictResolver: "
            << "remote note guid = "
            << (remoteNote.guid.isSet() ? remoteNote.guid.ref()
                                        : QStringLiteral("<not set>")));

    if (remoteNote.guid.isSet()) {
        auto noteSyncConflictResolvers =
            findChildren<NoteSyncConflictResolver *>();

        for (const auto * pResolver: ::qAsConst(noteSyncConflictResolvers)) {
            const auto & resolverRemoteNote = pResolver->remoteNote();
            if (resolverRemoteNote.guid.isSet() &&
                (resolverRemoteNote.guid.ref() == remoteNote.guid.ref()))
            {
                QNDEBUG(
                    "synchronization:remote_to_local",
                    "Note sync conflict "
                        << "resolver already exists for remote note with guid "
                        << remoteNote.guid.ref());
                return;
            }
        }
    }

    auto * pResolver = new NoteSyncConflictResolver(
        *m_pNoteSyncConflictResolverManager, remoteNote, localConflict, this);

    QObject::connect(
        pResolver, &NoteSyncConflictResolver::finished, this,
        &RemoteToLocalSynchronizationManager::
            onNoteSyncConflictResolverFinished,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        pResolver, &NoteSyncConflictResolver::failure, this,
        &RemoteToLocalSynchronizationManager::onNoteSyncConflictResolvedFailure,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        pResolver, &NoteSyncConflictResolver::rateLimitExceeded, this,
        &RemoteToLocalSynchronizationManager::
            onNoteSyncConflictRateLimitExceeded,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::DirectConnection));

    QObject::connect(
        pResolver, &NoteSyncConflictResolver::notifyAuthExpiration, this,
        &RemoteToLocalSynchronizationManager::
            onNoteSyncConflictAuthenticationExpired,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    pResolver->start();
}

QString RemoteToLocalSynchronizationManager::clientNameForProtocolVersionCheck()
    const
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

Note RemoteToLocalSynchronizationManager::createConflictingNote(
    const Note & originalNote, const qevercloud::Note * pRemoteNote) const
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::createConflictingNote: "
            << "original note local uid = " << originalNote.localUid());

    Note conflictingNote(originalNote);
    conflictingNote.setLocalUid(UidGenerator::Generate());
    conflictingNote.setGuid(QString());
    conflictingNote.setUpdateSequenceNumber(-1);
    conflictingNote.setDirty(true);
    conflictingNote.setLocal(false);

    if (originalNote.hasGuid()) {
        auto & attributes = conflictingNote.noteAttributes();
        if (!attributes.conflictSourceNoteGuid.isSet()) {
            attributes.conflictSourceNoteGuid = originalNote.guid();
        }
    }

    if (conflictingNote.hasResources()) {
        // Need to update the conflicting note's resources:
        // 1) give each of them new local uid + unset guid
        // 2) make each of them point to the conflicting note

        auto resources = conflictingNote.resources();
        for (auto & resource: resources) {
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
    if (conflictingNote.hasTitle()) {
        conflictingNoteTitle =
            conflictingNote.title() + QStringLiteral(" - ") + tr("conflicting");
    }
    else {
        QString previewText = conflictingNote.plainText();
        if (!previewText.isEmpty()) {
            previewText.truncate(12);
            conflictingNoteTitle =
                previewText + QStringLiteral("... - ") + tr("conflicting");
        }
        else {
            conflictingNoteTitle = tr("Conflicting note");
        }
    }

    conflictingNote.setTitle(conflictingNoteTitle);

    if (pRemoteNote && pRemoteNote->notebookGuid.isSet() &&
        conflictingNote.hasNotebookGuid() &&
        (pRemoteNote->notebookGuid.ref() != conflictingNote.notebookGuid()))
    {
        // Check if the conflicting note's notebook is about to be expunged;
        // if so, put the note into the remote note's notebook
        auto notebookIt = std::find(
            m_expungedNotebooks.constBegin(), m_expungedNotebooks.constEnd(),
            conflictingNote.notebookGuid());

        if (notebookIt != m_expungedNotebooks.constEnd()) {
            QNDEBUG(
                "synchronization:remote_to_local",
                "Conflicting note's "
                    << "original notebook is about to be expunged (guid = "
                    << conflictingNote.notebookGuid()
                    << "), using the remote note's notebook (guid = "
                    << pRemoteNote->notebookGuid.ref() << ")");

            conflictingNote.setNotebookLocalUid(QString());
            conflictingNote.setNotebookGuid(pRemoteNote->notebookGuid.ref());
        }
    }

    return conflictingNote;
}

template <class T>
QString RemoteToLocalSynchronizationManager::findLinkedNotebookGuidForItem(
    const T & item) const
{
    Q_UNUSED(item)
    return QString();
}

template <>
QString RemoteToLocalSynchronizationManager::findLinkedNotebookGuidForItem<
    qevercloud::Notebook>(const qevercloud::Notebook & notebook) const
{
    if (Q_UNLIKELY(!notebook.guid.isSet())) {
        return QString();
    }

    auto it = m_linkedNotebookGuidsByNotebookGuids.find(notebook.guid.ref());
    if (it == m_linkedNotebookGuidsByNotebookGuids.end()) {
        return QString();
    }

    return it.value();
}

template <>
QString RemoteToLocalSynchronizationManager::findLinkedNotebookGuidForItem<
    qevercloud::Tag>(const qevercloud::Tag & tag) const
{
    if (Q_UNLIKELY(!tag.guid.isSet())) {
        return QString();
    }

    auto it = m_linkedNotebookGuidsByTagGuids.find(tag.guid.ref());
    if (it == m_linkedNotebookGuidsByTagGuids.end()) {
        return QString();
    }

    return it.value();
}

template <>
QString RemoteToLocalSynchronizationManager::findLinkedNotebookGuidForItem<
    qevercloud::Note>(const qevercloud::Note & note) const
{
    if (Q_UNLIKELY(!note.notebookGuid.isSet())) {
        return QString();
    }

    auto it =
        m_linkedNotebookGuidsByNotebookGuids.find(note.notebookGuid.ref());
    if (it == m_linkedNotebookGuidsByNotebookGuids.end()) {
        return QString();
    }

    return it.value();
}

template <>
QString RemoteToLocalSynchronizationManager::findLinkedNotebookGuidForItem<
    qevercloud::Resource>(const qevercloud::Resource & resource) const
{
    if (Q_UNLIKELY(!resource.guid.isSet())) {
        return QString();
    }

    auto it = m_linkedNotebookGuidsByResourceGuids.find(resource.guid.ref());
    if (it == m_linkedNotebookGuidsByResourceGuids.end()) {
        return QString();
    }

    return it.value();
}

template <class T>
void RemoteToLocalSynchronizationManager::checkNonSyncedItemForSmallestUsn(
    const T & item, const QString & linkedNotebookGuid,
    qint32 & smallestUsn) const
{
    QNTRACE(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager"
            << "::checkNonSyncedItemForSmallestUsn: linked notebook guid = "
            << linkedNotebookGuid << ", item: " << item);

    if (Q_UNLIKELY(!item.updateSequenceNum.isSet())) {
        QNWARNING(
            "synchronization:remote_to_local",
            "Skipping item with empty "
                << "update sequence number: " << item);
        return;
    }

    if (Q_UNLIKELY(!item.guid.isSet())) {
        QNWARNING(
            "synchronization:remote_to_local",
            "Skipping item without "
                << "guid: " << item);
        return;
    }

    QString itemLinkedNotebookGuid = findLinkedNotebookGuidForItem(item);
    if (itemLinkedNotebookGuid != linkedNotebookGuid) {
        QNTRACE(
            "synchronization:remote_to_local",
            "Skipping item as it "
                << "doesn't match by linked notebook guid: item's linked "
                   "notebook "
                << "guid is " << itemLinkedNotebookGuid
                << " while the requested one is " << linkedNotebookGuid);
        return;
    }

    QNTRACE(
        "synchronization:remote_to_local",
        "Checking item with USN " << item.updateSequenceNum.ref() << ": "
                                  << item);
    if ((smallestUsn < 0) || (item.updateSequenceNum.ref() < smallestUsn)) {
        smallestUsn = item.updateSequenceNum.ref();
        QNTRACE(
            "synchronization:remote_to_local",
            "Updated smallest "
                << "non-processed items USN to " << smallestUsn);
    }
}

template <class ContainerType>
void RemoteToLocalSynchronizationManager::
    checkNonSyncedItemsContainerForSmallestUsn(
        const ContainerType & container, const QString & linkedNotebookGuid,
        qint32 & smallestUsn) const
{
    for (auto & item: container) {
        checkNonSyncedItemForSmallestUsn(item, linkedNotebookGuid, smallestUsn);
    }
}

template <>
void RemoteToLocalSynchronizationManager::
    checkNonSyncedItemsContainerForSmallestUsn<QHash<int, Note>>(
        const QHash<int, Note> & notes, const QString & linkedNotebookGuid,
        qint32 & smallestUsn) const
{
    for (const auto it: qevercloud::toRange(qAsConst(notes))) {
        checkNonSyncedItemForSmallestUsn(
            it.value().qevercloudNote(), linkedNotebookGuid, smallestUsn);
    }
}

template <>
void RemoteToLocalSynchronizationManager::
    checkNonSyncedItemsContainerForSmallestUsn<QHash<QString, Note>>(
        const QHash<QString, Note> & notes, const QString & linkedNotebookGuid,
        qint32 & smallestUsn) const
{
    for (const auto it: qevercloud::toRange(qAsConst(notes))) {
        checkNonSyncedItemForSmallestUsn(
            it.value().qevercloudNote(), linkedNotebookGuid, smallestUsn);
    }
}

template <>
void RemoteToLocalSynchronizationManager::
    checkNonSyncedItemsContainerForSmallestUsn<QHash<QUuid, Note>>(
        const QHash<QUuid, Note> & notes, const QString & linkedNotebookGuid,
        qint32 & smallestUsn) const
{
    for (const auto it: qevercloud::toRange(qAsConst(notes))) {
        checkNonSyncedItemForSmallestUsn(
            it.value().qevercloudNote(), linkedNotebookGuid, smallestUsn);
    }
}

template <>
void RemoteToLocalSynchronizationManager::
    checkNonSyncedItemsContainerForSmallestUsn<
        RemoteToLocalSynchronizationManager::NoteDataPerFindNotebookRequestId>(
        const NoteDataPerFindNotebookRequestId & notes,
        const QString & linkedNotebookGuid, qint32 & smallestUsn) const
{
    for (const auto it: qevercloud::toRange(qAsConst(notes))) {
        checkNonSyncedItemForSmallestUsn(
            it.value().first.qevercloudNote(), linkedNotebookGuid, smallestUsn);
    }
}

template <>
void RemoteToLocalSynchronizationManager::
    checkNonSyncedItemsContainerForSmallestUsn<
        QHash<QString, qevercloud::Note>>(
        const QHash<QString, qevercloud::Note> & notes,
        const QString & linkedNotebookGuid, qint32 & smallestUsn) const
{
    for (const auto it: qevercloud::toRange(::qAsConst(notes))) {
        checkNonSyncedItemForSmallestUsn(
            it.value(), linkedNotebookGuid, smallestUsn);
    }
}

template <>
void RemoteToLocalSynchronizationManager::
    checkNonSyncedItemsContainerForSmallestUsn<
        QHash<int, std::pair<Resource, Note>>>(
        const QHash<int, std::pair<Resource, Note>> & resources,
        const QString & linkedNotebookGuid, qint32 & smallestUsn) const
{
    for (const auto it: qevercloud::toRange(::qAsConst(resources))) {
        checkNonSyncedItemForSmallestUsn(
            it.value().first.qevercloudResource(), linkedNotebookGuid,
            smallestUsn);
    }
}

qint32 RemoteToLocalSynchronizationManager::findSmallestUsnOfNonSyncedItems(
    const QString & linkedNotebookGuid) const
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager"
            << "::findSmallestUsnOfNonSyncedItems: linked notebook guid = "
            << linkedNotebookGuid);

    qint32 smallestUsn = -1;

    checkNonSyncedItemsContainerForSmallestUsn(
        m_tags, linkedNotebookGuid, smallestUsn);

    checkNonSyncedItemsContainerForSmallestUsn(
        m_tagsPendingAddOrUpdate, linkedNotebookGuid, smallestUsn);

    checkNonSyncedItemsContainerForSmallestUsn(
        m_notebooks, linkedNotebookGuid, smallestUsn);

    checkNonSyncedItemsContainerForSmallestUsn(
        m_notebooksPendingAddOrUpdate, linkedNotebookGuid, smallestUsn);

    if (linkedNotebookGuid.isEmpty()) {
        checkNonSyncedItemsContainerForSmallestUsn(
            m_savedSearches, linkedNotebookGuid, smallestUsn);

        checkNonSyncedItemsContainerForSmallestUsn(
            m_savedSearchesPendingAddOrUpdate, linkedNotebookGuid, smallestUsn);

        checkNonSyncedItemsContainerForSmallestUsn(
            m_linkedNotebooks, linkedNotebookGuid, smallestUsn);

        checkNonSyncedItemsContainerForSmallestUsn(
            m_linkedNotebooksPendingAddOrUpdate, linkedNotebookGuid,
            smallestUsn);
    }

    bool syncingNotebooks =
        m_pendingNotebooksSyncStart || notebooksSyncInProgress();

    bool syncingTags = m_pendingTagsSyncStart || tagsSyncInProgress();

    if (syncingNotebooks || syncingTags) {
        QNTRACE(
            "synchronization:remote_to_local",
            "The sync of notes hasn't "
                << "started yet, checking notes from sync chunks");

        const QVector<qevercloud::SyncChunk> & syncChunks =
            (linkedNotebookGuid.isEmpty() ? m_syncChunks
                                          : m_linkedNotebookSyncChunks);

        for (const auto & syncChunk: ::qAsConst(syncChunks)) {
            if (!syncChunk.notes.isSet()) {
                continue;
            }

            checkNonSyncedItemsContainerForSmallestUsn(
                syncChunk.notes.ref(), linkedNotebookGuid, smallestUsn);
        }
    }
    else {
        QNTRACE(
            "synchronization:remote_to_local",
            "The sync of notes has "
                << "already started, checking notes from pending lists");

        QNTRACE(
            "synchronization:remote_to_local",
            "Collecting from m_notes, "
                << "smallest USN before: " << smallestUsn);

        checkNonSyncedItemsContainerForSmallestUsn(
            m_notes, linkedNotebookGuid, smallestUsn);

        QNTRACE(
            "synchronization:remote_to_local",
            "Collected from m_notes, "
                << "smallest USN after: " << smallestUsn);

        QNTRACE(
            "synchronization:remote_to_local",
            "Collecting from "
                << "m_notesPendingAddOrUpdate, smallest USN before: "
                << smallestUsn);

        checkNonSyncedItemsContainerForSmallestUsn(
            m_notesPendingAddOrUpdate, linkedNotebookGuid, smallestUsn);

        QNTRACE(
            "synchronization:remote_to_local",
            "Collected from "
                << "m_notesPendingAddOrUpdate, smallest USN after: "
                << smallestUsn);

        QNTRACE(
            "synchronization:remote_to_local",
            "Collecting from "
                << "m_notesToAddPerAPICallPostponeTimerId, smallest USN "
                   "before: "
                << smallestUsn);

        checkNonSyncedItemsContainerForSmallestUsn(
            m_notesToAddPerAPICallPostponeTimerId, linkedNotebookGuid,
            smallestUsn);

        QNTRACE(
            "synchronization:remote_to_local",
            "Collected from "
                << "m_notesToAddPerAPICallPostponeTimerId, smallest USN after: "
                << smallestUsn);

        QNTRACE(
            "synchronization:remote_to_local",
            "Collecting from "
                << "m_notesToUpdatePerAPICallPostponeTimerId, smallest USN "
                   "before: "
                << smallestUsn);

        checkNonSyncedItemsContainerForSmallestUsn(
            m_notesToUpdatePerAPICallPostponeTimerId, linkedNotebookGuid,
            smallestUsn);

        QNTRACE(
            "synchronization:remote_to_local",
            "Collected from "
                << "m_notesToUpdatePerAPICallPostponeTimerId, smallest USN "
                   "after: "
                << smallestUsn);

        // Also need to check for notes which are currently pending download
        // for adding to local storage or for updating within the local storage
        QNTRACE(
            "synchronization:remote_to_local",
            "Collecting from "
                << "m_notesPendingDownloadForAddingToLocalStorage, smallest "
                   "USN "
                << "before: " << smallestUsn);

        checkNonSyncedItemsContainerForSmallestUsn(
            m_notesPendingDownloadForAddingToLocalStorage, linkedNotebookGuid,
            smallestUsn);

        QNTRACE(
            "synchronization:remote_to_local",
            "Collected from "
                << "m_notesPendingDownloadForAddingToLocalStorage, smallest "
                   "USN "
                << "after: " << smallestUsn);

        QNTRACE(
            "synchronization:remote_to_local",
            "Collecting from "
                << "m_notesPendingDownloadForUpdatingInLocalStorageByGuid, "
                << "smallest USN before: " << smallestUsn);

        checkNonSyncedItemsContainerForSmallestUsn(
            m_notesPendingDownloadForUpdatingInLocalStorageByGuid,
            linkedNotebookGuid, smallestUsn);

        QNTRACE(
            "synchronization:remote_to_local",
            "Collected from "
                << "m_notesPendingDownloadForUpdatingInLocalStorageByGuid, "
                << "smallest USN after: " << smallestUsn);

        // Also need to check for notes which might be pending the download of
        // ink note image or thumbnail (these downloads should not cause API
        // limit breach since they are not fully a part of Evernote API but just
        // to be on the safe side)
        QNTRACE(
            "synchronization:remote_to_local",
            "Collecting from "
                << "m_notesPendingInkNoteImagesDownloadByFindNotebookRequestId,"
                   " "
                << "smallest USN before: " << smallestUsn);

        checkNonSyncedItemsContainerForSmallestUsn(
            m_notesPendingInkNoteImagesDownloadByFindNotebookRequestId,
            linkedNotebookGuid, smallestUsn);

        QNTRACE(
            "synchronization:remote_to_local",
            "Collected from "
                << "m_notesPendingInkNoteImagesDownloadByFindNotebookRequestId,"
                   " "
                << "smallest USN after: " << smallestUsn);

        QNTRACE(
            "synchronization:remote_to_local",
            "Collecting from "
                << "m_notesPendingThumbnailDownloadByFindNotebookRequestId, "
                << "smallest USN before: " << smallestUsn);

        checkNonSyncedItemsContainerForSmallestUsn(
            m_notesPendingThumbnailDownloadByFindNotebookRequestId,
            linkedNotebookGuid, smallestUsn);

        QNTRACE(
            "synchronization:remote_to_local",
            "Collected from "
                << "m_notesPendingThumbnailDownloadByFindNotebookRequestId, "
                << "smallest USN before: " << smallestUsn);

        QNTRACE(
            "synchronization:remote_to_local",
            "Collecting from "
                << "m_notesPendingThumbnailDownloadByGuid, smallest USN "
                   "before: "
                << smallestUsn);

        checkNonSyncedItemsContainerForSmallestUsn(
            m_notesPendingThumbnailDownloadByGuid, linkedNotebookGuid,
            smallestUsn);

        QNTRACE(
            "synchronization:remote_to_local",
            "Collected from "
                << "m_notesPendingThumbnailDownloadByGuid, smallest USN after: "
                << smallestUsn);

        QNTRACE(
            "synchronization:remote_to_local",
            "Overall smallest USN after "
                << "collecting it from notes: " << smallestUsn);
    }

    if (syncingNotebooks || syncingTags || notesSyncInProgress()) {
        QNTRACE(
            "synchronization:remote_to_local",
            "The sync of resources "
                << "hasn't started yet, checking resources from sync chunks");

        const QVector<qevercloud::SyncChunk> & syncChunks =
            (linkedNotebookGuid.isEmpty() ? m_syncChunks
                                          : m_linkedNotebookSyncChunks);

        for (const auto & syncChunk: ::qAsConst(syncChunks)) {
            if (!syncChunk.resources.isSet()) {
                continue;
            }

            checkNonSyncedItemsContainerForSmallestUsn(
                syncChunk.resources.ref(), linkedNotebookGuid, smallestUsn);
        }
    }
    else {
        QNTRACE(
            "synchronization:remote_to_local",
            "The sync of resources has "
                << "already started, checking resources from pending lists");

        checkNonSyncedItemsContainerForSmallestUsn(
            m_resources, linkedNotebookGuid, smallestUsn);

        checkNonSyncedItemsContainerForSmallestUsn(
            m_resourcesPendingAddOrUpdate, linkedNotebookGuid, smallestUsn);

        checkNonSyncedItemsContainerForSmallestUsn(
            m_resourcesToAddWithNotesPerAPICallPostponeTimerId,
            linkedNotebookGuid, smallestUsn);

        checkNonSyncedItemsContainerForSmallestUsn(
            m_resourcesToUpdateWithNotesPerAPICallPostponeTimerId,
            linkedNotebookGuid, smallestUsn);
    }

    QNTRACE(
        "synchronization:remote_to_local",
        "Overall smallest USN: " << smallestUsn);

    return smallestUsn;
}

void RemoteToLocalSynchronizationManager::registerTagPendingAddOrUpdate(
    const Tag & tag)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::registerTagPendingAddOrUpdate: "
            << tag);

    if (!tag.hasGuid()) {
        return;
    }

    auto it = std::find_if(
        m_tagsPendingAddOrUpdate.begin(), m_tagsPendingAddOrUpdate.end(),
        CompareItemByGuid<TagsList::value_type>(tag.guid()));

    if (it == m_tagsPendingAddOrUpdate.end()) {
        m_tagsPendingAddOrUpdate << tag.qevercloudTag();
    }
}

void RemoteToLocalSynchronizationManager::registerSavedSearchPendingAddOrUpdate(
    const SavedSearch & search)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager"
            << "::registerSavedSearchPendingAddOrUpdate: " << search);

    if (!search.hasGuid()) {
        return;
    }

    auto it = std::find_if(
        m_savedSearchesPendingAddOrUpdate.begin(),
        m_savedSearchesPendingAddOrUpdate.end(),
        CompareItemByGuid<SavedSearchesList::value_type>(search.guid()));

    if (it == m_savedSearchesPendingAddOrUpdate.end()) {
        m_savedSearchesPendingAddOrUpdate << search.qevercloudSavedSearch();
    }
}

void RemoteToLocalSynchronizationManager::
    registerLinkedNotebookPendingAddOrUpdate(
        const LinkedNotebook & linkedNotebook)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager"
            << "::registerLinkedNotebookPendingAddOrUpdate: "
            << linkedNotebook);

    if (!linkedNotebook.hasGuid()) {
        return;
    }

    auto it = std::find_if(
        m_linkedNotebooksPendingAddOrUpdate.begin(),
        m_linkedNotebooksPendingAddOrUpdate.end(),
        CompareItemByGuid<LinkedNotebooksList::value_type>(
            linkedNotebook.guid()));

    if (it == m_linkedNotebooksPendingAddOrUpdate.end()) {
        m_linkedNotebooksPendingAddOrUpdate
            << linkedNotebook.qevercloudLinkedNotebook();
    }
}

void RemoteToLocalSynchronizationManager::registerNotebookPendingAddOrUpdate(
    const Notebook & notebook)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager"
            << "::registerNotebookPendingAddOrUpdate: " << notebook);

    if (!notebook.hasGuid()) {
        return;
    }

    auto it = std::find_if(
        m_notebooksPendingAddOrUpdate.begin(),
        m_notebooksPendingAddOrUpdate.end(),
        CompareItemByGuid<NotebooksList::value_type>(notebook.guid()));

    if (it == m_notebooksPendingAddOrUpdate.end()) {
        m_notebooksPendingAddOrUpdate << notebook.qevercloudNotebook();
    }
}

void RemoteToLocalSynchronizationManager::registerNotePendingAddOrUpdate(
    const Note & note)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager"
            << "::registerNotePendingAddOrUpdate: " << note);

    if (!note.hasGuid()) {
        return;
    }

    auto it = std::find_if(
        m_notesPendingAddOrUpdate.begin(), m_notesPendingAddOrUpdate.end(),
        CompareItemByGuid<NotesList::value_type>(note.guid()));

    if (it == m_notesPendingAddOrUpdate.end()) {
        m_notesPendingAddOrUpdate << note.qevercloudNote();
    }
}

void RemoteToLocalSynchronizationManager::registerResourcePendingAddOrUpdate(
    const Resource & resource)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager"
            << "::registerResourcePendingAddOrUpdate: " << resource);

    if (!resource.hasGuid()) {
        return;
    }

    auto it = std::find_if(
        m_resourcesPendingAddOrUpdate.begin(),
        m_resourcesPendingAddOrUpdate.end(),
        CompareItemByGuid<ResourcesList::value_type>(resource.guid()));

    if (it == m_resourcesPendingAddOrUpdate.end()) {
        m_resourcesPendingAddOrUpdate << resource.qevercloudResource();
    }
}

void RemoteToLocalSynchronizationManager::unregisterTagPendingAddOrUpdate(
    const Tag & tag)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager"
            << "::unregisterTagPendingAddOrUpdate: " << tag);

    if (!tag.hasGuid()) {
        return;
    }

    auto it = std::find_if(
        m_tagsPendingAddOrUpdate.begin(), m_tagsPendingAddOrUpdate.end(),
        CompareItemByGuid<TagsList::value_type>(tag.guid()));

    if (it != m_tagsPendingAddOrUpdate.end()) {
        Q_UNUSED(m_tagsPendingAddOrUpdate.erase(it))
    }
}

void RemoteToLocalSynchronizationManager::
    unregisterSavedSearchPendingAddOrUpdate(const SavedSearch & search)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager"
            << "::unregisterSavedSearchPendingAddOrUpdate: " << search);

    if (!search.hasGuid()) {
        return;
    }

    auto it = std::find_if(
        m_savedSearchesPendingAddOrUpdate.begin(),
        m_savedSearchesPendingAddOrUpdate.end(),
        CompareItemByGuid<SavedSearchesList::value_type>(search.guid()));

    if (it != m_savedSearchesPendingAddOrUpdate.end()) {
        Q_UNUSED(m_savedSearchesPendingAddOrUpdate.erase(it))
    }
}

void RemoteToLocalSynchronizationManager::
    unregisterLinkedNotebookPendingAddOrUpdate(
        const LinkedNotebook & linkedNotebook)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager"
            << "::unregisterLinkedNotebookPendingAddOrUpdate: "
            << linkedNotebook);

    if (!linkedNotebook.hasGuid()) {
        return;
    }

    auto it = std::find_if(
        m_linkedNotebooksPendingAddOrUpdate.begin(),
        m_linkedNotebooksPendingAddOrUpdate.end(),
        CompareItemByGuid<LinkedNotebooksList::value_type>(
            linkedNotebook.guid()));

    if (it != m_linkedNotebooksPendingAddOrUpdate.end()) {
        Q_UNUSED(m_linkedNotebooksPendingAddOrUpdate.erase(it))
    }
}

void RemoteToLocalSynchronizationManager::unregisterNotebookPendingAddOrUpdate(
    const Notebook & notebook)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager"
            << "::unregisterNotebookPendingAddOrUpdate: " << notebook);

    if (!notebook.hasGuid()) {
        return;
    }

    auto it = std::find_if(
        m_notebooksPendingAddOrUpdate.begin(),
        m_notebooksPendingAddOrUpdate.end(),
        CompareItemByGuid<NotebooksList::value_type>(notebook.guid()));

    if (it != m_notebooksPendingAddOrUpdate.end()) {
        Q_UNUSED(m_notebooksPendingAddOrUpdate.erase(it))
    }
}

void RemoteToLocalSynchronizationManager::unregisterNotePendingAddOrUpdate(
    const Note & note)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::unregisterNotePendingAddOrUpdate:"
        " " << note);

    if (!note.hasGuid()) {
        return;
    }

    auto it = std::find_if(
        m_notesPendingAddOrUpdate.begin(), m_notesPendingAddOrUpdate.end(),
        CompareItemByGuid<NotesList::value_type>(note.guid()));

    if (it != m_notesPendingAddOrUpdate.end()) {
        Q_UNUSED(m_notesPendingAddOrUpdate.erase(it))
    }
}

void RemoteToLocalSynchronizationManager::unregisterNotePendingAddOrUpdate(
    const qevercloud::Note & note)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::unregisterNotePendingAddOrUpdate:"
        " " << note);

    if (!note.guid.isSet()) {
        return;
    }

    auto it = std::find_if(
        m_notesPendingAddOrUpdate.begin(), m_notesPendingAddOrUpdate.end(),
        CompareItemByGuid<NotesList::value_type>(note.guid.ref()));

    if (it != m_notesPendingAddOrUpdate.end()) {
        Q_UNUSED(m_notesPendingAddOrUpdate.erase(it))
    }
}

void RemoteToLocalSynchronizationManager::unregisterResourcePendingAddOrUpdate(
    const Resource & resource)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager"
            << "::unregisterResourcePendingAddOrUpdate: " << resource);

    if (!resource.hasGuid()) {
        return;
    }

    auto it = std::find_if(
        m_resourcesPendingAddOrUpdate.begin(),
        m_resourcesPendingAddOrUpdate.end(),
        CompareItemByGuid<ResourcesList::value_type>(resource.guid()));

    if (it != m_resourcesPendingAddOrUpdate.end()) {
        Q_UNUSED(m_resourcesPendingAddOrUpdate.erase(it))
    }
}

void RemoteToLocalSynchronizationManager::overrideLocalNoteWithRemoteNote(
    Note & localNote, const qevercloud::Note & remoteNote) const
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager"
            << "::overrideLocalNoteWithRemoteNote: local note = " << localNote
            << "\nRemote note: " << remoteNote);

    // Need to clear out the tag local uids from the local note so that
    // the local storage uses tag guids list from the remote note instead
    localNote.setTagLocalUids(QStringList());

    // NOTE: dealing with resources is tricky: need to not screw up
    // the local uids of note's resources
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
    for (auto & resource: resources) {
        if (!resource.hasGuid()) {
            continue;
        }

        bool foundResource = false;
        for (const auto & updatedResource: ::qAsConst(updatedResources)) {
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
    for (const auto & updatedResource: ::qAsConst(updatedResources)) {
        if (Q_UNLIKELY(!updatedResource.guid.isSet())) {
            QNWARNING(
                "synchronization:remote_to_local",
                "Skipping resource "
                    << "from remote note without guid: " << updatedResource);
            continue;
        }

        const Resource * pExistingResource = nullptr;
        for (const auto & resource: ::qAsConst(resources)) {
            if (resource.hasGuid() &&
                (resource.guid() == updatedResource.guid.ref())) {
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
    QNTRACE(
        "synchronization:remote_to_local",
        "Local note after overriding: " << localNote);
}

void RemoteToLocalSynchronizationManager::processResourceConflictAsNoteConflict(
    Note & remoteNote, const Note & localConflictingNote,
    Resource & remoteNoteResource)
{
    QString authToken;
    ErrorString errorDescription;
    auto * pNoteStore =
        noteStoreForNote(remoteNote, authToken, errorDescription);

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

    qint32 errorCode = pNoteStore->getResource(
        withDataBody, withRecognitionDataBody, withAlternateDataBody,
        withAttributes, authToken, remoteNoteResource, errorDescription,
        rateLimitSeconds);

    if (errorCode ==
        static_cast<qint32>(qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED))
    {
        if (Q_UNLIKELY(rateLimitSeconds < 0)) {
            errorDescription.setBase(
                QT_TR_NOOP("Rate limit reached but the number "
                           "of seconds to wait is incorrect"));
            errorDescription.details() = QString::number(rateLimitSeconds);
            Q_EMIT failure(errorDescription);
            return;
        }

        int timerId = startTimer(secondsToMilliseconds(rateLimitSeconds));
        if (Q_UNLIKELY(timerId == 0)) {
            errorDescription.setBase(
                QT_TR_NOOP("Failed to start a timer to postpone "
                           "the Evernote API call due to rate "
                           "limit exceeding"));
            Q_EMIT failure(errorDescription);
            return;
        }

        auto & data =
            m_postponedConflictingResourceDataPerAPICallPostponeTimerId
                [timerId];
        data.m_remoteNote = remoteNote;
        data.m_localConflictingNote = localConflictingNote;
        data.m_remoteNoteResourceWithoutFullData = remoteNoteResource;

        Q_EMIT rateLimitExceeded(rateLimitSeconds);
        return;
    }
    else if (
        errorCode ==
        static_cast<qint32>(qevercloud::EDAMErrorCode::AUTH_EXPIRED))
    {
        handleAuthExpiration();
        return;
    }
    else if (errorCode != 0) {
        ErrorString errorMessage(
            QT_TR_NOOP("Failed to download full resource data"));
        errorMessage.additionalBases().append(errorDescription.base());
        errorMessage.additionalBases().append(
            errorDescription.additionalBases());
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
    for (int i = 0; i < numResources; ++i) {
        const Resource & existingResource = resources[i];
        if (existingResource.hasGuid() &&
            (existingResource.guid() == remoteNoteResource.guid()))
        {
            resourceIndex = i;
            break;
        }
    }

    if (resourceIndex < 0) {
        remoteNote.addResource(remoteNoteResource);
    }
    else {
        QList<Resource> noteResources = remoteNote.resources();
        noteResources[resourceIndex] = remoteNoteResource;
        remoteNote.setResources(noteResources);
    }

    // Update remote note
    registerNotePendingAddOrUpdate(remoteNote);
    QUuid updateNoteRequestId = QUuid::createUuid();
    Q_UNUSED(m_updateNoteRequestIds.insert(updateNoteRequestId))

    LocalStorageManager::UpdateNoteOptions options(
        LocalStorageManager::UpdateNoteOption::UpdateResourceMetadata |
        LocalStorageManager::UpdateNoteOption::UpdateResourceBinaryData |
        LocalStorageManager::UpdateNoteOption::UpdateTags);

    QNTRACE(
        "synchronization:remote_to_local",
        "Emitting the request to update "
            << "the remote note in the local storage: request id = "
            << updateNoteRequestId << ", note; " << remoteNote);

    Q_EMIT updateNote(remoteNote, options, updateNoteRequestId);

    // Add local conflicting note
    emitAddRequest(localConflictingNote);
}

void RemoteToLocalSynchronizationManager::syncNextTagPendingProcessing()
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::syncNextTagPendingProcessing");

    if (m_tagsPendingProcessing.isEmpty()) {
        QNDEBUG(
            "synchronization:remote_to_local",
            "No tags pending for "
                << "processing, nothing more to sync");
        return;
    }

    qevercloud::Tag frontTag = m_tagsPendingProcessing.takeFirst();
    emitFindByGuidRequest(frontTag);
}

void RemoteToLocalSynchronizationManager::removeNoteResourcesFromSyncChunks(
    const Note & note)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager"
            << "::removeNoteResourcesFromSyncChunks: note guid = "
            << (note.hasGuid() ? note.guid() : QStringLiteral("<not set>"))
            << ", local uid = " << note.localUid());

    if (!note.hasResources()) {
        return;
    }

    auto & syncChunks =
        (syncingLinkedNotebooksContent() ? m_linkedNotebookSyncChunks
                                         : m_syncChunks);

    QList<Resource> resources = note.resources();
    for (const auto & resource: qAsConst(resources)) {
        removeResourceFromSyncChunks(resource, syncChunks);
    }
}

void RemoteToLocalSynchronizationManager::removeResourceFromSyncChunks(
    const Resource & resource, QVector<qevercloud::SyncChunk> & syncChunks)
{
    if (Q_UNLIKELY(!resource.hasGuid())) {
        QNWARNING(
            "synchronization:remote_to_local",
            "Can't remove resource "
                << "from sync chunks as it has no guid: " << resource);
        return;
    }

    for (auto & syncChunk: syncChunks) {
        if (!syncChunk.resources.isSet()) {
            continue;
        }

        for (auto rit = syncChunk.resources->begin(),
                  rend = syncChunk.resources->end();
             rit != rend; ++rit)
        {
            if (rit->guid.isSet() && (rit->guid.ref() == resource.guid())) {
                Q_UNUSED(syncChunk.resources->erase(rit))
                QNDEBUG(
                    "synchronization:remote_to_local",
                    "Note: removed "
                        << "resource from sync chunk because it was downloaded "
                        << "along with the note containing it: " << resource);
                break;
            }
        }
    }
}

void RemoteToLocalSynchronizationManager::junkFullSyncStaleDataItemsExpunger(
    FullSyncStaleDataItemsExpunger & expunger)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager"
            << "::junkFullSyncStaleDataItemsExpunger: linked notebook guid = "
            << expunger.linkedNotebookGuid());

    QObject::disconnect(
        &expunger, &FullSyncStaleDataItemsExpunger::finished, this,
        &RemoteToLocalSynchronizationManager::
            onFullSyncStaleDataItemsExpungerFinished);

    QObject::disconnect(
        &expunger, &FullSyncStaleDataItemsExpunger::failure, this,
        &RemoteToLocalSynchronizationManager::
            onFullSyncStaleDataItemsExpungerFailure);

    expunger.setParent(nullptr);
    expunger.deleteLater();
}

INoteStore * RemoteToLocalSynchronizationManager::noteStoreForNote(
    const Note & note, QString & authToken, ErrorString & errorDescription)
{
    authToken.resize(0);

    if (!note.hasGuid()) {
        errorDescription.setBase(
            QT_TR_NOOP("Detected the attempt to get full "
                       "note's data for a note without guid"));
        APPEND_NOTE_DETAILS(errorDescription, note)
        QNWARNING(
            "synchronization:remote_to_local",
            errorDescription << ": " << note);
        return nullptr;
    }

    if (!note.hasNotebookGuid() && syncingLinkedNotebooksContent()) {
        errorDescription.setBase(
            QT_TR_NOOP("Detected the attempt to get full note's "
                       "data for a note without notebook guid "
                       "while syncing linked notebooks content"));
        APPEND_NOTE_DETAILS(errorDescription, note)
        QNWARNING(
            "synchronization:remote_to_local",
            errorDescription << ": " << note);
        return nullptr;
    }

    // Need to find out which note store is required - the one for user's own
    // account or the one for the stuff from some linked notebook

    INoteStore * pNoteStore = nullptr;

    auto linkedNotebookGuidIt = m_linkedNotebookGuidsByNotebookGuids.end();
    if (note.hasNotebookGuid()) {
        linkedNotebookGuidIt =
            m_linkedNotebookGuidsByNotebookGuids.find(note.notebookGuid());
    }

    if (linkedNotebookGuidIt == m_linkedNotebookGuidsByNotebookGuids.end()) {
        QNDEBUG(
            "synchronization:remote_to_local",
            "Found no linked notebook "
                << "corresponding to notebook guid "
                << (note.hasNotebookGuid() ? note.notebookGuid()
                                           : QStringLiteral("<null>"))
                << ", using the note store for user's own account");

        pNoteStore = &(m_manager.noteStore());
        connectToUserOwnNoteStore(pNoteStore);
        authToken = m_authenticationToken;
        return pNoteStore;
    }

    const QString & linkedNotebookGuid = linkedNotebookGuidIt.value();

    auto authTokenIt =
        m_authenticationTokensAndShardIdsByLinkedNotebookGuid.find(
            linkedNotebookGuid);

    if (Q_UNLIKELY(
            authTokenIt ==
            m_authenticationTokensAndShardIdsByLinkedNotebookGuid.end()))
    {
        errorDescription.setBase(
            QT_TR_NOOP("Can't find the authentication token "
                       "corresponding to the linked notebook"));
        APPEND_NOTE_DETAILS(errorDescription, note)
        QNWARNING(
            "synchronization:remote_to_local",
            errorDescription << ": " << note);
        return nullptr;
    }

    authToken = authTokenIt.value().first;
    const QString & linkedNotebookShardId = authTokenIt.value().second;

    QString linkedNotebookNoteStoreUrl;
    for (const auto & linkedNotebook: qAsConst(m_allLinkedNotebooks)) {
        if (linkedNotebook.hasGuid() &&
            (linkedNotebook.guid() == linkedNotebookGuid) &&
            linkedNotebook.hasNoteStoreUrl())
        {
            linkedNotebookNoteStoreUrl = linkedNotebook.noteStoreUrl();
            break;
        }
    }

    if (linkedNotebookNoteStoreUrl.isEmpty()) {
        errorDescription.setBase(
            QT_TR_NOOP("Can't find the note store URL corresponding "
                       "to the linked notebook"));
        APPEND_NOTE_DETAILS(errorDescription, note)
        QNWARNING(
            "synchronization:remote_to_local",
            errorDescription << ": " << note);
        return nullptr;
    }

    LinkedNotebook linkedNotebook;
    linkedNotebook.setGuid(linkedNotebookGuid);
    linkedNotebook.setShardId(linkedNotebookShardId);
    linkedNotebook.setNoteStoreUrl(linkedNotebookNoteStoreUrl);
    pNoteStore = m_manager.noteStoreForLinkedNotebook(linkedNotebook);
    if (Q_UNLIKELY(!pNoteStore)) {
        errorDescription.setBase(
            QT_TR_NOOP("Can't find or create note store for "));
        APPEND_NOTE_DETAILS(errorDescription, note)
        QNWARNING(
            "synchronization:remote_to_local",
            errorDescription << ": " << note);
        return nullptr;
    }

    if (Q_UNLIKELY(pNoteStore->noteStoreUrl().isEmpty())) {
        errorDescription.setBase(
            QT_TR_NOOP("Internal error: empty note store url "
                       "for the linked notebook's note store"));
        APPEND_NOTE_DETAILS(errorDescription, note)
        QNWARNING(
            "synchronization:remote_to_local",
            errorDescription << ": " << note);
        return nullptr;
    }

    QObject::connect(
        pNoteStore, &INoteStore::getNoteAsyncFinished, this,
        &RemoteToLocalSynchronizationManager::onGetNoteAsyncFinished,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QNDEBUG(
        "synchronization:remote_to_local",
        "Using INoteStore corresponding "
            << "to linked notebook with guid " << linkedNotebookGuid
            << ", note store url = " << pNoteStore->noteStoreUrl());
    return pNoteStore;
}

void RemoteToLocalSynchronizationManager::connectToUserOwnNoteStore(
    INoteStore * pNoteStore)
{
    // Apparently QObject::connect takes long enough when called multiple
    // times for the same signal-slot pair so need to ensure it is done only
    // once
    if (m_connectedToUserOwnNoteStore) {
        return;
    }

    QObject::connect(
        pNoteStore, &INoteStore::getNoteAsyncFinished, this,
        &RemoteToLocalSynchronizationManager::onGetNoteAsyncFinished,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        pNoteStore, &INoteStore::getResourceAsyncFinished, this,
        &RemoteToLocalSynchronizationManager::onGetResourceAsyncFinished,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    m_connectedToUserOwnNoteStore = true;
}

void RemoteToLocalSynchronizationManager::
    checkAndRemoveInaccessibleParentTagGuidsForTagsFromLinkedNotebook(
        const QString & linkedNotebookGuid, const TagSyncCache & tagSyncCache)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager"
            << "::"
               "checkAndRemoveInaccessibleParentTagGuidsForTagsFromLinkedNotebo"
               "ok: "
            << "linked notebook guid = " << linkedNotebookGuid);

    const auto & nameByGuidHash = tagSyncCache.nameByGuidHash();

    auto & tagIndexByGuid = m_tags.get<ByGuid>();
    for (auto it = tagIndexByGuid.begin(), end = tagIndexByGuid.end();
         it != end; ++it)
    {
        const auto & tag = *it;
        if (Q_UNLIKELY(!tag.guid.isSet())) {
            continue;
        }

        if (!tag.parentGuid.isSet()) {
            continue;
        }

        auto linkedNotebookGuidIt =
            m_linkedNotebookGuidsByTagGuids.find(tag.guid.ref());

        if (linkedNotebookGuidIt == m_linkedNotebookGuidsByTagGuids.end()) {
            continue;
        }

        if (linkedNotebookGuidIt.value() != linkedNotebookGuid) {
            continue;
        }

        auto nameIt = nameByGuidHash.find(tag.parentGuid.ref());
        if (nameIt != nameByGuidHash.end()) {
            continue;
        }

        QNDEBUG(
            "synchronization:remote_to_local",
            "Tag with guid "
                << tag.parentGuid.ref() << " was not found within the tag sync "
                << "cache, removing it as parent guid from tag: " << tag);

        qevercloud::Tag tagCopy(tag);
        tagCopy.parentGuid.clear();
        tagIndexByGuid.replace(it, tagCopy);
    }
}

void RemoteToLocalSynchronizationManager::
    startFeedingDownloadedTagsToLocalStorageOneByOne(
        const TagsContainer & container)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager"
            << "::startFeedingDownloadedTagsToLocalStorageOneByOne");

    m_tagsPendingProcessing.reserve(static_cast<int>(container.size()));
    const auto & tagIndexByGuid = container.get<ByGuid>();
    for (const auto & tag: tagIndexByGuid) {
        m_tagsPendingProcessing << tag;
    }

    if (!sortTagsByParentChildRelations(m_tagsPendingProcessing)) {
        return;
    }

    /**
     * NOTE: parent tags need to be added to the local storage before their
     * children, otherwise the local storage database would have a constraint
     * failure; by now the tags are already sorted by parent-child relations
     * but they need to be processed one by one
     */

    syncNextTagPendingProcessing();
}

#define PRINT_SYNC_MODE(StreamType)                                            \
    StreamType & operator<<(                                                   \
        StreamType & strm,                                                     \
        const RemoteToLocalSynchronizationManager::SyncMode & obj)             \
    {                                                                          \
        switch (obj) {                                                         \
        case RemoteToLocalSynchronizationManager::SyncMode::FullSync:          \
            strm << "FullSync";                                                \
            break;                                                             \
        case RemoteToLocalSynchronizationManager::SyncMode::IncrementalSync:   \
            strm << "IncrementalSync";                                         \
            break;                                                             \
        default:                                                               \
            strm << "<unknown>";                                               \
            break;                                                             \
        }                                                                      \
        return strm;                                                           \
    }                                                                          \
    // PRINT_SYNC_MODE

PRINT_SYNC_MODE(QTextStream)
PRINT_SYNC_MODE(QDebug)

#undef PRINT_SYNC_MODE

template <>
void RemoteToLocalSynchronizationManager::
    appendDataElementsFromSyncChunkToContainer<TagsContainer>(
        const qevercloud::SyncChunk & syncChunk, TagsContainer & container)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager"
            << "::appendDataElementsFromSyncChunkToContainer: tags");

    if (syncChunk.tags.isSet()) {
        const auto & tags = syncChunk.tags.ref();
        QNDEBUG(
            "synchronization:remote_to_local",
            "Appending " << tags.size() << " tags");

        for (const auto & tag: ::qAsConst(tags)) {
            container.insert(tag);
        }

        for (auto it = m_expungedTags.begin(); it != m_expungedTags.end();) {
            auto tagIt = std::find_if(
                tags.constBegin(), tags.constEnd(),
                CompareItemByGuid<qevercloud::Tag>(*it));

            if (tagIt == tags.constEnd()) {
                ++it;
            }
            else {
                it = m_expungedTags.erase(it);
            }
        }
    }

    if (syncChunk.expungedTags.isSet()) {
        const auto & expungedTags = syncChunk.expungedTags.ref();
        QNDEBUG(
            "synchronization:remote_to_local",
            "Processing " << expungedTags.size() << " expunged tags");

        auto & tagIndexByGuid = container.get<ByGuid>();

        const auto expungedTagsEnd = expungedTags.end();
        for (auto eit = expungedTags.begin(); eit != expungedTagsEnd; ++eit) {
            auto it = tagIndexByGuid.find(*eit);
            if (it != tagIndexByGuid.end()) {
                Q_UNUSED(tagIndexByGuid.erase(it))
            }

            for (auto iit = container.begin(), iend = container.end();
                 iit != iend; ++iit) {
                const qevercloud::Tag & tag = *iit;
                if (tag.parentGuid.isSet() && (tag.parentGuid.ref() == *eit)) {
                    qevercloud::Tag tagWithoutParentGuid(tag);
                    tagWithoutParentGuid.parentGuid.clear();
                    container.replace(iit, tagWithoutParentGuid);
                }
            }
        }
    }
}

template <>
void RemoteToLocalSynchronizationManager::
    appendDataElementsFromSyncChunkToContainer<
        RemoteToLocalSynchronizationManager::SavedSearchesList>(
        const qevercloud::SyncChunk & syncChunk,
        RemoteToLocalSynchronizationManager::SavedSearchesList & container)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager"
            << "::appendDataElementsFromSyncChunkToContainer: saved searches");

    if (syncChunk.searches.isSet()) {
        const auto & savedSearches = syncChunk.searches.ref();

        QNDEBUG(
            "synchronization:remote_to_local",
            "Appending " << savedSearches.size() << " saved searches");

        container.append(savedSearches);

        for (auto it = m_expungedSavedSearches.begin();
             it != m_expungedSavedSearches.end();)
        {
            auto searchIt = std::find_if(
                savedSearches.constBegin(), savedSearches.constEnd(),
                CompareItemByGuid<qevercloud::SavedSearch>(*it));

            if (searchIt == savedSearches.constEnd()) {
                ++it;
            }
            else {
                it = m_expungedSavedSearches.erase(it);
            }
        }
    }

    if (syncChunk.expungedSearches.isSet()) {
        const auto & expungedSearches = syncChunk.expungedSearches.ref();
        QNDEBUG(
            "synchronization:remote_to_local",
            "Processing " << expungedSearches.size()
                          << " expunged saved searches");

        const auto expungedSearchesEnd = expungedSearches.end();
        for (auto eit = expungedSearches.begin(); eit != expungedSearchesEnd;
             ++eit) {
            auto it = std::find_if(
                container.begin(), container.end(),
                CompareItemByGuid<qevercloud::SavedSearch>(*eit));

            if (it != container.end()) {
                Q_UNUSED(container.erase(it));
            }
        }
    }
}

template <>
void RemoteToLocalSynchronizationManager::
    appendDataElementsFromSyncChunkToContainer<
        RemoteToLocalSynchronizationManager::LinkedNotebooksList>(
        const qevercloud::SyncChunk & syncChunk,
        RemoteToLocalSynchronizationManager::LinkedNotebooksList & container)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager"
            << "::appendDataElementsFromSyncChunkToContainer: "
            << "linked notebooks");

    if (syncChunk.linkedNotebooks.isSet()) {
        const auto & linkedNotebooks = syncChunk.linkedNotebooks.ref();

        QNDEBUG(
            "synchronization:remote_to_local",
            "Appending " << linkedNotebooks.size() << " linked notebooks");

        container.append(linkedNotebooks);

        for (auto it = m_expungedLinkedNotebooks.begin();
             it != m_expungedLinkedNotebooks.end();)
        {
            auto linkedNotebookIt = std::find_if(
                linkedNotebooks.constBegin(), linkedNotebooks.constEnd(),
                CompareItemByGuid<qevercloud::LinkedNotebook>(*it));

            if (linkedNotebookIt == linkedNotebooks.constEnd()) {
                ++it;
            }
            else {
                it = m_expungedLinkedNotebooks.erase(it);
            }
        }
    }

    if (syncChunk.expungedLinkedNotebooks.isSet()) {
        const auto & expungedLinkedNotebooks =
            syncChunk.expungedLinkedNotebooks.ref();

        QNDEBUG(
            "synchronization:remote_to_local",
            "Processing " << expungedLinkedNotebooks.size()
                          << " expunged linked notebooks");

        const auto expungedLinkedNotebooksEnd = expungedLinkedNotebooks.end();
        for (auto eit = expungedLinkedNotebooks.begin();
             eit != expungedLinkedNotebooksEnd; ++eit)
        {
            auto it = std::find_if(
                container.begin(), container.end(),
                CompareItemByGuid<qevercloud::LinkedNotebook>(*eit));

            if (it != container.end()) {
                Q_UNUSED(container.erase(it));
            }
        }
    }
}

template <>
void RemoteToLocalSynchronizationManager::
    appendDataElementsFromSyncChunkToContainer<
        RemoteToLocalSynchronizationManager::NotebooksList>(
        const qevercloud::SyncChunk & syncChunk,
        RemoteToLocalSynchronizationManager::NotebooksList & container)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager"
            << "::appendDataElementsFromSyncChunkToContainer: notebooks");

    if (syncChunk.notebooks.isSet()) {
        const auto & notebooks = syncChunk.notebooks.ref();

        QNDEBUG(
            "synchronization:remote_to_local",
            "Appending " << notebooks.size() << " notebooks");

        container.append(notebooks);

        for (auto it = m_expungedNotebooks.begin();
             it != m_expungedNotebooks.end();) {
            auto notebookIt = std::find_if(
                notebooks.constBegin(), notebooks.constEnd(),
                CompareItemByGuid<qevercloud::Notebook>(*it));

            if (notebookIt == notebooks.constEnd()) {
                ++it;
            }
            else {
                it = m_expungedNotebooks.erase(it);
            }
        }
    }

    if (syncChunk.expungedNotebooks.isSet()) {
        const auto & expungedNotebooks = syncChunk.expungedNotebooks.ref();

        QNDEBUG(
            "synchronization:remote_to_local",
            "Processing " << expungedNotebooks.size() << " expunged notebooks");

        const auto expungedNotebooksEnd = expungedNotebooks.end();
        for (auto eit = expungedNotebooks.begin(); eit != expungedNotebooksEnd;
             ++eit) {
            auto it = std::find_if(
                container.begin(), container.end(),
                CompareItemByGuid<qevercloud::Notebook>(*eit));

            if (it != container.end()) {
                Q_UNUSED(container.erase(it));
            }
        }
    }
}

template <>
void RemoteToLocalSynchronizationManager::
    appendDataElementsFromSyncChunkToContainer<
        RemoteToLocalSynchronizationManager::NotesList>(
        const qevercloud::SyncChunk & syncChunk,
        RemoteToLocalSynchronizationManager::NotesList & container)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager"
            << "::appendDataElementsFromSyncChunkToContainer: notes");

    if (syncChunk.notes.isSet()) {
        const auto & syncChunkNotes = syncChunk.notes.ref();

        QNDEBUG(
            "synchronization:remote_to_local",
            "Appending " << syncChunkNotes.size() << " notes");

        container.append(syncChunkNotes);

        for (auto it = m_expungedNotes.begin(); it != m_expungedNotes.end();) {
            auto noteIt = std::find_if(
                syncChunkNotes.constBegin(), syncChunkNotes.constEnd(),
                CompareItemByGuid<qevercloud::Note>(*it));

            if (noteIt == syncChunkNotes.constEnd()) {
                ++it;
            }
            else {
                it = m_expungedNotes.erase(it);
            }
        }
    }

    if (syncChunk.expungedNotes.isSet()) {
        const auto & expungedNotes = syncChunk.expungedNotes.ref();

        QNDEBUG(
            "synchronization:remote_to_local",
            "Processing " << expungedNotes.size() << " expunged notes");

        const auto expungedNotesEnd = expungedNotes.end();
        for (auto eit = expungedNotes.begin(); eit != expungedNotesEnd; ++eit) {
            auto it = std::find_if(
                container.begin(), container.end(),
                CompareItemByGuid<qevercloud::Note>(*eit));

            if (it != container.end()) {
                Q_UNUSED(container.erase(it));
            }
        }
    }

    if (syncChunk.expungedNotebooks.isSet()) {
        const auto & expungedNotebooks = syncChunk.expungedNotebooks.ref();

        QNDEBUG(
            "synchronization:remote_to_local",
            "Processing " << expungedNotebooks.size() << " expunged notebooks");

        const auto expungedNotebooksEnd = expungedNotebooks.end();
        for (auto eit = expungedNotebooks.begin(); eit != expungedNotebooksEnd;
             ++eit) {
            const QString & expungedNotebookGuid = *eit;

            for (auto it = container.begin(); it != container.end();) {
                auto & note = *it;
                if (note.notebookGuid.isSet() &&
                    (note.notebookGuid.ref() == expungedNotebookGuid))
                {
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
void RemoteToLocalSynchronizationManager::
    appendDataElementsFromSyncChunkToContainer<
        RemoteToLocalSynchronizationManager::ResourcesList>(
        const qevercloud::SyncChunk & syncChunk,
        RemoteToLocalSynchronizationManager::ResourcesList & container)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager"
            << "::appendDataElementsFromSyncChunkToContainer: resources");

    if (!syncChunk.resources.isSet()) {
        return;
    }

    const auto & resources = syncChunk.resources.ref();
    QNDEBUG(
        "synchronization:remote_to_local",
        "Appending " << resources.size() << " resources");

    // Need to filter out those resources which belong to the notes which will
    // be downloaded along with their whole content, resources included or to
    // the notes which have already been downloaded
    QList<qevercloud::Resource> filteredResources;
    filteredResources.reserve(resources.size());

    for (const auto & resource: ::qAsConst(resources)) {
        if (Q_UNLIKELY(!resource.noteGuid.isSet())) {
            QNWARNING(
                "synchronization:remote_to_local",
                "Skipping resource "
                    << "without note guid: " << resource);
            continue;
        }

        QNTRACE(
            "synchronization:remote_to_local",
            "Checking whether resource "
                << "belongs to a note pending downloading or already "
                   "downloaded "
                << "one: " << resource);

        auto ngit =
            m_guidsOfProcessedNonExpungedNotes.find(resource.noteGuid.ref());

        if (ngit != m_guidsOfProcessedNonExpungedNotes.end()) {
            QNTRACE(
                "synchronization:remote_to_local",
                "Skipping resource as "
                    << "it belongs to the note which whole content has already "
                    << "been downloaded: " << resource);
            continue;
        }

        bool foundNote = false;
        for (const auto & note: ::qAsConst(m_notes)) {
            QNTRACE(
                "synchronization:remote_to_local", "Checking note: " << note);

            if (Q_UNLIKELY(!note.guid.isSet())) {
                continue;
            }

            if (note.guid.ref() == resource.noteGuid.ref()) {
                QNTRACE(
                    "synchronization:remote_to_local",
                    "Resource belongs "
                        << "to a note pending downloading: " << note);
                foundNote = true;
                break;
            }
        }

        if (foundNote) {
            QNTRACE(
                "synchronization:remote_to_local",
                "Skipping resource as "
                    << "it belongs to the note which while content would be "
                    << "downloaded a bit later: " << resource);
            continue;
        }

        QNTRACE(
            "synchronization:remote_to_local",
            "Appending the resource "
                << "which does not belong to any note pending downloading");
        filteredResources << resource;
    }

    QNTRACE(
        "synchronization:remote_to_local",
        "Will append " << filteredResources.size()
                       << " resources to the container");
    container.append(filteredResources);
}

template <class ContainerType, class ElementType>
typename ContainerType::iterator
RemoteToLocalSynchronizationManager::findItemByName(
    ContainerType & container, const ElementType & element,
    const QString & /* targetLinkedNotebookGuid */, const QString & typeName)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::findItemByName<" << typeName
                                                               << ">");

    if (Q_UNLIKELY(!element.hasName())) {
        ErrorString errorDescription(QT_TRANSLATE_NOOP(
            "RemoteToLocalSynchronizationManager",
            "Internal error: can't find data item from sync chunks by name: "
            "data item has no name"));

        errorDescription.appendBase(QT_TRANSLATE_NOOP(
            "RemoteToLocalSynchronizationManager", "item type is"));

        errorDescription.details() += typeName;

        QNWARNING(
            "synchronization:remote_to_local",
            errorDescription << ": " << element);

        Q_EMIT failure(errorDescription);
        return container.end();
    }

    if (container.isEmpty()) {
        SET_EMPTY_PENDING_LIST_ERROR();
        Q_EMIT failure(errorDescription);
        return container.end();
    }

    auto it = std::find_if(
        container.begin(), container.end(),
        CompareItemByName<typename ContainerType::value_type>(element.name()));

    if (it == container.end()) {
        SET_CANT_FIND_IN_PENDING_LIST_ERROR();
        Q_EMIT failure(errorDescription);
        return container.end();
    }

    return it;
}

template <>
TagsContainer::iterator
RemoteToLocalSynchronizationManager::findItemByName<TagsContainer, Tag>(
    TagsContainer & tagsContainer, const Tag & element,
    const QString & targetLinkedNotebookGuid, const QString & typeName)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::findItemByName<Tag>");

    Q_UNUSED(typeName)

    if (Q_UNLIKELY(!element.hasName())) {
        ErrorString errorDescription(QT_TRANSLATE_NOOP(
            "RemoteToLocalSynchronizationManager",
            "Internal error: can't find tag from sync chunks by name, tag has "
            "no name"));

        QNWARNING(
            "synchronization:remote_to_local",
            errorDescription << ": " << element);

        Q_EMIT failure(errorDescription);
        return tagsContainer.end();
    }

    if (tagsContainer.empty()) {
        SET_EMPTY_PENDING_LIST_ERROR();
        Q_EMIT failure(errorDescription);
        return tagsContainer.end();
    }

    auto & tagIndexByName = tagsContainer.get<ByName>();
    qevercloud::Optional<QString> optName;
    optName = element.name();
    auto range = tagIndexByName.equal_range(optName);
    if (range.first == range.second) {
        ErrorString errorDescription(QT_TRANSLATE_NOOP(
            "RemoteToLocalSynchronizationManager",
            "Internal error: can't find tag from sync chunks by name"));

        errorDescription.details() = optName;

        QNWARNING(
            "synchronization:remote_to_local",
            errorDescription << ": " << element << "\n"
                             << dumpTagsContainer(tagsContainer));

        Q_EMIT failure(errorDescription);
        return tagsContainer.end();
    }

    bool foundMatchingTag = false;
    auto it = range.first;
    for (; it != range.second; ++it) {
        const qevercloud::Tag & tag = *it;
        if (Q_UNLIKELY(!tag.guid.isSet())) {
            continue;
        }

        auto linkedNotebookGuidIt =
            m_linkedNotebookGuidsByTagGuids.find(tag.guid.ref());

        if (targetLinkedNotebookGuid.isEmpty() &&
            (linkedNotebookGuidIt == m_linkedNotebookGuidsByTagGuids.end()))
        {
            foundMatchingTag = true;
            break;
        }

        if ((linkedNotebookGuidIt != m_linkedNotebookGuidsByTagGuids.end()) &&
            (linkedNotebookGuidIt.value() == targetLinkedNotebookGuid))
        {
            foundMatchingTag = true;
            break;
        }
    }

    if (Q_UNLIKELY(!foundMatchingTag)) {
        ErrorString errorDescription(QT_TRANSLATE_NOOP(
            "RemoteToLocalSynchronizationManager",
            "Internal error: can't find tag from sync chunks by name, failed "
            "to find tag matching by linked notebook guid"));

        errorDescription.details() = optName;
        QNWARNING(
            "synchronization:remote_to_local",
            errorDescription
                << ", linked notebook guid = " << targetLinkedNotebookGuid
                << ", tag: " << element << "\n"
                << dumpTagsContainer(tagsContainer) << "\n"
                << dumpLinkedNotebookGuidsByTagGuids(
                       m_linkedNotebookGuidsByTagGuids));

        Q_EMIT failure(errorDescription);
        return tagsContainer.end();
    }

    auto cit = tagsContainer.project<0>(it);
    if (cit == tagsContainer.end()) {
        ErrorString errorDescription(QT_TRANSLATE_NOOP(
            "RemoteToLocalSynchronizationManager",
            "Internal error: can't find tag from sync chunks by name, failed "
            "to project tags container iterator"));

        errorDescription.details() = optName;

        QNWARNING(
            "synchronization:remote_to_local",
            errorDescription << ": " << element);

        Q_EMIT failure(errorDescription);
        return tagsContainer.end();
    }

    return cit;
}

template <>
RemoteToLocalSynchronizationManager::NotebooksList::iterator
RemoteToLocalSynchronizationManager::findItemByName<
    RemoteToLocalSynchronizationManager::NotebooksList, Notebook>(
    NotebooksList & container, const Notebook & element,
    const QString & targetLinkedNotebookGuid, const QString & typeName)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::findItemByName<Notebook>");

    Q_UNUSED(typeName)

    if (!element.hasName()) {
        ErrorString errorDescription(QT_TRANSLATE_NOOP(
            "RemoteToLocalSynchronizationManager",
            "Internal error: can't find notebook from sync chunks by name, "
            "notebook has no name"));

        QNWARNING(
            "synchronization:remote_to_local",
            errorDescription << ": " << element);

        Q_EMIT failure(errorDescription);
        return container.end();
    }

    if (container.isEmpty()) {
        SET_EMPTY_PENDING_LIST_ERROR();
        Q_EMIT failure(errorDescription);
        return container.end();
    }

    auto it = container.begin();
    for (auto end = container.end(); it != end; ++it) {
        const auto & notebook = *it;
        if (!notebook.name.isSet()) {
            continue;
        }

        if (notebook.name.ref().toUpper() != element.name().toUpper()) {
            continue;
        }

        if (!targetLinkedNotebookGuid.isEmpty()) {
            /**
             * If we got here, we are syncing notebooks from linked notebooks
             * As notebook name is unique only within user's own account or
             * within a single linked notebook, there can be name collisions
             * between linked notebooks. So need to ensure the linked notebook
             * guid corresponding to the current notebook is the same as the
             * target linked notebook guid
             */

            if (!notebook.guid.isSet()) {
                continue;
            }

            auto linkedNotebookGuidIt =
                m_linkedNotebookGuidsByNotebookGuids.find(notebook.guid.ref());

            if (linkedNotebookGuidIt ==
                m_linkedNotebookGuidsByNotebookGuids.end()) {
                continue;
            }

            if (linkedNotebookGuidIt.value() != targetLinkedNotebookGuid) {
                continue;
            }
        }

        break;
    }

    return it;
}

template <class ContainerType, class ElementType>
typename ContainerType::iterator
RemoteToLocalSynchronizationManager::findItemByGuid(
    ContainerType & container, const ElementType & element,
    const QString & typeName)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::findItemByGuid<" << typeName
                                                               << ">");

    if (Q_UNLIKELY(!element.hasGuid())) {
        SET_CANT_FIND_BY_GUID_ERROR()
        Q_EMIT failure(errorDescription);
        return container.end();
    }

    if (container.isEmpty()) {
        SET_EMPTY_PENDING_LIST_ERROR();
        Q_EMIT failure(errorDescription);
        return container.end();
    }

    auto it = std::find_if(
        container.begin(), container.end(),
        CompareItemByGuid<typename ContainerType::value_type>(element.guid()));

    if (it == container.end()) {
        SET_CANT_FIND_IN_PENDING_LIST_ERROR();
        Q_EMIT failure(errorDescription);
        return container.end();
    }

    return it;
}

template <>
TagsContainer::iterator
RemoteToLocalSynchronizationManager::findItemByGuid<TagsContainer, Tag>(
    TagsContainer & tagsContainer, const Tag & element,
    const QString & typeName)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::findItemByGuid"
            << "<TagsContainer, Tag>");

    Q_UNUSED(typeName)

    if (Q_UNLIKELY(!element.hasGuid())) {
        ErrorString errorDescription(QT_TRANSLATE_NOOP(
            "RemoteToLocalSynchronizationManager",
            "Internal error: can't find tag from sync chunks by guid, tag has "
            "no guid"));

        QNWARNING(
            "synchronization:remote_to_local",
            errorDescription << ": " << element);

        Q_EMIT failure(errorDescription);
        return tagsContainer.end();
    }

    if (tagsContainer.empty()) {
        SET_EMPTY_PENDING_LIST_ERROR();
        Q_EMIT failure(errorDescription);
        return tagsContainer.end();
    }

    auto & tagIndexByGuid = tagsContainer.get<ByGuid>();
    auto it = tagIndexByGuid.find(element.guid());
    if (it == tagIndexByGuid.end()) {
        ErrorString errorDescription(QT_TRANSLATE_NOOP(
            "RemoteToLocalSynchronizationManager",
            "Internal error: can't find tag from sync chunks by guid"));

        QNWARNING(
            "synchronization:remote_to_local",
            errorDescription << ": " << element << "\n"
                             << dumpTagsContainer(tagsContainer));

        Q_EMIT failure(errorDescription);
        return tagsContainer.end();
    }

    auto cit = tagsContainer.project<0>(it);
    if (cit == tagsContainer.end()) {
        ErrorString errorDescription(QT_TRANSLATE_NOOP(
            "RemoteToLocalSynchronizationManager",
            "Internal error: can't find tag from sync chunks by guid, failed "
            "to project tags container iterator"));

        QNWARNING(
            "synchronization:remote_to_local",
            errorDescription << ": " << element);

        Q_EMIT failure(errorDescription);
        return tagsContainer.end();
    }

    return cit;
}

template <class T>
bool RemoteToLocalSynchronizationManager::CompareItemByName<T>::operator()(
    const T & item) const
{
    if (item.name.isSet()) {
        return (m_name.toUpper() == item.name.ref().toUpper());
    }
    else {
        return false;
    }
}

template <>
bool RemoteToLocalSynchronizationManager::CompareItemByName<
    qevercloud::Note>::operator()(const qevercloud::Note & item) const
{
    if (item.title.isSet()) {
        return (m_name.toUpper() == item.title->toUpper());
    }
    else {
        return false;
    }
}

template <class T>
bool RemoteToLocalSynchronizationManager::CompareItemByGuid<T>::operator()(
    const T & item) const
{
    if (item.guid.isSet()) {
        return (m_guid == item.guid.ref());
    }
    else {
        return false;
    }
}

template <>
bool RemoteToLocalSynchronizationManager::CompareItemByGuid<
    LinkedNotebook>::operator()(const LinkedNotebook & item) const
{
    if (item.hasGuid()) {
        return (m_guid == item.guid());
    }
    else {
        return false;
    }
}

template <class ElementType>
void RemoteToLocalSynchronizationManager::extractExpungedElementsFromSyncChunk(
    const qevercloud::SyncChunk & syncChunk,
    QList<QString> & expungedElementGuids)
{
    Q_UNUSED(syncChunk);
    Q_UNUSED(expungedElementGuids);
    // do nothing by default
}

template <>
void RemoteToLocalSynchronizationManager::extractExpungedElementsFromSyncChunk<
    Tag>(
    const qevercloud::SyncChunk & syncChunk,
    QList<QString> & expungedElementGuids)
{
    if (syncChunk.expungedTags.isSet()) {
        expungedElementGuids = syncChunk.expungedTags.ref();
    }
}

template <>
void RemoteToLocalSynchronizationManager::extractExpungedElementsFromSyncChunk<
    SavedSearch>(
    const qevercloud::SyncChunk & syncChunk,
    QList<QString> & expungedElementGuids)
{
    if (syncChunk.expungedSearches.isSet()) {
        expungedElementGuids = syncChunk.expungedSearches.ref();
    }
}

template <>
void RemoteToLocalSynchronizationManager::extractExpungedElementsFromSyncChunk<
    Notebook>(
    const qevercloud::SyncChunk & syncChunk,
    QList<QString> & expungedElementGuids)
{
    if (syncChunk.expungedNotebooks.isSet()) {
        expungedElementGuids = syncChunk.expungedNotebooks.ref();
    }
}

template <>
void RemoteToLocalSynchronizationManager::extractExpungedElementsFromSyncChunk<
    Note>(
    const qevercloud::SyncChunk & syncChunk,
    QList<QString> & expungedElementGuids)
{
    if (syncChunk.expungedNotes.isSet()) {
        expungedElementGuids = syncChunk.expungedNotes.ref();
    }
}

template <>
void RemoteToLocalSynchronizationManager::extractExpungedElementsFromSyncChunk<
    LinkedNotebook>(
    const qevercloud::SyncChunk & syncChunk,
    QList<QString> & expungedElementGuids)
{
    if (syncChunk.expungedLinkedNotebooks.isSet()) {
        expungedElementGuids = syncChunk.expungedLinkedNotebooks.ref();
    }
}

template <>
void RemoteToLocalSynchronizationManager::emitFindByNameRequest<Tag>(
    const Tag & tag, const QString & linkedNotebookGuid)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::emitFindByNameRequest<Tag>: "
            << tag << "\nLinked notebook guid = " << linkedNotebookGuid);

    if (!tag.hasName()) {
        ErrorString errorDescription(QT_TRANSLATE_NOOP(
            "RemoteToLocalSynchronizationManager",
            "Detected tag from the remote storage which "
            "needs to be searched by name in the local "
            "storage but it has no name set"));
        QNWARNING(
            "synchronization:remote_to_local", errorDescription << ": " << tag);
        Q_EMIT failure(errorDescription);
        return;
    }

    QUuid findElementRequestId = QUuid::createUuid();
    Q_UNUSED(m_findTagByNameRequestIds.insert(findElementRequestId))

    m_linkedNotebookGuidsByFindTagByNameRequestIds[findElementRequestId] =
        linkedNotebookGuid;

    QNTRACE(
        "synchronization:remote_to_local",
        "Emitting the request to find "
            << "tag in the local storage: request id = " << findElementRequestId
            << ", tag: " << tag);

    Q_EMIT findTag(tag, findElementRequestId);
}

template <>
void RemoteToLocalSynchronizationManager::emitFindByNameRequest<SavedSearch>(
    const SavedSearch & search, const QString & /* linked notebook guid */)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::emitFindByNameRequest"
            << "<SavedSearch>: " << search);

    if (!search.hasName()) {
        ErrorString errorDescription(QT_TRANSLATE_NOOP(
            "RemoteToLocalSynchronizationManager",
            "Detected saved search from the remote storage "
            "which needs to be searched by name in the local "
            "storage but it has no name set"));
        QNWARNING(
            "synchronization:remote_to_local",
            errorDescription << ": " << search);
        Q_EMIT failure(errorDescription);
        return;
    }

    QUuid findElementRequestId = QUuid::createUuid();
    Q_UNUSED(m_findSavedSearchByNameRequestIds.insert(findElementRequestId))

    QNTRACE(
        "synchronization:remote_to_local",
        "Emitting the request to find "
            << "saved search in the local storage: request id = "
            << findElementRequestId << ", saved search: " << search);

    Q_EMIT findSavedSearch(search, findElementRequestId);
}

template <>
void RemoteToLocalSynchronizationManager::emitFindByNameRequest<Notebook>(
    const Notebook & notebook, const QString & linkedNotebookGuid)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::emitFindByNameRequest<Notebook>: "
            << notebook << "\nLinked notebook guid = " << linkedNotebookGuid);

    if (!notebook.hasName()) {
        ErrorString errorDescription(QT_TRANSLATE_NOOP(
            "RemoteToLocalSynchronizationManager",
            "Detected notebook from the remote storage "
            "which needs to be searched by name in the local "
            "storage but it has no name set"));
        QNWARNING(
            "synchronization:remote_to_local",
            errorDescription << ": " << notebook);
        Q_EMIT failure(errorDescription);
        return;
    }

    QUuid findElementRequestId = QUuid::createUuid();
    Q_UNUSED(m_findNotebookByNameRequestIds.insert(findElementRequestId))

    m_linkedNotebookGuidsByFindNotebookByNameRequestIds[findElementRequestId] =
        linkedNotebookGuid;

    QNTRACE(
        "synchronization:remote_to_local",
        "Emitting the request to find "
            << "notebook in the local storage by name: request id = "
            << findElementRequestId << ", notebook: " << notebook);

    Q_EMIT findNotebook(notebook, findElementRequestId);
}

template <class ContainerType, class PendingContainerType, class ElementType>
bool RemoteToLocalSynchronizationManager::onFoundDuplicateByName(
    ElementType element, const QUuid & requestId, const QString & typeName,
    ContainerType & container, PendingContainerType & pendingItemsContainer,
    QSet<QUuid> & findElementRequestIds)
{
    auto rit = findElementRequestIds.find(requestId);
    if (rit == findElementRequestIds.end()) {
        return false;
    }

    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::onFoundDuplicateByName<"
            << typeName << ">: " << typeName << " = " << element
            << ", requestId  = " << requestId);

    Q_UNUSED(findElementRequestIds.erase(rit));

    QString targetLinkedNotebookGuid;
    if (typeName == QStringLiteral("Tag")) {
        auto it =
            m_linkedNotebookGuidsByFindTagByNameRequestIds.find(requestId);
        if (it != m_linkedNotebookGuidsByFindTagByNameRequestIds.end()) {
            targetLinkedNotebookGuid = it.value();
            Q_UNUSED(m_linkedNotebookGuidsByFindTagByNameRequestIds.erase(it))
        }
    }
    else if (typeName == QStringLiteral("Notebook")) {
        auto it =
            m_linkedNotebookGuidsByFindNotebookByNameRequestIds.find(requestId);
        if (it != m_linkedNotebookGuidsByFindNotebookByNameRequestIds.end()) {
            targetLinkedNotebookGuid = it.value();
            Q_UNUSED(
                m_linkedNotebookGuidsByFindNotebookByNameRequestIds.erase(it))
        }
    }

    auto it =
        findItemByName(container, element, targetLinkedNotebookGuid, typeName);

    if (it == container.end()) {
        return true;
    }

    // The element exists both in the client and in the server
    const auto & remoteElement = *it;

    if (!remoteElement.updateSequenceNum.isSet()) {
        ErrorString errorDescription(QT_TRANSLATE_NOOP(
            "RemoteToLocalSynchronizationManager",
            "Found a data item without the update sequence "
            "number within the sync chunk"));
        SET_ITEM_TYPE_TO_ERROR();
        QNWARNING(
            "synchronization:remote_to_local",
            errorDescription << ": " << remoteElement);
        Q_EMIT failure(errorDescription);
        return true;
    }

    if (!remoteElement.guid.isSet()) {
        ErrorString errorDescription(QT_TRANSLATE_NOOP(
            "RemoteToLocalSynchronizationManager",
            "Found a data item without guid within the sync "
            "chunk"));
        SET_ITEM_TYPE_TO_ERROR();
        QNWARNING(
            "synchronization:remote_to_local",
            errorDescription << ": " << remoteElement);
        Q_EMIT failure(errorDescription);
        return true;
    }

    auto status = resolveSyncConflict(remoteElement, element);
    if (status == ResolveSyncConflictStatus::Pending) {
        auto pendingItemIt = std::find_if(
            pendingItemsContainer.begin(), pendingItemsContainer.end(),
            CompareItemByGuid<typename PendingContainerType::value_type>(
                remoteElement.guid.ref()));

        if (pendingItemIt == pendingItemsContainer.end()) {
            pendingItemsContainer << remoteElement;
        }
    }

    Q_UNUSED(container.erase(it))

    if (status == ResolveSyncConflictStatus::Ready) {
        checkServerDataMergeCompletion();
    }

    return true;
}

template <class ElementType, class ContainerType, class PendingContainerType>
bool RemoteToLocalSynchronizationManager::onFoundDuplicateByGuid(
    ElementType element, const QUuid & requestId, const QString & typeName,
    ContainerType & container, PendingContainerType & pendingItemsContainer,
    QSet<QUuid> & findByGuidRequestIds)
{
    auto rit = findByGuidRequestIds.find(requestId);
    if (rit == findByGuidRequestIds.end()) {
        return false;
    }

    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::onFoundDuplicateByGuid<"
            << typeName << ">: " << typeName << " = " << element
            << ", requestId = " << requestId);

    Q_UNUSED(findByGuidRequestIds.erase(rit));

    auto it = findItemByGuid(container, element, typeName);
    if (it == container.end()) {
        ErrorString errorDescription(QT_TRANSLATE_NOOP(
            "RemoteToLocalSynchronizationManager",
            "Could not find the remote item by guid when "
            "reported of duplicate by guid in the local storage"));
        SET_ITEM_TYPE_TO_ERROR();
        QNWARNING(
            "synchronization:remote_to_local",
            errorDescription << ": " << element);
        Q_EMIT failure(errorDescription);
        return true;
    }

    const auto & remoteElement = *it;
    if (!remoteElement.updateSequenceNum.isSet()) {
        ErrorString errorDescription(QT_TRANSLATE_NOOP(
            "RemoteToLocalSynchronizationManager",
            "Found a remote data item without the update "
            "sequence number"));
        SET_ITEM_TYPE_TO_ERROR();
        QNWARNING(
            "synchronization:remote_to_local",
            errorDescription << ": " << remoteElement);
        Q_EMIT failure(errorDescription);
        return true;
    }

    ResolveSyncConflictStatus status =
        resolveSyncConflict(remoteElement, element);

    if (status == ResolveSyncConflictStatus::Pending) {
        auto pendingItemIt = std::find_if(
            pendingItemsContainer.begin(), pendingItemsContainer.end(),
            CompareItemByGuid<typename PendingContainerType::value_type>(
                remoteElement.guid.ref()));

        if (pendingItemIt == pendingItemsContainer.end()) {
            pendingItemsContainer << remoteElement;
        }
    }

    Q_UNUSED(container.erase(it))

    if (status == ResolveSyncConflictStatus::Ready) {
        checkServerDataMergeCompletion();
    }

    return true;
}

template <class ContainerType, class ElementType>
bool RemoteToLocalSynchronizationManager::onNoDuplicateByGuid(
    ElementType element, const QUuid & requestId,
    const ErrorString & errorDescription, const QString & typeName,
    ContainerType & container, QSet<QUuid> & findElementRequestIds)
{
    auto rit = findElementRequestIds.find(requestId);
    if (rit == findElementRequestIds.end()) {
        return false;
    }

    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::onNoDuplicateByGuid<"
            << typeName << ">: " << element << ", errorDescription = "
            << errorDescription << ", requestId = " << requestId);

    Q_UNUSED(findElementRequestIds.erase(rit));

    auto it = findItemByGuid(container, element, typeName);
    if (it == container.end()) {
        return true;
    }

    // This element wasn't found in the local storage by guid, need to check
    // whether the element with similar name exists
    ElementType elementToFindByName(*it);
    elementToFindByName.unsetLocalUid();

    QString linkedNotebookGuid =
        checkAndAddLinkedNotebookBinding(elementToFindByName);

    elementToFindByName.setGuid(QString());
    emitFindByNameRequest(elementToFindByName, linkedNotebookGuid);

    return true;
}

template <class ContainerType, class ElementType>
bool RemoteToLocalSynchronizationManager::onNoDuplicateByName(
    ElementType element, const QUuid & requestId,
    const ErrorString & errorDescription, const QString & typeName,
    ContainerType & container, QSet<QUuid> & findElementRequestIds)
{
    auto rit = findElementRequestIds.find(requestId);
    if (rit == findElementRequestIds.end()) {
        return false;
    }

    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::onNoDuplicateByName<"
            << typeName << ">: " << element << ", errorDescription = "
            << errorDescription << ", requestId = " << requestId);

    Q_UNUSED(findElementRequestIds.erase(rit));

    QString targetLinkedNotebookGuid;
    if (typeName == QStringLiteral("Tag")) {
        auto it =
            m_linkedNotebookGuidsByFindTagByNameRequestIds.find(requestId);
        if (it != m_linkedNotebookGuidsByFindTagByNameRequestIds.end()) {
            targetLinkedNotebookGuid = it.value();
            Q_UNUSED(m_linkedNotebookGuidsByFindTagByNameRequestIds.erase(it))
        }
    }
    else if (typeName == QStringLiteral("Notebook")) {
        auto it =
            m_linkedNotebookGuidsByFindNotebookByNameRequestIds.find(requestId);

        if (it != m_linkedNotebookGuidsByFindNotebookByNameRequestIds.end()) {
            targetLinkedNotebookGuid = it.value();
            Q_UNUSED(
                m_linkedNotebookGuidsByFindNotebookByNameRequestIds.erase(it))
        }
    }

    auto it =
        findItemByName(container, element, targetLinkedNotebookGuid, typeName);

    if (it == container.end()) {
        return true;
    }

    if (Q_UNLIKELY(!it->guid.isSet())) {
        ErrorString error(QT_TRANSLATE_NOOP(
            "RemoteToLocalSynchronizationManager",
            "Internal error: found data item without guid "
            "within those from the downloaded sync chunks"));
        QNWARNING("synchronization:remote_to_local", error << ": " << *it);
        Q_EMIT failure(error);
        return true;
    }

    // This element wasn't found in the local storage by guid or name ==>
    // it's new from the remote storage, adding it
    ElementType newElement(*it);
    setNonLocalAndNonDirty(newElement);
    checkAndAddLinkedNotebookBinding(newElement);

    emitAddRequest(newElement);

    // also removing the element from the list of ones waiting for processing
    Q_UNUSED(container.erase(it));

    return true;
}

template <>
RemoteToLocalSynchronizationManager::ResolveSyncConflictStatus
RemoteToLocalSynchronizationManager::resolveSyncConflict(
    const qevercloud::Notebook & remoteNotebook, const Notebook & localConflict)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::resolveSyncConflict"
            << "<Notebook>: remote notebook = " << remoteNotebook
            << "\nLocal conflicting notebook: " << localConflict);

    if (Q_UNLIKELY(!remoteNotebook.guid.isSet())) {
        ErrorString error(QT_TRANSLATE_NOOP(
            "RemoteToLocalSynchronizationManager",
            "Can't resolve the conflict between remote and "
            "local notebooks: the remote notebook has no guid"));
        QNWARNING(
            "synchronization:remote_to_local",
            error << ", remote notebook: " << remoteNotebook);
        Q_EMIT failure(error);
        return ResolveSyncConflictStatus::Ready;
    }

    auto notebookSyncConflictResolvers =
        findChildren<NotebookSyncConflictResolver *>();

    for (const auto * pResolver: ::qAsConst(notebookSyncConflictResolvers)) {
        if (Q_UNLIKELY(!pResolver)) {
            QNWARNING(
                "synchronization:remote_to_local",
                "Skipping the null "
                    << "pointer to notebook sync conflict resolver");
            continue;
        }

        const auto & resolverRemoteNotebook = pResolver->remoteNotebook();
        if (Q_UNLIKELY(!resolverRemoteNotebook.guid.isSet())) {
            QNWARNING(
                "synchronization:remote_to_local",
                "Skipping "
                    << "the resolver with remote notebook containing no guid: "
                    << resolverRemoteNotebook);
            continue;
        }

        if (resolverRemoteNotebook.guid.ref() != remoteNotebook.guid.ref()) {
            QNTRACE(
                "synchronization:remote_to_local",
                "Skipping the existing "
                    << "notebook sync conflict resolver processing remote "
                       "notebook "
                    << "with another guid: " << resolverRemoteNotebook);
            continue;
        }

        const Notebook & resolverLocalConflict = pResolver->localConflict();
        if (resolverLocalConflict.localUid() != localConflict.localUid()) {
            QNTRACE(
                "synchronization:remote_to_local",
                "Skipping the existing "
                    << "notebook sync conflict resolver processing local "
                       "conflict "
                    << "with "
                    << "another local uid: " << resolverLocalConflict);
            continue;
        }

        QNDEBUG(
            "synchronization:remote_to_local",
            "Found existing notebook "
                << "sync conflict resolver for this pair of remote and local "
                << "notebooks");
        return ResolveSyncConflictStatus::Pending;
    }

    NotebookSyncCache * pCache = nullptr;
    if (localConflict.hasLinkedNotebookGuid()) {
        const QString & linkedNotebookGuid = localConflict.linkedNotebookGuid();

        auto it =
            m_notebookSyncCachesByLinkedNotebookGuids.find(linkedNotebookGuid);

        if (it == m_notebookSyncCachesByLinkedNotebookGuids.end()) {
            pCache = new NotebookSyncCache(
                m_manager.localStorageManagerAsync(), linkedNotebookGuid, this);

            it = m_notebookSyncCachesByLinkedNotebookGuids.insert(
                linkedNotebookGuid, pCache);
        }

        pCache = it.value();
    }
    else {
        pCache = &m_notebookSyncCache;
    }

    QString remoteNotebookLinkedNotebookGuid;

    auto it =
        m_linkedNotebookGuidsByNotebookGuids.find(remoteNotebook.guid.ref());

    if (it != m_linkedNotebookGuidsByNotebookGuids.end()) {
        remoteNotebookLinkedNotebookGuid = it.value();
    }

    auto * pResolver = new NotebookSyncConflictResolver(
        remoteNotebook, remoteNotebookLinkedNotebookGuid, localConflict,
        *pCache, m_manager.localStorageManagerAsync(), this);

    QObject::connect(
        pResolver, &NotebookSyncConflictResolver::finished, this,
        &RemoteToLocalSynchronizationManager::
            onNotebookSyncConflictResolverFinished,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        pResolver, &NotebookSyncConflictResolver::failure, this,
        &RemoteToLocalSynchronizationManager::
            onNotebookSyncConflictResolverFailure,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    pResolver->start();

    return ResolveSyncConflictStatus::Pending;
}

template <>
RemoteToLocalSynchronizationManager::ResolveSyncConflictStatus
RemoteToLocalSynchronizationManager::resolveSyncConflict(
    const qevercloud::Tag & remoteTag, const Tag & localConflict)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::resolveSyncConflict<Tag>: "
            << "remote tag = " << remoteTag
            << "\nLocal conflicting tag: " << localConflict);

    if (Q_UNLIKELY(!remoteTag.guid.isSet())) {
        ErrorString error(QT_TRANSLATE_NOOP(
            "RemoteToLocalSynchronizationManager",
            "Can't resolve the conflict between remote and "
            "local tags: the remote tag has no guid"));
        QNWARNING(
            "synchronization:remote_to_local",
            error << ", remote tag: " << remoteTag);
        Q_EMIT failure(error);
        return ResolveSyncConflictStatus::Ready;
    }

    auto tagSyncConflictResolvers = findChildren<TagSyncConflictResolver *>();
    for (const auto * pResolver: ::qAsConst(tagSyncConflictResolvers)) {
        if (Q_UNLIKELY(!pResolver)) {
            QNWARNING(
                "synchronization:remote_to_local",
                "Skipping the null "
                    << "pointer to tag sync conflict resolver");
            continue;
        }

        const qevercloud::Tag & resolverRemoteTag = pResolver->remoteTag();
        if (Q_UNLIKELY(!resolverRemoteTag.guid.isSet())) {
            QNWARNING(
                "synchronization:remote_to_local",
                "Skipping "
                    << "the resolver with remote tag containing no guid: "
                    << resolverRemoteTag);
            continue;
        }

        if (resolverRemoteTag.guid.ref() != remoteTag.guid.ref()) {
            QNTRACE(
                "synchronization:remote_to_local",
                "Skipping the existing "
                    << "tag sync conflict resolver processing remote tag with "
                    << "another guid: " << resolverRemoteTag);
            continue;
        }

        const Tag & resolverLocalConflict = pResolver->localConflict();
        if (resolverLocalConflict.localUid() != localConflict.localUid()) {
            QNTRACE(
                "synchronization:remote_to_local",
                "Skipping the existing "
                    << "tag sync conflict resolver processing local conflict "
                       "with "
                    << "another local uid: " << resolverLocalConflict);
            continue;
        }

        QNDEBUG(
            "synchronization:remote_to_local",
            "Found existing tag sync "
                << "conflict resolver for this pair of remote and local tags");
        return ResolveSyncConflictStatus::Pending;
    }

    TagSyncCache * pCache = nullptr;
    if (localConflict.hasLinkedNotebookGuid()) {
        const QString & linkedNotebookGuid = localConflict.linkedNotebookGuid();
        auto it = m_tagSyncCachesByLinkedNotebookGuids.find(linkedNotebookGuid);
        if (it == m_tagSyncCachesByLinkedNotebookGuids.end()) {
            pCache = new TagSyncCache(
                m_manager.localStorageManagerAsync(), linkedNotebookGuid, this);

            it = m_tagSyncCachesByLinkedNotebookGuids.insert(
                linkedNotebookGuid, pCache);
        }

        pCache = it.value();
    }
    else {
        pCache = &m_tagSyncCache;
    }

    QString remoteTagLinkedNotebookGuid;
    auto it = m_linkedNotebookGuidsByTagGuids.find(remoteTag.guid.ref());
    if (it != m_linkedNotebookGuidsByTagGuids.end()) {
        remoteTagLinkedNotebookGuid = it.value();
    }

    auto * pResolver = new TagSyncConflictResolver(
        remoteTag, remoteTagLinkedNotebookGuid, localConflict, *pCache,
        m_manager.localStorageManagerAsync(), this);

    QObject::connect(
        pResolver, &TagSyncConflictResolver::finished, this,
        &RemoteToLocalSynchronizationManager::onTagSyncConflictResolverFinished,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        pResolver, &TagSyncConflictResolver::failure, this,
        &RemoteToLocalSynchronizationManager::onTagSyncConflictResolverFailure,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    pResolver->start();
    return ResolveSyncConflictStatus::Pending;
}

template <>
RemoteToLocalSynchronizationManager::ResolveSyncConflictStatus
RemoteToLocalSynchronizationManager::resolveSyncConflict(
    const qevercloud::SavedSearch & remoteSavedSearch,
    const SavedSearch & localConflict)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::resolveSyncConflict"
            << "<SavedSearch>: remote saved search = " << remoteSavedSearch
            << "\nLocal conflicting saved search: " << localConflict);

    if (Q_UNLIKELY(!remoteSavedSearch.guid.isSet())) {
        ErrorString error(QT_TRANSLATE_NOOP(
            "RemoteToLocalSynchronizationManager",
            "Can't resolve the conflict between "
            "remote and local saved searches: "
            "the remote saved search has no guid"));
        QNWARNING(
            "synchronization:remote_to_local",
            error << ", remote saved search: " << remoteSavedSearch);
        Q_EMIT failure(error);
        return ResolveSyncConflictStatus::Ready;
    }

    auto savedSearchSyncConflictResolvers =
        findChildren<SavedSearchSyncConflictResolver *>();

    for (const auto * pResolver: ::qAsConst(savedSearchSyncConflictResolvers)) {
        if (Q_UNLIKELY(!pResolver)) {
            QNWARNING(
                "synchronization:remote_to_local",
                "Skipping the null "
                    << "pointer to saved search sync conflict resolver");
            continue;
        }

        const auto & resolverRemoteSavedSearch = pResolver->remoteSavedSearch();
        if (Q_UNLIKELY(!resolverRemoteSavedSearch.guid.isSet())) {
            QNWARNING(
                "synchronization:remote_to_local",
                "Skipping "
                    << "the existing saved search sync conflict resolver "
                    << "processing remote saved search with another guid: "
                    << resolverRemoteSavedSearch);
            continue;
        }

        if (resolverRemoteSavedSearch.guid.ref() !=
            remoteSavedSearch.guid.ref()) {
            QNTRACE(
                "synchronization:remote_to_local",
                "Skipping the existing "
                    << "saved search sync conflict resolver processing remote "
                    << "saved search with another guid: "
                    << resolverRemoteSavedSearch);
            continue;
        }

        const auto & resolverLocalConflict = pResolver->localConflict();
        if (resolverLocalConflict.localUid() != localConflict.localUid()) {
            QNTRACE(
                "synchronization:remote_to_local",
                "Skipping the existing "
                    << "saved search sync conflict resolver processing local "
                    << "conflict with another local uid: "
                    << resolverLocalConflict);
            continue;
        }

        QNDEBUG(
            "synchronization:remote_to_local",
            "Found existing saved "
                << "search conflict resolver for this pair of remote and local "
                << "saved searches");
        return ResolveSyncConflictStatus::Pending;
    }

    auto * pResolver = new SavedSearchSyncConflictResolver(
        remoteSavedSearch, localConflict, m_savedSearchSyncCache,
        m_manager.localStorageManagerAsync(), this);

    QObject::connect(
        pResolver, &SavedSearchSyncConflictResolver::finished, this,
        &RemoteToLocalSynchronizationManager::
            onSavedSearchSyncConflictResolverFinished,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        pResolver, &SavedSearchSyncConflictResolver::failure, this,
        &RemoteToLocalSynchronizationManager::
            onSavedSearchSyncConflictResolverFailure,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    pResolver->start();
    return ResolveSyncConflictStatus::Pending;
}

template <>
RemoteToLocalSynchronizationManager::ResolveSyncConflictStatus
RemoteToLocalSynchronizationManager::resolveSyncConflict(
    const qevercloud::Note & remoteNote, const Note & localConflict)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::resolveSyncConflict<Note>: "
            << "remote note = " << remoteNote
            << "\nLocal conflicting note: " << localConflict);

    if (Q_UNLIKELY(!remoteNote.guid.isSet())) {
        ErrorString errorDescription(QT_TRANSLATE_NOOP(
            "RemoteToLocalSynchronizationManager",
            "Found a remote note without guid set"));
        QNWARNING(
            "synchronization:remote_to_local",
            errorDescription << ", note: " << remoteNote);
        Q_EMIT failure(errorDescription);
        return ResolveSyncConflictStatus::Ready;
    }

    if (Q_UNLIKELY(!remoteNote.updateSequenceNum.isSet())) {
        ErrorString errorDescription(QT_TRANSLATE_NOOP(
            "RemoteToLocalSynchronizationManager",
            "Found a remote note without update sequence "
            "number set"));
        QNWARNING(
            "synchronization:remote_to_local",
            errorDescription << ", note: " << remoteNote);
        Q_EMIT failure(errorDescription);
        return ResolveSyncConflictStatus::Ready;
    }

    if (localConflict.hasGuid() &&
        (localConflict.guid() == remoteNote.guid.ref()) &&
        localConflict.hasUpdateSequenceNumber() &&
        (localConflict.updateSequenceNumber() >=
         remoteNote.updateSequenceNum.ref()))
    {
        QNDEBUG(
            "synchronization:remote_to_local",
            "The local conflicting "
                << "note's update sequence number is greater than or equal to "
                << "the remote note's one => the remote note shouldn't "
                   "override "
                << "the local note");
        return ResolveSyncConflictStatus::Ready;
    }

    launchNoteSyncConflictResolver(localConflict, remoteNote);
    return ResolveSyncConflictStatus::Pending;
}

template <>
RemoteToLocalSynchronizationManager::ResolveSyncConflictStatus
RemoteToLocalSynchronizationManager::resolveSyncConflict(
    const qevercloud::LinkedNotebook & remoteLinkedNotebook,
    const LinkedNotebook & localConflict)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::resolveSyncConflict"
            << "<LinkedNotebook>: remote linked notebook = "
            << remoteLinkedNotebook
            << "\nLocal conflicting linked notebook: " << localConflict);

    // NOTE: since linked notebook is just a pointer to a notebook in another
    // user's account, it makes little sense to even attempt to resolve any
    // potential conflict in favor of local changes - the remote changes
    // should always win

    LinkedNotebook linkedNotebook(localConflict);
    linkedNotebook.qevercloudLinkedNotebook() = remoteLinkedNotebook;
    linkedNotebook.setDirty(false);

    registerLinkedNotebookPendingAddOrUpdate(linkedNotebook);

    QUuid updateLinkedNotebookRequestId = QUuid::createUuid();

    Q_UNUSED(
        m_updateLinkedNotebookRequestIds.insert(updateLinkedNotebookRequestId))

    QNTRACE(
        "synchronization:remote_to_local",
        "Emitting the request to update "
            << "linked notebook: request id = " << updateLinkedNotebookRequestId
            << ", linked notebook: " << linkedNotebook);

    Q_EMIT updateLinkedNotebook(linkedNotebook, updateLinkedNotebookRequestId);

    return ResolveSyncConflictStatus::Pending;
}

bool RemoteToLocalSynchronizationManager::sortTagsByParentChildRelations(
    TagsList & tagList)
{
    QNDEBUG(
        "synchronization:remote_to_local",
        "RemoteToLocalSynchronizationManager::sortTagsByParentChildRelations");

    ErrorString errorDescription;

    bool res =
        ::quentier::sortTagsByParentChildRelations(tagList, errorDescription);

    if (!res) {
        QNWARNING("synchronization:remote_to_local", errorDescription);
        Q_EMIT failure(errorDescription);
        return false;
    }

    return true;
}

QTextStream &
RemoteToLocalSynchronizationManager::PostponedConflictingResourceData::print(
    QTextStream & strm) const
{
    strm << "PostponedConflictingResourceData: {\n  Remote note:\n"
         << m_remoteNote << "\n\n  Local conflicting note:\n"
         << m_localConflictingNote
         << "\n\n  Remote note's resource without full data:\n"
         << m_remoteNoteResourceWithoutFullData << "\n};\n";
    return strm;
}

NoteSyncConflictResolverManager::NoteSyncConflictResolverManager(
    RemoteToLocalSynchronizationManager & manager) :
    m_manager(manager)
{}

LocalStorageManagerAsync &
NoteSyncConflictResolverManager::localStorageManagerAsync()
{
    return m_manager.m_manager.localStorageManagerAsync();
}

INoteStore * NoteSyncConflictResolverManager::noteStoreForNote(
    const Note & note, QString & authToken, ErrorString & errorDescription)
{
    authToken.resize(0);
    errorDescription.clear();
    auto * pNoteStore =
        m_manager.noteStoreForNote(note, authToken, errorDescription);
    if (Q_UNLIKELY(!pNoteStore)) {
        return nullptr;
    }

    if (authToken.isEmpty()) {
        authToken = m_manager.m_authenticationToken;
    }

    return pNoteStore;
}

bool NoteSyncConflictResolverManager::syncingLinkedNotebooksContent() const
{
    return m_manager.syncingLinkedNotebooksContent();
}

} // namespace quentier
