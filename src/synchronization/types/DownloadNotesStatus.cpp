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

#include "DownloadNotesStatus.h"

#include <qevercloud/utility/ToRange.h>

namespace quentier::synchronization {

quint64 DownloadNotesStatus::totalNewNotes() const noexcept
{
    return m_totalNewNotes;
}

quint64 DownloadNotesStatus::totalUpdatedNotes() const noexcept
{
    return m_totalUpdatedNotes;
}

quint64 DownloadNotesStatus::totalExpungedNotes() const noexcept
{
    return m_totalExpungedNotes;
}

QList<DownloadNotesStatus::NoteWithException>
    DownloadNotesStatus::notesWhichFailedToDownload() const
{
    return m_notesWhichFailedToDownload;
}

QList<DownloadNotesStatus::NoteWithException>
    DownloadNotesStatus::notesWhichFailedToProcess() const
{
    return m_notesWhichFailedToProcess;
}

QList<DownloadNotesStatus::GuidWithException>
    DownloadNotesStatus::noteGuidsWhichFailedToExpunge() const
{
    return m_noteGuidsWhichFailedToExpunge;
}

DownloadNotesStatus::UpdateSequenceNumbersByGuid
    DownloadNotesStatus::processedNoteGuidsAndUsns() const
{
    return m_processedNoteGuidsAndUsns;
}

DownloadNotesStatus::UpdateSequenceNumbersByGuid
    DownloadNotesStatus::cancelledNoteGuidsAndUsns() const
{
    return m_cancelledNoteGuidsAndUsns;
}

QList<qevercloud::Guid> DownloadNotesStatus::expungedNoteGuids() const
{
    return m_expungedNoteGuids;
}

QTextStream & DownloadNotesStatus::print(QTextStream & strm) const
{
    strm << "DownloadNotesStatus: totalNewNotes = " << m_totalNewNotes
         << ", totalUpdatedNotes = " << m_totalUpdatedNotes
         << ", totalExpungedNotes = " << m_totalExpungedNotes;

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
    printNoteWithExceptionList(m_notesWhichFailedToDownload);

    strm << "notesWhichFailedToProcess = ";
    printNoteWithExceptionList(m_notesWhichFailedToProcess);

    strm << "noteGuidsWhichFailedToExpunge = ";
    if (m_noteGuidsWhichFailedToExpunge.isEmpty()) {
        strm << "<empty>, ";
    }
    else {
        for (const auto & guidWithException:
             qAsConst(m_noteGuidsWhichFailedToExpunge)) {
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
    printNoteGuidsAndUsns(m_processedNoteGuidsAndUsns);

    strm << "cancelledNoteGuidsAndUsns = ";
    printNoteGuidsAndUsns(m_cancelledNoteGuidsAndUsns);

    strm << "expungedNoteGuids = ";
    if (m_expungedNoteGuids.isEmpty()) {
        strm << "<empty>";
    }
    else {
        for (const auto & guid: qAsConst(m_expungedNoteGuids)) {
            strm << "{" << guid << "};";
        }
    }

    return strm;
}

bool operator==(
    const DownloadNotesStatus & lhs, const DownloadNotesStatus & rhs) noexcept
{
    return lhs.m_totalNewNotes == rhs.m_totalNewNotes &&
        lhs.m_totalUpdatedNotes == rhs.m_totalUpdatedNotes &&
        lhs.m_totalExpungedNotes == rhs.m_totalExpungedNotes &&
        lhs.m_notesWhichFailedToDownload == rhs.m_notesWhichFailedToDownload &&
        lhs.m_notesWhichFailedToProcess == rhs.m_notesWhichFailedToProcess &&
        lhs.m_noteGuidsWhichFailedToExpunge ==
        rhs.m_noteGuidsWhichFailedToExpunge &&
        lhs.m_processedNoteGuidsAndUsns == rhs.m_processedNoteGuidsAndUsns &&
        lhs.m_cancelledNoteGuidsAndUsns == rhs.m_cancelledNoteGuidsAndUsns &&
        lhs.m_expungedNoteGuids == rhs.m_expungedNoteGuids;
}

bool operator!=(
    const DownloadNotesStatus & lhs, const DownloadNotesStatus & rhs) noexcept
{
    return !(lhs == rhs);
}

} // namespace quentier::synchronization
