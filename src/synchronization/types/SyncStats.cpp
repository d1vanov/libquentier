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

#include <quentier/synchronization/types/SyncStats.h>

#include <QTextStream>

namespace quentier::synchronization {

QTextStream & SyncStats::print(QTextStream & strm) const
{
    strm << "SyncStats: syncChunksDownloaded = " << syncChunksDownloaded
         << ", linkedNotebooksDownloaded = " << linkedNotebooksDownloaded
         << ", notebooksDownloaded = " << notebooksDownloaded
         << ", savedSearchesDownloaded = " << savedSearchesDownloaded
         << ", tagsDownloaded = " << tagsDownloaded
         << ", notesDownloaded = " << notesDownloaded
         << ", resourcesDownloaded = " << resourcesDownloaded
         << ", linkedNotebooksExpunged = " << linkedNotebooksExpunged
         << ", notebooksExpunged = " << notebooksExpunged
         << ", savedSearchesExpunged = " << savedSearchesExpunged
         << ", tagsExpunged = " << tagsExpunged
         << ", notesExpunged = " << notesExpunged
         << ", resourcesExpunged = " << resourcesExpunged
         << ", notebooksSent = " << notebooksSent
         << ", savedSearchesSent = " << savedSearchesSent
         << ", tagsSent = " << tagsSent << ", notesSent = " << notesSent;

    return strm;
}

bool operator==(const SyncStats & lhs, const SyncStats & rhs) noexcept
{
    return lhs.syncChunksDownloaded == rhs.syncChunksDownloaded &&
        lhs.linkedNotebooksDownloaded == rhs.linkedNotebooksDownloaded &&
        lhs.notebooksDownloaded == rhs.notebooksDownloaded &&
        lhs.savedSearchesDownloaded == rhs.savedSearchesDownloaded &&
        lhs.tagsDownloaded == rhs.tagsDownloaded &&
        lhs.notesDownloaded == rhs.notesDownloaded &&
        lhs.resourcesDownloaded == rhs.resourcesDownloaded &&
        lhs.linkedNotebooksExpunged == rhs.linkedNotebooksExpunged &&
        lhs.notebooksExpunged == rhs.notebooksExpunged &&
        lhs.savedSearchesExpunged == rhs.savedSearchesExpunged &&
        lhs.tagsExpunged == rhs.tagsExpunged &&
        lhs.notesExpunged == rhs.notesExpunged &&
        lhs.resourcesExpunged == rhs.resourcesExpunged &&
        lhs.notebooksSent == rhs.notebooksSent &&
        lhs.savedSearchesSent == rhs.savedSearchesSent &&
        lhs.tagsSent == rhs.tagsSent && lhs.notesSent == rhs.notesSent;
}

bool operator!=(const SyncStats & lhs, const SyncStats & rhs) noexcept
{
    return !(lhs == rhs);
}

} // namespace quentier::synchronization
