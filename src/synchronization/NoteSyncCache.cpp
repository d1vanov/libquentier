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

#include "NoteSyncCache.h"

#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/Compat.h>

#define __NSLOG_BASE(message, level)                                           \
    if (m_linkedNotebookGuid.isEmpty()) {                                      \
        __QNLOG_BASE("synchronization:note_cache", message, level);            \
    }                                                                          \
    else {                                                                     \
        __QNLOG_BASE(                                                          \
            "synchronization:note_cache",                                      \
            "[linked notebook " << m_linkedNotebookGuid << "]: " << message,   \
            level);                                                            \
    }

#define NSTRACE(message) __NSLOG_BASE(message, Trace)

#define NSDEBUG(message) __NSLOG_BASE(message, Debug)

#define NSWARNING(message) __NSLOG_BASE(message, Warning)

namespace quentier {

NoteSyncCache::NoteSyncCache(
    LocalStorageManagerAsync & localStorageManagerAsync,
    const QString & linkedNotebookGuid, QObject * parent) :
    QObject(parent),
    m_localStorageManagerAsync(localStorageManagerAsync),
    m_linkedNotebookGuid(linkedNotebookGuid)
{}

void NoteSyncCache::clear()
{
    NSDEBUG("NoteSyncCache::clear");

    disconnectFromLocalStorage();

    m_noteGuidToLocalUidBimap.clear();
    m_dirtyNotesByGuid.clear();
    m_notebookGuidByNoteGuid.clear();
    m_listNotesRequestId = QUuid();
    m_offset = 0;
}

bool NoteSyncCache::isFilled() const
{
    if (!m_connectedToLocalStorage) {
        return false;
    }

    if (m_listNotesRequestId.isNull()) {
        return true;
    }

    return false;
}

void NoteSyncCache::fill()
{
    NSDEBUG("NoteSyncCache::fill");

    if (m_connectedToLocalStorage) {
        NSDEBUG(
            "Already connected to the local storage, no need "
            "to do anything");
        return;
    }

    connectToLocalStorage();
    requestNotesList();
}

void NoteSyncCache::onListNotesComplete(
    LocalStorageManager::ListObjectsOptions flag,
    LocalStorageManager::GetNoteOptions options, size_t limit, size_t offset,
    LocalStorageManager::ListNotesOrder order,
    LocalStorageManager::OrderDirection orderDirection,
    QString linkedNotebookGuid, QList<Note> foundNotes, QUuid requestId)
{
    if (requestId != m_listNotesRequestId) {
        return;
    }

    NSDEBUG(
        "NoteSyncCache::onListNotesComplete: flag = "
        << flag << ", with resource metadata = "
        << ((options & LocalStorageManager::GetNoteOption::WithResourceMetadata)
                ? "true"
                : "false")
        << ", with resource binary data = "
        << ((options &
             LocalStorageManager::GetNoteOption::WithResourceBinaryData)
                ? "true"
                : "false")
        << ", limit = " << limit << ", offset = " << offset
        << ", order = " << order << ", order direction = " << orderDirection
        << ", linked notebook guid = " << linkedNotebookGuid
        << ", num found notes = " << foundNotes.size()
        << ", request id = " << requestId);

    for (const auto & note: qAsConst(foundNotes)) {
        processNote(note);
    }

    m_listNotesRequestId = QUuid();

    if (foundNotes.size() == static_cast<int>(limit)) {
        NSTRACE(
            "The number of found notes matches the limit, "
            << "requesting more notes from the local storage");
        m_offset += limit;
        requestNotesList();
        return;
    }

    Q_EMIT filled();
}

void NoteSyncCache::onListNotesFailed(
    LocalStorageManager::ListObjectsOptions flag,
    LocalStorageManager::GetNoteOptions options, size_t limit, size_t offset,
    LocalStorageManager::ListNotesOrder order,
    LocalStorageManager::OrderDirection orderDirection,
    QString linkedNotebookGuid, ErrorString errorDescription, QUuid requestId)
{
    if (requestId != m_listNotesRequestId) {
        return;
    }

    NSDEBUG(
        "NoteSyncCache::onListNotesFailed: flag = "
        << flag << ", with resource metadata = "
        << ((options & LocalStorageManager::GetNoteOption::WithResourceMetadata)
                ? "true"
                : "false")
        << ", with resource binary data = "
        << ((options &
             LocalStorageManager::GetNoteOption::WithResourceBinaryData)
                ? "true"
                : "false")
        << ", limit = " << limit << ", offset = " << offset
        << ", order = " << order << ", order direction = " << orderDirection
        << ", linked notebook guid = " << linkedNotebookGuid
        << ", error description = " << errorDescription
        << ", request id = " << requestId);

    NSWARNING(
        "Failed to cache the note information required for the sync: "
        << errorDescription);

    m_noteGuidToLocalUidBimap.clear();
    m_dirtyNotesByGuid.clear();
    m_notebookGuidByNoteGuid.clear();
    disconnectFromLocalStorage();

    Q_EMIT failure(errorDescription);
}

void NoteSyncCache::onAddNoteComplete(Note note, QUuid requestId)
{
    NSDEBUG(
        "NoteSyncCache::onAddNoteComplete: request id = "
        << requestId << ", note: " << note);

    processNote(note);
}

void NoteSyncCache::onUpdateNoteComplete(
    Note note, LocalStorageManager::UpdateNoteOptions options, QUuid requestId)
{
    NSDEBUG(
        "NoteSyncCache::onUpdateNoteComplete: request id = "
        << requestId << ", update resource metadata = "
        << ((options &
             LocalStorageManager::UpdateNoteOption::UpdateResourceMetadata)
                ? "true"
                : "false")
        << ", update resource binary data = "
        << ((options &
             LocalStorageManager::UpdateNoteOption::UpdateResourceBinaryData)
                ? "true"
                : "false")
        << ", update tags = "
        << ((options & LocalStorageManager::UpdateNoteOption::UpdateTags)
                ? "true"
                : "false")
        << ", note: " << note);

    processNote(note);
}

void NoteSyncCache::onExpungeNoteComplete(Note note, QUuid requestId)
{
    NSDEBUG(
        "NoteSyncCache::onExpungeNoteComplete: request id = "
        << requestId << ", note: " << note);

    removeNote(note.localUid());
}

void NoteSyncCache::connectToLocalStorage()
{
    NSDEBUG("NoteSyncCache::connectToLocalStorage");

    if (m_connectedToLocalStorage) {
        NSDEBUG("Already connected to the local storage");
        return;
    }

    // Connect local signals to local storage manager async's slots
    QObject::connect(
        this, &NoteSyncCache::listNotes, &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::onListNotesRequest,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    // Connect local storage manager async's signals to local slots
    QObject::connect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::listNotesComplete, this,
        &NoteSyncCache::onListNotesComplete,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &m_localStorageManagerAsync, &LocalStorageManagerAsync::listNotesFailed,
        this, &NoteSyncCache::onListNotesFailed,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &m_localStorageManagerAsync, &LocalStorageManagerAsync::addNoteComplete,
        this, &NoteSyncCache::onAddNoteComplete,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::updateNoteComplete, this,
        &NoteSyncCache::onUpdateNoteComplete,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::expungeNoteComplete, this,
        &NoteSyncCache::onExpungeNoteComplete,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    m_connectedToLocalStorage = true;
}

void NoteSyncCache::disconnectFromLocalStorage()
{
    NSDEBUG("NoteSyncCache::disconnectFromLocalStorage");

    if (!m_connectedToLocalStorage) {
        NSDEBUG("Not connected to local storage at the moment");
        return;
    }

    // Disconnect local signals from local storage manager async's slots
    QObject::disconnect(
        this, &NoteSyncCache::listNotes, &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::onListNotesRequest);

    // Disconnect local storage manager async's signals from local slots
    QObject::disconnect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::listNotesComplete, this,
        &NoteSyncCache::onListNotesComplete);

    QObject::disconnect(
        &m_localStorageManagerAsync, &LocalStorageManagerAsync::listNotesFailed,
        this, &NoteSyncCache::onListNotesFailed);

    QObject::disconnect(
        &m_localStorageManagerAsync, &LocalStorageManagerAsync::addNoteComplete,
        this, &NoteSyncCache::onAddNoteComplete);

    QObject::disconnect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::updateNoteComplete, this,
        &NoteSyncCache::onUpdateNoteComplete);

