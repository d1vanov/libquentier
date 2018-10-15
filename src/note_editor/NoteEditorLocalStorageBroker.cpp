/*
 * Copyright 2018 Dmitry Ivanov
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

#include "NoteEditorLocalStorageBroker.h"
#include "../synchronization/SynchronizationShared.h"
#include <quentier/logging/QuentierLogger.h>

namespace quentier {

NoteEditorLocalStorageBroker::NoteEditorLocalStorageBroker(LocalStorageManagerAsync & localStorageManager,
                                                           QObject * parent) :
    QObject(parent),
    m_originalNoteResourceLocalUidsByNoteLocalUid(),
    m_findNoteRequestIds(),
    m_findNotebookRequestIds(),
    m_notesPendingNotebookFindingByNotebookLocalUid(),
    m_notesPendingNotebookFindingByNotebookGuid(),
    m_noteLocalUidsByAddResourceRequestIds(),
    m_noteLocalUidsByUpdateResourceRequestIds(),
    m_noteLocalUidsByExpungeResourceRequestIds(),
    m_notebooksCache(5),
    m_notesCache(5),
    m_saveNoteInfoByNoteLocalUids(),
    m_updateNoteRequestIds()
{
    createConnections(localStorageManager);
}

void NoteEditorLocalStorageBroker::saveNoteToLocalStorage(const Note & note)
{
    QNDEBUG(QStringLiteral("NoteEditorLocalStorageBroker::saveNoteToLocalStorage: note local uid = ") << note.localUid());

    const QSet<QString> * pOriginalNoteResourceLocalUids = Q_NULLPTR;

    auto it = m_originalNoteResourceLocalUidsByNoteLocalUid.find(note.localUid());
    if (Q_UNLIKELY(it != m_originalNoteResourceLocalUidsByNoteLocalUid.end())) {
        pOriginalNoteResourceLocalUids = &(it.value());
    }
    else {
        QNWARNING(QStringLiteral("Found no original note's resources for note with local uid ") << note.localUid()
                  << QStringLiteral(", assuming all resources with binary data within the note being saved are new"));
    }

    QList<Resource> newResources;
    QList<Resource> updatedResources;

    QList<Resource> resources = note.resources();
    for(auto it = resources.constBegin(), end = resources.constEnd(); it != end; ++it)
    {
        const Resource & resource = *it;
        if (resource.hasDataBody())
        {
            if (pOriginalNoteResourceLocalUids)
            {
                auto origIt = pOriginalNoteResourceLocalUids->find(resource.localUid());
                if (origIt == pOriginalNoteResourceLocalUids->constEnd()) {
                    newResources << resource;
                }
                else {
                    updatedResources << resource;
                }
            }
            else
            {
                newResources << resource;
            }
        }
    }

    QStringList expungedResourcesLocalUids;
    if (pOriginalNoteResourceLocalUids)
    {
        for(auto it = pOriginalNoteResourceLocalUids->constBegin(),
            end = pOriginalNoteResourceLocalUids->constEnd(); it != end; ++it)
        {
            const QString & originalResourceLocalUid = *it;

            bool foundResource = false;
            for(auto rit = resources.constBegin(), rend = resources.constEnd(); rit != rend; ++rit)
            {
                const Resource & resource = *rit;
                if (originalResourceLocalUid == resource.localUid()) {
                    foundResource = true;
                    break;
                }
            }

            if (!foundResource) {
                expungedResourcesLocalUids << originalResourceLocalUid;
            }
        }
    }

    QString noteLocalUid = note.localUid();

    int numAddResourceRequests = newResources.size();
    for(auto it = newResources.constBegin(), end = newResources.constEnd(); it != end; ++it)
    {
        const Resource & resource = *it;

        QUuid requestId = QUuid::createUuid();
        m_noteLocalUidsByAddResourceRequestIds[requestId] = noteLocalUid;
        QNDEBUG(QStringLiteral("Emitting the request to add resource to the local storage: request id = ")
                << requestId << QStringLiteral(", resource: ") << resource);
        Q_EMIT addResource(resource, requestId);
    }

    int numUpdateResourceRequests = updatedResources.size();
    for(auto it = updatedResources.constBegin(), end = updatedResources.constEnd(); it != end; ++it)
    {
        const Resource & resource = *it;

        QUuid requestId = QUuid::createUuid();
        m_noteLocalUidsByUpdateResourceRequestIds[requestId] = noteLocalUid;
        QNDEBUG(QStringLiteral("Emitting the request to update resource in the local storage: request id = ")
                << requestId << QStringLiteral(", resource: ") << resource);
        Q_EMIT updateResource(resource, requestId);
    }

    int numExpungeResourceRequests = expungedResourcesLocalUids.size();
    for(auto it = expungedResourcesLocalUids.constBegin(), end = expungedResourcesLocalUids.constEnd(); it != end; ++it)
    {
        Resource dummyResource;
        dummyResource.setLocalUid(*it);

        QUuid requestId = QUuid::createUuid();
        m_noteLocalUidsByExpungeResourceRequestIds[requestId] = noteLocalUid;
        QNDEBUG(QStringLiteral("Emitting the request to expunge resource from the local storage: request id = ")
                << requestId << QStringLiteral(", resource local uid = ") << *it);
        Q_EMIT expungeResource(dummyResource, requestId);
    }

    if ((numAddResourceRequests > 0) || (numUpdateResourceRequests > 0) || (numExpungeResourceRequests > 0))
    {
        SaveNoteInfo info;
        info.m_notePendingSaving = note;
        info.m_pendingAddResourceRequests = static_cast<quint32>(std::max(numAddResourceRequests, 0));
        info.m_pendingUpdateResourceRequests = static_cast<quint32>(std::max(numUpdateResourceRequests, 0));
        info.m_pendingExpungeResourceRequests = static_cast<quint32>(std::max(numExpungeResourceRequests, 0));
        m_saveNoteInfoByNoteLocalUids[noteLocalUid] = info;
        QNTRACE(QStringLiteral("Pending note saving: ") << info);
        return;
    }

    // Remove the note from the cache for the time being - during the attempt to
    // update its state within the local storage its state is not really quite
    // consistent
    Q_UNUSED(m_notesCache.remove(note.localUid()))

    LocalStorageManager::UpdateNoteOptions options(LocalStorageManager::UpdateNoteOption::UpdateResourceMetadata);
    QUuid requestId = QUuid::createUuid();
    Q_UNUSED(m_updateNoteRequestIds.insert(requestId))
    QNDEBUG(QStringLiteral("Emitting the request to update note in local storage: request id = ") << requestId
            << QStringLiteral(", note: ") << note);
    Q_EMIT updateNote(note, options, requestId);
}

void NoteEditorLocalStorageBroker::findNoteAndNotebook(const QString & noteLocalUid)
{
    QNDEBUG(QStringLiteral("NoteEditorLocalStorageBroker::findNoteAndNotebook: note local uid = ") << noteLocalUid);

    const Note * pCachedNote = m_notesCache.get(noteLocalUid);
    if (!pCachedNote) {
        QNDEBUG(QStringLiteral("Note was not found within the cache, looking it up in local storage"));
        emitFindNoteRequest(noteLocalUid);
        return;
    }

    if (Q_UNLIKELY(!pCachedNote->hasNotebookLocalUid() && !pCachedNote->hasNotebookGuid())) {
        Q_UNUSED(m_notesCache.remove(noteLocalUid))
        QNDEBUG(QStringLiteral("The note within the cache contained neither notebook local uid nor notebook guid, looking it up in local storage"));
        emitFindNoteRequest(noteLocalUid);
        return;
    }

    if (pCachedNote->hasNotebookLocalUid())
    {
        const QString & notebookLocalUid = pCachedNote->notebookLocalUid();

        const Notebook * pCachedNotebook = m_notebooksCache.get(notebookLocalUid);
        if (pCachedNotebook) {
            QNDEBUG(QStringLiteral("Found both note and notebook within caches"));
            Q_EMIT foundNoteAndNotebook(*pCachedNote, *pCachedNotebook);
        }
        else {
            QNDEBUG(QStringLiteral("Notebook was not found within the cache, looking it up in local storage"));
            emitFindNotebookRequest(notebookLocalUid, *pCachedNote);
        }

        return;
    }

    QUuid requestId = QUuid::createUuid();
    Q_UNUSED(m_findNotebookRequestIds.insert(requestId))
    NotesHash & notes = m_notesPendingNotebookFindingByNotebookGuid[pCachedNote->notebookGuid()];
    notes[pCachedNote->localUid()] = *pCachedNote;
    Notebook notebook;
    notebook.setGuid(pCachedNote->notebookGuid());
    QNDEBUG(QStringLiteral("Emitting the request to find notebook: request id = ") << requestId
            << QStringLiteral(", notebook guid = ") << pCachedNote->notebookGuid());
    Q_EMIT findNotebook(notebook, requestId);
}

void NoteEditorLocalStorageBroker::onUpdateNoteComplete(Note note, LocalStorageManager::UpdateNoteOptions options,
                                                        QUuid requestId)
{
    // TODO: implement
    // 1) Figure out if the note was updated in response to explicit request
    //    from NoteEditorLocalStorageBroker, if so, process it properly
    // 2) Otherwise figure out if the updated note is the one asked to be found, if no, return
    // 2) Emit noteUpdated signal

    QNDEBUG(QStringLiteral("NoteEditorLocalStorageBroker::onUpdateNoteComplete: request id = ")
            << requestId << QStringLiteral(", options = ") << options
            << QStringLiteral(", note: ") << note);

    if (m_notesCache.exists(note.localUid()))
    {
        if (!note.hasResources())
        {
            m_notesCache.put(note.localUid(), note);
        }
        else
        {
            QList<Resource> resources = note.resources();
            for(auto it = resources.begin(), end = resources.end(); it != end; ++it) {
                Resource & resource = *it;
                resource.setDataBody(QByteArray());
            }
            Note noteWithoutResourceDataBodies = note;
            noteWithoutResourceDataBodies.setResources(resources);
            m_notesCache.put(note.localUid(), noteWithoutResourceDataBodies);
        }
    }

    auto it = m_updateNoteRequestIds.find(requestId);
    if (it != m_updateNoteRequestIds.end()) {
        QNDEBUG(QStringLiteral("Note was successfully saved within the local storage"));
        m_updateNoteRequestIds.erase(it);
        Q_EMIT noteSavedToLocalStorage(note.localUid());
        return;
    }

    Q_EMIT noteUpdated(note);
}

void NoteEditorLocalStorageBroker::onUpdateNoteFailed(Note note, LocalStorageManager::UpdateNoteOptions options,
                                                      ErrorString errorDescription, QUuid requestId)
{
    auto it = m_updateNoteRequestIds.find(requestId);
    if (it == m_updateNoteRequestIds.end()) {
        return;
    }

    QNWARNING(QStringLiteral("Failed to update the note within the local storage: ") << errorDescription
              << QStringLiteral(", note: ") << note << QStringLiteral("\nUpdate options: ") << options
              << QStringLiteral(", request id = ") << requestId);

    m_updateNoteRequestIds.erase(it);
    Q_EMIT failedToSaveNoteToLocalStorage(note.localUid(), errorDescription);
}

void NoteEditorLocalStorageBroker::onUpdateNotebookComplete(Notebook notebook, QUuid requestId)
{
    Q_UNUSED(requestId)
    QString notebookLocalUid = notebook.localUid();
    if (m_notebooksCache.exists(notebookLocalUid)) {
        m_notebooksCache.put(notebookLocalUid, notebook);
    }
    Q_EMIT notebookUpdated(notebook);
}

void NoteEditorLocalStorageBroker::onFindNoteComplete(Note foundNote, bool withResourceMetadata,
                                                      bool withResourceBinaryData, QUuid requestId)
{
    auto it = m_findNoteRequestIds.find(requestId);
    if (it == m_findNoteRequestIds.end()) {
        return;
    }

    QNDEBUG(QStringLiteral("NoteEditorLocalStorageBroker::onFindNoteComplete: request id = ")
            << requestId << QStringLiteral(", with resource metadata = ")
            << (withResourceMetadata ? QStringLiteral("true") : QStringLiteral("false"))
            << QStringLiteral(", with resource binary data = ")
            << (withResourceBinaryData ? QStringLiteral("true") : QStringLiteral("false")));

    m_findNoteRequestIds.erase(it);

    if (Q_UNLIKELY(!foundNote.hasNotebookLocalUid() && !foundNote.hasNotebookGuid())) {
        ErrorString errorDescription(QT_TR_NOOP("note doesn't belong to any notebook"));
        APPEND_NOTE_DETAILS(errorDescription, foundNote)
        QNWARNING(errorDescription << QStringLiteral(", note: ") << foundNote);
        Q_EMIT failedToFindNoteOrNotebook(foundNote.localUid(), errorDescription);
        return;
    }

    m_notesCache.put(foundNote.localUid(), foundNote);

    if (foundNote.hasNotebookLocalUid())
    {
        const QString & notebookLocalUid = foundNote.notebookLocalUid();

        const Notebook * pCachedNotebook = m_notebooksCache.get(notebookLocalUid);
        if (pCachedNotebook) {
            QNDEBUG(QStringLiteral("Found notebook within the cache"));
            Q_EMIT foundNoteAndNotebook(foundNote, *pCachedNotebook);
        }
        else {
            QNDEBUG(QStringLiteral("Notebook was not found within the cache, looking it up in local storage"));
            emitFindNotebookRequest(notebookLocalUid, foundNote);
        }

        return;
    }

    QUuid findNotebookRequestId = QUuid::createUuid();
    Q_UNUSED(m_findNotebookRequestIds.insert(findNotebookRequestId))
    Notebook notebook;
    notebook.setGuid(foundNote.notebookGuid());
    QNDEBUG(QStringLiteral("Emitting the request to find notebook: request id = ") << findNotebookRequestId
            << QStringLiteral(", notebook guid = ") << foundNote.notebookGuid());
    Q_EMIT findNotebook(notebook, findNotebookRequestId);
}

void NoteEditorLocalStorageBroker::onFindNoteFailed(Note note, bool withResourceMetadata, bool withResourceBinaryData,
                                                    ErrorString errorDescription, QUuid requestId)
{
    auto it = m_findNoteRequestIds.find(requestId);
    if (it == m_findNoteRequestIds.end()) {
        return;
    }

    QNWARNING(QStringLiteral("NoteEditorLocalStorageBroker::onFindNoteFailed: request id = ") << requestId
              << QStringLiteral(", with resource metadata = ")
              << (withResourceMetadata ? QStringLiteral("true") : QStringLiteral("false"))
              << QStringLiteral(", with resource binary data = ")
              << (withResourceBinaryData ? QStringLiteral("true") : QStringLiteral("false"))
              << QStringLiteral(", error description: ") << errorDescription
              << QStringLiteral(", note: ") << note);

    m_findNoteRequestIds.erase(it);
    Q_EMIT failedToFindNoteOrNotebook(note.localUid(), errorDescription);
}

void NoteEditorLocalStorageBroker::onFindNotebookComplete(Notebook foundNotebook, QUuid requestId)
{
    auto it = m_findNotebookRequestIds.find(requestId);
    if (it == m_findNotebookRequestIds.end()) {
        return;
    }

    QNDEBUG(QStringLiteral("NoteEditorLocalStorageBroker::onFindNotebookComplete: request id = ")
            << requestId << QStringLiteral(", notebook: ") << foundNotebook);

    m_findNotebookRequestIds.erase(it);
    QString notebookLocalUid = foundNotebook.localUid();
    m_notebooksCache.put(notebookLocalUid, foundNotebook);

    bool foundNotesPendingNotebookFinding = true;
    bool foundByNotebookGuid = false;
    auto pendingNotesIt = m_notesPendingNotebookFindingByNotebookLocalUid.find(notebookLocalUid);
    if (pendingNotesIt == m_notesPendingNotebookFindingByNotebookLocalUid.end())
    {
        // Maybe this notebook was searched by guid
        if (foundNotebook.hasGuid())
        {
            pendingNotesIt = m_notesPendingNotebookFindingByNotebookGuid.find(foundNotebook.guid());
            if (pendingNotesIt == m_notesPendingNotebookFindingByNotebookGuid.end()) {
                foundNotesPendingNotebookFinding = false;
            }
            else {
                foundByNotebookGuid = true;
            }
        }
        else
        {
            foundNotesPendingNotebookFinding = false;
        }
    }

    if (!foundNotesPendingNotebookFinding) {
        QNWARNING(QStringLiteral("Found notebook but unable to detect which notes required its finding: notebook = ")
                  << foundNotebook);
        return;
    }

    const NotesHash & notes = pendingNotesIt.value();
    for(auto noteIt = notes.constBegin(), noteEnd = notes.constEnd(); noteIt != noteEnd; ++noteIt) {
        Q_EMIT foundNoteAndNotebook(noteIt.value(), foundNotebook);
    }

    if (foundByNotebookGuid) {
        m_notesPendingNotebookFindingByNotebookGuid.erase(pendingNotesIt);
    }
    else {
        m_notesPendingNotebookFindingByNotebookLocalUid.erase(pendingNotesIt);
    }
}

void NoteEditorLocalStorageBroker::onFindNotebookFailed(Notebook notebook, ErrorString errorDescription, QUuid requestId)
{
    // TODO: implement
    Q_UNUSED(notebook)
    Q_UNUSED(errorDescription)
    Q_UNUSED(requestId)
}

void NoteEditorLocalStorageBroker::onAddResourceComplete(Resource resource, QUuid requestId)
{
    // TODO: implement
    Q_UNUSED(resource)
    Q_UNUSED(requestId)
}

void NoteEditorLocalStorageBroker::onAddResourceFailed(Resource resource, ErrorString errorDescription, QUuid requestId)
{
    // TODO: implement
    Q_UNUSED(resource)
    Q_UNUSED(errorDescription)
    Q_UNUSED(requestId)
}

void NoteEditorLocalStorageBroker::onUpdateResourceComplete(Resource resource, QUuid requestId)
{
    // TODO: implement
    Q_UNUSED(resource)
    Q_UNUSED(requestId)
}

void NoteEditorLocalStorageBroker::onUpdateResourceFailed(Resource resource, ErrorString errorDescription, QUuid requestId)
{
    // TODO: implement
    Q_UNUSED(resource)
    Q_UNUSED(errorDescription)
    Q_UNUSED(requestId)
}

void NoteEditorLocalStorageBroker::onExpungeResourceComplete(Resource resource, QUuid requestId)
{
    // TODO: implement
    Q_UNUSED(resource)
    Q_UNUSED(requestId)
}

void NoteEditorLocalStorageBroker::onExpungeResourceFailed(Resource resource, ErrorString errorDescription, QUuid requestId)
{
    // TODO: implement
    Q_UNUSED(resource)
    Q_UNUSED(errorDescription)
    Q_UNUSED(requestId)
}

void NoteEditorLocalStorageBroker::onExpungeNoteComplete(Note note, QUuid requestId)
{
    Q_UNUSED(requestId)
    Q_EMIT noteDeleted(note.localUid());
}

void NoteEditorLocalStorageBroker::onExpungeNotebookComplete(Notebook notebook, QUuid requestId)
{
    Q_UNUSED(requestId)
    QString notebookLocalUid = notebook.localUid();
    Q_UNUSED(m_notebooksCache.remove(notebookLocalUid))
    Q_EMIT notebookDeleted(notebookLocalUid);
}

void NoteEditorLocalStorageBroker::createConnections(LocalStorageManagerAsync & localStorageManager)
{
    QNDEBUG(QStringLiteral("NoteEditorLocalStorageBroker::createConnections"));

    // Local signals to LocalStorageManagerAsync's slots
    QObject::connect(this, QNSIGNAL(NoteEditorLocalStorageBroker,updateNote,Note,LocalStorageManager::UpdateNoteOptions,QUuid),
                     &localStorageManager, QNSLOT(LocalStorageManagerAsync,onUpdateNoteRequest,Note,LocalStorageManager::UpdateNoteOptions,QUuid));
    QObject::connect(this, QNSIGNAL(NoteEditorLocalStorageBroker,addResource,Resource,QUuid),
                     &localStorageManager, QNSLOT(LocalStorageManagerAsync,onAddResourceRequest,Resource,QUuid));
    QObject::connect(this, QNSIGNAL(NoteEditorLocalStorageBroker,updateResource,Resource,QUuid),
                     &localStorageManager, QNSLOT(LocalStorageManagerAsync,onUpdateResourceRequest,Resource,QUuid));
    QObject::connect(this, QNSIGNAL(NoteEditorLocalStorageBroker,expungeResource,QUuid),
                     &localStorageManager, QNSLOT(LocalStorageManagerAsync,onExpungeResourceRequest,Resource,QUuid));
    QObject::connect(this, QNSIGNAL(NoteEditorLocalStorageBroker,findNote,Note,bool,bool,QUuid),
                     &localStorageManager, QNSLOT(LocalStorageManagerAsync,onFindNoteRequest,Note,bool,bool,QUuid));
    QObject::connect(this, QNSIGNAL(NoteEditorLocalStorageBroker,findNotebook,Notebook,QUuid),
                     &localStorageManager, QNSLOT(LocalStorageManagerAsync,onFindNotebookRequest,Notebook,QUuid));

    // LocalStorageManagerAsync's signals to local slots
    QObject::connect(&localStorageManager, QNSIGNAL(LocalStorageManagerAsync,updateNoteComplete,Note,LocalStorageManager::UpdateNoteOptions,QUuid),
                     this, QNSLOT(NoteEditorLocalStorageBroker,onUpdateNoteComplete,Note,LocalStorageManager::UpdateNoteOptions,QUuid));
    QObject::connect(&localStorageManager, QNSIGNAL(LocalStorageManagerAsync,updateNoteFailed,Note,LocalStorageManager::UpdateNoteOptions,ErrorString,QUuid),
                     this, QNSLOT(NoteEditorLocalStorageBroker,onUpdateNoteFailed,Note,LocalStorageManager::UpdateNoteOptions,ErrorString,QUuid));
    QObject::connect(&localStorageManager, QNSIGNAL(LocalStorageManagerAsync,updateNotebookComplete,Notebook,QUuid),
                     this, QNSLOT(NoteEditorLocalStorageBroker,onUpdateNotebookComplete,Notebook,QUuid));
    QObject::connect(&localStorageManager, QNSIGNAL(LocalStorageManagerAsync,addResourceComplete,Resource,QUuid),
                     this, QNSLOT(NoteEditorLocalStorageBroker,onAddResourceComplete,Resource,QUuid));
    QObject::connect(&localStorageManager, QNSIGNAL(LocalStorageManagerAsync,addResourceFailed,Resource,ErrorString,QUuid),
                     this, QNSLOT(NoteEditorLocalStorageBroker,onAddResourceFailed,Resource,ErrorString,QUuid));
    QObject::connect(&localStorageManager, QNSIGNAL(LocalStorageManagerAsync,updateResourceComplete,Resource,QUuid),
                     this, QNSLOT(NoteEditorLocalStorageBroker,onUpdateResourceComplete,Resource,QUuid));
    QObject::connect(&localStorageManager, QNSIGNAL(LocalStorageManagerAsync,updateResourceFailed,Resource,ErrorString,QUuid),
                     this, QNSLOT(NoteEditorLocalStorageBroker,onUpdateResourceFailed,Resource,ErrorString,QUuid));
    QObject::connect(&localStorageManager, QNSIGNAL(LocalStorageManagerAsync,expungeResourceComplete,Resource,QUuid),
                     this, QNSLOT(NoteEditorLocalStorageBroker,onExpungeResourceComplete,Resource,QUuid));
    QObject::connect(&localStorageManager, QNSIGNAL(LocalStorageManagerAsync,expungeResourceFailed,Resource,ErrorString,QUuid),
                     this, QNSLOT(NoteEditorLocalStorageBroker,onExpungeResourceFailed,Resource,ErrorString,QUuid));
    QObject::connect(&localStorageManager, QNSIGNAL(LocalStorageManagerAsync,findNoteComplete,Note,bool,bool,QUuid),
                     this, QNSLOT(NoteEditorLocalStorageBroker,onFindNoteComplete,Note,bool,bool,QUuid));
    QObject::connect(&localStorageManager, QNSIGNAL(LocalStorageManagerAsync,findNoteFailed,Note,bool,bool,ErrorString,QUuid),
                     this, QNSLOT(NoteEditorLocalStorageBroker,onFindNoteFailed,Note,bool,bool,ErrorString,QUuid));
    QObject::connect(&localStorageManager, QNSIGNAL(LocalStorageManagerAsync,findNotebookComplete,Notebook,QUuid),
                     this, QNSLOT(NoteEditorLocalStorageBroker,onFindNotebookComplete,Notebook,QUuid));
    QObject::connect(&localStorageManager, QNSIGNAL(LocalStorageManagerAsync,findNotebookFailed,Notebook,ErrorString,QUuid),
                     this, QNSLOT(NoteEditorLocalStorageBroker,onFindNotebookFailed,Notebook,ErrorString,QUuid));
    QObject::connect(&localStorageManager, QNSIGNAL(LocalStorageManagerAsync,expungeNoteComplete,Note,QUuid),
                     this, QNSLOT(NoteEditorLocalStorageBroker,onExpungeNoteComplete,Note,QUuid));
    QObject::connect(&localStorageManager, QNSIGNAL(LocalStorageManagerAsync,expungeNotebookComplete,Notebook,QUuid),
                     this, QNSLOT(NoteEditorLocalStorageBroker,onExpungeNotebookComplete,Notebook,QUuid));
}

void NoteEditorLocalStorageBroker::emitFindNoteRequest(const QString & noteLocalUid)
{
    QUuid requestId = QUuid::createUuid();
    Q_UNUSED(m_findNoteRequestIds.insert(requestId))
    Note note;
    note.setLocalUid(noteLocalUid);
    QNDEBUG(QStringLiteral("Emitting the request to find note: request id = ") << requestId
            << QStringLiteral(", note local uid = ") << noteLocalUid);
    Q_EMIT findNote(note, /* with resource metadata = */ true,
                    /* with resource binary data = */ false, requestId);
}

void NoteEditorLocalStorageBroker::emitFindNotebookRequest(const QString & notebookLocalUid, const Note & note)
{
    QUuid requestId = QUuid::createUuid();
    Q_UNUSED(m_findNotebookRequestIds.insert(requestId))
    NotesHash & notes = m_notesPendingNotebookFindingByNotebookLocalUid[notebookLocalUid];
    notes[note.localUid()] = note;
    Notebook notebook;
    notebook.setLocalUid(notebookLocalUid);
    QNDEBUG(QStringLiteral("Emitting the request to find notebook: request id = ") << requestId
            << QStringLiteral(", notebook local uid = ") << notebookLocalUid);
    Q_EMIT findNotebook(notebook, requestId);
}

QTextStream & NoteEditorLocalStorageBroker::SaveNoteInfo::print(QTextStream & strm) const
{
    strm << "SaveNoteInfo: \n"
         << "pending add resource requests: " << m_pendingAddResourceRequests << "\n"
         << ", pending update resource requests: " << m_pendingUpdateResourceRequests << "\n"
         << ", pending expunge resource requests: " << m_pendingExpungeResourceRequests << "\n"
         << ",  note: " << m_notePendingSaving
         << "\n";
    return strm;
}

} // namespace quentier
