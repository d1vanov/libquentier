/*
 * Copyright 2022 Dmitry Ivanov
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

#include "NoteResourcesBinaryDataFetcher.h"

#include <quentier/logging/QuentierLogger.h>

#include <algorithm>

namespace quentier {

NoteResourcesBinaryDataFetcher::NoteResourcesBinaryDataFetcher(
    LocalStorageManagerAsync & localStorageManagerAsync, QObject * parent) :
    QObject(parent)
{
    createConnections(localStorageManagerAsync);
}

void NoteResourcesBinaryDataFetcher::onFetchResourceBinaryData(
    Note note, QUuid requestId)
{
    QNDEBUG(
        "note_editor",
        "NoteResourcesBinaryDataFetcher::onFetchResourceBinaryData: note "
            << note.localUid() << ", request id " << requestId);

    if (!note.hasResources()) {
        QNDEBUG("note_editor", "Note has no resources");
        Q_EMIT finished(note, requestId);
        return;
    }

    const auto resources = note.resources();
    QList<Resource> resourcesWithoutBinaryData;
    for (const auto & resource: qAsConst(resources)) {
        const bool dataBodyOk = (!resource.hasData() || resource.hasDataBody());

        const bool alternateDataBodyOk =
            (!resource.hasAlternateData() || resource.hasAlternateDataBody());

        if (dataBodyOk && alternateDataBodyOk) {
            continue;
        }

        resourcesWithoutBinaryData << resource;
    }

    if (resourcesWithoutBinaryData.isEmpty()) {
        QNDEBUG("note_editor", "Note has no resources lacking binary data");
        Q_EMIT finished(note, requestId);
        return;
    }

    const LocalStorageManager::GetResourceOptions getResourceOptions{
        LocalStorageManager::GetResourceOption::WithBinaryData};

    const QString noteLocalUid = note.localUid();
    auto & noteData = m_noteDataByLocalUid[noteLocalUid];
    noteData.m_note = note;
    noteData.m_requestId = requestId;

    for (const auto & resource: qAsConst(resourcesWithoutBinaryData)) {
        const QUuid findResourceRequestId = QUuid::createUuid();
        QNDEBUG(
            "note_editor",
            "Emitting the request to find resource with binary data: "
                << findResourceRequestId
                << ", resource local uid = " << resource.localUid()
                << ", note local uid = " << noteLocalUid);

        m_findResourceRequestIdToNoteLocalUid[findResourceRequestId] =
            noteLocalUid;

        noteData.m_findResourceRequestIds.insert(findResourceRequestId);

        Q_EMIT findResource(
            resource, getResourceOptions, findResourceRequestId);
    }
}

void NoteResourcesBinaryDataFetcher::onFindResourceComplete(
    Resource resource, LocalStorageManager::GetResourceOptions options,
    QUuid requestId)
{
    Q_UNUSED(options)

    const auto it = m_findResourceRequestIdToNoteLocalUid.find(requestId);
    if (it == m_findResourceRequestIdToNoteLocalUid.end()) {
        return;
    }

    QNDEBUG(
        "note_editor",
        "NoteResourcesBinaryDataFetcher::onFindResourceComplete: request id = "
            << requestId);

    const QString noteLocalUid = it.value();
    m_findResourceRequestIdToNoteLocalUid.erase(it);

    auto noteDataIt = m_noteDataByLocalUid.find(noteLocalUid);
    if (Q_UNLIKELY(noteDataIt == m_noteDataByLocalUid.end())) {
        QNWARNING(
            "note_editor",
            "NoteResourcesBinaryDataFetcher::onFindResourceComplete: cannot "
                << "find note by local uid: " << noteLocalUid);
        return;
    }

    auto & noteData = noteDataIt.value();
    auto resources = noteData.m_note.resources();
    const auto resourceIt = std::find_if(
        resources.begin(),
        resources.end(),
        [resourceLocalUid = resource.localUid()](const Resource & resource)
        {
            return resource.localUid() == resourceLocalUid;
        });
    if (Q_UNLIKELY(resourceIt == resources.end())) {
        QNWARNING(
            "note_editor",
            "NoteResourcesBinaryDataFetcher::onFindResourceComplete: cannot "
                << "find resource within the note by local uid");
    }
    else {
        *resourceIt = resource;
        noteData.m_note.setResources(resources);
    }

    auto requestIdIt = noteData.m_findResourceRequestIds.find(requestId);
    if (requestIdIt != noteData.m_findResourceRequestIds.end()) {
        noteData.m_findResourceRequestIds.erase(requestIdIt);
    }

    if (noteData.m_findResourceRequestIds.empty()) {
        QNDEBUG(
            "note_editor",
            "NoteResourcesBinaryDataFetcher::onFindResourceComplete: completed "
                << "find resource tasks for all relevant resources, finished "
                << "processing for request id " << noteData.m_requestId
                << ", note local uid " << noteData.m_note.localUid());

        auto fetchResourcesRequestId = noteData.m_requestId;
        auto note = noteData.m_note;
        m_noteDataByLocalUid.erase(noteDataIt);
        Q_EMIT finished(note, fetchResourcesRequestId);
    }
}

void NoteResourcesBinaryDataFetcher::onFindResourceFailed(
    Resource resource, LocalStorageManager::GetResourceOptions options,
    ErrorString errorDescription, QUuid requestId)
{
    Q_UNUSED(resource)
    Q_UNUSED(options)

    const auto it = m_findResourceRequestIdToNoteLocalUid.find(requestId);
    if (it == m_findResourceRequestIdToNoteLocalUid.end()) {
        return;
    }

    QNDEBUG(
        "note_editor",
        "NoteResourcesBinaryDataFetcher::onFindResourceComplete: request id = "
            << requestId);

    const QString noteLocalUid = it.value();
    m_findResourceRequestIdToNoteLocalUid.erase(it);

    auto noteDataIt = m_noteDataByLocalUid.find(noteLocalUid);
    if (Q_UNLIKELY(noteDataIt == m_noteDataByLocalUid.end())) {
        QNWARNING(
            "note_editor",
            "NoteResourcesBinaryDataFetcher::onFindResourceFailed: cannot "
                << "find note by local uid: " << noteLocalUid);
        return;
    }

    auto & noteData = noteDataIt.value();

    for (auto id: qAsConst(noteData.m_findResourceRequestIds)) {
        const auto idIt = m_findResourceRequestIdToNoteLocalUid.find(id);
        if (idIt != m_findResourceRequestIdToNoteLocalUid.end()) {
            m_findResourceRequestIdToNoteLocalUid.erase(idIt);
        }
    }

    auto fetchResourcesRequestId = noteData.m_requestId;
    m_noteDataByLocalUid.erase(noteDataIt);

    Q_EMIT error(fetchResourcesRequestId, errorDescription);
}

void NoteResourcesBinaryDataFetcher::createConnections(
    LocalStorageManagerAsync & localStorageManagerAsync)
{
    QObject::connect(
        this, &NoteResourcesBinaryDataFetcher::findResource,
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::onFindResourceRequest);

    QObject::connect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::findResourceComplete,
        this, &NoteResourcesBinaryDataFetcher::onFindResourceComplete);

    QObject::connect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::findResourceFailed,
        this, &NoteResourcesBinaryDataFetcher::onFindResourceFailed);
}

} // namespace quentier
