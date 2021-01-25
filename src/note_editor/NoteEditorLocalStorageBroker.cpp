/*
 * Copyright 2018-2021 Dmitry Ivanov
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

NoteEditorLocalStorageBroker::NoteEditorLocalStorageBroker(QObject * parent) :
    QObject(parent), m_notebooksCache(5), m_notesCache(5), m_resourcesCache(5)
{}

NoteEditorLocalStorageBroker & NoteEditorLocalStorageBroker::instance()
{
    static NoteEditorLocalStorageBroker noteEditorLocalStorageBroker;
    return noteEditorLocalStorageBroker;
}

LocalStorageManagerAsync * NoteEditorLocalStorageBroker::localStorageManager()
{
    return m_pLocalStorageManagerAsync;
}

void NoteEditorLocalStorageBroker::setLocalStorageManager(
    LocalStorageManagerAsync & localStorageManagerAsync)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorLocalStorageBroker"
            << "::setLocalStorageManager");

    if (m_pLocalStorageManagerAsync == &localStorageManagerAsync) {
        QNDEBUG("note_editor", "LocalStorageManagerAsync is already set");
        return;
    }

    if (m_pLocalStorageManagerAsync) {
        disconnectFromLocalStorage(*m_pLocalStorageManagerAsync);
    }

    m_pLocalStorageManagerAsync = &localStorageManagerAsync;
    createConnections(localStorageManagerAsync);
}

void NoteEditorLocalStorageBroker::saveNoteToLocalStorage(
    const qevercloud::Note & note)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorLocalStorageBroker"
            << "::saveNoteToLocalStorage: note local id = "
            << note.localId());

    const auto * pCachedNote = m_notesCache.get(note.localId());
    if (!pCachedNote) {
        QNTRACE(
            "note_editor",
            "Haven't found the note to be saved within the cache");

        const QUuid requestId = QUuid::createUuid();
        m_notesPendingSavingByFindNoteRequestIds[requestId] = note;
        qevercloud::Note dummy;
        dummy.setLocalId(note.localId());

        QNDEBUG(
            "note_editor",
            "Emitting the request to find note for the sake "
                << "of resource list updates resolution");

        const LocalStorageManager::GetNoteOptions options(
            LocalStorageManager::GetNoteOption::WithResourceMetadata);

        Q_EMIT findNote(dummy, options, requestId);
        return;
    }

    saveNoteToLocalStorageImpl(*pCachedNote, note);
}

void NoteEditorLocalStorageBroker::findNoteAndNotebook(
    const QString & noteLocalId)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorLocalStorageBroker::findNoteAndNotebook: "
            << "note local id = " << noteLocalId);

    const auto * pCachedNote = m_notesCache.get(noteLocalId);
    if (!pCachedNote) {
        QNDEBUG(
            "note_editor",
            "Note was not found within the cache, looking "
                << "it up in the local storage");
        emitFindNoteRequest(noteLocalId);
        return;
    }

    if (Q_UNLIKELY(
            pCachedNote->notebookLocalId().isEmpty() &&
            !pCachedNote->notebookGuid()))
    {
        Q_UNUSED(m_notesCache.remove(noteLocalId))
        QNDEBUG(
            "note_editor",
            "The note within the cache contained neither "
                << "notebook local id nor notebook guid, looking it up in "
                << "the local storage");
        emitFindNoteRequest(noteLocalId);
        return;
    }

    if (!pCachedNote->notebookLocalId().isEmpty()) {
        const QString & notebookLocalId = pCachedNote->notebookLocalId();

        const auto * pCachedNotebook = m_notebooksCache.get(notebookLocalId);
        if (pCachedNotebook) {
            QNDEBUG(
                "note_editor", "Found both note and notebook within caches");
            Q_EMIT foundNoteAndNotebook(*pCachedNote, *pCachedNotebook);
        }
        else {
            QNDEBUG(
                "note_editor",
                "Notebook was not found within the cache, looking it up in "
                    << "the local storage");

            emitFindNotebookForNoteByLocalIdRequest(
                notebookLocalId, *pCachedNote);
        }

        return;
    }

    emitFindNotebookForNoteByGuidRequest(
        *pCachedNote->notebookGuid(), *pCachedNote);
}

void NoteEditorLocalStorageBroker::findResourceData(
    const QString & resourceLocalId)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorLocalStorageBroker::findResourceData: "
            << "resource local id = " << resourceLocalId);

    const auto * pCachedResource = m_resourcesCache.get(resourceLocalId);
    if (pCachedResource) {
        QNDEBUG("note_editor", "Found cached resource binary data");
        Q_EMIT foundResourceData(*pCachedResource);
        return;
    }

    const QUuid requestId = QUuid::createUuid();
    Q_UNUSED(m_findResourceRequestIds.insert(requestId))

    qevercloud::Resource resource;
    resource.setLocalId(resourceLocalId);

    QNDEBUG(
        "note_editor",
        "Emitting the request to find resource: request id = " << requestId
            << ", resource local id = " << resourceLocalId);

    const LocalStorageManager::GetResourceOptions options(
        LocalStorageManager::GetResourceOption::WithBinaryData);

    Q_EMIT findResource(resource, options, requestId);
}

void NoteEditorLocalStorageBroker::onUpdateNoteComplete(
    qevercloud::Note note, LocalStorageManager::UpdateNoteOptions options,
    QUuid requestId)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorLocalStorageBroker::onUpdateNoteComplete: "
            << "request id = " << requestId << ", options = " << options
            << ", note: " << note);

    if (m_notesCache.exists(note.localId())) {
        if (!note.resources() || note.resources()->empty()) {
            m_notesCache.put(note.localId(), note);
        }
        else {
            auto resources = *note.resources();
            for (auto & resource: resources) {
                if (resource.data()) {
                    resource.mutableData()->setBody(std::nullopt);
                }
                if (resource.alternateData()) {
                    resource.mutableAlternateData()->setBody(std::nullopt);
                }
            }

            qevercloud::Note noteWithoutResourceDataBodies = note;
            noteWithoutResourceDataBodies.setResources(resources);
            m_notesCache.put(note.localId(), noteWithoutResourceDataBodies);
        }
    }

    const auto it = m_updateNoteRequestIds.find(requestId);
    if (it != m_updateNoteRequestIds.end()) {
        QNDEBUG(
            "note_editor",
            "Note was successfully saved within the local storage");
        m_updateNoteRequestIds.erase(it);
        Q_EMIT noteSavedToLocalStorage(note.localId());
        return;
    }

    Q_EMIT noteUpdated(note);
}

void NoteEditorLocalStorageBroker::onUpdateNoteFailed(
    qevercloud::Note note, LocalStorageManager::UpdateNoteOptions options,
    ErrorString errorDescription, QUuid requestId)
{
    const auto it = m_updateNoteRequestIds.find(requestId);
    if (it == m_updateNoteRequestIds.end()) {
        return;
    }

    QNWARNING(
        "note_editor",
        "Failed to update the note within the local storage: "
            << errorDescription << ", note: " << note
            << "\nUpdate options: " << options
            << ", request id = " << requestId);

    m_updateNoteRequestIds.erase(it);
    Q_EMIT failedToSaveNoteToLocalStorage(note.localId(), errorDescription);
}

void NoteEditorLocalStorageBroker::onUpdateNotebookComplete(
    qevercloud::Notebook notebook, QUuid requestId)
{
    Q_UNUSED(requestId)

    QString notebookLocalId = notebook.localId();
    if (m_notebooksCache.exists(notebookLocalId)) {
        m_notebooksCache.put(notebookLocalId, notebook);
    }

    Q_EMIT notebookUpdated(notebook);
}

void NoteEditorLocalStorageBroker::onFindNoteComplete(
    qevercloud::Note foundNote, LocalStorageManager::GetNoteOptions options,
    QUuid requestId)
{
    const auto it = m_findNoteRequestIds.find(requestId);
    if (it != m_findNoteRequestIds.end()) {
        QNDEBUG(
            "note_editor",
            "NoteEditorLocalStorageBroker"
                << "::onFindNoteComplete: request id = " << requestId
                << ", with resource metadata = "
                << ((options &
                     LocalStorageManager::GetNoteOption::WithResourceMetadata)
                        ? "true"
                        : "false")
                << ", with resource binary data = "
                << ((options &
                     LocalStorageManager::GetNoteOption::WithResourceBinaryData)
                        ? "true"
                        : "false"));

        m_findNoteRequestIds.erase(it);

        if (Q_UNLIKELY(
                foundNote.notebookLocalId().isEmpty() &&
                !foundNote.notebookGuid()))
        {
            ErrorString errorDescription(
                QT_TR_NOOP("note doesn't belong to any notebook"));

            APPEND_NOTE_DETAILS(errorDescription, foundNote)
            QNWARNING(
                "note_editor", errorDescription << ", note: " << foundNote);

            Q_EMIT failedToFindNoteOrNotebook(
                foundNote.localId(), errorDescription);
            return;
        }

        m_notesCache.put(foundNote.localId(), foundNote);

        if (!foundNote.notebookLocalId().isEmpty()) {
            const QString & notebookLocalId = foundNote.notebookLocalId();

            const auto * pCachedNotebook =
                m_notebooksCache.get(notebookLocalId);

            if (pCachedNotebook) {
                QNDEBUG("note_editor", "Found notebook within the cache");
                Q_EMIT foundNoteAndNotebook(foundNote, *pCachedNotebook);
            }
            else {
                QNDEBUG(
                    "note_editor",
                    "Notebook was not found within the cache, looking it up "
                        << "in the local storage");

                emitFindNotebookForNoteByLocalIdRequest(
                    notebookLocalId, foundNote);
            }

            return;
        }

        emitFindNotebookForNoteByGuidRequest(
            *foundNote.notebookGuid(), foundNote);

        return;
    }

    const auto pit = m_notesPendingSavingByFindNoteRequestIds.find(requestId);
    if (pit != m_notesPendingSavingByFindNoteRequestIds.end()) {
        QNDEBUG(
            "note_editor",
            "NoteEditorLocalStorageBroker"
                << "::onFindNoteComplete: request id = " << requestId
                << ", with resource metadata = "
                << ((options &
                     LocalStorageManager::GetNoteOption::WithResourceMetadata)
                        ? "true"
                        : "false")
                << ", with resource binary data = "
                << ((options &
                     LocalStorageManager::GetNoteOption::WithResourceBinaryData)
                        ? "true"
                        : "false"));

        const qevercloud::Note updatedNote = pit.value();
        Q_UNUSED(m_notesPendingSavingByFindNoteRequestIds.erase(pit))
        saveNoteToLocalStorageImpl(foundNote, updatedNote);
        return;
    }
}

void NoteEditorLocalStorageBroker::onFindNoteFailed(
    qevercloud::Note note, LocalStorageManager::GetNoteOptions options,
    ErrorString errorDescription, QUuid requestId)
{
    const auto it = m_findNoteRequestIds.find(requestId);
    if (it == m_findNoteRequestIds.end()) {
        return;
    }

    QNWARNING(
        "note_editor",
        "NoteEditorLocalStorageBroker::onFindNoteFailed: "
            << "request id = " << requestId << ", with resource metadata = "
            << ((options &
                 LocalStorageManager::GetNoteOption::WithResourceMetadata)
                    ? "true"
                    : "false")
            << ", with resource binary data = "
            << ((options &
                 LocalStorageManager::GetNoteOption::WithResourceBinaryData)
                    ? "true"
                    : "false")
            << ", error description: " << errorDescription
            << ", note: " << note);

    m_findNoteRequestIds.erase(it);
    Q_EMIT failedToFindNoteOrNotebook(note.localId(), errorDescription);
}

void NoteEditorLocalStorageBroker::onFindNotebookComplete(
    qevercloud::Notebook foundNotebook, QUuid requestId)
{
    const auto it = m_findNotebookRequestIds.find(requestId);
    if (it == m_findNotebookRequestIds.end()) {
        return;
    }

    QNDEBUG(
        "note_editor",
        "NoteEditorLocalStorageBroker"
            << "::onFindNotebookComplete: request id = " << requestId
            << ", notebook: " << foundNotebook);

    m_findNotebookRequestIds.erase(it);

    const QString notebookLocalId = foundNotebook.localId();
    m_notebooksCache.put(notebookLocalId, foundNotebook);

    bool foundNotesPendingNotebookFinding = true;
    bool foundByNotebookGuid = false;

    auto pendingNotesIt =
        m_notesPendingNotebookFindingByNotebookLocalId.find(notebookLocalId);

    if (pendingNotesIt == m_notesPendingNotebookFindingByNotebookLocalId.end())
    {
        // Maybe this notebook was searched by guid
        if (foundNotebook.guid()) {
            pendingNotesIt = m_notesPendingNotebookFindingByNotebookGuid.find(
                *foundNotebook.guid());

            if (pendingNotesIt ==
                m_notesPendingNotebookFindingByNotebookGuid.end()) {
                foundNotesPendingNotebookFinding = false;
            }
            else {
                foundByNotebookGuid = true;
            }
        }
        else {
            foundNotesPendingNotebookFinding = false;
        }
    }

    if (!foundNotesPendingNotebookFinding) {
        QNWARNING(
            "note_editor",
            "Found notebook but unable to detect which "
                << "notes required its finding: notebook = " << foundNotebook);
        return;
    }

    const NotesHash & notes = pendingNotesIt.value();
    for (const auto & noteIt: qevercloud::toRange(notes)) {
        QNTRACE(
            "note_editor",
            "Found pending note, emitting "
                << "foundNoteAndNotebook signal: note local id = "
                << noteIt.value().localId());
        Q_EMIT foundNoteAndNotebook(noteIt.value(), foundNotebook);
    }

    if (foundByNotebookGuid) {
        m_notesPendingNotebookFindingByNotebookGuid.erase(pendingNotesIt);
    }
    else {
        m_notesPendingNotebookFindingByNotebookLocalId.erase(pendingNotesIt);
    }
}

void NoteEditorLocalStorageBroker::onFindNotebookFailed(
    qevercloud::Notebook notebook, ErrorString errorDescription,
    QUuid requestId)
{
    const auto it = m_findNotebookRequestIds.find(requestId);
    if (it == m_findNotebookRequestIds.end()) {
        return;
    }

    QNWARNING(
        "note_editor",
        "NoteEditorLocalStorageBroker"
            << "::onFindNotebookFailed: request id = " << requestId
            << ", error description: " << errorDescription
            << ", notebook: " << notebook);

    m_findNotebookRequestIds.erase(it);

    const QString notebookLocalId = notebook.localId();
    bool foundNotesPendingNotebookFinding = true;
    bool foundByNotebookGuid = false;

    auto pendingNotesIt =
        m_notesPendingNotebookFindingByNotebookLocalId.find(notebookLocalId);

    if (pendingNotesIt == m_notesPendingNotebookFindingByNotebookLocalId.end())
    {
        // Maybe this notebook was searched by guid
        if (notebook.guid()) {
            pendingNotesIt = m_notesPendingNotebookFindingByNotebookGuid.find(
                *notebook.guid());

            if (pendingNotesIt ==
                m_notesPendingNotebookFindingByNotebookGuid.end()) {
                foundNotesPendingNotebookFinding = false;
            }
            else {
                foundByNotebookGuid = true;
            }
        }
        else {
            foundNotesPendingNotebookFinding = false;
        }
    }

    if (!foundNotesPendingNotebookFinding) {
        QNDEBUG(
            "note_editor",
            "Failed to find notebook and unable to determine "
                << "for which notes it was required - nothing left to do");
        return;
    }

    const auto & notes = pendingNotesIt.value();
    for (const auto & noteIt: qevercloud::toRange(notes)) {
        Q_EMIT failedToFindNoteOrNotebook(
            noteIt.value().localId(), errorDescription);
    }

    if (foundByNotebookGuid) {
        m_notesPendingNotebookFindingByNotebookGuid.erase(pendingNotesIt);
    }
    else {
        m_notesPendingNotebookFindingByNotebookLocalId.erase(pendingNotesIt);
    }
}

void NoteEditorLocalStorageBroker::onAddResourceComplete(
    qevercloud::Resource resource, QUuid requestId)
{
    const auto it = m_noteLocalIdsByAddResourceRequestIds.find(requestId);
    if (it == m_noteLocalIdsByAddResourceRequestIds.end()) {
        return;
    }

    QNDEBUG(
        "note_editor",
        "NoteEditorLocalStorageBroker"
            << "::onAddResourceComplete: request id = " << requestId
            << ", resource: " << resource);

    const QString noteLocalId = it.value();
    m_noteLocalIdsByAddResourceRequestIds.erase(it);

    const auto saveNoteInfoIt = m_saveNoteInfoByNoteLocalIds.find(noteLocalId);
    if (Q_UNLIKELY(saveNoteInfoIt == m_saveNoteInfoByNoteLocalIds.end())) {
        QNWARNING(
            "note_editor",
            "Unable to find note for which the resource "
                << "was added to the local storage: resource = " << resource);
        return;
    }

    SaveNoteInfo & saveNoteInfo = saveNoteInfoIt.value();

    // Extra precaution against the case of miscounting and overflow
    if (saveNoteInfo.m_pendingAddResourceRequests > 0) {
        --saveNoteInfo.m_pendingAddResourceRequests;
    }

    if (saveNoteInfo.hasPendingResourceOperations()) {
        QNDEBUG(
            "note_editor",
            "Still pending resource data saving: " << saveNoteInfo);
        return;
    }

    emitUpdateNoteRequest(saveNoteInfo.m_notePendingSaving);
    m_saveNoteInfoByNoteLocalIds.erase(saveNoteInfoIt);
}

void NoteEditorLocalStorageBroker::onAddResourceFailed(
    qevercloud::Resource resource, ErrorString errorDescription,
    QUuid requestId)
{
    const auto it = m_noteLocalIdsByAddResourceRequestIds.find(requestId);
    if (it == m_noteLocalIdsByAddResourceRequestIds.end()) {
        return;
    }

    QNWARNING(
        "note_editor",
        "NoteEditorLocalStorageBroker"
            << "::onAddResourceFailed: request id = " << requestId
            << ", error description: " << errorDescription
            << ", resource: " << resource);

    const QString noteLocalId = it.value();
    m_noteLocalIdsByAddResourceRequestIds.erase(it);

    const auto saveNoteInfoIt = m_saveNoteInfoByNoteLocalIds.find(noteLocalId);
    if (saveNoteInfoIt != m_saveNoteInfoByNoteLocalIds.end()) {
        m_saveNoteInfoByNoteLocalIds.erase(saveNoteInfoIt);
    }

    Q_EMIT failedToSaveNoteToLocalStorage(noteLocalId, errorDescription);
}

void NoteEditorLocalStorageBroker::onUpdateResourceComplete(
    qevercloud::Resource resource, QUuid requestId)
{
    if (m_resourcesCache.get(resource.localId())) {
        m_resourcesCache.put(resource.localId(), resource);
    }

    const auto it = m_noteLocalIdsByUpdateResourceRequestIds.find(requestId);
    if (it == m_noteLocalIdsByUpdateResourceRequestIds.end()) {
        return;
    }

    QNDEBUG(
        "note_editor",
        "NoteEditorLocalStorageBroker"
            << "::onUpdateResourceComplete: request id = " << requestId
            << ", resource: " << resource);

    const QString noteLocalId = it.value();
    m_noteLocalIdsByUpdateResourceRequestIds.erase(it);

    const auto saveNoteInfoIt = m_saveNoteInfoByNoteLocalIds.find(noteLocalId);
    if (Q_UNLIKELY(saveNoteInfoIt == m_saveNoteInfoByNoteLocalIds.end())) {
        QNWARNING(
            "note_editor",
            "Unable to find note for which the resource "
                << "was updated in the local storage: note local id = "
                << noteLocalId << ", resource = " << resource);
        return;
    }

    SaveNoteInfo & saveNoteInfo = saveNoteInfoIt.value();

    // Extra precaution against the case of miscounting and overflow
    if (saveNoteInfo.m_pendingUpdateResourceRequests > 0) {
        --saveNoteInfo.m_pendingUpdateResourceRequests;
    }

    if (saveNoteInfo.hasPendingResourceOperations()) {
        QNDEBUG(
            "note_editor",
            "Still pending resource data saving: " << saveNoteInfo);
        return;
    }

    emitUpdateNoteRequest(saveNoteInfo.m_notePendingSaving);
    m_saveNoteInfoByNoteLocalIds.erase(saveNoteInfoIt);
}

void NoteEditorLocalStorageBroker::onUpdateResourceFailed(
    qevercloud::Resource resource, ErrorString errorDescription,
    QUuid requestId)
{
    const auto it = m_noteLocalIdsByUpdateResourceRequestIds.find(requestId);
    if (it == m_noteLocalIdsByUpdateResourceRequestIds.end()) {
        return;
    }

    QNWARNING(
        "note_editor",
        "NoteEditorLocalStorageBroker"
            << "::onUpdateResourceFailed: request id = " << requestId
            << ", error description: " << errorDescription
            << ", resource: " << resource);

    const QString noteLocalId = it.value();
    m_noteLocalIdsByUpdateResourceRequestIds.erase(it);

    const auto saveNoteInfoIt = m_saveNoteInfoByNoteLocalIds.find(noteLocalId);
    if (saveNoteInfoIt != m_saveNoteInfoByNoteLocalIds.end()) {
        m_saveNoteInfoByNoteLocalIds.erase(saveNoteInfoIt);
    }

    Q_EMIT failedToSaveNoteToLocalStorage(noteLocalId, errorDescription);
}

void NoteEditorLocalStorageBroker::onExpungeResourceComplete(
    qevercloud::Resource resource, QUuid requestId)
{
    Q_UNUSED(m_resourcesCache.remove(resource.localId()))

    const auto it = m_noteLocalIdsByExpungeResourceRequestIds.find(requestId);
    if (it == m_noteLocalIdsByExpungeResourceRequestIds.end()) {
        return;
    }

    QNDEBUG(
        "note_editor",
        "NoteEditorLocalStorageBroker::onExpungeResourceComplete");

    const QString noteLocalId = it.value();
    m_noteLocalIdsByExpungeResourceRequestIds.erase(it);

    const auto saveNoteInfoIt = m_saveNoteInfoByNoteLocalIds.find(noteLocalId);
    if (Q_UNLIKELY(saveNoteInfoIt == m_saveNoteInfoByNoteLocalIds.end())) {
        QNWARNING(
            "note_editor",
            "Unable to find note which resource was expunged from the local "
                << "storage: resource = " << resource);
        return;
    }

    SaveNoteInfo & saveNoteInfo = saveNoteInfoIt.value();

    // Extra precaution againts the case of miscounting and overflow
    if (saveNoteInfo.m_pendingExpungeResourceRequests > 0) {
        --saveNoteInfo.m_pendingExpungeResourceRequests;
    }

    if (saveNoteInfo.hasPendingResourceOperations()) {
        QNDEBUG(
            "note_editor",
            "Still pending resource data saving: " << saveNoteInfo);
        return;
    }

    emitUpdateNoteRequest(saveNoteInfo.m_notePendingSaving);
    m_saveNoteInfoByNoteLocalIds.erase(saveNoteInfoIt);
}

void NoteEditorLocalStorageBroker::onExpungeResourceFailed(
    qevercloud::Resource resource, ErrorString errorDescription,
    QUuid requestId)
{
    const auto it = m_noteLocalIdsByExpungeResourceRequestIds.find(requestId);
    if (it == m_noteLocalIdsByExpungeResourceRequestIds.end()) {
        return;
    }

    QNWARNING(
        "note_editor",
        "NoteEditorLocalStorageBroker::onExpungeResourceFailed: request id = "
            << requestId << ", error description: " << errorDescription
            << ", resource: " << resource);

    const QString noteLocalId = it.value();
    m_noteLocalIdsByExpungeResourceRequestIds.erase(it);

    const auto saveNoteInfoIt = m_saveNoteInfoByNoteLocalIds.find(noteLocalId);
    if (saveNoteInfoIt != m_saveNoteInfoByNoteLocalIds.end()) {
        m_saveNoteInfoByNoteLocalIds.erase(saveNoteInfoIt);
    }

    Q_EMIT failedToSaveNoteToLocalStorage(noteLocalId, errorDescription);
}

void NoteEditorLocalStorageBroker::onExpungeNoteComplete(
    qevercloud::Note note, QUuid requestId)
{
    Q_UNUSED(requestId)
    const QString noteLocalId = note.localId();
    Q_UNUSED(m_notesCache.remove(noteLocalId))

    QStringList resourceLocalIdsToRemoveFromCache;
    for (const auto & pair: m_resourcesCache) {
        if (Q_UNLIKELY(pair.second.noteLocalId().isEmpty())) {
            QNTRACE(
                "note_editor",
                "Detected resource without note local id; "
                    << "will remove it from the cache: " << pair.second);
            resourceLocalIdsToRemoveFromCache << pair.first;
            continue;
        }

        if (pair.second.noteLocalId() == noteLocalId) {
            resourceLocalIdsToRemoveFromCache << pair.first;
        }
    }

    for (const auto & localId: qAsConst(resourceLocalIdsToRemoveFromCache)) {
        Q_UNUSED(m_resourcesCache.remove(localId))
    }

    Q_EMIT noteDeleted(noteLocalId);
}

void NoteEditorLocalStorageBroker::onExpungeNotebookComplete(
    qevercloud::Notebook notebook, QUuid requestId)
{
    Q_UNUSED(requestId)
    const QString notebookLocalId = notebook.localId();
    Q_UNUSED(m_notebooksCache.remove(notebookLocalId))

    QStringList noteLocalIdsToRemoveFromCache;
    for (const auto & pair: m_notesCache) {
        if (Q_UNLIKELY(pair.second.notebookLocalId().isEmpty())) {
            QNTRACE(
                "note_editor",
                "Detected note without notebook local id; "
                    << "will remove it from the cache: " << pair.second);
            noteLocalIdsToRemoveFromCache << pair.first;
            continue;
        }

        if (pair.second.notebookLocalId() == notebookLocalId) {
            noteLocalIdsToRemoveFromCache << pair.first;
        }
    }

    for (const auto & localId: qAsConst(noteLocalIdsToRemoveFromCache)) {
        Q_UNUSED(m_notesCache.remove(localId))
    }

    /**
     * The list of all notes removed along with the notebook is not known:
     * if we remove only those cached resources belonging to notes we have
     * removed from the cache, we still might have stale resources within the
     * cache so it's safer to just clear all cached resources
     */
    m_resourcesCache.clear();

    Q_EMIT notebookDeleted(notebookLocalId);
}

