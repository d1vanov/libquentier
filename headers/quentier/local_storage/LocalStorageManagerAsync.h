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

#ifndef LIB_QUENTIER_LOCAL_STORAGE_LOCAL_STORAGE_MANAGER_ASYNC_H
#define LIB_QUENTIER_LOCAL_STORAGE_LOCAL_STORAGE_MANAGER_ASYNC_H

#include <quentier/local_storage/ILocalStorageCacheExpiryChecker.h>
#include <quentier/local_storage/LocalStorageCacheManager.h>
#include <quentier/local_storage/LocalStorageManager.h>
#include <quentier/types/ErrorString.h>
#include <quentier/types/LinkedNotebook.h>
#include <quentier/types/Note.h>
#include <quentier/types/Notebook.h>
#include <quentier/types/Resource.h>
#include <quentier/types/SavedSearch.h>
#include <quentier/types/SharedNotebook.h>
#include <quentier/types/Tag.h>
#include <quentier/types/User.h>

#include <QObject>

#include <memory>

namespace quentier {

QT_FORWARD_DECLARE_CLASS(LocalStorageManagerAsyncPrivate)

class QUENTIER_EXPORT LocalStorageManagerAsync : public QObject
{
    Q_OBJECT
public:
    explicit LocalStorageManagerAsync(
        const Account & account,
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
        LocalStorageManager::StartupOptions options = {},
#else
        LocalStorageManager::StartupOptions options = 0,
#endif
        QObject * parent = nullptr);

    virtual ~LocalStorageManagerAsync();

    void setUseCache(const bool useCache);

    const LocalStorageCacheManager * localStorageCacheManager() const;

    bool installCacheExpiryFunction(
        const ILocalStorageCacheExpiryChecker & checker);

    const LocalStorageManager * localStorageManager() const;
    LocalStorageManager * localStorageManager();

Q_SIGNALS:
    // Sent when the initialization is complete
    void initialized();

    // User-related signals:
    void getUserCountComplete(int userCount, QUuid requestId);
    void getUserCountFailed(ErrorString errorDescription, QUuid requestId);
    void switchUserComplete(Account account, QUuid requestId);

    void switchUserFailed(
        Account account, ErrorString errorDescription, QUuid requestId);

    void addUserComplete(User user, QUuid requestId);

    void addUserFailed(
        User user, ErrorString errorDescription, QUuid requestId);

    void updateUserComplete(User user, QUuid requestId);

    void updateUserFailed(
        User user, ErrorString errorDescription, QUuid requestId);

    void findUserComplete(User foundUser, QUuid requestId);

    void findUserFailed(
        User user, ErrorString errorDescription, QUuid requestId);

    void deleteUserComplete(User user, QUuid requestId);

    void deleteUserFailed(
        User user, ErrorString errorDescription, QUuid requestId);

    void expungeUserComplete(User user, QUuid requestId);

    void expungeUserFailed(
        User user, ErrorString errorDescription, QUuid requestId);

    // Notebook-related signals:
    void getNotebookCountComplete(int notebookCount, QUuid requestId);
    void getNotebookCountFailed(ErrorString errorDescription, QUuid requestId);
    void addNotebookComplete(Notebook notebook, QUuid requestId);

    void addNotebookFailed(
        Notebook notebook, ErrorString errorDescription, QUuid requestId);

    void updateNotebookComplete(Notebook notebook, QUuid requestId);

    void updateNotebookFailed(
        Notebook notebook, ErrorString errorDescription, QUuid requestId);

    void findNotebookComplete(Notebook foundNotebook, QUuid requestId);

    void findNotebookFailed(
        Notebook notebook, ErrorString errorDescription, QUuid requestId);

    void findDefaultNotebookComplete(Notebook foundNotebook, QUuid requestId);

    void findDefaultNotebookFailed(
        Notebook notebook, ErrorString errorDescription, QUuid requestId);

    void findLastUsedNotebookComplete(Notebook foundNotebook, QUuid requestId);

    void findLastUsedNotebookFailed(
        Notebook notebook, ErrorString errorDescription, QUuid requestId);

    void findDefaultOrLastUsedNotebookComplete(
        Notebook foundNotebook, QUuid requestId);

    void findDefaultOrLastUsedNotebookFailed(
        Notebook notebook, ErrorString errorDescription, QUuid requestId);

    void listAllNotebooksComplete(
        size_t limit, size_t offset,
        LocalStorageManager::ListNotebooksOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QString linkedNotebookGuid, QList<Notebook> foundNotebooks,
        QUuid requestId);

