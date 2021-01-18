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

#ifndef LIB_QUENTIER_TESTS_LOCAL_STORAGE_CACHE_ASYNC_TESTER_H
#define LIB_QUENTIER_TESTS_LOCAL_STORAGE_CACHE_ASYNC_TESTER_H

#include <quentier/local_storage/LocalStorageManagerAsync.h>

class QDebug;

namespace quentier {

class LocalStorageCacheManager;

namespace test {

class LocalStorageCacheAsyncTester final : public QObject
{
    Q_OBJECT
public:
    explicit LocalStorageCacheAsyncTester(QObject * parent = nullptr);
    ~LocalStorageCacheAsyncTester() noexcept override;

public Q_SLOTS:
    void onInitTestCase();

Q_SIGNALS:
    void success();
    void failure(QString errorDescription);

    // private signals
    void addNotebookRequest(qevercloud::Notebook notebook, QUuid requestId);
    void updateNotebookRequest(qevercloud::Notebook notebook, QUuid requestId);

    void addNoteRequest(qevercloud::Note note, QUuid requestId);

    void updateNoteRequest(
        qevercloud::Note note, LocalStorageManager::UpdateNoteOptions options,
        QUuid requestId);

    void addTagRequest(qevercloud::Tag tag, QUuid requestId);
    void updateTagRequest(qevercloud::Tag tag, QUuid requestId);

    void addLinkedNotebookRequest(
        qevercloud::LinkedNotebook linkedNotebook, QUuid requestId);

    void updateLinkedNotebookRequest(
        qevercloud::LinkedNotebook linkedNotebook, QUuid requestId);

    void addSavedSearchRequest(qevercloud::SavedSearch search, QUuid requestId);

    void updateSavedSearchRequest(
        qevercloud::SavedSearch search, QUuid requestId);

private Q_SLOTS:
    void initialize();

    void onAddNotebookCompleted(qevercloud::Notebook notebook, QUuid requestId);

    void onAddNotebookFailed(
        qevercloud::Notebook notebook, ErrorString errorDescription,
        QUuid requestId);

    void onUpdateNotebookCompleted(
        qevercloud::Notebook notebook, QUuid requestId);

    void onUpdateNotebookFailed(
        qevercloud::Notebook notebook, ErrorString errorDescription,
        QUuid requestId);

    void onAddNoteCompleted(qevercloud::Note note, QUuid requestId);

    void onAddNoteFailed(
        qevercloud::Note note, ErrorString errorDescription, QUuid requestId);

    void onUpdateNoteCompleted(
        qevercloud::Note note, LocalStorageManager::UpdateNoteOptions options,
        QUuid requestId);

    void onUpdateNoteFailed(
        qevercloud::Note note, LocalStorageManager::UpdateNoteOptions options,
        ErrorString errorDescription, QUuid requestId);

    void onAddTagCompleted(qevercloud::Tag tag, QUuid requestId);

    void onAddTagFailed(
        qevercloud::Tag tag, ErrorString errorDescription, QUuid requestId);

    void onUpdateTagCompleted(qevercloud::Tag tag, QUuid requestId);

    void onUpdateTagFailed(
        qevercloud::Tag tag, ErrorString errorDescription, QUuid requestId);

    void onAddLinkedNotebookCompleted(
        qevercloud::LinkedNotebook linkedNotebook, QUuid requestId);

    void onAddLinkedNotebookFailed(
        qevercloud::LinkedNotebook linkedNotebook, ErrorString errorDescription,
        QUuid requestId);

    void onUpdateLinkedNotebookCompleted(
        qevercloud::LinkedNotebook linkedNotebook, QUuid requestId);

    void onUpdateLinkedNotebookFailed(
        qevercloud::LinkedNotebook linkedNotebook, ErrorString errorDescription,
        QUuid requestId);

    void onAddSavedSearchCompleted(
        qevercloud::SavedSearch search, QUuid requestId);

    void onAddSavedSearchFailed(
        qevercloud::SavedSearch search, ErrorString errorDescription,
        QUuid requestId);

    void onUpdateSavedSearchCompleted(
        qevercloud::SavedSearch search, QUuid requestId);

    void onUpdateSavedSearchFailed(
        qevercloud::SavedSearch search, ErrorString errorDescription,
        QUuid requestId);

private:
    void createConnections();
    void clear() noexcept;

    void addNotebook();
    void updateNotebook();

    void addNote();
    void updateNote();

    void addTag();
    void updateTag();

    void addLinkedNotebook();
    void updateLinkedNotebook();

    void addSavedSearch();
    void updateSavedSearch();

    enum class State
    {
        STATE_UNINITIALIZED,
        STATE_SENT_NOTEBOOK_ADD_REQUEST,
        STATE_SENT_NOTEBOOK_UPDATE_REQUEST,
        STATE_SENT_NOTE_ADD_REQUEST,
        STATE_SENT_NOTE_UPDATE_REQUEST,
        STATE_SENT_TAG_ADD_REQUEST,
        STATE_SENT_TAG_UPDATE_REQUEST,
        STATE_SENT_LINKED_NOTEBOOK_ADD_REQUEST,
        STATE_SENT_LINKED_NOTEBOOK_UPDATE_REQUEST,
        STATE_SENT_SAVED_SEARCH_ADD_REQUEST,
        STATE_SENT_SAVED_SEARCH_UPDATE_REQUEST
    };

    friend QDebug & operator<<(QDebug & dbg, const State state);

private:
    State m_state = State::STATE_UNINITIALIZED;
    LocalStorageManagerAsync * m_pLocalStorageManagerAsync = nullptr;
    const LocalStorageCacheManager * m_pLocalStorageCacheManager = nullptr;
    QThread * m_pLocalStorageManagerThread = nullptr;

    qevercloud::Notebook m_firstNotebook;
    qevercloud::Notebook m_secondNotebook;
    qevercloud::Notebook m_currentNotebook;
    std::size_t m_addedNotebooksCount = 0;

    qevercloud::Note m_firstNote;
    qevercloud::Note m_secondNote;
    qevercloud::Note m_currentNote;
    std::size_t m_addedNotesCount = 0;

    qevercloud::Tag m_firstTag;
    qevercloud::Tag m_secondTag;
    qevercloud::Tag m_currentTag;
    std::size_t m_addedTagsCount = 0;

    qevercloud::LinkedNotebook m_firstLinkedNotebook;
    qevercloud::LinkedNotebook m_secondLinkedNotebook;
    qevercloud::LinkedNotebook m_currentLinkedNotebook;
    std::size_t m_addedLinkedNotebooksCount = 0;

    qevercloud::SavedSearch m_firstSavedSearch;
    qevercloud::SavedSearch m_secondSavedSearch;
    qevercloud::SavedSearch m_currentSavedSearch;
    std::size_t m_addedSavedSearchesCount = 0;
};

} // namespace test
} // namespace quentier

#endif // LIB_QUENTIER_TESTS_LOCAL_STORAGE_CACHE_ASYNC_TESTER_H
