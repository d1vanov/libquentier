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
#include <synchronization/sync_chunks/Utils.h>
#include <synchronization/types/DownloadNotesStatus.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/local_storage/ILocalStorage.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/synchronization/ISyncConflictResolver.h>
#include <quentier/threading/Future.h>
#include <quentier/threading/TrackedTask.h>
#include <quentier/utility/cancelers/ManualCanceler.h>

#include <qevercloud/services/INoteStore.h>
#include <qevercloud/types/SyncChunk.h>

#include <QFutureWatcher>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>

#include <algorithm>
#include <type_traits>

namespace quentier::synchronization {

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

QFuture<DownloadNotesStatusPtr> NotesProcessor::processNotes(
    const QList<qevercloud::SyncChunk> & syncChunks,
    ICallbackWeakPtr callbackWeak)
{
    QNDEBUG("synchronization::NotesProcessor", "NotesProcessor::processNotes");

    QList<qevercloud::Note> notes;
    QList<qevercloud::Guid> expungedNotes;
    for (const auto & syncChunk: qAsConst(syncChunks)) {
        notes << utils::collectNotesFromSyncChunk(syncChunk);

        expungedNotes << utils::collectExpungedNoteGuidsFromSyncChunk(
            syncChunk);
    }

    utils::filterOutExpungedItems(expungedNotes, notes);

    if (notes.isEmpty() && expungedNotes.isEmpty()) {
        QNDEBUG(
            "synchronization::NotesProcessor",
            "No new/updated/expunged notes in the sync chunks");

        return threading::makeReadyFuture<DownloadNotesStatusPtr>(
            std::make_shared<DownloadNotesStatus>());
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

    auto status = std::make_shared<DownloadNotesStatus>();
    status->m_totalExpungedNotes =
        static_cast<quint64>(std::max(expungedNoteCount, 0));

    // Processing of all notes might need to be globally canceled if certain
    // kind of exceptional situation occurs, for example:
    // 1. Evernote API rate limit gets exceeded - once this happens, all further
    //    immediate attempts to download full note data would fail with the same
    //    exception so it doesn't make sense to continue processing
    // 2. Authentication token expires during the attempt to download full note
    //    data - it's pretty unlikely as the first step of sync should ensure
    //    the auth token isn't close to expiration and re-acquire the token if
    //    it is close to expiration. But still need to be able to handle this
    //    situation.
    auto canceler = std::make_shared<utility::cancelers::ManualCanceler>();

    for (const auto & note: qAsConst(notes)) {
        auto notePromise = std::make_shared<QPromise<ProcessNoteStatus>>();
        noteFutures << notePromise->future();
        notePromise->start();

        Q_ASSERT(note.guid());
        Q_ASSERT(note.updateSequenceNum());

        auto findNoteByGuidFuture = m_localStorage->findNoteByGuid(
            *note.guid(),
            FetchNoteOptions{} | FetchNoteOption::WithResourceMetadata);

        auto thenFuture = threading::then(
            std::move(findNoteByGuidFuture),
            threading::TrackedTask{
                selfWeak,
                [this, updatedNote = note, notePromise, status, selfWeak,
                 canceler, callbackWeak](
                    const std::optional<qevercloud::Note> & note) mutable {
                    if (canceler->isCanceled()) {
                        const auto & guid = *updatedNote.guid();
                        status->m_cancelledNoteGuidsAndUsns[guid] =
                            updatedNote.updateSequenceNum().value();

                        if (const auto callback = callbackWeak.lock()) {
                            callback->onNoteProcessingCancelled(updatedNote);
                        }

                        notePromise->addResult(ProcessNoteStatus::Canceled);
                        notePromise->finish();
                        return;
                    }

                    if (note) {
                        ++status->m_totalUpdatedNotes;
                        onFoundDuplicate(
                            notePromise, status, canceler,
                            std::move(callbackWeak), std::move(updatedNote),
                            *note);
                        return;
                    }

                    ++status->m_totalNewNotes;

                    // No duplicate by guid was found, will download full note
                    // data and then put it into the local storage
                    downloadFullNoteData(
                        notePromise, status, canceler, std::move(callbackWeak),
                        updatedNote, NoteKind::NewNote);
                }});

        threading::onFailed(
            std::move(thenFuture),
            [notePromise, status, note,
             callbackWeak](const QException & e) mutable {
                if (const auto callback = callbackWeak.lock()) {
                    callback->onNoteFailedToProcess(note, e);
                }

                status->m_notesWhichFailedToProcess
                    << DownloadNotesStatus::NoteWithException{
                           note, std::shared_ptr<QException>(e.clone())};

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

        auto thenFuture = threading::then(
            std::move(expungeNoteByGuidFuture),
            [guid, status, promise, callbackWeak] {
                status->m_expungedNoteGuids << guid;

                if (const auto callback = callbackWeak.lock()) {
                    callback->onExpungedNote(guid);
                }

                promise->addResult(ProcessNoteStatus::ExpungedNote);
                promise->finish();
            });

        threading::onFailed(
            std::move(thenFuture),
            [promise, status, guid, callbackWeak](const QException & e) {
                status->m_noteGuidsWhichFailedToExpunge
                    << DownloadNotesStatus::GuidWithException{
                           guid, std::shared_ptr<QException>(e.clone())};

                if (const auto callback = callbackWeak.lock()) {
                    callback->onFailedToExpungeNote(guid, e);
                }

                promise->addResult(ProcessNoteStatus::FailedToExpungeNote);
                promise->finish();
            });
    }

    auto allNotesFuture =
        threading::whenAll<ProcessNoteStatus>(std::move(noteFutures));

    auto promise = std::make_shared<QPromise<DownloadNotesStatusPtr>>();
    auto future = promise->future();

    promise->setProgressRange(0, 100);
    promise->setProgressValue(0);
    threading::mapFutureProgress(allNotesFuture, promise);

    promise->start();

    threading::thenOrFailed(
        std::move(allNotesFuture), promise,
        [promise, status](const QList<ProcessNoteStatus> & statuses) mutable {
            Q_UNUSED(statuses)

            promise->addResult(std::move(status));
            promise->finish();
        });

    return future;
}

void NotesProcessor::onFoundDuplicate(
    const std::shared_ptr<QPromise<ProcessNoteStatus>> & notePromise,
    const DownloadNotesStatusPtr & status,
    const utility::cancelers::ManualCancelerPtr & canceler,
    ICallbackWeakPtr && callbackWeak, qevercloud::Note updatedNote,
    qevercloud::Note localNote)
{
    using ConflictResolution = ISyncConflictResolver::ConflictResolution;
    using NoteConflictResolution =
        ISyncConflictResolver::NoteConflictResolution;

    auto localNoteLocalId = localNote.localId();
    const auto localNoteLocallyFavorited = localNote.isLocallyFavorited();

    auto statusFuture = m_syncConflictResolver->resolveNoteConflict(
        updatedNote, std::move(localNote));

    const auto selfWeak = weak_from_this();

    Q_ASSERT(updatedNote.guid());
    Q_ASSERT(updatedNote.updateSequenceNum());

    auto thenFuture = threading::then(
        std::move(statusFuture),
        [this, selfWeak, notePromise, status, updatedNote, localNoteLocalId,
         localNoteLocallyFavorited, canceler, callbackWeak,
         updatedNoteGuid = *updatedNote.guid(),
         updatedNoteUsn = *updatedNote.updateSequenceNum()](
            const NoteConflictResolution & resolution) mutable {
            const auto self = selfWeak.lock();
            if (!self) {
                return;
            }

            if (canceler->isCanceled()) {
                status->m_cancelledNoteGuidsAndUsns[updatedNoteGuid] =
                    updatedNoteUsn;

                if (const auto callback = callbackWeak.lock()) {
                    callback->onNoteProcessingCancelled(updatedNote);
                }

                notePromise->addResult(ProcessNoteStatus::Canceled);
                notePromise->finish();
                return;
            }

            if (std::holds_alternative<ConflictResolution::UseTheirs>(
                    resolution)) {
                updatedNote.setLocalId(localNoteLocalId);
                updatedNote.setLocallyFavorited(localNoteLocallyFavorited);
                downloadFullNoteData(
                    notePromise, status, canceler, std::move(callbackWeak),
                    updatedNote, NoteKind::UpdatedNote);
                return;
            }

            if (std::holds_alternative<ConflictResolution::IgnoreMine>(
                    resolution)) {
                downloadFullNoteData(
                    notePromise, status, canceler, std::move(callbackWeak),
                    updatedNote, NoteKind::NewNote);
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
                        [this, notePromise, status, canceler, callbackWeak,
                         updatedNote = std::move(updatedNote)]() mutable {
                            if (canceler->isCanceled()) {
                                const auto & guid = *updatedNote.guid();
                                status->m_cancelledNoteGuidsAndUsns[guid] =
                                    updatedNote.updateSequenceNum().value();

                                if (const auto callback = callbackWeak.lock()) {
                                    callback->onNoteProcessingCancelled(
                                        updatedNote);
                                }

                                notePromise->addResult(
                                    ProcessNoteStatus::Canceled);

                                notePromise->finish();
                                return;
                            }

                            downloadFullNoteData(
                                notePromise, status, canceler,
                                std::move(callbackWeak), updatedNote,
                                NoteKind::NewNote);
                        }});

                threading::onFailed(
                    std::move(thenFuture),
                    [notePromise, status, callbackWeak,
                     note = mineResolution.mine](const QException & e) mutable {
                        if (const auto callback = callbackWeak.lock()) {
                            callback->onNoteFailedToProcess(note, e);
                        }

                        status->m_notesWhichFailedToProcess
                            << DownloadNotesStatus::NoteWithException{
                                   std::move(note),
                                   std::shared_ptr<QException>(e.clone())};

                        notePromise->addResult(
                            ProcessNoteStatus::FailedToPutNoteToLocalStorage);

                        notePromise->finish();
                    });
            }
        });

    threading::onFailed(
        std::move(thenFuture),
        [notePromise, status, callbackWeak,
         note = std::move(updatedNote)](const QException & e) mutable {
            if (const auto callback = callbackWeak.lock()) {
                callback->onNoteFailedToProcess(note, e);
            }

            status->m_notesWhichFailedToProcess
                << DownloadNotesStatus::NoteWithException{
                       std::move(note), std::shared_ptr<QException>(e.clone())};

            notePromise->addResult(
                ProcessNoteStatus::FailedToResolveNoteConflict);

            notePromise->finish();
        });
}

void NotesProcessor::downloadFullNoteData(
    const std::shared_ptr<QPromise<ProcessNoteStatus>> & notePromise,
    const DownloadNotesStatusPtr & status,
    const utility::cancelers::ManualCancelerPtr & canceler,
    ICallbackWeakPtr && callbackWeak, const qevercloud::Note & note,
    NoteKind noteKind)
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
            [this, notePromise, status, noteKind,
             callbackWeak](qevercloud::Note note) mutable {
                putNoteToLocalStorage(
                    notePromise, status, std::move(callbackWeak),
                    std::move(note), noteKind);
            }});

    threading::onFailed(
        std::move(thenFuture),
        [notePromise, status, note, canceler,
         callbackWeak](const QException & e) {
            status->m_notesWhichFailedToDownload
                << DownloadNotesStatus::NoteWithException{
                       note, std::shared_ptr<QException>(e.clone())};

            if (const auto callback = callbackWeak.lock()) {
                callback->onNoteFailedToDownload(note, e);
            }

            bool shouldCancelProcessing = false;
            try {
                e.raise();
            }
            catch (const qevercloud::EDAMSystemException & se) {
                if ((se.errorCode() ==
                     qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED) ||
                    (se.errorCode() == qevercloud::EDAMErrorCode::AUTH_EXPIRED))
                {
                    shouldCancelProcessing = true;
                }
            }
            catch (...) {
            }

            if (shouldCancelProcessing) {
                canceler->cancel();
            }

            notePromise->addResult(
                ProcessNoteStatus::FailedToDownloadFullNoteData);

            notePromise->finish();
        });
}

