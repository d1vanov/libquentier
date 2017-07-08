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
    m_noteGuidToLocalUidBimap(),
    m_dirtyNotesByGuid(),
    m_listNotesRequestId(),
    m_limit(40),
    m_offset(0)
{}

void NoteSyncCache::clear()
{
    QNDEBUG(QStringLiteral("NoteSyncCache::clear"));

    disconnectFromLocalStorage();

    m_noteGuidToLocalUidBimap.clear();
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
    QNDEBUG(QStringLiteral("NoteSyncCache::fill"));

    if (m_connectedToLocalStorage) {
        QNDEBUG(QStringLiteral("Already connected to the local storage, no need to do anything"));
        return;
    }

    connectToLocalStorage();
    requestNotesList();
}

void NoteSyncCache::onListNotesComplete(LocalStorageManager::ListObjectsOptions flag, bool withResourceBinaryData,
                                        size_t limit, size_t offset, LocalStorageManager::ListNotesOrder::type order,
                                        LocalStorageManager::OrderDirection::type orderDirection,
                                        QString linkedNotebookGuid, QList<Note> foundNotes, QUuid requestId)
{
    if (requestId != m_listNotesRequestId) {
        return;
    }

    QNDEBUG(QStringLiteral("NoteSyncCache::onListNotesComplete: flag = ") << flag << QStringLiteral(", with resource binary data = ")
            << (withResourceBinaryData ? QStringLiteral("true") : QStringLiteral("false"))
            << QStringLiteral(", limit = ") << limit << QStringLiteral(", offset = ") << offset
            << QStringLiteral(", order = ") << order << QStringLiteral(", order direction = ") << orderDirection
            << QStringLiteral(", linked notebook guid = ") << linkedNotebookGuid
            << QStringLiteral(", num found notes = ") << foundNotes.size() << QStringLiteral(", request id = ") << requestId);

    for(auto it = foundNotes.constBegin(), end = foundNotes.constEnd(); it != end; ++it) {
        processNote(*it);
    }

    m_listNotesRequestId = QUuid();

    if (foundNotes.size() == static_cast<int>(limit)) {
        QNTRACE(QStringLiteral("The number of found notes matches the limit, requesting more notes from the local storage"));
        m_offset += limit;
        requestNotesList();
        return;
    }

    emit filled();
}

void NoteSyncCache::onListNotesFailed(LocalStorageManager::ListObjectsOptions flag, bool withResourceBinaryData,
                                      size_t limit, size_t offset, LocalStorageManager::ListNotesOrder::type order,
                                      LocalStorageManager::OrderDirection::type orderDirection,
                                      QString linkedNotebookGuid, ErrorString errorDescription, QUuid requestId)
{
    if (requestId != m_listNotesRequestId) {
        return;
    }

    QNDEBUG(QStringLiteral("NoteSyncCache::onListNotesFailed: flag = ") << flag << QStringLiteral(", with resource binary data = ")
            << (withResourceBinaryData ? QStringLiteral("true") : QStringLiteral("false")) << QStringLiteral(", limit = ")
            << limit << QStringLiteral(", offset = ") << offset << QStringLiteral(", order = ") << order
            << QStringLiteral(", order direction = ") << orderDirection << QStringLiteral(", linked notebook guid = ")
            << linkedNotebookGuid << QStringLiteral(", error description = ") << errorDescription << QStringLiteral(", request id = ")
            << requestId);

    QNWARNING(QStringLiteral("Failed to cache the note information required for the sync: ") << errorDescription);

    m_noteGuidToLocalUidBimap.clear();
    m_dirtyNotesByGuid.clear();
    disconnectFromLocalStorage();

    emit failure(errorDescription);
}

void NoteSyncCache::onAddNoteComplete(Note note, QUuid requestId)
{
    QNDEBUG(QStringLiteral("NoteSyncCache::onAddNoteComplete: request id = ") << requestId
            << QStringLiteral(", note: ") << note);

    processNote(note);
}

