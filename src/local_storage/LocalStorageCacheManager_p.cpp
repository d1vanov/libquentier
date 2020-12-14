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
    return m_notesCache.get<NoteHolder::ByLocalId>().size();
}

size_t LocalStorageCacheManagerPrivate::numCachedResources() const
{
    return m_resourcesCache.get<ResourceHolder::ByLocalId>().size();
}

size_t LocalStorageCacheManagerPrivate::numCachedNotebooks() const
{
    return m_notebooksCache.get<NotebookHolder::ByLocalId>().size();
}

size_t LocalStorageCacheManagerPrivate::numCachedTags() const
{
    return m_tagsCache.get<TagHolder::ByLocalId>().size();
}

size_t LocalStorageCacheManagerPrivate::numCachedLinkedNotebooks() const
{
    return m_linkedNotebooksCache.get<LinkedNotebookHolder::ByGuid>().size();
}

size_t LocalStorageCacheManagerPrivate::numCachedSavedSearches() const
{
    return m_savedSearchesCache.get<SavedSearchHolder::ByLocalId>().size();
}

////////////////////////////////////////////////////////////////////////////////

namespace {

////////////////////////////////////////////////////////////////////////////////

template <typename TItem>
QString itemId(const TItem & item)
{
    return item.localId();
}

template <>
QString itemId(const qevercloud::LinkedNotebook & linkedNotebook)
{
    return linkedNotebook.guid() ? *linkedNotebook.guid() : QString();
}

////////////////////////////////////////////////////////////////////////////////

template <typename T>
bool checkExpiry(ILocalStorageCacheExpiryChecker & checker);

template <>
bool checkExpiry<qevercloud::Note>(ILocalStorageCacheExpiryChecker & checker)
{
    return checker.checkNotes();
}

template <>
bool checkExpiry<qevercloud::Notebook>(
    ILocalStorageCacheExpiryChecker & checker)
{
    return checker.checkNotebooks();
}

template <>
bool checkExpiry<qevercloud::Tag>(ILocalStorageCacheExpiryChecker & checker)
{
    return checker.checkTags();
}

template <>
bool checkExpiry<qevercloud::Resource>(
    ILocalStorageCacheExpiryChecker & checker)
{
    return checker.checkResources();
}

template <>
bool checkExpiry<qevercloud::LinkedNotebook>(
    ILocalStorageCacheExpiryChecker & checker)
{
    return checker.checkLinkedNotebooks();
}

template <>
bool checkExpiry<qevercloud::SavedSearch>(
    ILocalStorageCacheExpiryChecker & checker)
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
                    "Going to remove the object from the local storage cache: "
                        << *latIndexBegin);
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

void LocalStorageCacheManagerPrivate::cacheNote(const qevercloud::Note & note)
{
    cacheItem<
        qevercloud::Note, NotesCache, NoteHolder, NoteHolder::ByLocalId,
        ILocalStorageCacheExpiryChecker>(
        note, QStringLiteral("note"), m_notesCache, m_cacheExpiryChecker.get());
}

void LocalStorageCacheManagerPrivate::cacheNotebook(
    const qevercloud::Notebook & notebook)
{
    cacheItem<
        qevercloud::Notebook, NotebooksCache, NotebookHolder,
        NotebookHolder::ByLocalId,
        ILocalStorageCacheExpiryChecker>(
        notebook, QStringLiteral("notebook"), m_notebooksCache,
        m_cacheExpiryChecker.get());
}

void LocalStorageCacheManagerPrivate::cacheTag(const qevercloud::Tag & tag)
{
    cacheItem<
        qevercloud::Tag, TagsCache, TagHolder, TagHolder::ByLocalId,
        ILocalStorageCacheExpiryChecker>(
        tag, QStringLiteral("tag"), m_tagsCache, m_cacheExpiryChecker.get());
}

void LocalStorageCacheManagerPrivate::cacheResource(
    const qevercloud::Resource & resource)
{
    cacheItem<
        qevercloud::Resource, ResourcesCache, ResourceHolder,
        ResourceHolder::ByLocalId,
        ILocalStorageCacheExpiryChecker>(
        resource, QStringLiteral("resource"), m_resourcesCache,
        m_cacheExpiryChecker.get());
}