void NoteEditorLocalStorageBroker::onFindResourceComplete(
    qevercloud::Resource resource,
    LocalStorageManager::GetResourceOptions options,
    QUuid requestId)
{
    const auto it = m_findResourceRequestIds.find(requestId);
    if (it == m_findResourceRequestIds.end()) {
        return;
    }

    QNDEBUG(
        "note_editor",
        "NoteEditorLocalStorageBroker"
            << "::onFindResourceComplete: request id = " << requestId
            << ", with binary data = "
            << ((options &
                 LocalStorageManager::GetResourceOption::WithBinaryData)
                    ? "true"
                    : "false")
            << ", resource: " << resource);

    m_findResourceRequestIds.erase(it);

    qint32 totalBinaryDataSize = 0;
    if (resource.data() && resource.data()->size()) {
        totalBinaryDataSize += *resource.data()->size();
    }
    if (resource.alternateData() && resource.alternateData()->size()) {
        totalBinaryDataSize += *resource.alternateData()->size();
    }

    if (totalBinaryDataSize < MAX_TOTAL_RESOURCE_BINARY_DATA_SIZE_IN_BYTES) {
        m_resourcesCache.put(resource.localId(), resource);
    }

    Q_EMIT foundResourceData(resource);
}

void NoteEditorLocalStorageBroker::onFindResourceFailed(
    qevercloud::Resource resource,
    LocalStorageManager::GetResourceOptions options,
    ErrorString errorDescription, QUuid requestId)
{
    const auto it = m_findResourceRequestIds.find(requestId);
    if (it == m_findResourceRequestIds.end()) {
        return;
    }

    QNWARNING(
        "note_editor",
        "NoteEditorLocalStorageBroker"
            << "::onFindResourceFailed: request id = " << requestId
            << ", with binary data = "
            << ((options &
                 LocalStorageManager::GetResourceOption::WithBinaryData)
                    ? "true"
                    : "false")
            << ", error description = " << errorDescription
            << ", resource: " << resource);

    m_findResourceRequestIds.erase(it);
    Q_EMIT failedToFindResourceData(resource.localId(), errorDescription);
}

