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

#ifndef LIB_QUENTIER_NOTE_EDITOR_NOTE_EDITOR_LOCAL_STORAGE_BROKER_H
#define LIB_QUENTIER_NOTE_EDITOR_NOTE_EDITOR_LOCAL_STORAGE_BROKER_H

#include <quentier/local_storage/LocalStorageManagerAsync.h>
#include <quentier/utility/LRUCache.hpp>

#include <qevercloud/types/Note.h>
#include <qevercloud/types/Notebook.h>
#include <qevercloud/types/Resource.h>

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
    [[nodiscard]] static NoteEditorLocalStorageBroker & instance();

    [[nodiscard]] LocalStorageManagerAsync * localStorageManager();

    void setLocalStorageManager(
        LocalStorageManagerAsync & localStorageManagerAsync);

Q_SIGNALS:
    void noteSavedToLocalStorage(QString noteLocalId);

    void failedToSaveNoteToLocalStorage(
        QString noteLocalId, ErrorString errorDescription);

    void foundNoteAndNotebook(
        qevercloud::Note note, qevercloud::Notebook notebook);

    void failedToFindNoteOrNotebook(
        QString noteLocalId, ErrorString errorDescription);

    void noteUpdated(qevercloud::Note note);
    void notebookUpdated(qevercloud::Notebook notebook);
    void noteDeleted(QString noteLocalId);
    void notebookDeleted(QString notebookLocalId);

    void foundResourceData(qevercloud::Resource resource);

    void failedToFindResourceData(
        QString resourceLocalId, ErrorString errorDescription);

    // private signals
    void updateNote(
        qevercloud::Note note, LocalStorageManager::UpdateNoteOptions options,
        QUuid requestId);

    void addResource(qevercloud::Resource resource, QUuid requestId);
    void updateResource(qevercloud::Resource resource, QUuid requestId);
    void expungeResource(qevercloud::Resource resource, QUuid requestId);

    void findNote(
        qevercloud::Note note, LocalStorageManager::GetNoteOptions options,
        QUuid requestId);

    void findNotebook(qevercloud::Notebook notebook, QUuid requestId);

    void findResource(
        qevercloud::Resource resource,
        LocalStorageManager::GetResourceOptions options, QUuid requestId);

public Q_SLOTS:
    void saveNoteToLocalStorage(const qevercloud::Note & note);
    void findNoteAndNotebook(const QString & noteLocalId);
    void findResourceData(const QString & resourceLocalId);

private Q_SLOTS:
    void onUpdateNoteComplete(
        qevercloud::Note note, LocalStorageManager::UpdateNoteOptions options,
        QUuid requestId);

    void onUpdateNoteFailed(
        qevercloud::Note note, LocalStorageManager::UpdateNoteOptions options,
        ErrorString errorDescription, QUuid requestId);

    void onUpdateNotebookComplete(
        qevercloud::Notebook notebook, QUuid requestId);

    void onFindNoteComplete(
        qevercloud::Note foundNote, LocalStorageManager::GetNoteOptions options,
        QUuid requestId);

    void onFindNoteFailed(
        qevercloud::Note note, LocalStorageManager::GetNoteOptions options,
        ErrorString errorDescription, QUuid requestId);

    void onFindNotebookComplete(
        qevercloud::Notebook foundNotebook, QUuid requestId);

    void onFindNotebookFailed(
        qevercloud::Notebook notebook, ErrorString errorDescription,
        QUuid requestId);

    void onAddResourceComplete(qevercloud::Resource resource, QUuid requestId);

    void onAddResourceFailed(
        qevercloud::Resource resource, ErrorString errorDescription,
        QUuid requestId);

    void onUpdateResourceComplete(
        qevercloud::Resource resource, QUuid requestId);

    void onUpdateResourceFailed(
        qevercloud::Resource resource, ErrorString errorDescription,
        QUuid requestId);

    void onExpungeResourceComplete(
        qevercloud::Resource resource, QUuid requestId);

    void onExpungeResourceFailed(
        qevercloud::Resource resource, ErrorString errorDescription,
        QUuid requestId);

    void onExpungeNoteComplete(qevercloud::Note note, QUuid requestId);

    void onExpungeNotebookComplete(
        qevercloud::Notebook notebook, QUuid requestId);

    void onFindResourceComplete(
        qevercloud::Resource resource,
        LocalStorageManager::GetResourceOptions options, QUuid requestId);

    void onFindResourceFailed(
        qevercloud::Resource resource,
        LocalStorageManager::GetResourceOptions options,
        ErrorString errorDescription, QUuid requestId);

    void onSwitchUserComplete(Account account, QUuid requestId);

