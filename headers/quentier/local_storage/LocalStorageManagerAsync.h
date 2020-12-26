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

#include <qevercloud/generated/types/LinkedNotebook.h>
#include <qevercloud/generated/types/Note.h>
#include <qevercloud/generated/types/Notebook.h>
#include <qevercloud/generated/types/Resource.h>
#include <qevercloud/generated/types/SavedSearch.h>
#include <qevercloud/generated/types/SharedNotebook.h>
#include <qevercloud/generated/types/Tag.h>
#include <qevercloud/generated/types/User.h>

#include <QObject>

#include <memory>

namespace quentier {

class LocalStorageManagerAsyncPrivate;

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

    ~LocalStorageManagerAsync() noexcept override;

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

    void addUserComplete(qevercloud::User user, QUuid requestId);

    void addUserFailed(
        qevercloud::User user, ErrorString errorDescription, QUuid requestId);

    void updateUserComplete(qevercloud::User user, QUuid requestId);

    void updateUserFailed(
        qevercloud::User user, ErrorString errorDescription, QUuid requestId);

    void findUserComplete(qevercloud::User foundUser, QUuid requestId);

    void findUserFailed(
        qevercloud::User user, ErrorString errorDescription, QUuid requestId);

    void deleteUserComplete(qevercloud::User user, QUuid requestId);

    void deleteUserFailed(
        qevercloud::User user, ErrorString errorDescription, QUuid requestId);

    void expungeUserComplete(qevercloud::User user, QUuid requestId);

    void expungeUserFailed(
        qevercloud::User user, ErrorString errorDescription, QUuid requestId);

    // Notebook-related signals:
    void getNotebookCountComplete(int notebookCount, QUuid requestId);
    void getNotebookCountFailed(ErrorString errorDescription, QUuid requestId);
    void addNotebookComplete(qevercloud::Notebook notebook, QUuid requestId);

    void addNotebookFailed(
        qevercloud::Notebook notebook, ErrorString errorDescription,
        QUuid requestId);

    void updateNotebookComplete(qevercloud::Notebook notebook, QUuid requestId);

    void updateNotebookFailed(
        qevercloud::Notebook notebook, ErrorString errorDescription,
        QUuid requestId);

    void findNotebookComplete(
        qevercloud::Notebook foundNotebook, QUuid requestId);

    void findNotebookFailed(
        qevercloud::Notebook notebook, ErrorString errorDescription,
        QUuid requestId);

    void findDefaultNotebookComplete(
        qevercloud::Notebook foundNotebook, QUuid requestId);

    void findDefaultNotebookFailed(
        qevercloud::Notebook notebook, ErrorString errorDescription,
        QUuid requestId);

    void findLastUsedNotebookComplete(
        qevercloud::Notebook foundNotebook, QUuid requestId);

    void findLastUsedNotebookFailed(
        qevercloud::Notebook notebook, ErrorString errorDescription,
        QUuid requestId);

    void findDefaultOrLastUsedNotebookComplete(
        qevercloud::Notebook foundNotebook, QUuid requestId);

    void findDefaultOrLastUsedNotebookFailed(
        qevercloud::Notebook notebook, ErrorString errorDescription,
        QUuid requestId);

    void listAllNotebooksComplete(
        size_t limit, size_t offset,
        LocalStorageManager::ListNotebooksOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QString linkedNotebookGuid, QList<qevercloud::Notebook> foundNotebooks,
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
        QString linkedNotebookGuid, QList<qevercloud::Notebook> foundNotebooks,
        QUuid requestId);

    void listNotebooksFailed(
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListNotebooksOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QString linkedNotebookGuid, ErrorString errorDescription,
        QUuid requestId);

    void listAllSharedNotebooksComplete(
        QList<qevercloud::SharedNotebook> foundSharedNotebooks,
        QUuid requestId);

    void listAllSharedNotebooksFailed(
        ErrorString errorDescription, QUuid requestId);

    void listSharedNotebooksPerNotebookGuidComplete(
        QString notebookGuid,
        QList<qevercloud::SharedNotebook> foundSharedNotebooks,
        QUuid requestId);

    void listSharedNotebooksPerNotebookGuidFailed(
        QString notebookGuid, ErrorString errorDescription, QUuid requestId);

    void expungeNotebookComplete(
        qevercloud::Notebook notebook, QUuid requestId);

