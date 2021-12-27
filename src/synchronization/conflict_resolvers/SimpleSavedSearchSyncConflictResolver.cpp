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

#include <quentier/exception/InvalidArgument.h>

namespace quentier::synchronization {

SimpleSavedSearchSyncConflictResolver::SimpleSavedSearchSyncConflictResolver(
    local_storage::ILocalStoragePtr localStorage) :
    m_localStorage{std::move(localStorage)}
{
    if (Q_UNLIKELY(!m_localStorage)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::SimpleNotebookSyncConflictResolver",
            "SimpleSavedSearchSyncConflictResolver ctor: local storage is "
            "null")}};
    }
}

QFuture<ISyncConflictResolver::SavedSearchConflictResolution>
    SimpleSavedSearchSyncConflictResolver::resolveSavedSearchConflict(
        qevercloud::SavedSearch theirs, qevercloud::SavedSearch mine)
{
    // TODO: implement
    Q_UNUSED(theirs)
    Q_UNUSED(mine)
    return {};
}

} // namespace quentier::synchronization
