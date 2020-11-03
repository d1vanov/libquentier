/*
 * Copyright 2017-2020 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_SYNCHRONIZATION_REMOTE_TO_LOCAL_SYNCHRONIZATION_MANAGER_H
#define LIB_QUENTIER_SYNCHRONIZATION_REMOTE_TO_LOCAL_SYNCHRONIZATION_MANAGER_H

#include "FullSyncStaleDataItemsExpunger.h"
#include "NotebookSyncCache.h"
#include "NotebookSyncConflictResolver.h"
#include "SavedSearchSyncCache.h"
#include "SavedSearchSyncConflictResolver.h"
#include "SynchronizationShared.h"
#include "TagSyncCache.h"
#include "TagSyncConflictResolver.h"

#include <quentier/local_storage/LocalStorageManager.h>
#include <quentier/synchronization/INoteStore.h>
#include <quentier/synchronization/IUserStore.h>
#include <quentier/types/Account.h>
#include <quentier/types/ErrorString.h>
#include <quentier/types/LinkedNotebook.h>
#include <quentier/types/Note.h>
#include <quentier/types/Notebook.h>
#include <quentier/types/Resource.h>
#include <quentier/types/SavedSearch.h>
#include <quentier/types/Tag.h>
#include <quentier/types/User.h>

#include <qt5qevercloud/QEverCloud.h>

#include <QMap>
#include <QMultiHash>

#include <utility>

namespace quentier {

QT_FORWARD_DECLARE_CLASS(LocalStorageManagerAsync)
QT_FORWARD_DECLARE_CLASS(NoteSyncConflictResolverManager)

class Q_DECL_HIDDEN RemoteToLocalSynchronizationManager final : public QObject
{
    Q_OBJECT
public:
    class Q_DECL_HIDDEN IManager
    {
    public:
        virtual LocalStorageManagerAsync & localStorageManagerAsync() = 0;
        virtual INoteStore & noteStore() = 0;
        virtual IUserStore & userStore() = 0;

        virtual INoteStore * noteStoreForLinkedNotebook(
            const LinkedNotebook & linkedNotebook) = 0;

        virtual ~IManager() = default;
    };

    explicit RemoteToLocalSynchronizationManager(
        IManager & manager, const QString & host, QObject * parent = nullptr);

    virtual ~RemoteToLocalSynchronizationManager() override;

    bool active() const;

    void setAccount(const Account & account);
    Account account() const;

    bool syncUser(
        const qevercloud::UserID userId, ErrorString & errorDescription,
        const bool writeUserDataToLocalStorage = true);

    const User & user() const;

    bool downloadedSyncChunks() const
    {
        return m_syncChunksDownloaded;
    }

    bool downloadedLinkedNotebooksSyncChunks() const
    {
        return m_linkedNotebooksSyncChunksDownloaded;
    }

    bool shouldDownloadThumbnailsForNotes() const;
    bool shouldDownloadInkNoteImages() const;
    QString inkNoteImagesStoragePath() const;

Q_SIGNALS:
    void failure(ErrorString errorDescription);

    void finished(
        qint32 lastUpdateCount, qevercloud::Timestamp lastSyncTime,
        QHash<QString, qint32> lastUpdateCountByLinkedNotebookGuid,
        QHash<QString, qevercloud::Timestamp> lastSyncTimeByLinkedNotebookGuid);

    /**
     * Signal notifying that the Evernote API rate limit was exceeded so that
     * the synchronization needs to wait for the specified number of seconds
     * before it proceeds (that would happen automatically, there's no need to
     * restart the synchronization manually)
     */
    void rateLimitExceeded(qint32 secondsToWait);

    // signals notifying about the progress of synchronization
    void syncChunksDownloadProgress(
        qint32 highestDownloadedUsn, qint32 highestServerUsn,
        qint32 lastPreviousUsn);

    void syncChunksDownloaded();

    void notesDownloadProgress(
        quint32 notesDownloaded, quint32 totalNotesToDownload);

    void resourcesDownloadProgress(
        quint32 resourcesDownloaded, quint32 totalResourcesToDownload);

    void synchronizedContentFromUsersOwnAccount(
        qint32 lastUpdateCount, qevercloud::Timestamp lastSyncTime);

    void linkedNotebookSyncChunksDownloadProgress(
        qint32 highestDownloadedUsn, qint32 highestServerUsn,
        qint32 lastPreviousUsn, LinkedNotebook linkedNotebook);

    void linkedNotebooksSyncChunksDownloaded();

    void linkedNotebooksNotesDownloadProgress(
        quint32 notesDownloaded, quint32 totalNotesToDownload);

    void linkedNotebooksResourcesDownloadProgress(
        quint32 resourcesDownloaded, quint32 totalResourcesToDownload);

    void expungedFromServerToClient();

    void stopped();

    void requestAuthenticationToken();

    void requestAuthenticationTokensForLinkedNotebooks(
        QVector<LinkedNotebookAuthData> linkedNotebookAuthData);

    void requestLastSyncParameters();