void NoteEditorLocalStorageBroker::onSwitchUserComplete(
    Account account, QUuid requestId)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorLocalStorageBroker::onSwitchUserComplete: "
            << "account = " << account << "\nRequest id = " << requestId);

    m_findNoteRequestIds.clear();
    m_findNotebookRequestIds.clear();
    m_findResourceRequestIds.clear();
    m_notesPendingSavingByFindNoteRequestIds.clear();
    m_notesPendingNotebookFindingByNotebookGuid.clear();
    m_notesPendingNotebookFindingByNotebookLocalId.clear();

    m_noteLocalIdsByAddResourceRequestIds.clear();
    m_noteLocalIdsByUpdateResourceRequestIds.clear();
    m_noteLocalIdsByExpungeResourceRequestIds.clear();

    m_notebooksCache.clear();
    m_notesCache.clear();
    m_resourcesCache.clear();

    m_saveNoteInfoByNoteLocalIds.clear();
    m_updateNoteRequestIds.clear();
}

void NoteEditorLocalStorageBroker::createConnections(
    LocalStorageManagerAsync & localStorageManagerAsync)
{
    QNDEBUG("note_editor", "NoteEditorLocalStorageBroker::createConnections");

    // Local signals to LocalStorageManagerAsync's slots
    QObject::connect(
        this, &NoteEditorLocalStorageBroker::updateNote,
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::onUpdateNoteRequest);

    QObject::connect(
        this, &NoteEditorLocalStorageBroker::addResource,
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::onAddResourceRequest);

    QObject::connect(
        this, &NoteEditorLocalStorageBroker::updateResource,
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::onUpdateResourceRequest);

    QObject::connect(
        this, &NoteEditorLocalStorageBroker::expungeResource,
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::onExpungeResourceRequest);

    QObject::connect(
        this, &NoteEditorLocalStorageBroker::findNote,
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::onFindNoteRequest);

    QObject::connect(
        this, &NoteEditorLocalStorageBroker::findNotebook,
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::onFindNotebookRequest);

    QObject::connect(
        this, &NoteEditorLocalStorageBroker::findResource,
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::onFindResourceRequest);

    // LocalStorageManagerAsync's signals to local slots
    QObject::connect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::updateNoteComplete, this,
        &NoteEditorLocalStorageBroker::onUpdateNoteComplete);

    QObject::connect(
        &localStorageManagerAsync, &LocalStorageManagerAsync::updateNoteFailed,
        this, &NoteEditorLocalStorageBroker::onUpdateNoteFailed);

    QObject::connect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::updateNotebookComplete, this,
        &NoteEditorLocalStorageBroker::onUpdateNotebookComplete);

    QObject::connect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::addResourceComplete, this,
        &NoteEditorLocalStorageBroker::onAddResourceComplete);

    QObject::connect(
        &localStorageManagerAsync, &LocalStorageManagerAsync::addResourceFailed,
        this, &NoteEditorLocalStorageBroker::onAddResourceFailed);

    QObject::connect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::updateResourceComplete, this,
        &NoteEditorLocalStorageBroker::onUpdateResourceComplete);

    QObject::connect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::updateResourceFailed, this,
        &NoteEditorLocalStorageBroker::onUpdateResourceFailed);

    QObject::connect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::expungeResourceComplete, this,
        &NoteEditorLocalStorageBroker::onExpungeResourceComplete);

    QObject::connect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::expungeResourceFailed, this,
        &NoteEditorLocalStorageBroker::onExpungeResourceFailed);

    QObject::connect(
        &localStorageManagerAsync, &LocalStorageManagerAsync::findNoteComplete,
        this, &NoteEditorLocalStorageBroker::onFindNoteComplete);

    QObject::connect(
        &localStorageManagerAsync, &LocalStorageManagerAsync::findNoteFailed,
        this, &NoteEditorLocalStorageBroker::onFindNoteFailed);

    QObject::connect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::findNotebookComplete, this,
        &NoteEditorLocalStorageBroker::onFindNotebookComplete);

    QObject::connect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::findNotebookFailed, this,
        &NoteEditorLocalStorageBroker::onFindNotebookFailed);

    QObject::connect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::findResourceComplete, this,
        &NoteEditorLocalStorageBroker::onFindResourceComplete);

    QObject::connect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::findResourceFailed, this,
        &NoteEditorLocalStorageBroker::onFindResourceFailed);

    QObject::connect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::expungeNoteComplete, this,
        &NoteEditorLocalStorageBroker::onExpungeNoteComplete);

    QObject::connect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::expungeNotebookComplete, this,
        &NoteEditorLocalStorageBroker::onExpungeNotebookComplete);

    QObject::connect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::switchUserComplete, this,
        &NoteEditorLocalStorageBroker::onSwitchUserComplete);
}

