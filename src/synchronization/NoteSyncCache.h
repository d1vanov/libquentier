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

#ifndef LIB_QUENTIER_SYNCHRONIZATION_NOTE_SYNC_CACHE_H
#define LIB_QUENTIER_SYNCHRONIZATION_NOTE_SYNC_CACHE_H

#include <quentier/local_storage/LocalStorageManagerAsync.h>
#include <quentier/utility/SuppressWarnings.h>

#include <QHash>
#include <QObject>
#include <QSet>
#include <QUuid>

SAVE_WARNINGS

MSVC_SUPPRESS_WARNING(4834)

#include <boost/bimap.hpp>

RESTORE_WARNINGS

namespace quentier {

class Q_DECL_HIDDEN NoteSyncCache final : public QObject
{
    Q_OBJECT
public:
    NoteSyncCache(
        LocalStorageManagerAsync & localStorageManagerAsync,
        const QString & linkedNotebookGuid, QObject * parent = nullptr);

    void clear();

    const QString & linkedNotebookGuid() const
    {
        return m_linkedNotebookGuid;
    }

    /**
     * @return  True if the cache is already filled with up-to-moment data,
     *          false otherwise
     */
    bool isFilled() const;

    using NoteGuidToLocalUidBimap = boost::bimap<QString, QString>;

    const NoteGuidToLocalUidBimap & noteGuidToLocalUidBimap() const
    {
        return m_noteGuidToLocalUidBimap;
    }

    const QHash<QString, Note> & dirtyNotesByGuid() const
    {
        return m_dirtyNotesByGuid;
    }

    const QHash<QString, QString> & notebookGuidByNoteGuid() const
    {
        return m_notebookGuidByNoteGuid;
    }

Q_SIGNALS:
    void filled();
    void failure(ErrorString errorDescription);

    // private signals
    void listNotes(
        LocalStorageManager::ListObjectsOptions flag,
        LocalStorageManager::GetNoteOptions options, size_t limit,
        size_t offset, LocalStorageManager::ListNotesOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QString linkedNotebookGuid, QUuid requestId);

public Q_SLOTS:
    /**
     * Start collecting the information about notes; does nothing if the
     * information is already collected or is being collected at the moment,
     * otherwise initiates the sequence of actions required to collect the note
     * information
     */
    void fill();

private Q_SLOTS:
    void onListNotesComplete(
        LocalStorageManager::ListObjectsOptions flag,
        LocalStorageManager::GetNoteOptions options, size_t limit,
        size_t offset, LocalStorageManager::ListNotesOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QString linkedNotebookGuid, QList<Note> foundNotes, QUuid requestId);

    void onListNotesFailed(
        LocalStorageManager::ListObjectsOptions flag,
        LocalStorageManager::GetNoteOptions options, size_t limit,
        size_t offset, LocalStorageManager::ListNotesOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QString linkedNotebookGuid, ErrorString errorDescription,
        QUuid requestId);

    void onAddNoteComplete(Note note, QUuid requestId);

    void onUpdateNoteComplete(
        Note note, LocalStorageManager::UpdateNoteOptions options,
        QUuid requestId);

    void onExpungeNoteComplete(Note note, QUuid requestId);

private:
    void connectToLocalStorage();
    void disconnectFromLocalStorage();

    void requestNotesList();

    void removeNote(const QString & noteLocalUid);
    void processNote(const Note & note);

private:
    LocalStorageManagerAsync & m_localStorageManagerAsync;
    bool m_connectedToLocalStorage = false;

    QString m_linkedNotebookGuid;

    NoteGuidToLocalUidBimap m_noteGuidToLocalUidBimap;
    QHash<QString, Note> m_dirtyNotesByGuid;
    QHash<QString, QString> m_notebookGuidByNoteGuid;

    QUuid m_listNotesRequestId;
    size_t m_limit = 40;
    size_t m_offset = 0;
};

} // namespace quentier

#endif // LIB_QUENTIER_SYNCHRONIZATION_NOTE_SYNC_CACHE_H
