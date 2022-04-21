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

#include "NotesProcessor.h"
#include "Utils.h"

#include <synchronization/processors/INoteFullDataDownloader.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/local_storage/ILocalStorage.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/synchronization/ISyncConflictResolver.h>
#include <quentier/threading/Future.h>
#include <quentier/threading/TrackedTask.h>

#include <qevercloud/services/INoteStore.h>
#include <qevercloud/types/SyncChunk.h>

#include <QFutureWatcher>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>

#include <algorithm>
#include <type_traits>

namespace quentier::synchronization {

namespace {

[[nodiscard]] QList<qevercloud::Note> collectNotes(
    const qevercloud::SyncChunk & syncChunk)
{
    if (!syncChunk.notes() || syncChunk.notes()->isEmpty()) {
        return {};
    }

    QList<qevercloud::Note> notes;
    notes.reserve(syncChunk.notes()->size());
    for (const auto & note: qAsConst(*syncChunk.notes())) {
        if (Q_UNLIKELY(!note.guid())) {
            QNWARNING(
                "synchronization::NotesProcessor",
                "Detected note without guid, skipping it: " << note);
            continue;
        }

        if (Q_UNLIKELY(!note.updateSequenceNum())) {
            QNWARNING(
                "synchronization::NotesProcessor",
                "Detected note without update sequence number, skipping it: "
                    << note);
            continue;
        }

        if (Q_UNLIKELY(!note.notebookGuid())) {
            QNWARNING(
                "synchronization::NotesProcessor",
                "Detected note without notebook guid, skipping it: " << note);
            continue;
        }

        notes << note;
    }

    return notes;
}

[[nodiscard]] QList<qevercloud::Guid> collectExpungedNoteGuids(
    const qevercloud::SyncChunk & syncChunk)
{
    return syncChunk.expungedNotes().value_or(QList<qevercloud::Guid>{});
}

} // namespace

NotesProcessor::NotesProcessor(
    local_storage::ILocalStoragePtr localStorage,
    ISyncConflictResolverPtr syncConflictResolver,
    INoteFullDataDownloaderPtr noteFullDataDownloader,
    qevercloud::INoteStorePtr noteStore) :
    m_localStorage{std::move(localStorage)},
    m_syncConflictResolver{std::move(syncConflictResolver)},
    m_noteFullDataDownloader{std::move(noteFullDataDownloader)},
    m_noteStore{std::move(noteStore)}
{
    if (Q_UNLIKELY(!m_localStorage)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::NotesProcessor",
            "NotesProcessor ctor: local storage is null")}};
    }

    if (Q_UNLIKELY(!m_syncConflictResolver)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::NotesProcessor",
            "NotesProcessor ctor: sync conflict resolver is null")}};
    }

    if (Q_UNLIKELY(!m_noteFullDataDownloader)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::NotesProcessor",
            "NotesProcessor ctor: note full data downloader is null")}};
    }

    if (Q_UNLIKELY(!m_noteStore)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::NotesProcessor",
            "NotesProcessor ctor: note store is null")}};
    }
}

