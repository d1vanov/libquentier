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

#include <memory>

namespace quentier {

QT_FORWARD_DECLARE_CLASS(LinkedNotebook)
QT_FORWARD_DECLARE_CLASS(Note)
QT_FORWARD_DECLARE_CLASS(Notebook)
QT_FORWARD_DECLARE_CLASS(Resource)
QT_FORWARD_DECLARE_CLASS(SavedSearch)
QT_FORWARD_DECLARE_CLASS(Tag)

QT_FORWARD_DECLARE_CLASS(ILocalStorageCacheExpiryChecker)

QT_FORWARD_DECLARE_CLASS(LocalStorageCacheManagerPrivate)
class QUENTIER_EXPORT LocalStorageCacheManager : public Printable
{
public:
    LocalStorageCacheManager();
    virtual ~LocalStorageCacheManager();

    enum WhichUid
    {
        LocalUid,
        Guid
    };

    void clear();
    bool empty() const;

    // Notes cache
    size_t numCachedNotes() const;
    void cacheNote(const Note & note);
    void expungeNote(const Note & note);

    const Note * findNote(const QString & uid, const WhichUid whichUid) const;

    void clearAllNotes();

    // Resources cache
    size_t numCachedResources() const;
    void cacheResource(const Resource & resource);
    void expungeResource(const Resource & resource);

    const Resource * findResource(
        const QString & id, const WhichUid whichUid) const;

    void clearAllResources();

    // Notebooks cache
    size_t numCachedNotebooks() const;
    void cacheNotebook(const Notebook & notebook);
    void expungeNotebook(const Notebook & notebook);

    const Notebook * findNotebook(
        const QString & uid, const WhichUid whichUid) const;

    const Notebook * findNotebookByName(const QString & name) const;
    void clearAllNotebooks();

    // Tags cache
    size_t numCachedTags() const;
    void cacheTag(const Tag & tag);
    void expungeTag(const Tag & tag);
    const Tag * findTag(const QString & uid, const WhichUid whichUid) const;
    const Tag * findTagByName(const QString & name) const;
    void clearAllTags();

    // Linked notebooks cache
    size_t numCachedLinkedNotebooks() const;
    void cacheLinkedNotebook(const LinkedNotebook & linkedNotebook);
    void expungeLinkedNotebook(const LinkedNotebook & linkedNotebook);
    const LinkedNotebook * findLinkedNotebook(const QString & guid) const;
    void clearAllLinkedNotebooks();

    // Saved searches cache
    size_t numCachedSavedSearches() const;
    void cacheSavedSearch(const SavedSearch & savedSearch);
    void expungeSavedSearch(const SavedSearch & savedSearch);

    const SavedSearch * findSavedSearch(
        const QString & uid, const WhichUid whichUid) const;

    const SavedSearch * findSavedSearchByName(const QString & name) const;
    void clearAllSavedSearches();

    void installCacheExpiryFunction(
        const ILocalStorageCacheExpiryChecker & checker);

    virtual QTextStream & print(QTextStream & strm) const override;

private:
    Q_DISABLE_COPY(LocalStorageCacheManager)

    LocalStorageCacheManagerPrivate * const d_ptr;
    Q_DECLARE_PRIVATE(LocalStorageCacheManager)
};

} // namespace quentier

#endif // LIB_QUENTIER_LOCAL_STORAGE_LOCAL_STORAGE_CACHE_MANAGER_H
