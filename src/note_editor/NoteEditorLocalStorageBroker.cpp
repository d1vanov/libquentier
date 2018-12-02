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

// 10 Mb
#define MAX_TOTAL_RESOURCE_BINARY_DATA_SIZE_IN_BYTES (10485760)

namespace quentier {

NoteEditorLocalStorageBroker::NoteEditorLocalStorageBroker() :
    QObject(),
    m_pLocalStorageManagerAsync(Q_NULLPTR),
    m_originalNoteResourceLocalUidsByNoteLocalUid(),
    m_findNoteRequestIds(),
    m_findNotebookRequestIds(),
    m_findResourceRequestIds(),
    m_notesPendingNotebookFindingByNotebookLocalUid(),
    m_notesPendingNotebookFindingByNotebookGuid(),
    m_noteLocalUidsByAddResourceRequestIds(),
    m_noteLocalUidsByUpdateResourceRequestIds(),
    m_noteLocalUidsByExpungeResourceRequestIds(),
    m_notebooksCache(5),
    m_notesCache(5),
    m_resourcesCache(5),
    m_saveNoteInfoByNoteLocalUids(),
    m_updateNoteRequestIds()
{}

NoteEditorLocalStorageBroker & NoteEditorLocalStorageBroker::instance()
{
    static NoteEditorLocalStorageBroker noteEditorLocalStorageBroker;
    return noteEditorLocalStorageBroker;
}

void NoteEditorLocalStorageBroker::setLocalStorageManager(LocalStorageManagerAsync & localStorageManagerAsync)
{
    QNDEBUG(QStringLiteral("NoteEditorLocalStorageBroker::setLocalStorageManager"));

    if (m_pLocalStorageManagerAsync == &localStorageManagerAsync) {
        QNDEBUG(QStringLiteral("LocalStorageManagerAsync is already set"));
        return;
    }

    if (m_pLocalStorageManagerAsync) {
        disconnectFromLocalStorage(*m_pLocalStorageManagerAsync);
    }

    m_pLocalStorageManagerAsync = &localStorageManagerAsync;
    createConnections(localStorageManagerAsync);
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

    emitUpdateNoteRequest(note);
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

void NoteEditorLocalStorageBroker::findResourceData(const QString & resourceLocalUid)
{
    QNDEBUG(QStringLiteral("NoteEditorLocalStorageBroker::findResourceData: resource local uid = ") << resourceLocalUid);

    const Resource * pCachedResource = m_resourcesCache.get(resourceLocalUid);
    if (pCachedResource) {
        QNDEBUG(QStringLiteral("Found cached resource binary data"));
        Q_EMIT foundResourceData(*pCachedResource);
        return;
    }

    QUuid requestId = QUuid::createUuid();
    Q_UNUSED(m_findResourceRequestIds.insert(requestId))
    Resource resource;
    resource.setLocalUid(resourceLocalUid);
    QNDEBUG(QStringLiteral("Emitting the request to find resource: request id = ") << requestId
            << QStringLiteral(", resource local uid = ") << resource);
    Q_EMIT findResource(resource, /* with binary data = */ true, requestId);
}

void NoteEditorLocalStorageBroker::onUpdateNoteComplete(Note note, LocalStorageManager::UpdateNoteOptions options,
                                                        QUuid requestId)
{
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
                resource.setAlternateDataBody(QByteArray());
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
    for(auto noteIt = notes.constBegin(), noteEnd = notes.constEnd(); noteIt != noteEnd; ++noteIt)
    {
        QNTRACE(QStringLiteral("Found pending note, emitting foundNoteAndNotebook signal: note local uid = ")
                << noteIt.value().localUid());
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
    auto it = m_findNotebookRequestIds.find(requestId);
    if (it == m_findNotebookRequestIds.end()) {
        return;
    }

    QNWARNING(QStringLiteral("NoteEditorLocalStorageBroker::onFindNotebookFailed: request id = ") << requestId
              << QStringLiteral(", error description: ") << errorDescription
              << QStringLiteral(", notebook: ") << notebook);

    m_findNotebookRequestIds.erase(it);

    QString notebookLocalUid = notebook.localUid();
    bool foundNotesPendingNotebookFinding = true;
    bool foundByNotebookGuid = false;
    auto pendingNotesIt = m_notesPendingNotebookFindingByNotebookLocalUid.find(notebookLocalUid);
    if (pendingNotesIt == m_notesPendingNotebookFindingByNotebookLocalUid.end())
    {
        // Maybe this notebook was searched by guid
        if (notebook.hasGuid())
        {
            pendingNotesIt = m_notesPendingNotebookFindingByNotebookGuid.find(notebook.guid());
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
        QNDEBUG(QStringLiteral("Failed to find notebook and unable to determine for which notes it was required - nothing left to do"));
        return;
    }

    const NotesHash & notes = pendingNotesIt.value();
    for(auto noteIt = notes.constBegin(), noteEnd = notes.constEnd(); noteIt != noteEnd; ++noteIt) {
        Q_EMIT failedToFindNoteOrNotebook(noteIt.value().localUid(), errorDescription);
    }

    if (foundByNotebookGuid) {
        m_notesPendingNotebookFindingByNotebookGuid.erase(pendingNotesIt);
    }
    else {
        m_notesPendingNotebookFindingByNotebookLocalUid.erase(pendingNotesIt);
    }
}

void NoteEditorLocalStorageBroker::onAddResourceComplete(Resource resource, QUuid requestId)
{
    auto it = m_noteLocalUidsByAddResourceRequestIds.find(requestId);
    if (it == m_noteLocalUidsByAddResourceRequestIds.end()) {
        return;
    }

    QNDEBUG(QStringLiteral("NoteEditorLocalStorageBroker::onAddResourceComplete: request id = ") << requestId
            << QStringLiteral(", resource: ") << resource);

    QString noteLocalUid = it.value();
    m_noteLocalUidsByAddResourceRequestIds.erase(it);

    auto saveNoteInfoIt = m_saveNoteInfoByNoteLocalUids.find(noteLocalUid);
    if (Q_UNLIKELY(saveNoteInfoIt == m_saveNoteInfoByNoteLocalUids.end())) {
        QNWARNING(QStringLiteral("Unable to find note for which the resource was added to the local storage: resource = ")
                  << resource);
        return;
    }

    SaveNoteInfo & saveNoteInfo = saveNoteInfoIt.value();

    // Extra precaution against the case of miscounting and overflow
    if (saveNoteInfo.m_pendingAddResourceRequests > 0) {
        --saveNoteInfo.m_pendingAddResourceRequests;
    }

    if (saveNoteInfo.hasPendingResourceOperations()) {
        return;
    }

    emitUpdateNoteRequest(saveNoteInfo.m_notePendingSaving);
    m_saveNoteInfoByNoteLocalUids.erase(saveNoteInfoIt);
}

void NoteEditorLocalStorageBroker::onAddResourceFailed(Resource resource, ErrorString errorDescription, QUuid requestId)
{
    auto it = m_noteLocalUidsByAddResourceRequestIds.find(requestId);
    if (it == m_noteLocalUidsByAddResourceRequestIds.end()) {
        return;
    }

    QNWARNING(QStringLiteral("NoteEditorLocalStorageBroker::onAddResourceFailed: request id = ") << requestId
              << QStringLiteral(", error description: ") << errorDescription
              << QStringLiteral(", resource: ") << resource);

    QString noteLocalUid = it.value();
    m_noteLocalUidsByAddResourceRequestIds.erase(it);

    auto saveNoteInfoIt = m_saveNoteInfoByNoteLocalUids.find(noteLocalUid);
    if (saveNoteInfoIt != m_saveNoteInfoByNoteLocalUids.end()) {
        m_saveNoteInfoByNoteLocalUids.erase(saveNoteInfoIt);
    }

    Q_EMIT failedToSaveNoteToLocalStorage(noteLocalUid, errorDescription);
}

void NoteEditorLocalStorageBroker::onUpdateResourceComplete(Resource resource, QUuid requestId)
{
    if (m_resourcesCache.get(resource.localUid())) {
        m_resourcesCache.put(resource.localUid(), resource);
    }

    auto it = m_noteLocalUidsByUpdateResourceRequestIds.find(requestId);
    if (it == m_noteLocalUidsByUpdateResourceRequestIds.end()) {
        return;
    }

    QNDEBUG(QStringLiteral("NoteEditorLocalStorageBroker::onUpdateResourceComplete: request id = ") << requestId
            << QStringLiteral(", resource: ") << resource);

    QString noteLocalUid = it.value();
    m_noteLocalUidsByUpdateResourceRequestIds.erase(it);

    auto saveNoteInfoIt = m_saveNoteInfoByNoteLocalUids.find(noteLocalUid);
    if (Q_UNLIKELY(saveNoteInfoIt == m_saveNoteInfoByNoteLocalUids.end())) {
        QNWARNING(QStringLiteral("Unable to find note for which the resource was updated in the local storage: resource = ")
                  << resource);
        return;
    }

    SaveNoteInfo & saveNoteInfo = saveNoteInfoIt.value();

    // Extra precaution against the case of miscounting and overflow
    if (saveNoteInfo.m_pendingUpdateResourceRequests > 0) {
        --saveNoteInfo.m_pendingUpdateResourceRequests;
    }

    if (saveNoteInfo.hasPendingResourceOperations()) {
        return;
    }

    emitUpdateNoteRequest(saveNoteInfo.m_notePendingSaving);
    m_saveNoteInfoByNoteLocalUids.erase(saveNoteInfoIt);
}

void NoteEditorLocalStorageBroker::onUpdateResourceFailed(Resource resource, ErrorString errorDescription, QUuid requestId)
{
    auto it = m_noteLocalUidsByUpdateResourceRequestIds.find(requestId);
    if (it == m_noteLocalUidsByUpdateResourceRequestIds.end()) {
        return;
    }

    QNWARNING(QStringLiteral("NoteEditorLocalStorageBroker::onUpdateResourceFailed: request id = ") << requestId
              << QStringLiteral(", error description: ") << errorDescription
              << QStringLiteral(", resource: ") << resource);

    QString noteLocalUid = it.value();
    m_noteLocalUidsByUpdateResourceRequestIds.erase(it);

    auto saveNoteInfoIt = m_saveNoteInfoByNoteLocalUids.find(noteLocalUid);
    if (saveNoteInfoIt != m_saveNoteInfoByNoteLocalUids.end()) {
        m_saveNoteInfoByNoteLocalUids.erase(saveNoteInfoIt);
    }

    Q_EMIT failedToSaveNoteToLocalStorage(noteLocalUid, errorDescription);
}

void NoteEditorLocalStorageBroker::onExpungeResourceComplete(Resource resource, QUuid requestId)
{
    Q_UNUSED(m_resourcesCache.remove(resource.localUid()))

    auto it = m_noteLocalUidsByExpungeResourceRequestIds.find(requestId);
    if (it == m_noteLocalUidsByExpungeResourceRequestIds.end()) {
        return;
    }

    QNDEBUG(QStringLiteral("NoteEditorLocalStorageBroker::onExpungeResourceComplete"));

    QString noteLocalUid = it.value();
    m_noteLocalUidsByExpungeResourceRequestIds.erase(it);

    auto saveNoteInfoIt = m_saveNoteInfoByNoteLocalUids.find(noteLocalUid);
    if (Q_UNLIKELY(saveNoteInfoIt == m_saveNoteInfoByNoteLocalUids.end())) {
        QNWARNING(QStringLiteral("Unable to find note which resource was expunged from the local storage: resource = ")
                  << resource);
        return;
    }

    SaveNoteInfo & saveNoteInfo = saveNoteInfoIt.value();

    // Extra precaution againts the case of miscounting and overflow
    if (saveNoteInfo.m_pendingExpungeResourceRequests > 0) {
        --saveNoteInfo.m_pendingExpungeResourceRequests;
    }

    if (saveNoteInfo.hasPendingResourceOperations()) {
        return;
    }

    emitUpdateNoteRequest(saveNoteInfo.m_notePendingSaving);
    m_saveNoteInfoByNoteLocalUids.erase(saveNoteInfoIt);
}

void NoteEditorLocalStorageBroker::onExpungeResourceFailed(Resource resource, ErrorString errorDescription, QUuid requestId)
{
    auto it = m_noteLocalUidsByExpungeResourceRequestIds.find(requestId);
    if (it == m_noteLocalUidsByExpungeResourceRequestIds.end()) {
        return;
    }

    QNWARNING(QStringLiteral("NoteEditorLocalStorageBroker::onExpungeResourceFailed: request id = ") << requestId
              << QStringLiteral(", error description: ") << errorDescription
              << QStringLiteral(", resource: ") << resource);

    QString noteLocalUid = it.value();
    m_noteLocalUidsByExpungeResourceRequestIds.erase(it);

    auto saveNoteInfoIt = m_saveNoteInfoByNoteLocalUids.find(noteLocalUid);
    if (saveNoteInfoIt != m_saveNoteInfoByNoteLocalUids.end()) {
        m_saveNoteInfoByNoteLocalUids.erase(saveNoteInfoIt);
    }

    Q_EMIT failedToSaveNoteToLocalStorage(noteLocalUid, errorDescription);
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

void NoteEditorLocalStorageBroker::onFindResourceComplete(Resource resource, bool withBinaryData, QUuid requestId)
{
    auto it = m_findResourceRequestIds.find(requestId);
    if (it == m_findResourceRequestIds.end()) {
        return;
    }

    QNDEBUG(QStringLiteral("NoteEditorLocalStorageBroker::onFindResourceComplete: request id = ") << requestId
            << QStringLiteral(", with binary data = ") << (withBinaryData ? QStringLiteral("true") : QStringLiteral("false"))
            << QStringLiteral(", resource: ") << resource);

    m_findResourceRequestIds.erase(it);

    qint32 totalBinaryDataSize = 0;
    if (resource.hasDataSize()) {
        totalBinaryDataSize += resource.dataSize();
    }
    if (resource.hasAlternateDataSize()) {
        totalBinaryDataSize += resource.alternateDataSize();
    }

    if (totalBinaryDataSize < MAX_TOTAL_RESOURCE_BINARY_DATA_SIZE_IN_BYTES) {
        m_resourcesCache.put(resource.localUid(), resource);
    }

    Q_EMIT foundResourceData(resource);
}

void NoteEditorLocalStorageBroker::onFindResourceFailed(Resource resource, bool withBinaryData,
                                                        ErrorString errorDescription, QUuid requestId)
{
    auto it = m_findResourceRequestIds.find(requestId);
    if (it == m_findResourceRequestIds.end()) {
        return;
    }

    QNWARNING(QStringLiteral("NoteEditorLocalStorageBroker::onFindResourceFailed: request id = ") << requestId
              << QStringLiteral(", with binary data = ") << (withBinaryData ? QStringLiteral("true") : QStringLiteral("false"))
              << QStringLiteral(", error description = ") << errorDescription << QStringLiteral(", resource: ")
              << resource);

    m_findResourceRequestIds.erase(it);
    Q_EMIT failedToFindResourceData(resource.localUid(), errorDescription);
}

void NoteEditorLocalStorageBroker::onSwitchUserComplete(Account account, QUuid requestId)
{
    QNDEBUG(QStringLiteral("NoteEditorLocalStorageBroker::onSwitchUserComplete: account = ")
            << account << QStringLiteral("\nRequest id = ") << requestId);

    m_findNoteRequestIds.clear();
    m_findNotebookRequestIds.clear();
    m_findResourceRequestIds.clear();
    m_notesPendingNotebookFindingByNotebookGuid.clear();
    m_notesPendingNotebookFindingByNotebookLocalUid.clear();

    m_noteLocalUidsByAddResourceRequestIds.clear();
    m_noteLocalUidsByUpdateResourceRequestIds.clear();
    m_noteLocalUidsByExpungeResourceRequestIds.clear();

    m_notebooksCache.clear();
    m_notesCache.clear();
    m_resourcesCache.clear();

    m_saveNoteInfoByNoteLocalUids.clear();
    m_updateNoteRequestIds.clear();
}

void NoteEditorLocalStorageBroker::createConnections(LocalStorageManagerAsync & localStorageManagerAsync)
{
    QNDEBUG(QStringLiteral("NoteEditorLocalStorageBroker::createConnections"));

    // Local signals to LocalStorageManagerAsync's slots
    QObject::connect(this, QNSIGNAL(NoteEditorLocalStorageBroker,updateNote,Note,LocalStorageManager::UpdateNoteOptions,QUuid),
                     &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onUpdateNoteRequest,Note,LocalStorageManager::UpdateNoteOptions,QUuid));
    QObject::connect(this, QNSIGNAL(NoteEditorLocalStorageBroker,addResource,Resource,QUuid),
                     &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onAddResourceRequest,Resource,QUuid));
    QObject::connect(this, QNSIGNAL(NoteEditorLocalStorageBroker,updateResource,Resource,QUuid),
                     &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onUpdateResourceRequest,Resource,QUuid));
    QObject::connect(this, QNSIGNAL(NoteEditorLocalStorageBroker,expungeResource,QUuid),
                     &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onExpungeResourceRequest,Resource,QUuid));
    QObject::connect(this, QNSIGNAL(NoteEditorLocalStorageBroker,findNote,Note,bool,bool,QUuid),
                     &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onFindNoteRequest,Note,bool,bool,QUuid));
    QObject::connect(this, QNSIGNAL(NoteEditorLocalStorageBroker,findNotebook,Notebook,QUuid),
                     &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onFindNotebookRequest,Notebook,QUuid));
    QObject::connect(this, QNSIGNAL(NoteEditorLocalStorageBroker,findResource,Resource,bool,QUuid),
                     &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onFindResourceRequest,Resource,bool,QUuid));

    // LocalStorageManagerAsync's signals to local slots
    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateNoteComplete,Note,LocalStorageManager::UpdateNoteOptions,QUuid),
                     this, QNSLOT(NoteEditorLocalStorageBroker,onUpdateNoteComplete,Note,LocalStorageManager::UpdateNoteOptions,QUuid));
    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateNoteFailed,Note,LocalStorageManager::UpdateNoteOptions,ErrorString,QUuid),
                     this, QNSLOT(NoteEditorLocalStorageBroker,onUpdateNoteFailed,Note,LocalStorageManager::UpdateNoteOptions,ErrorString,QUuid));
    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateNotebookComplete,Notebook,QUuid),
                     this, QNSLOT(NoteEditorLocalStorageBroker,onUpdateNotebookComplete,Notebook,QUuid));
    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addResourceComplete,Resource,QUuid),
                     this, QNSLOT(NoteEditorLocalStorageBroker,onAddResourceComplete,Resource,QUuid));
    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addResourceFailed,Resource,ErrorString,QUuid),
                     this, QNSLOT(NoteEditorLocalStorageBroker,onAddResourceFailed,Resource,ErrorString,QUuid));
    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateResourceComplete,Resource,QUuid),
                     this, QNSLOT(NoteEditorLocalStorageBroker,onUpdateResourceComplete,Resource,QUuid));
    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateResourceFailed,Resource,ErrorString,QUuid),
                     this, QNSLOT(NoteEditorLocalStorageBroker,onUpdateResourceFailed,Resource,ErrorString,QUuid));
    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeResourceComplete,Resource,QUuid),
                     this, QNSLOT(NoteEditorLocalStorageBroker,onExpungeResourceComplete,Resource,QUuid));
    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeResourceFailed,Resource,ErrorString,QUuid),
                     this, QNSLOT(NoteEditorLocalStorageBroker,onExpungeResourceFailed,Resource,ErrorString,QUuid));
    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findNoteComplete,Note,bool,bool,QUuid),
                     this, QNSLOT(NoteEditorLocalStorageBroker,onFindNoteComplete,Note,bool,bool,QUuid));
    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findNoteFailed,Note,bool,bool,ErrorString,QUuid),
                     this, QNSLOT(NoteEditorLocalStorageBroker,onFindNoteFailed,Note,bool,bool,ErrorString,QUuid));
    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findNotebookComplete,Notebook,QUuid),
                     this, QNSLOT(NoteEditorLocalStorageBroker,onFindNotebookComplete,Notebook,QUuid));
    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findNotebookFailed,Notebook,ErrorString,QUuid),
                     this, QNSLOT(NoteEditorLocalStorageBroker,onFindNotebookFailed,Notebook,ErrorString,QUuid));
    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findResourceComplete,Resource,bool,QUuid),
                     this, QNSLOT(NoteEditorLocalStorageBroker,onFindResourceComplete,Resource,bool,QUuid));
    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findResourceFailed,Resource,bool,ErrorString,QUuid),
                     this, QNSLOT(NoteEditorLocalStorageBroker,onFindResourceFailed,Resource,bool,ErrorString,QUuid));
    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeNoteComplete,Note,QUuid),
                     this, QNSLOT(NoteEditorLocalStorageBroker,onExpungeNoteComplete,Note,QUuid));
    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeNotebookComplete,Notebook,QUuid),
                     this, QNSLOT(NoteEditorLocalStorageBroker,onExpungeNotebookComplete,Notebook,QUuid));
    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,switchUserComplete,Account,QUuid),
                     this, QNSLOT(NoteEditorLocalStorageBroker,onSwitchUserComplete,Account,QUuid));
}