void NoteEditorLocalStorageBroker::disconnectFromLocalStorage(
    LocalStorageManagerAsync & localStorageManagerAsync)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorLocalStorageBroker"
            << "::disconnectFromLocalStorage");

    // Disconnect local signals from LocalStorageManagerAsync's slots
    QObject::disconnect(
        this, &NoteEditorLocalStorageBroker::updateNote,
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::onUpdateNoteRequest);

    QObject::disconnect(
        this, &NoteEditorLocalStorageBroker::addResource,
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::onAddResourceRequest);

    QObject::disconnect(
        this, &NoteEditorLocalStorageBroker::updateResource,
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::onUpdateResourceRequest);

    QObject::disconnect(
        this, &NoteEditorLocalStorageBroker::expungeResource,
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::onExpungeResourceRequest);

    QObject::disconnect(
        this, &NoteEditorLocalStorageBroker::findNote,
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::onFindNoteRequest);

    QObject::disconnect(
        this, &NoteEditorLocalStorageBroker::findNotebook,
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::onFindNotebookRequest);

    QObject::disconnect(
        this, &NoteEditorLocalStorageBroker::findResource,
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::onFindResourceRequest);

    // Disconnect LocalStorageManagerAsync's signals from local slots
    QObject::disconnect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::updateNoteComplete, this,
        &NoteEditorLocalStorageBroker::onUpdateNoteComplete);

    QObject::disconnect(
        &localStorageManagerAsync, &LocalStorageManagerAsync::updateNoteFailed,
        this, &NoteEditorLocalStorageBroker::onUpdateNoteFailed);

    QObject::disconnect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::updateNotebookComplete, this,
        &NoteEditorLocalStorageBroker::onUpdateNotebookComplete);

    QObject::disconnect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::addResourceComplete, this,
        &NoteEditorLocalStorageBroker::onAddResourceComplete);

    QObject::disconnect(
        &localStorageManagerAsync, &LocalStorageManagerAsync::addResourceFailed,
        this, &NoteEditorLocalStorageBroker::onAddResourceFailed);

    QObject::disconnect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::updateResourceComplete, this,
        &NoteEditorLocalStorageBroker::onUpdateResourceComplete);

    QObject::disconnect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::updateResourceFailed, this,
        &NoteEditorLocalStorageBroker::onUpdateResourceFailed);

    QObject::disconnect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::expungeResourceComplete, this,
        &NoteEditorLocalStorageBroker::onExpungeResourceComplete);

    QObject::disconnect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::expungeResourceFailed, this,
        &NoteEditorLocalStorageBroker::onExpungeResourceFailed);

    QObject::disconnect(
        &localStorageManagerAsync, &LocalStorageManagerAsync::findNoteComplete,
        this, &NoteEditorLocalStorageBroker::onFindNoteComplete);

    QObject::disconnect(
        &localStorageManagerAsync, &LocalStorageManagerAsync::findNoteFailed,
        this, &NoteEditorLocalStorageBroker::onFindNoteFailed);

    QObject::disconnect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::findNotebookComplete, this,
        &NoteEditorLocalStorageBroker::onFindNotebookComplete);

    QObject::disconnect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::findNotebookFailed, this,
        &NoteEditorLocalStorageBroker::onFindNotebookFailed);

    QObject::disconnect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::findResourceComplete, this,
        &NoteEditorLocalStorageBroker::onFindResourceComplete);

    QObject::disconnect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::findResourceFailed, this,
        &NoteEditorLocalStorageBroker::onFindResourceFailed);

    QObject::disconnect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::expungeNoteComplete, this,
        &NoteEditorLocalStorageBroker::onExpungeNoteComplete);

    QObject::disconnect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::expungeNotebookComplete, this,
        &NoteEditorLocalStorageBroker::onExpungeNotebookComplete);

    QObject::disconnect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::switchUserComplete, this,
        &NoteEditorLocalStorageBroker::onSwitchUserComplete);
}

