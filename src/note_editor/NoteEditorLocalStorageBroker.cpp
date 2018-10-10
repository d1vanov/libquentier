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
    m_originalNoteResourceLocalUidsByNoteLocalUid()
{
    createConnections(localStorageManager);
}

void NoteEditorLocalStorageBroker::saveNoteToLocalStorage(const Note & note)
{
    QNDEBUG(QStringLiteral("NoteEditorLocalStorageBroker::saveNoteToLocalStorage: note local uid = ") << note.localUid());

    // TODO: update note and its resources/attachments properly:
    // 1) if new resources were added to the note, add them
    // 2) if some resources were updated within the note, update them
    // 3) if some resources were expunged from the note, expunge them
    // 4) update the note without resource binary data
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
    QObject::connect(&localStorageManager, QNSIGNAL(LocalStorageManagerAsync,findNoteComplete,Note,bool,bool,QUuid),
                     this, QNSLOT(NoteEditorLocalStorageBroker,onFindNoteComplete,Note,bool,bool,QUuid));
    QObject::connect(&localStorageManager, QNSIGNAL(LocalStorageManagerAsync,findNoteFailed,Note,bool,bool,ErrorString,QUuid),
                     this, QNSLOT(NoteEditorLocalStorageBroker,onFindNoteFailed,Note,bool,bool,ErrorString,QUuid));
    QObject::connect(&localStorageManager, QNSIGNAL(LocalStorageManagerAsync,findNotebookComplete,Notebook,QUuid),
                     this, QNSLOT(NoteEditorLocalStorageBroker,onFindNotebookComplete,Notebook,QUuid));
    QObject::connect(&localStorageManager, QNSIGNAL(LocalStorageManagerAsync,findNotebookFailed,Notebook,ErrorString,QUuid),
                     this, QNSLOT(NoteEditorLocalStorageBroker,onFindNotebookFailed,Notebook,ErrorString,QUuid));
}

} // namespace quentier
