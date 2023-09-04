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

#include <quentier/synchronization/types/ISyncStateBuilder.h>

namespace quentier::synchronization {

class SyncStateBuilder final : public ISyncStateBuilder
{
public:
    ISyncStateBuilder & setUserDataUpdateCount(qint32 updateCount) noexcept override;

    ISyncStateBuilder & setUserDataLastSyncTime(
        qevercloud::Timestamp lastSyncTime) noexcept override;

    ISyncStateBuilder & setLinkedNotebookUpdateCounts(
        QHash<qevercloud::Guid, qint32> updateCounts) override;

    ISyncStateBuilder & setLinkedNotebookLastSyncTimes(
        QHash<qevercloud::Guid, qevercloud::Timestamp> lastSyncTimes) override;

    [[nodiscard]] ISyncStatePtr build() override;

private:
    qint32 m_userDataUpdateCount = 0;
    qevercloud::Timestamp m_userDataLastSyncTime = 0;
    QHash<qevercloud::Guid, qint32> m_linkedNotebookUpdateCounts;
    QHash<qevercloud::Guid, qevercloud::Timestamp>
        m_linkedNotebookLastSyncTimes;
};

} // namespace quentier::synchronization
