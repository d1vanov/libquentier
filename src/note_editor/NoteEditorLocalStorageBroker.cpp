/*
 * Copyright 2018-2020 Dmitry Ivanov
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
#include <quentier/utility/Compat.h>

#include <algorithm>

// 10 Mb
#define MAX_TOTAL_RESOURCE_BINARY_DATA_SIZE_IN_BYTES (10485760)

namespace quentier {

namespace {

bool compareResourcesWithoutBinaryData(
    Resource lhs, Resource rhs)
{
    const auto clearBinaryData = [](Resource & resource)
    {
        if (resource.hasDataBody()) {
            resource.setDataBody(QByteArray{});
        }

        if (resource.hasAlternateDataBody()) {
            resource.setAlternateDataBody(QByteArray{});
        }
    };

    clearBinaryData(lhs);
    clearBinaryData(rhs);
    return lhs == rhs;
}

bool checkIfNoteResourcesChanged(
    const QList<Resource> & previousNoteVersionResources,
    const QList<Resource> & currentNoteVersionResources)
{
    if (previousNoteVersionResources.size() != currentNoteVersionResources.size()) {
        return true;
    }

    for (const auto & resource: qAsConst(currentNoteVersionResources)) {
        const auto it = std::find_if(
            previousNoteVersionResources.begin(),
            previousNoteVersionResources.end(),
            [&resource](const Resource & previousResource)
            {
                return resource.localUid() == previousResource.localUid();
            });
        if (it == previousNoteVersionResources.end()) {
            return true;
        }

        if (!compareResourcesWithoutBinaryData(resource, *it)) {
            return true;
        }
    }

    return false;
}

} // namespace

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

void NoteEditorLocalStorageBroker::saveNoteToLocalStorage(const Note & note)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorLocalStorageBroker"
            << "::saveNoteToLocalStorage: note local uid = "
            << note.localUid());

    const Note * pCachedNote = m_notesCache.get(note.localUid());
    if (!pCachedNote) {
        QNTRACE(
            "note_editor",
            "Haven't found the note to be saved within "
                << "the cache");

        QUuid requestId = QUuid::createUuid();
        m_notesPendingSavingByFindNoteRequestIds[requestId] = note;
        Note dummy;
        dummy.setLocalUid(note.localUid());

        QNDEBUG(
            "note_editor",
            "Emitting the request to find note for the sake "
                << "of resource list updates resolution");

        LocalStorageManager::GetNoteOptions options(
            LocalStorageManager::GetNoteOption::WithResourceMetadata);

        Q_EMIT findNote(dummy, options, requestId);
        return;
    }

    saveNoteToLocalStorageImpl(*pCachedNote, note);
}

void NoteEditorLocalStorageBroker::findNoteAndNotebook(
    const QString & noteLocalUid)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorLocalStorageBroker::findNoteAndNotebook: "
            << "note local uid = " << noteLocalUid);

    const Note * pCachedNote = m_notesCache.get(noteLocalUid);
    if (!pCachedNote) {
        QNDEBUG(
            "note_editor",
            "Note was not found within the cache, looking "
                << "it up in the local storage");
        emitFindNoteRequest(noteLocalUid);
        return;
    }

    if (Q_UNLIKELY(
            !pCachedNote->hasNotebookLocalUid() &&
            !pCachedNote->hasNotebookGuid()))
    {
        Q_UNUSED(m_notesCache.remove(noteLocalUid))
        QNDEBUG(
            "note_editor",
            "The note within the cache contained neither "
                << "notebook local uid nor notebook guid, looking it up in "
                << "the local storage");
        emitFindNoteRequest(noteLocalUid);
        return;
    }

    if (pCachedNote->hasNotebookLocalUid()) {
        const QString & notebookLocalUid = pCachedNote->notebookLocalUid();

        const auto * pCachedNotebook = m_notebooksCache.get(notebookLocalUid);
        if (pCachedNotebook) {
            QNDEBUG(
                "note_editor", "Found both note and notebook within caches");
            Q_EMIT foundNoteAndNotebook(*pCachedNote, *pCachedNotebook);
        }
        else {
            QNDEBUG(
                "note_editor",
                "Notebook was not found within the cache, "
                    << "looking it up in local storage");

            emitFindNotebookForNoteByLocalUidRequest(
                notebookLocalUid, *pCachedNote);
        }

        return;
    }

    emitFindNotebookForNoteByGuidRequest(
        pCachedNote->notebookGuid(), *pCachedNote);
}

void NoteEditorLocalStorageBroker::findResourceData(
    const QString & resourceLocalUid)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorLocalStorageBroker::findResourceData: "
            << "resource local uid = " << resourceLocalUid);

    const auto * pCachedResource = m_resourcesCache.get(resourceLocalUid);
    if (pCachedResource) {
        QNDEBUG("note_editor", "Found cached resource binary data");
        Q_EMIT foundResourceData(*pCachedResource);
        return;
    }

    QUuid requestId = QUuid::createUuid();
    Q_UNUSED(m_findResourceRequestIds.insert(requestId))

    Resource resource;
    resource.setLocalUid(resourceLocalUid);

    QNDEBUG(
        "note_editor",
        "Emitting the request to find resource: "
            << "request id = " << requestId
            << ", resource local uid = " << resourceLocalUid);

    LocalStorageManager::GetResourceOptions options(
        LocalStorageManager::GetResourceOption::WithBinaryData);

    Q_EMIT findResource(resource, options, requestId);
}

void NoteEditorLocalStorageBroker::onUpdateNoteComplete(
    Note note, LocalStorageManager::UpdateNoteOptions options, QUuid requestId)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorLocalStorageBroker::onUpdateNoteComplete: "
            << "request id = " << requestId << ", options = " << options
            << ", note: " << note);

    if (m_notesCache.exists(note.localUid())) {
        if (!note.hasResources()) {
            m_notesCache.put(note.localUid(), note);
        }
        else {
            auto resources = note.resources();
            for (auto & resource: resources) {
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
        QNDEBUG(
            "note_editor",
            "Note was successfully saved within "
                << "the local storage");
        m_updateNoteRequestIds.erase(it);
        Q_EMIT noteSavedToLocalStorage(note.localUid());
        return;
    }

    Q_EMIT noteUpdated(note);
}

void NoteEditorLocalStorageBroker::onUpdateNoteFailed(
    Note note, LocalStorageManager::UpdateNoteOptions options,
    ErrorString errorDescription, QUuid requestId)
{
    auto it = m_updateNoteRequestIds.find(requestId);
    if (it == m_updateNoteRequestIds.end()) {
        return;
    }

    QNWARNING(
        "note_editor",
        "Failed to update the note within "
            << "the local storage: " << errorDescription << ", note: " << note
            << "\nUpdate options: " << options
            << ", request id = " << requestId);

    m_updateNoteRequestIds.erase(it);
    Q_EMIT failedToSaveNoteToLocalStorage(note.localUid(), errorDescription);
}

void NoteEditorLocalStorageBroker::onUpdateNotebookComplete(
    Notebook notebook, QUuid requestId)
{
    Q_UNUSED(requestId)

    QString notebookLocalUid = notebook.localUid();
    if (m_notebooksCache.exists(notebookLocalUid)) {
        m_notebooksCache.put(notebookLocalUid, notebook);
    }

    Q_EMIT notebookUpdated(notebook);
}

void NoteEditorLocalStorageBroker::onFindNoteComplete(
    Note foundNote, LocalStorageManager::GetNoteOptions options,
    QUuid requestId)
{
    auto it = m_findNoteRequestIds.find(requestId);
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
                !foundNote.hasNotebookLocalUid() &&
                !foundNote.hasNotebookGuid()))
        {
            ErrorString errorDescription(
                QT_TR_NOOP("note doesn't belong to any notebook"));

            APPEND_NOTE_DETAILS(errorDescription, foundNote)
            QNWARNING(
                "note_editor", errorDescription << ", note: " << foundNote);

            Q_EMIT failedToFindNoteOrNotebook(
                foundNote.localUid(), errorDescription);
            return;
        }

        m_notesCache.put(foundNote.localUid(), foundNote);

        if (foundNote.hasNotebookLocalUid()) {
            const QString & notebookLocalUid = foundNote.notebookLocalUid();

            const auto * pCachedNotebook =
                m_notebooksCache.get(notebookLocalUid);

            if (pCachedNotebook) {
                QNDEBUG("note_editor", "Found notebook within the cache");
                Q_EMIT foundNoteAndNotebook(foundNote, *pCachedNotebook);
            }
            else {
                QNDEBUG(
                    "note_editor",
                    "Notebook was not found within "
                        << "the cache, looking it up in local storage");

                emitFindNotebookForNoteByLocalUidRequest(
                    notebookLocalUid, foundNote);
            }

            return;
        }

        emitFindNotebookForNoteByGuidRequest(
            foundNote.notebookGuid(), foundNote);

        return;
    }

    auto pit = m_notesPendingSavingByFindNoteRequestIds.find(requestId);
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

        Note updatedNote = pit.value();
        Q_UNUSED(m_notesPendingSavingByFindNoteRequestIds.erase(pit))
        saveNoteToLocalStorageImpl(foundNote, updatedNote);
        return;
    }
}

void NoteEditorLocalStorageBroker::onFindNoteFailed(
    Note note, LocalStorageManager::GetNoteOptions options,
    ErrorString errorDescription, QUuid requestId)
{
    auto it = m_findNoteRequestIds.find(requestId);
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
    Q_EMIT failedToFindNoteOrNotebook(note.localUid(), errorDescription);
}

void NoteEditorLocalStorageBroker::onFindNotebookComplete(
    Notebook foundNotebook, QUuid requestId)
{
    auto it = m_findNotebookRequestIds.find(requestId);
    if (it == m_findNotebookRequestIds.end()) {
        return;
    }

    QNDEBUG(
        "note_editor",
        "NoteEditorLocalStorageBroker"
            << "::onFindNotebookComplete: request id = " << requestId
            << ", notebook: " << foundNotebook);

    m_findNotebookRequestIds.erase(it);

    QString notebookLocalUid = foundNotebook.localUid();
    m_notebooksCache.put(notebookLocalUid, foundNotebook);

    bool foundNotesPendingNotebookFinding = true;
    bool foundByNotebookGuid = false;

    auto pendingNotesIt =
        m_notesPendingNotebookFindingByNotebookLocalUid.find(notebookLocalUid);

    if (pendingNotesIt == m_notesPendingNotebookFindingByNotebookLocalUid.end())
    {
        // Maybe this notebook was searched by guid
        if (foundNotebook.hasGuid()) {
            pendingNotesIt = m_notesPendingNotebookFindingByNotebookGuid.find(
                foundNotebook.guid());

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
    for (const auto noteIt: qevercloud::toRange(notes)) {
        QNTRACE(
            "note_editor",
            "Found pending note, emitting "
                << "foundNoteAndNotebook signal: note local uid = "
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

void NoteEditorLocalStorageBroker::onFindNotebookFailed(
    Notebook notebook, ErrorString errorDescription, QUuid requestId)
{
    auto it = m_findNotebookRequestIds.find(requestId);
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

    QString notebookLocalUid = notebook.localUid();
    bool foundNotesPendingNotebookFinding = true;
    bool foundByNotebookGuid = false;

    auto pendingNotesIt =
        m_notesPendingNotebookFindingByNotebookLocalUid.find(notebookLocalUid);

    if (pendingNotesIt == m_notesPendingNotebookFindingByNotebookLocalUid.end())
    {
        // Maybe this notebook was searched by guid
        if (notebook.hasGuid()) {
            pendingNotesIt = m_notesPendingNotebookFindingByNotebookGuid.find(
                notebook.guid());

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
    for (const auto noteIt: qevercloud::toRange(notes)) {
        Q_EMIT failedToFindNoteOrNotebook(
            noteIt.value().localUid(), errorDescription);
    }

    if (foundByNotebookGuid) {
        m_notesPendingNotebookFindingByNotebookGuid.erase(pendingNotesIt);
    }
    else {
        m_notesPendingNotebookFindingByNotebookLocalUid.erase(pendingNotesIt);
    }
}

void NoteEditorLocalStorageBroker::onExpungeNoteComplete(
    Note note, QUuid requestId)
{
    Q_UNUSED(requestId)
    QString noteLocalUid = note.localUid();
    Q_UNUSED(m_notesCache.remove(noteLocalUid))

    QStringList resourceLocalUidsToRemoveFromCache;
    for (const auto & pair: m_resourcesCache) {
        if (Q_UNLIKELY(!pair.second.hasNoteLocalUid())) {
            QNTRACE(
                "note_editor",
                "Detected resource without note local uid; "
                    << "will remove it from the cache: " << pair.second);
            resourceLocalUidsToRemoveFromCache << pair.first;
            continue;
        }

        if (pair.second.noteLocalUid() == noteLocalUid) {
            resourceLocalUidsToRemoveFromCache << pair.first;
        }
    }

    for (const auto & localUid: qAsConst(resourceLocalUidsToRemoveFromCache)) {
        Q_UNUSED(m_resourcesCache.remove(localUid))
    }

    Q_EMIT noteDeleted(noteLocalUid);
}

void NoteEditorLocalStorageBroker::onExpungeNotebookComplete(
    Notebook notebook, QUuid requestId)
{
    Q_UNUSED(requestId)
    QString notebookLocalUid = notebook.localUid();
    Q_UNUSED(m_notebooksCache.remove(notebookLocalUid))

    QStringList noteLocalUidsToRemoveFromCache;
    for (const auto & pair: m_notesCache) {
        if (Q_UNLIKELY(!pair.second.hasNotebookLocalUid())) {
            QNTRACE(
                "note_editor",
                "Detected note without notebook local uid; "
                    << "will remove it from the cache: " << pair.second);
            noteLocalUidsToRemoveFromCache << pair.first;
            continue;
        }

        if (pair.second.notebookLocalUid() == notebookLocalUid) {
            noteLocalUidsToRemoveFromCache << pair.first;
        }
    }

    for (const auto & localUid: qAsConst(noteLocalUidsToRemoveFromCache)) {
        Q_UNUSED(m_notesCache.remove(localUid))
    }

    /**
     * The list of all notes removed along with the notebook is not known:
     * if we remove only those cached resources belonging to notes we have
     * removed from the cache, we still might have stale resources within the
     * cache so it's safer to just clear all cached resources
     */
    m_resourcesCache.clear();

    Q_EMIT notebookDeleted(notebookLocalUid);
}

