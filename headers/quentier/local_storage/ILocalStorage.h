/*
 * Copyright 2020-2021 Dmitry Ivanov
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

#include <qevercloud/types/All.h>

#include <quentier/local_storage/Fwd.h>
#include <quentier/local_storage/NoteSearchQuery.h>
#include <quentier/local_storage/Result.h>
#include <quentier/utility/Linkage.h>

#include <QFlags>
#include <QFuture>
#include <QHash>
#include <QList>
#include <QStringList>
#include <QThreadPool>

#include <optional>
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
        QTextStream & strm, StartupOption option);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & dbg, StartupOption option);

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, StartupOptions options);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & dbg, StartupOptions options);

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
        QTextStream & strm, ListObjectsOption option);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & dbg, ListObjectsOption option);

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, ListObjectsOptions options);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & dbg, ListObjectsOptions options);

    ////////////////////////////////////////////////////////////////////////////

    enum class OrderDirection
    {
        Ascending,
        Descending
    };

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, OrderDirection orderDirection);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & dbg, OrderDirection orderDirection);

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
        QTextStream & strm, ListNotebooksOrder order);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & dbg, ListNotebooksOrder order);

    ////////////////////////////////////////////////////////////////////////////

    enum class ListLinkedNotebooksOrder
    {
        NoOrder,
        ByUpdateSequenceNumber,
        ByShareName,
        ByUsername,
    };

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, ListLinkedNotebooksOrder order);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & strm, ListLinkedNotebooksOrder order);

    ////////////////////////////////////////////////////////////////////////////

    enum class ListTagsOrder
    {
        NoOrder,
        ByUpdateSequenceNumber,
        ByName
    };

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, ListTagsOrder order);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & strm, ListTagsOrder order);

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
        QTextStream & strm, ListNotesOrder order);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & strm, ListNotesOrder order);

    ////////////////////////////////////////////////////////////////////////////

    enum class ListSavedSearchesOrder
    {
        NoOrder,
        ByUpdateSequenceNumber,
        ByName,
        ByFormat
    };

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, ListSavedSearchesOrder order);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & strm, ListSavedSearchesOrder order);

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
        QTextStream & strm, ListOptions<Order> options);

    template <class Order>
    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & strm, ListOptions<Order> options);

    ////////////////////////////////////////////////////////////////////////////

    enum class NoteCountOption
    {
        IncludeNonDeletedNotes = 1 << 1,
        IncludeDeletedNotes = 1 << 2
    };
    Q_DECLARE_FLAGS(NoteCountOptions, NoteCountOption)

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, NoteCountOption option);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & dbg, NoteCountOption option);

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, NoteCountOptions options);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & strm, NoteCountOptions options);

    ////////////////////////////////////////////////////////////////////////////

    enum class UpdateNoteOption
    {
        UpdateResourceMetadata = 1 << 1,
        UpdateResourceBinaryData = 1 << 2,
        UpdateTags = 1 << 3
    };
    Q_DECLARE_FLAGS(UpdateNoteOptions, UpdateNoteOption)

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, UpdateNoteOption option);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & strm, UpdateNoteOption option);

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, UpdateNoteOptions options);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & strm, UpdateNoteOptions options);

    ////////////////////////////////////////////////////////////////////////////

    enum class FetchNoteOption
    {
        WithResourceMetadata = 1 << 1,
        WithResourceBinaryData = 1 << 2
    };
    Q_DECLARE_FLAGS(FetchNoteOptions, FetchNoteOption)

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, FetchNoteOption option);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & dbg, FetchNoteOption option);

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, FetchNoteOptions options);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & strm, FetchNoteOptions options);

    ////////////////////////////////////////////////////////////////////////////

    enum class FetchResourceOption
    {
        WithBinaryData = 1 << 1
    };
    Q_DECLARE_FLAGS(FetchResourceOptions, FetchResourceOption)

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, FetchResourceOption option);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & dbg, FetchResourceOption option);

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, FetchResourceOptions options);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & strm, FetchResourceOptions options);

    ////////////////////////////////////////////////////////////////////////////

    enum class HighestUsnOption
    {
        WithinUserOwnContent,
        WithinUserOwnContentAndLinkedNotebooks
    };

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, HighestUsnOption option);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & dbg, HighestUsnOption option);

public:
    // Versions/upgrade API
    [[nodiscard]] virtual QFuture<bool> isVersionTooHigh() const = 0;
    [[nodiscard]] virtual QFuture<bool> requiresUpgrade() const = 0;

    [[nodiscard]] virtual QFuture<QList<ILocalStoragePatchPtr>>
        requiredPatches() const = 0;

    [[nodiscard]] virtual QFuture<qint32> version() const = 0;
    [[nodiscard]] virtual QFuture<qint32> highestSupportedVersion() const = 0;

    // Users API
    [[nodiscard]] virtual QFuture<quint32> userCount() const = 0;
    [[nodiscard]] virtual QFuture<void> putUser(qevercloud::User user) = 0;

    [[nodiscard]] virtual QFuture<std::optional<qevercloud::User>> findUserById(
        qevercloud::UserID userId) const = 0;

    [[nodiscard]] virtual QFuture<void> expungeUserById(
        qevercloud::UserID userId) = 0;

    [[nodiscard]] virtual QFuture<QList<qevercloud::User>> listUsers()
        const = 0;

    // Notebooks API
    [[nodiscard]] virtual QFuture<quint32> notebookCount() const = 0;

    [[nodiscard]] virtual QFuture<void> putNotebook(
        qevercloud::Notebook notebook) = 0;

    [[nodiscard]] virtual QFuture<std::optional<qevercloud::Notebook>>
        findNotebookByLocalId(QString notebookLocalId) const = 0;

    [[nodiscard]] virtual QFuture<std::optional<qevercloud::Notebook>>
        findNotebookByGuid(qevercloud::Guid guid) const = 0;

    [[nodiscard]] virtual QFuture<std::optional<qevercloud::Notebook>>
        findDefaultNotebook() const = 0;

    [[nodiscard]] virtual QFuture<void> expungeNotebookByLocalId(
        QString notebookLocalId) = 0;

    [[nodiscard]] virtual QFuture<void> expungeNotebookByGuid(
        qevercloud::Guid notebookGuid) = 0;

    [[nodiscard]] virtual QFuture<QList<qevercloud::Notebook>> listNotebooks(
        ListOptions<ListNotebooksOrder> options = {}) const = 0;

    [[nodiscard]] virtual QFuture<QVector<qevercloud::SharedNotebook>>
        listSharedNotebooks(qevercloud::Guid notebookGuid = {}) const = 0;

    // Linked notebooks API
    [[nodiscard]] virtual QFuture<quint32> linkedNotebookCount() const = 0;

    [[nodiscard]] virtual QFuture<void> putLinkedNotebook(
        qevercloud::LinkedNotebook linkedNotebook) = 0;

    [[nodiscard]] virtual QFuture<std::optional<qevercloud::LinkedNotebook>>
        findLinkedNotebookByGuid(qevercloud::Guid guid) const = 0;

    [[nodiscard]] virtual QFuture<void> expungeLinkedNotebookByGuid(
        qevercloud::Guid guid) = 0;

    [[nodiscard]] virtual QFuture<QList<qevercloud::LinkedNotebook>>
        listLinkedNotebooks(
            ListOptions<ListLinkedNotebooksOrder> options = {}) const = 0;

    // Notes API
    [[nodiscard]] virtual QFuture<quint32> noteCount(
        NoteCountOptions options = NoteCountOptions(
            NoteCountOption::IncludeNonDeletedNotes)) const = 0;

    [[nodiscard]] virtual QFuture<quint32> noteCountPerNotebookLocalId(
        QString notebookLocalId,
        NoteCountOptions options = NoteCountOptions(
            NoteCountOption::IncludeNonDeletedNotes)) const = 0;

    [[nodiscard]] virtual QFuture<quint32> noteCountPerTagLocalId(
        QString tagLocalId,
        NoteCountOptions options = NoteCountOptions(
            NoteCountOption::IncludeNonDeletedNotes)) const = 0;

    [[nodiscard]] virtual QFuture<QHash<QString, quint32>>
        noteCountsPerTagLocalIds(
            ListOptions<ListTagsOrder> listTagsOptions = {},
            NoteCountOptions options = NoteCountOptions(
                NoteCountOption::IncludeNonDeletedNotes)) const = 0;

    [[nodiscard]] virtual QFuture<quint32> noteCountPerNotebookAndTagLocalIds(
        QStringList notebookLocalIds, QStringList tagLocalIds,
        NoteCountOptions options = NoteCountOptions(
            NoteCountOption::IncludeNonDeletedNotes)) const = 0;

    [[nodiscard]] virtual QFuture<void> putNote(qevercloud::Note note) = 0;

    [[nodiscard]] virtual QFuture<void> updateNote(
        qevercloud::Note note, UpdateNoteOptions options) = 0;

    [[nodiscard]] virtual QFuture<std::optional<qevercloud::Note>>
        findNoteByLocalId(
            QString noteLocalId, FetchNoteOptions options) const = 0;

    [[nodiscard]] virtual QFuture<std::optional<qevercloud::Note>>
        findNoteByGuid(
            qevercloud::Guid noteGuid, FetchNoteOptions options) const = 0;

    [[nodiscard]] virtual QFuture<QList<qevercloud::Note>> listNotes(
        FetchNoteOptions fetchOptions,
        ListOptions<ListNotesOrder> listOptions = {}) const = 0;

    [[nodiscard]] virtual QFuture<QList<qevercloud::Note>>
        listNotesPerNotebookLocalId(
            QString notebookLocalId, FetchNoteOptions fetchOptions,
            ListOptions<ListNotesOrder> listOptions = {}) const = 0;

    [[nodiscard]] virtual QFuture<QList<qevercloud::Note>>
        listNotesPerTagLocalId(
            QString tagLocalId, FetchNoteOptions fetchOptions,
            ListOptions<ListNotesOrder> listOptions = {}) const = 0;

    [[nodiscard]] virtual QFuture<QList<qevercloud::Note>>
        listNotesPerNotebookAndTagLocalIds(
            QStringList notebookLocalIds, QStringList tagLocalIds,
            FetchNoteOptions fetchOptions,
            ListOptions<ListNotesOrder> listOptions = {}) const = 0;

    [[nodiscard]] virtual QFuture<QList<qevercloud::Note>>
        listNotesByLocalIds(
            QStringList noteLocalIds, FetchNoteOptions fetchOptions,
            ListOptions<ListNotesOrder> listOptions = {}) const = 0;

    [[nodiscard]] virtual QFuture<QList<qevercloud::Note>> queryNotes(
        NoteSearchQuery query, FetchNoteOptions fetchOptions) = 0;

    [[nodiscard]] virtual QFuture<void> expungeNoteByLocalId(
        QString noteLocalId) = 0;

    [[nodiscard]] virtual QFuture<void> expungeNoteByGuid(
        qevercloud::Guid noteGuid) = 0;

    // Tags API
    virtual QFuture<Result<size_t>> tagCount() = 0;
    virtual QFuture<Result<void>> putTag(qevercloud::Tag tag) = 0;

    virtual QFuture<Result<qevercloud::Tag>> findTag(
        FindTagBy searchCriteria, QString searchCriteriaValue) = 0;

    virtual QFuture<Result<QVector<qevercloud::Tag>>> listTags(
        ListOptions<ListTagsOrder> options = {}) = 0;

    virtual QFuture<Result<QVector<qevercloud::Tag>>> listTagsPerNote(
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

    virtual QFuture<Result<void>> putResource(qevercloud::Resource resource) = 0;

    virtual QFuture<Result<qevercloud::Resource>> findResource(
        FindResourceBy searchCriteria, QString searchCriteriaValue,
        FetchResourceOptions options = {}) = 0;

    virtual QFuture<Result<void>> expungeResource(
        FindResourceBy searchCriteria, QString searchCriteriaValue) = 0;

    // Saved searches API
    virtual QFuture<Result<size_t>> savedSearchCount() = 0;

    virtual QFuture<Result<void>> putSavedSearch(qevercloud::SavedSearch search) = 0;

    virtual QFuture<Result<qevercloud::SavedSearch>> findSavedSearch(
        FindSavedSearchBy searchCriteria, QString searchCriteriaValue) = 0;

    virtual QFuture<Result<QVector<qevercloud::SavedSearch>>> listSavedSearches(
        ListOptions<ListSavedSearchesOrder> options = {}) = 0;

    virtual QFuture<Result<void>> expungeSavedSearch(
        FindSavedSearchBy searchCriteria, QString searchCriteriaValue) = 0;

    // Auxiliary methods for synchronization
    virtual QFuture<Result<qint32>> highestUpdateSequenceNumber(
        HighestUsnOption option) = 0;

    virtual QFuture<Result<qint32>> highestUpdateSequenceNumber(
        QString linkedNotebookGuid) = 0;

    // Notifications about events occurring in local storage are done via
    // signals emitted by ILocalStorageNotifier
    virtual ILocalStorageNotifier * notifier() = 0;
};

} // namespace quentier::local_storage
