/*
 * Copyright 2021 Dmitry Ivanov
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

#include <quentier/synchronization/SyncChunksDataCounters.h>

namespace quentier {

QTextStream & SyncChunksDataCounters::print(QTextStream & strm) const
{
    strm << "SyncChunksDataCounters: {\n"
        << "  total saved searches = " << totalSavedSearches << "\n"
        << "  total expunged saved searches = " << totalExpungedSavedSearches
        << "\n  added saved searches = " << addedSavedSearches << "\n"
        << "  updated saved searches = " << updatedSavedSearches << "\n"
        << "  expunged saved searches = " << expungedSavedSearches << "\n"
        << "  total tags = " << totalTags << "\n"
        << "  total expunged tags = " << totalExpungedTags << "\n"
        << "  added tags = " << addedTags << "\n"
        << "  updated tags = " << updatedTags << "\n"
        << "  expunged tags = " << expungedTags << "\n"
        << "  total linked notebooks = " << totalLinkedNotebooks << "\n"
        << "  total expunged linked notebooks = "
        << totalExpungedLinkedNotebooks << "\n"
        << "  added linked notebooks = " << addedLinkedNotebooks << "\n"
        << "  updated linked notebooks = " << updatedLinkedNotebooks << "\n"
        << "  expunged linked notebooks = " << expungedLinkedNotebooks << "\n"
        << "  total notebooks = " << totalNotebooks << "\n"
        << "  total expunged notebooks = " << totalExpungedNotebooks << "\n"
        << "  added notebooks = " << addedNotebooks << "\n"
        << "  updated notebooks = " << updatedNotebooks << "\n"
        << "  expunged notebooks = " << expungedNotebooks << "\n"
        << "}\n";

    return strm;
}

} // namespace quentier
