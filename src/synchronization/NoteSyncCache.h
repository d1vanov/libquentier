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

#ifndef LIB_QUENTIER_SYNCHRONIZATION_NOTE_SYNC_CACHE_H
#define LIB_QUENTIER_SYNCHRONIZATION_NOTE_SYNC_CACHE_H

#include <quentier/local_storage/LocalStorageManagerAsync.h>
#include <QObject>
#include <QHash>
#include <QSet>
#include <QUuid>

// NOTE: Workaround a bug in Qt4 which may prevent building with some boost versions
#ifndef Q_MOC_RUN
#include <boost/bimap.hpp>
#endif

namespace quentier {

class NoteSyncCache: public QObject
{
    Q_OBJECT
public:
    NoteSyncCache(LocalStorageManagerAsync & localStorageManagerAsync, QObject * parent = Q_NULLPTR);

    void clear();

    /**
     * @return True if the cache is already filled with up-to-moment data, false otherwise
     */
    bool isFilled() const;

    typedef boost::bimap<QString, QString> NoteGuidToLocalUidBimap;

    const NoteGuidToLocalUidBimap & noteGuidToLocalUidBimap() const { return m_noteGuidToLocalUidBimap; }
    const QHash<QString,Note> & dirtyNotesByGuid() const { return m_dirtyNotesByGuid; }

Q_SIGNALS:
    void filled();
    void failure(ErrorString errorDescription);

// private signals
    void listNotes(LocalStorageManager::ListObjectsOptions flag,
                   bool withResourceBinaryData, size_t limit, size_t offset,
                   LocalStorageManager::ListNotesOrder::type order,
                   LocalStorageManager::OrderDirection::type orderDirection,
                   QString linkedNotebookGuid, QUuid requestId);

public Q_SLOTS:
    /**
     * Start collecting the information about notes; does nothing if the information is already collected
     * or is being collected at the moment, otherwise initiates the sequence of actions required to collect
     * the note information
     */
    void fill();

private Q_SLOTS:
    void onListNotesComplete(LocalStorageManager::ListObjectsOptions flag, bool withResourceBinaryData,
                             size_t limit, size_t offset, LocalStorageManager::ListNotesOrder::type order,
                             LocalStorageManager::OrderDirection::type orderDirection,
                             QString linkedNotebookGuid, QList<Note> foundNotes, QUuid requestId);
    void onListNotesFailed(LocalStorageManager::ListObjectsOptions flag, bool withResourceBinaryData,
                           size_t limit, size_t offset, LocalStorageManager::ListNotesOrder::type order,
                           LocalStorageManager::OrderDirection::type orderDirection,
                           QString linkedNotebookGuid, ErrorString errorDescription, QUuid requestId);
    void onAddNoteComplete(Note note, QUuid requestId);
    void onUpdateNoteComplete(Note note, bool updateResources, bool updateTags, QUuid requestId);
    void onExpungeNoteComplete(Note note, QUuid requestId);

private:
    void connectToLocalStorage();
    void disconnectFromLocalStorage();

    void requestNotesList();

    void removeNote(const QString & noteLocalUid);
    void processNote(const Note & note);

private:
    LocalStorageManagerAsync &          m_localStorageManagerAsync;
    bool                                m_connectedToLocalStorage;

    NoteGuidToLocalUidBimap             m_noteGuidToLocalUidBimap;
    QHash<QString,Note>                 m_dirtyNotesByGuid;

    QUuid                               m_listNotesRequestId;
    size_t                              m_limit;
    size_t                              m_offset;
};

} // namespace quentier

#endif // LIB_QUENTIER_SYNCHRONIZATION_NOTE_SYNC_CACHE_H
