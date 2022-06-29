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

#include <quentier/synchronization/types/DownloadNotesStatus.h>
#include <quentier/synchronization/types/DownloadResourcesStatus.h>
#include <quentier/synchronization/types/Fwd.h>

#include <quentier/utility/Linkage.h>
#include <quentier/utility/Printable.h>

#include <QHash>

namespace quentier::synchronization {

struct QUENTIER_EXPORT SyncResult : public Printable
{
    QTextStream & print(QTextStream & strm) const override;

    ISyncStatePtr syncState;

    DownloadNotesStatus userAccountDownloadNotesStatus;
    QHash<qevercloud::Guid, DownloadNotesStatus>
        linkedNotebookDownloadNotesStatuses;

    DownloadResourcesStatus userAccountDownloadResourcesStatus;
    QHash<qevercloud::Guid, DownloadResourcesStatus>
        linkedNotebookDownloadResourcesStatuses;

    ISyncStatsPtr syncStats;
};

[[nodiscard]] bool operator==(
    const SyncResult & lhs, const SyncResult & rhs) noexcept;

[[nodiscard]] bool operator!=(
    const SyncResult & lhs, const SyncResult & rhs) noexcept;

} // namespace quentier::synchronization