void NotesProcessor::putNoteToLocalStorage(
    const std::shared_ptr<QPromise<ProcessNoteStatus>> & notePromise,
    const DownloadNotesStatusPtr & status,
    ICallbackWeakPtr && callbackWeak, qevercloud::Note note,
    NoteKind putNoteKind)
{
    auto putNoteFuture = m_localStorage->putNote(note);

    auto thenFuture = threading::then(
        std::move(putNoteFuture),
        [notePromise, putNoteKind, status, callbackWeak, noteGuid = note.guid(),
         noteUsn = note.updateSequenceNum()] {
            if (noteGuid.has_value() && noteUsn.has_value()) {
                status->m_processedNoteGuidsAndUsns[*noteGuid] = *noteUsn;

                if (const auto callback = callbackWeak.lock()) {
                    callback->onProcessedNote(*noteGuid, *noteUsn);
                }
            }

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
        [notePromise, status, callbackWeak,
         note = std::move(note)](const QException & e) mutable {
            if (const auto callback = callbackWeak.lock()) {
                callback->onNoteFailedToProcess(note, e);
            }

            status->m_notesWhichFailedToProcess
                << DownloadNotesStatus::NoteWithException{
                       std::move(note), std::shared_ptr<QException>(e.clone())};

            notePromise->addResult(
                ProcessNoteStatus::FailedToPutNoteToLocalStorage);

            notePromise->finish();
        });
}

} // namespace quentier::synchronization
