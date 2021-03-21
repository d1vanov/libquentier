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

#include <qevercloud/generated/types/LinkedNotebook.h>
#include <qevercloud/generated/types/Note.h>
#include <qevercloud/generated/types/Notebook.h>
#include <qevercloud/generated/types/Resource.h>
#include <qevercloud/generated/types/SavedSearch.h>
#include <qevercloud/generated/types/Tag.h>

#include <quentier/local_storage/LocalStorageCacheManager.h>
#include <quentier/utility/Compat.h>
#include <quentier/utility/SuppressWarnings.h>

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>

#include <cstddef>
#include <memory>

namespace quentier {

class Q_DECL_HIDDEN LocalStorageCacheManagerPrivate final : public Printable
{
    Q_DECLARE_PUBLIC(LocalStorageCacheManager)
public:
    LocalStorageCacheManagerPrivate(LocalStorageCacheManager & q);

    ~LocalStorageCacheManagerPrivate() override;

    void clear();
    [[nodiscard]] bool empty() const noexcept;

    // Notes cache
    [[nodiscard]] std::size_t numCachedNotes() const;
    void cacheNote(const qevercloud::Note & note);
    void expungeNote(const qevercloud::Note & note);

    [[nodiscard]] const qevercloud::Note * findNoteByLocalId(
        const QString & localId) const;

    [[nodiscard]] const qevercloud::Note * findNoteByGuid(
        const QString & guid) const;

    void clearAllNotes();

    // Resources cache
    [[nodiscard]] std::size_t numCachedResources() const;
    void cacheResource(const qevercloud::Resource & resource);
    void expungeResource(const qevercloud::Resource & resource);

    [[nodiscard]] const qevercloud::Resource * findResourceByLocalId(
        const QString & localId) const;

    [[nodiscard]] const qevercloud::Resource * findResourceByGuid(
        const QString & guid) const;

    void clearAllResources();

    // Notebooks cache
    [[nodiscard]] std::size_t numCachedNotebooks() const;
    void cacheNotebook(const qevercloud::Notebook & notebook);
    void expungeNotebook(const qevercloud::Notebook & notebook);

    [[nodiscard]] const qevercloud::Notebook * findNotebookByLocalId(
        const QString & localId) const;

    [[nodiscard]] const qevercloud::Notebook * findNotebookByGuid(
        const QString & guid) const;

    [[nodiscard]] const qevercloud::Notebook * findNotebookByName(
        const QString & name) const;

    void clearAllNotebooks();

    // Tags cache
    [[nodiscard]] std::size_t numCachedTags() const;
    void cacheTag(const qevercloud::Tag & tag);
    void expungeTag(const qevercloud::Tag & tag);

    [[nodiscard]] const qevercloud::Tag * findTagByLocalId(
        const QString & localId) const;

    [[nodiscard]] const qevercloud::Tag * findTagByGuid(
        const QString & guid) const;

    [[nodiscard]] const qevercloud::Tag * findTagByName(
        const QString & name) const;

    void clearAllTags();

    // Linked notebooks cache
    [[nodiscard]] std::size_t numCachedLinkedNotebooks() const;
    void cacheLinkedNotebook(const qevercloud::LinkedNotebook & linkedNotebook);

    void expungeLinkedNotebook(
        const qevercloud::LinkedNotebook & linkedNotebook);

    [[nodiscard]] const qevercloud::LinkedNotebook * findLinkedNotebookByGuid(
        const QString & guid) const;

    void clearAllLinkedNotebooks();

    // Saved searches cache
    [[nodiscard]] std::size_t numCachedSavedSearches() const;
    void cacheSavedSearch(const qevercloud::SavedSearch & savedSearch);
    void expungeSavedSearch(const qevercloud::SavedSearch & savedSearch);

    [[nodiscard]] const qevercloud::SavedSearch * findSavedSearchByLocalId(
        const QString & localId) const;

    [[nodiscard]] const qevercloud::SavedSearch * findSavedSearchByGuid(
        const QString & guid) const;

    [[nodiscard]] const qevercloud::SavedSearch * findSavedSearchByName(
        const QString & name) const;

    void clearAllSavedSearches();

    void installCacheExpiryFunction(
        const ILocalStorageCacheExpiryChecker & checker);

    LocalStorageCacheManager * q_ptr;

    QTextStream & print(QTextStream & strm) const override;

private:
    class NoteHolder final: public Printable
    {
    public:
        NoteHolder() = default;
        NoteHolder(const NoteHolder & other) = default;
        NoteHolder(NoteHolder && other) = default;

