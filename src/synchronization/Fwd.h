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

#include <memory>

namespace quentier::synchronization {

////////////////////////////////////////////////////////////////////////////////

class ISimpleNotebookSyncConflictResolver;

using ISimpleNotebookSyncConflictResolverPtr =
    std::shared_ptr<ISimpleNotebookSyncConflictResolver>;

class ISimpleNoteSyncConflictResolver;

using ISimpleNoteSyncConflictResolverPtr =
    std::shared_ptr<ISimpleNoteSyncConflictResolver>;

class ISimpleSavedSearchSyncConflictResolver;

using ISimpleSavedSearchSyncConflictResolverPtr =
    std::shared_ptr<ISimpleSavedSearchSyncConflictResolver>;

class ISimpleTagSyncConflictResolver;

using ISimpleTagSyncConflictResolverPtr =
    std::shared_ptr<ISimpleTagSyncConflictResolver>;

////////////////////////////////////////////////////////////////////////////////

class ISyncChunksProvider;
using ISyncChunksProviderPtr = std::shared_ptr<ISyncChunksProvider>;

} // namespace quentier::synchronization
