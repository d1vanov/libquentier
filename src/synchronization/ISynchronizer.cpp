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

namespace quentier::synchronization {

bool operator==(
    const ISynchronizer::Options & lhs,
    const ISynchronizer::Options & rhs) noexcept
{
    return lhs.downloadNoteThumbnails == rhs.downloadNoteThumbnails &&
        lhs.inkNoteImagesStorageDir == rhs.inkNoteImagesStorageDir;
}

bool operator!=(
    const ISynchronizer::Options & lhs,
    const ISynchronizer::Options & rhs) noexcept
{
    return !(lhs == rhs);
}

bool operator==(
    const ISynchronizer::AuthResult & lhs,
    const ISynchronizer::AuthResult & rhs) noexcept
{
    return lhs.userId == rhs.userId && lhs.authToken == rhs.authToken &&
        lhs.authTokenExpirationTime == rhs.authTokenExpirationTime &&
        lhs.shardId == rhs.shardId && lhs.noteStoreUrl == rhs.noteStoreUrl &&
        lhs.webApiUrlPrefix == rhs.webApiUrlPrefix &&
        lhs.userStoreCookies == rhs.userStoreCookies;
}

bool operator!=(
    const ISynchronizer::AuthResult & lhs,
    const ISynchronizer::AuthResult & rhs) noexcept
{
    return !(lhs == rhs);
}

bool operator==(
    const ISynchronizer::SyncState & lhs,
    const ISynchronizer::SyncState & rhs) noexcept
{
    return lhs.updateCount == rhs.updateCount &&
        lhs.lastSyncTime == rhs.lastSyncTime;
}

bool operator!=(
    const ISynchronizer::SyncState & lhs,
    const ISynchronizer::SyncState & rhs) noexcept
{
    return !(lhs == rhs);
}

bool operator==(
    const ISynchronizer::DownloadNotesStatus & lhs,
    const ISynchronizer::DownloadNotesStatus & rhs) noexcept
{
    return lhs.totalNewNotes == rhs.totalNewNotes &&
        lhs.totalUpdatedNotes == rhs.totalUpdatedNotes &&
        lhs.totalExpungedNotes == rhs.totalExpungedNotes &&
        lhs.notesWhichFailedToDownload == rhs.notesWhichFailedToDownload &&
        lhs.notesWhichFailedToProcess == rhs.notesWhichFailedToProcess &&
        lhs.noteGuidsWhichFailedToExpunge ==
        rhs.noteGuidsWhichFailedToExpunge &&
        lhs.processedNoteGuidsAndUsns == rhs.processedNoteGuidsAndUsns &&
        lhs.cancelledNoteGuidsAndUsns == rhs.cancelledNoteGuidsAndUsns &&
        lhs.expungedNoteGuids == rhs.expungedNoteGuids;
}

bool operator!=(
    const ISynchronizer::DownloadNotesStatus & lhs,
    const ISynchronizer::DownloadNotesStatus & rhs) noexcept
{
    return !(lhs == rhs);
}

bool operator==(
    const ISynchronizer::DownloadNotesStatus::NoteWithException & lhs,
    const ISynchronizer::DownloadNotesStatus::NoteWithException & rhs) noexcept
{
    return lhs.note == rhs.note && lhs.exception == rhs.exception;
}

bool operator!=(
    const ISynchronizer::DownloadNotesStatus::NoteWithException & lhs,
    const ISynchronizer::DownloadNotesStatus::NoteWithException & rhs) noexcept
{
    return !(lhs == rhs);
}

bool operator==(
    const ISynchronizer::DownloadNotesStatus::GuidWithException & lhs,
    const ISynchronizer::DownloadNotesStatus::GuidWithException & rhs) noexcept
{
    return lhs.guid == rhs.guid && lhs.exception == rhs.exception;
}

bool operator!=(
    const ISynchronizer::DownloadNotesStatus::GuidWithException & lhs,
    const ISynchronizer::DownloadNotesStatus::GuidWithException & rhs) noexcept
{
    return !(lhs == rhs);
}

bool operator==(
    const ISynchronizer::DownloadNotesStatus::GuidWithUsn & lhs,
    const ISynchronizer::DownloadNotesStatus::GuidWithUsn & rhs) noexcept
{
    return lhs.guid == rhs.guid &&
        lhs.updateSequenceNumber == rhs.updateSequenceNumber;
}

bool operator!=(
    const ISynchronizer::DownloadNotesStatus::GuidWithUsn & lhs,
    const ISynchronizer::DownloadNotesStatus::GuidWithUsn & rhs) noexcept
{
    return !(lhs == rhs);
}

} // namespace quentier::synchronization
