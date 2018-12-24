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

#ifndef LIB_QUENTIER_LOCAL_STORAGE_DEFAULT_LOCAL_STORAGE_CACHE_EXPIRY_CHECKER_H
#define LIB_QUENTIER_LOCAL_STORAGE_DEFAULT_LOCAL_STORAGE_CACHE_EXPIRY_CHECKER_H

#include <quentier/local_storage/ILocalStorageCacheExpiryChecker.h>

namespace quentier {

/**
 * brief The DefaultLocalStorageCacheExpiryChecker class is the implementation of
 * ILocalStorageCacheExpiryChecker interface used by LocalStorageCacheManager by default,
 * if no another implementation of ILocalStorageCacheExpiryChecker is set to be used by
 * LocalStorageCacheManager
 */
class QUENTIER_EXPORT DefaultLocalStorageCacheExpiryChecker: public ILocalStorageCacheExpiryChecker
{
public:
    DefaultLocalStorageCacheExpiryChecker(const LocalStorageCacheManager & cacheManager);
    virtual ~DefaultLocalStorageCacheExpiryChecker();

    /**
     * @return a pointer to the newly allocated copy of the current DefaultLocalStorageCacheExpiryChecker
     */
    virtual DefaultLocalStorageCacheExpiryChecker * clone() const Q_DECL_OVERRIDE;

    /**
     * @return false if the current number of cached notes is higher than a reasonable limit, true otherwise
     */
    virtual bool checkNotes() const Q_DECL_OVERRIDE;

    /**
     * @return false if the current number of cached resource is higher than a reasonable limit, true otherwise
     */
    virtual bool checkResources() const Q_DECL_OVERRIDE;

    /**
     * @return false if the current number of cached notebooks is higher than a reasonable limit, true otherwise
     */
    virtual bool checkNotebooks() const Q_DECL_OVERRIDE;

    /**
     * @return false if the current number of cached tags is higher than a reasonable limit, true otherwise
     */
    virtual bool checkTags() const Q_DECL_OVERRIDE;

    /**
     * @return false if the current number of cached linked notebooks is higher than a reasonable limit, true otherwise
     */
    virtual bool checkLinkedNotebooks() const Q_DECL_OVERRIDE;

    /**
     * @return false if the current number of cached saved searches is higher than a reasonable limit, true otherwise
     */
    virtual bool checkSavedSearches() const Q_DECL_OVERRIDE;

    /**
     * @brief print the internal information about the current DefaultLocalStorageCacheExpiryChecker instance to the text stream
     */
    virtual QTextStream & print(QTextStream & strm) const Q_DECL_OVERRIDE;

private:
    DefaultLocalStorageCacheExpiryChecker() Q_DECL_EQ_DELETE;
    Q_DISABLE_COPY(DefaultLocalStorageCacheExpiryChecker)
};

} // namespace quentier

#endif // LIB_QUENTIER_LOCAL_STORAGE_DEFAULT_LOCAL_STORAGE_CACHE_EXPIRY_CHECKER_H
