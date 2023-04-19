/*
 * Copyright 2022-2023 Dmitry Ivanov
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

#include <quentier/synchronization/types/ISyncResult.h>

#include <synchronization/Fwd.h>
#include <synchronization/types/Fwd.h>

namespace quentier::synchronization {

struct SyncResult final: public ISyncResult
{
    [[nodiscard]] ISyncStatePtr syncState() const noexcept override;

    [[nodiscard]] ISyncChunksDataCountersPtr
        userAccountSyncChunksDataCounters() const noexcept override;

    [[nodiscard]] QHash<qevercloud::Guid, ISyncChunksDataCountersPtr>
        linkedNotebookSyncChunksDataCounters() const override;

    [[nodiscard]] IDownloadNotesStatusPtr
        userAccountDownloadNotesStatus() const noexcept override;

    [[nodiscard]] QHash<qevercloud::Guid, IDownloadNotesStatusPtr>
        linkedNotebookDownloadNotesStatuses() const override;

    [[nodiscard]] IDownloadResourcesStatusPtr
        userAccountDownloadResourcesStatus() const noexcept override;

    [[nodiscard]] QHash<qevercloud::Guid, IDownloadResourcesStatusPtr>
        linkedNotebookDownloadResourcesStatuses() const override;

    [[nodiscard]] ISendStatusPtr userAccountSendStatus() const override;

    [[nodiscard]] QHash<qevercloud::Guid, ISendStatusPtr>
        linkedNotebookSendStatuses() const override;

    [[nodiscard]] StopSynchronizationError stopSynchronizationError()
        const override;

    QTextStream & print(QTextStream & strm) const override;

    SyncStatePtr m_syncState;

    SyncChunksDataCountersPtr m_userAccountSyncChunksDataCounters;
    QHash<qevercloud::Guid, SyncChunksDataCountersPtr>
        m_linkedNotebookSyncChunksDataCounters;

    DownloadNotesStatusPtr m_userAccountDownloadNotesStatus;
    QHash<qevercloud::Guid, DownloadNotesStatusPtr>
        m_linkedNotebookDownloadNotesStatuses;

    DownloadResourcesStatusPtr m_userAccountDownloadResourcesStatus;
    QHash<qevercloud::Guid, DownloadResourcesStatusPtr>
        m_linkedNotebookDownloadResourcesStatuses;

    SendStatusPtr m_userAccountSendStatus;
    QHash<qevercloud::Guid, SendStatusPtr> m_linkedNotebookSendStatuses;

    StopSynchronizationError m_stopSynchronizationError =
        StopSynchronizationError{std::monostate{}};
};

[[nodiscard]] bool operator==(
    const SyncResult & lhs, const SyncResult & rhs) noexcept;

[[nodiscard]] bool operator!=(
    const SyncResult & lhs, const SyncResult & rhs) noexcept;

} // namespace quentier::synchronization
