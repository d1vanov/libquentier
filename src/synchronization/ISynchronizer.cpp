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

#include <quentier/synchronization/ISynchronizer.h>
#include <quentier/utility/DateTime.h>

#include <qevercloud/utility/ToRange.h>

namespace quentier::synchronization {

QTextStream & ISynchronizer::SyncResult::print(QTextStream & strm) const
{
    strm << "ISynchronizer::SyncResult: userAccountSyncState = "
         << userAccountSyncState << ", linkedNotebookSyncStates = ";

    if (linkedNotebookSyncStates.isEmpty()) {
        strm << "<empty>, ";
    }
    else {
        for (const auto it: qevercloud::toRange(linkedNotebookSyncStates)) {
            strm << "{" << it.key() << ": " << it.value() << "};";
        }
        strm << " ";
    }

    strm << "userAccountDownloadNotesStatus = "
         << userAccountDownloadNotesStatus
         << ", linkedNotebookDownloadNotesStatuses = ";

    if (linkedNotebookDownloadNotesStatuses.isEmpty()) {
        strm << "<empty>, ";
    }
    else {
        for (const auto it:
             qevercloud::toRange(linkedNotebookDownloadNotesStatuses)) {
            strm << "{" << it.key() << ": " << it.value() << "};";
        }
        strm << " ";
    }

    strm << "userAccountDownloadResourcesStatus = "
         << userAccountDownloadResourcesStatus
         << ", linkedNotebookDownloadResourcesStatuses = ";
    if (linkedNotebookDownloadResourcesStatuses.isEmpty()) {
        strm << "<empty>, ";
    }
    else {
        for (const auto it:
             qevercloud::toRange(linkedNotebookDownloadResourcesStatuses))
        {
            strm << "{" << it.key() << ": " << it.value() << "};";
        }
        strm << " ";
    }

    strm << "syncStats = " << syncStats;
    return strm;
}

} // namespace quentier::synchronization