void NoteEditorLocalStorageBroker::onFindResourceComplete(
    Resource resource, LocalStorageManager::GetResourceOptions options,
    QUuid requestId)
{
    auto it = m_findResourceRequestIds.find(requestId);
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

void NoteEditorLocalStorageBroker::onFindResourceFailed(
    Resource resource, LocalStorageManager::GetResourceOptions options,
    ErrorString errorDescription, QUuid requestId)
{
    auto it = m_findResourceRequestIds.find(requestId);
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
    Q_EMIT failedToFindResourceData(resource.localUid(), errorDescription);
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
    m_notesPendingNotebookFindingByNotebookLocalUid.clear();

    m_notebooksCache.clear();
    m_notesCache.clear();
    m_resourcesCache.clear();

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
    const QString & noteLocalUid)
{
    QUuid requestId = QUuid::createUuid();
    Q_UNUSED(m_findNoteRequestIds.insert(requestId))
    Note note;
    note.setLocalUid(noteLocalUid);

    QNDEBUG(
        "note_editor",
        "Emitting the request to find note: request id = "
            << requestId << ", note local uid = " << noteLocalUid);

    LocalStorageManager::GetNoteOptions options(
        LocalStorageManager::GetNoteOption::WithResourceMetadata);

    Q_EMIT findNote(note, options, requestId);
}

void NoteEditorLocalStorageBroker::emitUpdateNoteRequest(
    const Note & note, const UpdateNoteOption option)
{
    // Remove the note from the cache for the time being - during the attempt to
    // update its state within the local storage its state is not really quite
    // consistent
    Q_UNUSED(m_notesCache.remove(note.localUid()))

    LocalStorageManager::UpdateNoteOptions options(
        LocalStorageManager::UpdateNoteOption::UpdateTags |
        LocalStorageManager::UpdateNoteOption::UpdateResourceMetadata);

    if (option == UpdateNoteOption::WithResourceBinaryData) {
        options |=
            LocalStorageManager::UpdateNoteOption::UpdateResourceBinaryData;
    }

    QUuid requestId = QUuid::createUuid();
    Q_UNUSED(m_updateNoteRequestIds.insert(requestId))
    QNDEBUG(
        "note_editor",
        "Emitting the request to update note in the local "
            << "storage: request id = " << requestId << ", note: " << note);

    Q_EMIT updateNote(note, options, requestId);
}

void NoteEditorLocalStorageBroker::emitFindNotebookForNoteByLocalUidRequest(
    const QString & notebookLocalUid, const Note & note)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorLocalStorageBroker::"
            << "emitFindNotebookForNoteByLocalUidRequest: "
            << "notebook local uid = " << notebookLocalUid
            << ", note local uid = " << note.localUid());

    Notebook notebook;
    notebook.setLocalUid(notebookLocalUid);

    emitFindNotebookForNoteRequest(
        notebook, note, m_notesPendingNotebookFindingByNotebookLocalUid);
}

