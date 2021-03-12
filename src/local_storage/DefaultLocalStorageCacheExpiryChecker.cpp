/*
 * Copyright 2016-2021 Dmitry Ivanov
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

#include "DefaultLocalStorageCacheExpiryCheckerConfig.h"

#include <quentier/local_storage/DefaultLocalStorageCacheExpiryChecker.h>
#include <quentier/local_storage/LocalStorageCacheManager.h>

namespace quentier {

DefaultLocalStorageCacheExpiryChecker::DefaultLocalStorageCacheExpiryChecker(
    const LocalStorageCacheManager & cacheManager) :
    ILocalStorageCacheExpiryChecker(cacheManager)
{}

DefaultLocalStorageCacheExpiryChecker::~DefaultLocalStorageCacheExpiryChecker()
    = default;

DefaultLocalStorageCacheExpiryChecker *
DefaultLocalStorageCacheExpiryChecker::clone() const
{
    return new DefaultLocalStorageCacheExpiryChecker(
        m_localStorageCacheManager);
}

bool DefaultLocalStorageCacheExpiryChecker::checkNotes() const
{
    size_t numNotes = m_localStorageCacheManager.numCachedNotes();
    return (numNotes < local_storage::maxNotesToCache);
}

bool DefaultLocalStorageCacheExpiryChecker::checkResources() const
{
    size_t numResources = m_localStorageCacheManager.numCachedResources();
    return (numResources < local_storage::maxResourcesToCache);
}

bool DefaultLocalStorageCacheExpiryChecker::checkNotebooks() const
{
    size_t numNotebooks = m_localStorageCacheManager.numCachedNotebooks();
    return (numNotebooks < local_storage::maxNotebooksToCache);
}

bool DefaultLocalStorageCacheExpiryChecker::checkTags() const
{
    size_t numTags = m_localStorageCacheManager.numCachedTags();
    return (numTags < local_storage::maxTagsToCache);
}

bool DefaultLocalStorageCacheExpiryChecker::checkLinkedNotebooks() const
{
    size_t numCachedLinkedNotebooks =
        m_localStorageCacheManager.numCachedLinkedNotebooks();

    return (numCachedLinkedNotebooks < local_storage::maxLinkedNotebooksToCache);
}

bool DefaultLocalStorageCacheExpiryChecker::checkSavedSearches() const
{
    size_t numCachedSavedSearches =
        m_localStorageCacheManager.numCachedSavedSearches();

    return (numCachedSavedSearches < local_storage::maxSavedSearchesToCache);
}

QTextStream & DefaultLocalStorageCacheExpiryChecker::print(
    QTextStream & strm) const
{
    const char * indent = "  ";

    strm << "DefaultLocalStorageCacheExpiryChecker: {\n"
         << indent << "max notes to cache: " << local_storage::maxNotesToCache
         << ";\n"
         << indent << "max notebooks to cache: "
         << local_storage::maxNotebooksToCache
         << ";\n"
         << indent << "max tags to cache: " << local_storage::maxTagsToCache
         << ";\n" << indent
         << "max linked notebooks to cache: "
         << local_storage::maxLinkedNotebooksToCache
         << ";\n" << indent
         << "max saved searches to cache: "
         << local_storage::maxSavedSearchesToCache
         << "\n"
         << "};\n";

    return strm;
}

} // namespace quentier
