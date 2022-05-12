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

#include "INotesDownloader.h"

#include <qevercloud/types/Note.h>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include <quentier/threading/Qt5Promise.h>
#endif

#include <QDir>

namespace quentier::synchronization {

class NotesDownloader final :
    public INotesDownloader,
    public std::enable_shared_from_this<NotesDownloader>
{
public:
    NotesDownloader(
        INotesProcessorPtr notesProcessor, QDir syncPersistentStorageDir);

    [[nodiscard]] QFuture<DownloadNotesStatus> downloadNotes(
        const QList<qevercloud::SyncChunk> & syncChunks) override;

private:
    [[nodiscard]] QList<qevercloud::Note> notesFromPreviousSync() const;
    [[nodiscard]] QList<qevercloud::Guid> expungedNotesFromPreviousSync() const;

    [[nodiscard]] QFuture<DownloadNotesStatus> downloadNotesImpl(
        const QList<qevercloud::SyncChunk> & syncChunks,
        QList<qevercloud::Note> previousNotes,
        QList<qevercloud::Guid> previousExpungedNotes);

    [[nodiscard]] QFuture<DownloadNotesStatus> processNotesFromPreviousSync(
        const QList<qevercloud::Note> & notes);

    void processPostSyncStatus(const DownloadNotesStatus & status);

    [[nodiscard]] DownloadNotesStatus mergeStatus(
        DownloadNotesStatus lhs, const DownloadNotesStatus & rhs) const;

private:
    const INotesProcessorPtr m_notesProcessor;
    const QDir m_syncPersistentStorageDir;
};

} // namespace quentier::synchronization
