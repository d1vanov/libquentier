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

#include "NoteSyncConflictResolver.h"
#include <quentier/local_storage/LocalStorageManagerAsync.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier_private/synchronization/INoteStore.h>

namespace quentier {

NoteSyncConflictResolver::NoteSyncConflictResolver(IManager & manager,
                                                   const qevercloud::Note & remoteNote,
                                                   const Note & localConflict,
                                                   QObject * parent) :
    QObject(parent),
    m_manager(manager),
    m_remoteNote(remoteNote),
    m_localConflict(localConflict),
    m_state(State::Undefined),
    m_addNoteRequestId(),
    m_updateNoteRequestId(),
    m_started(false)
{}

void NoteSyncConflictResolver::start()
{
    QNDEBUG(QStringLiteral("NoteSyncConflictResolver::start: remote note guid = ")
            << (m_remoteNote.guid.isSet() ? m_remoteNote.guid.ref() : QStringLiteral("<not set>"))
            << QStringLiteral(", local conflict local uid = ") << m_localConflict);

    if (m_started) {
        QNDEBUG(QStringLiteral("Already started"));
        return;
    }

    m_started = true;

    if (Q_UNLIKELY(!m_remoteNote.guid.isSet())) {
        ErrorString error(QT_TR_NOOP("Can't resolve the confict between remote and local notes: "
                                     "the remote note has no guid set"));
        QNWARNING(error << QStringLiteral(": ") << m_remoteNote);
        Q_EMIT failure(m_remoteNote, error);
        return;
    }

    connectToLocalStorage();
    processNotesConflictByGuid();
}

void NoteSyncConflictResolver::connectToLocalStorage()
{
    QNDEBUG(QStringLiteral("NoteSyncConflictResolver::connectToLocalStorage"));

    LocalStorageManagerAsync & localStorageManagerAsync = m_manager.localStorageManagerAsync();

    // Connect local signals to local storage manager async's slots
    QObject::connect(this, QNSIGNAL(NoteSyncConflictResolver,addNote,Note,QUuid),
                     &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onAddNoteRequest,Note,QUuid));
    QObject::connect(this, QNSIGNAL(NoteSyncConflictResolver,updateNote,Note,bool,bool,QUuid),
                     &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onUpdateNoteRequest,Note,bool,bool,QUuid));

    // Connect local storage manager async's signals to local slots
    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addNoteComplete,Note,QUuid),
                     this, QNSLOT(NoteSyncConflictResolver,onAddNoteComplete,Note,QUuid));
    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addNoteFailed,Note,ErrorString,QUuid),
                     this, QNSLOT(NoteSyncConflictResolver,onAddNoteFailed,Note,ErrorString,QUuid));
    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateNoteComplete,Note,bool,bool,QUuid),
                     this, QNSLOT(NoteSyncConflictResolver,onUpdateNoteComplete,Note,bool,bool,QUuid));
    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateNoteFailed,Note,bool,bool,ErrorString,QUuid),
                     this, QNSLOT(NoteSyncConflictResolver,onUpdateNoteFailed,Note,bool,bool,ErrorString,QUuid));
}

} // namespace quentier
