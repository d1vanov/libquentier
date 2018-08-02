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

#ifndef LIB_QUENTIER_SYNCHRONIZATION_NOTE_SYNC_CONFLICT_RESOLVER_H
#define LIB_QUENTIER_SYNCHRONIZATION_NOTE_SYNC_CONFLICT_RESOLVER_H

#include <quentier/types/Note.h>
#include <QObject>

#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
#include <qt5qevercloud/QEverCloud.h>
#else
#include <qt4qevercloud/QEverCloud.h>
#endif

namespace quentier {

QT_FORWARD_DECLARE_CLASS(LocalStorageManagerAsync)
QT_FORWARD_DECLARE_CLASS(INoteStore)

/**
 * The NoteSyncConflictResolver class resolves the conflict between two notes: the one downloaded
 * from the remote server (but without full note data downloaded yet) and the local one. The conflict resolution
 * might lead to either overriding the local conflicting notes with remote changes or to clearing out Evernote-assigned
 * fields from the local conflicting note and any resources it might have - such fields as guid and update sequence number
 * in particular; in the latter case the local note would be converted to a local i.e. "not yet synchronized with Evernote"
 * note and the remote note would be treated as a new note coming from Evernote
 */
class Q_DECL_HIDDEN NoteSyncConflictResolver: public QObject
{
    Q_OBJECT
public:
    class Q_DECL_HIDDEN IManager
    {
    public:
        virtual LocalStorageManagerAsync & localStorageManagerAsync() = 0;
        virtual INoteStore * noteStoreForNote(const Note & note, QString & authToken, ErrorString & errorDescription) = 0;
        virtual ~IManager() {}
    };

    explicit NoteSyncConflictResolver(IManager & manager,
                                      const qevercloud::Note & remoteNote,
                                      const Note & localConflict,
                                      QObject * parent = Q_NULLPTR);

    void start();

    const qevercloud::Note & remoteNote() const { return m_remoteNote; }
    const Note & localConflict() const { return m_localConflict; }

Q_SIGNALS:
    void finished(qevercloud::Note remoteNote);
    void failure(qevercloud::Note remoteNote, ErrorString errorDescription);

// private signals
    void addNote(Note note, QUuid requestId);
    void updateNote(Note note, bool updateResources, bool updateTags, QUuid requestId);

private Q_SLOTS:
    void onAddNoteComplete(Note note, QUuid requestId);
    void onAddNoteFailed(Note note, QUuid requestId);
    void onUpdateNoteComplete(Note note, bool updateResources, bool updateTags, QUuid requestId);
    void onUpdateNoteFailed(Note note, bool updateResources, bool updateTags,
                            ErrorString errorDescription, QUuid requestId);

    void onGetNoteAsyncFinished(qint32 errorCode, qevercloud::Note qecNote,
                                qint32 rateLimitSeconds, ErrorString errorDescription);

private:
    void connectToLocalStorage();
    void processNotesConflictByGuid();
    void overrideLocalNoteWithRemoteChanges();

    struct State
    {
        enum type
        {
            Undefined = 0,
            OverrideLocalChangesWithRemoteChanges,
            PendingConflictingNoteUpdate,
            PendingFullRemoteNoteDataDownload,
            PendingRemoteNoteAdoptionInLocalStorage
        };
    };

private:
    Q_DISABLE_COPY(NoteSyncConflictResolver)

private:
    IManager &          m_manager;

    qevercloud::Note    m_remoteNote;
    Note                m_localConflict;

    State::type         m_state;

    QUuid               m_addNoteRequestId;
    QUuid               m_updateNoteRequestId;

    bool                m_started;
};

} // namespace quentier

#endif // LIB_QUENTIER_SYNCHRONIZATION_NOTE_SYNC_CONFLICT_RESOLVER_H