        [[nodiscard]] NoteHolder & operator=(const NoteHolder & other) = default;
        [[nodiscard]] NoteHolder & operator=(NoteHolder && other) = default;

        qevercloud::Note m_value;
        qint64 m_lastAccessTimestamp = 0;

        [[nodiscard]] QString localId() const
        {
            return m_value.localId();
        }

        [[nodiscard]] QString guid() const;

        struct ByLastAccessTimestamp
        {};

        struct ByLocalId
        {};

        struct ByGuid
        {};

        QTextStream & print(QTextStream & strm) const override;
    };

    using NotesCache = boost::multi_index_container<
        NoteHolder,
        boost::multi_index::indexed_by<
            boost::multi_index::ordered_non_unique<
                boost::multi_index::tag<NoteHolder::ByLastAccessTimestamp>,
                boost::multi_index::member<
                    NoteHolder, qint64, &NoteHolder::m_lastAccessTimestamp>>,
            boost::multi_index::hashed_unique<
                boost::multi_index::tag<NoteHolder::ByLocalId>,
                boost::multi_index::const_mem_fun<
                    NoteHolder, QString, &NoteHolder::localId>>,
            // NOTE: non-unique for proper support of empty guids
            boost::multi_index::hashed_non_unique<
                boost::multi_index::tag<NoteHolder::ByGuid>,
                boost::multi_index::const_mem_fun<
                    NoteHolder, QString, &NoteHolder::guid>>>>;

    class ResourceHolder final: public Printable
    {
    public:
        ResourceHolder() = default;
        ResourceHolder(const ResourceHolder & other) = default;
        ResourceHolder(ResourceHolder && other) = default;

        [[nodiscard]] ResourceHolder & operator=(const ResourceHolder & other) = default;
        [[nodiscard]] ResourceHolder & operator=(ResourceHolder && other) = default;

        qevercloud::Resource m_value;
        qint64 m_lastAccessTimestamp = 0;

        [[nodiscard]] QString localId() const
        {
            return m_value.localId();
        }

        [[nodiscard]] QString guid() const;

        struct ByLastAccessTimestamp
        {};

        struct ByLocalId
        {};

        struct ByGuid
        {};

        QTextStream & print(QTextStream & strm) const override;
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
                boost::multi_index::tag<ResourceHolder::ByLocalId>,
                boost::multi_index::const_mem_fun<
                    ResourceHolder, QString, &ResourceHolder::localId>>,
            // NOTE: non-unique for proper support of empty guids
            boost::multi_index::hashed_non_unique<
                boost::multi_index::tag<ResourceHolder::ByGuid>,
                boost::multi_index::const_mem_fun<
                    ResourceHolder, QString, &ResourceHolder::guid>>>>;

    class NotebookHolder final: public Printable
    {
    public:
        NotebookHolder() = default;
        NotebookHolder(const NotebookHolder & other) = default;
        NotebookHolder(NotebookHolder && other) = default;

        [[nodiscard]] NotebookHolder & operator=(const NotebookHolder & other) = default;
        [[nodiscard]] NotebookHolder & operator=(NotebookHolder && other) = default;

        qevercloud::Notebook m_value;
        qint64 m_lastAccessTimestamp = 0;

        [[nodiscard]] QString localId() const
        {
            return m_value.localId();
        }

        [[nodiscard]] QString guid() const;

        [[nodiscard]] QString nameUpper() const
        {
            return (m_value.name() ? m_value.name()->toUpper() : QString());
        }

        struct ByLastAccessTimestamp
        {};

        struct ByLocalId
        {};

        struct ByGuid
        {};

        struct ByName
        {};

        QTextStream & print(QTextStream & strm) const override;
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
                boost::multi_index::tag<NotebookHolder::ByLocalId>,
                boost::multi_index::const_mem_fun<
                    NotebookHolder, QString, &NotebookHolder::localId>>,
            // NOTE: non-unique for proper support of empty guids
            boost::multi_index::hashed_non_unique<
                boost::multi_index::tag<NotebookHolder::ByGuid>,
                boost::multi_index::const_mem_fun<
                    NotebookHolder, QString, &NotebookHolder::guid>>,
            // NOTE: non-unique for proper support of empty names and possible
            // name collisions due to linked notebooks
            boost::multi_index::hashed_non_unique<
                boost::multi_index::tag<NotebookHolder::ByName>,
                boost::multi_index::const_mem_fun<
                    NotebookHolder, QString, &NotebookHolder::nameUpper>>>>;

    class TagHolder final: public Printable
    {
    public:
        TagHolder() = default;
        TagHolder(const TagHolder & other) = default;
        TagHolder(TagHolder && other) = default;

