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

#include "SyncStats.h"

#include <QTextStream>

namespace quentier::synchronization {

quint64 SyncStats::syncChunksDownloaded() const noexcept
{
    return m_syncChunksDownloaded;
}

quint64 SyncStats::linkedNotebooksDownloaded() const noexcept
{
    return m_linkedNotebooksDownloaded;
}

quint64 SyncStats::notebooksDownloaded() const noexcept
{
    return m_notebooksDownloaded;
}

quint64 SyncStats::savedSearchesDownloaded() const noexcept
{
    return m_savedSearchesDownloaded;
}

quint64 SyncStats::tagsDownloaded() const noexcept
{
    return m_tagsDownloaded;
}

quint64 SyncStats::notesDownloaded() const noexcept
{
    return m_notesDownloaded;
}

quint64 SyncStats::resourcesDownloaded() const noexcept
{
    return m_resourcesDownloaded;
}

quint64 SyncStats::linkedNotebooksExpunged() const noexcept
{
    return m_linkedNotebooksExpunged;
}

quint64 SyncStats::notebooksExpunged() const noexcept
{
    return m_notebooksExpunged;
}

quint64 SyncStats::savedSearchesExpunged() const noexcept
{
    return m_savedSearchesExpunged;
}

quint64 SyncStats::tagsExpunged() const noexcept
{
    return m_tagsExpunged;
}

quint64 SyncStats::notesExpunged() const noexcept
{
    return m_notesExpunged;
}

quint64 SyncStats::resourcesExpunged() const noexcept
{
    return m_resourcesExpunged;
}

quint64 SyncStats::notebooksSent() const noexcept
{
    return m_notebooksSent;
}

quint64 SyncStats::savedSearchesSent() const noexcept
{
    return m_savedSearchesSent;
}

quint64 SyncStats::tagsSent() const noexcept
{
    return m_tagsSent;
}

quint64 SyncStats::notesSent() const noexcept
{
    return m_notesSent;
}

QTextStream & SyncStats::print(QTextStream & strm) const
{
    strm << "SyncStats: syncChunksDownloaded = " << m_syncChunksDownloaded
         << ", linkedNotebooksDownloaded = " << m_linkedNotebooksDownloaded
         << ", notebooksDownloaded = " << m_notebooksDownloaded
         << ", savedSearchesDownloaded = " << m_savedSearchesDownloaded
         << ", tagsDownloaded = " << m_tagsDownloaded
         << ", notesDownloaded = " << m_notesDownloaded
         << ", resourcesDownloaded = " << m_resourcesDownloaded
         << ", linkedNotebooksExpunged = " << m_linkedNotebooksExpunged
         << ", notebooksExpunged = " << m_notebooksExpunged
         << ", savedSearchesExpunged = " << m_savedSearchesExpunged
         << ", tagsExpunged = " << m_tagsExpunged
         << ", notesExpunged = " << m_notesExpunged
         << ", resourcesExpunged = " << m_resourcesExpunged
         << ", notebooksSent = " << m_notebooksSent
         << ", savedSearchesSent = " << m_savedSearchesSent
         << ", tagsSent = " << m_tagsSent << ", notesSent = " << m_notesSent;

    return strm;
}

bool operator==(const SyncStats & lhs, const SyncStats & rhs) noexcept
{
    return lhs.m_syncChunksDownloaded == rhs.m_syncChunksDownloaded &&
        lhs.m_linkedNotebooksDownloaded == rhs.m_linkedNotebooksDownloaded &&
        lhs.m_notebooksDownloaded == rhs.m_notebooksDownloaded &&
        lhs.m_savedSearchesDownloaded == rhs.m_savedSearchesDownloaded &&
        lhs.m_tagsDownloaded == rhs.m_tagsDownloaded &&
        lhs.m_notesDownloaded == rhs.m_notesDownloaded &&
        lhs.m_resourcesDownloaded == rhs.m_resourcesDownloaded &&
        lhs.m_linkedNotebooksExpunged == rhs.m_linkedNotebooksExpunged &&
        lhs.m_notebooksExpunged == rhs.m_notebooksExpunged &&
        lhs.m_savedSearchesExpunged == rhs.m_savedSearchesExpunged &&
        lhs.m_tagsExpunged == rhs.m_tagsExpunged &&
        lhs.m_notesExpunged == rhs.m_notesExpunged &&
        lhs.m_resourcesExpunged == rhs.m_resourcesExpunged &&
        lhs.m_notebooksSent == rhs.m_notebooksSent &&
        lhs.m_savedSearchesSent == rhs.m_savedSearchesSent &&
        lhs.m_tagsSent == rhs.m_tagsSent && lhs.m_notesSent == rhs.m_notesSent;
}

bool operator!=(const SyncStats & lhs, const SyncStats & rhs) noexcept
{
    return !(lhs == rhs);
}

} // namespace quentier::synchronization
