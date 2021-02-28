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

#ifndef LIB_QUENTIER_SYNCHRONIZATION_SEND_LOCAL_CHANGES_MANAGER_H
#define LIB_QUENTIER_SYNCHRONIZATION_SEND_LOCAL_CHANGES_MANAGER_H

#include "SynchronizationShared.h"

#include <quentier/local_storage/LocalStorageManager.h>
#include <quentier/types/ErrorString.h>
#include <quentier/types/Note.h>
#include <quentier/types/Notebook.h>
#include <quentier/types/SavedSearch.h>
#include <quentier/types/Tag.h>

#include <quentier/synchronization/INoteStore.h>

#include <QObject>

#include <utility>

namespace quentier {

QT_FORWARD_DECLARE_CLASS(LocalStorageManagerAsync)

class Q_DECL_HIDDEN SendLocalChangesManager final : public QObject
{
    Q_OBJECT
public:
    class Q_DECL_HIDDEN IManager
    {
    public:
        virtual LocalStorageManagerAsync & localStorageManagerAsync() = 0;
        virtual INoteStore & noteStore() = 0;

        virtual INoteStore * noteStoreForLinkedNotebook(
            const LinkedNotebook & linkedNotebook) = 0;

        virtual ~IManager() {}
    };

    explicit SendLocalChangesManager(
        IManager & manager, QObject * parent = nullptr);

    bool active() const;

Q_SIGNALS:
    void failure(ErrorString errorDescription);

    void finished(
        qint32 lastUpdateCount,
        QHash<QString, qint32> lastUpdateCountByLinkedNotebookGuid);

    void rateLimitExceeded(qint32 secondsToWait);
    void conflictDetected();
    void shouldRepeatIncrementalSync();

    void stopped();

    void requestAuthenticationToken();
    void requestAuthenticationTokensForLinkedNotebooks(
        QVector<LinkedNotebookAuthData>
            linkedNotebookGuidsAndSharedNotebookGlobalIds);

    // progress information
    void receivedUserAccountDirtyObjects();
    void receivedDirtyObjectsFromLinkedNotebooks();

public Q_SLOTS:
    void start(
        qint32 updateCount,
        QHash<QString, qint32> updateCountByLinkedNotebookGuid);
    void stop();

    void onAuthenticationTokensForLinkedNotebooksReceived(
        QHash<QString, std::pair<QString, QString>>
            authTokensAndShardIdsByLinkedNotebookGuid,
        QHash<QString, qevercloud::Timestamp>
            authTokenExpirationByLinkedNotebookGuid);

    // private signals:
Q_SIGNALS:
    // signals to request dirty & not yet synchronized objects from local
    // storage
    void requestLocalUnsynchronizedTags(
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListTagsOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QString linkedNotebookGuid, QUuid requestId);

    void requestLocalUnsynchronizedSavedSearches(
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListSavedSearchesOrder order,
        LocalStorageManager::OrderDirection orderDirection, QUuid requestId);

    void requestLocalUnsynchronizedNotebooks(
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListNotebooksOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QString linkedNotebookGuid, QUuid requestId);

    void requestLocalUnsynchronizedNotes(
        LocalStorageManager::ListObjectsOptions flag,
        LocalStorageManager::GetNoteOptions options, size_t limit,
        size_t offset, LocalStorageManager::ListNotesOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QString linkedNotebookGuid, QUuid requestId);

    // signal to request the list of linked notebooks so that all linked
    // notebook guids would be available
    void requestLinkedNotebooksList(
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListLinkedNotebooksOrder order,
        LocalStorageManager::OrderDirection orderDirection, QUuid requestId);

    void updateTag(Tag tag, QUuid requestId);
    void updateSavedSearch(SavedSearch savedSearch, QUuid requestId);
    void updateNotebook(Notebook notebook, QUuid requestId);

    void updateNote(
        Note note, LocalStorageManager::UpdateNoteOptions options,
        QUuid requestId);

    void findNotebook(Notebook notebook, QUuid requestId);

private Q_SLOTS:
    void onListDirtyTagsCompleted(
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListTagsOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QString linkedNotebookGuid, QList<Tag> tags, QUuid requestId);

    void onListDirtyTagsFailed(
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListTagsOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QString linkedNotebookGuid, ErrorString errorDescription,
        QUuid requestId);

    void onListDirtySavedSearchesCompleted(
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListSavedSearchesOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QList<SavedSearch> savedSearches, QUuid requestId);

    void onListDirtySavedSearchesFailed(
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListSavedSearchesOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        ErrorString errorDescription, QUuid requestId);

    void onListDirtyNotebooksCompleted(
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListNotebooksOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QString linkedNotebookGuid, QList<Notebook> notebooks, QUuid requestId);

    void onListDirtyNotebooksFailed(
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListNotebooksOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QString linkedNotebookGuid, ErrorString errorDescription,
        QUuid requestId);

    void onListDirtyNotesCompleted(
        LocalStorageManager::ListObjectsOptions flag,
        LocalStorageManager::GetNoteOptions options, size_t limit,
        size_t offset, LocalStorageManager::ListNotesOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QString linkedNotebookGuid, QList<Note> notes, QUuid requestId);

    void onListDirtyNotesFailed(
        LocalStorageManager::ListObjectsOptions flag,
        LocalStorageManager::GetNoteOptions options, size_t limit,
        size_t offset, LocalStorageManager::ListNotesOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QString linkedNotebookGuid, ErrorString errorDescription,
        QUuid requestId);

