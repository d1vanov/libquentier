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

#ifndef LIB_QUENTIER_LOCAL_STORAGE_LOCAL_STORAGE_CACHE_MANAGER_H
#define LIB_QUENTIER_LOCAL_STORAGE_LOCAL_STORAGE_CACHE_MANAGER_H

#include <quentier/utility/Printable.h>

#include <cstdint>
#include <memory>

namespace qevercloud {

class LinkedNotebook;
class Note;
class Notebook;
class Resource;
class SavedSearch;
class Tag;

} // namespace qevercloud

namespace quentier {

class ILocalStorageCacheExpiryChecker;

class LocalStorageCacheManagerPrivate;
class QUENTIER_EXPORT LocalStorageCacheManager : public Printable
{
public:
    LocalStorageCacheManager();
    ~LocalStorageCacheManager() noexcept override;

    enum WhichUid
    {
        LocalUid,
        Guid
    };

    void clear();
    [[nodiscard]] bool empty() const;

    // Notes cache
    [[nodiscard]] std::size_t numCachedNotes() const;
    void cacheNote(const qevercloud::Note & note);
    void expungeNote(const qevercloud::Note & note);

    [[nodiscard]] const qevercloud::Note * findNote(
        const QString & uid, const WhichUid whichUid) const;

    void clearAllNotes();

    // Resources cache
    [[nodiscard]] std::size_t numCachedResources() const;
    void cacheResource(const qevercloud::Resource & resource);
    void expungeResource(const qevercloud::Resource & resource);

    [[nodiscard]] const qevercloud::Resource * findResource(
        const QString & id, const WhichUid whichUid) const;

    void clearAllResources();

    // Notebooks cache
    [[nodiscard]] std::size_t numCachedNotebooks() const;
    void cacheNotebook(const qevercloud::Notebook & notebook);
    void expungeNotebook(const qevercloud::Notebook & notebook);

    [[nodiscard]] const qevercloud::Notebook * findNotebook(
        const QString & uid, const WhichUid whichUid) const;

    const qevercloud::Notebook * findNotebookByName(const QString & name) const;
    void clearAllNotebooks();

    // Tags cache
    [[nodiscard]] std::size_t numCachedTags() const;
    void cacheTag(const qevercloud::Tag & tag);
    void expungeTag(const qevercloud::Tag & tag);

    [[nodiscard]] const qevercloud::Tag * findTag(
        const QString & uid, const WhichUid whichUid) const;

    [[nodiscard]] const qevercloud::Tag * findTagByName(
        const QString & name) const;

    void clearAllTags();

    // Linked notebooks cache
    [[nodiscard]] std::size_t numCachedLinkedNotebooks() const;
    void cacheLinkedNotebook(const qevercloud::LinkedNotebook & linkedNotebook);

    void expungeLinkedNotebook(
        const qevercloud::LinkedNotebook & linkedNotebook);

    const qevercloud::LinkedNotebook * findLinkedNotebook(
        const QString & guid) const;

    void clearAllLinkedNotebooks();

    // Saved searches cache
    [[nodiscard]] std::size_t numCachedSavedSearches() const;
    void cacheSavedSearch(const qevercloud::SavedSearch & savedSearch);
    void expungeSavedSearch(const qevercloud::SavedSearch & savedSearch);

    [[nodiscard]] const qevercloud::SavedSearch * findSavedSearch(
        const QString & uid, const WhichUid whichUid) const;

    [[nodiscard]] const qevercloud::SavedSearch * findSavedSearchByName(
        const QString & name) const;

    void clearAllSavedSearches();

    void installCacheExpiryFunction(
        const ILocalStorageCacheExpiryChecker & checker);

    QTextStream & print(QTextStream & strm) const override;

private:
    Q_DISABLE_COPY(LocalStorageCacheManager)

    LocalStorageCacheManagerPrivate * const d_ptr;
    Q_DECLARE_PRIVATE(LocalStorageCacheManager)
};

} // namespace quentier

#endif // LIB_QUENTIER_LOCAL_STORAGE_LOCAL_STORAGE_CACHE_MANAGER_H
