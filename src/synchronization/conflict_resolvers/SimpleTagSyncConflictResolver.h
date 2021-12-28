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

#include "ISimpleTagSyncConflictResolver.h"
#include "SimpleGenericSyncConflictResolver.h"

namespace quentier::synchronization {

class SimpleTagSyncConflictResolver final :
    public ISimpleTagSyncConflictResolver
{
public:
    explicit SimpleTagSyncConflictResolver(
        local_storage::ILocalStoragePtr localStorage);

    [[nodiscard]] QFuture<TagConflictResolution> resolveTagConflict(
        qevercloud::Tag theirs, qevercloud::Tag mine) override;

private:
    // Declaring pointer to method of local_storage::ILocalStorage
    // to find notebook by name
    QFuture<std::optional<qevercloud::Tag>> (
        local_storage::ILocalStorage::*findTagByNameMemFn)(
        QString, std::optional<qevercloud::Guid>) const;

    using GenericResolver = SimpleGenericSyncConflictResolver<
        qevercloud::Tag,
        TagConflictResolution,
        decltype(findTagByNameMemFn)>;

    using GenericResolverPtr = std::shared_ptr<GenericResolver>;

private:
    GenericResolverPtr m_genericResolver;
};

} // namespace quentier::synchronization
