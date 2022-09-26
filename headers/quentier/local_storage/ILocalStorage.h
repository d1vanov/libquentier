/*
 * Copyright 2020-2022 Dmitry Ivanov
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

#include <qevercloud/types/LinkedNotebook.h>
#include <qevercloud/types/Note.h>
#include <qevercloud/types/Notebook.h>
#include <qevercloud/types/Resource.h>
#include <qevercloud/types/SavedSearch.h>
#include <qevercloud/types/SharedNotebook.h>
#include <qevercloud/types/Tag.h>
#include <qevercloud/types/User.h>

#include <quentier/local_storage/Fwd.h>
#include <quentier/local_storage/NoteSearchQuery.h>
#include <quentier/utility/Linkage.h>

#include <QFlags>
#include <QFuture>
#include <QHash>
#include <QList>
#include <QStringList>
#include <QThreadPool>

#include <optional>
#include <utility>

class QDebug;
class QDir;
class QTextStream;
class QThreadPool;

namespace quentier {

class Account;

} // namespace quentier

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

    enum class ListObjectsFilter
    {
        Include,
        Exclude
    };

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, ListObjectsFilter filter);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & dbg, ListObjectsFilter filter);

    ////////////////////////////////////////////////////////////////////////////

    struct QUENTIER_EXPORT ListObjectsFilters
    {
        std::optional<ListObjectsFilter> m_locallyModifiedFilter;
        std::optional<ListObjectsFilter> m_withGuidFilter;
        std::optional<ListObjectsFilter> m_localOnlyFilter;
        std::optional<ListObjectsFilter> m_locallyFavoritedFilter;
    };

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, const ListObjectsFilters & filters);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & dbg, const ListObjectsFilters & filters);

    ////////////////////////////////////////////////////////////////////////////

    struct QUENTIER_EXPORT ListGuidsFilters
    {
        std::optional<ListObjectsFilter> m_locallyModifiedFilter;
        std::optional<ListObjectsFilter> m_locallyFavoritedFilter;
    };

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, const ListGuidsFilters & filters);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & dbg, const ListGuidsFilters & filters);

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
        QDebug & dbg, ListLinkedNotebooksOrder order);

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
        QDebug & dbg, ListTagsOrder order);

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
        QDebug & dbg, ListNotesOrder order);

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
        QDebug & dbg, ListSavedSearchesOrder order);

    ////////////////////////////////////////////////////////////////////////////

    /// Denotes whether some data item belongs to user's own account,
    /// any of linked notebooks or particular linked notebooks
    enum class Affiliation
    {
        Any,
        User,
        AnyLinkedNotebook,
        ParticularLinkedNotebooks
    };

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, Affiliation affiliation);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & dbg, Affiliation affiliation);

    ////////////////////////////////////////////////////////////////////////////

    struct QUENTIER_EXPORT ListOptionsBase
    {
        ListOptionsBase() noexcept {}; // NOLINT

        ListObjectsFilters m_filters = {};
        quint64 m_limit = 0UL;
        quint64 m_offset = 0UL;
        OrderDirection m_direction = OrderDirection::Ascending;
    };

    struct QUENTIER_EXPORT ListNotebooksOptions : public ListOptionsBase
    {
        ListNotebooksOptions() noexcept {}; // NOLINT

        ListNotebooksOrder m_order = ListNotebooksOrder::NoOrder;
        Affiliation m_affiliation = Affiliation::Any;
        QList<qevercloud::Guid> m_linkedNotebookGuids;
    };

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, const ListNotebooksOptions & options);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & dbg, const ListNotebooksOptions & options);

    struct QUENTIER_EXPORT ListLinkedNotebooksOptions : public ListOptionsBase
    {
        ListLinkedNotebooksOptions() noexcept {}; // NOLINT

        ListLinkedNotebooksOrder m_order = ListLinkedNotebooksOrder::NoOrder;
    };

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, const ListLinkedNotebooksOptions & options);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & dbg, const ListLinkedNotebooksOptions & options);

    struct QUENTIER_EXPORT ListSavedSearchesOptions : public ListOptionsBase
    {
        ListSavedSearchesOptions() noexcept {}; // NOLINT

        ListSavedSearchesOrder m_order = ListSavedSearchesOrder::NoOrder;
    };

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, const ListSavedSearchesOptions & options);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & dbg, const ListSavedSearchesOptions & options);

    struct QUENTIER_EXPORT ListNotesOptions : public ListOptionsBase
    {
        ListNotesOptions() noexcept {}; // NOLINT

        ListNotesOrder m_order = ListNotesOrder::NoOrder;
    };

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, const ListNotesOptions & options);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & dbg, const ListNotesOptions & options);

    /// Denotes the relation between tag and notes - whether any note us using
    /// the given tag
    enum class TagNotesRelation
    {
        /// The tag might be used by some notes or it might not be
        Any,
        /// The tag is used by some notes
        WithNotes,
        /// The tag is not used by any note
        WithoutNotes
    };

    struct QUENTIER_EXPORT ListTagsOptions : public ListOptionsBase
    {
        ListTagsOptions() noexcept {}; // NOLINT

        ListTagsOrder m_order = ListTagsOrder::NoOrder;
        Affiliation m_affiliation = Affiliation::Any;
        QList<qevercloud::Guid> m_linkedNotebookGuids;
        TagNotesRelation m_tagNotesRelation = TagNotesRelation::Any;
    };

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, const ListTagsOptions & options);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & dbg, const ListTagsOptions & options);

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
        QDebug & dbg, NoteCountOptions options);

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
        QDebug & dbg, UpdateNoteOption option);

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, UpdateNoteOptions options);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & dbg, UpdateNoteOptions options);

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
        QDebug & dbg, FetchNoteOptions options);

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
        QDebug & dbg, FetchResourceOptions options);

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
    [[nodiscard]] virtual QFuture<QList<IPatchPtr>> requiredPatches() const = 0;
    [[nodiscard]] virtual QFuture<qint32> version() const = 0;
    [[nodiscard]] virtual QFuture<qint32> highestSupportedVersion() const = 0;

    // Users API
    [[nodiscard]] virtual QFuture<quint32> userCount() const = 0;
    [[nodiscard]] virtual QFuture<void> putUser(qevercloud::User user) = 0;

    [[nodiscard]] virtual QFuture<std::optional<qevercloud::User>> findUserById(
        qevercloud::UserID userId) const = 0;

    [[nodiscard]] virtual QFuture<void> expungeUserById(
        qevercloud::UserID userId) = 0;

    // Notebooks API
    [[nodiscard]] virtual QFuture<quint32> notebookCount() const = 0;

    [[nodiscard]] virtual QFuture<void> putNotebook(
        qevercloud::Notebook notebook) = 0;

    [[nodiscard]] virtual QFuture<std::optional<qevercloud::Notebook>>
        findNotebookByLocalId(QString notebookLocalId) const = 0;

    [[nodiscard]] virtual QFuture<std::optional<qevercloud::Notebook>>
        findNotebookByGuid(qevercloud::Guid guid) const = 0;

    [[nodiscard]] virtual QFuture<std::optional<qevercloud::Notebook>>
        findNotebookByName(
            QString notebookName,
            std::optional<qevercloud::Guid> linkedNotebookGuid =
                std::nullopt) const = 0;

    [[nodiscard]] virtual QFuture<std::optional<qevercloud::Notebook>>
        findDefaultNotebook() const = 0;

    [[nodiscard]] virtual QFuture<void> expungeNotebookByLocalId(
        QString notebookLocalId) = 0;

    [[nodiscard]] virtual QFuture<void> expungeNotebookByGuid(
        qevercloud::Guid notebookGuid) = 0;

    [[nodiscard]] virtual QFuture<void> expungeNotebookByName(
        QString name,
        std::optional<qevercloud::Guid> linkedNotebookGuid = std::nullopt) = 0;

    [[nodiscard]] virtual QFuture<QList<qevercloud::Notebook>> listNotebooks(
        ListNotebooksOptions options = {}) const = 0;

    [[nodiscard]] virtual QFuture<QList<qevercloud::SharedNotebook>>
        listSharedNotebooks(qevercloud::Guid notebookGuid = {}) const = 0;

    [[nodiscard]] virtual QFuture<QSet<qevercloud::Guid>> listNotebookGuids(
        ListGuidsFilters filters,
        std::optional<qevercloud::Guid> linkedNotebookGuid = {}) const = 0;

    // Linked notebooks API
    [[nodiscard]] virtual QFuture<quint32> linkedNotebookCount() const = 0;

    [[nodiscard]] virtual QFuture<void> putLinkedNotebook(
        qevercloud::LinkedNotebook linkedNotebook) = 0;

    [[nodiscard]] virtual QFuture<std::optional<qevercloud::LinkedNotebook>>
        findLinkedNotebookByGuid(qevercloud::Guid guid) const = 0;

    [[nodiscard]] virtual QFuture<void> expungeLinkedNotebookByGuid(
        qevercloud::Guid guid) = 0;

    [[nodiscard]] virtual QFuture<QList<qevercloud::LinkedNotebook>>
        listLinkedNotebooks(ListLinkedNotebooksOptions options = {}) const = 0;

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

    [[nodiscard]] virtual QFuture<QHash<QString, quint32>> noteCountsPerTags(
        ListTagsOptions listTagsOptions = {},
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
        ListNotesOptions listOptions = {}) const = 0;

    [[nodiscard]] virtual QFuture<QList<qevercloud::Note>>
        listNotesPerNotebookLocalId(
            QString notebookLocalId, FetchNoteOptions fetchOptions,
            ListNotesOptions listOptions = {}) const = 0;

    [[nodiscard]] virtual QFuture<QList<qevercloud::Note>>
        listNotesPerTagLocalId(
            QString tagLocalId, FetchNoteOptions fetchOptions,
            ListNotesOptions listOptions = {}) const = 0;

    [[nodiscard]] virtual QFuture<QList<qevercloud::Note>>
        listNotesPerNotebookAndTagLocalIds(
            QStringList notebookLocalIds, QStringList tagLocalIds,
            FetchNoteOptions fetchOptions,
            ListNotesOptions listOptions = {}) const = 0;

    [[nodiscard]] virtual QFuture<QList<qevercloud::Note>> listNotesByLocalIds(
        QStringList noteLocalIds, FetchNoteOptions fetchOptions,
        ListNotesOptions listOptions = {}) const = 0;

    [[nodiscard]] virtual QFuture<QSet<qevercloud::Guid>> listNoteGuids(
        ListGuidsFilters filters,
        std::optional<qevercloud::Guid> linkedNotebookGuid = {}) const = 0;

    [[nodiscard]] virtual QFuture<QList<qevercloud::Note>> queryNotes(
        NoteSearchQuery query, FetchNoteOptions fetchOptions) const = 0;

    [[nodiscard]] virtual QFuture<QStringList> queryNoteLocalIds(
        NoteSearchQuery query) const = 0;

    [[nodiscard]] virtual QFuture<void> expungeNoteByLocalId(
        QString noteLocalId) = 0;

    [[nodiscard]] virtual QFuture<void> expungeNoteByGuid(
        qevercloud::Guid noteGuid) = 0;

    // Tags API
    [[nodiscard]] virtual QFuture<quint32> tagCount() const = 0;
    [[nodiscard]] virtual QFuture<void> putTag(qevercloud::Tag tag) = 0;

    [[nodiscard]] virtual QFuture<std::optional<qevercloud::Tag>>
        findTagByLocalId(QString tagLocalId) const = 0;

    [[nodiscard]] virtual QFuture<std::optional<qevercloud::Tag>> findTagByGuid(
        qevercloud::Guid tagGuid) const = 0;

    [[nodiscard]] virtual QFuture<std::optional<qevercloud::Tag>> findTagByName(
        QString tagName,
        std::optional<qevercloud::Guid> linkedNotebookGuid =
            std::nullopt) const = 0;

    [[nodiscard]] virtual QFuture<QList<qevercloud::Tag>> listTags(
        ListTagsOptions options = {}) const = 0;

    [[nodiscard]] virtual QFuture<QList<qevercloud::Tag>>
        listTagsPerNoteLocalId(
            QString noteLocalId, ListTagsOptions options = {}) const = 0;

    [[nodiscard]] virtual QFuture<QSet<qevercloud::Guid>> listTagGuids(
        ListGuidsFilters filters,
        std::optional<qevercloud::Guid> linkedNotebookGuid = {}) const = 0;

    [[nodiscard]] virtual QFuture<void> expungeTagByLocalId(
        QString tagLocalId) = 0;

    [[nodiscard]] virtual QFuture<void> expungeTagByGuid(
        qevercloud::Guid tagGuid) = 0;

    [[nodiscard]] virtual QFuture<void> expungeTagByName(
        QString name,
        std::optional<qevercloud::Guid> linkedNotebookGuid = std::nullopt) = 0;

    // Resources API
    [[nodiscard]] virtual QFuture<quint32> resourceCount(
        NoteCountOptions options = NoteCountOptions(
            NoteCountOption::IncludeNonDeletedNotes)) const = 0;

    [[nodiscard]] virtual QFuture<quint32> resourceCountPerNoteLocalId(
        QString noteLocalId) const = 0;

    [[nodiscard]] virtual QFuture<void> putResource(
        qevercloud::Resource resource) = 0;

    [[nodiscard]] virtual QFuture<std::optional<qevercloud::Resource>>
        findResourceByLocalId(
            QString resourceLocalId,
            FetchResourceOptions options = {}) const = 0;

    [[nodiscard]] virtual QFuture<std::optional<qevercloud::Resource>>
        findResourceByGuid(
            qevercloud::Guid resourceGuid,
            FetchResourceOptions options = {}) const = 0;

    [[nodiscard]] virtual QFuture<void> expungeResourceByLocalId(
        QString resourceLocalId) = 0;

    [[nodiscard]] virtual QFuture<void> expungeResourceByGuid(
        qevercloud::Guid resourceGuid) = 0;

    // Saved searches API
    [[nodiscard]] virtual QFuture<quint32> savedSearchCount() const = 0;

    [[nodiscard]] virtual QFuture<void> putSavedSearch(
        qevercloud::SavedSearch search) = 0;

    [[nodiscard]] virtual QFuture<std::optional<qevercloud::SavedSearch>>
        findSavedSearchByLocalId(QString savedSearchLocalId) const = 0;

    [[nodiscard]] virtual QFuture<std::optional<qevercloud::SavedSearch>>
        findSavedSearchByGuid(qevercloud::Guid guid) const = 0;

    [[nodiscard]] virtual QFuture<std::optional<qevercloud::SavedSearch>>
        findSavedSearchByName(QString name) const = 0;

    [[nodiscard]] virtual QFuture<QList<qevercloud::SavedSearch>>
        listSavedSearches(ListSavedSearchesOptions options = {}) const = 0;

    [[nodiscard]] virtual QFuture<QSet<qevercloud::Guid>> listSavedSearchGuids(
        ListGuidsFilters filters) const = 0;

    [[nodiscard]] virtual QFuture<void> expungeSavedSearchByLocalId(
        QString savedSearchLocalId) = 0;

    [[nodiscard]] virtual QFuture<void> expungeSavedSearchByGuid(
        qevercloud::Guid guid) = 0;

    // Synchronization API
    [[nodiscard]] virtual QFuture<qint32> highestUpdateSequenceNumber(
        HighestUsnOption option) const = 0;

    [[nodiscard]] virtual QFuture<qint32> highestUpdateSequenceNumber(
        qevercloud::Guid linkedNotebookGuid) const = 0;

    /**
     * Notifications about the events occurring with the local storage are done
     * via signals emitted by ILocalStorageNotifier.
     * @return the pointer to ILocalStorageNotifier object which would be valid
     * for at least as long as ILocalStorage object that returned it is alive.
     */
    [[nodiscard]] virtual ILocalStorageNotifier * notifier() const = 0;
};