QFuture<INotesProcessor::ProcessNotesStatus> NotesProcessor::processNotes(
    const QList<qevercloud::SyncChunk> & syncChunks)
{
    QNDEBUG("synchronization::NotesProcessor", "NotesProcessor::processNotes");

    QList<qevercloud::Note> notes;
    QList<qevercloud::Guid> expungedNotes;
    for (const auto & syncChunk: qAsConst(syncChunks)) {
        notes << collectNotes(syncChunk);
        expungedNotes << collectExpungedNoteGuids(syncChunk);
    }

    utils::filterOutExpungedItems(expungedNotes, notes);

    if (notes.isEmpty() && expungedNotes.isEmpty()) {
        QNDEBUG(
            "synchronization::NotesProcessor",
            "No new/updated/expunged notes in the sync chunks");

        return threading::makeReadyFuture<ProcessNotesStatus>({});
    }

    const int noteCount = notes.size();
    const int expungedNoteCount = expungedNotes.size();
    const int totalItemCount = noteCount + expungedNoteCount;
    Q_ASSERT(totalItemCount > 0);

    const auto selfWeak = weak_from_this();

    QList<QFuture<ProcessNoteStatus>> noteFutures;
    noteFutures.reserve(noteCount + expungedNoteCount);

    using FetchNoteOptions = local_storage::ILocalStorage::FetchNoteOptions;
    using FetchNoteOption = local_storage::ILocalStorage::FetchNoteOption;

    auto status = std::make_shared<ProcessNotesStatus>();
    status->m_totalExpungedNotes =
        static_cast<quint64>(std::max(expungedNoteCount, 0));

    for (const auto & note: qAsConst(notes)) {
        auto notePromise = std::make_shared<QPromise<ProcessNoteStatus>>();
        noteFutures << notePromise->future();
        notePromise->start();

        Q_ASSERT(note.guid());

        auto findNoteByGuidFuture = m_localStorage->findNoteByGuid(
            *note.guid(),
            FetchNoteOptions{} | FetchNoteOption::WithResourceMetadata);

        auto thenFuture = threading::then(
            std::move(findNoteByGuidFuture),
            threading::TrackedTask{
                selfWeak,
                [this, updatedNote = note, notePromise, status, selfWeak](
                    const std::optional<qevercloud::Note> & note) mutable {
                    if (note) {
                        ++status->m_totalUpdatedNotes;
                        onFoundDuplicate(
                            notePromise, status, std::move(updatedNote), *note);
                        return;
                    }

                    ++status->m_totalNewNotes;

                    // No duplicate by guid was found, will download full note
                    // data and then put it into the local storage
                    downloadFullNoteData(
                        notePromise, status, updatedNote, NoteKind::NewNote);
                }});

        threading::onFailed(
            std::move(thenFuture),
            [notePromise, status, note](const QException & e) {
                status->m_notesWhichFailedToProcess << std::make_pair(
                    note, std::shared_ptr<QException>(e.clone()));

                notePromise->addResult(
                    ProcessNoteStatus::FailedToPutNoteToLocalStorage);

                notePromise->finish();
            });
    }

    for (const auto & guid: qAsConst(expungedNotes)) {
        auto promise = std::make_shared<QPromise<ProcessNoteStatus>>();
        noteFutures << promise->future();
        promise->start();

        auto expungeNoteByGuidFuture = m_localStorage->expungeNoteByGuid(guid);

        auto thenFuture =
            threading::then(std::move(expungeNoteByGuidFuture), [promise] {
                promise->addResult(ProcessNoteStatus::ExpungedNote);
                promise->finish();
            });

        threading::onFailed(
            std::move(thenFuture),
            [promise, status, guid](const QException & e) {
                status->m_noteGuidsWhichFailedToExpunge << std::make_pair(
                    guid, std::shared_ptr<QException>(e.clone()));

                promise->addResult(ProcessNoteStatus::FailedToExpungeNote);
                promise->finish();
            });
    }

    auto allNotesFuture =
        threading::whenAll<ProcessNoteStatus>(std::move(noteFutures));

    auto promise = std::make_shared<QPromise<ProcessNotesStatus>>();
    auto future = promise->future();

    promise->setProgressRange(0, 100);
    promise->setProgressValue(0);
    threading::mapFutureProgress(allNotesFuture, promise);

    promise->start();

    threading::thenOrFailed(
        std::move(allNotesFuture), promise,
        [promise, status](const QList<ProcessNoteStatus> & statuses)
        {
            Q_UNUSED(statuses)

            promise->addResult(*status);
            promise->finish();
        });

    return future;
}