    void listAllNotebooksFailed(
        size_t limit, size_t offset,
        LocalStorageManager::ListNotebooksOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QString linkedNotebookGuid, ErrorString errorDescription,
        QUuid requestId);

    void listNotebooksComplete(
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListNotebooksOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QString linkedNotebookGuid, QList<Notebook> foundNotebooks,
        QUuid requestId);

    void listNotebooksFailed(
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListNotebooksOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QString linkedNotebookGuid, ErrorString errorDescription,
        QUuid requestId);

    void listAllSharedNotebooksComplete(
        QList<SharedNotebook> foundSharedNotebooks, QUuid requestId);

    void listAllSharedNotebooksFailed(
        ErrorString errorDescription, QUuid requestId);

    void listSharedNotebooksPerNotebookGuidComplete(
        QString notebookGuid, QList<SharedNotebook> foundSharedNotebooks,
        QUuid requestId);

    void listSharedNotebooksPerNotebookGuidFailed(
        QString notebookGuid, ErrorString errorDescription, QUuid requestId);

    void expungeNotebookComplete(Notebook notebook, QUuid requestId);

    void expungeNotebookFailed(
        Notebook notebook, ErrorString errorDescription, QUuid requestId);

    // Linked notebook-related signals:
    void getLinkedNotebookCountComplete(
        int linkedNotebookCount, QUuid requestId);

    void getLinkedNotebookCountFailed(
        ErrorString errorDescription, QUuid requestId);

    void addLinkedNotebookComplete(
        LinkedNotebook linkedNotebook, QUuid requestId);

    void addLinkedNotebookFailed(
        LinkedNotebook linkedNotebook, ErrorString errorDescription,
        QUuid requestId);

    void updateLinkedNotebookComplete(
        LinkedNotebook linkedNotebook, QUuid requestId);

    void updateLinkedNotebookFailed(
        LinkedNotebook linkedNotebook, ErrorString errorDescription,
        QUuid requestId);

    void findLinkedNotebookComplete(
        LinkedNotebook foundLinkedNotebook, QUuid requestId);

    void findLinkedNotebookFailed(
        LinkedNotebook linkedNotebook, ErrorString errorDescription,
        QUuid requestId);

    void listAllLinkedNotebooksComplete(
        size_t limit, size_t offset,
        LocalStorageManager::ListLinkedNotebooksOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QList<LinkedNotebook> foundLinkedNotebooks, QUuid requestId);

    void listAllLinkedNotebooksFailed(
        size_t limit, size_t offset,
        LocalStorageManager::ListLinkedNotebooksOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        ErrorString errorDescription, QUuid requestId);

    void listLinkedNotebooksComplete(
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListLinkedNotebooksOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QList<LinkedNotebook> foundLinkedNotebooks, QUuid requestId);

    void listLinkedNotebooksFailed(
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListLinkedNotebooksOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        ErrorString errorDescription, QUuid requestId);

    void expungeLinkedNotebookComplete(
        LinkedNotebook linkedNotebook, QUuid requestId);

    void expungeLinkedNotebookFailed(
        LinkedNotebook linkedNotebook, ErrorString errorDescription,
        QUuid requestId);

    // Note-related signals:
    void getNoteCountComplete(
        int noteCount, LocalStorageManager::NoteCountOptions options,
        QUuid requestId);

    void getNoteCountFailed(
        ErrorString errorDescription,
        LocalStorageManager::NoteCountOptions options, QUuid requestId);

    void getNoteCountPerNotebookComplete(
        int noteCount, Notebook notebook,
        LocalStorageManager::NoteCountOptions options, QUuid requestId);

    void getNoteCountPerNotebookFailed(
        ErrorString errorDescription, Notebook notebook,
        LocalStorageManager::NoteCountOptions options, QUuid requestId);

    void getNoteCountPerTagComplete(
        int noteCount, Tag tag, LocalStorageManager::NoteCountOptions options,
        QUuid requestId);

    void getNoteCountPerTagFailed(
        ErrorString errorDescription, Tag tag,
        LocalStorageManager::NoteCountOptions options, QUuid requestId);

    void getNoteCountsPerAllTagsComplete(
        QHash<QString, int> noteCountsPerTagLocalUid,
        LocalStorageManager::NoteCountOptions options, QUuid requestId);