[[nodiscard]] QUENTIER_EXPORT bool operator==(
    const ILocalStorage::ListObjectsFilters & lhs,
    const ILocalStorage::ListObjectsFilters & rhs) noexcept;

[[nodiscard]] QUENTIER_EXPORT bool operator==(
    const ILocalStorage::ListOptionsBase & lhs,
    const ILocalStorage::ListOptionsBase & rhs) noexcept;

[[nodiscard]] QUENTIER_EXPORT bool operator!=(
    const ILocalStorage::ListOptionsBase & lhs,
    const ILocalStorage::ListOptionsBase & rhs) noexcept;

[[nodiscard]] QUENTIER_EXPORT bool operator==(
    const ILocalStorage::ListNotebooksOptions & lhs,
    const ILocalStorage::ListNotebooksOptions & rhs) noexcept;

[[nodiscard]] QUENTIER_EXPORT bool operator!=(
    const ILocalStorage::ListNotebooksOptions & lhs,
    const ILocalStorage::ListNotebooksOptions & rhs) noexcept;

[[nodiscard]] QUENTIER_EXPORT bool operator==(
    const ILocalStorage::ListLinkedNotebooksOptions & lhs,
    const ILocalStorage::ListLinkedNotebooksOptions & rhs) noexcept;

[[nodiscard]] QUENTIER_EXPORT bool operator!=(
    const ILocalStorage::ListLinkedNotebooksOptions & lhs,
    const ILocalStorage::ListLinkedNotebooksOptions & rhs) noexcept;

