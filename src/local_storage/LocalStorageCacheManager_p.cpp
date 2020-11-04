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

#include "LocalStorageCacheManager_p.h"

#include <quentier/exception/LocalStorageCacheManagerException.h>
#include <quentier/local_storage/DefaultLocalStorageCacheExpiryChecker.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/DateTime.h>
#include <quentier/utility/QuentierCheckPtr.h>

#include <QDateTime>

namespace quentier {

////////////////////////////////////////////////////////////////////////////////

LocalStorageCacheManagerPrivate::LocalStorageCacheManagerPrivate(
    LocalStorageCacheManager & q) :
    q_ptr(&q),
    m_cacheExpiryChecker(new DefaultLocalStorageCacheExpiryChecker(q))
{}

LocalStorageCacheManagerPrivate::~LocalStorageCacheManagerPrivate() {}

void LocalStorageCacheManagerPrivate::clear()
{
    m_notesCache.clear();
    m_resourcesCache.clear();
    m_notebooksCache.clear();
    m_tagsCache.clear();
    m_linkedNotebooksCache.clear();
    m_savedSearchesCache.clear();
}

bool LocalStorageCacheManagerPrivate::empty() const
{
    return (
        m_notesCache.empty() && m_resourcesCache.empty() &&
        m_notebooksCache.empty() && m_tagsCache.empty() &&
        m_linkedNotebooksCache.empty() && m_savedSearchesCache.empty());
}

////////////////////////////////////////////////////////////////////////////////

size_t LocalStorageCacheManagerPrivate::numCachedNotes() const
{
    return m_notesCache.get<NoteHolder::ByLocalUid>().size();
}

size_t LocalStorageCacheManagerPrivate::numCachedResources() const
{
    return m_resourcesCache.get<ResourceHolder::ByLocalUid>().size();
}

size_t LocalStorageCacheManagerPrivate::numCachedNotebooks() const
{
    return m_notebooksCache.get<NotebookHolder::ByLocalUid>().size();
}

size_t LocalStorageCacheManagerPrivate::numCachedTags() const
{
    return m_tagsCache.get<TagHolder::ByLocalUid>().size();
}

size_t LocalStorageCacheManagerPrivate::numCachedLinkedNotebooks() const
{
    return m_linkedNotebooksCache.get<LinkedNotebookHolder::ByGuid>().size();
}

size_t LocalStorageCacheManagerPrivate::numCachedSavedSearches() const
{
    return m_savedSearchesCache.get<SavedSearchHolder::ByLocalUid>().size();
}

////////////////////////////////////////////////////////////////////////////////

namespace {

////////////////////////////////////////////////////////////////////////////////

template <typename TItem>
QString itemId(const TItem & item)
{
    return item.localUid();
}

template <>
QString itemId(const LinkedNotebook & linkedNotebook)
{
    return linkedNotebook.guid();
}

////////////////////////////////////////////////////////////////////////////////

template <typename T>
bool checkExpiry(ILocalStorageCacheExpiryChecker & checker);

template <>
bool checkExpiry<Note>(ILocalStorageCacheExpiryChecker & checker)
{
    return checker.checkNotes();
}

template <>
bool checkExpiry<Notebook>(ILocalStorageCacheExpiryChecker & checker)
{
    return checker.checkNotebooks();
}

template <>
bool checkExpiry<Tag>(ILocalStorageCacheExpiryChecker & checker)
{
    return checker.checkTags();
}

template <>
bool checkExpiry<Resource>(ILocalStorageCacheExpiryChecker & checker)
{
    return checker.checkResources();
}

template <>
bool checkExpiry<LinkedNotebook>(ILocalStorageCacheExpiryChecker & checker)
{
    return checker.checkLinkedNotebooks();
}

template <>
bool checkExpiry<SavedSearch>(ILocalStorageCacheExpiryChecker & checker)
{
    return checker.checkSavedSearches();
}

////////////////////////////////////////////////////////////////////////////////

template <
    typename TItem, typename TCache, typename THolder, typename TIndex,
    typename TChecker>
void cacheItem(
    const TItem & item, const QString & itemTypeName, TCache & cache,
    TChecker * pChecker)
{
    auto & latIndex =
        cache.template get<typename THolder::ByLastAccessTimestamp>();

    if (Q_LIKELY(pChecker)) {
        bool res = false;
        while (!res && !latIndex.empty()) {
            res = checkExpiry<TItem>(*pChecker);
            if (Q_UNLIKELY(!res)) {
                auto latIndexBegin = latIndex.begin();
                QNTRACE(
                    "local_storage",
                    "Going to remove the object from "
                        << "the local storage cache: " << *latIndexBegin);
                Q_UNUSED(latIndex.erase(latIndexBegin));
                continue;
            }
        }
    }

    THolder holder;
    holder.m_value = item;
    holder.m_lastAccessTimestamp = QDateTime::currentMSecsSinceEpoch();

    // See whether the item is already in the cache
    auto & uniqueIndex = cache.template get<TIndex>();
    auto it = uniqueIndex.find(itemId(item));
    if (it != uniqueIndex.end()) {
        uniqueIndex.replace(it, holder);
        QNTRACE(
            "local_storage",
            "Updated " << itemTypeName
                       << " in the local storage cache: " << item);
        return;
    }

    // If got here, no existing item was found in the cache
    auto insertionResult = cache.insert(holder);
    if (Q_UNLIKELY(!insertionResult.second)) {
        QNWARNING(
            "local_storage",
            "Failed to insert "
                << itemTypeName
                << " into the cache of local storage manager: " << item);

        ErrorString error(QT_TRANSLATE_NOOP(
            "LocalStorageCacheManagerPrivate",
            "Unable to insert the data item into "
            "the local storage cache"));

        error.details() = itemTypeName;
        throw LocalStorageCacheManagerException(error);
    }

    QNTRACE(
        "local_storage",
        "Added " << itemTypeName << " to the local storage cache: " << item);
}

} // namespace

////////////////////////////////////////////////////////////////////////////////

void LocalStorageCacheManagerPrivate::cacheNote(const Note & note)
{
    cacheItem<
        Note, NotesCache, NoteHolder, NoteHolder::ByLocalUid,
        ILocalStorageCacheExpiryChecker>(
        note, QStringLiteral("note"), m_notesCache, m_cacheExpiryChecker.get());
}

void LocalStorageCacheManagerPrivate::cacheNotebook(const Notebook & notebook)
{
    cacheItem<
        Notebook, NotebooksCache, NotebookHolder, NotebookHolder::ByLocalUid,
        ILocalStorageCacheExpiryChecker>(
        notebook, QStringLiteral("notebook"), m_notebooksCache,
        m_cacheExpiryChecker.get());
}

void LocalStorageCacheManagerPrivate::cacheTag(const Tag & tag)
{
    cacheItem<
        Tag, TagsCache, TagHolder, TagHolder::ByLocalUid,
        ILocalStorageCacheExpiryChecker>(
        tag, QStringLiteral("tag"), m_tagsCache, m_cacheExpiryChecker.get());
}

void LocalStorageCacheManagerPrivate::cacheResource(const Resource & resource)
{
    cacheItem<
        Resource, ResourcesCache, ResourceHolder, ResourceHolder::ByLocalUid,
        ILocalStorageCacheExpiryChecker>(
        resource, QStringLiteral("resource"), m_resourcesCache,
        m_cacheExpiryChecker.get());
}

void LocalStorageCacheManagerPrivate::cacheLinkedNotebook(
    const LinkedNotebook & linkedNotebook)
{
    cacheItem<
        LinkedNotebook, LinkedNotebooksCache, LinkedNotebookHolder,
        LinkedNotebookHolder::ByGuid, ILocalStorageCacheExpiryChecker>(
        linkedNotebook, QStringLiteral("linked notebook"),
        m_linkedNotebooksCache, m_cacheExpiryChecker.get());
}

void LocalStorageCacheManagerPrivate::cacheSavedSearch(
    const SavedSearch & savedSearch)
{
    cacheItem<
        SavedSearch, SavedSearchesCache, SavedSearchHolder,
        SavedSearchHolder::ByLocalUid, ILocalStorageCacheExpiryChecker>(
        savedSearch, QStringLiteral("saved search"), m_savedSearchesCache,
        m_cacheExpiryChecker.get());
}

////////////////////////////////////////////////////////////////////////////////

namespace {

template <typename TItem, typename TCache, typename THolder>
void expungeItem(
    const TItem & item, const QString & itemTypeName, TCache & cache)
{
    bool itemHasGuid = item.hasGuid();
    const QString uid = (itemHasGuid ? item.guid() : item.localUid());
    if (itemHasGuid) {
        auto & index = cache.template get<typename THolder::ByGuid>();
        auto it = index.find(uid);
        if (it != index.end()) {
            index.erase(it);
            QNDEBUG(
                "local_storage",
                "Expunged " << itemTypeName
                            << " from the local storage cache: " << item);
        }
    }
    else {
        auto & index = cache.template get<typename THolder::ByLocalUid>();
        auto it = index.find(uid);
        if (it != index.end()) {
            index.erase(it);
            QNDEBUG(
                "local_storage",
                "Expunged " << itemTypeName
                            << " from the local storage cache: " << item);
        }
    }
}

} // namespace

////////////////////////////////////////////////////////////////////////////////

void LocalStorageCacheManagerPrivate::expungeNote(const Note & note)
{
    expungeItem<Note, NotesCache, NoteHolder>(
        note, QStringLiteral("note"), m_notesCache);
}

void LocalStorageCacheManagerPrivate::expungeResource(const Resource & resource)
{
    expungeItem<Resource, ResourcesCache, ResourceHolder>(
        resource, QStringLiteral("resource"), m_resourcesCache);
}

void LocalStorageCacheManagerPrivate::expungeNotebook(const Notebook & notebook)
{
    expungeItem<Notebook, NotebooksCache, NotebookHolder>(
        notebook, QStringLiteral("notebook"), m_notebooksCache);
}

void LocalStorageCacheManagerPrivate::expungeTag(const Tag & tag)
{
    expungeItem<Tag, TagsCache, TagHolder>(
        tag, QStringLiteral("tag"), m_tagsCache);
}

void LocalStorageCacheManagerPrivate::expungeSavedSearch(
    const SavedSearch & search)
{
    expungeItem<SavedSearch, SavedSearchesCache, SavedSearchHolder>(
        search, QStringLiteral("saved search"), m_savedSearchesCache);
}

void LocalStorageCacheManagerPrivate::expungeLinkedNotebook(
    const LinkedNotebook & linkedNotebook)
{
    const QString guid = linkedNotebook.guid();
    auto & index = m_linkedNotebooksCache.get<LinkedNotebookHolder::ByGuid>();
    auto it = index.find(guid);
    if (it != index.end()) {
        index.erase(it);
        QNDEBUG(
            "local_storage",
            "Expunged linked notebook from the local "
                << "storage cache: " << linkedNotebook);
    }
}

////////////////////////////////////////////////////////////////////////////////

namespace {

template <typename TItem, typename TCache, typename TIndex>
const TItem * findItem(const QString & id, const TCache & cache)
{
    const auto & index = cache.template get<TIndex>();
    auto it = index.find(id);
    if (it == index.end()) {
        return nullptr;
    }
    return &(it->m_value);
}

} // namespace

////////////////////////////////////////////////////////////////////////////////

const Note * LocalStorageCacheManagerPrivate::findNoteByLocalUid(
    const QString & localUid) const
{
    return findItem<Note, NotesCache, NoteHolder::ByLocalUid>(
        localUid, m_notesCache);
}

const Note * LocalStorageCacheManagerPrivate::findNoteByGuid(
    const QString & guid) const
{
    return findItem<Note, NotesCache, NoteHolder::ByGuid>(guid, m_notesCache);
}

const Resource * LocalStorageCacheManagerPrivate::findResourceByLocalUid(
    const QString & localUid) const
{
    return findItem<Resource, ResourcesCache, ResourceHolder::ByLocalUid>(
        localUid, m_resourcesCache);
}

const Resource * LocalStorageCacheManagerPrivate::findResourceByGuid(
    const QString & guid) const
{
    return findItem<Resource, ResourcesCache, ResourceHolder::ByGuid>(
        guid, m_resourcesCache);
}

const Notebook * LocalStorageCacheManagerPrivate::findNotebookByLocalUid(
    const QString & localUid) const
{
    return findItem<Notebook, NotebooksCache, NotebookHolder::ByLocalUid>(
        localUid, m_notebooksCache);
}

const Notebook * LocalStorageCacheManagerPrivate::findNotebookByGuid(
    const QString & guid) const
{
    return findItem<Notebook, NotebooksCache, NotebookHolder::ByGuid>(
        guid, m_notebooksCache);
}

const Notebook * LocalStorageCacheManagerPrivate::findNotebookByName(
    const QString & name) const
{
    return findItem<Notebook, NotebooksCache, NotebookHolder::ByName>(
        name, m_notebooksCache);
}

const Tag * LocalStorageCacheManagerPrivate::findTagByLocalUid(
    const QString & localUid) const
{
    return findItem<Tag, TagsCache, TagHolder::ByLocalUid>(
        localUid, m_tagsCache);
}

const Tag * LocalStorageCacheManagerPrivate::findTagByGuid(
    const QString & guid) const
{
    return findItem<Tag, TagsCache, TagHolder::ByGuid>(guid, m_tagsCache);
}

const Tag * LocalStorageCacheManagerPrivate::findTagByName(
    const QString & name) const
{
    return findItem<Tag, TagsCache, TagHolder::ByName>(name, m_tagsCache);
}

const LinkedNotebook *
LocalStorageCacheManagerPrivate::findLinkedNotebookByGuid(
    const QString & guid) const
{
    return findItem<
        LinkedNotebook, LinkedNotebooksCache, LinkedNotebookHolder::ByGuid>(
        guid, m_linkedNotebooksCache);
}

const SavedSearch * LocalStorageCacheManagerPrivate::findSavedSearchByLocalUid(
    const QString & localUid) const
{
    return findItem<
        SavedSearch, SavedSearchesCache, SavedSearchHolder::ByLocalUid>(
        localUid, m_savedSearchesCache);
}

const SavedSearch * LocalStorageCacheManagerPrivate::findSavedSearchByGuid(
    const QString & guid) const
{
    return findItem<SavedSearch, SavedSearchesCache, SavedSearchHolder::ByGuid>(
        guid, m_savedSearchesCache);
}

const SavedSearch * LocalStorageCacheManagerPrivate::findSavedSearchByName(
    const QString & name) const
{
    return findItem<SavedSearch, SavedSearchesCache, SavedSearchHolder::ByName>(
        name, m_savedSearchesCache);
}

void LocalStorageCacheManagerPrivate::clearAllNotes()
{
    m_notesCache.clear();
}

void LocalStorageCacheManagerPrivate::clearAllResources()
{
    m_resourcesCache.clear();
}

void LocalStorageCacheManagerPrivate::clearAllNotebooks()
{
    m_notebooksCache.clear();
}

void LocalStorageCacheManagerPrivate::clearAllTags()
{
    m_tagsCache.clear();
}

void LocalStorageCacheManagerPrivate::clearAllLinkedNotebooks()
{
    m_linkedNotebooksCache.clear();
}

void LocalStorageCacheManagerPrivate::clearAllSavedSearches()
{
    m_savedSearchesCache.clear();
}

void LocalStorageCacheManagerPrivate::installCacheExpiryFunction(
    const ILocalStorageCacheExpiryChecker & checker)
{
    m_cacheExpiryChecker.reset(checker.clone());
}

QTextStream & LocalStorageCacheManagerPrivate::print(QTextStream & strm) const
{
    strm << "LocalStorageCacheManager: {\n";
    strm << "Notes cache: {\n";

    const auto & notesCacheIndex = m_notesCache.get<NoteHolder::ByLocalUid>();
    for (const auto & note: notesCacheIndex) {
        strm << note;
    }

    strm << "}; \n";
    strm << "Resources cache: {\n";

    const auto & resourcesCacheIndex =
        m_resourcesCache.get<ResourceHolder::ByLocalUid>();
    for (const auto & resource: resourcesCacheIndex) {
        strm << resource;
    }

    strm << "}; \n";
    strm << "Notebooks cache: {\n";

    const auto & notebooksCacheIndex =
        m_notebooksCache.get<NotebookHolder::ByLocalUid>();
    for (const auto & notebook: notebooksCacheIndex) {
        strm << notebook;
    }

    strm << "}; \n";
    strm << "Tags cache: {\n";

    const auto & tagsCacheIndex = m_tagsCache.get<TagHolder::ByLocalUid>();
    for (const auto & tag: tagsCacheIndex) {
        strm << tag;
    }

    strm << "}; \n";
    strm << "Linked notebooks cache: {\n";

    const auto & linkedNotebooksCacheIndex =
        m_linkedNotebooksCache.get<LinkedNotebookHolder::ByGuid>();
    for (const auto & linkedNotebook: linkedNotebooksCacheIndex) {
        strm << linkedNotebook;
    }

    strm << "}; \n";
    strm << "Saved searches cache: {\n";

    const auto & savedSearchesCacheIndex =
        m_savedSearchesCache.get<SavedSearchHolder::ByLocalUid>();
    for (const auto & savedSearch: savedSearchesCacheIndex) {
        strm << savedSearch;
    }

    strm << "}; \n";

    if (!m_cacheExpiryChecker) {
        strm << "Cache expiry checker is null! \n";
    }
    else {
        strm << *m_cacheExpiryChecker;
    }

    strm << "}; \n";
    return strm;
}

const QString LocalStorageCacheManagerPrivate::NoteHolder::guid() const
{
    return (m_value.hasGuid() ? m_value.guid() : QString());
}

const QString LocalStorageCacheManagerPrivate::NotebookHolder::guid() const
{
    return (m_value.hasGuid() ? m_value.guid() : QString());
}

const QString LocalStorageCacheManagerPrivate::ResourceHolder::guid() const
{
    return (m_value.hasGuid() ? m_value.guid() : QString());
}

const QString LocalStorageCacheManagerPrivate::TagHolder::guid() const
{
    return (m_value.hasGuid() ? m_value.guid() : QString());
}

const QString LocalStorageCacheManagerPrivate::LinkedNotebookHolder::guid()
    const
{
    return (m_value.hasGuid() ? m_value.guid() : QString());
}

const QString LocalStorageCacheManagerPrivate::SavedSearchHolder::guid() const
{
    return (m_value.hasGuid() ? m_value.guid() : QString());
}

QTextStream & LocalStorageCacheManagerPrivate::NoteHolder::print(
    QTextStream & strm) const
{
    strm << "NoteHolder: note = " << m_value << "last access timestamp = "
         << printableDateTimeFromTimestamp(m_lastAccessTimestamp) << "\n";
    return strm;
}

QTextStream & LocalStorageCacheManagerPrivate::ResourceHolder::print(
    QTextStream & strm) const
{
    strm << "ResourceHolder: resource = " << m_value
         << ", last access timestamp = "
         << printableDateTimeFromTimestamp(m_lastAccessTimestamp) << "\n";
    return strm;
}

QTextStream & LocalStorageCacheManagerPrivate::NotebookHolder::print(
    QTextStream & strm) const
{
    strm << "NotebookHolder: notebook = " << m_value
         << "last access timestamp = "
         << printableDateTimeFromTimestamp(m_lastAccessTimestamp) << "\n";
    return strm;
}

QTextStream & LocalStorageCacheManagerPrivate::TagHolder::print(
    QTextStream & strm) const
{
    strm << "TagHolder: tag = " << m_value << "last access timestamp = "
         << printableDateTimeFromTimestamp(m_lastAccessTimestamp) << "\n";
    return strm;
}

QTextStream & LocalStorageCacheManagerPrivate::LinkedNotebookHolder::print(
    QTextStream & strm) const
{
    strm << "LinkedNotebookHolder: linked notebook = " << m_value
         << "last access timestamp = "
         << printableDateTimeFromTimestamp(m_lastAccessTimestamp) << "\n";
    return strm;
}

QTextStream & LocalStorageCacheManagerPrivate::SavedSearchHolder::print(
    QTextStream & strm) const
{
    strm << "SavedSearchHolder: saved search = " << m_value
         << "last access timestamp = "
         << printableDateTimeFromTimestamp(m_lastAccessTimestamp) << "\n";
    return strm;
}

} // namespace quentier