public Q_SLOTS:
    void start(qint32 afterUsn = 0);

    void stop();

    void onAuthenticationInfoReceived(
        QString authToken, QString shardId,
        qevercloud::Timestamp expirationTime);

    void onAuthenticationTokensForLinkedNotebooksReceived(
        QHash<QString, std::pair<QString, QString>>
            authTokensAndShardIdsByLinkedNotebookGuid,
        QHash<QString, qevercloud::Timestamp>
            authTokenExpirationTimesByLinkedNotebookGuid);

    void onLastSyncParametersReceived(
        qint32 lastUpdateCount, qevercloud::Timestamp lastSyncTime,
        QHash<QString, qint32> lastUpdateCountByLinkedNotebookGuid,
        QHash<QString, qevercloud::Timestamp> lastSyncTimeByLinkedNotebookGuid);

    void setDownloadNoteThumbnails(const bool flag);
    void setDownloadInkNoteImages(const bool flag);
    void setInkNoteImagesStoragePath(const QString & path);

    void collectNonProcessedItemsSmallestUsns(
        qint32 & usn, QHash<QString, qint32> & usnByLinkedNotebookGuid);

    // private signals
Q_SIGNALS:
    void addUser(User user, QUuid requestId);
    void updateUser(User user, QUuid requestId);
    void findUser(User user, QUuid requestId);

    void addNotebook(Notebook notebook, QUuid requestId);
    void updateNotebook(Notebook notebook, QUuid requestId);
    void findNotebook(Notebook notebook, QUuid requestId);
    void expungeNotebook(Notebook notebook, QUuid requestId);

    void addNote(Note note, QUuid requestId);

    void updateNote(
        Note note, LocalStorageManager::UpdateNoteOptions options,
        QUuid requestId);

    void findNote(
        Note note, LocalStorageManager::GetNoteOptions options,
        QUuid requestId);

    void expungeNote(Note note, QUuid requestId);

    void addTag(Tag tag, QUuid requestId);
    void updateTag(Tag tag, QUuid requestId);
    void findTag(Tag tag, QUuid requestId);
    void expungeTag(Tag tag, QUuid requestId);

    void expungeNotelessTagsFromLinkedNotebooks(QUuid requestId);

    void addResource(Resource resource, QUuid requestId);
    void updateResource(Resource resource, QUuid requestId);

    void findResource(
        Resource resource, LocalStorageManager::GetResourceOptions options,
        QUuid requestId);

    void addLinkedNotebook(LinkedNotebook notebook, QUuid requestId);
    void updateLinkedNotebook(LinkedNotebook notebook, QUuid requestId);
    void findLinkedNotebook(LinkedNotebook linkedNotebook, QUuid requestId);
    void expungeLinkedNotebook(LinkedNotebook notebook, QUuid requestId);

    void listAllLinkedNotebooks(
        size_t limit, size_t offset,
        LocalStorageManager::ListLinkedNotebooksOrder,
        LocalStorageManager::OrderDirection orderDirection, QUuid requestId);

    void addSavedSearch(SavedSearch savedSearch, QUuid requestId);
    void updateSavedSearch(SavedSearch savedSearch, QUuid requestId);
    void findSavedSearch(SavedSearch savedSearch, QUuid requestId);
    void expungeSavedSearch(SavedSearch savedSearch, QUuid requestId);

    void authDataUpdated(
        QString authToken, QString shardId,
        qevercloud::Timestamp expirationTime);

    void linkedNotebookAuthDataUpdated(
        QHash<QString, std::pair<QString, QString>>
            authTokensAndShardIdsByLinkedNotebookGuid,
        QHash<QString, qevercloud::Timestamp>
            authTokenExpirationTimesByLinkedNotebookGuid);