    void expungeNotebookFailed(
        qevercloud::Notebook notebook, ErrorString errorDescription,
        QUuid requestId);

    // Linked notebook-related signals:
    void getLinkedNotebookCountComplete(
        int linkedNotebookCount, QUuid requestId);

    void getLinkedNotebookCountFailed(
        ErrorString errorDescription, QUuid requestId);

    void addLinkedNotebookComplete(
        qevercloud::LinkedNotebook linkedNotebook, QUuid requestId);

    void addLinkedNotebookFailed(
        qevercloud::LinkedNotebook linkedNotebook, ErrorString errorDescription,
        QUuid requestId);

    void updateLinkedNotebookComplete(
        qevercloud::LinkedNotebook linkedNotebook, QUuid requestId);

    void updateLinkedNotebookFailed(
        qevercloud::LinkedNotebook linkedNotebook, ErrorString errorDescription,
        QUuid requestId);

    void findLinkedNotebookComplete(
        qevercloud::LinkedNotebook foundLinkedNotebook, QUuid requestId);

    void findLinkedNotebookFailed(
        qevercloud::LinkedNotebook linkedNotebook, ErrorString errorDescription,
        QUuid requestId);

    void listAllLinkedNotebooksComplete(
        size_t limit, size_t offset,
        LocalStorageManager::ListLinkedNotebooksOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QList<qevercloud::LinkedNotebook> foundLinkedNotebooks,
        QUuid requestId);

    void listAllLinkedNotebooksFailed(
        size_t limit, size_t offset,
        LocalStorageManager::ListLinkedNotebooksOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        ErrorString errorDescription, QUuid requestId);

    void listLinkedNotebooksComplete(
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListLinkedNotebooksOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QList<qevercloud::LinkedNotebook> foundLinkedNotebooks,
        QUuid requestId);

    void listLinkedNotebooksFailed(
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListLinkedNotebooksOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        ErrorString errorDescription, QUuid requestId);

    void expungeLinkedNotebookComplete(
        qevercloud::LinkedNotebook linkedNotebook, QUuid requestId);

    void expungeLinkedNotebookFailed(
        qevercloud::LinkedNotebook linkedNotebook, ErrorString errorDescription,
        QUuid requestId);

    // Note-related signals:
    void getNoteCountComplete(
        int noteCount, LocalStorageManager::NoteCountOptions options,
        QUuid requestId);

    void getNoteCountFailed(
        ErrorString errorDescription,
        LocalStorageManager::NoteCountOptions options, QUuid requestId);

    void getNoteCountPerNotebookComplete(
        int noteCount, qevercloud::Notebook notebook,
        LocalStorageManager::NoteCountOptions options, QUuid requestId);

    void getNoteCountPerNotebookFailed(
        ErrorString errorDescription, qevercloud::Notebook notebook,
        LocalStorageManager::NoteCountOptions options, QUuid requestId);

    void getNoteCountPerTagComplete(
        int noteCount, qevercloud::Tag tag,
        LocalStorageManager::NoteCountOptions options,
        QUuid requestId);

    void getNoteCountPerTagFailed(
        ErrorString errorDescription, qevercloud::Tag tag,
        LocalStorageManager::NoteCountOptions options, QUuid requestId);

    void getNoteCountsPerAllTagsComplete(
        QHash<QString, int> noteCountsPerTagLocalId,
        LocalStorageManager::NoteCountOptions options, QUuid requestId);

    void getNoteCountsPerAllTagsFailed(
        ErrorString errorDescription,
        LocalStorageManager::NoteCountOptions options, QUuid requestId);

    void getNoteCountPerNotebooksAndTagsComplete(
        int noteCount, QStringList notebookLocalIds, QStringList tagLocalIds,
        LocalStorageManager::NoteCountOptions options, QUuid requestId);

    void getNoteCountPerNotebooksAndTagsFailed(
        ErrorString errorDescription, QStringList notebookLocalIds,
        QStringList tagLocalIds, LocalStorageManager::NoteCountOptions options,
        QUuid requestId);

    void addNoteComplete(qevercloud::Note note, QUuid requestId);

    void addNoteFailed(
        qevercloud::Note note, ErrorString errorDescription, QUuid requestId);

    void updateNoteComplete(
        qevercloud::Note note, LocalStorageManager::UpdateNoteOptions options,
        QUuid requestId);