void NotesProcessor::onFoundDuplicate(
    const std::shared_ptr<QPromise<ProcessNoteStatus>> & notePromise,
    const std::shared_ptr<ProcessNotesStatus> & status,
    qevercloud::Note updatedNote, qevercloud::Note localNote)
{
    using ConflictResolution = ISyncConflictResolver::ConflictResolution;
    using NoteConflictResolution =
        ISyncConflictResolver::NoteConflictResolution;

    auto statusFuture = m_syncConflictResolver->resolveNoteConflict(
        updatedNote, std::move(localNote));

    const auto selfWeak = weak_from_this();

    auto thenFuture = threading::then(
        std::move(statusFuture),
        [this, selfWeak, notePromise, status,
         updatedNote](const NoteConflictResolution & resolution) mutable {
            const auto self = selfWeak.lock();
            if (!self) {
                return;
            }

            if (std::holds_alternative<ConflictResolution::UseTheirs>(
                    resolution)) {
                downloadFullNoteData(
                    notePromise, status, updatedNote, NoteKind::UpdatedNote);
                return;
            }

            if (std::holds_alternative<ConflictResolution::IgnoreMine>(
                    resolution)) {
                downloadFullNoteData(
                    notePromise, status, updatedNote, NoteKind::NewNote);
                return;
            }

            if (std::holds_alternative<ConflictResolution::UseMine>(resolution))
            {
                notePromise->addResult(ProcessNoteStatus::IgnoredNote);
                notePromise->finish();
                return;
            }

            if (std::holds_alternative<
                    ConflictResolution::MoveMine<qevercloud::Note>>(resolution))
            {
                const auto & mineResolution =
                    std::get<ConflictResolution::MoveMine<qevercloud::Note>>(
                        resolution);

                auto updateLocalNoteFuture =
                    m_localStorage->putNote(mineResolution.mine);

                auto thenFuture = threading::then(
                    std::move(updateLocalNoteFuture),
                    threading::TrackedTask{
                        selfWeak,
                        [this, notePromise, status,
                         updatedNote = std::move(updatedNote)]() mutable {
                            downloadFullNoteData(
                                notePromise, status, updatedNote,
                                NoteKind::NewNote);
                        }});

                threading::onFailed(
                    std::move(thenFuture),
                    [notePromise, status,
                     note = mineResolution.mine](const QException & e) mutable {
                        status->m_notesWhichFailedToProcess << std::make_pair(
                            std::move(note),
                            std::shared_ptr<QException>(e.clone()));

                        notePromise->addResult(
                            ProcessNoteStatus::FailedToPutNoteToLocalStorage);

                        notePromise->finish();
                    });
            }
        });

    threading::onFailed(
        std::move(thenFuture),
        [notePromise, status,
         note = std::move(updatedNote)](const QException & e) mutable {
            status->m_notesWhichFailedToProcess << std::make_pair(
                std::move(note), std::shared_ptr<QException>(e.clone()));

            notePromise->addResult(
                ProcessNoteStatus::FailedToResolveNoteConflict);

            notePromise->finish();
        });
}

void NotesProcessor::downloadFullNoteData(
    const std::shared_ptr<QPromise<ProcessNoteStatus>> & notePromise,
    const std::shared_ptr<ProcessNotesStatus> & status,
    const qevercloud::Note & note, NoteKind noteKind)
{
    Q_ASSERT(note.guid());

    auto downloadFullNoteDataFuture =
        m_noteFullDataDownloader->downloadFullNoteData(
            *note.guid(),
            m_noteStore->linkedNotebookGuid().has_value()
                ? INoteFullDataDownloader::IncludeNoteLimits::Yes
                : INoteFullDataDownloader::IncludeNoteLimits::No);

    const auto selfWeak = weak_from_this();

    auto thenFuture = threading::then(
        std::move(downloadFullNoteDataFuture),
        threading::TrackedTask{
            selfWeak,
            [this, notePromise, status, noteKind](qevercloud::Note note) {
                putNoteToLocalStorage(
                    notePromise, status, std::move(note), noteKind);
            }});

    threading::onFailed(
        std::move(thenFuture),
        [notePromise, status, note](const QException & e) {
            status->m_notesWhichFailedToDownload
                << std::make_pair(note, std::shared_ptr<QException>(e.clone()));

            notePromise->addResult(
                ProcessNoteStatus::FailedToDownloadFullNoteData);

            notePromise->finish();
        });
}

void NotesProcessor::putNoteToLocalStorage(
    const std::shared_ptr<QPromise<ProcessNoteStatus>> & notePromise,
    const std::shared_ptr<ProcessNotesStatus> & status, qevercloud::Note note,
    NoteKind putNoteKind)
{
    auto putNoteFuture = m_localStorage->putNote(note);

    auto thenFuture =
        threading::then(std::move(putNoteFuture), [notePromise, putNoteKind] {
            if (putNoteKind == NoteKind::NewNote) {
                notePromise->addResult(ProcessNoteStatus::AddedNote);
            }
            else {
                notePromise->addResult(ProcessNoteStatus::UpdatedNote);
            }
            notePromise->finish();
        });

    threading::onFailed(
        std::move(thenFuture),
        [notePromise, status,
         note = std::move(note)](const QException & e) mutable {
            status->m_notesWhichFailedToProcess << std::make_pair(
                std::move(note), std::shared_ptr<QException>(e.clone()));

            notePromise->addResult(
                ProcessNoteStatus::FailedToPutNoteToLocalStorage);

            notePromise->finish();
        });
}

} // namespace quentier::synchronization