void NoteSyncCache::onUpdateNoteComplete(Note note, bool updateResources, bool updateTags, QUuid requestId)
{
    QNDEBUG(QStringLiteral("NoteSyncCache::onUpdateNoteComplete: request id = ") << requestId
            << QStringLiteral(", update resources = ") << (updateResources ? QStringLiteral("true") : QStringLiteral("false"))
            << QStringLiteral(", update tags = ") << (updateTags ? QStringLiteral("true") : QStringLiteral("false"))
            << QStringLiteral(", note: ") << note);

    processNote(note);
}

void NoteSyncCache::onExpungeNoteComplete(Note note, QUuid requestId)
{
    QNDEBUG(QStringLiteral("NoteSyncCache::onExpungeNoteComplete: request id = ") << requestId
            << QStringLiteral(", note: ") << note);

    removeNote(note.localUid());
}

void NoteSyncCache::connectToLocalStorage()
{
    QNDEBUG(QStringLiteral("NoteSyncCache::connectToLocalStorage"));

    if (m_connectedToLocalStorage) {
        QNDEBUG(QStringLiteral("Already connected to the local storage"));
        return;
    }

    // Connect local signals to local storage manager async's slots
    QObject::connect(this, QNSIGNAL(NoteSyncCache,listNotes,LocalStorageManager::ListObjectsOptions,
                                    bool,size_t,size_t,LocalStorageManager::ListNotesOrder::type,
                                    LocalStorageManager::OrderDirection::type,QString,QUuid),
                     &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onListNotesRequest,
                                                         LocalStorageManager::ListObjectsOptions,bool,
                                                         size_t,size_t,LocalStorageManager::ListNotesOrder::type,
                                                         LocalStorageManager::OrderDirection::type,QString,QUuid));

    // Connect local storage manager async's signals to local slots
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,listNotesComplete,
                                                           LocalStorageManager::ListObjectsOptions,bool,
                                                           size_t,size_t,LocalStorageManager::ListNotesOrder::type,
                                                           LocalStorageManager::OrderDirection::type,
                                                           QString,QList<Note>,QUuid),
                     this, QNSLOT(NoteSyncCache,onListNotesComplete,LocalStorageManager::ListObjectsOptions,bool,
                                  size_t,size_t,LocalStorageManager::ListNotesOrder::type,
                                  LocalStorageManager::OrderDirection::type,QString,QList<Note>,QUuid));
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,listNotesFailed,
                                                           LocalStorageManager::ListObjectsOptions,bool,
                                                           size_t,size_t,LocalStorageManager::ListNotesOrder::type,
                                                           LocalStorageManager::OrderDirection::type,
                                                           QString,ErrorString,QUuid),
                     this, QNSLOT(NoteSyncCache,onListNotesFailed,LocalStorageManager::ListObjectsOptions,bool,
                                  size_t,size_t,LocalStorageManager::ListNotesOrder::type,
                                  LocalStorageManager::OrderDirection::type,QString,ErrorString,QUuid));
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addNoteComplete,Note,QUuid),
                     this, QNSLOT(NoteSyncCache,onAddNoteComplete,Note,QUuid));
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateNoteComplete,Note,bool,bool,QUuid),
                     this, QNSLOT(NoteSyncCache,onUpdateNoteComplete,Note,bool,bool,QUuid));
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeNoteComplete,Note,QUuid),
                     this, QNSLOT(NoteSyncCache,onExpungeNoteComplete,Note,QUuid));

    m_connectedToLocalStorage = true;
}

