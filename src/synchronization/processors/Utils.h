/*
 * Copyright 2022-2023 Dmitry Ivanov
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

#include "INotesProcessor.h"
#include "IResourcesProcessor.h"

#include <synchronization/types/DownloadNotesStatus.h>
#include <synchronization/types/DownloadResourcesStatus.h>

#include <qevercloud/types/Fwd.h>
#include <qevercloud/types/TypeAliases.h>

#include <QList>

#include <algorithm>
#include <utility>

class QDir;

namespace quentier::synchronization::utils {

// Given the list of items and a list of item guids meant to be expunged from
// the local storage, removed the items meant to be expunged from the list
// of items
template <class T>
void filterOutExpungedItems(
    const QList<qevercloud::Guid> & expungedGuids, QList<T> & items)
{
    if (expungedGuids.isEmpty()) {
        return;
    }

    for (const auto & guid: std::as_const(expungedGuids)) {
        auto it =
            std::find_if(items.begin(), items.end(), [&guid](const T & item) {
                return item.guid() && (*item.guid() == guid);
            });

        if (it != items.end()) {
            items.erase(it);
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

// Merges DownloadNotesStatuses: rhs into lhs.
[[nodiscard]] DownloadNotesStatus mergeDownloadNotesStatuses(
    DownloadNotesStatus lhs, const DownloadNotesStatus & rhs);

// Merges DownloadResourcesStatuses: rhs into lhs.
[[nodiscard]] DownloadResourcesStatus mergeDownloadResourcesStatuses(
    DownloadResourcesStatus lhs, const DownloadResourcesStatus & rhs);

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

// Remove persistent information about processed notes inside the passed in dir
void clearProcessedNotesInfos(const QDir & lastSyncNotesDir);

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

// Persists information about a note which failed to expunge inside the passed
// in dir
void writeFailedToExpungeNote(
    const qevercloud::Guid & noteGuid, const QDir & lastSyncNotesDir);

// Persists information about processed resource inside the passed in dir
void writeProcessedResourceInfo(
    const qevercloud::Guid & resourceGuid, qint32 updateSequenceNum,
    const QDir & lastSyncResourcesDir);

// Remove persistent information about processed resources inside the passed in dir
void clearProcessedResourcesInfos(const QDir & lastSyncResourcesDir);

// Persists information about resource which data failed to get downloaded
// inside the passed in dir
void writeFailedToDownloadResource(
    const qevercloud::Resource & resource, const QDir & lastSyncResourcesDir);

// Persists information about resource which processing has failed for some
// reason inside the passed in dir
void writeFailedToProcessResource(
    const qevercloud::Resource & resource, const QDir & lastSyncResourcesDir);

// Persists information about resource which processing was cancelled inside
// the passed in dir
void writeCancelledResource(
    const qevercloud::Resource & resource, const QDir & lastSyncResourcesDir);

////////////////////////////////////////////////////////////////////////////////

// Functions below retrieve the persistently stored information from the last
// sync. If the last sync was not finished completely and successfully, these
// functions might return non-empty results, otherwise the results would be
// empty.

// Returns a hash from guid to USN for notes which were fully processed
// during the last sync.
[[nodiscard]] QHash<qevercloud::Guid, qint32> processedNotesInfoFromLastSync(
    const QDir & lastSyncNotesDir);

// Returns a list of notes which full content failed to be downloaded during
// the last sync.
[[nodiscard]] QList<qevercloud::Note> notesWhichFailedToDownloadDuringLastSync(
    const QDir & lastSyncNotesDir);

// Returns a list of notes which processing has failed for some reason during
// the last sync.
[[nodiscard]] QList<qevercloud::Note> notesWhichFailedToProcessDuringLastSync(
    const QDir & lastSyncNotesDir);

// Returns a list of notes which processing was cancelled during the last sync
// (because the sync was stopped prematurately for some reason).
[[nodiscard]] QList<qevercloud::Note> notesCancelledDuringLastSync(
    const QDir & lastSyncNotesDir);

// Returns a list of guids of notes which were expunged during the last sync.
[[nodiscard]] QList<qevercloud::Guid> noteGuidsExpungedDuringLastSync(
    const QDir & lastSyncNotesDir);

// Returns a list of guids of notes which failed to get expunged during the last
// sync.
[[nodiscard]] QList<qevercloud::Guid>
    noteGuidsWhichFailedToExpungeDuringLastSync(const QDir & lastSyncNotesDir);

// Returns a hash from guid to USN for resources which were fully processed
// during the last sync.
[[nodiscard]] QHash<qevercloud::Guid, qint32>
    processedResourcesInfoFromLastSync(const QDir & lastSyncResourcesDir);

// Returns a list of resources which full content failed to be downloaded during
// the last sync.
[[nodiscard]] QList<qevercloud::Resource>
    resourcesWhichFailedToDownloadDuringLastSync(
        const QDir & lastSyncResourcesDir);

// Returns a list of resources which processing has failed for some reason
// during the last sync.
[[nodiscard]] QList<qevercloud::Resource>
    resourcesWhichFailedToProcessDuringLastSync(
        const QDir & lastSyncResourcesDir);

// Returns a list of resources which processing was cancelled during the last
// sync (because the sync was stopped prematurately for some reason).
[[nodiscard]] QList<qevercloud::Resource> resourcesCancelledDuringLastSync(
    const QDir & lastSyncResourcesDir);

} // namespace quentier::synchronization::utils
