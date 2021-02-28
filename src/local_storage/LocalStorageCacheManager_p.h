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

#ifndef LIB_QUENTIER_LOCAL_STORAGE_LOCAL_STORAGE_CACHE_MANAGER_PRIVATE_H
#define LIB_QUENTIER_LOCAL_STORAGE_LOCAL_STORAGE_CACHE_MANAGER_PRIVATE_H

#include <quentier/local_storage/LocalStorageCacheManager.h>
#include <quentier/types/LinkedNotebook.h>
#include <quentier/types/Note.h>
#include <quentier/types/Notebook.h>
#include <quentier/types/Resource.h>
#include <quentier/types/SavedSearch.h>
#include <quentier/types/Tag.h>
#include <quentier/utility/Compat.h>
#include <quentier/utility/SuppressWarnings.h>

SAVE_WARNINGS
// clang-format off
GCC_SUPPRESS_WARNING(-Wdeprecated-declarations)
// clang-format on

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>

RESTORE_WARNINGS

#include <memory>

namespace quentier {

class Q_DECL_HIDDEN LocalStorageCacheManagerPrivate final : public Printable
{
    Q_DECLARE_PUBLIC(LocalStorageCacheManager)
public:
    LocalStorageCacheManagerPrivate(LocalStorageCacheManager & q);

    virtual ~LocalStorageCacheManagerPrivate() override;

    void clear();
    bool empty() const;

    // Notes cache
    size_t numCachedNotes() const;
    void cacheNote(const Note & note);
    void expungeNote(const Note & note);

    const Note * findNoteByLocalUid(const QString & localUid) const;
    const Note * findNoteByGuid(const QString & guid) const;

    void clearAllNotes();

    // Resources cache
    size_t numCachedResources() const;
    void cacheResource(const Resource & resource);
    void expungeResource(const Resource & resource);

    const Resource * findResourceByLocalUid(const QString & localUid) const;
    const Resource * findResourceByGuid(const QString & guid) const;

    void clearAllResources();

    // Notebooks cache
    size_t numCachedNotebooks() const;
    void cacheNotebook(const Notebook & notebook);
    void expungeNotebook(const Notebook & notebook);

    const Notebook * findNotebookByLocalUid(const QString & localUid) const;
    const Notebook * findNotebookByGuid(const QString & guid) const;
    const Notebook * findNotebookByName(const QString & name) const;

    void clearAllNotebooks();

    // Tags cache
    size_t numCachedTags() const;
    void cacheTag(const Tag & tag);
    void expungeTag(const Tag & tag);

    const Tag * findTagByLocalUid(const QString & localUid) const;
    const Tag * findTagByGuid(const QString & guid) const;
    const Tag * findTagByName(const QString & name) const;

    void clearAllTags();

    // Linked notebooks cache
    size_t numCachedLinkedNotebooks() const;
    void cacheLinkedNotebook(const LinkedNotebook & linkedNotebook);
    void expungeLinkedNotebook(const LinkedNotebook & linkedNotebook);

    const LinkedNotebook * findLinkedNotebookByGuid(const QString & guid) const;

    void clearAllLinkedNotebooks();

    // Saved searches cache
    size_t numCachedSavedSearches() const;
    void cacheSavedSearch(const SavedSearch & savedSearch);
    void expungeSavedSearch(const SavedSearch & savedSearch);

    const SavedSearch * findSavedSearchByLocalUid(
        const QString & localUid) const;

    const SavedSearch * findSavedSearchByGuid(const QString & guid) const;
    const SavedSearch * findSavedSearchByName(const QString & name) const;

    void clearAllSavedSearches();

    void installCacheExpiryFunction(
        const ILocalStorageCacheExpiryChecker & checker);

    LocalStorageCacheManager * q_ptr;

    virtual QTextStream & print(QTextStream & strm) const override;

private:
    class NoteHolder : public Printable
    {
    public:
        NoteHolder() = default;

