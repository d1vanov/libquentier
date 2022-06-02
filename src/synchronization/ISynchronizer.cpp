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
#include <quentier/utility/DateTime.h>

#include <qevercloud/utility/ToRange.h>

namespace quentier::synchronization {

QTextStream & ISynchronizer::Options::print(QTextStream & strm) const
{
    strm << "ISynchronizer::Options: downloadNoteThumbnails = "
         << (downloadNoteThumbnails ? "true" : "false")
         << ", inkNoteImagesStorageDir = "
         << (inkNoteImagesStorageDir ? inkNoteImagesStorageDir->absolutePath()
                                     : QString::fromUtf8("<not set>"));

    return strm;
}

QTextStream & ISynchronizer::AuthResult::print(QTextStream & strm) const
{
    strm << "ISynchronizer::AuthResult: userId = " << userId
         << ", authToken size = " << authToken.size()
         << ", authTokenExpirationTime = "
         << printableDateTimeFromTimestamp(authTokenExpirationTime)
         << ", shardId = " << shardId << ", noteStoreUrl = " << noteStoreUrl
         << ", webApiUrlPrefix = " << webApiUrlPrefix;

    strm << ", userStoreCookies: ";
    if (userStoreCookies.isEmpty()) {
        strm << "<empty>";
    }
    else {
        for (const auto & cookie: qAsConst(userStoreCookies)) {
            strm << "{" << QString::fromUtf8(cookie.toRawForm()) << "};";
        }
    }

    return strm;
}

QTextStream & ISynchronizer::SyncStats::print(QTextStream & strm) const
{
    strm << "ISynchronizer::SyncStats: syncChunksDownloaded = "
         << syncChunksDownloaded
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

QTextStream & ISynchronizer::SyncState::print(QTextStream & strm) const
{
    strm << "ISynchronizer::SyncState: updateCount = " << updateCount
         << ", lastSyncTime = " << printableDateTimeFromTimestamp(lastSyncTime);

    return strm;
}

QTextStream & ISynchronizer::DownloadNotesStatus::print(
    QTextStream & strm) const
{
    strm << "ISynchronizer::DownloadNotesStatus: totalNewNotes = "
         << totalNewNotes << ", totalUpdatedNotes = " << totalUpdatedNotes
         << ", totalExpungedNotes = " << totalExpungedNotes;

    const auto printNoteWithExceptionList =
        [&strm](
            const QList<ISynchronizer::DownloadNotesStatus::NoteWithException> &
                values) {
            if (values.isEmpty()) {
                strm << "<empty>, ";
                return;
            }

            for (const auto & noteWithException: qAsConst(values)) {
                strm << "{" << noteWithException << "};";
            }
            strm << " ";
        };

    strm << ", notesWhichFailedToDownload = ";
    printNoteWithExceptionList(notesWhichFailedToDownload);

    strm << "notesWhichFailedToProcess = ";
    printNoteWithExceptionList(notesWhichFailedToProcess);

    strm << "noteGuidsWhichFailedToExpunge = ";
    if (noteGuidsWhichFailedToExpunge.isEmpty()) {
        strm << "<empty>, ";
    }
    else {
        for (const auto & guidWithException:
             qAsConst(noteGuidsWhichFailedToExpunge)) {
            strm << "{" << guidWithException.guid;
            strm << ": ";

            if (guidWithException.exception) {
                try {
                    guidWithException.exception->raise();
                }
                catch (const QException & e) {
                    strm << e.what();
                }
            }
            else {
                strm << "<no exception info>";
            }

            strm << "};";
        }

        strm << " ";
    }

    const auto printNoteGuidsAndUsns =
        [&strm](const ISynchronizer::DownloadNotesStatus::
                    UpdateSequenceNumbersByGuid & usns) {
            if (usns.isEmpty()) {
                strm << "<empty>, ";
                return;
            }

            for (const auto it: qevercloud::toRange(qAsConst(usns))) {
                strm << "{" << it.key() << ": " << it.value() << "};";
            }
            strm << " ";
        };

    strm << "processedNoteGuidsAndUsns = ";
    printNoteGuidsAndUsns(processedNoteGuidsAndUsns);

    strm << "cancelledNoteGuidsAndUsns = ";
    printNoteGuidsAndUsns(cancelledNoteGuidsAndUsns);

    strm << "expungedNoteGuids = ";
    if (expungedNoteGuids.isEmpty()) {
        strm << "<empty>";
    }
    else {
        for (const auto & guid: qAsConst(expungedNoteGuids)) {
            strm << "{" << guid << "};";
        }
    }

    return strm;
}

QTextStream & ISynchronizer::DownloadNotesStatus::NoteWithException::print(
    QTextStream & strm) const
{
    strm << "ISynchronizer::DownloadNotesStatus::NoteWithException: note = "
         << note << ", exception: ";

    if (exception) {
        try {
            exception->raise();
        }
        catch (const QException & e) {
            strm << e.what();
        }
    }
    else {
        strm << "<no info>";
    }

    return strm;
}

QTextStream & ISynchronizer::DownloadNotesStatus::GuidWithException::print(
    QTextStream & strm) const
{
    strm << "ISynchronizer::DownloadNotesStatus::GuidWithException: guid = "
         << guid << ", exception: ";

    if (exception) {
        try {
            exception->raise();
        }
        catch (const QException & e) {
            strm << e.what();
        }
    }
    else {
        strm << "<no info>";
    }

    return strm;
}

QTextStream & ISynchronizer::DownloadNotesStatus::GuidWithUsn::print(
    QTextStream & strm) const
{
    strm << "ISynchronizer::DownloadNotesStatus::GuidWithUsn: guid = " << guid
         << ", usn = " << updateSequenceNumber;

    return strm;
}

QTextStream & ISynchronizer::DownloadResourcesStatus::print(
    QTextStream & strm) const
{
    strm << "ISynchronizer::DownloadResourcesStatus: "
         << "totalNewResources = " << totalNewResources
         << ", totalUpdatedResources = " << totalUpdatedResources;

    const auto printResourceWithExceptionList =
        [&strm](const QList<
                ISynchronizer::DownloadResourcesStatus::ResourceWithException> &
                    values) {
            if (values.isEmpty()) {
                strm << "<empty>, ";
                return;
            }

            for (const auto & resourceWithException: qAsConst(values)) {
                strm << "{" << resourceWithException << "};";
            }
            strm << " ";
        };

    strm << ", resourcesWhichFailedToDownload = ";
    printResourceWithExceptionList(resourcesWhichFailedToDownload);

    strm << "resourcesWhichFailedToProcess = ";
    printResourceWithExceptionList(resourcesWhichFailedToProcess);

    const auto printResourceGuidsAndUsns =
        [&strm](const ISynchronizer::DownloadResourcesStatus::
                    UpdateSequenceNumbersByGuid & usns) {
            if (usns.isEmpty()) {
                strm << "<empty>, ";
                return;
            }

            for (const auto it: qevercloud::toRange(qAsConst(usns))) {
                strm << "{" << it.key() << ": " << it.value() << "};";
            }
            strm << " ";
        };

    strm << "processedResourceGuidsAndUsns = ";
    printResourceGuidsAndUsns(processedResourceGuidsAndUsns);

    strm << "cancelledResourceGuidsAndUsns = ";
    printResourceGuidsAndUsns(cancelledResourceGuidsAndUsns);

    return strm;
}

QTextStream &
    ISynchronizer::DownloadResourcesStatus::ResourceWithException::print(
        QTextStream & strm) const
{
    strm << "ISynchronizer::DownloadNotesStatus::ResourceWithException: "
            "resource = "
         << resource << ", exception: ";

    if (exception) {
        try {
            exception->raise();
        }
        catch (const QException & e) {
            strm << e.what();
        }
    }
    else {
        strm << "<no info>";
    }

    return strm;
}

QTextStream & ISynchronizer::SyncResult::print(QTextStream & strm) const
{
    strm << "ISynchronizer::SyncResult: userAccountSyncState = "
         << userAccountSyncState << ", linkedNotebookSyncStates = ";

    if (linkedNotebookSyncStates.isEmpty()) {
        strm << "<empty>, ";
    }
    else {
        for (const auto it: qevercloud::toRange(linkedNotebookSyncStates)) {
            strm << "{" << it.key() << ": " << it.value() << "};";
        }
        strm << " ";
    }

    strm << "userAccountDownloadNotesStatus = "
         << userAccountDownloadNotesStatus
         << ", linkedNotebookDownloadNotesStatuses = ";

    if (linkedNotebookDownloadNotesStatuses.isEmpty()) {
        strm << "<empty>, ";
    }
    else {
        for (const auto it:
             qevercloud::toRange(linkedNotebookDownloadNotesStatuses)) {
            strm << "{" << it.key() << ": " << it.value() << "};";
        }
        strm << " ";
    }

    strm << "userAccountDownloadResourcesStatus = "
         << userAccountDownloadResourcesStatus
         << ", linkedNotebookDownloadResourcesStatuses = ";
    if (linkedNotebookDownloadResourcesStatuses.isEmpty()) {
        strm << "<empty>, ";
    }
    else {
        for (const auto it:
             qevercloud::toRange(linkedNotebookDownloadResourcesStatuses))
        {
            strm << "{" << it.key() << ": " << it.value() << "};";
        }
        strm << " ";
    }

    strm << "syncStats = " << syncStats;
    return strm;
}

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
