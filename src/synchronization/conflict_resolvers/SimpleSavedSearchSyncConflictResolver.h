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

#include <quentier/local_storage/Fwd.h>

#include <memory>

namespace quentier::synchronization {

class SimpleSavedSearchSyncConflictResolver final :
    public std::enable_shared_from_this<SimpleSavedSearchSyncConflictResolver>,
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
    [[nodiscard]] QFuture<SavedSearchConflictResolution>
        processSavedSearchesConflictByName(
            const qevercloud::SavedSearch & theirs,
            qevercloud::SavedSearch mine);

    [[nodiscard]] QFuture<SavedSearchConflictResolution>
        processSavedSearchesConflictByGuid(qevercloud::SavedSearch theirs);

    [[nodiscard]] QFuture<qevercloud::SavedSearch> renameConflictingSavedSearch(
        qevercloud::SavedSearch savedSearch, int counter = 1);

private:
    local_storage::ILocalStoragePtr m_localStorage;
};

} // namespace quentier::synchronization