    void getNoteCountsPerAllTagsFailed(
        ErrorString errorDescription,
        LocalStorageManager::NoteCountOptions options, QUuid requestId);

    void getNoteCountPerNotebooksAndTagsComplete(
        int noteCount, QStringList notebookLocalUids, QStringList tagLocalUids,
        LocalStorageManager::NoteCountOptions options, QUuid requestId);

    void getNoteCountPerNotebooksAndTagsFailed(
        ErrorString errorDescription, QStringList notebookLocalUids,
        QStringList tagLocalUids, LocalStorageManager::NoteCountOptions options,
        QUuid requestId);

    void addNoteComplete(Note note, QUuid requestId);

    void addNoteFailed(
        Note note, ErrorString errorDescription, QUuid requestId);

    void updateNoteComplete(
        Note note, LocalStorageManager::UpdateNoteOptions options,
        QUuid requestId);

    void updateNoteFailed(
        Note note, LocalStorageManager::UpdateNoteOptions options,
        ErrorString errorDescription, QUuid requestId);

    void findNoteComplete(
        Note foundNote, LocalStorageManager::GetNoteOptions options,
        QUuid requestId);

    void findNoteFailed(
        Note note, LocalStorageManager::GetNoteOptions options,
        ErrorString errorDescription, QUuid requestId);

    void listNotesPerNotebookComplete(
        Notebook notebook, LocalStorageManager::GetNoteOptions options,
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListNotesOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QList<Note> foundNotes, QUuid requestId);

    void listNotesPerNotebookFailed(
        Notebook notebook, LocalStorageManager::GetNoteOptions options,
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListNotesOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        ErrorString errorDescription, QUuid requestId);

    void listNotesPerTagComplete(
        Tag tag, LocalStorageManager::GetNoteOptions options,
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListNotesOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QList<Note> foundNotes, QUuid requestId);

    void listNotesPerTagFailed(
        Tag tag, LocalStorageManager::GetNoteOptions options,
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListNotesOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        ErrorString errorDescription, QUuid requestId);

    void listNotesPerNotebooksAndTagsComplete(
        QStringList notebookLocalUids, QStringList tagLocalUids,
        LocalStorageManager::GetNoteOptions options,
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListNotesOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QList<Note> foundNotes, QUuid requestId);

    void listNotesPerNotebooksAndTagsFailed(
        QStringList notebookLocalUids, QStringList tagLocalUids,
        LocalStorageManager::GetNoteOptions options,
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListNotesOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        ErrorString errorDescription, QUuid requestId);

    void listNotesByLocalUidsComplete(
        QStringList noteLocalUids, LocalStorageManager::GetNoteOptions options,
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListNotesOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QList<Note> foundNotes, QUuid requestId);

    void listNotesByLocalUidsFailed(
        QStringList noteLocalUids, LocalStorageManager::GetNoteOptions options,
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListNotesOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        ErrorString errorDescription, QUuid requestId);

    void listNotesComplete(
        LocalStorageManager::ListObjectsOptions flag,
        LocalStorageManager::GetNoteOptions options, size_t limit,
        size_t offset, LocalStorageManager::ListNotesOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QString linkedNotebookGuid, QList<Note> foundNotes, QUuid requestId);

    void listNotesFailed(
        LocalStorageManager::ListObjectsOptions flag,
        LocalStorageManager::GetNoteOptions options, size_t limit,
        size_t offset, LocalStorageManager::ListNotesOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QString linkedNotebookGuid, ErrorString errorDescription,
        QUuid requestId);

    void findNoteLocalUidsWithSearchQueryComplete(
        QStringList noteLocalUids, NoteSearchQuery noteSearchQuery,
        QUuid requestId);

    void findNoteLocalUidsWithSearchQueryFailed(
        NoteSearchQuery noteSearchQuery, ErrorString errorDescription,
        QUuid requestId);

    void expungeNoteComplete(Note note, QUuid requestId);

    void expungeNoteFailed(
        Note note, ErrorString errorDescription, QUuid requestId);

    // Specialized signal emitted alongside updateNoteComplete (after it)
    // if the update of a note causes the change of its notebook
    void noteMovedToAnotherNotebook(
        QString noteLocalUid, QString previousNotebookLocalUid,
        QString newNotebookLocalUid);

    // Specialized signal emitted alongside updateNoteComplete (after it)
    // if the update of a note causes the change of its set of tags
    void noteTagListChanged(
        QString noteLocalUid, QStringList previousNoteTagLocalUids,
        QStringList newNoteTagLocalUids);

