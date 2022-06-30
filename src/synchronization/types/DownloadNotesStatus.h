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

#include <quentier/synchronization/types/IDownloadNotesStatus.h>

namespace quentier::synchronization {

struct DownloadNotesStatus final : public IDownloadNotesStatus
{
    [[nodiscard]] quint64 totalNewNotes() const noexcept override;
    [[nodiscard]] quint64 totalUpdatedNotes() const noexcept override;
    [[nodiscard]] quint64 totalExpungedNotes() const noexcept override;

    [[nodiscard]] QList<NoteWithException> notesWhichFailedToDownload()
        const override;

    [[nodiscard]] QList<NoteWithException> notesWhichFailedToProcess()
        const override;

    [[nodiscard]] QList<GuidWithException> noteGuidsWhichFailedToExpunge()
        const override;

    [[nodiscard]] UpdateSequenceNumbersByGuid processedNoteGuidsAndUsns()
        const override;

    [[nodiscard]] UpdateSequenceNumbersByGuid cancelledNoteGuidsAndUsns()
        const override;

    [[nodiscard]] QList<qevercloud::Guid> expungedNoteGuids() const override;

    QTextStream & print(QTextStream & strm) const override;

    quint64 m_totalNewNotes = 0UL;
    quint64 m_totalUpdatedNotes = 0UL;
    quint64 m_totalExpungedNotes = 0UL;

    QList<NoteWithException> m_notesWhichFailedToDownload;
    QList<NoteWithException> m_notesWhichFailedToProcess;
    QList<GuidWithException> m_noteGuidsWhichFailedToExpunge;

    UpdateSequenceNumbersByGuid m_processedNoteGuidsAndUsns;
    UpdateSequenceNumbersByGuid m_cancelledNoteGuidsAndUsns;
    QList<qevercloud::Guid> m_expungedNoteGuids;
};

[[nodiscard]] bool operator==(
    const DownloadNotesStatus & lhs, const DownloadNotesStatus & rhs) noexcept;

[[nodiscard]] bool operator!=(
    const DownloadNotesStatus & lhs, const DownloadNotesStatus & rhs) noexcept;

} // namespace quentier::synchronization