void NoteEditorLocalStorageBroker::emitFindNoteRequest(
    const QString & noteLocalId)
{
    const QUuid requestId = QUuid::createUuid();
    Q_UNUSED(m_findNoteRequestIds.insert(requestId))

    qevercloud::Note note;
    note.setLocalId(noteLocalId);

    QNDEBUG(
        "note_editor",
        "Emitting the request to find note: request id = "
            << requestId << ", note local id = " << noteLocalId);

    LocalStorageManager::GetNoteOptions options(
        LocalStorageManager::GetNoteOption::WithResourceMetadata);

    Q_EMIT findNote(note, options, requestId);
}

void NoteEditorLocalStorageBroker::emitUpdateNoteRequest(
    const qevercloud::Note & note)
{
    // Remove the note from the cache for the time being - during the attempt to
    // update its state within the local storage its state is not really quite
    // consistent
    Q_UNUSED(m_notesCache.remove(note.localId()))

    const LocalStorageManager::UpdateNoteOptions options(
        LocalStorageManager::UpdateNoteOption::UpdateTags |
        LocalStorageManager::UpdateNoteOption::UpdateResourceMetadata);

    const QUuid requestId = QUuid::createUuid();
    Q_UNUSED(m_updateNoteRequestIds.insert(requestId))
    QNDEBUG(
        "note_editor",
        "Emitting the request to update note in the local "
            << "storage: request id = " << requestId << ", note: " << note);

    Q_EMIT updateNote(note, options, requestId);
}

