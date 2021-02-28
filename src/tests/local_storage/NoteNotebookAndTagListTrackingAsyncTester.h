/*
 * Copyright 2019-2020 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_TESTS_NOTE_NOTEBOOK_AND_TAG_LIST_TRACKING_ASYNC_TESTER_H
#define LIB_QUENTIER_TESTS_NOTE_NOTEBOOK_AND_TAG_LIST_TRACKING_ASYNC_TESTER_H

#include <quentier/local_storage/LocalStorageManagerAsync.h>
#include <quentier/types/ErrorString.h>

namespace quentier {
namespace test {

class NoteNotebookAndTagListTrackingAsyncTester final : public QObject
{
    Q_OBJECT
public:
    explicit NoteNotebookAndTagListTrackingAsyncTester(
        QObject * parent = nullptr);

    ~NoteNotebookAndTagListTrackingAsyncTester();

public Q_SLOTS:
    void onInitTestCase();

Q_SIGNALS:
    void success();
    void failure(QString errorDescription);

    // private signals
    void addNotebook(Notebook notebook, QUuid requestId);
    void addTag(Tag tag, QUuid requestId);
    void addNote(Note note, QUuid requestId);

    void updateNote(
        Note note, LocalStorageManager::UpdateNoteOptions options,
        QUuid requestId);

private Q_SLOTS:
    void initialize();
    void onAddNotebookComplete(Notebook notebook, QUuid requestId);

    void onAddNotebookFailed(
        Notebook notebook, ErrorString errorDescription, QUuid requestId);

    void onAddTagComplete(Tag tag, QUuid requestId);
    void onAddTagFailed(Tag tag, ErrorString errorDescription, QUuid requestId);
    void onAddNoteComplete(Note note, QUuid requestId);

    void onAddNoteFailed(
        Note note, ErrorString errorDescription, QUuid requestId);

    void onUpdateNoteComplete(
        Note note, LocalStorageManager::UpdateNoteOptions options,
        QUuid requestId);

    void onUpdateNoteFailed(
        Note note, LocalStorageManager::UpdateNoteOptions options,
        ErrorString errorDescription, QUuid requestId);

    void onNoteMovedToAnotherNotebook(
        QString noteLocalUid, QString previousNotebookLocalUid,
        QString newNotebookLocalUid);

    void onNoteTagListUpdated(
        QString noteLocalUid, QStringList previousTagLocalUids,
        QStringList newTagLocalUids);

private:
    void createConnections();
    void clear();

    void createNoteInLocalStorage();
    void moveNoteToAnotherNotebook();
    void changeNoteTagsList();
    void moveNoteToAnotherNotebookAlongWithTagListChange();

    bool checkTagsListEqual(
        const QVector<Tag> & lhs, const QStringList & rhs) const;

private:
    enum State
    {
        STATE_UNINITIALIZED,
        STATE_PENDING_NOTEBOOKS_AND_TAGS_CREATION,
        STATE_PENDING_NOTE_CREATION,
        STATE_PENDING_NOTE_UPDATE_WITHOUT_NOTEBOOK_OR_TAG_LIST_CHANGE,
        STATE_PENDING_NOTE_UPDATE_WITH_NOTEBOOK_CHANGE_ONLY,
        STATE_PENDING_NOTE_UPDATE_WITH_TAG_LIST_CHANGE_ONLY,
        STATE_PENDING_NOTE_UPDATE_WITH_NOTEBOOK_AND_TAG_LIST_CHANGES
    };

    State m_state = STATE_UNINITIALIZED;

    LocalStorageManagerAsync * m_pLocalStorageManagerAsync = nullptr;
    QThread * m_pLocalStorageManagerThread = nullptr;

    Notebook m_firstNotebook;
    Notebook m_secondNotebook;
    int m_addedNotebooksCount = 0;

    QVector<Tag> m_firstNoteTagsSet;
    QVector<Tag> m_secondNoteTagsSet;
    int m_addedTagsCount = 0;

    Note m_note;

    bool m_receivedUpdateNoteCompleteSignal = false;
    bool m_receivedNoteMovedToAnotherNotebookSignal = false;
    bool m_receivedNoteTagsListChangedSignal = false;

    int m_noteMovedToAnotherNotebookSlotInvocationCount = 0;
    int m_noteTagsListChangedSlotInvocationCount = 0;
};

} // namespace test
} // namespace quentier

#endif // LIB_QUENTIER_TESTS_NOTE_NOTEBOOK_AND_TAG_LIST_TRACKING_ASYNC_TESTER_H
