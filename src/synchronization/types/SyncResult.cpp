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

#include <quentier/synchronization/types/IDownloadNotesStatus.h>
#include <quentier/synchronization/types/IDownloadResourcesStatus.h>
#include <quentier/synchronization/types/ISyncState.h>
#include <quentier/synchronization/types/ISyncStats.h>
#include <quentier/synchronization/types/SyncResult.h>

#include <qevercloud/utility/ToRange.h>

namespace quentier::synchronization {

QTextStream & SyncResult::print(QTextStream & strm) const
{
    strm << "SyncResult: ";

    if (syncState) {
        strm << "sync state = ";
        syncState->print(strm);
    }

    if (userAccountDownloadNotesStatus) {
        strm << "userAccountDownloadNotesStatus = ";
        userAccountDownloadNotesStatus->print(strm);
    }

    strm << ", linkedNotebookDownloadNotesStatuses = ";
    if (linkedNotebookDownloadNotesStatuses.isEmpty()) {
        strm << "<empty>, ";
    }
    else {
        for (const auto it:
             qevercloud::toRange(linkedNotebookDownloadNotesStatuses)) {
            if (Q_UNLIKELY(!it.value())) {
                continue;
            }

            strm << "{" << it.key() << ": ";
            it.value()->print(strm);
            strm << "};";
        }
        strm << " ";
    }

    if (userAccountDownloadResourcesStatus) {
        strm << "userAccountDownloadResourcesStatus = ";
        userAccountDownloadResourcesStatus->print(strm);
    }

    strm << ", linkedNotebookDownloadResourcesStatuses = ";
    if (linkedNotebookDownloadResourcesStatuses.isEmpty()) {
        strm << "<empty>, ";
    }
    else {
        for (const auto it:
             qevercloud::toRange(linkedNotebookDownloadResourcesStatuses))
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

    if (syncStats) {
        strm << "syncStats = ";
        syncStats->print(strm);
    }
    return strm;
}

} // namespace quentier::synchronization