    void updateNoteFailed(
        qevercloud::Note note, LocalStorageManager::UpdateNoteOptions options,
        ErrorString errorDescription, QUuid requestId);

    void findNoteComplete(
        qevercloud::Note foundNote, LocalStorageManager::GetNoteOptions options,
        QUuid requestId);

    void findNoteFailed(
        qevercloud::Note note, LocalStorageManager::GetNoteOptions options,
        ErrorString errorDescription, QUuid requestId);

    void listNotesPerNotebookComplete(
        qevercloud::Notebook notebook,
        LocalStorageManager::GetNoteOptions options,
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListNotesOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QList<qevercloud::Note> foundNotes, QUuid requestId);

    void listNotesPerNotebookFailed(
        qevercloud::Notebook notebook,
        LocalStorageManager::GetNoteOptions options,
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListNotesOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        ErrorString errorDescription, QUuid requestId);

    void listNotesPerTagComplete(
        qevercloud::Tag tag, LocalStorageManager::GetNoteOptions options,
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListNotesOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QList<qevercloud::Note> foundNotes, QUuid requestId);

    void listNotesPerTagFailed(
        qevercloud::Tag tag, LocalStorageManager::GetNoteOptions options,
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListNotesOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        ErrorString errorDescription, QUuid requestId);

    void listNotesPerNotebooksAndTagsComplete(
        QStringList notebookLocalIds, QStringList tagLocalIds,
        LocalStorageManager::GetNoteOptions options,
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListNotesOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QList<qevercloud::Note> foundNotes, QUuid requestId);

    void listNotesPerNotebooksAndTagsFailed(
        QStringList notebookLocalIds, QStringList tagLocalIds,
        LocalStorageManager::GetNoteOptions options,
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListNotesOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        ErrorString errorDescription, QUuid requestId);

    void listNotesByLocalIdsComplete(
        QStringList noteLocalIds, LocalStorageManager::GetNoteOptions options,
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListNotesOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QList<qevercloud::Note> foundNotes, QUuid requestId);

    void listNotesByLocalIdsFailed(
        QStringList noteLocalIds, LocalStorageManager::GetNoteOptions options,
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListNotesOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        ErrorString errorDescription, QUuid requestId);

    void listNotesComplete(
        LocalStorageManager::ListObjectsOptions flag,
        LocalStorageManager::GetNoteOptions options, size_t limit,
        size_t offset, LocalStorageManager::ListNotesOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QString linkedNotebookGuid, QList<qevercloud::Note> foundNotes,
        QUuid requestId);

    void listNotesFailed(
        LocalStorageManager::ListObjectsOptions flag,
        LocalStorageManager::GetNoteOptions options, size_t limit,
        size_t offset, LocalStorageManager::ListNotesOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QString linkedNotebookGuid, ErrorString errorDescription,
        QUuid requestId);

    void findNoteLocalIdsWithSearchQueryComplete(
        QStringList noteLocalIds, NoteSearchQuery noteSearchQuery,
        QUuid requestId);

    void findNoteLocalIdsWithSearchQueryFailed(
        NoteSearchQuery noteSearchQuery, ErrorString errorDescription,
        QUuid requestId);

    void expungeNoteComplete(qevercloud::Note note, QUuid requestId);

    void expungeNoteFailed(
        qevercloud::Note note, ErrorString errorDescription, QUuid requestId);

    // Specialized signal emitted alongside updateNoteComplete (after it)
    // if the update of a note causes the change of its notebook
    void noteMovedToAnotherNotebook(
        QString noteLocalId, QString previousNotebookLocalId,
        QString newNotebookLocalId);

    // Specialized signal emitted alongside updateNoteComplete (after it)
    // if the update of a note causes the change of its set of tags
    void noteTagListChanged(
        QString noteLocalId, QStringList previousNoteTagLocalIds,
        QStringList newNoteTagLocalIds);

    // Tag-related signals:
    void getTagCountComplete(int tagCount, QUuid requestId);
    void getTagCountFailed(ErrorString errorDescription, QUuid requestId);
    void addTagComplete(qevercloud::Tag tag, QUuid requestId);

    void addTagFailed(
        qevercloud::Tag tag, ErrorString errorDescription, QUuid requestId);

    void updateTagComplete(qevercloud::Tag tag, QUuid requestId);