void NoteSyncCache::disconnectFromLocalStorage()
{
    QNDEBUG(QStringLiteral("NoteSyncCache::disconnectFromLocalStorage"));

    if (!m_connectedToLocalStorage) {
        QNDEBUG(QStringLiteral("Not connected to local storage at the moment"));
        return;
    }

    // Disconnect local signals from local storage manager async's slots
    QObject::disconnect(this, QNSIGNAL(NoteSyncCache,listNotes,LocalStorageManager::ListObjectsOptions,
                                       bool,size_t,size_t,LocalStorageManager::ListNotesOrder::type,
                                       LocalStorageManager::OrderDirection::type,QString,QUuid),
                        &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onListNotesRequest,
                                                            LocalStorageManager::ListObjectsOptions,bool,
                                                            size_t,size_t,LocalStorageManager::ListNotesOrder::type,
                                                            LocalStorageManager::OrderDirection::type,QString,QUuid));

    // Disconnect local storage manager async's signals from local slots
    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,listNotesComplete,
                                                              LocalStorageManager::ListObjectsOptions,bool,
                                                              size_t,size_t,LocalStorageManager::ListNotesOrder::type,
                                                              LocalStorageManager::OrderDirection::type,
                                                              QString,QList<Note>,QUuid),
                        this, QNSLOT(NoteSyncCache,onListNotesComplete,LocalStorageManager::ListObjectsOptions,bool,
                                     size_t,size_t,LocalStorageManager::ListNotesOrder::type,
                                     LocalStorageManager::OrderDirection::type,QString,QList<Note>,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,listNotesFailed,
                                                              LocalStorageManager::ListObjectsOptions,bool,
                                                              size_t,size_t,LocalStorageManager::ListNotesOrder::type,
                                                              LocalStorageManager::OrderDirection::type,
                                                              QString,ErrorString,QUuid),
                        this, QNSLOT(NoteSyncCache,onListNotesFailed,LocalStorageManager::ListObjectsOptions,bool,
                                     size_t,size_t,LocalStorageManager::ListNotesOrder::type,
                                     LocalStorageManager::OrderDirection::type,QString,ErrorString,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addNoteComplete,Note,QUuid),
                        this, QNSLOT(NoteSyncCache,onAddNoteComplete,Note,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateNoteComplete,Note,bool,bool,QUuid),
                        this, QNSLOT(NoteSyncCache,onUpdateNoteComplete,Note,bool,bool,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeNoteComplete,Note,QUuid),
                        this, QNSLOT(NoteSyncCache,onExpungeNoteComplete,Note,QUuid));

    m_connectedToLocalStorage = false;
}

void NoteSyncCache::requestNotesList()
{
    QNDEBUG(QStringLiteral("NoteSyncCache::requestNotesList"));

    m_listNotesRequestId = QUuid::createUuid();

    QNTRACE(QStringLiteral("Emitting the request to list notes: request id = ")
            << m_listNotesRequestId << QStringLiteral(", offset = ") << m_offset);
    emit listNotes(LocalStorageManager::ListAll, /* with resource binary data = */ false,
                   m_limit, m_offset, LocalStorageManager::ListNotesOrder::NoOrder,
                   LocalStorageManager::OrderDirection::Ascending, QString(), m_listNotesRequestId);
}

void NoteSyncCache::removeNote(const QString & noteLocalUid)
{
    QNDEBUG(QStringLiteral("NoteSyncCache::removeNote: ") << noteLocalUid);

    auto localUidIt = m_noteGuidToLocalUidBimap.right.find(noteLocalUid);
    if (localUidIt == m_noteGuidToLocalUidBimap.right.end()) {
        QNDEBUG(QStringLiteral("Found no cached note to remove"));
        return;
    }

    QString guid = localUidIt->second;
    Q_UNUSED(m_noteGuidToLocalUidBimap.right.erase(localUidIt))

    auto dirtyNoteIt = m_dirtyNotesByGuid.find(guid);
    if (dirtyNoteIt != m_dirtyNotesByGuid.end()) {
        Q_UNUSED(m_dirtyNotesByGuid.erase(dirtyNoteIt))
    }
}

void NoteSyncCache::processNote(const Note & note)
{
    QNDEBUG(QStringLiteral("NoteSyncCache::processNote: ") << note);

    if (note.hasGuid())
    {
        Q_UNUSED(m_noteGuidToLocalUidBimap.insert(NoteGuidToLocalUidBimap::value_type(note.guid(), note.localUid())))
    }
    else
    {
        auto localUidIt = m_noteGuidToLocalUidBimap.right.find(note.localUid());
        if (localUidIt != m_noteGuidToLocalUidBimap.right.end()) {
            Q_UNUSED(m_noteGuidToLocalUidBimap.right.erase(localUidIt))
        }
    }

    if (note.hasGuid())
    {
        if (note.isDirty())
        {
            m_dirtyNotesByGuid[note.guid()] = note;
        }
        else
        {
            auto it = m_dirtyNotesByGuid.find(note.guid());
            if (it != m_dirtyNotesByGuid.end()) {
                Q_UNUSED(m_dirtyNotesByGuid.erase(it))
            }
        }
    }
}

} // namespace quentier