private Q_SLOTS:
    void onFindUserCompleted(User user, QUuid requestId);

    void onFindUserFailed(
        User user, ErrorString errorDescription, QUuid requestId);

    void onFindNotebookCompleted(Notebook notebook, QUuid requestId);

    void onFindNotebookFailed(
        Notebook notebook, ErrorString errorDescription, QUuid requestId);

    void onFindNoteCompleted(
        Note note, LocalStorageManager::GetNoteOptions options,
        QUuid requestId);

    void onFindNoteFailed(
        Note note, LocalStorageManager::GetNoteOptions options,
        ErrorString errorDescription, QUuid requestId);

    void onFindTagCompleted(Tag tag, QUuid requestId);

    void onFindTagFailed(
        Tag tag, ErrorString errorDescription, QUuid requestId);

    void onFindResourceCompleted(
        Resource resource, LocalStorageManager::GetResourceOptions options,
        QUuid requestId);

    void onFindResourceFailed(
        Resource resource, LocalStorageManager::GetResourceOptions options,
        ErrorString errorDescription, QUuid requestId);

    void onFindLinkedNotebookCompleted(
        LinkedNotebook linkedNotebook, QUuid requestId);

    void onFindLinkedNotebookFailed(
        LinkedNotebook linkedNotebook, ErrorString errorDescription,
        QUuid requestId);

    void onFindSavedSearchCompleted(SavedSearch savedSearch, QUuid requestId);

    void onFindSavedSearchFailed(
        SavedSearch savedSearch, ErrorString errorDescription, QUuid requestId);

    void onAddUserCompleted(User user, QUuid requestId);

    void onAddUserFailed(
        User user, ErrorString errorDescription, QUuid requestId);

    void onAddTagCompleted(Tag tag, QUuid requestId);
    void onAddTagFailed(Tag tag, ErrorString errorDescription, QUuid requestId);
    void onUpdateUserCompleted(User user, QUuid requestId);

    void onUpdateUserFailed(
        User user, ErrorString errorDescription, QUuid requestId);

    void onUpdateTagCompleted(Tag tag, QUuid requestId);

    void onUpdateTagFailed(
        Tag tag, ErrorString errorDescription, QUuid requestId);

    void onExpungeTagCompleted(
        Tag tag, QStringList expungedChildTagLocalUids, QUuid requestId);

    void onExpungeTagFailed(
        Tag tag, ErrorString errorDescription, QUuid requestId);

    void onExpungeNotelessTagsFromLinkedNotebooksCompleted(QUuid requestId);

    void onExpungeNotelessTagsFromLinkedNotebooksFailed(
        ErrorString errorDescription, QUuid requestId);

    void onAddSavedSearchCompleted(SavedSearch search, QUuid requestId);

    void onAddSavedSearchFailed(
        SavedSearch search, ErrorString errorDescription, QUuid requestId);

    void onUpdateSavedSearchCompleted(SavedSearch search, QUuid requestId);

    void onUpdateSavedSearchFailed(
        SavedSearch search, ErrorString errorDescription, QUuid requestId);

    void onExpungeSavedSearchCompleted(SavedSearch search, QUuid requestId);

    void onExpungeSavedSearchFailed(
        SavedSearch search, ErrorString errorDescription, QUuid requestId);

    void onAddLinkedNotebookCompleted(
        LinkedNotebook linkedNotebook, QUuid requestId);

    void onAddLinkedNotebookFailed(
        LinkedNotebook linkedNotebook, ErrorString errorDescription,
        QUuid requestId);

    void onUpdateLinkedNotebookCompleted(
        LinkedNotebook linkedNotebook, QUuid requestId);

    void onUpdateLinkedNotebookFailed(
        LinkedNotebook linkedNotebook, ErrorString errorDescription,
        QUuid requestId);

    void onExpungeLinkedNotebookCompleted(
        LinkedNotebook linkedNotebook, QUuid requestId);

    void onExpungeLinkedNotebookFailed(
        LinkedNotebook linkedNotebook, ErrorString errorDescription,
        QUuid requestId);

    void onListAllLinkedNotebooksCompleted(
        size_t limit, size_t offset,
        LocalStorageManager::ListLinkedNotebooksOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QList<LinkedNotebook> linkedNotebooks, QUuid requestId);

    void onListAllLinkedNotebooksFailed(
        size_t limit, size_t offset,
        LocalStorageManager::ListLinkedNotebooksOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        ErrorString errorDescription, QUuid requestId);

    void onAddNotebookCompleted(Notebook notebook, QUuid requestId);

    void onAddNotebookFailed(
        Notebook notebook, ErrorString errorDescription, QUuid requestId);

    void onUpdateNotebookCompleted(Notebook notebook, QUuid requestId);

    void onUpdateNotebookFailed(
        Notebook notebook, ErrorString errorDescription, QUuid requestId);

    void onExpungeNotebookCompleted(Notebook notebook, QUuid requestId);

    void onExpungeNotebookFailed(
        Notebook notebook, ErrorString errorDescription, QUuid requestId);

    void onAddNoteCompleted(Note note, QUuid requestId);

    void onAddNoteFailed(
        Note note, ErrorString errorDescription, QUuid requestId);

    void onUpdateNoteCompleted(
        Note note, LocalStorageManager::UpdateNoteOptions options,
        QUuid requestId);

    void onUpdateNoteFailed(
        Note note, LocalStorageManager::UpdateNoteOptions options,
        ErrorString errorDescription, QUuid requestId);

    void onExpungeNoteCompleted(Note note, QUuid requestId);

    void onExpungeNoteFailed(
        Note note, ErrorString errorDescription, QUuid requestId);

    void onAddResourceCompleted(Resource resource, QUuid requestId);

    void onAddResourceFailed(
        Resource resource, ErrorString errorDescription, QUuid requestId);

    void onUpdateResourceCompleted(Resource resource, QUuid requestId);

    void onUpdateResourceFailed(
        Resource resource, ErrorString errorDescription, QUuid requestId);

    void onInkNoteImageDownloadFinished(
        bool status, QString resourceGuid, QString noteGuid,
        ErrorString errorDescription);

    void onNoteThumbnailDownloadingFinished(
        bool status, QString noteGuid, QByteArray downloadedThumbnailImageData,
        ErrorString errorDescription);

    void onGetNoteAsyncFinished(
        qint32 errorCode, qevercloud::Note qecNote, qint32 rateLimitSeconds,
        ErrorString errorDescription);

    void onGetResourceAsyncFinished(
        qint32 errorCode, qevercloud::Resource qecResource,
        qint32 rateLimitSeconds, ErrorString errorDescription);

    // Slots for TagSyncCache
    void onTagSyncCacheFilled();
    void onTagSyncCacheFailure(ErrorString errorDescription);

    // Slots for sync conflict resolvers
    void onNotebookSyncConflictResolverFinished(
        qevercloud::Notebook remoteNotebook);

    void onNotebookSyncConflictResolverFailure(
        qevercloud::Notebook remoteNotebook, ErrorString errorDescription);

    void onTagSyncConflictResolverFinished(qevercloud::Tag remoteTag);

    void onTagSyncConflictResolverFailure(
        qevercloud::Tag remoteTag, ErrorString errorDescription);

    void onSavedSearchSyncConflictResolverFinished(
        qevercloud::SavedSearch remoteSavedSearch);

    void onSavedSearchSyncConflictResolverFailure(
        qevercloud::SavedSearch remoteSavedSearch,
        ErrorString errorDescription);

    void onNoteSyncConflictResolverFinished(qevercloud::Note remoteNote);

    void onNoteSyncConflictResolvedFailure(
        qevercloud::Note remoteNote, ErrorString errorDescription);

    void onNoteSyncConflictRateLimitExceeded(qint32 secondsToWait);
    void onNoteSyncConflictAuthenticationExpired();

    // Slots for FullSyncStaleDataItemsExpunger signals
    void onFullSyncStaleDataItemsExpungerFinished();
    void onFullSyncStaleDataItemsExpungerFailure(ErrorString errorDescription);