    void updateTagFailed(
        qevercloud::Tag tag, ErrorString errorDescription, QUuid requestId);

    void linkTagWithNoteComplete(
        qevercloud::Tag tag, qevercloud::Note note, QUuid requestId);

    void linkTagWithNoteFailed(
        qevercloud::Tag tag, qevercloud::Note note,
        ErrorString errorDescription, QUuid requestId);

    void findTagComplete(qevercloud::Tag tag, QUuid requestId);

    void findTagFailed(
        qevercloud::Tag tag, ErrorString errorDescription, QUuid requestId);

    void listAllTagsPerNoteComplete(
        QList<qevercloud::Tag> foundTags, qevercloud::Note note,
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListTagsOrder order,
        LocalStorageManager::OrderDirection orderDirection, QUuid requestId);

    void listAllTagsPerNoteFailed(
        qevercloud::Note note, LocalStorageManager::ListObjectsOptions flag,
        size_t limit, size_t offset, LocalStorageManager::ListTagsOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        ErrorString errorDescription, QUuid requestId);

    void listAllTagsComplete(
        size_t limit, size_t offset, LocalStorageManager::ListTagsOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QString linkedNotebookGuid, QList<qevercloud::Tag> foundTags,
        QUuid requestId);

    void listAllTagsFailed(
        size_t limit, size_t offset, LocalStorageManager::ListTagsOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QString linkedNotebookGuid, ErrorString errorDescription,
        QUuid requestId);

    void listTagsComplete(
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListTagsOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QString linkedNotebookGuid, QList<qevercloud::Tag> foundTags,
        QUuid requestId);

    void listTagsFailed(
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListTagsOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QString linkedNotebookGuid, ErrorString errorDescription,
        QUuid requestId);

    void listTagsWithNoteLocalIdsComplete(
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListTagsOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QString linkedNotebookGuid,
        QList<std::pair<qevercloud::Tag, QStringList>> foundTags,
        QUuid requestId);

    void listTagsWithNoteLocalIdsFailed(
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListTagsOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QString linkedNotebookGuid, ErrorString errorDescription,
        QUuid requestId);

    void expungeTagComplete(
        qevercloud::Tag tag, QStringList expungedChildTagLocalIds,
        QUuid requestId);

    void expungeTagFailed(
        qevercloud::Tag tag, ErrorString errorDescription, QUuid requestId);

    void expungeNotelessTagsFromLinkedNotebooksComplete(QUuid requestId);

    void expungeNotelessTagsFromLinkedNotebooksFailed(
        ErrorString errorDescription, QUuid requestId);

    // Resource-related signals:
    void getResourceCountComplete(int resourceCount, QUuid requestId);
    void getResourceCountFailed(ErrorString errorDescription, QUuid requestId);
    void addResourceComplete(qevercloud::Resource resource, QUuid requestId);

    void addResourceFailed(
        qevercloud::Resource resource, ErrorString errorDescription,
        QUuid requestId);

    void updateResourceComplete(qevercloud::Resource resource, QUuid requestId);

    void updateResourceFailed(
        qevercloud::Resource resource, ErrorString errorDescription,
        QUuid requestId);

    void findResourceComplete(
        qevercloud::Resource resource,
        LocalStorageManager::GetResourceOptions options, QUuid requestId);

    void findResourceFailed(
        qevercloud::Resource resource,
        LocalStorageManager::GetResourceOptions options,
        ErrorString errorDescription, QUuid requestId);

    void expungeResourceComplete(
        qevercloud::Resource resource, QUuid requestId);

    void expungeResourceFailed(
        qevercloud::Resource resource, ErrorString errorDescription,
        QUuid requestId);

    // Saved search-related signals:
    void getSavedSearchCountComplete(int savedSearchCount, QUuid requestId);

    void getSavedSearchCountFailed(
        ErrorString errorDescription, QUuid requestId);

    void addSavedSearchComplete(
        qevercloud::SavedSearch search, QUuid requestId);

    void addSavedSearchFailed(
        qevercloud::SavedSearch search, ErrorString errorDescription,
        QUuid requestId);

    void updateSavedSearchComplete(
        qevercloud::SavedSearch search, QUuid requestId);

    void updateSavedSearchFailed(
        qevercloud::SavedSearch search, ErrorString errorDescription,
        QUuid requestId);

    void findSavedSearchComplete(
        qevercloud::SavedSearch search, QUuid requestId);

