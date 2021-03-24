/*
 * Copyright 2016-2021 Dmitry Ivanov
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

#include <qevercloud/types/Note.h>
#include <qevercloud/types/Notebook.h>

#include <QUuid>

namespace quentier {

class LocalStorageManagerAsync;

namespace test {

class NoteLocalStorageManagerAsyncTester final : public QObject
{
    Q_OBJECT
public:
    explicit NoteLocalStorageManagerAsyncTester(QObject * parent = nullptr);
    ~NoteLocalStorageManagerAsyncTester() override;

public Q_SLOTS:
    void onInitTestCase();

Q_SIGNALS:
    void success();
    void failure(QString errorDescription);

    // private signals
    void addNotebookRequest(qevercloud::Notebook notebook, QUuid requestId);

    void getNoteCountRequest(
        LocalStorageManager::NoteCountOptions options, QUuid requestId);

    void addNoteRequest(qevercloud::Note note, QUuid requestId);

    void updateNoteRequest(
        qevercloud::Note note, LocalStorageManager::UpdateNoteOptions options,
        QUuid requestId);

    void findNoteRequest(
        qevercloud::Note note, LocalStorageManager::GetNoteOptions options,
        QUuid requestId);

    void listNotesPerNotebookRequest(
        qevercloud::Notebook notebook,
        LocalStorageManager::GetNoteOptions options,
        LocalStorageManager::ListObjectsOptions flag, std::size_t limit,
        std::size_t offset, LocalStorageManager::ListNotesOrder order,
        LocalStorageManager::OrderDirection orderDirection, QUuid requestId);

    void expungeNoteRequest(qevercloud::Note note, QUuid requestId);

private Q_SLOTS:
    void initialize();
    void onAddNotebookCompleted(qevercloud::Notebook notebook, QUuid requestId);

    void onAddNotebookFailed(
        qevercloud::Notebook notebook, ErrorString errorDescription,
        QUuid requestId);

    void onGetNoteCountCompleted(
        int count, LocalStorageManager::NoteCountOptions options,
        QUuid requestId);

    void onGetNoteCountFailed(
        ErrorString errorDescription,
        LocalStorageManager::NoteCountOptions options, QUuid requestId);

    void onAddNoteCompleted(qevercloud::Note note, QUuid requestId);

    void onAddNoteFailed(
        qevercloud::Note note, ErrorString errorDescription, QUuid requestId);

    void onUpdateNoteCompleted(
        qevercloud::Note note, LocalStorageManager::UpdateNoteOptions options,
        QUuid requestId);

    void onUpdateNoteFailed(
        qevercloud::Note note, LocalStorageManager::UpdateNoteOptions options,
        ErrorString errorDescription, QUuid requestId);

    void onFindNoteCompleted(
        qevercloud::Note note, LocalStorageManager::GetNoteOptions options,
        QUuid requestId);

    void onFindNoteFailed(
        qevercloud::Note note, LocalStorageManager::GetNoteOptions options,
        ErrorString errorDescription, QUuid requestId);

    void onListNotesPerNotebookCompleted(
        qevercloud::Notebook notebook,
        LocalStorageManager::GetNoteOptions options,
        LocalStorageManager::ListObjectsOptions flag, std::size_t limit,
        std::size_t offset, LocalStorageManager::ListNotesOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QList<qevercloud::Note> notes, QUuid requestId);

    void onListNotesPerNotebookFailed(
        qevercloud::Notebook notebook,
        LocalStorageManager::GetNoteOptions options,
        LocalStorageManager::ListObjectsOptions flag, std::size_t limit,
        std::size_t offset, LocalStorageManager::ListNotesOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        ErrorString errorDescription, QUuid requestId);

    void onExpungeNoteCompleted(qevercloud::Note note, QUuid requestId);

    void onExpungeNoteFailed(
        qevercloud::Note note, ErrorString errorDescription, QUuid requestId);

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

    qevercloud::Notebook m_notebook;
    qevercloud::Notebook m_extraNotebook;
    qevercloud::Note m_initialNote;
    qevercloud::Note m_foundNote;
    qevercloud::Note m_modifiedNote;
    QList<qevercloud::Note> m_initialNotes;
    QList<qevercloud::Note> m_extraNotes;
};

} // namespace test
} // namespace quentier

#endif // LIB_QUENTIER_TESTS_NOTE_LOCAL_STORAGE_MANAGER_ASYNC_TESTER_H