    void onListLinkedNotebooksCompleted(
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListLinkedNotebooksOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QList<LinkedNotebook> linkedNotebooks, QUuid requestId);

    void onListLinkedNotebooksFailed(
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListLinkedNotebooksOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        ErrorString errorDescription, QUuid requestId);

    void onUpdateTagCompleted(Tag tag, QUuid requestId);
    void onUpdateTagFailed(
        Tag tag, ErrorString errorDescription, QUuid requestId);

    void onUpdateSavedSearchCompleted(SavedSearch savedSearch, QUuid requestId);

    void onUpdateSavedSearchFailed(
        SavedSearch savedSearch, ErrorString errorDescription, QUuid requestId);

    void onUpdateNotebookCompleted(Notebook notebook, QUuid requestId);

    void onUpdateNotebookFailed(
        Notebook notebook, ErrorString errorDescription, QUuid requestId);

    void onUpdateNoteCompleted(
        Note note, LocalStorageManager::UpdateNoteOptions options,
        QUuid requestId);

    void onUpdateNoteFailed(
        Note note, LocalStorageManager::UpdateNoteOptions options,
        ErrorString errorDescription, QUuid requestId);

    void onFindNotebookCompleted(Notebook notebook, QUuid requestId);

    void onFindNotebookFailed(
        Notebook notebook, ErrorString errorDescription, QUuid requestId);

private:
    virtual void timerEvent(QTimerEvent * pEvent) override;

private:
    void connectToLocalStorage();
    void disconnectFromLocalStorage();

    bool requestStuffFromLocalStorage(
        const QString & linkedNotebookGuid = QLatin1String(""));

    void checkListLocalStorageObjectsCompletion();

    void sendLocalChanges();
    void sendTags();
    void sendSavedSearches();
    void sendNotebooks();

    void checkAndSendNotes();
    void sendNotes();

    void findNotebooksForNotes();

    bool rateLimitIsActive() const;

    void checkSendLocalChangesAndDirtyFlagsRemovingUpdatesAndFinalize();
    void checkDirtyFlagRemovingUpdatesAndFinalize();
    void finalize();
    void clear();
    void killAllTimers();

    bool checkAndRequestAuthenticationTokensForLinkedNotebooks();

    void handleAuthExpiration();

private:
    class Q_DECL_HIDDEN CompareLinkedNotebookAuthDataByGuid
    {
    public:
        CompareLinkedNotebookAuthDataByGuid(const QString & guid) : m_guid(guid)
        {}

        bool operator()(const LinkedNotebookAuthData & authData) const
        {
            return authData.m_guid == m_guid;
        }

    private:
        QString m_guid;
    };

    class Q_DECL_HIDDEN FlagGuard
    {
    public:
        FlagGuard(bool & flag) : m_flag(flag)
        {
            m_flag = true;
        }
        ~FlagGuard()
        {
            m_flag = false;
        }

    private:
        Q_DISABLE_COPY(FlagGuard)

    private:
        bool & m_flag;
    };

private:
    IManager & m_manager;

    qint32 m_lastUpdateCount = 0;
    QHash<QString, qint32> m_lastUpdateCountByLinkedNotebookGuid;

    bool m_shouldRepeatIncrementalSync = false;

    bool m_active = false;

    bool m_connectedToLocalStorage = false;
    bool m_receivedDirtyLocalStorageObjectsFromUsersAccount = false;
    bool m_receivedAllDirtyLocalStorageObjects = false;

    QUuid m_listDirtyTagsRequestId;
    QUuid m_listDirtySavedSearchesRequestId;
    QUuid m_listDirtyNotebooksRequestId;
    QUuid m_listDirtyNotesRequestId;
    QUuid m_listLinkedNotebooksRequestId;

    QSet<QUuid> m_listDirtyTagsFromLinkedNotebooksRequestIds;
    QSet<QUuid> m_listDirtyNotebooksFromLinkedNotebooksRequestIds;
    QSet<QUuid> m_listDirtyNotesFromLinkedNotebooksRequestIds;

    QList<Tag> m_tags;
    QList<SavedSearch> m_savedSearches;
    QList<Notebook> m_notebooks;
    QList<Note> m_notes;

    bool m_sendingTags = false;
    bool m_sendingSavedSearches = false;
    bool m_sendingNotebooks = false;
    bool m_sendingNotes = false;

    QSet<QString>
        m_linkedNotebookGuidsForWhichStuffWasRequestedFromLocalStorage;

    QVector<LinkedNotebookAuthData> m_linkedNotebookAuthData;

    QHash<QString, std::pair<QString, QString>>
        m_authenticationTokensAndShardIdsByLinkedNotebookGuid;
    QHash<QString, qevercloud::Timestamp>
        m_authenticationTokenExpirationTimesByLinkedNotebookGuid;
    bool m_pendingAuthenticationTokensForLinkedNotebooks = false;

    QSet<QUuid> m_updateTagRequestIds;
    QSet<QUuid> m_updateSavedSearchRequestIds;
    QSet<QUuid> m_updateNotebookRequestIds;
    QSet<QUuid> m_updateNoteRequestIds;

    QSet<QUuid> m_findNotebookRequestIds;
    QHash<QString, Notebook> m_notebooksByGuidsCache;

    int m_sendTagsPostponeTimerId = 0;
    int m_sendSavedSearchesPostponeTimerId = 0;
    int m_sendNotebooksPostponeTimerId = 0;
    int m_sendNotesPostponeTimerId = 0;
};

} // namespace quentier

#endif // LIB_QUENTIER_SYNCHRONIZATION_SEND_LOCAL_CHANGES_MANAGER_H
