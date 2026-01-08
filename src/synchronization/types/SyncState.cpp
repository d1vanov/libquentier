/*
 * Copyright 2022-2025 Dmitry Ivanov
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

#include "SyncState.h"

#include <quentier/utility/DateTime.h>

#include <qevercloud/utility/ToRange.h>

#include <QTextStream>

namespace quentier::synchronization {

SyncState::SyncState(
    qint32 userDataUpdateCount, qevercloud::Timestamp userDataLastSyncTime,
    QHash<qevercloud::Guid, qint32> linkedNotebookUpdateCounts,
    QHash<qevercloud::Guid, qevercloud::Timestamp>
        linkedNotebookLastSyncTimes) :
    m_userDataUpdateCount{userDataUpdateCount},
    m_userDataLastSyncTime{userDataLastSyncTime},
    m_linkedNotebookUpdateCounts{std::move(linkedNotebookUpdateCounts)},
    m_linkedNotebookLastSyncTimes{std::move(linkedNotebookLastSyncTimes)}
{}

qint32 SyncState::userDataUpdateCount() const noexcept
{
    return m_userDataUpdateCount;
}

qevercloud::Timestamp SyncState::userDataLastSyncTime() const noexcept
{
    return m_userDataLastSyncTime;
}

QHash<qevercloud::Guid, qint32> SyncState::linkedNotebookUpdateCounts() const
{
    return m_linkedNotebookUpdateCounts;
}

QHash<qevercloud::Guid, qevercloud::Timestamp>
    SyncState::linkedNotebookLastSyncTimes() const
{
    return m_linkedNotebookLastSyncTimes;
}

QTextStream & SyncState::print(QTextStream & strm) const
{
    strm << "SyncState:\n"
         << "    userDataUpdateCount = " << m_userDataUpdateCount << ",\n"
         << "    userDataLastSyncTime = "
         << utility::printableDateTimeFromTimestamp(m_userDataLastSyncTime)
         << ",\n";

    strm << "    linked notebook update counts:";
    if (m_linkedNotebookUpdateCounts.isEmpty()) {
        strm << " <empty>,\n";
    }
    else {
        strm << "\n";
        for (const auto it: qevercloud::toRange(m_linkedNotebookUpdateCounts)) {
            strm << "        [" << it.key() << " => " << it.value() << "];\n";
        }
    }

    strm << "    linked notebook last sync times:";
    if (m_linkedNotebookLastSyncTimes.isEmpty()) {
        strm << " <empty>\n";
    }
    else {
        strm << "\n";
        for (const auto it: qevercloud::toRange(m_linkedNotebookLastSyncTimes))
        {
            strm << "        [" << it.key() << " => "
                 << utility::printableDateTimeFromTimestamp(it.value())
                 << "];\n";
        }
    }

    return strm;
}

bool operator==(const SyncState & lhs, const SyncState & rhs) noexcept
{
    return lhs.m_userDataUpdateCount == rhs.m_userDataUpdateCount &&
        lhs.m_userDataLastSyncTime == rhs.m_userDataLastSyncTime &&
        lhs.m_linkedNotebookUpdateCounts == rhs.m_linkedNotebookUpdateCounts &&
        lhs.m_linkedNotebookLastSyncTimes == rhs.m_linkedNotebookLastSyncTimes;
}

bool operator!=(const SyncState & lhs, const SyncState & rhs) noexcept
{
    return !(lhs == rhs);
}

} // namespace quentier::synchronization
