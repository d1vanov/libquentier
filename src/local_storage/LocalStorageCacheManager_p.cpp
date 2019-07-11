/*
 * Copyright 2016-2019 Dmitry Ivanov
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
#include <quentier/local_storage/DefaultLocalStorageCacheExpiryChecker.h>
#include <quentier/exception/LocalStorageCacheManagerException.h>
#include <quentier/utility/QuentierCheckPtr.h>
#include <quentier/utility/Utility.h>
#include <quentier/logging/QuentierLogger.h>
#include <QDateTime>

namespace quentier {

LocalStorageCacheManagerPrivate::LocalStorageCacheManagerPrivate(
        LocalStorageCacheManager & q) :
    q_ptr(&q),
    m_cacheExpiryChecker(new DefaultLocalStorageCacheExpiryChecker(q)),
    m_notesCache(),
    m_resourcesCache(),
    m_notebooksCache(),
    m_tagsCache(),
    m_linkedNotebooksCache(),
    m_savedSearchesCache()
{}

LocalStorageCacheManagerPrivate::~LocalStorageCacheManagerPrivate()
{}

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
    return (m_notesCache.empty() &&
            m_resourcesCache.empty() &&
            m_notebooksCache.empty() &&
            m_tagsCache.empty() &&
            m_linkedNotebooksCache.empty() &&
            m_savedSearchesCache.empty());
}

#define NUM_CACHED_OBJECTS(type, method_name, cache_name, IndexType)           \
size_t LocalStorageCacheManagerPrivate::method_name() const                    \
{                                                                              \
    const auto & index = cache_name.get<type##Holder::IndexType>();            \
    return index.size();                                                       \
}                                                                              \
// NUM_CACHED_OBJECTS

NUM_CACHED_OBJECTS(Note, numCachedNotes, m_notesCache, ByLocalUid)
NUM_CACHED_OBJECTS(Resource, numCachesResources, m_resourcesCache, ByLocalUid)
NUM_CACHED_OBJECTS(Notebook, numCachedNotebooks, m_notebooksCache, ByLocalUid)
NUM_CACHED_OBJECTS(Tag, numCachedTags, m_tagsCache, ByLocalUid)
NUM_CACHED_OBJECTS(LinkedNotebook, numCachedLinkedNotebooks,
                   m_linkedNotebooksCache, ByGuid)
NUM_CACHED_OBJECTS(SavedSearch, numCachedSavedSearches,
                   m_savedSearchesCache, ByLocalUid)

#undef NUM_CACHED_OBJECTS

#define CACHE_OBJECT(Type, name, cache_type, cache_name, expiry_checker,            \
                     IndexType, IndexAccessor)                                      \
void LocalStorageCacheManagerPrivate::cache##Type(const Type & name)                \
{                                                                                   \
    QUENTIER_CHECK_PTR(m_cacheExpiryChecker.data());                                \
    auto & latIndex =                                                               \
        cache_name.get<Type##Holder::ByLastAccessTimestamp>();                      \
    if (Q_LIKELY(!m_cacheExpiryChecker.isNull()))                                   \
    {                                                                               \
        bool res = false;                                                           \
        while(!res && !latIndex.empty())                                            \
        {                                                                           \
            res = m_cacheExpiryChecker->expiry_checker();                           \
            if (Q_UNLIKELY(!res)) {                                                 \
                auto latIndexBegin = latIndex.begin();                              \
                QNTRACE("Going to remove the object from the local "                \
                        << "storage cache: " << *latIndexBegin);                    \
                Q_UNUSED(latIndex.erase(latIndexBegin));                            \
                continue;                                                           \
            }                                                                       \
        }                                                                           \
    }                                                                               \
    Type##Holder name##Holder;                                                      \
    name##Holder.m_##name = name;                                                   \
    name##Holder.m_lastAccessTimestamp = QDateTime::currentMSecsSinceEpoch();       \
    /* See whether the item is already in the cache */                              \
    auto & uniqueIndex = cache_name.get<Type##Holder::IndexType>();                 \
    auto it = uniqueIndex.find(name.IndexAccessor());                               \
    if (it != uniqueIndex.end()) {                                                  \
        uniqueIndex.replace(it, name##Holder);                                      \
        QNTRACE("Updated " #name " in the local storage cache: " << name);          \
        return;                                                                     \
    }                                                                               \
    /* If got here, no existing item was found in the cache */                      \
    auto insertionResult = cache_name.insert(name##Holder);                         \
    if (Q_UNLIKELY(!insertionResult.second))                                        \
    {                                                                               \
        QNWARNING("Failed to insert " #name " into the cache of "                   \
                  << "local storage manager: " << name);                            \
        ErrorString error(QT_TRANSLATE_NOOP("LocalStorageCacheManagerPrivate",      \
                                            "Unable to insert the data item into "  \
                                            "the local storage cache"));            \
        error.details() = QStringLiteral( #name );                                  \
        throw LocalStorageCacheManagerException(error);                             \
    }                                                                               \
    QNTRACE("Added " #name " to the local storage cache: " << name);                \
}                                                                                   \
// CACHE_OBJECT

CACHE_OBJECT(Note, note, NotesCache, m_notesCache, checkNotes, ByLocalUid, localUid)
CACHE_OBJECT(Resource, resource, ResourcesCache, m_resourcesCache,
             checkResources, ByLocalUid, localUid)
CACHE_OBJECT(Notebook, notebook, NotebooksCache, m_notebooksCache,
             checkNotebooks, ByLocalUid, localUid)
CACHE_OBJECT(Tag, tag, TagsCache, m_tagsCache, checkTags, ByLocalUid, localUid)
CACHE_OBJECT(LinkedNotebook, linkedNotebook, LinkedNotebooksCache,
             m_linkedNotebooksCache, checkLinkedNotebooks, ByGuid, guid)
CACHE_OBJECT(SavedSearch, savedSearch, SavedSearchesCache, m_savedSearchesCache,
             checkSavedSearches, ByLocalUid, localUid)

#undef CACHE_OBJECT

#define EXPUNGE_OBJECT(Type, name, cache_type, cache_name)                     \
void LocalStorageCacheManagerPrivate::expunge##Type(const Type & name)         \
{                                                                              \
    bool name##HasGuid = name.hasGuid();                                       \
    const QString uid = (name##HasGuid ? name.guid() : name.localUid());       \
    if (name##HasGuid)                                                         \
    {                                                                          \
        auto & index = cache_name.get<Type##Holder::ByGuid>();                 \
        auto it = index.find(uid);                                             \
        if (it != index.end()) {                                               \
            index.erase(it);                                                   \
            QNDEBUG("Expunged " #name " from the local storage cache: "        \
                    << name);                                                  \
        }                                                                      \
    }                                                                          \
    else                                                                       \
    {                                                                          \
        auto & index = cache_name.get<Type##Holder::ByLocalUid>();             \
        auto it = index.find(uid);                                             \
        if (it != index.end()) {                                               \
            index.erase(it);                                                   \
            QNDEBUG("Expunged " #name " from the local storage cache: "        \
                    << name);                                                  \
        }                                                                      \
    }                                                                          \
}                                                                              \
// EXPUNGE_OBJECT

EXPUNGE_OBJECT(Note, note, NotesCache, m_notesCache)
EXPUNGE_OBJECT(Resource, resource, ResourcesCache, m_resourcesCache)
EXPUNGE_OBJECT(Notebook, notebook, NotebooksCache, m_notebooksCache)
EXPUNGE_OBJECT(Tag, tag, TagsCache, m_tagsCache)
EXPUNGE_OBJECT(SavedSearch, savedSearch, SavedSearchesCache, m_savedSearchesCache)

#undef EXPUNGE_OBJECT

void LocalStorageCacheManagerPrivate::expungeLinkedNotebook(
    const LinkedNotebook & linkedNotebook)
{
    const QString guid = linkedNotebook.guid();
    auto & index = m_linkedNotebooksCache.get<LinkedNotebookHolder::ByGuid>();
    auto it = index.find(guid);
    if (it != index.end()) {
        index.erase(it);
        QNDEBUG("Expunged linked notebook from the local storage cache: "
                << linkedNotebook);
    }
}

#define FIND_OBJECT(Type, name, tag, cache_name)                               \
const Type * LocalStorageCacheManagerPrivate::find##Type##tag(                 \
    const QString & guid) const                                                \
{                                                                              \
    const auto & index = cache_name.get<Type##Holder::tag>();                  \
    auto it = index.find(guid);                                                \
    if (it == index.end()) {                                                   \
        return Q_NULLPTR;                                                      \
    }                                                                          \
    return &(it->m_##name);                                                    \
}                                                                              \
// FIND_OBJECT

FIND_OBJECT(Note, note, ByLocalUid, m_notesCache)
FIND_OBJECT(Note, note, ByGuid, m_notesCache)
FIND_OBJECT(Resource, resource, ByLocalUid, m_resourcesCache)
FIND_OBJECT(Resource, resource, ByGuid, m_resourcesCache)
FIND_OBJECT(Notebook, notebook, ByLocalUid, m_notebooksCache)
FIND_OBJECT(Notebook, notebook, ByGuid, m_notebooksCache)
FIND_OBJECT(Notebook, notebook, ByName, m_notebooksCache)
FIND_OBJECT(Tag, tag, ByLocalUid, m_tagsCache)
FIND_OBJECT(Tag, tag, ByGuid, m_tagsCache)
FIND_OBJECT(Tag, tag, ByName, m_tagsCache)
FIND_OBJECT(LinkedNotebook, linkedNotebook, ByGuid, m_linkedNotebooksCache)
FIND_OBJECT(SavedSearch, savedSearch, ByLocalUid, m_savedSearchesCache)
FIND_OBJECT(SavedSearch, savedSearch, ByGuid, m_savedSearchesCache)
FIND_OBJECT(SavedSearch, savedSearch, ByName, m_savedSearchesCache)

#undef FIND_OBJECT

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
    auto notesCacheEnd = notesCacheIndex.end();
    for(auto it = notesCacheIndex.begin(); it != notesCacheEnd; ++it) {
        strm << *it;
    }

    strm << "}; \n";
    strm << "Resources cache: {\n";

    const auto & resourcesCacheIndex =
        m_resourcesCache.get<ResourceHolder::ByLocalUid>();
    auto resourcesCacheEnd = resourcesCacheIndex.end();
    for(auto it = resourcesCacheIndex.begin(); it != resourcesCacheEnd; ++it)
    {
        strm << *it;
    }

    strm << "}; \n";
    strm << "Notebooks cache: {\n";

    const auto & notebooksCacheIndex =
        m_notebooksCache.get<NotebookHolder::ByLocalUid>();
    auto notebooksCacheEnd = notebooksCacheIndex.end();
    for(auto it = notebooksCacheIndex.begin();
        it != notebooksCacheEnd; ++it)
    {
        strm << *it;
    }

    strm << "}; \n";
    strm << "Tags cache: {\n";

    const auto & tagsCacheIndex = m_tagsCache.get<TagHolder::ByLocalUid>();
    auto tagsCacheEnd = tagsCacheIndex.end();
    for(auto it = tagsCacheIndex.begin(); it != tagsCacheEnd; ++it) {
        strm << *it;
    }

    strm << "}; \n";
    strm << "Linked notebooks cache: {\n";

    const auto & linkedNotebooksCacheIndex =
        m_linkedNotebooksCache.get<LinkedNotebookHolder::ByGuid>();
    auto linkedNotebooksCacheEnd = linkedNotebooksCacheIndex.end();
    for(auto it = linkedNotebooksCacheIndex.begin();
        it != linkedNotebooksCacheEnd; ++it)
    {
        strm << *it;
    }

    strm << "}; \n";
    strm << "Saved searches cache: {\n";

    const auto & savedSearchesCacheIndex =
        m_savedSearchesCache.get<SavedSearchHolder::ByLocalUid>();
    auto savedSearchesCacheEnd = savedSearchesCacheIndex.end();
    for(auto it = savedSearchesCacheIndex.begin();
        it != savedSearchesCacheEnd; ++it)
    {
        strm << *it;
    }

    strm << "}; \n";

    if (m_cacheExpiryChecker.isNull()) {
        strm << "Cache expiry checker is null! \n";
    }
    else {
        strm << *m_cacheExpiryChecker;
    }

    strm << "}; \n";
    return strm;
}

#define GET_GUID(Type, name)                                                   \
const QString LocalStorageCacheManagerPrivate::Type##Holder::guid() const      \
{                                                                              \
    /* NOTE: This precaution is required for storage of local notes in the */  \
    /* cache */                                                                \
    if (m_##name.hasGuid()) {                                                  \
        return m_##name.guid();                                                \
    }                                                                          \
    else {                                                                     \
        return QString();                                                      \
    }                                                                          \
}                                                                              \
// GET_GUID

GET_GUID(Note, note)
GET_GUID(Resource, resource)
GET_GUID(Notebook, notebook)
GET_GUID(Tag, tag)
GET_GUID(LinkedNotebook, linkedNotebook)
GET_GUID(SavedSearch, savedSearch)

#undef GET_GUID

QTextStream & LocalStorageCacheManagerPrivate::NoteHolder::print(
    QTextStream & strm) const
{
    strm << "NoteHolder: note = " << m_note << "last access timestamp = "
         << printableDateTimeFromTimestamp(m_lastAccessTimestamp) << "\n";
    return strm;
}

QTextStream & LocalStorageCacheManagerPrivate::ResourceHolder::print(
    QTextStream & strm) const
{
    strm << "ResourceHolder: resource = " << m_resource
         << ", last access timestamp = "
         << printableDateTimeFromTimestamp(m_lastAccessTimestamp) << "\n";
    return strm;
}

QTextStream & LocalStorageCacheManagerPrivate::NotebookHolder::print(
    QTextStream & strm) const
{
    strm << "NotebookHolder: notebook = " << m_notebook
         << "last access timestamp = "
         << printableDateTimeFromTimestamp(m_lastAccessTimestamp) << "\n";
    return strm;
}

QTextStream & LocalStorageCacheManagerPrivate::TagHolder::print(
    QTextStream & strm) const
{
    strm << "TagHolder: tag = " << m_tag << "last access timestamp = "
         << printableDateTimeFromTimestamp(m_lastAccessTimestamp) << "\n";
    return strm;
}

QTextStream & LocalStorageCacheManagerPrivate::LinkedNotebookHolder::print(
    QTextStream & strm) const
{
    strm << "LinkedNotebookHolder: linked notebook = "
         << m_linkedNotebook << "last access timestamp = "
         << printableDateTimeFromTimestamp(m_lastAccessTimestamp) << "\n";
    return strm;
}

QTextStream & LocalStorageCacheManagerPrivate::SavedSearchHolder::print(
    QTextStream & strm) const
{
    strm << "SavedSearchHolder: saved search = " << m_savedSearch
         << "last access timestamp = "
         << printableDateTimeFromTimestamp(m_lastAccessTimestamp) << "\n";
    return strm;
}

LocalStorageCacheManagerPrivate::NoteHolder &
LocalStorageCacheManagerPrivate::NoteHolder::operator=(
    const LocalStorageCacheManagerPrivate::NoteHolder & other)
{
    if (this != &other) {
        m_note = other.m_note;
        m_lastAccessTimestamp = other.m_lastAccessTimestamp;
    }

    return *this;
}

LocalStorageCacheManagerPrivate::ResourceHolder &
LocalStorageCacheManagerPrivate::ResourceHolder::operator=(
    const LocalStorageCacheManagerPrivate::ResourceHolder & other)
{
    if (this != &other) {
        m_resource = other.m_resource;
        m_lastAccessTimestamp = other.m_lastAccessTimestamp;
    }

    return *this;
}

LocalStorageCacheManagerPrivate::NotebookHolder &
LocalStorageCacheManagerPrivate::NotebookHolder::operator=(
    const LocalStorageCacheManagerPrivate::NotebookHolder & other)
{
    if (this != &other) {
        m_notebook = other.m_notebook;
        m_lastAccessTimestamp = other.m_lastAccessTimestamp;
    }

    return *this;
}

LocalStorageCacheManagerPrivate::TagHolder &
LocalStorageCacheManagerPrivate::TagHolder::operator=(
    const LocalStorageCacheManagerPrivate::TagHolder & other)
{
    if (this != &other) {
        m_tag = other.m_tag;
        m_lastAccessTimestamp = other.m_lastAccessTimestamp;
    }

    return *this;
}

LocalStorageCacheManagerPrivate::LinkedNotebookHolder &
LocalStorageCacheManagerPrivate::LinkedNotebookHolder::operator=(
    const LocalStorageCacheManagerPrivate::LinkedNotebookHolder & other)
{
    if (this != &other) {
        m_linkedNotebook = other.m_linkedNotebook;
        m_lastAccessTimestamp = other.m_lastAccessTimestamp;
    }

    return *this;
}

LocalStorageCacheManagerPrivate::SavedSearchHolder &
LocalStorageCacheManagerPrivate::SavedSearchHolder::operator=(
    const LocalStorageCacheManagerPrivate::SavedSearchHolder & other)
{
    if (this != &other) {
        m_savedSearch = other.m_savedSearch;
        m_lastAccessTimestamp = other.m_lastAccessTimestamp;
    }

    return *this;
}

} // namespace quentier