    QObject::disconnect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::expungeNoteComplete, this,
        &NoteSyncCache::onExpungeNoteComplete);

    m_connectedToLocalStorage = false;
}

void NoteSyncCache::requestNotesList()
{
    NSDEBUG("NoteSyncCache::requestNotesList");

    m_listNotesRequestId = QUuid::createUuid();

    NSTRACE(
        "Emitting the request to list notes: request id = "
        << m_listNotesRequestId << ", offset = " << m_offset);

    LocalStorageManager::GetNoteOptions options(
        LocalStorageManager::GetNoteOption::WithResourceMetadata);

    Q_EMIT listNotes(
        LocalStorageManager::ListObjectsOption::ListAll, options, m_limit,
        m_offset, LocalStorageManager::ListNotesOrder::NoOrder,
        LocalStorageManager::OrderDirection::Ascending,
        (m_linkedNotebookGuid.isEmpty() ? QLatin1String("")
                                        : m_linkedNotebookGuid),
        m_listNotesRequestId);
}

void NoteSyncCache::removeNote(const QString & noteLocalUid)
{
    NSDEBUG("NoteSyncCache::removeNote: " << noteLocalUid);

    auto localUidIt = m_noteGuidToLocalUidBimap.right.find(noteLocalUid);
    if (localUidIt == m_noteGuidToLocalUidBimap.right.end()) {
        NSDEBUG("Found no cached note to remove");
        return;
    }

    QString guid = localUidIt->second;
    Q_UNUSED(m_noteGuidToLocalUidBimap.right.erase(localUidIt))

    auto dirtyNoteIt = m_dirtyNotesByGuid.find(guid);
    if (dirtyNoteIt != m_dirtyNotesByGuid.end()) {
        Q_UNUSED(m_dirtyNotesByGuid.erase(dirtyNoteIt))
    }

    auto notebookGuitIt = m_notebookGuidByNoteGuid.find(guid);
    if (notebookGuitIt != m_notebookGuidByNoteGuid.end()) {
        Q_UNUSED(m_notebookGuidByNoteGuid.erase(notebookGuitIt))
    }
}

