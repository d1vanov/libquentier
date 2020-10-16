/*
 * Copyright 2020 Dmitry Ivanov
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

#pragma once

#include <quentier/local_storage/Fwd.h>
#include <quentier/local_storage/NoteSearchQuery.h>
#include <quentier/local_storage/Result.h>
#include <quentier/types/LinkedNotebook.h>
#include <quentier/types/Note.h>
#include <quentier/types/Notebook.h>
#include <quentier/types/Resource.h>
#include <quentier/types/SavedSearch.h>
#include <quentier/types/SharedNotebook.h>
#include <quentier/types/Tag.h>
#include <quentier/types/User.h>
#include <quentier/utility/Linkage.h>

#include <QFlags>
#include <QFuture>
#include <QHash>
#include <QStringList>
#include <QThreadPool>
#include <QVector>

#include <utility>

class QTextStream;
class QDebug;

namespace quentier::local_storage {

class QUENTIER_EXPORT ILocalStorage
{
public:
    virtual ~ILocalStorage() = default;

public:
    enum class StartupOption
    {
        ClearDatabase = 1 << 1,
        OverrideLock = 1 << 2
    };

    Q_DECLARE_FLAGS(StartupOptions, StartupOption);

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, const StartupOption option);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & dbg, const StartupOption option);

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, const StartupOptions options);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & dbg, const StartupOptions options);

    ////////////////////////////////////////////////////////////////////////////

    enum class ListObjectsOption
    {
        ListAll = 1 << 0,
        ListDirty = 1 << 1,
        ListNonDirty = 1 << 2,
        ListElementsWithoutGuid = 1 << 3,
        ListElementsWithGuid = 1 << 4,
        ListLocal = 1 << 5,
        ListNonLocal = 1 << 6,
        ListFavoritedElements = 1 << 7,
        ListNonFavoritedElements = 1 << 8
    };

    Q_DECLARE_FLAGS(ListObjectsOptions, ListObjectsOption)

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, const ListObjectsOption option);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & dbg, const ListObjectsOption option);

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, const ListObjectsOptions options);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & dbg, const ListObjectsOptions options);

    ////////////////////////////////////////////////////////////////////////////

    enum class OrderDirection
    {
        Ascending,
        Descending
    };

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, const OrderDirection orderDirection);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & dbg, const OrderDirection orderDirection);

    ////////////////////////////////////////////////////////////////////////////

    enum class FindNotebookBy
    {
        LocalUid,
        Guid,
        Name
    };

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, const FindNotebookBy what);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & dbg, const FindNotebookBy what);

    ////////////////////////////////////////////////////////////////////////////

    enum class FindTagBy
    {
        LocalUid,
        Guid,
        Name
    };

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, const FindTagBy what);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & dbg, const FindTagBy what);

    ////////////////////////////////////////////////////////////////////////////

    enum class FindNoteBy
    {
        LocalUid,
        Guid,
    };

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, const FindNoteBy what);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & dbg, const FindNoteBy what);

    ////////////////////////////////////////////////////////////////////////////

    enum class FindResourceBy
    {
        LocalUid,
        Guid
    };

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, const FindResourceBy what);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & dbg, const FindResourceBy what);

    ////////////////////////////////////////////////////////////////////////////

    enum class FindSavedSearchBy
    {
        LocalUid,
        Guid
    };

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, const FindSavedSearchBy what);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & dbg, const FindSavedSearchBy what);

    ////////////////////////////////////////////////////////////////////////////

    enum class ListNotebooksOrder
    {
        NoOrder,
        ByUpdateSequenceNumber,
        ByNotebookName,
        ByCreationTimestamp,
        ByModificationTimestamp
    };

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, const ListNotebooksOrder order);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & dbg, const ListNotebooksOrder order);

    ////////////////////////////////////////////////////////////////////////////

    enum class ListLinkedNotebooksOrder
    {
        NoOrder,
        ByUpdateSequenceNumber,
        ByShareName,
        ByUsername,
    };

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, const ListLinkedNotebooksOrder order);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & strm, const ListLinkedNotebooksOrder order);

    ////////////////////////////////////////////////////////////////////////////

    enum class ListTagsOrder
    {
        NoOrder,
        ByUpdateSequenceNumber,
        ByName
    };

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, const ListTagsOrder order);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & strm, const ListTagsOrder order);

    ////////////////////////////////////////////////////////////////////////////

    enum class ListNotesOrder
    {
        NoOrder,
        ByUpdateSequenceNumber,
        ByTitle,
        ByCreationTimestamp,
        ByModificationTimestamp,
        ByDeletionTimestamp,
        ByAuthor,
        BySource,
        BySourceApplication,
        ByReminderTime,
        ByPlaceName
    };

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, const ListNotesOrder order);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & strm, const ListNotesOrder order);

    ////////////////////////////////////////////////////////////////////////////

    enum class ListSavedSearchesOrder
    {
        NoOrder,
        ByUpdateSequenceNumber,
        ByName,
        ByFormat
    };

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, const ListSavedSearchesOrder order);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & strm, const ListSavedSearchesOrder order);

    ////////////////////////////////////////////////////////////////////////////

    struct QUENTIER_EXPORT ListOptionsBase
    {
        ListOptionsBase() noexcept;

        ListObjectsOptions m_options;
        size_t m_limit;
        size_t m_offset;
        OrderDirection m_direction;
    };

    template <class Order>
    struct QUENTIER_EXPORT ListOptions : ListOptionsBase
    {
        ListOptions() noexcept;

        Order m_order;
        QString m_linkedNotebookGuid;
    };

    template <>
    struct QUENTIER_EXPORT ListOptions<ListLinkedNotebooksOrder> :
        ListOptionsBase
    {
        ListOptions() noexcept;

        ListLinkedNotebooksOrder m_order;
    };

    template <class Order>
    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, const ListOptions<Order> options);

    template <class Order>
    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & strm, const ListOptions<Order> options);

    ////////////////////////////////////////////////////////////////////////////

    enum class NoteCountOption
    {
        IncludeNonDeletedNotes = 1 << 1,
        IncludeDeletedNotes = 1 << 2
    };
    Q_DECLARE_FLAGS(NoteCountOptions, NoteCountOption)

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, const NoteCountOption option);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & dbg, const NoteCountOption option);

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, const NoteCountOptions options);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & strm, const NoteCountOptions options);

    ////////////////////////////////////////////////////////////////////////////

    enum class UpdateNoteOption
    {
        UpdateResourceMetadata = 1 << 1,
        UpdateResourceBinaryData = 1 << 2,
        UpdateTags = 1 << 3
    };
    Q_DECLARE_FLAGS(UpdateNoteOptions, UpdateNoteOption)

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, const UpdateNoteOption option);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & strm, const UpdateNoteOption option);

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, const UpdateNoteOptions options);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & strm, const UpdateNoteOptions options);

    ////////////////////////////////////////////////////////////////////////////

    enum class FetchNoteOption
    {
        WithResourceMetadata = 1 << 1,
        WithResourceBinaryData = 1 << 2
    };
    Q_DECLARE_FLAGS(FetchNoteOptions, FetchNoteOption)

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, const FetchNoteOption option);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & dbg, const FetchNoteOption option);

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, const FetchNoteOptions options);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & strm, const FetchNoteOptions options);

    ////////////////////////////////////////////////////////////////////////////

    enum class FetchResourceOption
    {
        WithBinaryData = 1 << 1
    };
    Q_DECLARE_FLAGS(FetchResourceOptions, FetchResourceOption)

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, const FetchResourceOption option);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & dbg, const FetchResourceOption option);

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, const FetchResourceOptions options);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & strm, const FetchResourceOptions options);

    ////////////////////////////////////////////////////////////////////////////

    enum class HighestUsnOption
    {
        WithinUserOwnContent,
        WithinUserOwnContentAndLinkedNotebooks
    };

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, const HighestUsnOption option);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & dbg, const HighestUsnOption option);

public:
    // Versions/upgrade API
    virtual QFuture<Result<bool>> isVersionTooHigh() = 0;
    virtual QFuture<Result<bool>> requiresUpgrade() = 0;

    virtual QFuture<Result<QVector<ILocalStoragePatchPtr>>>
        requiredPatches() = 0;

    virtual QFuture<Result<qint32>> version() = 0;
    virtual QFuture<qint32> highestSupportedVersion() = 0;

    // Users API
    virtual QFuture<Result<size_t>> userCount() = 0;
    virtual QFuture<Result<void>> putUser(User user) = 0;
    virtual QFuture<Result<User>> findUser(qint32 userId) = 0;
    virtual QFuture<Result<void>> expungeUser(qint32 userId) = 0;
    virtual QFuture<Result<QVector<User>>> listUsers() = 0;

    // Notebooks API
    virtual QFuture<Result<size_t>> notebookCount() = 0;
    virtual QFuture<Result<void>> putNotebook(Notebook notebook) = 0;

    virtual QFuture<Result<Notebook>> findNotebook(
        FindNotebookBy searchCriteria, QString searchCriteriaValue) = 0;

    virtual QFuture<Result<Notebook>> findDefaultNotebook() = 0;
    virtual QFuture<Result<Notebook>> findLastUsedNotebook() = 0;
    virtual QFuture<Result<Notebook>> findDefaultOrLastUsedNotebook() = 0;
    virtual QFuture<Result<void>> expungeNotebook(Notebook notebook) = 0;

    virtual QFuture<Result<QVector<Notebook>>> listNotebooks(
        ListOptions<ListNotebooksOrder> options = {}) = 0;

    virtual QFuture<Result<QVector<SharedNotebook>>> listSharedNotebooks(
        QString notebookGuid = {}) = 0;

    // Linked notebooks API
    virtual QFuture<Result<size_t>> linkedNotebookCount() = 0;

    virtual QFuture<Result<void>> putLinkedNotebook(
        LinkedNotebook linkedNotebook) = 0;

    virtual QFuture<Result<LinkedNotebook>> findLinkedNotebook(
        QString guid) = 0;

    virtual QFuture<Result<void>> expungeLinkedNotebook(QString guid) = 0;

    virtual QFuture<Result<QVector<LinkedNotebook>>> listLinkedNotebooks(
        ListOptions<ListLinkedNotebooksOrder> options = {}) = 0;

    // Notes API
    virtual QFuture<Result<size_t>> noteCount(
        NoteCountOptions options =
            NoteCountOptions(NoteCountOption::IncludeNonDeletedNotes)) = 0;

    virtual QFuture<Result<size_t>> noteCountPerNotebook(
        FindNotebookBy notebookSearchCriteria,
        QString notebookSearchCriteriaValue,
        NoteCountOptions options =
            NoteCountOptions(NoteCountOption::IncludeNonDeletedNotes)) = 0;

    virtual QFuture<Result<size_t>> noteCountPerTag(
        FindTagBy tagSearchCriteria, QString tagSearchCriteriaValue,
        NoteCountOptions options =
            NoteCountOptions(NoteCountOption::IncludeNonDeletedNotes)) = 0;

    virtual QFuture<Result<QHash<QString, size_t>>> noteCountsPerTags(
        ListOptions<ListTagsOrder> listTagsOptions = {},
        NoteCountOptions options =
            NoteCountOptions(NoteCountOption::IncludeNonDeletedNotes)) = 0;

    virtual QFuture<Result<size_t>> noteCountPerNotebooksAndTags(
        QStringList notebookLocalUids, QStringList tagLocalUids,
        NoteCountOptions options =
            NoteCountOptions(NoteCountOption::IncludeNonDeletedNotes)) = 0;

    virtual QFuture<Result<void>> putNote(Note note) = 0;

    virtual QFuture<Result<void>> updateNote(
        Note note, UpdateNoteOptions options) = 0;

    virtual QFuture<Result<Note>> findNote(
        FindNoteBy searchCriteria, QString searchCriteriaValue,
        FetchNoteOptions options);

    virtual QFuture<Result<QVector<Note>>> listNotes(
        FetchNoteOptions fetchOptions,
        ListOptions<ListNotesOrder> listOptions = {});

    virtual QFuture<Result<QVector<Note>>> listNotesPerNotebook(
        FindNotebookBy notebookSearchCriteria,
        QString notebookSearchCriteriaValue, FetchNoteOptions fetchOptions,
        ListOptions<ListNotesOrder> listOptions = {}) = 0;

    virtual QFuture<Result<QVector<Note>>> listNotesPerTag(
        FindTagBy tagSearchCriteria, QString tagSearchCriteriaValue,
        FetchNoteOptions fetchOptions,
        ListOptions<ListNotesOrder> listOptions = {}) = 0;

    virtual QFuture<Result<QVector<Note>>> listNotesPerNotebooksAndTags(
        QStringList notebookLocalUids, QStringList tagLocalUids,
        FetchNoteOptions fetchOptions,
        ListOptions<ListNotesOrder> listOptions = {}) = 0;

    virtual QFuture<Result<QVector<Note>>> searchNotes(
        FindNoteBy searchCriteria, QStringList searchCriteriaValues,
        FetchNoteOptions fetchOptions,
        ListOptions<ListNotesOrder> listOptions = {}) = 0;

    virtual QFuture<Result<QVector<Note>>> queryNotes(
        NoteSearchQuery query, FetchNoteOptions fetchOptions) = 0;

    virtual QFuture<Result<void>> expungeNote(
        FindNoteBy searchCriteria, QString searchCriteriaValue) = 0;

    // Tags API
    virtual QFuture<Result<size_t>> tagCount() = 0;
    virtual QFuture<Result<void>> putTag(Tag tag) = 0;

    virtual QFuture<Result<Tag>> findTag(
        FindTagBy searchCriteria, QString searchCriteriaValue) = 0;

    virtual QFuture<Result<QVector<Tag>>> listTags(
        ListOptions<ListTagsOrder> options = {}) = 0;

    virtual QFuture<Result<QVector<Tag>>> listTagsPerNote(
        FindNoteBy noteSearchCriteria, QString noteSearchCriteriaValue,
        ListOptions<ListTagsOrder> options = {}) = 0;

    virtual QFuture<Result<void>> expungeTag(
        FindTagBy searchCriteria, QString searchCriteriaValue) = 0;

    // Resources API
    virtual QFuture<Result<size_t>> resourceCount(
        NoteCountOptions options =
            NoteCountOptions(NoteCountOption::IncludeNonDeletedNotes)) = 0;

    virtual QFuture<Result<size_t>> resourceCountPerNote(
        FindNoteBy noteSearchCriteria, QString noteSearchCriteriaValue) = 0;

    virtual QFuture<Result<void>> putResource(Resource resource) = 0;

    virtual QFuture<Result<Resource>> findResource(
        FindResourceBy searchCriteria, QString searchCriteriaValue,
        FetchResourceOptions options = {}) = 0;

    virtual QFuture<Result<void>> expungeResource(
        FindResourceBy searchCriteria, QString searchCriteriaValue) = 0;

    // Saved searches API
    virtual QFuture<Result<size_t>> savedSearchCount() = 0;

    virtual QFuture<Result<void>> putSavedSearch(SavedSearch search) = 0;

    virtual QFuture<Result<SavedSearch>> findSavedSearch(
        FindSavedSearchBy searchCriteria, QString searchCriteriaValue) = 0;

    virtual QFuture<Result<QVector<SavedSearch>>> listSavedSearches(
        ListOptions<ListSavedSearchesOrder> options = {}) = 0;

    virtual QFuture<Result<void>> expungeSavedSearch(
        FindSavedSearchBy searchCriteria, QString searchCriteriaValue) = 0;

    // Auxiliary methods for synchronization
    virtual QFuture<Result<qint32>> highestUpdateSequenceNumber(
        HighestUsnOption option) = 0;

    virtual QFuture<Result<qint32>> highestUpdateSequenceNumber(
        QString linkedNotebookGuid) = 0;
};

} // namespace quentier::local_storage
