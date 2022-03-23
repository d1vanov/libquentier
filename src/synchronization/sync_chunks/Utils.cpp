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

#include "Utils.h"

#include <qevercloud/types/SyncChunk.h>

namespace quentier::synchronization::utils {

[[nodiscard]] std::optional<qint32> syncChunkLowUsn(
    const qevercloud::SyncChunk & syncChunk)
{
    std::optional<qint32> lowUsn;

    const auto checkLowUsn = [&](const auto & items)
    {
        for (const auto & item: qAsConst(items)) {
            if (item.updateSequenceNum() &&
                (!lowUsn || *lowUsn > *item.updateSequenceNum()))
            {
                lowUsn = *item.updateSequenceNum();
            }
        }
    };

    if (syncChunk.notes()) {
        checkLowUsn(*syncChunk.notes());
    }

    if (syncChunk.notebooks()) {
        checkLowUsn(*syncChunk.notebooks());
    }

    if (syncChunk.tags()) {
        checkLowUsn(*syncChunk.tags());
    }

    if (syncChunk.searches()) {
        checkLowUsn(*syncChunk.searches());
    }

    if (syncChunk.resources()) {
        checkLowUsn(*syncChunk.resources());
    }

    if (syncChunk.linkedNotebooks()) {
        checkLowUsn(*syncChunk.linkedNotebooks());
    }

    return lowUsn;
}

void setLinkedNotebookGuidToSyncChunkEntries(
    const qevercloud::Guid & linkedNotebookGuid,
    qevercloud::SyncChunk & syncChunk)
{
    if (syncChunk.notebooks())
    {
        for (auto & notebook: *syncChunk.mutableNotebooks())
        {
            notebook.setLinkedNotebookGuid(linkedNotebookGuid);
        }
    }

    if (syncChunk.tags())
    {
        for (auto & tag: *syncChunk.mutableTags())
        {
            tag.setLinkedNotebookGuid(linkedNotebookGuid);
        }
    }
}

} // namespace quentier::synchronization::utils