private:
    void createConnections(LocalStorageManagerAsync & localStorageManagerAsync);

    void disconnectFromLocalStorage(
        LocalStorageManagerAsync & localStorageManagerAsync);

    void emitFindNoteRequest(const QString & noteLocalId);
    void emitUpdateNoteRequest(const qevercloud::Note & note);

    void emitFindNotebookForNoteByLocalIdRequest(
        const QString & notebookLocalId, const qevercloud::Note & note);

    void emitFindNotebookForNoteByGuidRequest(
        const QString & notebookGuid, const qevercloud::Note & note);

    using NotesHash = QHash<QString, qevercloud::Note>;
    using NotesPendingNotebookFindingHash = QHash<QString, NotesHash>;

    void emitFindNotebookForNoteRequest(
        const qevercloud::Notebook & notebook, const qevercloud::Note & note,
        NotesPendingNotebookFindingHash & notesPendingNotebookFinding);

    void saveNoteToLocalStorageImpl(
        const qevercloud::Note & previousNoteVersion,
        const qevercloud::Note & updatedNoteVersion);

    class SaveNoteInfo final : public Printable
    {
    public:
        QTextStream & print(QTextStream & strm) const override;

        [[nodiscard]] bool hasPendingResourceOperations() const;

        qevercloud::Note m_notePendingSaving;
        quint32 m_pendingAddResourceRequests = 0;
        quint32 m_pendingUpdateResourceRequests = 0;
        quint32 m_pendingExpungeResourceRequests = 0;
    };

private:
    Q_DISABLE_COPY(NoteEditorLocalStorageBroker)

private:
    LocalStorageManagerAsync * m_pLocalStorageManagerAsync = nullptr;

    QSet<QUuid> m_findNoteRequestIds;
    QSet<QUuid> m_findNotebookRequestIds;
    QSet<QUuid> m_findResourceRequestIds;

    QHash<QUuid, qevercloud::Note> m_notesPendingSavingByFindNoteRequestIds;

    NotesPendingNotebookFindingHash
        m_notesPendingNotebookFindingByNotebookLocalId;
    NotesPendingNotebookFindingHash m_notesPendingNotebookFindingByNotebookGuid;

    QHash<QUuid, QString> m_noteLocalIdsByAddResourceRequestIds;
    QHash<QUuid, QString> m_noteLocalIdsByUpdateResourceRequestIds;
    QHash<QUuid, QString> m_noteLocalIdsByExpungeResourceRequestIds;

    LRUCache<QString, qevercloud::Notebook> m_notebooksCache;
    LRUCache<QString, qevercloud::Note> m_notesCache;

    /**
     * This cache stores resources with binary data but only if that data is not
     * too large to prevent spending too much memory on it
     */
    LRUCache<QString, qevercloud::Resource> m_resourcesCache;

    QHash<QString, SaveNoteInfo> m_saveNoteInfoByNoteLocalIds;
    QSet<QUuid> m_updateNoteRequestIds;
};

} // namespace quentier

#endif // LIB_QUENTIER_NOTE_EDITOR_NOTE_EDITOR_LOCAL_STORAGE_BROKER_H
