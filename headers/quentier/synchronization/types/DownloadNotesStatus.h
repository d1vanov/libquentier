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

#include <quentier/utility/Linkage.h>
#include <quentier/utility/Printable.h>

#include <qevercloud/types/Note.h>
#include <qevercloud/types/TypeAliases.h>

#include <QException>

#include <memory>
#include <utility>

namespace quentier::synchronization {

struct QUENTIER_EXPORT DownloadNotesStatus : public Printable
{
    QTextStream & print(QTextStream & strm) const override;

    using QExceptionPtr = std::shared_ptr<QException>;
    using NoteWithException = std::pair<qevercloud::Note, QExceptionPtr>;
    using GuidWithException = std::pair<qevercloud::Guid, QExceptionPtr>;
    using UpdateSequenceNumbersByGuid = QHash<qevercloud::Guid, qint32>;

    quint64 totalNewNotes = 0UL;
    quint64 totalUpdatedNotes = 0UL;
    quint64 totalExpungedNotes = 0UL;

    QList<NoteWithException> notesWhichFailedToDownload;
    QList<NoteWithException> notesWhichFailedToProcess;
    QList<GuidWithException> noteGuidsWhichFailedToExpunge;

    UpdateSequenceNumbersByGuid processedNoteGuidsAndUsns;
    UpdateSequenceNumbersByGuid cancelledNoteGuidsAndUsns;
    QList<qevercloud::Guid> expungedNoteGuids;
};

[[nodiscard]] QUENTIER_EXPORT bool operator==(
    const DownloadNotesStatus & lhs, const DownloadNotesStatus & rhs) noexcept;

[[nodiscard]] QUENTIER_EXPORT bool operator!=(
    const DownloadNotesStatus & lhs, const DownloadNotesStatus & rhs) noexcept;

} // namespace quentier::synchronization
