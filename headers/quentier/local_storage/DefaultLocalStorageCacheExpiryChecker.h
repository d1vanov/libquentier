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

#ifndef LIB_QUENTIER_LOCAL_STORAGE_DEFAULT_LOCAL_STORAGE_CACHE_EXPIRY_CHECKER_H
#define LIB_QUENTIER_LOCAL_STORAGE_DEFAULT_LOCAL_STORAGE_CACHE_EXPIRY_CHECKER_H

#include <quentier/local_storage/ILocalStorageCacheExpiryChecker.h>

namespace quentier {

/**
 * brief The DefaultLocalStorageCacheExpiryChecker class is the implementation
 * of ILocalStorageCacheExpiryChecker interface used by LocalStorageCacheManager
 * by default, if no another implementation of ILocalStorageCacheExpiryChecker
 * is set to be used by LocalStorageCacheManager
 */
class QUENTIER_EXPORT DefaultLocalStorageCacheExpiryChecker :
    public ILocalStorageCacheExpiryChecker
{
public:
    DefaultLocalStorageCacheExpiryChecker(
        const LocalStorageCacheManager & cacheManager);

    virtual ~DefaultLocalStorageCacheExpiryChecker();

    /**
     * @return              A pointer to the newly allocated copy of the current
     *                      DefaultLocalStorageCacheExpiryChecker
     */
    virtual DefaultLocalStorageCacheExpiryChecker * clone() const override;

    /**
     * @return              False if the current number of cached notes is
     * higher than a reasonable limit, true otherwise
     */
    virtual bool checkNotes() const override;

    /**
     * @return              False if the current number of cached resource is
     *                      higher than a reasonable limit, true otherwise
     */
    virtual bool checkResources() const override;

    /**
     * @return              False if the current number of cached notebooks is
     *                      higher than a reasonable limit, true otherwise
     */
    virtual bool checkNotebooks() const override;

    /**
     * @return              False if the current number of cached tags is higher
     *                      than a reasonable limit, true otherwise
     */
    virtual bool checkTags() const override;

    /**
     * @return              False if the current number of cached linked
     * notebooks is higher than a reasonable limit, true otherwise
     */
    virtual bool checkLinkedNotebooks() const override;

    /**
     * @return              False if the current number of cached saved searches
     *                      is higher than a reasonable limit, true otherwise
     */
    virtual bool checkSavedSearches() const override;

    /**
     * @brief               Print the internal information about the current
     *                      DefaultLocalStorageCacheExpiryChecker instance
     *                      to the text stream
     */
    virtual QTextStream & print(QTextStream & strm) const override;

private:
    Q_DISABLE_COPY(DefaultLocalStorageCacheExpiryChecker)
};

} // namespace quentier

#endif // LIB_QUENTIER_LOCAL_STORAGE_DEFAULT_LOCAL_STORAGE_CACHE_EXPIRY_CHECKER_H