void NoteEditorLocalStorageBroker::emitFindNotebookForNoteByLocalIdRequest(
    const QString & notebookLocalId, const qevercloud::Note & note)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorLocalStorageBroker::"
            << "emitFindNotebookForNoteByLocalIdRequest: "
            << "notebook local id = " << notebookLocalId
            << ", note local id = " << note.localId());

    qevercloud::Notebook notebook;
    notebook.setLocalId(notebookLocalId);

    emitFindNotebookForNoteRequest(
        notebook, note, m_notesPendingNotebookFindingByNotebookLocalId);
}

void NoteEditorLocalStorageBroker::emitFindNotebookForNoteByGuidRequest(
    const QString & notebookGuid, const qevercloud::Note & note)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorLocalStorageBroker::"
            << "emitFindNotebookForNoteByGuidRequest: "
            << "notebook guid = " << notebookGuid
            << ", note local id = " << note.localId());

    qevercloud::Notebook notebook;
    notebook.setGuid(notebookGuid);

    emitFindNotebookForNoteRequest(
        notebook, note, m_notesPendingNotebookFindingByNotebookGuid);
}

void NoteEditorLocalStorageBroker::emitFindNotebookForNoteRequest(
    const qevercloud::Notebook & notebook, const qevercloud::Note & note,
    NotesPendingNotebookFindingHash & notesPendingNotebookFinding)
{
    const QString id =
        notebook.guid() ? *notebook.guid() : notebook.localId();

    const QString noteLocalId = note.localId();

    const auto it = notesPendingNotebookFinding.find(id);
    if (it != notesPendingNotebookFinding.end()) {
        QNDEBUG(
            "note_editor",
            "Adding note with local id " << noteLocalId
                << " to the list of those pending finding notebook with "
                << (notebook.guid() ? "guid " : "local id ") << id);

        NotesHash & notes = it.value();
        notes[noteLocalId] = note;
        return;
    }

    const QUuid requestId = QUuid::createUuid();
    Q_UNUSED(m_findNotebookRequestIds.insert(requestId))

    NotesHash & notes = notesPendingNotebookFinding[id];
    notes[noteLocalId] = note;

    QNDEBUG(
        "note_editor",
        "Emitting the request to find notebook: "
            << "request id = " << requestId << ", notebook: " << notebook);

    Q_EMIT findNotebook(notebook, requestId);
}

