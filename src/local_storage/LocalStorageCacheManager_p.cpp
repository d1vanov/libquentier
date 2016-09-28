/*
 * Copyright 2016 Dmitry Ivanov
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
#include <quentier/logging/QuentierLogger.h>
#include <QDateTime>

namespace quentier {

LocalStorageCacheManagerPrivate::LocalStorageCacheManagerPrivate(LocalStorageCacheManager & q) :
    q_ptr(&q),
    m_cacheExpiryChecker(new DefaultLocalStorageCacheExpiryChecker(q)),
    m_notesCache(),
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
    m_notebooksCache.clear();
    m_tagsCache.clear();
    m_linkedNotebooksCache.clear();
    m_savedSearchesCache.clear();
}

bool LocalStorageCacheManagerPrivate::empty() const
{
    return (m_notesCache.empty() &&
            m_notebooksCache.empty() &&
            m_tagsCache.empty() &&
            m_linkedNotebooksCache.empty() &&
            m_savedSearchesCache.empty());
}

#define NUM_CACHED_OBJECTS(type, method_name, cache_name, IndexType) \
size_t LocalStorageCacheManagerPrivate::method_name() const \
{ \
    const auto & index = cache_name.get<type##Holder::IndexType>(); \
    return index.size(); \
}

NUM_CACHED_OBJECTS(Note, numCachedNotes, m_notesCache, ByLocalUid)
NUM_CACHED_OBJECTS(Notebook, numCachedNotebooks, m_notebooksCache, ByLocalUid)
NUM_CACHED_OBJECTS(Tag, numCachedTags, m_tagsCache, ByLocalUid)
NUM_CACHED_OBJECTS(LinkedNotebook, numCachedLinkedNotebooks, m_linkedNotebooksCache, ByGuid)
NUM_CACHED_OBJECTS(SavedSearch, numCachedSavedSearches, m_savedSearchesCache, ByLocalUid)

#undef NUM_CACHED_OBJECTS

#define CACHE_OBJECT(Type, name, cache_type, cache_name, expiry_checker, IndexType, IndexAccessor) \
void LocalStorageCacheManagerPrivate::cache##Type(const Type & name) \
{ \
    QUENTIER_CHECK_PTR(m_cacheExpiryChecker.data()); \
    \
    typedef boost::multi_index::index<cache_type,Type##Holder::ByLastAccessTimestamp>::type LastAccessTimestampIndex; \
    LastAccessTimestampIndex & latIndex = cache_name.get<Type##Holder::ByLastAccessTimestamp>(); \
    \
    if (Q_LIKELY(!m_cacheExpiryChecker.isNull())) \
    {   \
        bool res = false;   \
        while(!res && !latIndex.empty()) \
        { \
            res = m_cacheExpiryChecker->expiry_checker(); \
            if (Q_UNLIKELY(!res)) { \
                auto latIndexBegin = latIndex.begin(); \
                QNDEBUG(QStringLiteral("Going to remove the object from local storage cache: ") << *latIndexBegin); \
                Q_UNUSED(latIndex.erase(latIndexBegin)); \
                continue; \
            } \
        } \
    } \
    \
    Type##Holder name##Holder; \
    name##Holder.m_##name = name; \
    name##Holder.m_lastAccessTimestamp = QDateTime::currentMSecsSinceEpoch(); \
    \
    /* See whether the item is already in the cache */ \
    typedef boost::multi_index::index<cache_type,Type##Holder::IndexType>::type Index; \
    Index & uniqueIndex = cache_name.get<Type##Holder::IndexType>(); \
    Index::iterator it = uniqueIndex.find(name.IndexAccessor()); \
    if (it != uniqueIndex.end()) { \
        uniqueIndex.replace(it, name##Holder); \
        QNDEBUG(QStringLiteral("Updated " #name " in the local storage cache: ") << name); \
        return; \
    } \
    \
    /* If got here, no existing item was found in the cache */ \
    auto insertionResult = cache_name.insert(name##Holder); \
    if (Q_UNLIKELY(!insertionResult.second)) { \
        QNWARNING(QStringLiteral("Failed to insert " #name " into the cache of local storage manager: ") << name); \
        throw LocalStorageCacheManagerException("Unable to insert " #name " into the local storage cache"); \
    } \
    \
    QNDEBUG(QStringLiteral("Added " #name " to the local storage cache: ") << name); \
}

CACHE_OBJECT(Note, note, NotesCache, m_notesCache, checkNotes, ByLocalUid, localUid)
CACHE_OBJECT(Notebook, notebook, NotebooksCache, m_notebooksCache, checkNotebooks, ByLocalUid, localUid)
CACHE_OBJECT(Tag, tag, TagsCache, m_tagsCache, checkTags, ByLocalUid, localUid)
CACHE_OBJECT(LinkedNotebook, linkedNotebook, LinkedNotebooksCache, m_linkedNotebooksCache,
             checkLinkedNotebooks, ByGuid, guid)
CACHE_OBJECT(SavedSearch, savedSearch, SavedSearchesCache, m_savedSearchesCache,
             checkSavedSearches, ByLocalUid, localUid)

#undef CACHE_OBJECT

#define EXPUNGE_OBJECT(Type, name, cache_type, cache_name) \
void LocalStorageCacheManagerPrivate::expunge##Type(const Type & name) \
{ \
    bool name##HasGuid = name.hasGuid(); \
    const QString uid = (name##HasGuid ? name.guid() : name.localUid()); \
    \
    if (name##HasGuid) \
    { \
        typedef boost::multi_index::index<cache_type,Type##Holder::ByGuid>::type UidIndex; \
        UidIndex & index = cache_name.get<Type##Holder::ByGuid>(); \
        UidIndex::iterator it = index.find(uid); \
        if (it != index.end()) { \
            index.erase(it); \
            QNDEBUG(QStringLiteral("Expunged " #name " from the local storage cache: ") << name); \
        } \
    } \
    else \
    { \
        typedef boost::multi_index::index<cache_type,Type##Holder::ByLocalUid>::type UidIndex; \
        UidIndex & index = cache_name.get<Type##Holder::ByLocalUid>(); \
        UidIndex::iterator it = index.find(uid); \
        if (it != index.end()) { \
            index.erase(it); \
            QNDEBUG(QStringLiteral("Expunged " #name " from the local storage cache: ") << name); \
        } \
    } \
}

EXPUNGE_OBJECT(Note, note, NotesCache, m_notesCache)
EXPUNGE_OBJECT(Notebook, notebook, NotebooksCache, m_notebooksCache)
EXPUNGE_OBJECT(Tag, tag, TagsCache, m_tagsCache)
EXPUNGE_OBJECT(SavedSearch, savedSearch, SavedSearchesCache, m_savedSearchesCache)

#undef EXPUNGE_OBJECT

void LocalStorageCacheManagerPrivate::expungeLinkedNotebook(const LinkedNotebook & linkedNotebook)
{
    const QString guid = linkedNotebook.guid();

    typedef boost::multi_index::index<LinkedNotebooksCache,LinkedNotebookHolder::ByGuid>::type GuidIndex;
    GuidIndex & index = m_linkedNotebooksCache.get<LinkedNotebookHolder::ByGuid>();
    GuidIndex::iterator it = index.find(guid);
    if (it != index.end()) {
        index.erase(it);
        QNDEBUG(QStringLiteral("Expunged linked notebook from the local storage cache: ") << linkedNotebook);
    }
}

#define FIND_OBJECT(Type, name, tag, cache_name) \
const Type * LocalStorageCacheManagerPrivate::find##Type##tag(const QString & guid) const \
{ \
    const auto & index = cache_name.get<Type##Holder::tag>(); \
    auto it = index.find(guid); \
    if (it == index.end()) { \
        return Q_NULLPTR; \
    } \
    \
    return &(it->m_##name); \
}

FIND_OBJECT(Note, note, ByLocalUid, m_notesCache)
FIND_OBJECT(Note, note, ByGuid, m_notesCache)
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

void LocalStorageCacheManagerPrivate::installCacheExpiryFunction(const ILocalStorageCacheExpiryChecker & checker)
{
    m_cacheExpiryChecker.reset(checker.clone());
}

QTextStream & LocalStorageCacheManagerPrivate::print(QTextStream & strm) const
{
    strm << QStringLiteral("LocalStorageCacheManager: {\n");
    strm << QStringLiteral("Notes cache: {\n");

    const NotesCache::index<NoteHolder::ByLocalUid>::type & notesCacheIndex = m_notesCache.get<NoteHolder::ByLocalUid>();
    typedef NotesCache::index<NoteHolder::ByLocalUid>::type::const_iterator NotesConstIter;
    NotesConstIter notesCacheEnd = notesCacheIndex.end();
    for(NotesConstIter it = notesCacheIndex.begin(); it != notesCacheEnd; ++it) {
        strm << *it;
    }

    strm << QStringLiteral("}; \n");
    strm << QStringLiteral("Notebooks cache: {\n");

    const NotebooksCache::index<NotebookHolder::ByLocalUid>::type & notebooksCacheIndex = m_notebooksCache.get<NotebookHolder::ByLocalUid>();
    typedef NotebooksCache::index<NotebookHolder::ByLocalUid>::type::const_iterator NotebooksConstIter;
    NotebooksConstIter notebooksCacheEnd = notebooksCacheIndex.end();
    for(NotebooksConstIter it = notebooksCacheIndex.begin(); it != notebooksCacheEnd; ++it) {
        strm << *it;
    }

    strm << QStringLiteral("}; \n");
    strm << QStringLiteral("Tags cache: {\n");

    const TagsCache::index<TagHolder::ByLocalUid>::type & tagsCacheIndex = m_tagsCache.get<TagHolder::ByLocalUid>();
    typedef TagsCache::index<TagHolder::ByLocalUid>::type::const_iterator TagsConstIter;
    TagsConstIter tagsCacheEnd = tagsCacheIndex.end();
    for(TagsConstIter it = tagsCacheIndex.begin(); it != tagsCacheEnd; ++it) {
        strm << *it;
    }

    strm << QStringLiteral("}; \n");
    strm << QStringLiteral("Linked notebooks cache: {\n");

    const LinkedNotebooksCache::index<LinkedNotebookHolder::ByGuid>::type & linkedNotebooksCacheIndex = m_linkedNotebooksCache.get<LinkedNotebookHolder::ByGuid>();
    typedef LinkedNotebooksCache::index<LinkedNotebookHolder::ByGuid>::type::const_iterator LinkedNotebooksConstIter;
    LinkedNotebooksConstIter linkedNotebooksCacheEnd = linkedNotebooksCacheIndex.end();
    for(LinkedNotebooksConstIter it = linkedNotebooksCacheIndex.begin(); it != linkedNotebooksCacheEnd; ++it) {
        strm << *it;
    }

    strm << QStringLiteral("}; \n");
    strm << QStringLiteral("Saved searches cache: {\n");

    const SavedSearchesCache::index<SavedSearchHolder::ByLocalUid>::type & savedSearchesCacheIndex = m_savedSearchesCache.get<SavedSearchHolder::ByLocalUid>();
    typedef SavedSearchesCache::index<SavedSearchHolder::ByLocalUid>::type::const_iterator SavedSearchesConstIter;
    SavedSearchesConstIter savedSearchesCacheEnd = savedSearchesCacheIndex.end();
    for(SavedSearchesConstIter it = savedSearchesCacheIndex.begin(); it != savedSearchesCacheEnd; ++it) {
        strm << *it;
    }

    strm << QStringLiteral("}; \n");

    if (m_cacheExpiryChecker.isNull()) {
        strm << QStringLiteral("Cache expiry checker is null! \n");
    }
    else {
        strm << *m_cacheExpiryChecker;
    }

    strm << QStringLiteral("}; \n");
    return strm;
}

#define GET_GUID(Type, name) \
const QString LocalStorageCacheManagerPrivate::Type##Holder::guid() const \
{ \
    /* NOTE: This precaution is required for storage of local notes in the cache */ \
    if (m_##name.hasGuid()) { \
        return m_##name.guid(); \
    } \
    else { \
        return QString(); \
    } \
}

GET_GUID(Note, note)
GET_GUID(Notebook, notebook)
GET_GUID(Tag, tag)
GET_GUID(LinkedNotebook, linkedNotebook)
GET_GUID(SavedSearch, savedSearch)

#undef GET_GUID

QTextStream & LocalStorageCacheManagerPrivate::NoteHolder::print(QTextStream & strm) const
{
    strm << QStringLiteral("NoteHolder: note = ") << m_note << QStringLiteral("last access timestamp = ") << m_lastAccessTimestamp
         << QStringLiteral(" (") << QDateTime::fromMSecsSinceEpoch(m_lastAccessTimestamp).toString(Qt::ISODate) << QStringLiteral("); \n");
    return strm;
}

QTextStream & LocalStorageCacheManagerPrivate::NotebookHolder::print(QTextStream & strm) const
{
    strm << QStringLiteral("NotebookHolder: notebook = ") << m_notebook
         << QStringLiteral("last access timestamp = ") << m_lastAccessTimestamp
            << QStringLiteral(" (") << QDateTime::fromMSecsSinceEpoch(m_lastAccessTimestamp).toString(Qt::ISODate) << QStringLiteral("); \n");
    return strm;
}

QTextStream & LocalStorageCacheManagerPrivate::TagHolder::print(QTextStream & strm) const
{
    strm << QStringLiteral("TagHolder: tag = ") << m_tag << QStringLiteral("last access timestamp = ") << m_lastAccessTimestamp
         << QStringLiteral(" (") << QDateTime::fromMSecsSinceEpoch(m_lastAccessTimestamp).toString(Qt::ISODate) << QStringLiteral("); \n");
    return strm;
}

QTextStream & LocalStorageCacheManagerPrivate::LinkedNotebookHolder::print(QTextStream & strm) const
{
    strm << QStringLiteral("LinkedNotebookHolder: linked notebook = ") << m_linkedNotebook
         << QStringLiteral("last access timestamp = ") << m_lastAccessTimestamp << QStringLiteral(" (")
         << QDateTime::fromMSecsSinceEpoch(m_lastAccessTimestamp).toString(Qt::ISODate) << QStringLiteral("); \n");
    return strm;
}

QTextStream & LocalStorageCacheManagerPrivate::SavedSearchHolder::print(QTextStream & strm) const
{
    strm << QStringLiteral("SavedSearchHolder: saved search = ") << m_savedSearch
         << QStringLiteral("last access timestamp = ") << m_lastAccessTimestamp
         << QStringLiteral("( ") << QDateTime::fromMSecsSinceEpoch(m_lastAccessTimestamp).toString(Qt::ISODate) << QStringLiteral("); \n");
    return strm;
}

LocalStorageCacheManagerPrivate::NoteHolder & LocalStorageCacheManagerPrivate::NoteHolder::operator=(const LocalStorageCacheManagerPrivate::NoteHolder & other)
{
    if (this != &other) {
        m_note = other.m_note;
        m_lastAccessTimestamp = other.m_lastAccessTimestamp;
    }

    return *this;
}

LocalStorageCacheManagerPrivate::NotebookHolder & LocalStorageCacheManagerPrivate::NotebookHolder::operator=(const LocalStorageCacheManagerPrivate::NotebookHolder & other)
{
    if (this != &other) {
        m_notebook = other.m_notebook;
        m_lastAccessTimestamp = other.m_lastAccessTimestamp;
    }

    return *this;
}

LocalStorageCacheManagerPrivate::TagHolder & LocalStorageCacheManagerPrivate::TagHolder::operator=(const LocalStorageCacheManagerPrivate::TagHolder & other)
{
    if (this != &other) {
        m_tag = other.m_tag;
        m_lastAccessTimestamp = other.m_lastAccessTimestamp;
    }

    return *this;
}

LocalStorageCacheManagerPrivate::LinkedNotebookHolder & LocalStorageCacheManagerPrivate::LinkedNotebookHolder::operator=(const LocalStorageCacheManagerPrivate::LinkedNotebookHolder & other)
{
    if (this != &other) {
        m_linkedNotebook = other.m_linkedNotebook;
        m_lastAccessTimestamp = other.m_lastAccessTimestamp;
    }

    return *this;
}

LocalStorageCacheManagerPrivate::SavedSearchHolder & LocalStorageCacheManagerPrivate::SavedSearchHolder::operator=(const LocalStorageCacheManagerPrivate::SavedSearchHolder & other)
{
    if (this != &other) {
        m_savedSearch = other.m_savedSearch;
        m_lastAccessTimestamp = other.m_lastAccessTimestamp;
    }

    return *this;
}

} // namespace quentier