        [[nodiscard]] TagHolder & operator=(const TagHolder & other) = default;
        [[nodiscard]] TagHolder & operator=(TagHolder && other) = default;

        qevercloud::Tag m_value;
        qint64 m_lastAccessTimestamp = 0;

        [[nodiscard]] QString localId() const
        {
            return m_value.localId();
        }

        [[nodiscard]] QString guid() const;

        [[nodiscard]] QString nameUpper() const
        {
            return (m_value.name() ? m_value.name()->toUpper() : QString());
        }

        struct ByLastAccessTimestamp
        {};

        struct ByLocalId
        {};

        struct ByGuid
        {};

        struct ByName
        {};

        QTextStream & print(QTextStream & strm) const override;
    };

    using TagsCache = boost::multi_index_container<
        TagHolder,
        boost::multi_index::indexed_by<
            boost::multi_index::ordered_non_unique<
                boost::multi_index::tag<TagHolder::ByLastAccessTimestamp>,
                boost::multi_index::member<
                    TagHolder, qint64, &TagHolder::m_lastAccessTimestamp>>,
            boost::multi_index::hashed_unique<
                boost::multi_index::tag<TagHolder::ByLocalId>,
                boost::multi_index::const_mem_fun<
                    TagHolder, QString, &TagHolder::localId>>,
            // NOTE: non-unique for proper support of empty guids
            boost::multi_index::hashed_non_unique<
                boost::multi_index::tag<TagHolder::ByGuid>,
                boost::multi_index::const_mem_fun<
                    TagHolder, QString, &TagHolder::guid>>,
            // NOTE: non-unique for proper support of empty names and possible
            // name collisions due to linked notebooks */
            boost::multi_index::hashed_non_unique<
                boost::multi_index::tag<TagHolder::ByName>,
                boost::multi_index::const_mem_fun<
                    TagHolder, QString, &TagHolder::nameUpper>>>>;

    class LinkedNotebookHolder final: public Printable
    {
    public:
        LinkedNotebookHolder() = default;
        LinkedNotebookHolder(const LinkedNotebookHolder & other) = default;
        LinkedNotebookHolder(LinkedNotebookHolder && other) = default;

        [[nodiscard]] LinkedNotebookHolder & operator=(
            const LinkedNotebookHolder & other) = default;

        [[nodiscard]] LinkedNotebookHolder & operator=(
            LinkedNotebookHolder && other) = default;

        qevercloud::LinkedNotebook m_value;
        qint64 m_lastAccessTimestamp = 0;

        [[nodiscard]] QString guid() const;

        struct ByLastAccessTimestamp
        {};

        struct ByGuid
        {};

        QTextStream & print(QTextStream & strm) const override;
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
                    LinkedNotebookHolder, QString,
                    &LinkedNotebookHolder::guid>>>>;

    class SavedSearchHolder final: public Printable
    {
    public:
        SavedSearchHolder() = default;
        SavedSearchHolder(const SavedSearchHolder & other) = default;
        SavedSearchHolder(SavedSearchHolder && other) = default;

        [[nodiscard]] SavedSearchHolder & operator=(
            const SavedSearchHolder & other) = default;

        [[nodiscard]] SavedSearchHolder & operator=(SavedSearchHolder && other)
            = default;

        qevercloud::SavedSearch m_value;
        qint64 m_lastAccessTimestamp = 0;

        [[nodiscard]] QString localId() const
        {
            return m_value.localId();
        }

        [[nodiscard]] QString guid() const;

        [[nodiscard]] QString nameUpper() const
        {
            return (m_value.name() ? m_value.name()->toUpper() : QString());
        }

        struct ByLastAccessTimestamp
        {};

        struct ByLocalId
        {};

        struct ByGuid
        {};

        struct ByName
        {};

        QTextStream & print(QTextStream & strm) const override;
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
                boost::multi_index::tag<SavedSearchHolder::ByLocalId>,
                boost::multi_index::const_mem_fun<
                    SavedSearchHolder, QString,
                    &SavedSearchHolder::localId>>,
            // NOTE: non-unique for proper support of empty guids
            boost::multi_index::hashed_non_unique<
                boost::multi_index::tag<SavedSearchHolder::ByGuid>,
                boost::multi_index::const_mem_fun<
                    SavedSearchHolder, QString,
                    &SavedSearchHolder::guid>>,
            // NOTE: non-unique for proper support of empty names
            boost::multi_index::hashed_non_unique<
                boost::multi_index::tag<SavedSearchHolder::ByName>,
                boost::multi_index::const_mem_fun<
                    SavedSearchHolder, QString,
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
