/*
 * Copyright 2021 Dmitry Ivanov
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

#pragma once

#include "ISimpleSavedSearchSyncConflictResolver.h"
#include "SimpleGenericSyncConflictResolver.h"

#include <qevercloud/types/SavedSearch.h>

namespace quentier::synchronization {

class SimpleSavedSearchSyncConflictResolver final :
    public ISimpleSavedSearchSyncConflictResolver
{
public:
    explicit SimpleSavedSearchSyncConflictResolver(
        local_storage::ILocalStoragePtr localStorage);

    [[nodiscard]] QFuture<SavedSearchConflictResolution>
        resolveSavedSearchConflict(
            qevercloud::SavedSearch theirs,
            qevercloud::SavedSearch mine) override;

private:
    // Declaring pointer to method of local_storage::ILocalStorage
    // to find saved search by name
    QFuture<std::optional<qevercloud::SavedSearch>> (
        local_storage::ILocalStorage::*findSavedSearchByNameMemFn)(
        QString) const;

    using GenericResolver = SimpleGenericSyncConflictResolver<
        qevercloud::SavedSearch,
        SavedSearchConflictResolution,
        decltype(findSavedSearchByNameMemFn)>;

    using GenericResolverPtr = std::shared_ptr<GenericResolver>;

private:
    GenericResolverPtr m_genericResolver;
};

} // namespace quentier::synchronization