private:
    void connectToLocalStorage();
    void disconnectFromLocalStorage();

    void resetCurrentSyncState();

    QString defaultInkNoteImageStoragePath() const;

    void launchSync();

    // If any of these return false, it is either due to error or due to
    // API rate limit exceeding
    bool checkProtocolVersion(ErrorString & errorDescription);

    bool syncUserImpl(
        const bool waitIfRateLimitReached, ErrorString & errorDescription,
        const bool writeUserDataToLocalStorage = true);

    void launchWritingUserDataToLocalStorage();

    bool checkAndSyncAccountLimits(
        const bool waitIfRateLimitReached, ErrorString & errorDescription);

    bool syncAccountLimits(
        const bool waitIfRateLimitReached, ErrorString & errorDescription);

    void readSavedAccountLimits();
    void writeAccountLimitsToAppSettings();

    void launchTagsSync();
    void launchSavedSearchSync();
    void launchLinkedNotebookSync();
    void launchNotebookSync();

    void collectSyncedGuidsForFullSyncStaleDataItemsExpunger();
    void launchFullSyncStaleDataItemsExpunger();

    // Returns true if full sync stale data items expunger was launched
    // for at least one linked notebook i.e. if the last sync performed
    // for at least one linked notebook was full and it was not the first sync
    // of this linked notebook's contents
    bool launchFullSyncStaleDataItemsExpungersForLinkedNotebooks();

    void launchExpungingOfNotelessTagsFromLinkedNotebooks();

    bool syncingLinkedNotebooksContent() const;

    void checkAndIncrementNoteDownloadProgress(const QString & noteGuid);

    void checkAndIncrementResourceDownloadProgress(
        const QString & resourceGuid);

    bool notebooksSyncInProgress() const;
    bool tagsSyncInProgress() const;
    bool notesSyncInProgress() const;
    bool resourcesSyncInProgress() const;

    enum class ContentSource
    {
        UserAccount,
        LinkedNotebook
    };

    friend QTextStream & operator<<(
        QTextStream & strm, const ContentSource & obj);

    friend QDebug & operator<<(QDebug & dbg, const ContentSource & obj);

    template <class ContainerType, class LocalType>
    void launchDataElementSync(
        const ContentSource contentSource, const QString & typeName,
        ContainerType & container, QList<QString> & expungedElements);

    template <class ContainerType, class LocalType>
    void launchDataElementSyncCommon(
        const ContentSource contentSource, ContainerType & container,
        QList<QString> & expungedElements);

    template <class ElementType>
    void extractExpungedElementsFromSyncChunk(
        const qevercloud::SyncChunk & syncChunk,
        QList<QString> & expungedElementGuids);

    // Returns binded linked notebook guid or empty string if no linked notebook
    // guid was bound
    template <class ElementType>
    QString checkAndAddLinkedNotebookBinding(ElementType & targetElement);

    enum class ResolveSyncConflictStatus
    {
        Ready = 0,
        Pending
    };

    template <class RemoteElementType, class ElementType>
    ResolveSyncConflictStatus resolveSyncConflict(
        const RemoteElementType & remoteElement,
        const ElementType & localConflict);

    template <class ContainerType>
    bool mapContainerElementsWithLinkedNotebookGuid(
        const QString & linkedNotebookGuid, const ContainerType & container);

    template <class ElementType>
    void unmapContainerElementsFromLinkedNotebookGuid(
        const QList<QString> & guids);

    template <class ContainerType>
    void appendDataElementsFromSyncChunkToContainer(
        const qevercloud::SyncChunk & syncChunk, ContainerType & container);

    // ======== Find by guid helpers ==========

    template <class ElementType>
    void emitFindByGuidRequest(const ElementType & element);

    template <
        class ElementType, class ContainerType, class PendingContainerType>
    bool onFoundDuplicateByGuid(
        ElementType element, const QUuid & requestId, const QString & typeName,
        ContainerType & container, PendingContainerType & pendingItemsContainer,
        QSet<QUuid> & findByGuidRequestIds);

    template <class ContainerType, class ElementType>
    bool onNoDuplicateByGuid(
        ElementType element, const QUuid & requestId,
        const ErrorString & errorDescription, const QString & typeName,
        ContainerType & container, QSet<QUuid> & findElementRequestIds);

    // ======== Find by name helpers ==========

    template <class ElementType>
    void emitFindByNameRequest(
        const ElementType & elementToFind, const QString & linkedNotebookGuid);

    template <
        class ContainerType, class PendingContainerType, class ElementType>
    bool onFoundDuplicateByName(
        ElementType element, const QUuid & requestId, const QString & typeName,
        ContainerType & container, PendingContainerType & pendingItemsContainer,
        QSet<QUuid> & findElementRequestIds);

    template <class ContainerType, class ElementType>
    bool onNoDuplicateByName(
        ElementType element, const QUuid & requestId,
        const ErrorString & errorDescription, const QString & typeName,
        ContainerType & container, QSet<QUuid> & findElementRequestIds);

    // ======== Add helpers ===========

    template <class ElementType>
    void emitAddRequest(const ElementType & elementToAdd);

    template <class ElementType>
    void onAddDataElementCompleted(
        const ElementType & element, const QUuid & requestId,
        const QString & typeName, QSet<QUuid> & addElementRequestIds);

    template <class ElementType>
    void onAddDataElementFailed(
        const ElementType & element, const QUuid & requestId,
        const ErrorString & errorDescription, const QString & typeName,
        QSet<QUuid> & addElementRequestIds);

    // ========= Update helpers ==========

    template <class ElementType>
    void onUpdateDataElementCompleted(
        const ElementType & element, const QUuid & requestId,
        const QString & typeName, QSet<QUuid> & updateElementRequestIds);

    template <class ElementType>
    void onUpdateDataElementFailed(
        const ElementType & element, const QUuid & requestId,
        const ErrorString & errorDescription, const QString & typeName,
        QSet<QUuid> & updateElementRequestIds);

    template <class ElementType>
    void performPostAddOrUpdateChecks(const ElementType & element);

    template <class ElementType>
    void unsetLocalUid(ElementType & element);

    template <class ElementType>
    void setNonLocalAndNonDirty(ElementType & element);

    // ======== Expunge helpers ===========

    template <class ElementType>
    void onExpungeDataElementCompleted(
        const ElementType & element, const QUuid & requestId,
        const QString & typeName, QSet<QUuid> & expungeElementRequestIds);

    template <class ElementType>
    void onExpungeDataElementFailed(
        const ElementType & element, const QUuid & requestId,
        const ErrorString & errorDescription, const QString & typeName,
        QSet<QUuid> & expungeElementRequestIds);

    void expungeTags();
    void expungeSavedSearches();
    void expungeLinkedNotebooks();
    void expungeNotebooks();
    void expungeNotes();

    template <class ElementType>
    void performPostExpungeChecks();

    void expungeFromServerToClient();
    void checkExpungesCompletion();

    // ======== Find in blocks from sync chunk helpers ==========

    template <class ContainerType, class ElementType>
    typename ContainerType::iterator findItemByName(
        ContainerType & container, const ElementType & element,
        const QString & targetLinkedNotebookGuid, const QString & typeName);

    template <class ContainerType, class ElementType>
    typename ContainerType::iterator findItemByGuid(
        ContainerType & container, const ElementType & element,
        const QString & typeName);

    // ======== Helpers launching the sync of dependent data elements =========
    void checkNotebooksAndTagsSyncCompletionAndLaunchNotesAndResourcesSync();
    void launchNotesSync(const ContentSource & contentSource);

    void checkNotesSyncCompletionAndLaunchResourcesSync();
    void launchResourcesSync(const ContentSource & contentSource);

    /**
     * Helpers launching the sync of content from someone else's shared
     * notebooks, to be used when LinkedNotebook representing pointers to
     * content from someone else's account are in sync
     */
    void checkLinkedNotebooksSyncAndLaunchLinkedNotebookContentSync();

    void launchLinkedNotebooksContentsSync();
    void startLinkedNotebooksSync();

    bool checkAndRequestAuthenticationTokensForLinkedNotebooks();
    void requestAuthenticationTokensForAllLinkedNotebooks();

    void requestAllLinkedNotebooks();

    void getLinkedNotebookSyncState(
        const LinkedNotebook & linkedNotebook, const QString & authToken,
        qevercloud::SyncState & syncState, bool & asyncWait, bool & error);

    bool downloadLinkedNotebooksSyncChunks();

    void launchLinkedNotebooksTagsSync();
    void launchLinkedNotebooksNotebooksSync();

    void checkServerDataMergeCompletion();

    void finalize();
    void clear();
    void clearAll();

    void handleLinkedNotebookAdded(const LinkedNotebook & linkedNotebook);
    void handleLinkedNotebookUpdated(const LinkedNotebook & linkedNotebook);

    virtual void timerEvent(QTimerEvent * pEvent) override;

    void getFullNoteDataAsync(const Note & note);
    void getFullNoteDataAsyncAndAddToLocalStorage(const Note & note);
    void getFullNoteDataAsyncAndUpdateInLocalStorage(const Note & note);

    void getFullResourceDataAsync(
        const Resource & resource, const Note & resourceOwningNote);

    void getFullResourceDataAsyncAndAddToLocalStorage(
        const Resource & resource, const Note & resourceOwningNote);

    void getFullResourceDataAsyncAndUpdateInLocalStorage(
        const Resource & resource, const Note & resourceOwningNote);

    void downloadSyncChunksAndLaunchSync(qint32 afterUsn);

    const Notebook * getNotebookPerNote(const Note & note) const;

    void handleAuthExpiration();

    bool checkUserAccountSyncState(
        bool & asyncWait, bool & error, qint32 & afterUsn);

    bool checkLinkedNotebooksSyncStates(bool & asyncWait, bool & error);

    void authenticationInfoForNotebook(
        const Notebook & notebook, QString & authToken, QString & shardId,
        bool & isPublic) const;

    bool findNotebookForInkNoteImageDownloading(const Note & note);

    void setupInkNoteImageDownloading(
        const QString & resourceGuid, const int resourceHeight,
        const int resourceWidth, const QString & noteGuid,
        const Notebook & notebook);

    bool setupInkNoteImageDownloadingForNote(
        const Note & note, const Notebook & notebook);

    bool findNotebookForNoteThumbnailDownloading(const Note & note);

    bool setupNoteThumbnailDownloading(
        const Note & note, const Notebook & notebook);

    void launchNoteSyncConflictResolver(
        const Note & localConflict, const qevercloud::Note & remoteNote);

    QString clientNameForProtocolVersionCheck() const;

    // Infrastructure for persisting the sync state corresponding to data synced
    // so far when API rate limit breach occurs
    qint32 findSmallestUsnOfNonSyncedItems(
        const QString & linkedNotebookGuid = {}) const;

    QHash<QString, QString> linkedNotebookGuidByNoteGuidHash() const;

    template <class T>
    QString findLinkedNotebookGuidForItem(const T & item) const;

    template <class T>
    void checkNonSyncedItemForSmallestUsn(
        const T & item, const QString & linkedNotebookGuid,
        qint32 & smallestUsn) const;

    template <class ContainerType>
    void checkNonSyncedItemsContainerForSmallestUsn(
        const ContainerType & container, const QString & linkedNotebookGuid,
        qint32 & smallestUsn) const;

    // Infrastructure for handling of asynchronous data items add/update
    // requests
    void registerTagPendingAddOrUpdate(const Tag & tag);
    void registerSavedSearchPendingAddOrUpdate(const SavedSearch & search);

    void registerLinkedNotebookPendingAddOrUpdate(
        const LinkedNotebook & linkedNotebook);

    void registerNotebookPendingAddOrUpdate(const Notebook & notebook);
    void registerNotePendingAddOrUpdate(const Note & note);
    void registerResourcePendingAddOrUpdate(const Resource & resource);

    void unregisterTagPendingAddOrUpdate(const Tag & tag);
    void unregisterSavedSearchPendingAddOrUpdate(const SavedSearch & search);

    void unregisterLinkedNotebookPendingAddOrUpdate(
        const LinkedNotebook & linkedNotebook);

    void unregisterNotebookPendingAddOrUpdate(const Notebook & notebook);
    void unregisterNotePendingAddOrUpdate(const Note & note);
    void unregisterNotePendingAddOrUpdate(const qevercloud::Note & note);
    void unregisterResourcePendingAddOrUpdate(const Resource & resource);

    // Infrastructure for processing of conflicts occurred during sync
    Note createConflictingNote(
        const Note & originalNote,
        const qevercloud::Note * pRemoteNote = nullptr) const;

    void overrideLocalNoteWithRemoteNote(
        Note & localNote, const qevercloud::Note & remoteNote) const;

    void processResourceConflictAsNoteConflict(
        Note & remoteNote, const Note & localConflictingNote,
        Resource & remoteNoteResource);

    void junkFullSyncStaleDataItemsExpunger(
        FullSyncStaleDataItemsExpunger & expunger);

    INoteStore * noteStoreForNote(
        const Note & note, QString & authToken, ErrorString & errorDescription);

    void connectToUserOwnNoteStore(INoteStore * pNoteStore);

    void checkAndRemoveInaccessibleParentTagGuidsForTagsFromLinkedNotebook(
        const QString & linkedNotebookGuid, const TagSyncCache & tagSyncCache);

    void startFeedingDownloadedTagsToLocalStorageOneByOne(
        const TagsContainer & container);

    void syncNextTagPendingProcessing();

    void removeNoteResourcesFromSyncChunks(const Note & note);

    void removeResourceFromSyncChunks(
        const Resource & resource, QVector<qevercloud::SyncChunk> & syncChunks);

