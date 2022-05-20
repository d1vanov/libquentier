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
#include <synchronization/processors/Utils.h>
#include <synchronization/sync_chunks/Utils.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/threading/QtFutureContinuations.h>
#include <quentier/threading/TrackedTask.h>

#include <qevercloud/types/builders/SyncChunkBuilder.h>
#include <qevercloud/utility/ToRange.h>

#include <algorithm>
#include <set>

namespace quentier::synchronization {

NotesDownloader::NotesDownloader(
    INotesProcessorPtr notesProcessor, const QDir & syncPersistentStorageDir) :
    m_notesProcessor{std::move(notesProcessor)},
    m_syncNotesDir{[&] {
        QDir lastSyncDataDir{syncPersistentStorageDir.absoluteFilePath(
            QStringLiteral("lastSyncData"))};
        return QDir{lastSyncDataDir.absoluteFilePath(QStringLiteral("notes"))};
    }()}
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
    auto previousNotes = notesFromPreviousSync();
    auto previousExpungedNotes = failedToExpungeNotesFromPreviousSync();

    // Also need to check whether there are notes which were fully processed
    // during the last sync within the sync chunks. If so, such notes should
    // not be processed again.
    const auto alreadyProcessedNotesInfo =
        utils::processedNotesInfoFromLastSync(m_syncNotesDir);

    const auto alreadyExpungedNoteGuids =
        utils::noteGuidsExpungedDuringLastSync(m_syncNotesDir);

    if (alreadyProcessedNotesInfo.isEmpty() &&
        alreadyExpungedNoteGuids.isEmpty()) {
        return downloadNotesImpl(
            syncChunks, std::move(previousNotes),
            std::move(previousExpungedNotes));
    }

    auto filteredSyncChunks = syncChunks;
    for (auto & syncChunk: filteredSyncChunks) {
        if (syncChunk.notes()) {
            auto & notes = *syncChunk.mutableNotes();
            for (auto it = notes.begin(); it != notes.end();) {
                if (Q_UNLIKELY(!it->guid())) {
                    QNWARNING(
                        "synchronization::NotesDownloader",
                        "Detected note within sync chunks without guid: "
                            << *it);
                    ++it;
                    continue;
                }

                const auto processedNoteIt =
                    alreadyProcessedNotesInfo.find(*it->guid());

                if (processedNoteIt != alreadyProcessedNotesInfo.end()) {
                    it = notes.erase(it);
                    continue;
                }

                ++it;
            }
        }

        if (syncChunk.expungedNotes()) {
            auto & expungedNotes = *syncChunk.mutableExpungedNotes();
            for (auto it = expungedNotes.begin(); it != expungedNotes.end();) {
                const auto expungedNoteIt = std::find_if(
                    alreadyExpungedNoteGuids.begin(),
                    alreadyExpungedNoteGuids.end(),
                    [it](const auto & guid) { return guid == *it; });
                if (expungedNoteIt != alreadyExpungedNoteGuids.end()) {
                    it = expungedNotes.erase(it);
                    continue;
                }

                ++it;
            }
        }
    }

    return downloadNotesImpl(
        filteredSyncChunks, std::move(previousNotes),
        std::move(previousExpungedNotes));
}

void NotesDownloader::onProcessedNote(
    const qevercloud::Guid & noteGuid, qint32 noteUpdateSequenceNum) noexcept
{
    try {
        utils::writeProcessedNoteInfo(
            noteGuid, noteUpdateSequenceNum, m_syncNotesDir);
    }
    catch (const std::exception & e) {
        QNWARNING(
            "synchronization::NotesDownloader",
            "Failed to write processed note info: "
                << e.what() << ", note guid = " << noteGuid
                << ", note usn = " << noteUpdateSequenceNum);
    }
    catch (...) {
        QNWARNING(
            "synchronization::NotesDownloader",
            "Failed to write processed note info: unknown exception, "
                << "note guid = " << noteGuid
                << ", note usn = " << noteUpdateSequenceNum);
    }
}

void NotesDownloader::onExpungedNote(const qevercloud::Guid & noteGuid) noexcept
{
    try {
        utils::writeExpungedNote(noteGuid, m_syncNotesDir);
    }
    catch (const std::exception & e) {
        QNWARNING(
            "synchronization::NotesDownloader",
            "Failed to write expunged note guid: "
                << e.what() << ", note guid = " << noteGuid);
    }
    catch (...) {
        QNWARNING(
            "synchronization::NotesDownloader",
            "Failed to write expunged note guid: unknown exception, "
                << "note guid = " << noteGuid);
    }
}