void NoteSyncCache::processNote(const Note & note)
{
    NSDEBUG("NoteSyncCache::processNote: " << note);

    if (note.hasGuid()) {
        Q_UNUSED(m_noteGuidToLocalUidBimap.insert(
            NoteGuidToLocalUidBimap::value_type(note.guid(), note.localUid())))
    }
    else {
        auto localUidIt = m_noteGuidToLocalUidBimap.right.find(note.localUid());
        if (localUidIt != m_noteGuidToLocalUidBimap.right.end()) {
            Q_UNUSED(m_noteGuidToLocalUidBimap.right.erase(localUidIt))
        }
    }

    if (note.hasGuid()) {
        if (note.isDirty()) {
            m_dirtyNotesByGuid[note.guid()] = note;
        }
        else {
            auto it = m_dirtyNotesByGuid.find(note.guid());
            if (it != m_dirtyNotesByGuid.end()) {
                Q_UNUSED(m_dirtyNotesByGuid.erase(it))
            }
        }

        if (note.hasNotebookGuid()) {
            m_notebookGuidByNoteGuid[note.guid()] = note.notebookGuid();
        }
        else {
            auto it = m_notebookGuidByNoteGuid.find(note.guid());
            if (it != m_notebookGuidByNoteGuid.end()) {
                Q_UNUSED(m_notebookGuidByNoteGuid.erase(it))
            }
        }
    }
}

} // namespace quentier