private:
    template <class T>
    class Q_DECL_HIDDEN CompareItemByName
    {
    public:
        CompareItemByName(const QString & name) : m_name(name) {}
        bool operator()(const T & item) const;

    private:
        const QString m_name;
    };

    template <class T>
    class Q_DECL_HIDDEN CompareItemByGuid
    {
    public:
        CompareItemByGuid(const QString & guid) : m_guid(guid) {}
        bool operator()(const T & item) const;

    private:
        const QString m_guid;
    };

    using TagsList = QList<qevercloud::Tag>;
    using SavedSearchesList = QList<qevercloud::SavedSearch>;
    using LinkedNotebooksList = QList<qevercloud::LinkedNotebook>;
    using NotebooksList = QList<qevercloud::Notebook>;
    using NotesList = QList<qevercloud::Note>;
    using ResourcesList = QList<qevercloud::Resource>;

    bool sortTagsByParentChildRelations(TagsList & tags);

    struct InkNoteResourceData
    {
        InkNoteResourceData() = default;

        InkNoteResourceData(
            const QString & resourceGuid, const QString & noteGuid, int height,
            int width) :
            m_resourceGuid(resourceGuid),
            m_noteGuid(noteGuid), m_resourceHeight(height),
            m_resourceWidth(width)
        {}

        QString m_resourceGuid;
        QString m_noteGuid;
        int m_resourceHeight = 0;
        int m_resourceWidth = 0;
    };

    /**
     * @brief The PostponedConflictingResourceData class encapsulates several
     * data pieces required to be stored when there's a conflict during the sync
     * of some individual resource but the attempt to download the full resource
     * data causes the rate limit exceeding.
     *
     * The conflict of the individual resource is treated as the conflict of
     * note owning that resource; so we need to preserve the "remote" version
     * of the note (with the resource in question downloaded from Evernote
     * service) + the local conflicting note (with local version of conflicting
     * resource) and the resource (without full data as its downloading failed).
     * So when the time comes, we can try to download the full resource data and
     * if it works out, resolve the resource sync conflict.
     */
    class PostponedConflictingResourceData : public Printable
    {
    public:
        Note m_remoteNote;
        Note m_localConflictingNote;
        Resource m_remoteNoteResourceWithoutFullData;

        virtual QTextStream & print(QTextStream & strm) const override;
    };

    enum class SyncMode
    {
        FullSync = 0,
        IncrementalSync
    };

    friend QTextStream & operator<<(QTextStream & strm, const SyncMode & obj);
    friend QDebug & operator<<(QDebug & dbg, const SyncMode & obj);

    friend class NoteSyncConflictResolverManager;

