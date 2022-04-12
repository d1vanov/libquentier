/*
 * Copyright 2018-2022 Dmitry Ivanov
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
#include <quentier/utility/LRUCache.hpp>

#include <QHash>
#include <QObject>
#include <QSet>
#include <QVector>

namespace quentier {

class NoteEditorLocalStorageBroker : public QObject
{
    Q_OBJECT
private:
    explicit NoteEditorLocalStorageBroker(QObject * parent = nullptr);

public:
    static NoteEditorLocalStorageBroker & instance();

    LocalStorageManagerAsync * localStorageManager();

    void setLocalStorageManager(
        LocalStorageManagerAsync & localStorageManagerAsync);

Q_SIGNALS:
    void noteSavedToLocalStorage(QString noteLocalUid);

    void failedToSaveNoteToLocalStorage(
        QString noteLocalUid, ErrorString errorDescription);

    void foundNoteAndNotebook(Note note, Notebook notebook);

    void failedToFindNoteOrNotebook(
        QString noteLocalUid, ErrorString errorDescription);

    void noteUpdated(Note note);
    void notebookUpdated(Notebook);
    void noteDeleted(QString noteLocalUid);
    void notebookDeleted(QString notebookLocalUid);

    void foundResourceData(Resource resource);

    void failedToFindResourceData(
        QString resourceLocalUid, ErrorString errorDescription);

    // private signals
    void updateNote(
        Note note, LocalStorageManager::UpdateNoteOptions options,
        QUuid requestId);

    void addResource(Resource resource, QUuid requestId);
    void updateResource(Resource resource, QUuid requestId);
    void expungeResource(Resource resource, QUuid requestId);

    void findNote(
        Note note, LocalStorageManager::GetNoteOptions options,
        QUuid requestId);

    void findNotebook(Notebook notebook, QUuid requestId);

    void findResource(
        Resource resource, LocalStorageManager::GetResourceOptions options,
        QUuid requestId);

    void fetchResourcesBinaryData(Note note, QUuid requestId);

public Q_SLOTS:
    void saveNoteToLocalStorage(const Note & note);
    void findNoteAndNotebook(const QString & noteLocalUid);
    void findResourceData(const QString & resourceLocalUid);

private Q_SLOTS:
    void onUpdateNoteComplete(
        Note note, LocalStorageManager::UpdateNoteOptions options,
        QUuid requestId);

    void onUpdateNoteFailed(
        Note note, LocalStorageManager::UpdateNoteOptions options,
        ErrorString errorDescription, QUuid requestId);

    void onUpdateNotebookComplete(Notebook notebook, QUuid requestId);

    void onFindNoteComplete(
        Note foundNote, LocalStorageManager::GetNoteOptions options,
        QUuid requestId);

    void onFindNoteFailed(
        Note note, LocalStorageManager::GetNoteOptions options,
        ErrorString errorDescription, QUuid requestId);

    void onFindNotebookComplete(Notebook foundNotebook, QUuid requestId);

    void onFindNotebookFailed(
        Notebook notebook, ErrorString errorDescription, QUuid requestId);

    void onExpungeNoteComplete(Note note, QUuid requestId);
    void onExpungeNotebookComplete(Notebook notebook, QUuid requestId);

    void onFindResourceComplete(
        Resource resource, LocalStorageManager::GetResourceOptions options,
        QUuid requestId);

    void onFindResourceFailed(
        Resource resource, LocalStorageManager::GetResourceOptions options,
        ErrorString errorDescription, QUuid requestId);

    void onSwitchUserComplete(Account account, QUuid requestId);

    void onNoteBinaryDataFetcherFinished(Note note, QUuid requestId);

    void onNoteBinaryDataFetcherError(
        QUuid requestId, ErrorString errorDescription);

private:
    void createConnections(LocalStorageManagerAsync & localStorageManagerAsync);

    void disconnectFromLocalStorage(
        LocalStorageManagerAsync & localStorageManagerAsync);

    void emitFindNoteRequest(const QString & noteLocalUid);

    enum class UpdateNoteOption
    {
        WithResourceBinaryData,
        WithoutResourceBinaryData
    };

    void emitUpdateNoteRequest(
        const Note & note, const UpdateNoteOption option);

    void emitFindNotebookForNoteByLocalUidRequest(
        const QString & notebookLocalUid, const Note & note);

    void emitFindNotebookForNoteByGuidRequest(
        const QString & notebookGuid, const Note & note);

    using NotesHash = QHash<QString, Note>;
    using NotesPendingNotebookFindingHash = QHash<QString, NotesHash>;

    void emitFindNotebookForNoteRequest(
        const Notebook & notebook, const Note & note,
        NotesPendingNotebookFindingHash & notesPendingNotebookFinding);

    void saveNoteToLocalStorageImpl(
        const Note & previousNoteVersion, const Note & updatedNoteVersion);

private:
    Q_DISABLE_COPY(NoteEditorLocalStorageBroker)

private:
    LocalStorageManagerAsync * m_pLocalStorageManagerAsync = nullptr;

    QSet<QUuid> m_findNoteRequestIds;
    QSet<QUuid> m_findNotebookRequestIds;
    QSet<QUuid> m_findResourceRequestIds;

    QHash<QUuid, Note> m_notesPendingSavingByFindNoteRequestIds;

    NotesPendingNotebookFindingHash
        m_notesPendingNotebookFindingByNotebookLocalUid;
    NotesPendingNotebookFindingHash m_notesPendingNotebookFindingByNotebookGuid;

    LRUCache<QString, Notebook> m_notebooksCache;
    LRUCache<QString, Note> m_notesCache;

    /**
     * This cache stores resources with binary data but only if that data is not
     * too large to prevent spending too much memory on it
     */
    LRUCache<QString, Resource> m_resourcesCache;

    // Request id to the number of attempts to update a particular note
    // (it can recoverably fail from the first attempt)
    QHash<QUuid, int> m_updateNoteRequestIdsWithAttemptCounts;

    QHash<QUuid, QString> m_fetchNoteResourcesBinaryDataRequestIdsToNoteLocalUids;
};

} // namespace quentier

#endif // LIB_QUENTIER_NOTE_EDITOR_NOTE_EDITOR_LOCAL_STORAGE_BROKER_H