void NoteEditorLocalStorageBroker::saveNoteToLocalStorageImpl(
    const qevercloud::Note & previousNoteVersion,
    const qevercloud::Note & updatedNoteVersion)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorLocalStorageBroker::saveNoteToLocalStorageImpl");

    QNTRACE(
        "note_editor",
        "Previous note version: " << previousNoteVersion
                                  << "\nUpdated note version: "
                                  << updatedNoteVersion);

    auto previousNoteResources =
        (previousNoteVersion.resources()
         ? *previousNoteVersion.resources()
         : QList<qevercloud::Resource>());

    QList<qevercloud::Resource> newResources;
    QList<qevercloud::Resource> updatedResources;

    auto resources =
        (updatedNoteVersion.resources()
         ? *updatedNoteVersion.resources()
         : QList<qevercloud::Resource>());

    for (const auto & resource: qAsConst(resources)) {
        if (!resource.data() || !resource.data()->body()) {
            continue;
        }

        const QString resourceLocalId = resource.localId();
        bool foundResourceInPreviousNoteVersion = false;
        bool resourceDataSizeOrHashChanged = false;

        for (const auto & prevResource: qAsConst(previousNoteResources)) {
            if (prevResource.localId() != resourceLocalId) {
                continue;
            }

            foundResourceInPreviousNoteVersion = true;

            bool dataSizeEqual = true;
            if (((prevResource.data() && prevResource.data()->size()) !=
                 (resource.data() && resource.data()->size())) ||
                (prevResource.data() && resource.data() &&
                 (prevResource.data()->size() != resource.data()->size())))
            {
                dataSizeEqual = false;
            }

            bool dataHashEqual = true;
            if (dataSizeEqual &&
                (((prevResource.data() && prevResource.data()->bodyHash()) !=
                  (resource.data() && resource.data()->bodyHash())) ||
                 (prevResource.data() && resource.data() &&
                  (prevResource.data()->bodyHash() !=
                   resource.data()->bodyHash()))))
            {
                dataHashEqual = false;
            }

            bool alternateDataSizeEqual = true;
            if (dataSizeEqual && dataHashEqual &&
                (((prevResource.alternateData() && prevResource.alternateData()->size()) !=
                  (resource.alternateData() && resource.alternateData()->size())) ||
                 (prevResource.alternateData() && resource.alternateData() &&
                  (prevResource.alternateData()->size() !=
                   resource.alternateData()->size()))))
            {
                alternateDataSizeEqual = false;
            }

            bool alternateDataHashEqual = true;
            if (dataSizeEqual && dataHashEqual && alternateDataSizeEqual &&
                (((prevResource.alternateData() && prevResource.alternateData()->bodyHash()) !=
                  (resource.alternateData() && resource.alternateData()->bodyHash())) ||
                 (prevResource.alternateData() && resource.alternateData() &&
                  (prevResource.alternateData()->bodyHash() !=
                   resource.alternateData()->bodyHash()))))
            {
                alternateDataHashEqual = false;
            }

            if (!dataSizeEqual || !dataHashEqual || !alternateDataSizeEqual ||
                !alternateDataHashEqual)
            {
                resourceDataSizeOrHashChanged = true;
            }

            break;
        }

        if (foundResourceInPreviousNoteVersion) {
            if (!resourceDataSizeOrHashChanged) {
                QNTRACE(
                    "note_editor",
                    "Resource with local id " << resource.localId()
                        << " has not changed since the previous note version");
                continue;
            }

            QNTRACE(
                "note_editor",
                "Resource with local id " << resource.localId()
                    << " has changed since the previous note version");
            updatedResources << resource;
        }
        else {
            QNTRACE(
                "note_editor",
                "Resource with local id " << resource.localId()
                    << " did not appear in the previous note version");
            newResources << resource;
        }
    }

    QStringList expungedResourcesLocalIds;
    for (const auto & previousNoteResource: qAsConst(previousNoteResources)) {
        const QString previousNoteResourceLocalId =
            previousNoteResource.localId();

        bool foundResource = false;
        for (const auto & resource: qAsConst(resources)) {
            if (previousNoteResourceLocalId == resource.localId()) {
                foundResource = true;
                break;
            }
        }

        if (!foundResource) {
            QNTRACE(
                "note_editor",
                "Resource with local id "
                    << previousNoteResourceLocalId
                    << " no longer appears within the new note version");
            expungedResourcesLocalIds << previousNoteResourceLocalId;
        }
    }

    const QString noteLocalId = updatedNoteVersion.localId();

    const int numAddResourceRequests = newResources.size();
    const int numUpdateResourceRequests = updatedResources.size();
    const int numExpungeResourceRequests = expungedResourcesLocalIds.size();

    auto saveNoteInfoIt = m_saveNoteInfoByNoteLocalIds.find(noteLocalId);
    if (saveNoteInfoIt == m_saveNoteInfoByNoteLocalIds.end()) {
        saveNoteInfoIt =
            m_saveNoteInfoByNoteLocalIds.insert(noteLocalId, SaveNoteInfo());
    }

    SaveNoteInfo & info = saveNoteInfoIt.value();
    info.m_notePendingSaving = updatedNoteVersion;

    info.m_pendingAddResourceRequests +=
        static_cast<quint32>(std::max(numAddResourceRequests, 0));

    info.m_pendingUpdateResourceRequests +=
        static_cast<quint32>(std::max(numUpdateResourceRequests, 0));

    info.m_pendingExpungeResourceRequests +=
        static_cast<quint32>(std::max(numExpungeResourceRequests, 0));

    m_saveNoteInfoByNoteLocalIds[noteLocalId] = info;
    QNTRACE(
        "note_editor",
        "Added pending save note info for note local id " << noteLocalId
                                                          << ": " << info);

    for (const auto & resource: qAsConst(newResources)) {
        const QUuid requestId = QUuid::createUuid();
        m_noteLocalIdsByAddResourceRequestIds[requestId] = noteLocalId;

        QNDEBUG(
            "note_editor",
            "Emitting the request to add resource to "
                << "the local storage: request id = " << requestId
                << ", resource: " << resource);

        Q_EMIT addResource(resource, requestId);
    }

    for (const auto & resource: qAsConst(updatedResources)) {
        const QUuid requestId = QUuid::createUuid();
        m_noteLocalIdsByUpdateResourceRequestIds[requestId] = noteLocalId;

        QNDEBUG(
            "note_editor",
            "Emitting the request to update resource in "
                << "the local storage: request id = " << requestId
                << ", resource: " << resource);

        Q_EMIT updateResource(resource, requestId);
    }

    for (const auto & resourceLocalId: qAsConst(expungedResourcesLocalIds)) {
        qevercloud::Resource dummyResource;
        dummyResource.setLocalId(resourceLocalId);

        const QUuid requestId = QUuid::createUuid();
        m_noteLocalIdsByExpungeResourceRequestIds[requestId] = noteLocalId;

        QNDEBUG(
            "note_editor",
            "Emitting the request to expunge resource from "
                << "the local storage: request id = " << requestId
                << ", resource local id = " << resourceLocalId);

        Q_EMIT expungeResource(dummyResource, requestId);
    }

    if ((numAddResourceRequests > 0) || (numUpdateResourceRequests > 0) ||
        (numExpungeResourceRequests > 0))
    {
        QNDEBUG("note_editor", "Pending note saving: " << info);
        return;
    }

    emitUpdateNoteRequest(updatedNoteVersion);
}

QTextStream & NoteEditorLocalStorageBroker::SaveNoteInfo::print(
    QTextStream & strm) const
{
    strm << "SaveNoteInfo: \n"
         << "pending add resource requests: " << m_pendingAddResourceRequests
         << "\n"
         << ", pending update resource requests: "
         << m_pendingUpdateResourceRequests << "\n"
         << ", pending expunge resource requests: "
         << m_pendingExpungeResourceRequests << "\n"
         << ",  note: " << m_notePendingSaving << "\n";
    return strm;
}

bool NoteEditorLocalStorageBroker::SaveNoteInfo::hasPendingResourceOperations()
    const
{
    if ((m_pendingAddResourceRequests != 0) ||
        (m_pendingUpdateResourceRequests != 0) ||
        (m_pendingExpungeResourceRequests != 0))
    {
        QNDEBUG(
            "note_editor",
            "Still pending for "
                << m_pendingAddResourceRequests
                << " add resource requests and/or "
                << m_pendingUpdateResourceRequests
                << " update resource requests and/or "
                << m_pendingExpungeResourceRequests
                << " expunge resource requests for note with local id "
                << m_notePendingSaving.localId());
        return true;
    }

    return false;
}

} // namespace quentier