    void findSavedSearchFailed(
        qevercloud::SavedSearch search, ErrorString errorDescription,
        QUuid requestId);

    void listAllSavedSearchesComplete(
        size_t limit, size_t offset,
        LocalStorageManager::ListSavedSearchesOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QList<qevercloud::SavedSearch> foundSearches, QUuid requestId);

    void listAllSavedSearchesFailed(
        size_t limit, size_t offset,
        LocalStorageManager::ListSavedSearchesOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        ErrorString errorDescription, QUuid requestId);

    void listSavedSearchesComplete(
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListSavedSearchesOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QList<qevercloud::SavedSearch> foundSearches, QUuid requestId);

    void listSavedSearchesFailed(
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListSavedSearchesOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        ErrorString errorDescription, QUuid requestId);

    void expungeSavedSearchComplete(
        qevercloud::SavedSearch search, QUuid requestId);

    void expungeSavedSearchFailed(
        qevercloud::SavedSearch search, ErrorString errorDescription,
        QUuid requestId);

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

    void onAddUserRequest(qevercloud::User user, QUuid requestId);
    void onUpdateUserRequest(qevercloud::User user, QUuid requestId);
    void onFindUserRequest(qevercloud::User user, QUuid requestId);
    void onDeleteUserRequest(qevercloud::User user, QUuid requestId);
    void onExpungeUserRequest(qevercloud::User user, QUuid requestId);

    // Notebook-related slots:
    void onGetNotebookCountRequest(QUuid requestId);
    void onAddNotebookRequest(qevercloud::Notebook notebook, QUuid requestId);

    void onUpdateNotebookRequest(
        qevercloud::Notebook notebook, QUuid requestId);

    void onFindNotebookRequest(qevercloud::Notebook notebook, QUuid requestId);

    void onFindDefaultNotebookRequest(
        qevercloud::Notebook notebook, QUuid requestId);

    void onFindLastUsedNotebookRequest(
        qevercloud::Notebook notebook, QUuid requestId);

    void onFindDefaultOrLastUsedNotebookRequest(
        qevercloud::Notebook notebook, QUuid requestId);

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

    void onExpungeNotebookRequest(
        qevercloud::Notebook notebook, QUuid requestId);

    // Linked notebook-related slots:
    void onGetLinkedNotebookCountRequest(QUuid requestId);

    void onAddLinkedNotebookRequest(
        qevercloud::LinkedNotebook linkedNotebook, QUuid requestId);

    void onUpdateLinkedNotebookRequest(
        qevercloud::LinkedNotebook linkedNotebook, QUuid requestId);

    void onFindLinkedNotebookRequest(
        qevercloud::LinkedNotebook linkedNotebook, QUuid requestId);

    void onListAllLinkedNotebooksRequest(
        size_t limit, size_t offset,
        LocalStorageManager::ListLinkedNotebooksOrder order,
        LocalStorageManager::OrderDirection orderDirection, QUuid requestId);

    void onListLinkedNotebooksRequest(
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListLinkedNotebooksOrder order,
        LocalStorageManager::OrderDirection orderDirection, QUuid requestId);

    void onExpungeLinkedNotebookRequest(
        qevercloud::LinkedNotebook linkedNotebook, QUuid requestId);

    // Note-related slots:
    void onGetNoteCountRequest(
        LocalStorageManager::NoteCountOptions options, QUuid requestId);

    void onGetNoteCountPerNotebookRequest(
        qevercloud::Notebook notebook,
        LocalStorageManager::NoteCountOptions options, QUuid requestId);

    void onGetNoteCountPerTagRequest(
        qevercloud::Tag tag, LocalStorageManager::NoteCountOptions options,
        QUuid requestId);

    void onGetNoteCountsPerAllTagsRequest(
        LocalStorageManager::NoteCountOptions options, QUuid requestId);

    void onGetNoteCountPerNotebooksAndTagsRequest(
        QStringList notebookLocalIds, QStringList tagLocalIds,
        LocalStorageManager::NoteCountOptions options, QUuid requestId);

    void onAddNoteRequest(qevercloud::Note note, QUuid requestId);

    void onUpdateNoteRequest(
        qevercloud::Note note, LocalStorageManager::UpdateNoteOptions options,
        QUuid requestId);

