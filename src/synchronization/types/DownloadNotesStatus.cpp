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

#include <quentier/synchronization/types/DownloadNotesStatus.h>

#include <qevercloud/utility/ToRange.h>

namespace quentier::synchronization {

QTextStream & DownloadNotesStatus::print(QTextStream & strm) const
{
    strm << "DownloadNotesStatus: totalNewNotes = " << totalNewNotes
         << ", totalUpdatedNotes = " << totalUpdatedNotes
         << ", totalExpungedNotes = " << totalExpungedNotes;

    const auto printNoteWithExceptionList =
        [&strm](const QList<DownloadNotesStatus::NoteWithException> & values) {
            if (values.isEmpty()) {
                strm << "<empty>, ";
                return;
            }

            for (const auto & noteWithException: qAsConst(values)) {
                strm << "{note: " << noteWithException.first << "\nException: ";

                if (noteWithException.second) {
                    try {
                        noteWithException.second->raise();
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
            strm << "{" << guidWithException.first;
            strm << ": ";

            if (guidWithException.second) {
                try {
                    guidWithException.second->raise();
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
        [&strm](const DownloadNotesStatus::UpdateSequenceNumbersByGuid & usns) {
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

bool operator==(
    const DownloadNotesStatus & lhs, const DownloadNotesStatus & rhs) noexcept
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
    const DownloadNotesStatus & lhs, const DownloadNotesStatus & rhs) noexcept
{
    return !(lhs == rhs);
}

} // namespace quentier::synchronization
