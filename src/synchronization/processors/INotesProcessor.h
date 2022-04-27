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
#include <qevercloud/types/Note.h>

#include <QException>
#include <QFuture>
#include <QHash>
#include <QList>

#include <memory>
#include <utility>

namespace quentier::synchronization {

class INotesProcessor
{
public:
    virtual ~INotesProcessor() = default;

    struct ProcessNotesStatus
    {
        quint64 m_totalNewNotes = 0UL;
        quint64 m_totalUpdatedNotes = 0UL;
        quint64 m_totalExpungedNotes = 0UL;

        struct NoteWithException
        {
            qevercloud::Note m_note;
            std::shared_ptr<QException> m_exception;
        };

        struct GuidWithException
        {
            qevercloud::Guid m_guid;
            std::shared_ptr<QException> m_exception;
        };

        using UpdateSequenceNumbersByGuid = QHash<qevercloud::Guid, qint32>;

        struct GuidWithUsn
        {
            qevercloud::Guid m_guid;
            qint32 m_updateSequenceNumber = 0;
        };

        QList<NoteWithException> m_notesWhichFailedToDownload;
        QList<NoteWithException> m_notesWhichFailedToProcess;
        QList<GuidWithException> m_noteGuidsWhichFailedToExpunge;

        UpdateSequenceNumbersByGuid m_processedNoteGuidsAndUsns;
        UpdateSequenceNumbersByGuid m_cancelledNoteGuidsAndUsns;
    };

    [[nodiscard]] virtual QFuture<ProcessNotesStatus> processNotes(
        const QList<qevercloud::SyncChunk> & syncChunks) = 0;
};

} // namespace quentier::synchronization
