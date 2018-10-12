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
#include <quentier/logging/QuentierLogger.h>

namespace quentier {

NoteEditorLocalStorageBroker::NoteEditorLocalStorageBroker(LocalStorageManagerAsync & localStorageManager,
                                                           QObject * parent) :
    QObject(parent),
    m_originalNoteResourceLocalUidsByNoteLocalUid(),
    m_noteLocalUidsByAddResourceRequestIds(),
    m_noteLocalUidsByUpdateResourceRequestIds(),
    m_noteLocalUidsByExpungeResourceRequestIds(),
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

    LocalStorageManager::UpdateNoteOptions options(LocalStorageManager::UpdateNoteOption::UpdateResourceMetadata);
    QUuid requestId = QUuid::createUuid();
    Q_UNUSED(m_updateNoteRequestIds.insert(requestId))
    QNDEBUG(QStringLiteral("Emitting the request to update note in local storage: requets id = ") << requestId
            << QStringLiteral(", note: ") << note);
    Q_EMIT updateNote(note, options, requestId);
}

void NoteEditorLocalStorageBroker::findNoteAndNotebook(const QString & noteLocalUid)
{
    QNDEBUG(QStringLiteral("NoteEditorLocalStorageBroker::findNoteAndNotebook: note local uid = ") << noteLocalUid);

    // TODO: emit requests to find note and notebook unless they are already in progress
}

void NoteEditorLocalStorageBroker::onUpdateNoteComplete(Note note, LocalStorageManager::UpdateNoteOptions options, QUuid requestId)
{
    // TODO: implement
    // 1) Figure out if the note was updated in response to explicit request
    //    from NoteEditorLocalStorageBroker, if so, process it properly
    // 2) Otherwise figure out if the updated note is the one asked to be found, if no, return
    // 2) Emit noteUpdated signal

    Q_UNUSED(note)
    Q_UNUSED(options)
    Q_UNUSED(requestId)
}

void NoteEditorLocalStorageBroker::onUpdateNoteFailed(Note note, LocalStorageManager::UpdateNoteOptions options,
                                                      ErrorString errorDescription, QUuid requestId)
{
    // TODO: implement
    // 1) Figure out if this failure came in response to explicit request
    //    from NoteEditorLocalStorageBroker, if so, process it properly
    // 2) Otherwise just return, it's none of our damn business

    Q_UNUSED(note)
    Q_UNUSED(options)
    Q_UNUSED(errorDescription)
    Q_UNUSED(requestId)
}

void NoteEditorLocalStorageBroker::onFindNoteComplete(Note foundNote, bool withResourceMetadata, bool withResourceBinaryData, QUuid requestId)
{
    // TODO: implement
    Q_UNUSED(foundNote)
    Q_UNUSED(withResourceMetadata)
    Q_UNUSED(withResourceBinaryData)
    Q_UNUSED(requestId)
}

void NoteEditorLocalStorageBroker::onFindNoteFailed(Note note, bool withResourceMetadata, bool withResourceBinaryData,
                                                    ErrorString errorDescription, QUuid requestId)
{
    // TODO: implement
    Q_UNUSED(note)
    Q_UNUSED(withResourceMetadata)
    Q_UNUSED(withResourceBinaryData)
    Q_UNUSED(errorDescription)
    Q_UNUSED(requestId)
}

void NoteEditorLocalStorageBroker::onFindNotebookComplete(Notebook foundNotebook, QUuid requestId)
{
    // TODO: implement
    Q_UNUSED(foundNotebook)
    Q_UNUSED(requestId)
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
