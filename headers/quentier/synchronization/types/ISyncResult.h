/*
 * Copyright 2022-2024 Dmitry Ivanov
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

#include <quentier/synchronization/Fwd.h>
#include <quentier/synchronization/types/Errors.h>
#include <quentier/synchronization/types/Fwd.h>
#include <quentier/utility/Linkage.h>
#include <quentier/utility/Printable.h>

#include <qevercloud/types/TypeAliases.h>

#include <QHash>
#include <QSet>

namespace quentier::synchronization {

class QUENTIER_EXPORT ISyncResult : public Printable
{
public:
    [[nodiscard]] virtual ISyncStatePtr syncState() const = 0;

    [[nodiscard]] virtual ISyncChunksDataCountersPtr
        userAccountSyncChunksDataCounters() const = 0;

    [[nodiscard]] virtual QHash<qevercloud::Guid, ISyncChunksDataCountersPtr>
        linkedNotebookSyncChunksDataCounters() const = 0;

    [[nodiscard]] virtual bool userAccountSyncChunksDownloaded() const = 0;

    [[nodiscard]] virtual QSet<qevercloud::Guid>
        linkedNotebookGuidsWithSyncChunksDownloaded() const = 0;

    [[nodiscard]] virtual IDownloadNotesStatusPtr
        userAccountDownloadNotesStatus() const = 0;

    [[nodiscard]] virtual QHash<qevercloud::Guid, IDownloadNotesStatusPtr>
        linkedNotebookDownloadNotesStatuses() const = 0;

    [[nodiscard]] virtual IDownloadResourcesStatusPtr
        userAccountDownloadResourcesStatus() const = 0;

    [[nodiscard]] virtual QHash<qevercloud::Guid, IDownloadResourcesStatusPtr>
        linkedNotebookDownloadResourcesStatuses() const = 0;

    [[nodiscard]] virtual ISendStatusPtr userAccountSendStatus() const = 0;

    [[nodiscard]] virtual QHash<qevercloud::Guid, ISendStatusPtr>
        linkedNotebookSendStatuses() const = 0;

    [[nodiscard]] virtual StopSynchronizationError stopSynchronizationError()
        const = 0;
};

} // namespace quentier::synchronization
