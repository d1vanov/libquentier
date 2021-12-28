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

#include "SimpleSavedSearchSyncConflictResolver.h"

#include <quentier/local_storage/ILocalStorage.h>

namespace quentier::synchronization {

SimpleSavedSearchSyncConflictResolver::SimpleSavedSearchSyncConflictResolver(
    local_storage::ILocalStoragePtr localStorage) :
    m_genericResolver{std::make_shared<GenericResolver>(
        std::move(localStorage),
        &local_storage::ILocalStorage::findSavedSearchByName,
        QStringLiteral("saved search"))}
{}

QFuture<ISyncConflictResolver::SavedSearchConflictResolution>
    SimpleSavedSearchSyncConflictResolver::resolveSavedSearchConflict(
        qevercloud::SavedSearch theirs, qevercloud::SavedSearch mine)
{
    return m_genericResolver->resolveConflict(
        std::move(theirs), std::move(mine));
}

} // namespace quentier::synchronization