void NotesDownloader::onFailedToExpungeNote(
    const qevercloud::Guid & noteGuid, const QException & e) noexcept
{
    Q_UNUSED(e)

    try {
        utils::writeFailedToExpungeNote(noteGuid, m_syncNotesDir);
    }
    catch (const std::exception & e) {
        QNWARNING(
            "synchronization::NotesDownloader",
            "Failed to write failed to expunge note guid: "
                << e.what() << ", note guid = " << noteGuid);
    }
    catch (...) {
        QNWARNING(
            "synchronization::NotesDownloader",
            "Failed to write failed to expunge note guid: unknown exception, "
                << "note guid = " << noteGuid);
    }
}

void NotesDownloader::onNoteFailedToDownload(
    const qevercloud::Note & note, const QException & e) noexcept
{
    Q_UNUSED(e)

    try {
        utils::writeFailedToDownloadNote(note, m_syncNotesDir);
    }
    catch (const std::exception & e) {
        QNWARNING(
            "synchronization::NotesDownloader",
            "Failed to write failed to download note: " << e.what()
                                                        << ", note: " << note);
    }
    catch (...) {
        QNWARNING(
            "synchronization::NotesDownloader",
            "Failed to write failed to download note: unknown exception, "
                << "note: " << note);
    }
}

void NotesDownloader::onNoteFailedToProcess(
    const qevercloud::Note & note, const QException & e) noexcept
{
    Q_UNUSED(e)

    try {
        utils::writeFailedToProcessNote(note, m_syncNotesDir);
    }
    catch (const std::exception & e) {
        QNWARNING(
            "synchronization::NotesDownloader",
            "Failed to write failed to process note: " << e.what()
                                                       << ", note: " << note);
    }
    catch (...) {
        QNWARNING(
            "synchronization::NotesDownloader",
            "Failed to write failed to process note: unknown exception, "
                << "note: " << note);
    }
}

void NotesDownloader::onNoteProcessingCancelled(
    const qevercloud::Note & note) noexcept
{
    try {
        utils::writeCancelledNote(note, m_syncNotesDir);
    }
    catch (const std::exception & e) {
        QNWARNING(
            "synchronization::NotesDownloader",
            "Failed to write cancelled note: " << e.what()
                                               << ", note: " << note);
    }
    catch (...) {
        QNWARNING(
            "synchronization::NotesDownloader",
            "Failed to write cancelled note: unknown exception, note: "
                << note);
    }
}

QList<qevercloud::Note> NotesDownloader::notesFromPreviousSync() const
{
    if (!m_syncNotesDir.exists()) {
        return {};
    }

    QList<qevercloud::Note> result;
    result << utils::notesWhichFailedToDownloadDuringLastSync(m_syncNotesDir);
    result << utils::notesWhichFailedToProcessDuringLastSync(m_syncNotesDir);
    result << utils::notesCancelledDuringLastSync(m_syncNotesDir);
    return result;
}

QList<qevercloud::Guid> NotesDownloader::failedToExpungeNotesFromPreviousSync()
    const
{
    if (!m_syncNotesDir.exists()) {
        return {};
    }

    return utils::noteGuidsWhichFailedToExpungeDuringLastSync(m_syncNotesDir);
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

    if (previousNotes.isEmpty() && previousExpungedNotes.isEmpty()) {
        auto processSyncChunksFuture =
            m_notesProcessor->processNotes(syncChunks, selfWeak);

        threading::thenOrFailed(
            std::move(processSyncChunksFuture), promise,
            [promise](DownloadNotesStatus status) {
                promise->addResult(std::move(status));
                promise->finish();
            });

        return future;
    }

    if (!previousExpungedNotes.isEmpty()) {
        const auto pseudoSyncChunks = QList<qevercloud::SyncChunk>{}
            << qevercloud::SyncChunkBuilder{}
                   .setExpungedNotes(std::move(previousExpungedNotes))
                   .build();

        auto expungeNotesFuture =
            m_notesProcessor->processNotes(pseudoSyncChunks, selfWeak);

        threading::thenOrFailed(
            std::move(expungeNotesFuture), promise,
            threading::TrackedTask{
                selfWeak,
                [this, selfWeak, promise, syncChunks = syncChunks,
                 previousNotes = std::move(previousNotes)](
                    DownloadNotesStatus expungeNotesStatus) mutable {
                    auto processNotesFuture = downloadNotesImpl(
                        syncChunks, std::move(previousNotes), {});

                    threading::thenOrFailed(
                        std::move(processNotesFuture), promise,
                        threading::TrackedTask{
                            selfWeak,
                            [this, selfWeak, promise,
                             expungeNotesStatus = std::move(expungeNotesStatus),
                             syncChunks = std::move(syncChunks)](
                                DownloadNotesStatus status) mutable {
                                status = mergeStatus(
                                    std::move(status), expungeNotesStatus);

                                promise->addResult(std::move(status));
                                promise->finish();
                            }});
                }});

        return future;
    }

    if (!previousNotes.isEmpty()) {
        const auto pseudoSyncChunks = QList<qevercloud::SyncChunk>{}
            << qevercloud::SyncChunkBuilder{}
                   .setNotes(std::move(previousNotes))
                   .build();

        auto notesFuture =
            m_notesProcessor->processNotes(pseudoSyncChunks, selfWeak);

        threading::thenOrFailed(
            std::move(notesFuture), promise,
            threading::TrackedTask{
                selfWeak,
                [this, selfWeak, promise,
                 syncChunks = syncChunks](DownloadNotesStatus status) mutable {
                    auto processNotesFuture =
                        downloadNotesImpl(syncChunks, {}, {});

                    threading::thenOrFailed(
                        std::move(processNotesFuture), promise,
                        threading::TrackedTask{
                            selfWeak,
                            [this, selfWeak, promise,
                             processNotesStatus = std::move(status)](
                                DownloadNotesStatus status) mutable {
                                status = mergeStatus(
                                    std::move(status), processNotesStatus);

                                promise->addResult(std::move(status));
                                promise->finish();
                            }});
                }});
    }

    auto processSyncChunksFuture =
        m_notesProcessor->processNotes(syncChunks, selfWeak);

    threading::thenOrFailed(
        std::move(processSyncChunksFuture), promise,
        [promise](DownloadNotesStatus status) {
            promise->addResult(std::move(status));
            promise->finish();
        });

    return future;
}