private:
    IManager & m_manager;
    bool m_connectedToLocalStorage = false;

    mutable bool m_connectedToUserOwnNoteStore = false;

    QString m_host;

    qint32 m_maxSyncChunksPerOneDownload = 50;
    SyncMode m_lastSyncMode = SyncMode::FullSync;

    qint32 m_lastUpdateCount = 0;
    qevercloud::Timestamp m_lastSyncTime = 0;

    // Denotes whether the full sync of stuff from user's own account
    // had been performed at least once in the past
    bool m_onceSyncDone = false;

    qint32 m_lastUsnOnStart = -1;
    qint32 m_lastSyncChunksDownloadedUsn = -1;

    bool m_syncChunksDownloaded = false;
    bool m_fullNoteContentsDownloaded = false;
    bool m_expungedFromServerToClient = false;
    bool m_linkedNotebooksSyncChunksDownloaded = false;

    bool m_active = false;

    bool m_edamProtocolVersionChecked = false;

    QVector<qevercloud::SyncChunk> m_syncChunks;
    QVector<qevercloud::SyncChunk> m_linkedNotebookSyncChunks;
    QSet<QString> m_linkedNotebookGuidsForWhichSyncChunksWereDownloaded;

    qevercloud::AccountLimits m_accountLimits;

    TagsContainer m_tags;
    TagsList m_tagsPendingProcessing;
    TagsList m_tagsPendingAddOrUpdate;
    QList<QString> m_expungedTags;
    QSet<QUuid> m_findTagByNameRequestIds;
    QHash<QUuid, QString> m_linkedNotebookGuidsByFindTagByNameRequestIds;
    QSet<QUuid> m_findTagByGuidRequestIds;
    QSet<QUuid> m_addTagRequestIds;
    QSet<QUuid> m_updateTagRequestIds;
    QSet<QUuid> m_expungeTagRequestIds;
    bool m_pendingTagsSyncStart = false;

    TagSyncCache m_tagSyncCache;
    QMap<QString, TagSyncCache *> m_tagSyncCachesByLinkedNotebookGuids;
    QSet<QString> m_linkedNotebookGuidsPendingTagSyncCachesFill;

    QHash<QString, QString> m_linkedNotebookGuidsByTagGuids;
    QUuid m_expungeNotelessTagsRequestId;

    SavedSearchesList m_savedSearches;
    SavedSearchesList m_savedSearchesPendingAddOrUpdate;
    QList<QString> m_expungedSavedSearches;
    QSet<QUuid> m_findSavedSearchByNameRequestIds;
    QSet<QUuid> m_findSavedSearchByGuidRequestIds;
    QSet<QUuid> m_addSavedSearchRequestIds;
    QSet<QUuid> m_updateSavedSearchRequestIds;
    QSet<QUuid> m_expungeSavedSearchRequestIds;

    SavedSearchSyncCache m_savedSearchSyncCache;

    LinkedNotebooksList m_linkedNotebooks;
    LinkedNotebooksList m_linkedNotebooksPendingAddOrUpdate;
    QList<QString> m_expungedLinkedNotebooks;
    QSet<QUuid> m_findLinkedNotebookRequestIds;
    QSet<QUuid> m_addLinkedNotebookRequestIds;
    QSet<QUuid> m_updateLinkedNotebookRequestIds;
    QSet<QUuid> m_expungeLinkedNotebookRequestIds;
    bool m_pendingLinkedNotebooksSyncStart = false;

    QList<LinkedNotebook> m_allLinkedNotebooks;
    QUuid m_listAllLinkedNotebooksRequestId;
    bool m_allLinkedNotebooksListed = false;

    QString m_authenticationToken;
    QString m_shardId;
    qevercloud::Timestamp m_authenticationTokenExpirationTime = 0;
    bool m_pendingAuthenticationTokenAndShardId = false;

    User m_user;
    QUuid m_findUserRequestId;
    QUuid m_addOrUpdateUserRequestId;
    bool m_onceAddedOrUpdatedUserInLocalStorage = false;

    QHash<QString, std::pair<QString, QString>>
        m_authenticationTokensAndShardIdsByLinkedNotebookGuid;
    QHash<QString, qevercloud::Timestamp>
        m_authenticationTokenExpirationTimesByLinkedNotebookGuid;
    bool m_pendingAuthenticationTokensForLinkedNotebooks = false;

    QHash<QString, qevercloud::SyncState> m_syncStatesByLinkedNotebookGuid;

    QHash<QString, qint32> m_lastUpdateCountByLinkedNotebookGuid;
    QHash<QString, qevercloud::Timestamp> m_lastSyncTimeByLinkedNotebookGuid;
    QSet<QString> m_linkedNotebookGuidsForWhichFullSyncWasPerformed;

    // Guids of linked notebooks for which full sync of stuff from these
    // linked notebooks had been performed at least once in the past
    QSet<QString> m_linkedNotebookGuidsOnceFullySynced;

    NotebooksList m_notebooks;
    NotebooksList m_notebooksPendingAddOrUpdate;
    QList<QString> m_expungedNotebooks;
    QSet<QUuid> m_findNotebookByNameRequestIds;
    QHash<QUuid, QString> m_linkedNotebookGuidsByFindNotebookByNameRequestIds;
    QSet<QUuid> m_findNotebookByGuidRequestIds;
    QSet<QUuid> m_addNotebookRequestIds;
    QSet<QUuid> m_updateNotebookRequestIds;
    QSet<QUuid> m_expungeNotebookRequestIds;
    bool m_pendingNotebooksSyncStart = false;

    NotebookSyncCache m_notebookSyncCache;
    QMap<QString, NotebookSyncCache *>
        m_notebookSyncCachesByLinkedNotebookGuids;

    QHash<QString, QString> m_linkedNotebookGuidsByNotebookGuids;
    QHash<QString, QString> m_linkedNotebookGuidsByResourceGuids;

    NotesList m_notes;
    NotesList m_notesPendingAddOrUpdate;
    quint32 m_originalNumberOfNotes;
    quint32 m_numNotesDownloaded;
    QList<QString> m_expungedNotes;
    QSet<QUuid> m_findNoteByGuidRequestIds;
    QSet<QUuid> m_addNoteRequestIds;
    QSet<QUuid> m_updateNoteRequestIds;
    QSet<QUuid> m_expungeNoteRequestIds;
    QSet<QString> m_guidsOfProcessedNonExpungedNotes;

    using NoteDataPerFindNotebookRequestId =
        QHash<QUuid, std::pair<Note, QUuid>>;
    NoteDataPerFindNotebookRequestId
        m_notesWithFindRequestIdsPerFindNotebookRequestId;

    QScopedPointer<NoteSyncConflictResolverManager>
        m_pNoteSyncConflictResolverManager;

    QMap<std::pair<QString, QString>, Notebook> m_notebooksPerNoteIds;

    ResourcesList m_resources;
    ResourcesList m_resourcesPendingAddOrUpdate;
    quint32 m_originalNumberOfResources;
    quint32 m_numResourcesDownloaded;
    QSet<QUuid> m_findResourceByGuidRequestIds;
    QSet<QUuid> m_addResourceRequestIds;
    QSet<QUuid> m_updateResourceRequestIds;
    QHash<QUuid, Resource> m_resourcesByMarkNoteOwningResourceDirtyRequestIds;
    QHash<QUuid, Resource> m_resourcesByFindNoteRequestIds;

    using InkNoteResourceDataPerFindNotebookRequestId =
        QHash<QUuid, InkNoteResourceData>;
    InkNoteResourceDataPerFindNotebookRequestId
        m_inkNoteResourceDataPerFindNotebookRequestId;

    using ResourceGuidsPendingInkNoteImageDownloadPerNoteGuid =
        QMultiHash<QString, QString>;
    ResourceGuidsPendingInkNoteImageDownloadPerNoteGuid
        m_resourceGuidsPendingInkNoteImageDownloadPerNoteGuid;

    ResourceGuidsPendingInkNoteImageDownloadPerNoteGuid
        m_resourceGuidsPendingFindNotebookForInkNoteImageDownloadPerNoteGuid;

    QHash<QUuid, Note>
        m_notesPendingInkNoteImagesDownloadByFindNotebookRequestId;
    QHash<QUuid, Note> m_notesPendingThumbnailDownloadByFindNotebookRequestId;

    QHash<QString, Note> m_notesPendingThumbnailDownloadByGuid;
    QSet<QUuid> m_updateNoteWithThumbnailRequestIds;

    /**
     * This set contains the guids of resources found existing within
     * the local storage; it is used in the series of async resource processing
     * to judge whether it should be added to the local storage or updated
     * within it
     */
    QSet<QString> m_guidsOfResourcesFoundWithinTheLocalStorage;

    QSet<QString> m_localUidsOfElementsAlreadyAttemptedToFindByName;

    QHash<QString, qevercloud::Note>
        m_notesPendingDownloadForAddingToLocalStorage;
    QHash<QString, Note> m_notesPendingDownloadForUpdatingInLocalStorageByGuid;

    QHash<QString, std::pair<Resource, Note>>
        m_resourcesPendingDownloadForAddingToLocalStorageWithNotesByResourceGuid;
    QHash<QString, std::pair<Resource, Note>>
        m_resourcesPendingDownloadForUpdatingInLocalStorageWithNotesByResourceGuid;

    FullSyncStaleDataItemsExpunger::SyncedGuids
        m_fullSyncStaleDataItemsSyncedGuids;
    FullSyncStaleDataItemsExpunger * m_pFullSyncStaleDataItemsExpunger =
        nullptr;
    QMap<QString, FullSyncStaleDataItemsExpunger *>
        m_fullSyncStaleDataItemsExpungersByLinkedNotebookGuid;

    QHash<int, Note> m_notesToAddPerAPICallPostponeTimerId;
    QHash<int, Note> m_notesToUpdatePerAPICallPostponeTimerId;

    QHash<int, std::pair<Resource, Note>>
        m_resourcesToAddWithNotesPerAPICallPostponeTimerId;
    QHash<int, std::pair<Resource, Note>>
        m_resourcesToUpdateWithNotesPerAPICallPostponeTimerId;

    QHash<int, PostponedConflictingResourceData>
        m_postponedConflictingResourceDataPerAPICallPostponeTimerId;

    QHash<int, qint32> m_afterUsnForSyncChunkPerAPICallPostponeTimerId;

    int m_getLinkedNotebookSyncStateBeforeStartAPICallPostponeTimerId = 0;
    int m_downloadLinkedNotebookSyncChunkAPICallPostponeTimerId = 0;
    int m_getSyncStateBeforeStartAPICallPostponeTimerId = 0;
    int m_syncUserPostponeTimerId = 0;
    int m_syncAccountLimitsPostponeTimerId = 0;

    bool m_gotLastSyncParameters = false;
};

} // namespace quentier

#endif // LIB_QUENTIER_SYNCHRONIZATION_REMOTE_TO_LOCAL_SYNCHRONIZATION_MANAGER_H
