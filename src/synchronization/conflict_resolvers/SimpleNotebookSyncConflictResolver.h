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

#include "ISimpleNotebookSyncConflictResolver.h"
#include "SimpleGenericSyncConflictResolver.h"

namespace quentier::synchronization {

class SimpleNotebookSyncConflictResolver final :
    public ISimpleNotebookSyncConflictResolver
{
public:
    explicit SimpleNotebookSyncConflictResolver(
        local_storage::ILocalStoragePtr localStorage);

    [[nodiscard]] QFuture<NotebookConflictResolution> resolveNotebookConflict(
        qevercloud::Notebook theirs, qevercloud::Notebook mine) override;

private:
    // Declaring pointer to method of local_storage::ILocalStorage
    // to find notebook by name
    QFuture<std::optional<qevercloud::Notebook>> (
        local_storage::ILocalStorage::*findNotebookByNameMemFn)(
        QString, std::optional<qevercloud::Guid>) const;

    using GenericResolver = SimpleGenericSyncConflictResolver<
        qevercloud::Notebook,
        NotebookConflictResolution,
        decltype(findNotebookByNameMemFn)>;

    using GenericResolverPtr = std::shared_ptr<GenericResolver>;

private:
    GenericResolverPtr m_genericResolver;
};

} // namespace quentier::synchronization