NotesDownloader::DownloadNotesStatus NotesDownloader::mergeStatus(
    DownloadNotesStatus lhs, const DownloadNotesStatus & rhs) const
{
    lhs.totalNewNotes += rhs.totalNewNotes;
    lhs.totalUpdatedNotes += rhs.totalUpdatedNotes;
    lhs.totalExpungedNotes += rhs.totalExpungedNotes;

    const auto mergeNoteLists =
        [](QList<DownloadNotesStatus::NoteWithException> lhs,
           const QList<DownloadNotesStatus::NoteWithException> & rhs) {
            using Iter =
                QList<DownloadNotesStatus::NoteWithException>::const_iterator;
            QHash<qevercloud::Guid, Iter> rhsNotesByGuid;
            rhsNotesByGuid.reserve(rhs.size());
            for (auto it = rhs.constBegin(); it != rhs.constEnd(); ++it) {
                const auto & noteWithException = *it;
                const auto & note = noteWithException.note;
                if (Q_UNLIKELY(!note.guid())) {
                    continue;
                }
                rhsNotesByGuid[*note.guid()] = it;
            }

            std::set<Iter> replacedIters;
            for (auto it = lhs.begin(); it != lhs.end();) {
                if (Q_UNLIKELY(!it->note.guid())) {
                    it = lhs.erase(it);
                    continue;
                }

                const auto rit = rhsNotesByGuid.find(*it->note.guid());
                if (rit != rhsNotesByGuid.end()) {
                    *it = *(*rit);
                    replacedIters.insert(*rit);
                }

                ++it;
            }

            for (auto it = rhs.constBegin(); it != rhs.constEnd(); ++it) {
                if (replacedIters.find(it) != replacedIters.end()) {
                    continue;
                }

                lhs << *it;
            }

            return lhs;
        };

    lhs.notesWhichFailedToDownload = mergeNoteLists(
        lhs.notesWhichFailedToDownload, rhs.notesWhichFailedToDownload);

    lhs.notesWhichFailedToProcess = mergeNoteLists(
        lhs.notesWhichFailedToProcess, rhs.notesWhichFailedToProcess);

    lhs.noteGuidsWhichFailedToExpunge << rhs.noteGuidsWhichFailedToExpunge;
    lhs.noteGuidsWhichFailedToExpunge.erase(
        std::unique(
            lhs.noteGuidsWhichFailedToExpunge.begin(),
            lhs.noteGuidsWhichFailedToExpunge.end(),
            [](const auto & lhs, const auto & rhs) {
                return lhs.guid == rhs.guid;
            }),
        lhs.noteGuidsWhichFailedToExpunge.end());

    for (const auto it: qevercloud::toRange(rhs.processedNoteGuidsAndUsns)) {
        lhs.processedNoteGuidsAndUsns[it.key()] = it.value();
    }

    for (const auto it: qevercloud::toRange(rhs.cancelledNoteGuidsAndUsns)) {
        lhs.cancelledNoteGuidsAndUsns[it.key()] = it.value();
    }

    lhs.expungedNoteGuids << rhs.expungedNoteGuids;
    lhs.expungedNoteGuids.erase(
        std::unique(lhs.expungedNoteGuids.begin(), lhs.expungedNoteGuids.end()),
        lhs.expungedNoteGuids.end());

    return lhs;
}

} // namespace quentier::synchronization
