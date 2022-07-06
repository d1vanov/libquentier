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

#include "SyncResult.h"

#include <synchronization/types/DownloadNotesStatus.h>
#include <synchronization/types/DownloadResourcesStatus.h>
#include <synchronization/types/SyncState.h>
#include <synchronization/types/SyncStats.h>

#include <qevercloud/utility/ToRange.h>

namespace quentier::synchronization {

ISyncStatePtr SyncResult::syncState() const noexcept
{
    return m_syncState;
}

IDownloadNotesStatusPtr SyncResult::userAccountDownloadNotesStatus()
    const noexcept
{
    return m_userAccountDownloadNotesStatus;
}

QHash<qevercloud::Guid, IDownloadNotesStatusPtr>
    SyncResult::linkedNotebookDownloadNotesStatuses() const
{
    QHash<qevercloud::Guid, IDownloadNotesStatusPtr> result;
    result.reserve(m_linkedNotebookDownloadNotesStatuses.size());
    for (const auto it:
         qevercloud::toRange(qAsConst(m_linkedNotebookDownloadNotesStatuses)))
    {
        result[it.key()] = it.value();
    }

    return result;
}

IDownloadResourcesStatusPtr SyncResult::userAccountDownloadResourcesStatus()
    const noexcept
{
    return m_userAccountDownloadResourcesStatus;
}

QHash<qevercloud::Guid, IDownloadResourcesStatusPtr>
    SyncResult::linkedNotebookDownloadResourcesStatuses() const
{
    QHash<qevercloud::Guid, IDownloadResourcesStatusPtr> result;
    result.reserve(m_linkedNotebookDownloadResourcesStatuses.size());
    for (const auto it: qevercloud::toRange(
             qAsConst(m_linkedNotebookDownloadResourcesStatuses)))
    {
        result[it.key()] = it.value();
    }
    return result;
}

ISyncStatsPtr SyncResult::syncStats() const noexcept
{
    return m_syncStats;
}

QTextStream & SyncResult::print(QTextStream & strm) const
{
    strm << "SyncResult: ";

    if (m_syncState) {
        strm << "sync state = ";
        m_syncState->print(strm);
    }

    if (m_userAccountDownloadNotesStatus) {
        strm << "userAccountDownloadNotesStatus = ";
        m_userAccountDownloadNotesStatus->print(strm);
    }

    strm << ", linkedNotebookDownloadNotesStatuses = ";
    if (m_linkedNotebookDownloadNotesStatuses.isEmpty()) {
        strm << "<empty>, ";
    }
    else {
        for (const auto it:
             qevercloud::toRange(m_linkedNotebookDownloadNotesStatuses)) {
            if (Q_UNLIKELY(!it.value())) {
                continue;
            }

            strm << "{" << it.key() << ": ";
            it.value()->print(strm);
            strm << "};";
        }
        strm << " ";
    }

    if (m_userAccountDownloadResourcesStatus) {
        strm << "userAccountDownloadResourcesStatus = ";
        m_userAccountDownloadResourcesStatus->print(strm);
    }

    strm << ", linkedNotebookDownloadResourcesStatuses = ";
    if (m_linkedNotebookDownloadResourcesStatuses.isEmpty()) {
        strm << "<empty>, ";
    }
    else {
        for (const auto it:
             qevercloud::toRange(m_linkedNotebookDownloadResourcesStatuses))
        {
            if (Q_UNLIKELY(!it.value())) {
                continue;
            }

            strm << "{" << it.key() << ": ";
            it.value()->print(strm);
            strm << "};";
        }
        strm << " ";
    }

    if (m_syncStats) {
        strm << "syncStats = ";
        m_syncStats->print(strm);
    }
    return strm;
}

} // namespace quentier::synchronization
