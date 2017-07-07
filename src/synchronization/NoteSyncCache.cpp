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

#include "NoteSyncCache.h"
#include <quentier/logging/QuentierLogger.h>

namespace quentier {

NoteSyncCache::NoteSyncCache(LocalStorageManagerAsync & localStorageManagerAsync, QObject * parent) :
    QObject(parent),
    m_localStorageManagerAsync(localStorageManagerAsync),
    m_connectedToLocalStorage(false),
    m_noteGuidsByLocalUid(),
    m_dirtyNotesByGuid(),
    m_listNotesRequestId(),
    m_limit(40),
    m_offset(0)
{}

void NoteSyncCache::clear()
{
    QNDEBUG(QStringLiteral("NoteSyncCache::clear"));

    disconnectFromLocalStorage();

    m_noteGuidsByLocalUid.clear();
    m_dirtyNotesByGuid.clear();
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
    // TODO: implement
}

void NoteSyncCache::onListNotesComplete(LocalStorageManager::ListObjectsOptions flag, bool withResourceBinaryData,
                                        size_t limit, size_t offset, LocalStorageManager::ListNotesOrder::type order,
                                        LocalStorageManager::OrderDirection::type orderDirection,
                                        QString linkedNotebookGuid, QList<Note> foundNotes, QUuid requestId)
{
    // TODO: implement
    Q_UNUSED(flag)
    Q_UNUSED(withResourceBinaryData)
    Q_UNUSED(limit)
    Q_UNUSED(offset)
    Q_UNUSED(order)
    Q_UNUSED(orderDirection)
    Q_UNUSED(linkedNotebookGuid)
    Q_UNUSED(foundNotes)
    Q_UNUSED(requestId)
}

void NoteSyncCache::onListNotesFailed(LocalStorageManager::ListObjectsOptions flag, bool withResourceBinaryData,
                                      size_t limit, size_t offset, LocalStorageManager::ListNotesOrder::type order,
                                      LocalStorageManager::OrderDirection::type orderDirection,
                                      QString linkedNotebookGuid, ErrorString errorDescription, QUuid requestId)
{
    // TODO: implement
    Q_UNUSED(flag)
    Q_UNUSED(withResourceBinaryData)
    Q_UNUSED(limit)
    Q_UNUSED(offset)
    Q_UNUSED(order)
    Q_UNUSED(orderDirection)
    Q_UNUSED(linkedNotebookGuid)
    Q_UNUSED(errorDescription)
    Q_UNUSED(requestId)
}

void NoteSyncCache::onAddNoteComplete(Note note, QUuid requestId)
{
    // TODO: implement
    Q_UNUSED(note)
    Q_UNUSED(requestId)
}

void NoteSyncCache::onUpdateNoteComplete(Note note, bool updateResources, bool updateTags, QUuid requestId)
{
    // TODO: implement
    Q_UNUSED(note)
    Q_UNUSED(updateResources)
    Q_UNUSED(updateTags)
    Q_UNUSED(requestId)
}

void NoteSyncCache::onExpungeNoteComplete(Note note, QUuid requestId)
{
    // TODO: implement
    Q_UNUSED(note)
    Q_UNUSED(requestId)
}

void NoteSyncCache::connectToLocalStorage()
{
    // TODO: implement
}

void NoteSyncCache::disconnectFromLocalStorage()
{
    // TODO: implement
}

void NoteSyncCache::requestNotesList()
{
    // TODO: implement
}

void NoteSyncCache::removeNote(const QString & noteLocalUid)
{
    // TODO: implement
    Q_UNUSED(noteLocalUid)
}

void NoteSyncCache::processNote(const Note & note)
{
    // TODO: implement
    Q_UNUSED(note)
}

} // namespace quentier
