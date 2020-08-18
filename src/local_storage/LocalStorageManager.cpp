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

#include "LocalStorageManager_p.h"

#include <quentier/local_storage/ILocalStoragePatch.h>
#include <quentier/local_storage/LocalStorageManager.h>

namespace quentier {

////////////////////////////////////////////////////////////////////////////////

LocalStorageManager::LocalStorageManager(
    const Account & account, const StartupOptions options, QObject * parent) :
    QObject(parent),
    d_ptr(new LocalStorageManagerPrivate(account, options, this))
{
    QObject::connect(
        d_ptr, &LocalStorageManagerPrivate::upgradeProgress, this,
        &LocalStorageManager::upgradeProgress);
}

LocalStorageManager::~LocalStorageManager()
{
    delete d_ptr;
}

bool LocalStorageManager::addUser(
    const User & user, ErrorString & errorDescription)
{
    Q_D(LocalStorageManager);
    return d->addUser(user, errorDescription);
}

bool LocalStorageManager::updateUser(
    const User & user, ErrorString & errorDescription)
{
    Q_D(LocalStorageManager);
    return d->updateUser(user, errorDescription);
}

bool LocalStorageManager::findUser(
    User & user, ErrorString & errorDescription) const
{
    Q_D(const LocalStorageManager);
    return d->findUser(user, errorDescription);
}

bool LocalStorageManager::deleteUser(
    const User & user, ErrorString & errorDescription)
{
    Q_D(LocalStorageManager);
    return d->deleteUser(user, errorDescription);
}

bool LocalStorageManager::expungeUser(
    const User & user, ErrorString & errorDescription)
{
    Q_D(LocalStorageManager);
    return d->expungeUser(user, errorDescription);
}

int LocalStorageManager::notebookCount(ErrorString & errorDescription) const
{
    Q_D(const LocalStorageManager);
    return d->notebookCount(errorDescription);
}

void LocalStorageManager::switchUser(
    const Account & account, const StartupOptions options)
{
    Q_D(LocalStorageManager);
    d->switchUser(account, options);
}

bool LocalStorageManager::isLocalStorageVersionTooHigh(
    ErrorString & errorDescription)
{
    Q_D(LocalStorageManager);
    return d->isLocalStorageVersionTooHigh(errorDescription);
}

bool LocalStorageManager::localStorageRequiresUpgrade(
    ErrorString & errorDescription)
{
    Q_D(LocalStorageManager);
    return d->localStorageRequiresUpgrade(errorDescription);
}

QVector<std::shared_ptr<ILocalStoragePatch>>
LocalStorageManager::requiredLocalStoragePatches()
{
    Q_D(LocalStorageManager);
    return d->requiredLocalStoragePatches();
}

qint32 LocalStorageManager::localStorageVersion(ErrorString & errorDescription)
{
    Q_D(LocalStorageManager);
    return d->localStorageVersion(errorDescription);
}

qint32 LocalStorageManager::highestSupportedLocalStorageVersion() const
{
    Q_D(const LocalStorageManager);
    return d->highestSupportedLocalStorageVersion();
}

int LocalStorageManager::userCount(ErrorString & errorDescription) const
{
    Q_D(const LocalStorageManager);
    return d->userCount(errorDescription);
}

bool LocalStorageManager::addNotebook(
    Notebook & notebook, ErrorString & errorDescription)
{
    Q_D(LocalStorageManager);
    return d->addNotebook(notebook, errorDescription);
}

bool LocalStorageManager::updateNotebook(
    Notebook & notebook, ErrorString & errorDescription)
{
    Q_D(LocalStorageManager);
    return d->updateNotebook(notebook, errorDescription);
}

bool LocalStorageManager::findNotebook(
    Notebook & notebook, ErrorString & errorDescription) const
{
    Q_D(const LocalStorageManager);
    return d->findNotebook(notebook, errorDescription);
}

bool LocalStorageManager::findDefaultNotebook(
    Notebook & notebook, ErrorString & errorDescription) const
{
    Q_D(const LocalStorageManager);
    return d->findDefaultNotebook(notebook, errorDescription);
}

bool LocalStorageManager::findLastUsedNotebook(
    Notebook & notebook, ErrorString & errorDescription) const
{
    Q_D(const LocalStorageManager);
    return d->findLastUsedNotebook(notebook, errorDescription);
}

bool LocalStorageManager::findDefaultOrLastUsedNotebook(
    Notebook & notebook, ErrorString & errorDescription) const
{
    Q_D(const LocalStorageManager);
    return d->findDefaultOrLastUsedNotebook(notebook, errorDescription);
}

QList<Notebook> LocalStorageManager::listAllNotebooks(
    ErrorString & errorDescription, const size_t limit, const size_t offset,
    const ListNotebooksOrder order, const OrderDirection orderDirection,
    const QString & linkedNotebookGuid) const
{
    Q_D(const LocalStorageManager);
    return d->listAllNotebooks(
        errorDescription, limit, offset, order, orderDirection,
        linkedNotebookGuid);
}

QList<Notebook> LocalStorageManager::listNotebooks(
    const ListObjectsOptions flag, ErrorString & errorDescription,
    const size_t limit, const size_t offset, const ListNotebooksOrder order,
    const OrderDirection orderDirection,
    const QString & linkedNotebookGuid) const
{
    Q_D(const LocalStorageManager);
    return d->listNotebooks(
        flag, errorDescription, limit, offset, order, orderDirection,
        linkedNotebookGuid);
}

QList<SharedNotebook> LocalStorageManager::listAllSharedNotebooks(
    ErrorString & errorDescription) const
{
    Q_D(const LocalStorageManager);
    return d->listAllSharedNotebooks(errorDescription);
}

QList<SharedNotebook> LocalStorageManager::listSharedNotebooksPerNotebookGuid(
    const QString & notebookGuid, ErrorString & errorDescription) const
{
    Q_D(const LocalStorageManager);
    return d->listSharedNotebooksPerNotebookGuid(
        notebookGuid, errorDescription);
}

bool LocalStorageManager::expungeNotebook(
    Notebook & notebook, ErrorString & errorDescription)
{
    Q_D(LocalStorageManager);
    return d->expungeNotebook(notebook, errorDescription);
}

int LocalStorageManager::linkedNotebookCount(
    ErrorString & errorDescription) const
{
    Q_D(const LocalStorageManager);
    return d->linkedNotebookCount(errorDescription);
}

bool LocalStorageManager::addLinkedNotebook(
    const LinkedNotebook & linkedNotebook, ErrorString & errorDescription)
{
    Q_D(LocalStorageManager);
    return d->addLinkedNotebook(linkedNotebook, errorDescription);
}

bool LocalStorageManager::updateLinkedNotebook(
    const LinkedNotebook & linkedNotebook, ErrorString & errorDescription)
{
    Q_D(LocalStorageManager);
    return d->updateLinkedNotebook(linkedNotebook, errorDescription);
}

bool LocalStorageManager::findLinkedNotebook(
    LinkedNotebook & linkedNotebook, ErrorString & errorDescription) const
{
    Q_D(const LocalStorageManager);
    return d->findLinkedNotebook(linkedNotebook, errorDescription);
}

QList<LinkedNotebook> LocalStorageManager::listAllLinkedNotebooks(
    ErrorString & errorDescription, const size_t limit, const size_t offset,
    const ListLinkedNotebooksOrder order,
    const OrderDirection orderDirection) const
{
    Q_D(const LocalStorageManager);
    return d->listAllLinkedNotebooks(
        errorDescription, limit, offset, order, orderDirection);
}

QList<LinkedNotebook> LocalStorageManager::listLinkedNotebooks(
    const ListObjectsOptions flag, ErrorString & errorDescription,
    const size_t limit, const size_t offset,
    const ListLinkedNotebooksOrder order,
    const OrderDirection orderDirection) const
{
    Q_D(const LocalStorageManager);
    return d->listLinkedNotebooks(
        flag, errorDescription, limit, offset, order, orderDirection);
}

bool LocalStorageManager::expungeLinkedNotebook(
    const LinkedNotebook & linkedNotebook, ErrorString & errorDescription)
{
    Q_D(LocalStorageManager);
    return d->expungeLinkedNotebook(linkedNotebook, errorDescription);
}

int LocalStorageManager::noteCount(
    ErrorString & errorDescription, const NoteCountOptions options) const
{
    Q_D(const LocalStorageManager);
    return d->noteCount(errorDescription, options);
}

int LocalStorageManager::noteCountPerNotebook(
    const Notebook & notebook, ErrorString & errorDescription,
    const LocalStorageManager::NoteCountOptions options) const
{
    Q_D(const LocalStorageManager);
    return d->noteCountPerNotebook(notebook, errorDescription, options);
}

int LocalStorageManager::noteCountPerTag(
    const Tag & tag, ErrorString & errorDescription,
    const LocalStorageManager::NoteCountOptions options) const
{
    Q_D(const LocalStorageManager);
    return d->noteCountPerTag(tag, errorDescription, options);
}

bool LocalStorageManager::noteCountsPerAllTags(
    QHash<QString, int> & noteCountsPerTagLocalUid,
    ErrorString & errorDescription,
    const LocalStorageManager::NoteCountOptions options) const
{
    Q_D(const LocalStorageManager);
    return d->noteCountsPerAllTags(
        noteCountsPerTagLocalUid, errorDescription, options);
}

int LocalStorageManager::noteCountPerNotebooksAndTags(
    const QStringList & notebookLocalUids, const QStringList & tagLocalUids,
    ErrorString & errorDescription,
    const LocalStorageManager::NoteCountOptions options) const
{
    Q_D(const LocalStorageManager);
    return d->noteCountPerNotebooksAndTags(
        notebookLocalUids, tagLocalUids, errorDescription, options);
}

bool LocalStorageManager::addNote(Note & note, ErrorString & errorDescription)
{
    Q_D(LocalStorageManager);
    return d->addNote(note, errorDescription);
}

bool LocalStorageManager::updateNote(
    Note & note, const UpdateNoteOptions options,
    ErrorString & errorDescription)
{
    Q_D(LocalStorageManager);
    return d->updateNote(note, options, errorDescription);
}

bool LocalStorageManager::findNote(
    Note & note, const GetNoteOptions options,
    ErrorString & errorDescription) const
{
    Q_D(const LocalStorageManager);
    return d->findNote(note, options, errorDescription);
}

QList<Note> LocalStorageManager::listNotesPerNotebook(
    const Notebook & notebook, const GetNoteOptions options,
    ErrorString & errorDescription, const ListObjectsOptions & flag,
    const size_t limit, const size_t offset, const ListNotesOrder & order,
    const OrderDirection & orderDirection) const
{
    Q_D(const LocalStorageManager);
    return d->listNotesPerNotebook(
        notebook, options, errorDescription, flag, limit, offset, order,
        orderDirection);
}

QList<Note> LocalStorageManager::listNotesPerTag(
    const Tag & tag, const GetNoteOptions options,
    ErrorString & errorDescription, const ListObjectsOptions & flag,
    const size_t limit, const size_t offset, const ListNotesOrder & order,
    const OrderDirection & orderDirection) const
{
    Q_D(const LocalStorageManager);
    return d->listNotesPerTag(
        tag, options, errorDescription, flag, limit, offset, order,
        orderDirection);
}

QList<Note> LocalStorageManager::listNotesPerNotebooksAndTags(
    const QStringList & notebookLocalUids, const QStringList & tagLocalUids,
    const LocalStorageManager::GetNoteOptions options,
    ErrorString & errorDescription,
    const LocalStorageManager::ListObjectsOptions & flag, const size_t limit,
    const size_t offset, const LocalStorageManager::ListNotesOrder & order,
    const LocalStorageManager::OrderDirection & orderDirection) const
{
    Q_D(const LocalStorageManager);
    return d->listNotesPerNotebooksAndTags(
        notebookLocalUids, tagLocalUids, options, errorDescription, flag, limit,
        offset, order, orderDirection);
}

QList<Note> LocalStorageManager::listNotesByLocalUids(
    const QStringList & noteLocalUids,
    const LocalStorageManager::GetNoteOptions options,
    ErrorString & errorDescription,
    const LocalStorageManager::ListObjectsOptions & flag, const size_t limit,
    const size_t offset, const LocalStorageManager::ListNotesOrder & order,
    const LocalStorageManager::OrderDirection & orderDirection) const
{
    Q_D(const LocalStorageManager);
    return d->listNotesByLocalUids(
        noteLocalUids, options, errorDescription, flag, limit, offset, order,
        orderDirection);
}

QList<Note> LocalStorageManager::listNotes(
    const ListObjectsOptions flag, const GetNoteOptions options,
    ErrorString & errorDescription, const size_t limit, const size_t offset,
    const ListNotesOrder order, const OrderDirection orderDirection,
    const QString & linkedNotebookGuid) const
{
    Q_D(const LocalStorageManager);
    return d->listNotes(
        flag, options, errorDescription, limit, offset, order, orderDirection,
        linkedNotebookGuid);
}

QStringList LocalStorageManager::findNoteLocalUidsWithSearchQuery(
    const NoteSearchQuery & noteSearchQuery,
    ErrorString & errorDescription) const
{
    Q_D(const LocalStorageManager);
    return d->findNoteLocalUidsWithSearchQuery(
        noteSearchQuery, errorDescription);
}

NoteList LocalStorageManager::findNotesWithSearchQuery(
    const NoteSearchQuery & noteSearchQuery, const GetNoteOptions options,
    ErrorString & errorDescription) const
{
    Q_D(const LocalStorageManager);
    return d->findNotesWithSearchQuery(
        noteSearchQuery, options, errorDescription);
}

bool LocalStorageManager::expungeNote(
    Note & note, ErrorString & errorDescription)
{
    Q_D(LocalStorageManager);
    return d->expungeNote(note, errorDescription);
}

int LocalStorageManager::tagCount(ErrorString & errorDescription) const
{
    Q_D(const LocalStorageManager);
    return d->tagCount(errorDescription);
}

bool LocalStorageManager::addTag(Tag & tag, ErrorString & errorDescription)
{
    Q_D(LocalStorageManager);
    return d->addTag(tag, errorDescription);
}

bool LocalStorageManager::updateTag(Tag & tag, ErrorString & errorDescription)
{
    Q_D(LocalStorageManager);
    return d->updateTag(tag, errorDescription);
}

bool LocalStorageManager::findTag(
    Tag & tag, ErrorString & errorDescription) const
{
    Q_D(const LocalStorageManager);
    return d->findTag(tag, errorDescription);
}

QList<Tag> LocalStorageManager::listAllTagsPerNote(
    const Note & note, ErrorString & errorDescription,
    const ListObjectsOptions & flag, const size_t limit, const size_t offset,
    const ListTagsOrder & order, const OrderDirection & orderDirection) const
{
    Q_D(const LocalStorageManager);
    return d->listAllTagsPerNote(
        note, errorDescription, flag, limit, offset, order, orderDirection);
}

QList<Tag> LocalStorageManager::listAllTags(
    ErrorString & errorDescription, const size_t limit, const size_t offset,
    const ListTagsOrder order, const OrderDirection orderDirection,
    const QString & linkedNotebookGuid) const
{
    Q_D(const LocalStorageManager);
    return d->listAllTags(
        errorDescription, limit, offset, order, orderDirection,
        linkedNotebookGuid);
}

QList<Tag> LocalStorageManager::listTags(
    const ListObjectsOptions flag, ErrorString & errorDescription,
    const size_t limit, const size_t offset, const ListTagsOrder & order,
    const OrderDirection orderDirection,
    const QString & linkedNotebookGuid) const
{
    Q_D(const LocalStorageManager);
    return d->listTags(
        flag, errorDescription, limit, offset, order, orderDirection,
        linkedNotebookGuid);
}

QList<std::pair<Tag, QStringList>>
LocalStorageManager::listTagsWithNoteLocalUids(
    const ListObjectsOptions flag, ErrorString & errorDescription,
    const size_t limit, const size_t offset, const ListTagsOrder & order,
    const OrderDirection orderDirection,
    const QString & linkedNotebookGuid) const
{
    Q_D(const LocalStorageManager);
    return d->listTagsWithNoteLocalUids(
        flag, errorDescription, limit, offset, order, orderDirection,
        linkedNotebookGuid);
}

bool LocalStorageManager::expungeTag(
    Tag & tag, QStringList & expungedChildTagLocalUids,
    ErrorString & errorDescription)
{
    Q_D(LocalStorageManager);
    return d->expungeTag(tag, expungedChildTagLocalUids, errorDescription);
}

bool LocalStorageManager::expungeNotelessTagsFromLinkedNotebooks(
    ErrorString & errorDescription)
{
    Q_D(LocalStorageManager);
    return d->expungeNotelessTagsFromLinkedNotebooks(errorDescription);
}

int LocalStorageManager::enResourceCount(ErrorString & errorDescription) const
{
    Q_D(const LocalStorageManager);
    return d->enResourceCount(errorDescription);
}

bool LocalStorageManager::addEnResource(
    Resource & resource, ErrorString & errorDescription)
{
    Q_D(LocalStorageManager);
    return d->addEnResource(resource, errorDescription);
}

bool LocalStorageManager::updateEnResource(
    Resource & resource, ErrorString & errorDescription)
{
    Q_D(LocalStorageManager);
    return d->updateEnResource(resource, errorDescription);
}

bool LocalStorageManager::findEnResource(
    Resource & resource, const GetResourceOptions options,
    ErrorString & errorDescription) const
{
    Q_D(const LocalStorageManager);
    return d->findEnResource(resource, options, errorDescription);
}

bool LocalStorageManager::expungeEnResource(
    Resource & resource, ErrorString & errorDescription)
{
    Q_D(LocalStorageManager);
    return d->expungeEnResource(resource, errorDescription);
}

int LocalStorageManager::savedSearchCount(ErrorString & errorDescription) const
{
    Q_D(const LocalStorageManager);
    return d->savedSearchCount(errorDescription);
}

bool LocalStorageManager::addSavedSearch(
    SavedSearch & search, ErrorString & errorDescription)
{
    Q_D(LocalStorageManager);
    return d->addSavedSearch(search, errorDescription);
}

bool LocalStorageManager::updateSavedSearch(
    SavedSearch & search, ErrorString & errorDescription)
{
    Q_D(LocalStorageManager);
    return d->updateSavedSearch(search, errorDescription);
}

bool LocalStorageManager::findSavedSearch(
    SavedSearch & search, ErrorString & errorDescription) const
{
    Q_D(const LocalStorageManager);
    return d->findSavedSearch(search, errorDescription);
}

QList<SavedSearch> LocalStorageManager::listAllSavedSearches(
    ErrorString & errorDescription, const size_t limit, const size_t offset,
    const ListSavedSearchesOrder order,
    const OrderDirection orderDirection) const
{
    Q_D(const LocalStorageManager);
    return d->listAllSavedSearches(
        errorDescription, limit, offset, order, orderDirection);
}

QList<SavedSearch> LocalStorageManager::listSavedSearches(
    const ListObjectsOptions flag, ErrorString & errorDescription,
    const size_t limit, const size_t offset, const ListSavedSearchesOrder order,
    const OrderDirection orderDirection) const
{
    Q_D(const LocalStorageManager);
    return d->listSavedSearches(
        flag, errorDescription, limit, offset, order, orderDirection);
}

bool LocalStorageManager::expungeSavedSearch(
    SavedSearch & search, ErrorString & errorDescription)
{
    Q_D(LocalStorageManager);
    return d->expungeSavedSearch(search, errorDescription);
}

qint32 LocalStorageManager::accountHighUsn(
    const QString & linkedNotebookGuid, ErrorString & errorDescription)
{
    Q_D(LocalStorageManager);
    return d->accountHighUsn(linkedNotebookGuid, errorDescription);
}

////////////////////////////////////////////////////////////////////////////////

namespace {

template <typename T>
T & printStartupOption(T & t, const LocalStorageManager::StartupOption option)
{
    using StartupOption = LocalStorageManager::StartupOption;

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

template <typename T>
T & printStartupOptions(
    T & t, const LocalStorageManager::StartupOptions options)
{
    using StartupOption = LocalStorageManager::StartupOption;

    if (options & StartupOption::ClearDatabase) {
        t << "Clear database; ";
    }

    if (options & StartupOption::OverrideLock) {
        t << "Override lock; ";
    }

    return t;
}

} // namespace

QTextStream & operator<<(
    QTextStream & strm, const LocalStorageManager::StartupOption option)
{
    return printStartupOption(strm, option);
}

QDebug & operator<<(
    QDebug & dbg, const LocalStorageManager::StartupOption option)
{
    return printStartupOption(dbg, option);
}

QTextStream & operator<<(
    QTextStream & strm, const LocalStorageManager::StartupOptions options)
{
    return printStartupOptions(strm, options);
}

QDebug & operator<<(
    QDebug & dbg, const LocalStorageManager::StartupOptions options)
{
    return printStartupOptions(dbg, options);
}

////////////////////////////////////////////////////////////////////////////////

namespace {

template <typename T>
T & printListObjectsOption(
    T & t, const LocalStorageManager::ListObjectsOption option)
{
    using ListObjectsOption = LocalStorageManager::ListObjectsOption;

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

template <typename T>
T & printListObjectsOptions(
    T & t, const LocalStorageManager::ListObjectsOptions options)
{
    using ListObjectsOption = LocalStorageManager::ListObjectsOption;

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

} // namespace

QTextStream & operator<<(
    QTextStream & strm, const LocalStorageManager::ListObjectsOption option)
{
    return printListObjectsOption(strm, option);
}

QDebug & operator<<(
    QDebug & dbg, const LocalStorageManager::ListObjectsOption option)
{
    return printListObjectsOption(dbg, option);
}

QTextStream & operator<<(
    QTextStream & strm, const LocalStorageManager::ListObjectsOptions options)
{
    return printListObjectsOptions(strm, options);
}

QDebug & operator<<(
    QDebug & dbg, const LocalStorageManager::ListObjectsOptions options)
{
    return printListObjectsOptions(dbg, options);
}

////////////////////////////////////////////////////////////////////////////////

namespace {

template <typename T>
T & printOrderDirection(
    T & t, const LocalStorageManager::OrderDirection orderDirection)
{
    using OrderDirection = LocalStorageManager::OrderDirection;

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

} // namespace

QTextStream & operator<<(
    QTextStream & strm,
    const LocalStorageManager::OrderDirection orderDirection)
{
    return printOrderDirection(strm, orderDirection);
}

QDebug & operator<<(
    QDebug & dbg, const LocalStorageManager::OrderDirection orderDirection)
{
    return printOrderDirection(dbg, orderDirection);
}

////////////////////////////////////////////////////////////////////////////////

namespace {

template <typename T>
T & printListNotebooksOrder(
    T & t, const LocalStorageManager::ListNotebooksOrder order)
{
    using ListNotebooksOrder = LocalStorageManager::ListNotebooksOrder;

    switch (order) {
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
    case ListNotebooksOrder::NoOrder:
        t << "No order";
        break;
    default:
        t << "Unknown (" << static_cast<qint64>(order) << ")";
        break;
    }

    return t;
}

} // namespace

QTextStream & operator<<(
    QTextStream & strm, const LocalStorageManager::ListNotebooksOrder order)
{
    return printListNotebooksOrder(strm, order);
}

QDebug & operator<<(
    QDebug & dbg, const LocalStorageManager::ListNotebooksOrder order)
{
    return printListNotebooksOrder(dbg, order);
}

////////////////////////////////////////////////////////////////////////////////

namespace {

template <typename T>
T & printListLinkedNotebooksOrder(
    T & t, const LocalStorageManager::ListLinkedNotebooksOrder order)
{
    using ListLinkedNotebooksOrder =
        LocalStorageManager::ListLinkedNotebooksOrder;

    switch (order) {
    case ListLinkedNotebooksOrder::ByUpdateSequenceNumber:
        t << "By update sequence number";
        break;
    case ListLinkedNotebooksOrder::ByShareName:
        t << "By share name";
        break;
    case ListLinkedNotebooksOrder::ByUsername:
        t << "By username";
        break;
    case ListLinkedNotebooksOrder::NoOrder:
        t << "No order";
        break;
    default:
        t << "Unknown (" << static_cast<qint64>(order) << ")";
        break;
    }

    return t;
}

} // namespace

QTextStream & operator<<(
    QTextStream & strm,
    const LocalStorageManager::ListLinkedNotebooksOrder order)
{
    return printListLinkedNotebooksOrder(strm, order);
}

QDebug & operator<<(
    QDebug & dbg, const LocalStorageManager::ListLinkedNotebooksOrder order)
{
    return printListLinkedNotebooksOrder(dbg, order);
}

////////////////////////////////////////////////////////////////////////////////

namespace {

template <typename T>
T & printUpdateNoteOption(
    T & t, const LocalStorageManager::UpdateNoteOption option)
{
    using UpdateNoteOption = LocalStorageManager::UpdateNoteOption;

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

template <typename T>
T & printUpdateNoteOptions(
    T & t, const LocalStorageManager::UpdateNoteOptions options)
{
    using UpdateNoteOption = LocalStorageManager::UpdateNoteOption;

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

} // namespace

QTextStream & operator<<(
    QTextStream & strm, const LocalStorageManager::UpdateNoteOption option)
{
    return printUpdateNoteOption(strm, option);
}

QDebug & operator<<(
    QDebug & dbg, const LocalStorageManager::UpdateNoteOption option)
{
    return printUpdateNoteOption(dbg, option);
}

QTextStream & operator<<(
    QTextStream & strm, const LocalStorageManager::UpdateNoteOptions options)
{
    return printUpdateNoteOptions(strm, options);
}

QDebug & operator<<(
    QDebug & dbg, const LocalStorageManager::UpdateNoteOptions options)
{
    return printUpdateNoteOptions(dbg, options);
}

////////////////////////////////////////////////////////////////////////////////

namespace {

template <typename T>
T & printGetNoteOption(T & t, const LocalStorageManager::GetNoteOption option)
{
    using GetNoteOption = LocalStorageManager::GetNoteOption;

    switch (option) {
    case GetNoteOption::WithResourceMetadata:
        t << "With resource metadata";
        break;
    case GetNoteOption::WithResourceBinaryData:
        t << "With resource binary data";
        break;
    default:
        t << "Unknown (" << static_cast<qint64>(option) << ")";
        break;
    }

    return t;
}

template <typename T>
T & printGetNoteOptions(
    T & t, const LocalStorageManager::GetNoteOptions options)
{
    using GetNoteOption = LocalStorageManager::GetNoteOption;

    if (options & GetNoteOption::WithResourceMetadata) {
        t << "With resource metadata; ";
    }

    if (options & GetNoteOption::WithResourceBinaryData) {
        t << "With resource binary data; ";
    }

    return t;
}

} // namespace

QTextStream & operator<<(
    QTextStream & strm, const LocalStorageManager::GetNoteOption option)
{
    return printGetNoteOption(strm, option);
}

QDebug & operator<<(
    QDebug & dbg, const LocalStorageManager::GetNoteOption option)
{
    return printGetNoteOption(dbg, option);
}

QTextStream & operator<<(
    QTextStream & strm, const LocalStorageManager::GetNoteOptions options)
{
    return printGetNoteOptions(strm, options);
}

QDebug & operator<<(
    QDebug & dbg, const LocalStorageManager::GetNoteOptions options)
{
    return printGetNoteOptions(dbg, options);
}

////////////////////////////////////////////////////////////////////////////////

namespace {

template <typename T>
T & printListNotesOrder(T & t, const LocalStorageManager::ListNotesOrder order)
{
    using ListNotesOrder = LocalStorageManager::ListNotesOrder;

    switch (order) {
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
    case ListNotesOrder::NoOrder:
        t << "No order";
        break;
    default:
        t << "Unknown (" << static_cast<qint64>(order) << ")";
        break;
    }

    return t;
}

} // namespace

QTextStream & operator<<(
    QTextStream & strm, const LocalStorageManager::ListNotesOrder order)
{
    return printListNotesOrder(strm, order);
}

QDebug & operator<<(
    QDebug & dbg, const LocalStorageManager::ListNotesOrder order)
{
    return printListNotesOrder(dbg, order);
}

////////////////////////////////////////////////////////////////////////////////

namespace {

template <typename T>
T & printGetResourceOption(
    T & t, const LocalStorageManager::GetResourceOption option)
{
    using GetResourceOption = LocalStorageManager::GetResourceOption;

    switch (option) {
    case GetResourceOption::WithBinaryData:
        t << "With binary metadata";
        break;
    default:
        t << "Unknown (" << static_cast<qint64>(option) << ")";
        break;
    }

    return t;
}

template <typename T>
T & printGetResourceOptions(
    T & t, const LocalStorageManager::GetResourceOptions options)
{
    using GetResourceOption = LocalStorageManager::GetResourceOption;

    if (options & GetResourceOption::WithBinaryData) {
        t << "With binary data; ";
    }

    return t;
}

} // namespace

QTextStream & operator<<(
    QTextStream & strm, const LocalStorageManager::GetResourceOption option)
{
    return printGetResourceOption(strm, option);
}

QDebug & operator<<(
    QDebug & dbg, const LocalStorageManager::GetResourceOption option)
{
    return printGetResourceOption(dbg, option);
}

QTextStream & operator<<(
    QTextStream & strm, const LocalStorageManager::GetResourceOptions options)
{
    return printGetResourceOptions(strm, options);
}

QDebug & operator<<(
    QDebug & dbg, const LocalStorageManager::GetResourceOptions options)
{
    return printGetResourceOptions(dbg, options);
}

////////////////////////////////////////////////////////////////////////////////

namespace {

template <typename T>
T & printListTagsOrder(T & t, const LocalStorageManager::ListTagsOrder order)
{
    using ListTagsOrder = LocalStorageManager::ListTagsOrder;

    switch (order) {
    case ListTagsOrder::ByUpdateSequenceNumber:
        t << "By update sequence number";
        break;
    case ListTagsOrder::ByName:
        t << "By name";
        break;
    case ListTagsOrder::NoOrder:
        t << "No order";
        break;
    default:
        t << "Unknown (" << static_cast<qint64>(order) << ")";
        break;
    }

    return t;
}

} // namespace

QTextStream & operator<<(
    QTextStream & strm, const LocalStorageManager::ListTagsOrder order)
{
    return printListTagsOrder(strm, order);
}

QDebug & operator<<(
    QDebug & dbg, const LocalStorageManager::ListTagsOrder order)
{
    return printListTagsOrder(dbg, order);
}

////////////////////////////////////////////////////////////////////////////////

namespace {

template <typename T>
T & printListSavedSearchesOrder(
    T & t, const LocalStorageManager::ListSavedSearchesOrder order)
{
    using ListSavedSearchesOrder = LocalStorageManager::ListSavedSearchesOrder;

    switch (order) {
    case ListSavedSearchesOrder::ByUpdateSequenceNumber:
        t << "By update sequence number";
        break;
    case ListSavedSearchesOrder::ByName:
        t << "By name";
        break;
    case ListSavedSearchesOrder::ByFormat:
        t << "By format";
        break;
    case ListSavedSearchesOrder::NoOrder:
        t << "No order";
        break;
    default:
        t << "Unknown (" << static_cast<qint64>(order) << ")";
        break;
    }

    return t;
}

} // namespace

QTextStream & operator<<(
    QTextStream & strm, const LocalStorageManager::ListSavedSearchesOrder order)
{
    return printListSavedSearchesOrder(strm, order);
}

QDebug & operator<<(
    QDebug & dbg, const LocalStorageManager::ListSavedSearchesOrder order)
{
    return printListSavedSearchesOrder(dbg, order);
}

////////////////////////////////////////////////////////////////////////////////

namespace {

template <typename T>
T & printNoteCountOption(
    T & t, const LocalStorageManager::NoteCountOption option)
{
    using NoteCountOption = LocalStorageManager::NoteCountOption;

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

template <typename T>
T & printNoteCountOptions(
    T & t, const LocalStorageManager::NoteCountOptions options)
{
    using NoteCountOption = LocalStorageManager::NoteCountOption;

    if (options & NoteCountOption::IncludeNonDeletedNotes) {
        t << "Include non-deleted notes; ";
    }

    if (options & NoteCountOption::IncludeDeletedNotes) {
        t << "Include deleted notes; ";
    }

    return t;
}

} // namespace

QTextStream & operator<<(
    QTextStream & strm, const LocalStorageManager::NoteCountOption option)
{
    return printNoteCountOption(strm, option);
}

QDebug & operator<<(
    QDebug & dbg, const LocalStorageManager::NoteCountOption option)
{
    return printNoteCountOption(dbg, option);
}

QTextStream & operator<<(
    QTextStream & strm, const LocalStorageManager::NoteCountOptions options)
{
    return printNoteCountOptions(strm, options);
}

QDebug & operator<<(
    QDebug & dbg, const LocalStorageManager::NoteCountOptions options)
{
    return printNoteCountOptions(dbg, options);
}

} // namespace quentier
