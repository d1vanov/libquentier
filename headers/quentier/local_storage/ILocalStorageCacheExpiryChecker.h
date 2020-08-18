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

#ifndef LIB_QUENTIER_LOCAL_STORAGE_I_LOCAL_STORAGE_CACHE_EXPIRY_CHECKER_H
#define LIB_QUENTIER_LOCAL_STORAGE_I_LOCAL_STORAGE_CACHE_EXPIRY_CHECKER_H

#include <quentier/utility/Printable.h>

namespace quentier {

QT_FORWARD_DECLARE_CLASS(LocalStorageCacheManager)

/**
 * @brief The ILocalStorageCacheExpiryChecker class represents the interface
 * for cache expiry checker used by LocalStorageCacheManager to see whether
 * particular caches (of notes, notebooks, tags, linked notebooks and/or saved
 * searches) need to be shrunk
 */
class QUENTIER_EXPORT ILocalStorageCacheExpiryChecker : public Printable
{
public:
    virtual ~ILocalStorageCacheExpiryChecker();

    /**
     * @return              A pointer to the newly allocated copy of a
     * particular ILocalStorageCacheExpiryChecker implementation
     */
    virtual ILocalStorageCacheExpiryChecker * clone() const = 0;

    /**
     * @return              False if the cache of notes needs to be shrunk (due
     *                      to its size or whatever other reason), true
     * otherwise
     */
    virtual bool checkNotes() const = 0;

    /**
     * @return              False if the cache of resources needs to be shrunk
     *                      (due to its size or whatever other reason), true
     *                      otherwise
     */
    virtual bool checkResources() const = 0;

    /**
     * @return              False if the cache of notebooks needs to be shrunk
     *                      (due to its size or whatever other reason), true
     *                      otherwise
     */
    virtual bool checkNotebooks() const = 0;

    /**
     * @return              False if the cache of tags needs to be shrunk
     *                      (due to its size or whatever other reason), true
     *                      otherwise
     */
    virtual bool checkTags() const = 0;

    /**
     * @return              False if the cache of linked notebooks needs to be
     *                      shrunk (due to its size or whatever other reason),
     *                      true otherwise
     */
    virtual bool checkLinkedNotebooks() const = 0;

    /**
     * @return              False if the cache of saved searches needs to be
     *                      shrunk (due to its size or whatever other reason),
     *                      true otherwise
     */
    virtual bool checkSavedSearches() const = 0;

    /**
     * @brief               Print the internal information about
     *                      ILocalStorageCacheExpiryChecker implementation
     *                      instance to the text stream
     */
    virtual QTextStream & print(QTextStream & strm) const = 0;

protected:
    ILocalStorageCacheExpiryChecker(
        const LocalStorageCacheManager & cacheManager);

    const LocalStorageCacheManager & m_localStorageCacheManager;

private:
    ILocalStorageCacheExpiryChecker() = delete;
    Q_DISABLE_COPY(ILocalStorageCacheExpiryChecker)
};

} // namespace quentier

#endif // LIB_QUENTIER_LOCAL_STORAGE_I_LOCAL_STORAGE_CACHE_EXPIRY_CHECKER_H
