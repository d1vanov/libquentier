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
#include "SynchronizationShared.h"
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

    connectToLocalStorage();
    processNotesConflictByGuid();
}

void NoteSyncConflictResolver::onAddNoteComplete(Note note, QUuid requestId)
{
    if (requestId != m_addNoteRequestId) {
        return;
    }

    QNDEBUG(QStringLiteral("NoteSyncConflictResolver::onAddNoteComplete: request id = ")
            << requestId << QStringLiteral(", note: ") << note);

    // TODO: implement further
}

void NoteSyncConflictResolver::onAddNoteFailed(Note note, ErrorString errorDescription, QUuid requestId)
{
    if (requestId != m_addNoteRequestId) {
        return;
    }

    QNDEBUG(QStringLiteral("NoteSyncConflictResolver::onAddNoteFailed: request id = ")
            << requestId << QStringLiteral(", error description = ") << errorDescription
            << QStringLiteral("; note: ") << note);

    Q_EMIT failure(m_remoteNote, errorDescription);
}

void NoteSyncConflictResolver::onUpdateNoteComplete(Note note, bool updateResources, bool updateTags, QUuid requestId)
{
    if (requestId != m_updateNoteRequestId) {
        return;
    }

    QNDEBUG(QStringLiteral("NoteSyncConflictResolver::onUpdateNoteComplete: note = ") << note
            << QStringLiteral("\nRequest id = ") << requestId << QStringLiteral(", update resources = ")
            << (updateResources ? QStringLiteral("true") : QStringLiteral("false")) << QStringLiteral(", update tags = ")
            << (updateTags ? QStringLiteral("true") : QStringLiteral("false")));

    // TODO: implement further
}

void NoteSyncConflictResolver::onUpdateNoteFailed(Note note, bool updateResources, bool updateTags,
                                                  ErrorString errorDescription, QUuid requestId)
{
    if (requestId != m_updateNoteRequestId) {
        return;
    }

    QNDEBUG(QStringLiteral("NoteSyncConflictResolver::onUpdateNoteFailed: note = ") << note
            << QStringLiteral("\nRequest id = ") << requestId << QStringLiteral(", update resources = ")
            << (updateResources ? QStringLiteral("true") : QStringLiteral("false")) << QStringLiteral(", update tags = ")
            << (updateTags ? QStringLiteral("true") : QStringLiteral("false"))
            << QStringLiteral("; error description = ") << errorDescription);

    // TODO: implement further
}

void NoteSyncConflictResolver::onGetNoteAsyncFinished(qint32 errorCode, qevercloud::Note qecNote,
                                                      qint32 rateLimitSeconds, ErrorString errorDescription)
{
    QNDEBUG(QStringLiteral("NoteSyncConflictResolver::onGetNoteAsyncFinished: error code = ") << errorCode
            << QStringLiteral(", note = ") << qecNote << QStringLiteral("\nRate limit seconds = ")
            << rateLimitSeconds << QStringLiteral(", error description = ") << errorDescription);

    // TODO: implement
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

void NoteSyncConflictResolver::processNotesConflictByGuid()
{
    QNDEBUG(QStringLiteral("NoteSyncConflictResolver::processNotesConflictByGuid"));

    if (Q_UNLIKELY(!m_remoteNote.guid.isSet())) {
        ErrorString error(QT_TR_NOOP("Can't resolve the confict between remote and local notes: "
                                     "the remote note has no guid set"));
        APPEND_NOTE_DETAILS(error, Note(m_remoteNote))
        QNWARNING(error << QStringLiteral(": ") << m_remoteNote);
        Q_EMIT failure(m_remoteNote, error);
        return;
    }

    if (Q_UNLIKELY(!m_remoteNote.updateSequenceNum.isSet())) {
        ErrorString error(QT_TR_NOOP("Can't resolve the conflict between remote and local notes: "
                                     "the remote note has no update sequence number set"));
        APPEND_NOTE_DETAILS(error, Note(m_remoteNote))
        QNWARNING(error << QStringLiteral(": ") << m_remoteNote);
        Q_EMIT failure(m_remoteNote, error);
        return;
    }

    if (Q_UNLIKELY(!m_localConflict.hasGuid())) {
        ErrorString error(QT_TR_NOOP("Can't resolve the conflict between remote and local notes: "
                                     "the local note has no guid set"));
        APPEND_NOTE_DETAILS(error, m_localConflict)
        QNWARNING(error << QStringLiteral(": ") << m_localConflict);
        Q_EMIT failure(m_remoteNote, error);
        return;
    }

    if (Q_UNLIKELY(m_remoteNote.guid.ref() != m_localConflict.guid())) {
        ErrorString error(QT_TR_NOOP("Note sync conflict resolution was applied to notes which do not conflict by guid"));
        APPEND_NOTE_DETAILS(error, m_localConflict)
        QNWARNING(error << QStringLiteral(": ") << m_localConflict);
        Q_EMIT failure(m_remoteNote, error);
        return;
    }

    bool shouldClearEvernoteFieldsFromConflictingNote = true;
    if (!m_localConflict.isDirty()) {
        QNDEBUG(QStringLiteral("The local note is not dirty, can just override it with remote changes"));
        shouldClearEvernoteFieldsFromConflictingNote = false;
    }
    else if (m_localConflict.hasUpdateSequenceNumber() && (m_localConflict.updateSequenceNumber() == m_remoteNote.updateSequenceNum.ref())) {
        QNDEBUG(QStringLiteral("The notes match by update sequence number but the local note is dirty => local note should override the remote changes"));
        Q_EMIT finished(m_remoteNote);
        return;
    }

    if (shouldClearEvernoteFieldsFromConflictingNote)
    {
        m_localConflict.setGuid(QString());
        m_localConflict.setUpdateSequenceNumber(-1);
        m_localConflict.setDirty(true);

        if (m_localConflict.hasResources())
        {
            QList<Resource> resources = m_localConflict.resources();
            for(auto it = resources.begin(), end = resources.end(); it != end; ++it) {
                Resource & resource = *it;
                resource.setGuid(QString());
                resource.setUpdateSequenceNumber(-1);
                resource.setDirty(true);
            }

            m_localConflict.setResources(resources);
        }

        m_updateNoteRequestId = QUuid::createUuid();
        QNDEBUG(QStringLiteral("Emitting the request to update the local conflicting note (after clearing Evernote assigned fields from it): request id = ")
                << m_updateNoteRequestId << QStringLiteral(", note to update: ") << m_localConflict);
        // TODO: update local conflict note but update only resource metadata
    }
}


} // namespace quentier
