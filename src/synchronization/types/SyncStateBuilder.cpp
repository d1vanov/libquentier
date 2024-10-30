/*
 * Copyright 2023-2024 Dmitry Ivanov
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

#include "SyncStateBuilder.h"
#include "SyncState.h"

namespace quentier::synchronization {

ISyncStateBuilder & SyncStateBuilder::setUserDataUpdateCount(
    const qint32 updateCount) noexcept
{
    m_userDataUpdateCount = updateCount;
    return *this;
}

ISyncStateBuilder & SyncStateBuilder::setUserDataLastSyncTime(
    qevercloud::Timestamp lastSyncTime) noexcept
{
    m_userDataLastSyncTime = lastSyncTime;
    return *this;
}

ISyncStateBuilder & SyncStateBuilder::setLinkedNotebookUpdateCounts(
    QHash<qevercloud::Guid, qint32> updateCounts)
{
    m_linkedNotebookUpdateCounts = std::move(updateCounts);
    return *this;
}

ISyncStateBuilder & SyncStateBuilder::setLinkedNotebookLastSyncTimes(
    QHash<qevercloud::Guid, qevercloud::Timestamp> lastSyncTimes)
{
    m_linkedNotebookLastSyncTimes = std::move(lastSyncTimes);
    return *this;
}

ISyncStatePtr SyncStateBuilder::build()
{
    auto syncState = std::make_shared<SyncState>();
    syncState->m_userDataUpdateCount = m_userDataUpdateCount;
    syncState->m_userDataLastSyncTime = m_userDataLastSyncTime;

    syncState->m_linkedNotebookUpdateCounts =
        std::move(m_linkedNotebookUpdateCounts);

    syncState->m_linkedNotebookLastSyncTimes =
        std::move(m_linkedNotebookLastSyncTimes);

    m_userDataUpdateCount = 0;
    m_userDataLastSyncTime = 0;
    m_linkedNotebookUpdateCounts.clear();
    m_linkedNotebookLastSyncTimes.clear();

    return syncState;
}

} // namespace quentier::synchronization