void NoteEditorLocalStorageBroker::emitFindNotebookForNoteByGuidRequest(
    const QString & notebookGuid, const Note & note)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorLocalStorageBroker::"
            << "emitFindNotebookForNoteByGuidRequest: "
            << "notebook guid = " << notebookGuid
            << ", note local uid = " << note.localUid());

    Notebook notebook;
    notebook.setGuid(notebookGuid);

    emitFindNotebookForNoteRequest(
        notebook, note, m_notesPendingNotebookFindingByNotebookGuid);
}

void NoteEditorLocalStorageBroker::emitFindNotebookForNoteRequest(
    const Notebook & notebook, const Note & note,
    NotesPendingNotebookFindingHash & notesPendingNotebookFinding)
{
    const QString id =
        notebook.hasGuid() ? notebook.guid() : notebook.localUid();

    const QString noteLocalUid = note.localUid();

    auto it = notesPendingNotebookFinding.find(id);
    if (it != notesPendingNotebookFinding.end()) {
        QNDEBUG(
            "note_editor",
            "Adding note with local uid "
                << noteLocalUid
                << " to the list of those pending finding notebook with "
                << (notebook.hasGuid() ? "guid " : "local uid ") << id);

        NotesHash & notes = it.value();
        notes[noteLocalUid] = note;
        return;
    }

    QUuid requestId = QUuid::createUuid();
    Q_UNUSED(m_findNotebookRequestIds.insert(requestId))

    NotesHash & notes = notesPendingNotebookFinding[id];
    notes[noteLocalUid] = note;

    QNDEBUG(
        "note_editor",
        "Emitting the request to find notebook: "
            << "request id = " << requestId << ", notebook: " << notebook);

    Q_EMIT findNotebook(notebook, requestId);
}

void NoteEditorLocalStorageBroker::saveNoteToLocalStorageImpl(
    const Note & previousNoteVersion, const Note & updatedNoteVersion)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorLocalStorageBroker"
            << "::saveNoteToLocalStorageImpl");

    QNTRACE(
        "note_editor",
        "Previous note version: " << previousNoteVersion
                                  << "\nUpdated note version: "
                                  << updatedNoteVersion);

    const auto previousNoteResources = previousNoteVersion.resources();
    const auto resources = updatedNoteVersion.resources();

    if (!checkIfNoteResourcesChanged(previousNoteResources, resources)) {
        emitUpdateNoteRequest(
            updatedNoteVersion, UpdateNoteOption::WithoutResourceBinaryData);
    }
    else {
        emitUpdateNoteRequest(
            updatedNoteVersion, UpdateNoteOption::WithResourceBinaryData);
    }
}

} // namespace quentier
