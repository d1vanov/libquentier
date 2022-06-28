/*
 * Copyright 2022 Dmitry Ivanov
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

#include <quentier/synchronization/types/ISyncState.h>

namespace quentier::synchronization {

/**
 * @brief The SyncState structure represents the information about the state
 * of the sync process which would need to be used during the next sync.
 */
struct SyncState final : public ISyncState
{
    [[nodiscard]] qint32 userDataUpdateCount() const noexcept override;

    [[nodiscard]] qevercloud::Timestamp userDataLastSyncTime()
        const noexcept override;

    [[nodiscard]] QHash<qevercloud::Guid, qint32> linkedNotebookUpdateCounts()
        const override;

    [[nodiscard]] QHash<qevercloud::Guid, qevercloud::Timestamp>
        linkedNotebookLastSyncTimes() const override;

    QTextStream & print(QTextStream & strm) const override;

    qint32 m_userDataUpdateCount = 0;
    qevercloud::Timestamp m_userDataLastSyncTime = 0;

    QHash<qevercloud::Guid, qint32> m_linkedNotebookUpdateCounts;

    QHash<qevercloud::Guid, qevercloud::Timestamp>
        m_linkedNotebookLastSyncTimes;
};

[[nodiscard]] QUENTIER_EXPORT bool operator==(
    const SyncState & lhs, const SyncState & rhs) noexcept;

[[nodiscard]] QUENTIER_EXPORT bool operator!=(
    const SyncState & lhs, const SyncState & rhs) noexcept;

} // namespace quentier::synchronization