void NoteEditorLocalStorageBroker::disconnectFromLocalStorage(LocalStorageManagerAsync & localStorageManagerAsync)
{
    QNDEBUG(QStringLiteral("NoteEditorLocalStorageBroker::disconnectFromLocalStorage"));

    // Disconnect local signals from LocalStorageManagerAsync's slots
    QObject::disconnect(this, QNSIGNAL(NoteEditorLocalStorageBroker,updateNote,Note,LocalStorageManager::UpdateNoteOptions,QUuid),
                        &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onUpdateNoteRequest,Note,LocalStorageManager::UpdateNoteOptions,QUuid));
    QObject::disconnect(this, QNSIGNAL(NoteEditorLocalStorageBroker,addResource,Resource,QUuid),
                        &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onAddResourceRequest,Resource,QUuid));
    QObject::disconnect(this, QNSIGNAL(NoteEditorLocalStorageBroker,updateResource,Resource,QUuid),
                        &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onUpdateResourceRequest,Resource,QUuid));
    QObject::disconnect(this, QNSIGNAL(NoteEditorLocalStorageBroker,expungeResource,QUuid),
                        &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onExpungeResourceRequest,Resource,QUuid));
    QObject::disconnect(this, QNSIGNAL(NoteEditorLocalStorageBroker,findNote,Note,bool,bool,QUuid),
                        &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onFindNoteRequest,Note,bool,bool,QUuid));
    QObject::disconnect(this, QNSIGNAL(NoteEditorLocalStorageBroker,findNotebook,Notebook,QUuid),
                        &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onFindNotebookRequest,Notebook,QUuid));
    QObject::disconnect(this, QNSIGNAL(NoteEditorLocalStorageBroker,findResource,Resource,bool,QUuid),
                        &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onFindResourceRequest,Resource,bool,QUuid));

    // Disconnect LocalStorageManagerAsync's signals from local slots
    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateNoteComplete,Note,LocalStorageManager::UpdateNoteOptions,QUuid),
                        this, QNSLOT(NoteEditorLocalStorageBroker,onUpdateNoteComplete,Note,LocalStorageManager::UpdateNoteOptions,QUuid));
    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateNoteFailed,Note,LocalStorageManager::UpdateNoteOptions,ErrorString,QUuid),
                        this, QNSLOT(NoteEditorLocalStorageBroker,onUpdateNoteFailed,Note,LocalStorageManager::UpdateNoteOptions,ErrorString,QUuid));
    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateNotebookComplete,Notebook,QUuid),
                        this, QNSLOT(NoteEditorLocalStorageBroker,onUpdateNotebookComplete,Notebook,QUuid));
    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addResourceComplete,Resource,QUuid),
                        this, QNSLOT(NoteEditorLocalStorageBroker,onAddResourceComplete,Resource,QUuid));
    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addResourceFailed,Resource,ErrorString,QUuid),
                        this, QNSLOT(NoteEditorLocalStorageBroker,onAddResourceFailed,Resource,ErrorString,QUuid));
    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateResourceComplete,Resource,QUuid),
                        this, QNSLOT(NoteEditorLocalStorageBroker,onUpdateResourceComplete,Resource,QUuid));
    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateResourceFailed,Resource,ErrorString,QUuid),
                        this, QNSLOT(NoteEditorLocalStorageBroker,onUpdateResourceFailed,Resource,ErrorString,QUuid));
    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeResourceComplete,Resource,QUuid),
                        this, QNSLOT(NoteEditorLocalStorageBroker,onExpungeResourceComplete,Resource,QUuid));
    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeResourceFailed,Resource,ErrorString,QUuid),
                        this, QNSLOT(NoteEditorLocalStorageBroker,onExpungeResourceFailed,Resource,ErrorString,QUuid));
    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findNoteComplete,Note,bool,bool,QUuid),
                        this, QNSLOT(NoteEditorLocalStorageBroker,onFindNoteComplete,Note,bool,bool,QUuid));
    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findNoteFailed,Note,bool,bool,ErrorString,QUuid),
                        this, QNSLOT(NoteEditorLocalStorageBroker,onFindNoteFailed,Note,bool,bool,ErrorString,QUuid));
    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findNotebookComplete,Notebook,QUuid),
                        this, QNSLOT(NoteEditorLocalStorageBroker,onFindNotebookComplete,Notebook,QUuid));
    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findNotebookFailed,Notebook,ErrorString,QUuid),
                        this, QNSLOT(NoteEditorLocalStorageBroker,onFindNotebookFailed,Notebook,ErrorString,QUuid));
    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findResourceComplete,Resource,bool,QUuid),
                        this, QNSLOT(NoteEditorLocalStorageBroker,onFindResourceComplete,Resource,bool,QUuid));
    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findResourceFailed,Resource,bool,ErrorString,QUuid),
                        this, QNSLOT(NoteEditorLocalStorageBroker,onFindResourceFailed,Resource,bool,ErrorString,QUuid));
    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeNoteComplete,Note,QUuid),
                        this, QNSLOT(NoteEditorLocalStorageBroker,onExpungeNoteComplete,Note,QUuid));
    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeNotebookComplete,Notebook,QUuid),
                        this, QNSLOT(NoteEditorLocalStorageBroker,onExpungeNotebookComplete,Notebook,QUuid));
    QObject::disconnect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,switchUserComplete,Account,QUuid),
                        this, QNSLOT(NoteEditorLocalStorageBroker,onSwitchUserComplete,Account,QUuid));
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