    // Tag-related signals:
    void getTagCountComplete(int tagCount, QUuid requestId);
    void getTagCountFailed(ErrorString errorDescription, QUuid requestId);
    void addTagComplete(Tag tag, QUuid requestId);
    void addTagFailed(Tag tag, ErrorString errorDescription, QUuid requestId);
    void updateTagComplete(Tag tag, QUuid requestId);

    void updateTagFailed(
        Tag tag, ErrorString errorDescription, QUuid requestId);

    void linkTagWithNoteComplete(Tag tag, Note note, QUuid requestId);

    void linkTagWithNoteFailed(
        Tag tag, Note note, ErrorString errorDescription, QUuid requestId);

    void findTagComplete(Tag tag, QUuid requestId);
    void findTagFailed(Tag tag, ErrorString errorDescription, QUuid requestId);

    void listAllTagsPerNoteComplete(
        QList<Tag> foundTags, Note note,
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListTagsOrder order,
        LocalStorageManager::OrderDirection orderDirection, QUuid requestId);

    void listAllTagsPerNoteFailed(
        Note note, LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListTagsOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        ErrorString errorDescription, QUuid requestId);

    void listAllTagsComplete(
        size_t limit, size_t offset, LocalStorageManager::ListTagsOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QString linkedNotebookGuid, QList<Tag> foundTags, QUuid requestId);

    void listAllTagsFailed(
        size_t limit, size_t offset, LocalStorageManager::ListTagsOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QString linkedNotebookGuid, ErrorString errorDescription,
        QUuid requestId);

    void listTagsComplete(
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListTagsOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QString linkedNotebookGuid, QList<Tag> foundTags,
        QUuid requestId = QUuid());

    void listTagsFailed(
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListTagsOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QString linkedNotebookGuid, ErrorString errorDescription,
        QUuid requestId);

    void listTagsWithNoteLocalUidsComplete(
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListTagsOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QString linkedNotebookGuid,
        QList<std::pair<Tag, QStringList>> foundTags, QUuid requestId);

    void listTagsWithNoteLocalUidsFailed(
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListTagsOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QString linkedNotebookGuid, ErrorString errorDescription,
        QUuid requestId);

    void expungeTagComplete(
        Tag tag, QStringList expungedChildTagLocalUids, QUuid requestId);

    void expungeTagFailed(
        Tag tag, ErrorString errorDescription, QUuid requestId);

    void expungeNotelessTagsFromLinkedNotebooksComplete(QUuid requestId);

    void expungeNotelessTagsFromLinkedNotebooksFailed(
        ErrorString errorDescription, QUuid requestId);

    // Resource-related signals:
    void getResourceCountComplete(int resourceCount, QUuid requestId);
    void getResourceCountFailed(ErrorString errorDescription, QUuid requestId);
    void addResourceComplete(Resource resource, QUuid requestId);

    void addResourceFailed(
        Resource resource, ErrorString errorDescription, QUuid requestId);

    void updateResourceComplete(Resource resource, QUuid requestId);

    void updateResourceFailed(
        Resource resource, ErrorString errorDescription, QUuid requestId);

    void findResourceComplete(
        Resource resource, LocalStorageManager::GetResourceOptions options,
        QUuid requestId);

    void findResourceFailed(
        Resource resource, LocalStorageManager::GetResourceOptions options,
        ErrorString errorDescription, QUuid requestId);

    void expungeResourceComplete(Resource resource, QUuid requestId);

    void expungeResourceFailed(
        Resource resource, ErrorString errorDescription, QUuid requestId);

    // Saved search-related signals:
    void getSavedSearchCountComplete(int savedSearchCount, QUuid requestId);

    void getSavedSearchCountFailed(
        ErrorString errorDescription, QUuid requestId);

    void addSavedSearchComplete(SavedSearch search, QUuid requestId);

    void addSavedSearchFailed(
        SavedSearch search, ErrorString errorDescription, QUuid requestId);

    void updateSavedSearchComplete(SavedSearch search, QUuid requestId);

    void updateSavedSearchFailed(
        SavedSearch search, ErrorString errorDescription, QUuid requestId);

    void findSavedSearchComplete(SavedSearch search, QUuid requestId);

    void findSavedSearchFailed(
        SavedSearch search, ErrorString errorDescription, QUuid requestId);

    void listAllSavedSearchesComplete(
        size_t limit, size_t offset,
        LocalStorageManager::ListSavedSearchesOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QList<SavedSearch> foundSearches, QUuid requestId);

