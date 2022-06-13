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

#include <quentier/utility/Linkage.h>
#include <quentier/utility/Printable.h>

#include <qevercloud/types/TypeAliases.h>

namespace quentier::synchronization {

/**
 * @brief The SyncState structure represents the information about the state
 * of the sync process which would need to be used during the next sync.
 */
struct QUENTIER_EXPORT SyncState : public Printable
{
    QTextStream & print(QTextStream & strm) const override;

    /**
     * Update sequence number from which the next sync should be started
     */
    qint32 updateCount = 0;

    /**
     * Timestamp of the last synchronization procedure.
     */
    qevercloud::Timestamp lastSyncTime = 0;
};

[[nodiscard]] QUENTIER_EXPORT bool operator==(
    const SyncState & lhs, const SyncState & rhs) noexcept;

[[nodiscard]] QUENTIER_EXPORT bool operator!=(
    const SyncState & lhs, const SyncState & rhs) noexcept;

} // namespace quentier::synchronization
