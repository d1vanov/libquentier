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

#include "IDurableNotesProcessor.h"
#include "INotesProcessor.h"

#include <qevercloud/types/Note.h>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include <quentier/threading/Qt5Promise.h>
#endif

#include <QDir>

namespace quentier::synchronization {

class DurableNotesProcessor final :
    public IDurableNotesProcessor,
    public INotesProcessor::ICallback,
    public std::enable_shared_from_this<DurableNotesProcessor>
{
public:
    DurableNotesProcessor(
        INotesProcessorPtr notesProcessor,
        const QDir & syncPersistentStorageDir);

    // IDurableNotesProcessor
    [[nodiscard]] QFuture<DownloadNotesStatus> processNotes(
        const QList<qevercloud::SyncChunk> & syncChunks) override;

private:
    // INotesProcessor::ICallback
    void onProcessedNote(
        const qevercloud::Guid & noteGuid,
        qint32 noteUpdateSequenceNum) noexcept override;

    void onExpungedNote(
        const qevercloud::Guid & noteGuid) noexcept override;

    void onFailedToExpungeNote(
        const qevercloud::Guid & noteGuid,
        const QException & e) noexcept override;

    void onNoteFailedToDownload(
        const qevercloud::Note & note,
        const QException & e) noexcept override;

    void onNoteFailedToProcess(
        const qevercloud::Note & note,
        const QException & e) noexcept override;

    void onNoteProcessingCancelled(
        const qevercloud::Note & note) noexcept override;

private:
    [[nodiscard]] QList<qevercloud::Note> notesFromPreviousSync() const;

    [[nodiscard]] QList<qevercloud::Guid> failedToExpungeNotesFromPreviousSync()
        const;

    [[nodiscard]] QFuture<DownloadNotesStatus> downloadNotesImpl(
        const QList<qevercloud::SyncChunk> & syncChunks,
        QList<qevercloud::Note> previousNotes,
        QList<qevercloud::Guid> previousExpungedNotes);

private:
    const INotesProcessorPtr m_notesProcessor;
    const QDir m_syncNotesDir;
};

} // namespace quentier::synchronization
