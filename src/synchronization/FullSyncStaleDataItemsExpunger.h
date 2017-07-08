/*
 * Copyright 2017 Dmitry Ivanov
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
#include <QHash>
#include <QUuid>

namespace quentier {

QT_FORWARD_DECLARE_CLASS(NotebookSyncCache)
QT_FORWARD_DECLARE_CLASS(TagSyncCache)
QT_FORWARD_DECLARE_CLASS(SavedSearchSyncCache)

/**
 * @brief The FullSyncStaleDataItemsExpunger class ensures there would be no stale data items
 * left within the local storage after the full sync performed not for the first time.
 *
 * From time to time the Evernote synchronization protocol (EDAM) might require the client to perform
 * a full sync instead of incremental sync. It might happen because the client hasn't synced with the service
 * for too long so that the guids of expunged data items are no longer stored within the service.
 * It also might happen in case of some unforeseen service's malfunction so that the status quo needs to be
 * restored for all clients. In any event, sometimes Evernote might require the client to perform the full sync.
 *
 * When the client performs full sync for the first time, there is no need for the client to expunge anything:
 * it starts with empty local storage and only fills in the data received from the service into it. However,
 * when full sync is done after the local storage database has been filled with something, the client
 * needs to understand which data items are now stale within its local storage (i.e. were expunged from the service
 * at some point) and thus need to be expunged from the client's local storage. These are all data items which
 * have guids which were not referenced during the last full sync. However, for the sake of preserving the unsynced
 * data, the matching data items which are marked as dirty are not expunged from the local storage: instead
 * their guid and update sequence number are wiped out so that they are presented as new data items to the service.
 * That happens during sending the local changes to Evernote service.
 */
class FullSyncStaleDataItemsExpunger: public QObject
{
    Q_OBJECT
public:
    struct Caches
    {
        Caches(const QList<NotebookSyncCache*> & notebookSyncCaches,
               const QList<TagSyncCache*> & tagSyncCaches,
               SavedSearchSyncCache & savedSearchSyncCache);

        QList<QPointer<NotebookSyncCache> > m_notebookSyncCaches;
        QList<QPointer<TagSyncCache> >      m_tagSyncCaches;
        QPointer<SavedSearchSyncCache>      m_savedSearchSyncCache;
    };

    struct SyncedGuids
    {
        QSet<QString>   m_syncedNotebookGuids;
        QSet<QString>   m_syncedTagGuids;
        QSet<QString>   m_syncedNoteGuids;
        QSet<QString>   m_syncedSavedSearchGuids;
    };

public:
    explicit FullSyncStaleDataItemsExpunger(LocalStorageManagerAsync & localStorageManagerAsync,
                                            const Caches & caches, const SyncedGuids & syncedGuids,
                                            QObject * parent = Q_NULLPTR);

Q_SIGNALS:
    void finished();

private:
    void connectToLocalStorage();
    void disconnectFromLocalStorage();

private:
    LocalStorageManagerAsync &      m_localStorageManagerAsync;
    bool                            m_connectedToLocalStorage;

    Caches                          m_caches;
    SyncedGuids                     m_syncedGuids;

    QHash<QUuid, QString>           m_notebookGuidsByExpungeRequestId;
    QHash<QUuid, QString>           m_tagGuidsByExpungeRequestId;
    QHash<QUuid, QString>           m_noteGuidsByExpungeRequestId;
    QHash<QUuid, QString>           m_savedSearchGuidsByExpungeRequestId;

    QHash<QUuid, QString>           m_notebookGuidsByUpdateRequestId;
    QHash<QUuid, QString>           m_tagGuidsByUpdateRequestId;
    QHash<QUuid, QString>           m_noteGuidsByUpdateRequestId;
    QHash<QUuid, QString>           m_savedSearchGuidByUpdateRequestId;
};

} // namespace quentier

#endif // LIB_QUENTIER_SYNCHRONIZATION_FULL_SYNC_STALE_DATA_ITEMS_EXPUNGER_H
