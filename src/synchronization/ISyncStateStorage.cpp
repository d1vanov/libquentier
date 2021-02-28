/*
 * Copyright 2020 Dmitry Ivanov
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

#include <quentier/synchronization/ISyncStateStorage.h>

#include "SyncStateStorage.h"

#include <quentier/utility/Compat.h>
#include <quentier/utility/DateTime.h>

namespace quentier {

QTextStream & ISyncStateStorage::ISyncState::print(QTextStream & strm) const
{
    strm << "ISyncState: {\n"
         << "    user data update count = " << userDataUpdateCount() << "\n"
         << "    user data last sync time = "
         << printableDateTimeFromTimestamp(userDataLastSyncTime()) << "\n";

    auto updateCountsByLinkedNotebookGuid = linkedNotebookUpdateCounts();
    if (!updateCountsByLinkedNotebookGuid.isEmpty()) {
        strm << "    update counts by linked notebook guid:\n";

        for (const auto it:
             qevercloud::toRange(::qAsConst(updateCountsByLinkedNotebookGuid)))
        {
            strm << "        [" << it.key() << "] = " << it.value() << "\n";
        }
    }

    auto lastSyncTimesByLinkedNotebookGuid = linkedNotebookLastSyncTimes();
    if (!lastSyncTimesByLinkedNotebookGuid.isEmpty()) {
        strm << "    last sync times by linked notebook guid:\n";

        for (const auto it:
             qevercloud::toRange(::qAsConst(lastSyncTimesByLinkedNotebookGuid)))
        {
            strm << "        [" << it.key() << "] = " << it.value() << "\n";
        }
    }

    strm << "}\n";
    return strm;
}

ISyncStateStoragePtr newSyncStateStorage(QObject * parent)
{
    return std::make_shared<SyncStateStorage>(parent);
}

} // namespace quentier
