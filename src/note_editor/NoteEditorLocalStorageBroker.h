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

#ifndef LIB_QUENTIER_NOTE_EDITOR_NOTE_EDITOR_LOCAL_STORAGE_BROKER_H
#define LIB_QUENTIER_NOTE_EDITOR_NOTE_EDITOR_LOCAL_STORAGE_BROKER_H

#include <quentier/local_storage/LocalStorageManagerAsync.h>
#include <quentier/types/Note.h>
#include <QObject>
#include <QHash>
#include <QSet>

namespace quentier {

class NoteEditorLocalStorageBroker: public QObject
{
    Q_OBJECT
public:
    explicit NoteEditorLocalStorageBroker(LocalStorageManagerAsync & localStorageManager,
                                          QObject * parent = Q_NULLPTR);

Q_SIGNALS:
    void noteSavedToLocalStorage(QString noteLocalUid);
    void failedToSaveNoteToLocalStorage(QString noteLocalUid, ErrorString errorDescription);

    void foundNoteAndNotebook(Note note, Notebook notebook);
    void failedToFindNoteOrNotebook(QString noteLocalUid, ErrorString errorDescription);

// private signals
    void updateNote(Note note, LocalStorageManager::UpdateNoteOptions options, QUuid requestId);
    void addResource(Resource resource, QUuid requestId);
    void updateResource(Resource resource, QUuid requestId);
    void expungeResource(Resource resource, QUuid requestId);
    void findNote(Note note, bool withResourceMetadata, bool withResourceBinaryData, QUuid requestId);
    void findNotebook(Notebook notebook, QUuid requestId);

public Q_SLOTS:
    void saveNoteToLocalStorage(const Note & note);
    void findNoteAndNotebook(const QString & noteLocalUid);

private Q_SLOTS:
    void onUpdateNoteComplete(Note note, LocalStorageManager::UpdateNoteOptions options, QUuid requestId);
    void onUpdateNoteFailed(Note note, LocalStorageManager::UpdateNoteOptions options,
                            ErrorString errorDescription, QUuid requestId);
    void onFindNoteComplete(Note foundNote, bool withResourceMetadata, bool withResourceBinaryData, QUuid requestId);
    void onFindNoteFailed(Note note, bool withResourceMetadata, bool withResourceBinaryData,
                          ErrorString errorDescription, QUuid requestId);
    void onFindNotebookComplete(Notebook foundNotebook, QUuid requestId);
    void onFindNotebookFailed(Notebook notebook, ErrorString errorDescription, QUuid requestId);

private:
    void createConnections(LocalStorageManagerAsync & localStorageManager);

private:
    Q_DISABLE_COPY(NoteEditorLocalStorageBroker)

private:
    QHash<QString, QSet<QString> >   m_originalNoteResourceLocalUidsByNoteLocalUid;
};

} // namespace quentier

#endif // LIB_QUENTIER_NOTE_EDITOR_NOTE_EDITOR_LOCAL_STORAGE_BROKER_H