    void onFindNoteRequest(
        qevercloud::Note note, LocalStorageManager::GetNoteOptions options,
        QUuid requestId);

    void onListNotesPerNotebookRequest(
        qevercloud::Notebook notebook,
        LocalStorageManager::GetNoteOptions options,
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListNotesOrder order,
        LocalStorageManager::OrderDirection orderDirection, QUuid requestId);

    void onListNotesPerTagRequest(
        qevercloud::Tag tag, LocalStorageManager::GetNoteOptions options,
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListNotesOrder order,
        LocalStorageManager::OrderDirection orderDirection, QUuid requestId);

    void onListNotesPerNotebooksAndTagsRequest(
        QStringList notebookLocalIds, QStringList tagLocalIds,
        LocalStorageManager::GetNoteOptions options,
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListNotesOrder order,
        LocalStorageManager::OrderDirection orderDirection, QUuid requestId);

    void onListNotesByLocalIdsRequest(
        QStringList noteLocalIds, LocalStorageManager::GetNoteOptions options,
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListNotesOrder order,
        LocalStorageManager::OrderDirection orderDirection, QUuid requestId);

    void onListNotesRequest(
        LocalStorageManager::ListObjectsOptions flag,
        LocalStorageManager::GetNoteOptions options, size_t limit,
        size_t offset, LocalStorageManager::ListNotesOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QString linkedNotebookGuid, QUuid requestId);

    void onFindNoteLocalIdsWithSearchQuery(
        NoteSearchQuery noteSearchQuery, QUuid requestId);

    void onExpungeNoteRequest(qevercloud::Note note, QUuid requestId);

    // Tag-related slots:
    void onGetTagCountRequest(QUuid requestId);
    void onAddTagRequest(qevercloud::Tag tag, QUuid requestId);
    void onUpdateTagRequest(qevercloud::Tag tag, QUuid requestId);
    void onFindTagRequest(qevercloud::Tag tag, QUuid requestId);

    void onListAllTagsPerNoteRequest(
        qevercloud::Note note, LocalStorageManager::ListObjectsOptions flag,
        size_t limit, size_t offset, LocalStorageManager::ListTagsOrder order,
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

    void onListTagsWithNoteLocalIdsRequest(
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListTagsOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QString linkedNotebookGuid, QUuid requestId);

    void onExpungeTagRequest(qevercloud::Tag tag, QUuid requestId);
    void onExpungeNotelessTagsFromLinkedNotebooksRequest(QUuid requestId);

    // Resource-related slots:
    void onGetResourceCountRequest(QUuid requestId);
    void onAddResourceRequest(qevercloud::Resource resource, QUuid requestId);

    void onUpdateResourceRequest(
        qevercloud::Resource resource, QUuid requestId);

    void onFindResourceRequest(
        qevercloud::Resource resource,
        LocalStorageManager::GetResourceOptions options,
        QUuid requestId);

    void onExpungeResourceRequest(
        qevercloud::Resource resource, QUuid requestId);

    // Saved search-related slots:
    void onGetSavedSearchCountRequest(QUuid requestId);

    void onAddSavedSearchRequest(
        qevercloud::SavedSearch search, QUuid requestId);

    void onUpdateSavedSearchRequest(
        qevercloud::SavedSearch search, QUuid requestId);

    void onFindSavedSearchRequest(
        qevercloud::SavedSearch search, QUuid requestId);

    void onListAllSavedSearchesRequest(
        size_t limit, size_t offset,
        LocalStorageManager::ListSavedSearchesOrder order,
        LocalStorageManager::OrderDirection orderDirection, QUuid requestId);

    void onListSavedSearchesRequest(
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListSavedSearchesOrder order,
        LocalStorageManager::OrderDirection orderDirection, QUuid requestId);

    void onExpungeSavedSearchRequest(
        qevercloud::SavedSearch search, QUuid requestId);

    void onAccountHighUsnRequest(QString linkedNotebookGuid, QUuid requestId);

private:
    LocalStorageManagerAsync() = delete;
    Q_DISABLE_COPY(LocalStorageManagerAsync)

    LocalStorageManagerAsyncPrivate * const d_ptr;
    Q_DECLARE_PRIVATE(LocalStorageManagerAsync)
};

} // namespace quentier

#endif // LIB_QUENTIER_LOCAL_STORAGE_LOCAL_STORAGE_MANAGER_ASYNC_H
