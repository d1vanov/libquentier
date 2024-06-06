/*
 * Copyright 2024 Dmitry Ivanov
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

#include <synchronization/ISender.h>
#include <synchronization/types/SendStatus.h>
#include <synchronization/types/SyncState.h>

#include <qevercloud/utility/ToRange.h>

namespace quentier::synchronization {

QTextStream & ISender::Result::print(QTextStream & strm) const
{
    strm << "User own send status: "
         << (userOwnResult ? userOwnResult->toString()
                           : QStringLiteral("<null>"));

    strm << "\nLinked notebook send statuses: (" << linkedNotebookResults.size()
         << "):\n";

    for (const auto it: qevercloud::toRange(linkedNotebookResults)) {
        strm << "    [guid = " << it.key() << ", send status: "
             << (it.value() ? it.value()->toString() : QStringLiteral("<null>"))
             << "];\n";
    }

    strm << "Sync state: "
         << (syncState ? syncState->toString() : QStringLiteral("<null>"));

    return strm;
}

} // namespace quentier::synchronization