    void listAllSavedSearchesFailed(
        size_t limit, size_t offset,
        LocalStorageManager::ListSavedSearchesOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        ErrorString errorDescription, QUuid requestId);

    void listSavedSearchesComplete(
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListSavedSearchesOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QList<SavedSearch> foundSearches, QUuid requestId);

    void listSavedSearchesFailed(
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListSavedSearchesOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        ErrorString errorDescription, QUuid requestId);

    void expungeSavedSearchComplete(SavedSearch search, QUuid requestId);

    void expungeSavedSearchFailed(
        SavedSearch search, ErrorString errorDescription, QUuid requestId);

    void accountHighUsnComplete(
        qint32 usn, QString linkedNotebookGuid, QUuid requestId);

    void accountHighUsnFailed(
        QString linkedNotebookGuid, ErrorString errorDescription,
        QUuid requestId);

public Q_SLOTS:
    void init();

    // User-related slots:
    void onGetUserCountRequest(QUuid requestId);

    void onSwitchUserRequest(
        Account account, LocalStorageManager::StartupOptions startupOptions,
        QUuid requestId);

    void onAddUserRequest(User user, QUuid requestId);
    void onUpdateUserRequest(User user, QUuid requestId);
    void onFindUserRequest(User user, QUuid requestId);
    void onDeleteUserRequest(User user, QUuid requestId);
    void onExpungeUserRequest(User user, QUuid requestId);

    // Notebook-related slots:
    void onGetNotebookCountRequest(QUuid requestId);
    void onAddNotebookRequest(Notebook notebook, QUuid requestId);
    void onUpdateNotebookRequest(Notebook notebook, QUuid requestId);
    void onFindNotebookRequest(Notebook notebook, QUuid requestId);
    void onFindDefaultNotebookRequest(Notebook notebook, QUuid requestId);
    void onFindLastUsedNotebookRequest(Notebook notebook, QUuid requestId);

    void onFindDefaultOrLastUsedNotebookRequest(
        Notebook notebook, QUuid requestId);

    void onListAllNotebooksRequest(
        size_t limit, size_t offset,
        LocalStorageManager::ListNotebooksOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QString linkedNotebookGuid, QUuid requestId);

    void onListAllSharedNotebooksRequest(QUuid requestId);

    void onListNotebooksRequest(
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListNotebooksOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QString linkedNotebookGuid, QUuid requestId);

    void onListSharedNotebooksPerNotebookGuidRequest(
        QString notebookGuid, QUuid requestId);

    void onExpungeNotebookRequest(Notebook notebook, QUuid requestId);

    // Linked notebook-related slots:
    void onGetLinkedNotebookCountRequest(QUuid requestId);

    void onAddLinkedNotebookRequest(
        LinkedNotebook linkedNotebook, QUuid requestId);

    void onUpdateLinkedNotebookRequest(
        LinkedNotebook linkedNotebook, QUuid requestId);

    void onFindLinkedNotebookRequest(
        LinkedNotebook linkedNotebook, QUuid requestId);

    void onListAllLinkedNotebooksRequest(
        size_t limit, size_t offset,
        LocalStorageManager::ListLinkedNotebooksOrder order,
        LocalStorageManager::OrderDirection orderDirection, QUuid requestId);

    void onListLinkedNotebooksRequest(
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListLinkedNotebooksOrder order,
        LocalStorageManager::OrderDirection orderDirection, QUuid requestId);

    void onExpungeLinkedNotebookRequest(
        LinkedNotebook linkedNotebook, QUuid requestId);

    // Note-related slots:
    void onGetNoteCountRequest(
        LocalStorageManager::NoteCountOptions options, QUuid requestId);

    void onGetNoteCountPerNotebookRequest(
        Notebook notebook, LocalStorageManager::NoteCountOptions options,
        QUuid requestId);

    void onGetNoteCountPerTagRequest(
        Tag tag, LocalStorageManager::NoteCountOptions options,
        QUuid requestId);

    void onGetNoteCountsPerAllTagsRequest(
        LocalStorageManager::NoteCountOptions options, QUuid requestId);

    void onGetNoteCountPerNotebooksAndTagsRequest(
        QStringList notebookLocalUids, QStringList tagLocalUids,
        LocalStorageManager::NoteCountOptions options, QUuid requestId);

    void onAddNoteRequest(Note note, QUuid requestId);