void NoteEditorLocalStorageBroker::emitUpdateNoteRequest(const Note & note)
{
    LocalStorageManager::UpdateNoteOptions options(LocalStorageManager::UpdateNoteOption::UpdateTags | LocalStorageManager::UpdateNoteOption::UpdateResourceMetadata);
    QUuid requestId = QUuid::createUuid();
    Q_UNUSED(m_updateNoteRequestIds.insert(requestId))
    QNDEBUG(QStringLiteral("Emitting the request to update note in local storage: request id = ") << requestId
            << QStringLiteral(", note: ") << note);
    Q_EMIT updateNote(note, options, requestId);
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

bool NoteEditorLocalStorageBroker::SaveNoteInfo::hasPendingResourceOperations() const
{
    if ( (m_pendingAddResourceRequests != 0) ||
         (m_pendingUpdateResourceRequests != 0) ||
         (m_pendingExpungeResourceRequests != 0) )
    {
        QNDEBUG(QStringLiteral("Still pending for ")
                << m_pendingAddResourceRequests
                << QStringLiteral(" pending add resource requests and/or ")
                << m_pendingUpdateResourceRequests
                << QStringLiteral(" pending update resource requests and/or ")
                << m_pendingExpungeResourceRequests
                << QStringLiteral(" pending expunge resource requests for note with local uid ")
                << m_notePendingSaving.localUid());
        return true;
    }

    return false;
}

} // namespace quentier