        NoteHolder(const NoteHolder & other) = default;
        NoteHolder(NoteHolder && other) = default;

        NoteHolder & operator=(const NoteHolder & other) = default;
        NoteHolder & operator=(NoteHolder && other) = default;

        Note m_value;
        qint64 m_lastAccessTimestamp = 0;

        const QString localUid() const
        {
            return m_value.localUid();
        }

        const QString guid() const;

        struct ByLastAccessTimestamp
        {};

        struct ByLocalUid
        {};

        struct ByGuid
        {};

        virtual QTextStream & print(QTextStream & strm) const override;
    };

    using NotesCache = boost::multi_index_container<
        NoteHolder,
        boost::multi_index::indexed_by<
            boost::multi_index::ordered_non_unique<
                boost::multi_index::tag<NoteHolder::ByLastAccessTimestamp>,
                boost::multi_index::member<
                    NoteHolder, qint64, &NoteHolder::m_lastAccessTimestamp>>,
            boost::multi_index::hashed_unique<
                boost::multi_index::tag<NoteHolder::ByLocalUid>,
                boost::multi_index::const_mem_fun<
                    NoteHolder, const QString, &NoteHolder::localUid>>,
            // NOTE: non-unique for proper support of empty guids
            boost::multi_index::hashed_non_unique<
                boost::multi_index::tag<NoteHolder::ByGuid>,
                boost::multi_index::const_mem_fun<
                    NoteHolder, const QString, &NoteHolder::guid>>>>;

    class ResourceHolder : public Printable
    {
    public:
        ResourceHolder() = default;

        ResourceHolder(const ResourceHolder & other) = default;
        ResourceHolder(ResourceHolder && other) = default;

        ResourceHolder & operator=(const ResourceHolder & other) = default;
        ResourceHolder & operator=(ResourceHolder && other) = default;

        Resource m_value;
        qint64 m_lastAccessTimestamp = 0;

        const QString localUid() const
        {
            return m_value.localUid();
        }

        const QString guid() const;

        struct ByLastAccessTimestamp
        {};

        struct ByLocalUid
        {};

        struct ByGuid
        {};

        virtual QTextStream & print(QTextStream & strm) const override;
    };

    using ResourcesCache = boost::multi_index_container<
        ResourceHolder,
        boost::multi_index::indexed_by<
            boost::multi_index::ordered_non_unique<
                boost::multi_index::tag<ResourceHolder::ByLastAccessTimestamp>,
                boost::multi_index::member<
                    ResourceHolder, qint64,
                    &ResourceHolder::m_lastAccessTimestamp>>,
            boost::multi_index::hashed_unique<
                boost::multi_index::tag<ResourceHolder::ByLocalUid>,
                boost::multi_index::const_mem_fun<
                    ResourceHolder, const QString, &ResourceHolder::localUid>>,
            // NOTE: non-unique for proper support of empty guids
            boost::multi_index::hashed_non_unique<
                boost::multi_index::tag<ResourceHolder::ByGuid>,
                boost::multi_index::const_mem_fun<
                    ResourceHolder, const QString, &ResourceHolder::guid>>>>;

    class NotebookHolder : public Printable
    {
    public:
        NotebookHolder() = default;

        NotebookHolder(const NotebookHolder & other) = default;
        NotebookHolder(NotebookHolder && other) = default;

        NotebookHolder & operator=(const NotebookHolder & other) = default;
        NotebookHolder & operator=(NotebookHolder && other) = default;

        Notebook m_value;
        qint64 m_lastAccessTimestamp = 0;

        const QString localUid() const
        {
            return m_value.localUid();
        }

        const QString guid() const;

        const QString nameUpper() const
        {
            return (m_value.hasName() ? m_value.name().toUpper() : QString());
        }

        struct ByLastAccessTimestamp
        {};

        struct ByLocalUid
        {};