[[nodiscard]] QUENTIER_EXPORT bool operator==(
    const ILocalStorage::ListSavedSearchesOptions & lhs,
    const ILocalStorage::ListSavedSearchesOptions & rhs) noexcept;

[[nodiscard]] QUENTIER_EXPORT bool operator!=(
    const ILocalStorage::ListSavedSearchesOptions & lhs,
    const ILocalStorage::ListSavedSearchesOptions & rhs) noexcept;

[[nodiscard]] QUENTIER_EXPORT bool operator==(
    const ILocalStorage::ListNotesOptions & lhs,
    const ILocalStorage::ListNotesOptions & rhs) noexcept;

[[nodiscard]] QUENTIER_EXPORT bool operator!=(
    const ILocalStorage::ListNotesOptions & lhs,
    const ILocalStorage::ListNotesOptions & rhs) noexcept;

[[nodiscard]] QUENTIER_EXPORT bool operator==(
    const ILocalStorage::ListTagsOptions & lhs,
    const ILocalStorage::ListTagsOptions & rhs) noexcept;

[[nodiscard]] QUENTIER_EXPORT bool operator!=(
    const ILocalStorage::ListTagsOptions & lhs,
    const ILocalStorage::ListTagsOptions & rhs) noexcept;

[[nodiscard]] QUENTIER_EXPORT bool operator==(
    const ILocalStorage::ListGuidsFilters & lhs,
    const ILocalStorage::ListGuidsFilters & rhs) noexcept;

[[nodiscard]] QUENTIER_EXPORT bool operator!=(
    const ILocalStorage::ListGuidsFilters & lhs,
    const ILocalStorage::ListGuidsFilters & rhs) noexcept;

[[nodiscard]] QUENTIER_EXPORT ILocalStoragePtr createSqliteLocalStorage(
    const Account & account, const QDir & localStorageDir,
    QThreadPool * threadPool);

} // namespace quentier::local_storage
