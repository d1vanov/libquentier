/*
 * Copyright 2016-2020 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_TESTS_NOTE_LOCAL_STORAGE_MANAGER_ASYNC_TESTER_H
#define LIB_QUENTIER_TESTS_NOTE_LOCAL_STORAGE_MANAGER_ASYNC_TESTER_H

#include <quentier/local_storage/LocalStorageManager.h>
#include <quentier/types/Note.h>
#include <quentier/types/Notebook.h>

#include <QUuid>

namespace quentier {

QT_FORWARD_DECLARE_CLASS(LocalStorageManagerAsync)

namespace test {

class NoteLocalStorageManagerAsyncTester final : public QObject
{
    Q_OBJECT
public:
    explicit NoteLocalStorageManagerAsyncTester(QObject * parent = nullptr);
    ~NoteLocalStorageManagerAsyncTester();

public Q_SLOTS:
    void onInitTestCase();

Q_SIGNALS:
    void success();
    void failure(QString errorDescription);

    // private signals
    void addNotebookRequest(Notebook notebook, QUuid requestId);

    void getNoteCountRequest(
        LocalStorageManager::NoteCountOptions options, QUuid requestId);

    void addNoteRequest(Note note, QUuid requestId);

    void updateNoteRequest(
        Note note, LocalStorageManager::UpdateNoteOptions options,
        QUuid requestId);

    void findNoteRequest(
        Note note, LocalStorageManager::GetNoteOptions options,
        QUuid requestId);

    void listNotesPerNotebookRequest(
        Notebook notebook, LocalStorageManager::GetNoteOptions options,
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListNotesOrder order,
        LocalStorageManager::OrderDirection orderDirection, QUuid requestId);

    void expungeNoteRequest(Note note, QUuid requestId);

private Q_SLOTS:
    void initialize();
    void onAddNotebookCompleted(Notebook notebook, QUuid requestId);

    void onAddNotebookFailed(
        Notebook notebook, ErrorString errorDescription, QUuid requestId);

    void onGetNoteCountCompleted(
        int count, LocalStorageManager::NoteCountOptions options,
        QUuid requestId);

    void onGetNoteCountFailed(
        ErrorString errorDescription,
        LocalStorageManager::NoteCountOptions options, QUuid requestId);

    void onAddNoteCompleted(Note note, QUuid requestId);

    void onAddNoteFailed(
        Note note, ErrorString errorDescription, QUuid requestId);

    void onUpdateNoteCompleted(
        Note note, LocalStorageManager::UpdateNoteOptions options,
        QUuid requestId);

    void onUpdateNoteFailed(
        Note note, LocalStorageManager::UpdateNoteOptions options,
        ErrorString errorDescription, QUuid requestId);

    void onFindNoteCompleted(
        Note note, LocalStorageManager::GetNoteOptions options,
        QUuid requestId);

    void onFindNoteFailed(
        Note note, LocalStorageManager::GetNoteOptions options,
        ErrorString errorDescription, QUuid requestId);

    void onListNotesPerNotebookCompleted(
        Notebook notebook, LocalStorageManager::GetNoteOptions options,
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListNotesOrder order,
        LocalStorageManager::OrderDirection orderDirection, QList<Note> notes,
        QUuid requestId);

    void onListNotesPerNotebookFailed(
        Notebook notebook, LocalStorageManager::GetNoteOptions options,
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListNotesOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        ErrorString errorDescription, QUuid requestId);

    void onExpungeNoteCompleted(Note note, QUuid requestId);

    void onExpungeNoteFailed(
        Note note, ErrorString errorDescription, QUuid requestId);

private:
    void createConnections();
    void clear();

    enum State
    {
        STATE_UNINITIALIZED,
        STATE_SENT_ADD_NOTEBOOK_REQUEST,
        STATE_SENT_ADD_REQUEST,
        STATE_SENT_FIND_AFTER_ADD_REQUEST,
        STATE_SENT_UPDATE_REQUEST,
        STATE_SENT_FIND_AFTER_UPDATE_REQUEST,
        STATE_SENT_GET_COUNT_AFTER_UPDATE_REQUEST,
        STATE_SENT_DELETE_REQUEST,
        STATE_SENT_EXPUNGE_REQUEST,
        STATE_SENT_FIND_AFTER_EXPUNGE_REQUEST,
        STATE_SENT_GET_COUNT_AFTER_EXPUNGE_REQUEST,
        STATE_SENT_ADD_EXTRA_NOTEBOOK_REQUEST,
        STATE_SENT_ADD_EXTRA_NOTE_ONE_REQUEST,
        STATE_SENT_ADD_EXTRA_NOTE_TWO_REQUEST,
        STATE_SENT_ADD_EXTRA_NOTE_THREE_REQUEST,
        STATE_SENT_LIST_NOTES_PER_NOTEBOOK_ONE_REQUEST,
        STATE_SENT_LIST_NOTES_PER_NOTEBOOK_TWO_REQUEST
    };

    State m_state = STATE_UNINITIALIZED;

    LocalStorageManagerAsync * m_pLocalStorageManagerAsync = nullptr;
    QThread * m_pLocalStorageManagerThread = nullptr;

    Notebook m_notebook;
    Notebook m_extraNotebook;
    Note m_initialNote;
    Note m_foundNote;
    Note m_modifiedNote;
    QList<Note> m_initialNotes;
    QList<Note> m_extraNotes;
};

} // namespace test
} // namespace quentier

#endif // LIB_QUENTIER_TESTS_NOTE_LOCAL_STORAGE_MANAGER_ASYNC_TESTER_H
