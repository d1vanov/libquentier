/*
 * Copyright 2023 Dmitry Ivanov
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

#include <quentier/synchronization/types/Fwd.h>
#include <quentier/utility/Linkage.h>

#include <qevercloud/types/TypeAliases.h>

#include <QHash>

namespace quentier::synchronization {

class QUENTIER_EXPORT ISyncStateBuilder
{
public:
    virtual ~ISyncStateBuilder() noexcept;

    virtual ISyncStateBuilder & setUserDataUpdateCount(qint32 updateCount) = 0;

    virtual ISyncStateBuilder & setUserDataLastSyncTime(
        qevercloud::Timestamp lastSyncTime) = 0;

    virtual ISyncStateBuilder & setLinkedNotebookUpdateCounts(
        QHash<qevercloud::Guid, qint32> updateCounts) = 0;

    virtual ISyncStateBuilder & setLinkedNotebookLastSyncTimes(
        QHash<qevercloud::Guid, qevercloud::Timestamp> lastSyncTimes) = 0;

    [[nodiscard]] virtual ISyncStatePtr build() = 0;
};

[[nodiscard]] QUENTIER_EXPORT ISyncStateBuilderPtr createSyncStateBuilder();

} // namespace quentier::synchronization