        struct ByGuid
        {};

        struct ByName
        {};

        virtual QTextStream & print(QTextStream & strm) const override;
    };

    using NotebooksCache = boost::multi_index_container<
        NotebookHolder,
        boost::multi_index::indexed_by<
            boost::multi_index::ordered_non_unique<
                boost::multi_index::tag<NotebookHolder::ByLastAccessTimestamp>,
                boost::multi_index::member<
                    NotebookHolder, qint64,
                    &NotebookHolder::m_lastAccessTimestamp>>,
            boost::multi_index::hashed_unique<
                boost::multi_index::tag<NotebookHolder::ByLocalUid>,
                boost::multi_index::const_mem_fun<
                    NotebookHolder, const QString, &NotebookHolder::localUid>>,
            // NOTE: non-unique for proper support of empty guids
            boost::multi_index::hashed_non_unique<
                boost::multi_index::tag<NotebookHolder::ByGuid>,
                boost::multi_index::const_mem_fun<
                    NotebookHolder, const QString, &NotebookHolder::guid>>,
            // NOTE: non-unique for proper support of empty names and possible
            // name collisions due to linked notebooks
            boost::multi_index::hashed_non_unique<
                boost::multi_index::tag<NotebookHolder::ByName>,
                boost::multi_index::const_mem_fun<
                    NotebookHolder, const QString,
                    &NotebookHolder::nameUpper>>>>;

    class TagHolder : public Printable
    {
    public:
        TagHolder() = default;

        TagHolder(const TagHolder & other) = default;
        TagHolder(TagHolder && other) = default;

        TagHolder & operator=(const TagHolder & other) = default;
        TagHolder & operator=(TagHolder && other) = default;

        Tag m_value;
        qint64 m_lastAccessTimestamp = 0;

        const QString localUid() const
        {
            return m_value.localUid();
        }

        const QString guid() const;

        const QString nameUpper() const
        {
            return (m_value.hasName() ? m_value.name().toUpper() : QString());
        }

        struct ByLastAccessTimestamp
        {};

        struct ByLocalUid
        {};

        struct ByGuid
        {};

        struct ByName
        {};

        virtual QTextStream & print(QTextStream & strm) const override;
    };

    using TagsCache = boost::multi_index_container<
        TagHolder,
        boost::multi_index::indexed_by<
            boost::multi_index::ordered_non_unique<
                boost::multi_index::tag<TagHolder::ByLastAccessTimestamp>,
                boost::multi_index::member<
                    TagHolder, qint64, &TagHolder::m_lastAccessTimestamp>>,
            boost::multi_index::hashed_unique<
                boost::multi_index::tag<TagHolder::ByLocalUid>,
                boost::multi_index::const_mem_fun<
                    TagHolder, const QString, &TagHolder::localUid>>,
            // NOTE: non-unique for proper support of empty guids
            boost::multi_index::hashed_non_unique<
                boost::multi_index::tag<TagHolder::ByGuid>,
                boost::multi_index::const_mem_fun<
                    TagHolder, const QString, &TagHolder::guid>>,
            // NOTE: non-unique for proper support of empty names and possible
            // name collisions due to linked notebooks */
            boost::multi_index::hashed_non_unique<
                boost::multi_index::tag<TagHolder::ByName>,
                boost::multi_index::const_mem_fun<
                    TagHolder, const QString, &TagHolder::nameUpper>>>>;

    class LinkedNotebookHolder : public Printable
    {
    public:
        LinkedNotebookHolder() = default;

        LinkedNotebookHolder(const LinkedNotebookHolder & other) = default;
        LinkedNotebookHolder(LinkedNotebookHolder && other) = default;

        LinkedNotebookHolder & operator=(const LinkedNotebookHolder & other) =
            default;

        LinkedNotebookHolder & operator=(LinkedNotebookHolder && other) =
            default;

