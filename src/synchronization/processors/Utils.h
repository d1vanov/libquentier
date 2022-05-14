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

#pragma once

#include <qevercloud/types/Fwd.h>
#include <qevercloud/types/TypeAliases.h>

#include <QList>

#include <algorithm>

class QDir;

namespace quentier::synchronization::utils {

// Given the list of items and a list of item guids meant to be expunged from
// the local storage, removed the items meant to be expunged from the list
// of items
template <class T>
void filterOutExpungedItems(
    const QList<qevercloud::Guid> & expungedGuids,
    QList<T> & items)
{
    if (expungedGuids.isEmpty()) {
        return;
    }

    for (const auto & guid: qAsConst(expungedGuids)) {
        auto it = std::find_if(
            items.begin(),
            items.end(),
            [&guid](const T & item)
            {
                return item.guid() && (*item.guid() == guid);
            });

        if (it != items.end()) {
            items.erase(it);
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

// Functions below serve the purpose of persisting the information about running
// sync before it is finished. The reason for such persistence is to prevent
// duplicate work from occurring during subsequent sync attempts if the first
// attempt did not succeed/finish properly.
//
// When the sync finishes properly, all this persisted information is cleared
// from the filesystem as it is no longer needed by then.

// Persists information about processed note inside the passed in dir
void writeProcessedNoteInfo(
    const qevercloud::Guid & noteGuid, qint32 updateSequenceNum,
    const QDir & lastSyncNotesDir);

// Persists information about note which content and/or resources failed to get
// downloaded inside the passed in dir
void writeFailedToDownloadNote(
    const qevercloud::Note & note, const QDir & lastSyncNotesDir);

// Persists information about note which processing has failed for some reason
// inside the passed in dir
void writeFailedToProcessNote(
    const qevercloud::Note & note, const QDir & lastSyncNotesDir);

// Persists information about note which processing was cancelled inside
// the passed in dir
void writeCancelledNote(
    const qevercloud::Note & note, const QDir & lastSyncNotesDir);

// Persists information about expunged note guid inside the passed in dir
void writeExpungedNote(
    const qevercloud::Guid & expungedNoteGuid, const QDir & lastSyncNotesDir);

} // namespace quentier::synchronization::utils
