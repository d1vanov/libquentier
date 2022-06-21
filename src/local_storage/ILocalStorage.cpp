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

#include <quentier/local_storage/ILocalStorage.h>

#include <local_storage/sql/ConnectionPool.h>
#include <local_storage/sql/LocalStorage.h>
#include <local_storage/sql/LinkedNotebooksHandler.h>
#include <local_storage/sql/NotebooksHandler.h>
#include <local_storage/sql/NotesHandler.h>
#include <local_storage/sql/Notifier.h>
#include <local_storage/sql/ResourcesHandler.h>
#include <local_storage/sql/SavedSearchesHandler.h>
#include <local_storage/sql/SynchronizationInfoHandler.h>
#include <local_storage/sql/TagsHandler.h>
#include <local_storage/sql/VersionHandler.h>
#include <local_storage/sql/UsersHandler.h>

#include <QDebug>
#include <QTextStream>
#include <QReadWriteLock>

namespace quentier::local_storage {

namespace {

template <class T>
T & printStartupOption(T & t, const ILocalStorage::StartupOption option)
{
    using StartupOption = ILocalStorage::StartupOption;

    switch (option) {
    case StartupOption::ClearDatabase:
        t << "Clear database";
        break;
    case StartupOption::OverrideLock:
        t << "Override lock";
        break;
    default:
        t << "Unknown (" << static_cast<qint64>(option) << ")";
        break;
    }

    return t;
}

template <class T>
T & printStartupOptions(T & t, const ILocalStorage::StartupOptions options)
{
    using StartupOption = ILocalStorage::StartupOption;

    if (options & StartupOption::ClearDatabase) {
        t << "Clear database; ";
    }

    if (options & StartupOption::OverrideLock) {
        t << "Override lock; ";
    }

    return t;
}

template <class T>
T & printListObjectsOption(T & t, const ILocalStorage::ListObjectsOption option)
{
    using ListObjectsOption = ILocalStorage::ListObjectsOption;

    switch (option) {
    case ListObjectsOption::ListAll:
        t << "List all";
        break;
    case ListObjectsOption::ListDirty:
        t << "List dirty";
        break;
    case ListObjectsOption::ListNonDirty:
        t << "List non dirty";
        break;
    case ListObjectsOption::ListElementsWithoutGuid:
        t << "List elements without guid";
        break;
    case ListObjectsOption::ListElementsWithGuid:
        t << "List elements with guid";
        break;
    case ListObjectsOption::ListLocal:
        t << "List local";
        break;
    case ListObjectsOption::ListNonLocal:
        t << "List non local";
        break;
    case ListObjectsOption::ListFavoritedElements:
        t << "List favorited elements";
        break;
    case ListObjectsOption::ListNonFavoritedElements:
        t << "List non-favorited elements";
        break;
    default:
        t << "Unknown (" << static_cast<qint64>(option);
        break;
    }

    return t;
}

template <class T>
T & printListObjectsOptions(
    T & t, const ILocalStorage::ListObjectsOptions options)
{
    using ListObjectsOption = ILocalStorage::ListObjectsOption;

    if (options & ListObjectsOption::ListAll) {
        t << "List all; ";
    }

    if (options & ListObjectsOption::ListDirty) {
        t << "List dirty; ";
    }

    if (options & ListObjectsOption::ListNonDirty) {
        t << "List non dirty; ";
    }

    if (options & ListObjectsOption::ListElementsWithoutGuid) {
        t << "List elements without guid; ";
    }

    if (options & ListObjectsOption::ListElementsWithGuid) {
        t << "List elements with guid; ";
    }

    if (options & ListObjectsOption::ListLocal) {
        t << "List local; ";
    }

    if (options & ListObjectsOption::ListNonLocal) {
        t << "List non local; ";
    }

    if (options & ListObjectsOption::ListFavoritedElements) {
        t << "List favorited elements; ";
    }

    if (options & ListObjectsOption::ListNonFavoritedElements) {
        t << "List non-favorited elements; ";
    }

    return t;
}

template <class T>
T & printOrderDirection(
    T & t, const ILocalStorage::OrderDirection orderDirection)
{
    using OrderDirection = ILocalStorage::OrderDirection;

    switch (orderDirection) {
    case OrderDirection::Ascending:
        t << "Ascending";
        break;
    case OrderDirection::Descending:
        t << "Descending";
        break;
    default:
        t << "Unknown (" << static_cast<qint64>(orderDirection) << ")";
        break;
    }

    return t;
}

template <class T>
T & printListNotebooksOrder(
    T & t, const ILocalStorage::ListNotebooksOrder order)
{
    using ListNotebooksOrder = ILocalStorage::ListNotebooksOrder;

    switch (order) {
    case ListNotebooksOrder::NoOrder:
        t << "No order";
        break;
    case ListNotebooksOrder::ByUpdateSequenceNumber:
        t << "By update sequence number";
        break;
    case ListNotebooksOrder::ByNotebookName:
        t << "By notebook name";
        break;
    case ListNotebooksOrder::ByCreationTimestamp:
        t << "By creation timestamp";
        break;
    case ListNotebooksOrder::ByModificationTimestamp:
        t << "By modification timestamp";
        break;
    default:
        t << "Unknown (" << static_cast<qint64>(order) << ")";
        break;
    }

    return t;
}

template <class T>
T & printListLinkedNotebooksOrder(
    T & t, const ILocalStorage::ListLinkedNotebooksOrder order)
{
    using ListLinkedNotebooksOrder = ILocalStorage::ListLinkedNotebooksOrder;

    switch (order) {
    case ListLinkedNotebooksOrder::NoOrder:
        t << "No order";
        break;
    case ListLinkedNotebooksOrder::ByUpdateSequenceNumber:
        t << "By update sequence number";
        break;
    case ListLinkedNotebooksOrder::ByShareName:
        t << "By share name";
        break;
    case ListLinkedNotebooksOrder::ByUsername:
        t << "By username";
        break;
    default:
        t << "Unknown (" << static_cast<qint64>(order) << ")";
        break;
    }

    return t;
}

template <class T>
T & printListOptionsBase(T & t, const ILocalStorage::ListOptionsBase & options)
{
    t << "Flags: " << options.m_flags << "; limit = " << options.m_limit
      << ", offset = " << options.m_offset
      << ", direction = " << options.m_direction;
    return t;
}

template <class T>
T & printListNotebooksOptions(
    T & t, const ILocalStorage::ListNotebooksOptions & options)
{
    printListOptionsBase(t, options);
    t << ", order = " << options.m_order
      << ", affiliation = " << options.m_affiliation
      << ", linked notebook guids: ";
    for (const auto & linkedNotebookGuid:
         qAsConst(options.m_linkedNotebookGuids)) {
        t << linkedNotebookGuid;
        if (&linkedNotebookGuid != &options.m_linkedNotebookGuids.constLast()) {
            t << ", ";
        }
    }
    return t;
}

template <class T>
T & printListLinkedNotebooksOptions(
    T & t, const ILocalStorage::ListLinkedNotebooksOptions & options)
{
    printListOptionsBase(t, options);
    t << options.m_order;
    return t;
}

template <class T>
T & printListSavedSearchesOptions(
    T & t, const ILocalStorage::ListSavedSearchesOptions & options)
{
    printListOptionsBase(t, options);
    t << options.m_order;
    return t;
}

template <class T>
T & printTagNotesRelation(
    T & t, const ILocalStorage::TagNotesRelation tagNotesRelation)
{
    using TagNotesRelation = ILocalStorage::TagNotesRelation;

    switch (tagNotesRelation) {
    case TagNotesRelation::Any:
        t << "Any";
        break;
    case TagNotesRelation::WithNotes:
        t << "With notes";
        break;
    case TagNotesRelation::WithoutNotes:
        t << "Without notes";
        break;
    default:
        t << "Unknown (" << static_cast<qint64>(tagNotesRelation) << ")";
        break;
    }

    return t;
}

template <class T>
T & printListTagsOptions(T & t, const ILocalStorage::ListTagsOptions & options)
{
    printListOptionsBase(t, options);
    t << options.m_order;
    return t;
}

template <class T>
T & printListNotesOptions(
    T & t, const ILocalStorage::ListNotesOptions & options)
{
    printListOptionsBase(t, options);
    t << options.m_order;
    return t;
}

template <class T>
T & printAffiliation(T & t, const ILocalStorage::Affiliation affiliation)
{
    using Affiliation = ILocalStorage::Affiliation;

    switch (affiliation) {
    case Affiliation::Any:
        t << "Any";
        break;
    case Affiliation::AnyLinkedNotebook:
        t << "Any linked notebook";
        break;
    case Affiliation::ParticularLinkedNotebooks:
        t << "Particular linked notebooks";
        break;
    case Affiliation::User:
        t << "User";
        break;
    default:
        t << "Unknown (" << static_cast<qint64>(affiliation) << ")";
        break;
    }

    return t;
}

template <class T>
T & printUpdateNoteOption(T & t, const ILocalStorage::UpdateNoteOption option)
{
    using UpdateNoteOption = ILocalStorage::UpdateNoteOption;

    switch (option) {
    case UpdateNoteOption::UpdateResourceMetadata:
        t << "Update resource metadata";
        break;
    case UpdateNoteOption::UpdateResourceBinaryData:
        t << "Update resource binary data";
        break;
    case UpdateNoteOption::UpdateTags:
        t << "Update tags";
        break;
    default:
        t << "Unknown (" << static_cast<qint64>(option) << ")";
        break;
    }

    return t;
}

template <class T>
T & printUpdateNoteOptions(
    T & t, const ILocalStorage::UpdateNoteOptions options)
{
    using UpdateNoteOption = ILocalStorage::UpdateNoteOption;

    if (options & UpdateNoteOption::UpdateResourceMetadata) {
        t << "Update resource metadata; ";
    }

    if (options & UpdateNoteOption::UpdateResourceBinaryData) {
        t << "Update resource binary data; ";
    }

    if (options & UpdateNoteOption::UpdateTags) {
        t << "Update tags; ";
    }

    return t;
}

template <class T>
T & printFetchNoteOption(T & t, const ILocalStorage::FetchNoteOption option)
{
    using FetchNoteOption = ILocalStorage::FetchNoteOption;

    switch (option) {
    case FetchNoteOption::WithResourceMetadata:
        t << "With resource metadata";
        break;
    case FetchNoteOption::WithResourceBinaryData:
        t << "With resource binary data";
        break;
    default:
        t << "Unknown (" << static_cast<qint64>(option) << ")";
        break;
    }

    return t;
}

template <class T>
T & printFetchNoteOptions(T & t, const ILocalStorage::FetchNoteOptions options)
{
    using FetchNoteOption = ILocalStorage::FetchNoteOption;

    if (options & FetchNoteOption::WithResourceMetadata) {
        t << "With resource metadata; ";
    }

    if (options & FetchNoteOption::WithResourceBinaryData) {
        t << "With resource binary data; ";
    }

    return t;
}

template <class T>
T & printListNotesOrder(T & t, const ILocalStorage::ListNotesOrder order)
{
    using ListNotesOrder = ILocalStorage::ListNotesOrder;

    switch (order) {
    case ListNotesOrder::NoOrder:
        t << "No order";
        break;
    case ListNotesOrder::ByUpdateSequenceNumber:
        t << "By update sequence number";
        break;
    case ListNotesOrder::ByTitle:
        t << "By title";
        break;
    case ListNotesOrder::ByCreationTimestamp:
        t << "By creation timestamp";
        break;
    case ListNotesOrder::ByModificationTimestamp:
        t << "By modification timestamp";
        break;
    case ListNotesOrder::ByDeletionTimestamp:
        t << "By deletion timestamp";
        break;
    case ListNotesOrder::ByAuthor:
        t << "By author";
        break;
    case ListNotesOrder::BySource:
        t << "By source";
        break;
    case ListNotesOrder::BySourceApplication:
        t << "By source application";
        break;
    case ListNotesOrder::ByReminderTime:
        t << "By reminder time";
        break;
    case ListNotesOrder::ByPlaceName:
        t << "By place name";
        break;
    default:
        t << "Unknown (" << static_cast<qint64>(order) << ")";
        break;
    }

    return t;
}

template <class T>
T & printFetchResourceOption(
    T & t, const ILocalStorage::FetchResourceOption option)
{
    using FetchResourceOption = ILocalStorage::FetchResourceOption;

    switch (option) {
    case FetchResourceOption::WithBinaryData:
        t << "With binary metadata";
        break;
    default:
        t << "Unknown (" << static_cast<qint64>(option) << ")";
        break;
    }

    return t;
}

template <class T>
T & printFetchResourceOptions(
    T & t, const ILocalStorage::FetchResourceOptions options)
{
    using FetchResourceOption = ILocalStorage::FetchResourceOption;

    if (options & FetchResourceOption::WithBinaryData) {
        t << "With binary data; ";
    }

    return t;
}

template <class T>
T & printListTagsOrder(T & t, const ILocalStorage::ListTagsOrder order)
{
    using ListTagsOrder = ILocalStorage::ListTagsOrder;

    switch (order) {
    case ListTagsOrder::NoOrder:
        t << "No order";
        break;
    case ListTagsOrder::ByUpdateSequenceNumber:
        t << "By update sequence number";
        break;
    case ListTagsOrder::ByName:
        t << "By name";
        break;
    default:
        t << "Unknown (" << static_cast<qint64>(order) << ")";
        break;
    }

    return t;
}

template <class T>
T & printListSavedSearchesOrder(
    T & t, const ILocalStorage::ListSavedSearchesOrder order)
{
    using ListSavedSearchesOrder = ILocalStorage::ListSavedSearchesOrder;

    switch (order) {
    case ListSavedSearchesOrder::NoOrder:
        t << "No order";
        break;
    case ListSavedSearchesOrder::ByUpdateSequenceNumber:
        t << "By update sequence number";
        break;
    case ListSavedSearchesOrder::ByName:
        t << "By name";
        break;
    case ListSavedSearchesOrder::ByFormat:
        t << "By format";
        break;
    default:
        t << "Unknown (" << static_cast<qint64>(order) << ")";
        break;
    }

    return t;
}

template <class T>
T & printNoteCountOption(T & t, const ILocalStorage::NoteCountOption option)
{
    using NoteCountOption = ILocalStorage::NoteCountOption;

    switch (option) {
    case NoteCountOption::IncludeNonDeletedNotes:
        t << "Include non-deleted notes";
        break;
    case NoteCountOption::IncludeDeletedNotes:
        t << "Include deleted notes";
        break;
    default:
        t << "Unknown (" << static_cast<qint64>(option) << ")";
        break;
    }

    return t;
}

template <class T>
T & printNoteCountOptions(T & t, const ILocalStorage::NoteCountOptions options)
{
    using NoteCountOption = ILocalStorage::NoteCountOption;

    if (options & NoteCountOption::IncludeNonDeletedNotes) {
        t << "Include non-deleted notes; ";
    }

    if (options & NoteCountOption::IncludeDeletedNotes) {
        t << "Include deleted notes; ";
    }

    return t;
}

template <class T>
T & printHighestUsnOption(T & t, const ILocalStorage::HighestUsnOption option)
{
    using HighestUsnOption = ILocalStorage::HighestUsnOption;

    switch (option) {
    case HighestUsnOption::WithinUserOwnContent:
        t << "Within user own content";
        break;
    case HighestUsnOption::WithinUserOwnContentAndLinkedNotebooks:
        t << "Within user own content and linked notebooks";
        break;
    default:
        t << "Unknown (" << static_cast<qint64>(option) << ")";
        break;
    }

    return t;
}

bool compareListOptionsBase(
    const ILocalStorage::ListOptionsBase & lhs,
    const ILocalStorage::ListOptionsBase & rhs) noexcept
{
    return static_cast<qint64>(lhs.m_flags) ==
        static_cast<qint64>(rhs.m_flags) &&
        lhs.m_limit == rhs.m_limit && lhs.m_offset == rhs.m_offset &&
        lhs.m_direction == rhs.m_direction;
}

} // namespace

QTextStream & operator<<(
    QTextStream & strm, const ILocalStorage::StartupOption option)
{
    return printStartupOption(strm, option);
}

QDebug & operator<<(QDebug & dbg, const ILocalStorage::StartupOption option)
{
    return printStartupOption(dbg, option);
}

QTextStream & operator<<(
    QTextStream & strm, const ILocalStorage::StartupOptions options)
{
    return printStartupOptions(strm, options);
}

QDebug & operator<<(QDebug & dbg, const ILocalStorage::StartupOptions options)
{
    return printStartupOptions(dbg, options);
}

QTextStream & operator<<(
    QTextStream & strm, const ILocalStorage::ListObjectsOption option)
{
    return printListObjectsOption(strm, option);
}

QDebug & operator<<(QDebug & dbg, const ILocalStorage::ListObjectsOption option)
{
    return printListObjectsOption(dbg, option);
}

QTextStream & operator<<(
    QTextStream & strm, const ILocalStorage::ListObjectsOptions options)
{
    return printListObjectsOptions(strm, options);
}

QDebug & operator<<(
    QDebug & dbg, const ILocalStorage::ListObjectsOptions options)
{
    return printListObjectsOptions(dbg, options);
}

QTextStream & operator<<(
    QTextStream & strm, const ILocalStorage::OrderDirection orderDirection)
{
    return printOrderDirection(strm, orderDirection);
}

QDebug & operator<<(
    QDebug & dbg, const ILocalStorage::OrderDirection orderDirection)
{
    return printOrderDirection(dbg, orderDirection);
}

QTextStream & operator<<(
    QTextStream & strm, const ILocalStorage::ListNotebooksOrder order)
{
    return printListNotebooksOrder(strm, order);
}

QDebug & operator<<(QDebug & dbg, const ILocalStorage::ListNotebooksOrder order)
{
    return printListNotebooksOrder(dbg, order);
}

QTextStream & operator<<(
    QTextStream & strm, const ILocalStorage::ListLinkedNotebooksOrder order)
{
    return printListLinkedNotebooksOrder(strm, order);
}

QDebug & operator<<(
    QDebug & dbg, const ILocalStorage::ListLinkedNotebooksOrder order)
{
    return printListLinkedNotebooksOrder(dbg, order);
}

QTextStream & operator<<(
    QTextStream & strm, const ILocalStorage::UpdateNoteOption option)
{
    return printUpdateNoteOption(strm, option);
}

QDebug & operator<<(QDebug & dbg, const ILocalStorage::UpdateNoteOption option)
{
    return printUpdateNoteOption(dbg, option);
}

QTextStream & operator<<(
    QTextStream & strm, const ILocalStorage::UpdateNoteOptions options)
{
    return printUpdateNoteOptions(strm, options);
}

QDebug & operator<<(
    QDebug & dbg, const ILocalStorage::UpdateNoteOptions options)
{
    return printUpdateNoteOptions(dbg, options);
}

QTextStream & operator<<(
    QTextStream & strm, const ILocalStorage::FetchNoteOption option)
{
    return printFetchNoteOption(strm, option);
}

QDebug & operator<<(QDebug & dbg, const ILocalStorage::FetchNoteOption option)
{
    return printFetchNoteOption(dbg, option);
}

QTextStream & operator<<(
    QTextStream & strm, const ILocalStorage::FetchNoteOptions options)
{
    return printFetchNoteOptions(strm, options);
}

QDebug & operator<<(QDebug & dbg, const ILocalStorage::FetchNoteOptions options)
{
    return printFetchNoteOptions(dbg, options);
}

QTextStream & operator<<(
    QTextStream & strm, const ILocalStorage::ListNotesOrder order)
{
    return printListNotesOrder(strm, order);
}

QDebug & operator<<(QDebug & dbg, const ILocalStorage::ListNotesOrder order)
{
    return printListNotesOrder(dbg, order);
}

QTextStream & operator<<(
    QTextStream & strm, const ILocalStorage::FetchResourceOption option)
{
    return printFetchResourceOption(strm, option);
}

QDebug & operator<<(
    QDebug & dbg, const ILocalStorage::FetchResourceOption option)
{
    return printFetchResourceOption(dbg, option);
}

QTextStream & operator<<(
    QTextStream & strm, const ILocalStorage::FetchResourceOptions options)
{
    return printFetchResourceOptions(strm, options);
}

QDebug & operator<<(
    QDebug & dbg, const ILocalStorage::FetchResourceOptions options)
{
    return printFetchResourceOptions(dbg, options);
}

QTextStream & operator<<(
    QTextStream & strm, const ILocalStorage::ListTagsOrder order)
{
    return printListTagsOrder(strm, order);
}

QDebug & operator<<(QDebug & dbg, const ILocalStorage::ListTagsOrder order)
{
    return printListTagsOrder(dbg, order);
}

QTextStream & operator<<(
    QTextStream & strm, const ILocalStorage::ListSavedSearchesOrder order)
{
    return printListSavedSearchesOrder(strm, order);
}

QDebug & operator<<(
    QDebug & dbg, const ILocalStorage::ListSavedSearchesOrder order)
{
    return printListSavedSearchesOrder(dbg, order);
}

QTextStream & operator<<(
    QTextStream & strm, const ILocalStorage::Affiliation affiliation)
{
    return printAffiliation(strm, affiliation);
}

QDebug & operator<<(QDebug & dbg, const ILocalStorage::Affiliation affiliation)
{
    return printAffiliation(dbg, affiliation);
}

QTextStream & operator<<(
    QTextStream & strm,
    const ILocalStorage::ListLinkedNotebooksOptions & options)
{
    return printListLinkedNotebooksOptions(strm, options);
}

QDebug & operator<<(
    QDebug & dbg, const ILocalStorage::ListNotebooksOptions & options)
{
    return printListNotebooksOptions(dbg, options);
}

QTextStream & operator<<(
    QTextStream & strm, const ILocalStorage::ListNotebooksOptions & options)
{
    return printListNotebooksOptions(strm, options);
}

QDebug & operator<<(
    QDebug & dbg, const ILocalStorage::ListLinkedNotebooksOptions & options)
{
    return printListLinkedNotebooksOptions(dbg, options);
}

QTextStream & operator<<(
    QTextStream & strm, const ILocalStorage::ListTagsOptions & options)
{
    return printListTagsOptions(strm, options);
}

QDebug & operator<<(
    QDebug & dbg, const ILocalStorage::ListTagsOptions & options)
{
    return printListTagsOptions(dbg, options);
}

QTextStream & operator<<(
    QTextStream & strm, const ILocalStorage::ListNotesOptions & options)
{
    return printListNotesOptions(strm, options);
}

QDebug & operator<<(
    QDebug & dbg, const ILocalStorage::ListNotesOptions & options)
{
    return printListNotesOptions(dbg, options);
}

QTextStream & operator<<(
    QTextStream & strm, const ILocalStorage::ListSavedSearchesOptions & options)
{
    return printListSavedSearchesOptions(strm, options);
}

QDebug & operator<<(
    QDebug & dbg, const ILocalStorage::ListSavedSearchesOptions & options)
{
    return printListSavedSearchesOptions(dbg, options);
}

QTextStream & operator<<(
    QTextStream & strm, const ILocalStorage::NoteCountOption option)
{
    return printNoteCountOption(strm, option);
}

QDebug & operator<<(QDebug & dbg, const ILocalStorage::NoteCountOption option)
{
    return printNoteCountOption(dbg, option);
}

QTextStream & operator<<(
    QTextStream & strm, const ILocalStorage::NoteCountOptions options)
{
    return printNoteCountOptions(strm, options);
}

QDebug & operator<<(QDebug & dbg, const ILocalStorage::NoteCountOptions options)
{
    return printNoteCountOptions(dbg, options);
}

bool operator==(
    const ILocalStorage::ListOptionsBase & lhs,
    const ILocalStorage::ListOptionsBase & rhs) noexcept
{
    return compareListOptionsBase(lhs, rhs);
}

bool operator!=(
    const ILocalStorage::ListOptionsBase & lhs,
    const ILocalStorage::ListOptionsBase & rhs) noexcept
{
    return !(lhs == rhs);
}

bool operator==(
    const ILocalStorage::ListNotebooksOptions & lhs,
    const ILocalStorage::ListNotebooksOptions & rhs) noexcept
{
    if (!compareListOptionsBase(lhs, rhs)) {
        return false;
    }

    return lhs.m_order == rhs.m_order &&
        lhs.m_affiliation == rhs.m_affiliation &&
        lhs.m_linkedNotebookGuids == rhs.m_linkedNotebookGuids;
}

bool operator!=(
    const ILocalStorage::ListNotebooksOptions & lhs,
    const ILocalStorage::ListNotebooksOptions & rhs) noexcept
{
    return !(lhs == rhs);
}

bool operator==(
    const ILocalStorage::ListLinkedNotebooksOptions & lhs,
    const ILocalStorage::ListLinkedNotebooksOptions & rhs) noexcept
{
    if (!compareListOptionsBase(lhs, rhs)) {
        return false;
    }

    return lhs.m_order == rhs.m_order;
}

bool operator!=(
    const ILocalStorage::ListLinkedNotebooksOptions & lhs,
    const ILocalStorage::ListLinkedNotebooksOptions & rhs) noexcept
{
    return !(lhs == rhs);
}

bool operator==(
    const ILocalStorage::ListSavedSearchesOptions & lhs,
    const ILocalStorage::ListSavedSearchesOptions & rhs) noexcept
{
    if (!compareListOptionsBase(lhs, rhs)) {
        return false;
    }

    return lhs.m_order == rhs.m_order;
}

bool operator!=(
    const ILocalStorage::ListSavedSearchesOptions & lhs,
    const ILocalStorage::ListSavedSearchesOptions & rhs) noexcept
{
    return !(lhs == rhs);
}

bool operator==(
    const ILocalStorage::ListNotesOptions & lhs,
    const ILocalStorage::ListNotesOptions & rhs) noexcept
{
    if (!compareListOptionsBase(lhs, rhs)) {
        return false;
    }

    return lhs.m_order == rhs.m_order;
}

bool operator!=(
    const ILocalStorage::ListNotesOptions & lhs,
    const ILocalStorage::ListNotesOptions & rhs) noexcept
{
    return !(lhs == rhs);
}

bool operator==(
    const ILocalStorage::ListTagsOptions & lhs,
    const ILocalStorage::ListTagsOptions & rhs) noexcept
{
    if (!compareListOptionsBase(lhs, rhs)) {
        return false;
    }

    return lhs.m_order == rhs.m_order &&
        lhs.m_affiliation == rhs.m_affiliation &&
        lhs.m_linkedNotebookGuids == rhs.m_linkedNotebookGuids &&
        lhs.m_tagNotesRelation == rhs.m_tagNotesRelation;
}

bool operator!=(
    const ILocalStorage::ListTagsOptions & lhs,
    const ILocalStorage::ListTagsOptions & rhs) noexcept
{
    return !(lhs == rhs);
}

ILocalStoragePtr createSqliteLocalStorage(
    const Account & account, const QDir & localStorageDir,
    QThreadPool * threadPool)
{
    auto localStorageMainFilePath = localStorageDir.absoluteFilePath(
        QStringLiteral("qn.storage.sqlite"));

    auto connectionPool = std::make_shared<sql::ConnectionPool>(
        QStringLiteral("localhost"), QString{}, QString{},
        std::move(localStorageMainFilePath), QStringLiteral("QSQLITE"));

    auto resourceDataFilesLock = std::make_shared<QReadWriteLock>();

    sql::QThreadPtr writerThread;
    {
        auto deleter = [](QThread * thread)
        {
            thread->quit();
            thread->deleteLater();
        };
        auto writerThreadUnique = std::make_unique<QThread>();

        writerThread =
            sql::QThreadPtr{writerThreadUnique.get(), std::move(deleter)};

        Q_UNUSED(writerThreadUnique.release());
    }

    sql::Notifier * notifier = nullptr;
    {
        auto notifierUnique = std::make_unique<sql::Notifier>();
        notifierUnique->moveToThread(writerThread.get());

        QObject::connect(
            writerThread.get(), &QThread::finished, notifierUnique.get(),
            &QObject::deleteLater);

        notifier = notifierUnique.release();
    }

    writerThread->start();

    const QString localStorageDirPath = localStorageDir.absolutePath();

    auto linkedNotebooksHandler =
        std::make_shared<sql::LinkedNotebooksHandler>(
            connectionPool, threadPool, notifier,
            writerThread, localStorageDirPath);

    auto notebooksHandler = std::make_shared<sql::NotebooksHandler>(
        connectionPool, threadPool, notifier,
        writerThread, localStorageDirPath, resourceDataFilesLock);

    auto notesHandler = std::make_shared<sql::NotesHandler>(
        connectionPool, threadPool, notifier,
        writerThread, localStorageDirPath, resourceDataFilesLock);

    auto resourcesHandler = std::make_shared<sql::ResourcesHandler>(
        connectionPool, threadPool, notifier,
        writerThread, localStorageDirPath, resourceDataFilesLock);

    auto savedSearchesHandler = std::make_shared<sql::SavedSearchesHandler>(
        connectionPool, threadPool, notifier, writerThread);

    auto synchronizationInfoHandler =
        std::make_shared<sql::SynchronizationInfoHandler>(
            connectionPool, threadPool, writerThread);

    auto tagsHandler = std::make_shared<sql::TagsHandler>(
        connectionPool, threadPool, notifier, writerThread);

    auto versionHandler = std::make_shared<sql::VersionHandler>(
        account, connectionPool, threadPool, writerThread);

    auto usersHandler = std::make_shared<sql::UsersHandler>(
        connectionPool, threadPool, notifier, writerThread);

    return std::make_shared<sql::LocalStorage>(
        std::move(linkedNotebooksHandler), std::move(notebooksHandler),
        std::move(notesHandler), std::move(resourcesHandler),
        std::move(savedSearchesHandler), std::move(synchronizationInfoHandler),
        std::move(tagsHandler), std::move(versionHandler),
        std::move(usersHandler), notifier);
}

} // namespace quentier::local_storage
