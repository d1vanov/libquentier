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

#include "NotesDownloader.h"

#include <synchronization/processors/INotesProcessor.h>
#include <synchronization/sync_chunks/Utils.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/threading/QtFutureContinuations.h>
#include <quentier/threading/TrackedTask.h>

#include <qevercloud/types/builders/SyncChunkBuilder.h>

namespace quentier::synchronization {

NotesDownloader::NotesDownloader(
    INotesProcessorPtr notesProcessor,
    QDir syncPersistentStorageDir) : // NOLINT
    m_notesProcessor{std::move(notesProcessor)},
    m_syncPersistentStorageDir{std::move(syncPersistentStorageDir)} // NOLINT
{
    if (Q_UNLIKELY(!m_notesProcessor)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::NotesDownloader",
            "NotesDownloader ctor: notes processor is null")}};
    }
}

QFuture<ISynchronizer::DownloadNotesStatus> NotesDownloader::downloadNotes(
    const QList<qevercloud::SyncChunk> & syncChunks)
{
    // First need to check whether there are notes which failed to be processed
    // or which processing was cancelled. If such notes exist, they need to be
    // processed first.
    auto notes = notesFromPreviousSync();
    auto expungedNotes = expungedNotesFromPreviousSync();

    // TODO: also filter out of sync chunks the notes which were fully
    // processed during the last sync

    return downloadNotesImpl(
        syncChunks, std::move(notes), std::move(expungedNotes));
}

QList<qevercloud::Note> NotesDownloader::notesFromPreviousSync() const
{
    QDir lastSyncDataDir{m_syncPersistentStorageDir.absoluteFilePath(
        QStringLiteral("lastSyncData"))};

    if (!lastSyncDataDir.exists()) {
        return {};
    }

    QList<QDir> noteSubdirs;
    noteSubdirs.reserve(3);
    noteSubdirs << QDir{lastSyncDataDir.absoluteFilePath(
        QStringLiteral("notesWhichFailedToDownload"))};

    noteSubdirs << QDir{lastSyncDataDir.absoluteFilePath(
        QStringLiteral("notesWhichFailedToProcess"))};

    noteSubdirs << QDir{lastSyncDataDir.absoluteFilePath(
        QStringLiteral("notesWhichProcessingWasCancelled"))};

    QList<qevercloud::Note> notes;
    for (const QDir & noteDir: qAsConst(noteSubdirs)) {
        if (!noteDir.exists()) {
            continue;
        }

        const auto fileInfos = noteDir.entryInfoList(
            QDir::Files | QDir::NoDotAndDotDot);

        for (const auto & fileInfo: qAsConst(fileInfos)) {
            // TODO: deserialize of note from file and append to the list
        }
    }

    return notes;
}

QFuture<ISynchronizer::DownloadNotesStatus> NotesDownloader::downloadNotesImpl(
    const QList<qevercloud::SyncChunk> & syncChunks,
    QList<qevercloud::Note> previousNotes,
    QList<qevercloud::Guid> previousExpungedNotes)
{
    const auto selfWeak = weak_from_this();

    auto promise = std::make_shared<QPromise<DownloadNotesStatus>>();
    auto future = promise->future();
    promise->start();

    if (previousNotes.isEmpty() && previousExpungedNotes.isEmpty())
    {
        auto processSyncChunksFuture =
            m_notesProcessor->processNotes(syncChunks);

        threading::thenOrFailed(
            std::move(processSyncChunksFuture),
            promise,
            threading::TrackedTask{
                selfWeak,
                [this, promise](DownloadNotesStatus status)
                {
                    processPostSyncStatus(status);

                    promise->addResult(std::move(status));
                    promise->finish();
                }});

        return future;
    }

    if (!previousExpungedNotes.isEmpty())
    {
        const auto pseudoSyncChunks = QList<qevercloud::SyncChunk>{}
            << qevercloud::SyncChunkBuilder{}
                .setExpungedNotes(std::move(previousExpungedNotes))
                .build();

        auto expungeNotesFuture =
            m_notesProcessor->processNotes(pseudoSyncChunks);

        threading::thenOrFailed(
            std::move(expungeNotesFuture),
            promise,
            threading::TrackedTask{
                selfWeak,
                [this, selfWeak, promise, syncChunks = syncChunks,
                 previousNotes = std::move(previousNotes)](
                    DownloadNotesStatus expungeNotesStatus) mutable
                {
                    auto processNotesFuture = downloadNotesImpl(
                        syncChunks, std::move(previousNotes), {});

                    threading::thenOrFailed(
                        std::move(processNotesFuture),
                        promise,
                        threading::TrackedTask{
                            selfWeak,
                            [this, selfWeak, promise,
                             expungeNotesStatus = std::move(expungeNotesStatus),
                             syncChunks = std::move(syncChunks)](
                                DownloadNotesStatus status) mutable
                            {
                                status = mergeStatus(
                                    std::move(status),
                                    expungeNotesStatus);

                                processPostSyncStatus(status);

                                promise->addResult(std::move(status));
                                promise->finish();
                            }});
                }});

        return future;
    }

    if (!previousNotes.isEmpty()) {
        // TODO: implement this branch
    }

    auto processSyncChunksFuture = m_notesProcessor->processNotes(syncChunks);

    threading::thenOrFailed(
        std::move(processSyncChunksFuture),
        promise,
        threading::TrackedTask{
            selfWeak,
            [this, promise](DownloadNotesStatus status)
            {
                processPostSyncStatus(status);

                promise->addResult(std::move(status));
                promise->finish();
            }});

    return future;
}

} // namespace quentier::synchronization
