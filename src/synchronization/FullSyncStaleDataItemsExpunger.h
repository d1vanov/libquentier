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

#ifndef LIB_QUENTIER_SYNCHRONIZATION_FULL_SYNC_STALE_DATA_ITEMS_EXPUNGER_H
#define LIB_QUENTIER_SYNCHRONIZATION_FULL_SYNC_STALE_DATA_ITEMS_EXPUNGER_H

#include "NoteSyncCache.h"

#include <QPointer>
#include <QSet>
#include <QUuid>

namespace quentier {

QT_FORWARD_DECLARE_CLASS(NotebookSyncCache)
QT_FORWARD_DECLARE_CLASS(SavedSearchSyncCache)
QT_FORWARD_DECLARE_CLASS(TagSyncCache)

/**
 * @brief The FullSyncStaleDataItemsExpunger class ensures there would be no
 * stale data items left within the local storage after the full sync performed
 * not for the first time.
 *
 * From time to time the Evernote synchronization protocol (EDAM) might require
 * the client to perform a full sync instead of incremental sync. It might
 * happen because the client hasn't synced with the service for too long so that
 * the guids of expunged data items are no longer stored within the service. It
 * also might happen in case of some unforeseen service's malfunction so that
 * the status quo needs to be restored for all clients. In any event, sometimes
 * Evernote might require the client to perform the full sync.
 *
 * When the client performs full sync for the first time, there is no need for
 * the client to expunge anything: it starts with empty local storage and only
 * fills in the data received from the service into it. However, when full sync
 * is done after the local storage database has been filled with something,
 * the client needs to understand which data items are now stale within its
 * local storage (i.e. were expunged from the service at some point) and thus
 * need to be expunged from the client's local storage. These are all data items
 * which have guids which were not referenced during the last full sync.
 * However, for the sake of preserving the unsynced data, the matching data
 * items which are marked as dirty are not expunged from the local storage:
 * instead their guid and update sequence number are wiped out so that they are
 * presented as new data items to the service. That happens during sending
 * the local changes to Evernote service.
 */
class Q_DECL_HIDDEN FullSyncStaleDataItemsExpunger final : public QObject
{
    Q_OBJECT
public:
    struct SyncedGuids
    {
        QSet<QString> m_syncedNotebookGuids;
        QSet<QString> m_syncedTagGuids;
        QSet<QString> m_syncedNoteGuids;
        QSet<QString> m_syncedSavedSearchGuids;
    };

public:
    explicit FullSyncStaleDataItemsExpunger(
        LocalStorageManagerAsync & localStorageManagerAsync,
        NotebookSyncCache & notebookSyncCache, TagSyncCache & tagSyncCache,
        SavedSearchSyncCache & savedSearchSyncCache,
        const SyncedGuids & syncedGuids, const QString & linkedNotebookGuid,
        QObject * parent = nullptr);

    const QString & linkedNotebookGuid() const
    {
        return m_linkedNotebookGuid;
    }

Q_SIGNALS:
    void finished();
    void failure(ErrorString errorDescription);

    // private signals:
    void expungeNotebook(Notebook notebook, QUuid requestId);
    void expungeTag(Tag tag, QUuid requestId);
    void expungeSavedSearch(SavedSearch search, QUuid requestId);
    void expungeNote(Note note, QUuid requestId);

    void updateNotebook(Notebook notebook, QUuid requestId);
    void updateTag(Tag tag, QUuid requestId);
    void updateSavedSearch(SavedSearch search, QUuid requestId);

    void updateNote(
        Note note, LocalStorageManager::UpdateNoteOptions options,
        QUuid requestId);

public Q_SLOTS:
    void start();

private Q_SLOTS:
    void onNotebookCacheFilled();
    void onTagCacheFilled();
    void onSavedSearchCacheFilled();
    void onNoteCacheFilled();

    void onExpungeNotebookComplete(Notebook notebook, QUuid requestId);

    void onExpungeNotebookFailed(
        Notebook notebook, ErrorString errorDescription, QUuid requestId);

    void onExpungeTagComplete(
        Tag tag, QStringList expungedChildTagLocalUids, QUuid requestId);

    void onExpungeTagFailed(
        Tag tag, ErrorString errorDescription, QUuid requestId);

    void onExpungeSavedSearchComplete(SavedSearch search, QUuid requestId);

    void onExpungeSavedSearchFailed(
        SavedSearch search, ErrorString errorDescription, QUuid requestId);

    void onExpungeNoteComplete(Note note, QUuid requestId);

    void onExpungeNoteFailed(
        Note note, ErrorString errorDescription, QUuid requestId);

    void onUpdateNotebookComplete(Notebook notebook, QUuid requestId);

    void onUpdateNotebookFailed(
        Notebook notebook, ErrorString errorDescription, QUuid requestId);

    void onUpdateTagComplete(Tag tag, QUuid requestId);

    void onUpdateTagFailed(
        Tag tag, ErrorString errorDescription, QUuid requestId);

    void onUpdateSavedSearchComplete(SavedSearch search, QUuid requestId);

    void onUpdateSavedSearchFailed(
        SavedSearch search, ErrorString errorDescription, QUuid requestId);

    void onUpdateNoteComplete(
        Note note, LocalStorageManager::UpdateNoteOptions options,
        QUuid requestId);

    void onUpdateNoteFailed(
        Note note, LocalStorageManager::UpdateNoteOptions options,
        ErrorString errorDescription, QUuid requestId);

private:
    void connectToLocalStorage();
    void disconnectFromLocalStorage();

    void checkAndRequestCachesFilling();
    bool pendingCachesFilling() const;

    void analyzeDataAndSendRequestsOrResult();

    void checkRequestsCompletionAndSendResult();
    void checkTagUpdatesCompletionAndSendExpungeTagRequests();

private:
    LocalStorageManagerAsync & m_localStorageManagerAsync;
    bool m_connectedToLocalStorage = false;

    bool m_inProgress = false;

    QPointer<NotebookSyncCache> m_pNotebookSyncCache;
    QPointer<TagSyncCache> m_pTagSyncCache;
    QPointer<SavedSearchSyncCache> m_pSavedSearchSyncCache;
    NoteSyncCache m_noteSyncCache;

    SyncedGuids m_syncedGuids;

    QString m_linkedNotebookGuid;

    bool m_pendingNotebookSyncCache = false;
    bool m_pendingTagSyncCache = false;
    bool m_pendingSavedSearchSyncCache = false;
    bool m_pendingNoteSyncCache = false;

    QSet<QString> m_tagGuidsToExpunge;

    QSet<QUuid> m_expungeNotebookRequestIds;
    QSet<QUuid> m_expungeTagRequestIds;
    QSet<QUuid> m_expungeNoteRequestIds;
    QSet<QUuid> m_expungeSavedSearchRequestIds;

    QSet<QUuid> m_updateNotebookRequestId;
    QSet<QUuid> m_updateTagRequestIds;
    QSet<QUuid> m_updateNoteRequestIds;
    QSet<QUuid> m_updateSavedSearchRequestIds;
};

} // namespace quentier

#endif // LIB_QUENTIER_SYNCHRONIZATION_FULL_SYNC_STALE_DATA_ITEMS_EXPUNGER_H