    void onUpdateNoteRequest(
        Note note, LocalStorageManager::UpdateNoteOptions options,
        QUuid requestId);

    void onFindNoteRequest(
        Note note, LocalStorageManager::GetNoteOptions options,
        QUuid requestId);

    void onListNotesPerNotebookRequest(
        Notebook notebook, LocalStorageManager::GetNoteOptions options,
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListNotesOrder order,
        LocalStorageManager::OrderDirection orderDirection, QUuid requestId);

    void onListNotesPerTagRequest(
        Tag tag, LocalStorageManager::GetNoteOptions options,
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListNotesOrder order,
        LocalStorageManager::OrderDirection orderDirection, QUuid requestId);

    void onListNotesPerNotebooksAndTagsRequest(
        QStringList notebookLocalUids, QStringList tagLocalUids,
        LocalStorageManager::GetNoteOptions options,
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListNotesOrder order,
        LocalStorageManager::OrderDirection orderDirection, QUuid requestId);

    void onListNotesByLocalUidsRequest(
        QStringList noteLocalUids, LocalStorageManager::GetNoteOptions options,
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListNotesOrder order,
        LocalStorageManager::OrderDirection orderDirection, QUuid requestId);

    void onListNotesRequest(
        LocalStorageManager::ListObjectsOptions flag,
        LocalStorageManager::GetNoteOptions options, size_t limit,
        size_t offset, LocalStorageManager::ListNotesOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QString linkedNotebookGuid, QUuid requestId);

    void onFindNoteLocalUidsWithSearchQuery(
        NoteSearchQuery noteSearchQuery, QUuid requestId);

    void onExpungeNoteRequest(Note note, QUuid requestId);

    // Tag-related slots:
    void onGetTagCountRequest(QUuid requestId);
    void onAddTagRequest(Tag tag, QUuid requestId);
    void onUpdateTagRequest(Tag tag, QUuid requestId);
    void onFindTagRequest(Tag tag, QUuid requestId);

    void onListAllTagsPerNoteRequest(
        Note note, LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListTagsOrder order,
        LocalStorageManager::OrderDirection orderDirection, QUuid requestId);

    void onListAllTagsRequest(
        size_t limit, size_t offset, LocalStorageManager::ListTagsOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QString linkedNotebookGuid, QUuid requestId);

    void onListTagsRequest(
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListTagsOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QString linkedNotebookGuid, QUuid requestId);

    void onListTagsWithNoteLocalUidsRequest(
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListTagsOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QString linkedNotebookGuid, QUuid requestId);

    void onExpungeTagRequest(Tag tag, QUuid requestId);
    void onExpungeNotelessTagsFromLinkedNotebooksRequest(QUuid requestId);

    // Resource-related slots:
    void onGetResourceCountRequest(QUuid requestId);
    void onAddResourceRequest(Resource resource, QUuid requestId);
    void onUpdateResourceRequest(Resource resource, QUuid requestId);

    void onFindResourceRequest(
        Resource resource, LocalStorageManager::GetResourceOptions options,
        QUuid requestId);

    void onExpungeResourceRequest(Resource resource, QUuid requestId);

    // Saved search-related slots:
    void onGetSavedSearchCountRequest(QUuid requestId);
    void onAddSavedSearchRequest(SavedSearch search, QUuid requestId);
    void onUpdateSavedSearchRequest(SavedSearch search, QUuid requestId);
    void onFindSavedSearchRequest(SavedSearch search, QUuid requestId);

    void onListAllSavedSearchesRequest(
        size_t limit, size_t offset,
        LocalStorageManager::ListSavedSearchesOrder order,
        LocalStorageManager::OrderDirection orderDirection, QUuid requestId);

    void onListSavedSearchesRequest(
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListSavedSearchesOrder order,
        LocalStorageManager::OrderDirection orderDirection, QUuid requestId);

    void onExpungeSavedSearchRequest(SavedSearch search, QUuid requestId);

    void onAccountHighUsnRequest(QString linkedNotebookGuid, QUuid requestId);

private:
    LocalStorageManagerAsync() = delete;
    Q_DISABLE_COPY(LocalStorageManagerAsync)

    LocalStorageManagerAsyncPrivate * const d_ptr;
    Q_DECLARE_PRIVATE(LocalStorageManagerAsync)
};

} // namespace quentier

#endif // LIB_QUENTIER_LOCAL_STORAGE_LOCAL_STORAGE_MANAGER_ASYNC_H