void LocalStorageCacheManagerPrivate::cacheLinkedNotebook(
    const qevercloud::LinkedNotebook & linkedNotebook)
{
    cacheItem<
        qevercloud::LinkedNotebook, LinkedNotebooksCache, LinkedNotebookHolder,
        LinkedNotebookHolder::ByGuid, ILocalStorageCacheExpiryChecker>(
        linkedNotebook, QStringLiteral("linked notebook"),
        m_linkedNotebooksCache, m_cacheExpiryChecker.get());
}

void LocalStorageCacheManagerPrivate::cacheSavedSearch(
    const qevercloud::SavedSearch & savedSearch)
{
    cacheItem<
        qevercloud::SavedSearch, SavedSearchesCache, SavedSearchHolder,
        SavedSearchHolder::ByLocalId, ILocalStorageCacheExpiryChecker>(
        savedSearch, QStringLiteral("saved search"), m_savedSearchesCache,
        m_cacheExpiryChecker.get());
}

////////////////////////////////////////////////////////////////////////////////

namespace {

template <typename TItem, typename TCache, typename THolder>
void expungeItem(
    const TItem & item, const QString & itemTypeName, TCache & cache)
{
    bool itemHasGuid = (item.guid() != std::nullopt);
    const QString uid = (itemHasGuid ? *item.guid() : item.localId());
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
        auto & index = cache.template get<typename THolder::ByLocalId>();
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

void LocalStorageCacheManagerPrivate::expungeNote(const qevercloud::Note & note)
{
    expungeItem<qevercloud::Note, NotesCache, NoteHolder>(
        note, QStringLiteral("note"), m_notesCache);
}

void LocalStorageCacheManagerPrivate::expungeResource(
    const qevercloud::Resource & resource)
{
    expungeItem<qevercloud::Resource, ResourcesCache, ResourceHolder>(
        resource, QStringLiteral("resource"), m_resourcesCache);
}

void LocalStorageCacheManagerPrivate::expungeNotebook(
    const qevercloud::Notebook & notebook)
{
    expungeItem<qevercloud::Notebook, NotebooksCache, NotebookHolder>(
        notebook, QStringLiteral("notebook"), m_notebooksCache);
}

void LocalStorageCacheManagerPrivate::expungeTag(const qevercloud::Tag & tag)
{
    expungeItem<qevercloud::Tag, TagsCache, TagHolder>(
        tag, QStringLiteral("tag"), m_tagsCache);
}

void LocalStorageCacheManagerPrivate::expungeSavedSearch(
    const qevercloud::SavedSearch & search)
{
    expungeItem<qevercloud::SavedSearch, SavedSearchesCache, SavedSearchHolder>(
        search, QStringLiteral("saved search"), m_savedSearchesCache);
}

void LocalStorageCacheManagerPrivate::expungeLinkedNotebook(
    const qevercloud::LinkedNotebook & linkedNotebook)
{
    if (!linkedNotebook.guid()) {
        return;
    }

    const QString guid = *linkedNotebook.guid();
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

const qevercloud::Note * LocalStorageCacheManagerPrivate::findNoteByLocalUid(
    const QString & localUid) const
{
    return findItem<qevercloud::Note, NotesCache, NoteHolder::ByLocalId>(
        localUid, m_notesCache);
}

const qevercloud::Note * LocalStorageCacheManagerPrivate::findNoteByGuid(
    const QString & guid) const
{
    return findItem<qevercloud::Note, NotesCache, NoteHolder::ByGuid>(
        guid, m_notesCache);
}

const qevercloud::Resource * LocalStorageCacheManagerPrivate::findResourceByLocalUid(
    const QString & localUid) const
{
    return findItem<qevercloud::Resource, ResourcesCache, ResourceHolder::ByLocalId>(
        localUid, m_resourcesCache);
}

const qevercloud::Resource * LocalStorageCacheManagerPrivate::findResourceByGuid(
    const QString & guid) const
{
    return findItem<qevercloud::Resource, ResourcesCache, ResourceHolder::ByGuid>(
        guid, m_resourcesCache);
}

const qevercloud::Notebook * LocalStorageCacheManagerPrivate::findNotebookByLocalUid(
    const QString & localUid) const
{
    return findItem<qevercloud::Notebook, NotebooksCache, NotebookHolder::ByLocalId>(
        localUid, m_notebooksCache);
}

const qevercloud::Notebook * LocalStorageCacheManagerPrivate::findNotebookByGuid(
    const QString & guid) const
{
    return findItem<qevercloud::Notebook, NotebooksCache, NotebookHolder::ByGuid>(
        guid, m_notebooksCache);
}

const qevercloud::Notebook * LocalStorageCacheManagerPrivate::findNotebookByName(
    const QString & name) const
{
    return findItem<qevercloud::Notebook, NotebooksCache, NotebookHolder::ByName>(
        name, m_notebooksCache);
}

const qevercloud::Tag * LocalStorageCacheManagerPrivate::findTagByLocalUid(
    const QString & localUid) const
{
    return findItem<qevercloud::Tag, TagsCache, TagHolder::ByLocalId>(
        localUid, m_tagsCache);
}

const qevercloud::Tag * LocalStorageCacheManagerPrivate::findTagByGuid(
    const QString & guid) const
{
    return findItem<qevercloud::Tag, TagsCache, TagHolder::ByGuid>(guid, m_tagsCache);
}

const qevercloud::Tag * LocalStorageCacheManagerPrivate::findTagByName(
    const QString & name) const
{
    return findItem<qevercloud::Tag, TagsCache, TagHolder::ByName>(name, m_tagsCache);
}

const qevercloud::LinkedNotebook *
LocalStorageCacheManagerPrivate::findLinkedNotebookByGuid(
    const QString & guid) const
{
    return findItem<
        qevercloud::LinkedNotebook, LinkedNotebooksCache, LinkedNotebookHolder::ByGuid>(
        guid, m_linkedNotebooksCache);
}

const qevercloud::SavedSearch * LocalStorageCacheManagerPrivate::findSavedSearchByLocalUid(
    const QString & localUid) const
{
    return findItem<
        qevercloud::SavedSearch, SavedSearchesCache, SavedSearchHolder::ByLocalId>(
        localUid, m_savedSearchesCache);
}

const qevercloud::SavedSearch * LocalStorageCacheManagerPrivate::findSavedSearchByGuid(
    const QString & guid) const
{
    return findItem<qevercloud::SavedSearch, SavedSearchesCache, SavedSearchHolder::ByGuid>(
        guid, m_savedSearchesCache);
}

const qevercloud::SavedSearch * LocalStorageCacheManagerPrivate::findSavedSearchByName(
    const QString & name) const
{
    return findItem<qevercloud::SavedSearch, SavedSearchesCache, SavedSearchHolder::ByName>(
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

    const auto & notesCacheIndex = m_notesCache.get<NoteHolder::ByLocalId>();
    for (const auto & note: notesCacheIndex) {
        strm << note;
    }

    strm << "}; \n";
    strm << "Resources cache: {\n";

    const auto & resourcesCacheIndex =
        m_resourcesCache.get<ResourceHolder::ByLocalId>();
    for (const auto & resource: resourcesCacheIndex) {
        strm << resource;
    }

    strm << "}; \n";
    strm << "Notebooks cache: {\n";

    const auto & notebooksCacheIndex =
        m_notebooksCache.get<NotebookHolder::ByLocalId>();
    for (const auto & notebook: notebooksCacheIndex) {
        strm << notebook;
    }

    strm << "}; \n";
    strm << "Tags cache: {\n";

    const auto & tagsCacheIndex = m_tagsCache.get<TagHolder::ByLocalId>();
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
        m_savedSearchesCache.get<SavedSearchHolder::ByLocalId>();
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

QString LocalStorageCacheManagerPrivate::NoteHolder::guid() const
{
    return (m_value.guid() ? *m_value.guid() : QString());
}

QString LocalStorageCacheManagerPrivate::NotebookHolder::guid() const
{
    return (m_value.guid() ? *m_value.guid() : QString());
}

QString LocalStorageCacheManagerPrivate::ResourceHolder::guid() const
{
    return (m_value.guid() ? *m_value.guid() : QString());
}

QString LocalStorageCacheManagerPrivate::TagHolder::guid() const
{
    return (m_value.guid() ? *m_value.guid() : QString());
}

QString LocalStorageCacheManagerPrivate::LinkedNotebookHolder::guid() const
{
    return (m_value.guid() ? *m_value.guid() : QString());
}

QString LocalStorageCacheManagerPrivate::SavedSearchHolder::guid() const
{
    return (m_value.guid() ? *m_value.guid() : QString());
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