        LinkedNotebook m_value;
        qint64 m_lastAccessTimestamp = 0;

        const QString guid() const;

        struct ByLastAccessTimestamp
        {};

        struct ByGuid
        {};

        virtual QTextStream & print(QTextStream & strm) const override;
    };

    using LinkedNotebooksCache = boost::multi_index_container<
        LinkedNotebookHolder,
        boost::multi_index::indexed_by<
            boost::multi_index::ordered_non_unique<
                boost::multi_index::tag<
                    LinkedNotebookHolder::ByLastAccessTimestamp>,
                boost::multi_index::member<
                    LinkedNotebookHolder, qint64,
                    &LinkedNotebookHolder::m_lastAccessTimestamp>>,
            boost::multi_index::hashed_unique<
                boost::multi_index::tag<LinkedNotebookHolder::ByGuid>,
                boost::multi_index::const_mem_fun<
                    LinkedNotebookHolder, const QString,
                    &LinkedNotebookHolder::guid>>>>;

    class SavedSearchHolder : public Printable
    {
    public:
        SavedSearchHolder() = default;

        SavedSearchHolder(const SavedSearchHolder & other) = default;
        SavedSearchHolder(SavedSearchHolder && other) = default;

        SavedSearchHolder & operator=(const SavedSearchHolder & other) =
            default;

        SavedSearchHolder & operator=(SavedSearchHolder && other) = default;

        SavedSearch m_value;
        qint64 m_lastAccessTimestamp = 0;

        const QString localUid() const
        {
            return m_value.localUid();
        }

        const QString guid() const;

        const QString nameUpper() const
        {
            return (m_value.hasName() ? m_value.name().toUpper() : QString());
        }

        struct ByLastAccessTimestamp
        {};

        struct ByLocalUid
        {};

        struct ByGuid
        {};

        struct ByName
        {};

        virtual QTextStream & print(QTextStream & strm) const override;
    };

    using SavedSearchesCache = boost::multi_index_container<
        SavedSearchHolder,
        boost::multi_index::indexed_by<
            boost::multi_index::ordered_non_unique<
                boost::multi_index::tag<
                    SavedSearchHolder::ByLastAccessTimestamp>,
                boost::multi_index::member<
                    SavedSearchHolder, qint64,
                    &SavedSearchHolder::m_lastAccessTimestamp>>,
            boost::multi_index::hashed_unique<
                boost::multi_index::tag<SavedSearchHolder::ByLocalUid>,
                boost::multi_index::const_mem_fun<
                    SavedSearchHolder, const QString,
                    &SavedSearchHolder::localUid>>,
            // NOTE: non-unique for proper support of empty guids
            boost::multi_index::hashed_non_unique<
                boost::multi_index::tag<SavedSearchHolder::ByGuid>,
                boost::multi_index::const_mem_fun<
                    SavedSearchHolder, const QString,
                    &SavedSearchHolder::guid>>,
            // NOTE: non-unique for proper support of empty names
            boost::multi_index::hashed_non_unique<
                boost::multi_index::tag<SavedSearchHolder::ByName>,
                boost::multi_index::const_mem_fun<
                    SavedSearchHolder, const QString,
                    &SavedSearchHolder::nameUpper>>>>;

private:
    Q_DISABLE_COPY(LocalStorageCacheManagerPrivate)

private:
    std::unique_ptr<ILocalStorageCacheExpiryChecker> m_cacheExpiryChecker;

    NotesCache m_notesCache;
    ResourcesCache m_resourcesCache;
    NotebooksCache m_notebooksCache;
    TagsCache m_tagsCache;
    LinkedNotebooksCache m_linkedNotebooksCache;
    SavedSearchesCache m_savedSearchesCache;
};

} // namespace quentier

#endif // LIB_QUENTIER_LOCAL_STORAGE_LOCAL_STORAGE_CACHE_MANAGER_PRIVATE_H
