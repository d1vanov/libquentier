/*
 * Copyright 2022-2023 Dmitry Ivanov
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

#include <synchronization/INoteStoreProvider.h>
#include <synchronization/processors/INoteFullDataDownloader.h>
#include <synchronization/sync_chunks/Utils.h>
#include <synchronization/types/DownloadNotesStatus.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/OperationCanceled.h>
#include <quentier/local_storage/ILocalStorage.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/synchronization/ISyncConflictResolver.h>
#include <quentier/threading/Factory.h>
#include <quentier/threading/Future.h>
#include <quentier/threading/TrackedTask.h>
#include <quentier/utility/cancelers/AnyOfCanceler.h>
#include <quentier/utility/cancelers/ManualCanceler.h>

#include <qevercloud/services/INoteStore.h>
#include <qevercloud/types/SyncChunk.h>

#include <QFutureWatcher>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>

#include <algorithm>

namespace quentier::synchronization {

NotesProcessor::NotesProcessor(
    local_storage::ILocalStoragePtr localStorage,
    ISyncConflictResolverPtr syncConflictResolver,
    INoteFullDataDownloaderPtr noteFullDataDownloader,
    INoteStoreProviderPtr noteStoreProvider, qevercloud::IRequestContextPtr ctx,
    qevercloud::IRetryPolicyPtr retryPolicy,
    threading::QThreadPoolPtr threadPool) :
    m_localStorage{std::move(localStorage)},
    m_syncConflictResolver{std::move(syncConflictResolver)},
    m_noteFullDataDownloader{std::move(noteFullDataDownloader)},
    m_noteStoreProvider{std::move(noteStoreProvider)}, m_ctx{std::move(ctx)},
    m_retryPolicy{std::move(retryPolicy)},
    m_threadPool{
        threadPool ? std::move(threadPool) : threading::globalThreadPool()}
{
    if (Q_UNLIKELY(!m_localStorage)) {
        throw InvalidArgument{ErrorString{
            QStringLiteral("NotesProcessor ctor: local storage is null")}};
    }

    if (Q_UNLIKELY(!m_syncConflictResolver)) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "NotesProcessor ctor: sync conflict resolver is null")}};
    }

    if (Q_UNLIKELY(!m_noteFullDataDownloader)) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "NotesProcessor ctor: note full data downloader is null")}};
    }

    if (Q_UNLIKELY(!m_noteStoreProvider)) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "NotesProcessor ctor: note store provider is null")}};
    }

    Q_ASSERT(m_threadPool);
}

QFuture<DownloadNotesStatusPtr> NotesProcessor::processNotes(
    const QList<qevercloud::SyncChunk> & syncChunks,
    utility::cancelers::ICancelerPtr canceler, ICallbackWeakPtr callbackWeak)
{
    QNDEBUG("synchronization::NotesProcessor", "NotesProcessor::processNotes");

    Q_ASSERT(canceler);

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

    auto context = std::make_shared<Context>();

    context->status = std::make_shared<DownloadNotesStatus>();
    context->status->m_totalExpungedNotes =
        static_cast<quint64>(std::max(expungedNoteCount, 0));

    context->statusMutex = std::make_shared<QMutex>();

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
    context->manualCanceler =
        std::make_shared<utility::cancelers::ManualCanceler>();

    auto promise = std::make_shared<QPromise<DownloadNotesStatusPtr>>();
    auto future = promise->future();

    context->canceler = std::make_shared<utility::cancelers::AnyOfCanceler>(
        QList<utility::cancelers::ICancelerPtr>{
            context->manualCanceler, std::move(canceler)});

    context->callbackWeak = std::move(callbackWeak);

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
                [this, selfWeak, updatedNote = note, notePromise, context](
                    const std::optional<qevercloud::Note> & note) mutable {
                    if (context->canceler->isCanceled()) {
                        const auto & guid = *updatedNote.guid();
                        if (const auto callback = context->callbackWeak.lock())
                        {
                            callback->onNoteProcessingCancelled(updatedNote);
                        }

                        {
                            const QMutexLocker locker{
                                context->statusMutex.get()};

                            context->status->m_cancelledNoteGuidsAndUsns[guid] =
                                updatedNote.updateSequenceNum().value();
                        }

                        notePromise->addResult(ProcessNoteStatus::Canceled);
                        notePromise->finish();
                        return;
                    }

                    if (note) {
                        {
                            const QMutexLocker locker{
                                context->statusMutex.get()};
                            ++context->status->m_totalUpdatedNotes;
                        }
                        onFoundDuplicate(
                            context, notePromise, std::move(updatedNote),
                            *note);
                        return;
                    }

                    {
                        const QMutexLocker locker{context->statusMutex.get()};
                        ++context->status->m_totalNewNotes;
                    }

                    // No duplicate by guid was found, will download full note
                    // data and then put it into the local storage
                    downloadFullNoteData(
                        context, notePromise, updatedNote, NoteKind::NewNote);
                }});

        threading::onFailed(
            std::move(thenFuture),
            [notePromise, context, note](const QException & e) mutable {
                if (const auto callback = context->callbackWeak.lock()) {
                    callback->onNoteFailedToProcess(note, e);
                }

                {
                    const QMutexLocker locker{context->statusMutex.get()};
                    context->status->m_notesWhichFailedToProcess
                        << DownloadNotesStatus::NoteWithException{
                               note, std::shared_ptr<QException>(e.clone())};
                }

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
            std::move(expungeNoteByGuidFuture), [guid, context, promise] {
                if (const auto callback = context->callbackWeak.lock()) {
                    callback->onExpungedNote(guid);
                }

                {
                    const QMutexLocker locker{context->statusMutex.get()};
                    context->status->m_expungedNoteGuids << guid;
                }

                promise->addResult(ProcessNoteStatus::ExpungedNote);
                promise->finish();
            });

        threading::onFailed(
            std::move(thenFuture),
            [guid, context, promise](const QException & e) {
                if (const auto callback = context->callbackWeak.lock()) {
                    callback->onFailedToExpungeNote(guid, e);
                }

                {
                    const QMutexLocker locker{context->statusMutex.get()};
                    context->status->m_noteGuidsWhichFailedToExpunge
                        << DownloadNotesStatus::GuidWithException{
                               guid, std::shared_ptr<QException>(e.clone())};
                }

                promise->addResult(ProcessNoteStatus::FailedToExpungeNote);
                promise->finish();
            });
    }

    auto allNotesFuture =
        threading::whenAll<ProcessNoteStatus>(std::move(noteFutures));

    promise->setProgressRange(0, 100);
    promise->setProgressValue(0);
    threading::mapFutureProgress(allNotesFuture, promise);

    promise->start();

    threading::thenOrFailed(
        std::move(allNotesFuture), promise,
        [promise, context](const QList<ProcessNoteStatus> & statuses) mutable {
            Q_UNUSED(statuses)

            promise->addResult(std::move(context->status));
            promise->finish();
        });

    return future;
}

void NotesProcessor::onFoundDuplicate(
    const ContextPtr & context,
    const std::shared_ptr<QPromise<ProcessNoteStatus>> & promise,
    qevercloud::Note updatedNote, qevercloud::Note localNote)
{
    Q_ASSERT(context);

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
        [this, selfWeak, promise, context, updatedNote, localNoteLocalId,
         localNoteLocallyFavorited, updatedNoteGuid = *updatedNote.guid(),
         updatedNoteUsn = *updatedNote.updateSequenceNum()](
            const NoteConflictResolution & resolution) mutable {
            const auto self = selfWeak.lock();
            if (!self) {
                return;
            }

            if (context->canceler->isCanceled()) {
                if (const auto callback = context->callbackWeak.lock()) {
                    callback->onNoteProcessingCancelled(updatedNote);
                }

                {
                    const QMutexLocker locker{context->statusMutex.get()};
                    context->status
                        ->m_cancelledNoteGuidsAndUsns[updatedNoteGuid] =
                        updatedNoteUsn;
                }

                promise->addResult(ProcessNoteStatus::Canceled);
                promise->finish();
                return;
            }

            if (std::holds_alternative<ConflictResolution::UseTheirs>(
                    resolution)) {
                updatedNote.setLocalId(localNoteLocalId);
                updatedNote.setLocallyFavorited(localNoteLocallyFavorited);
                downloadFullNoteData(
                    context, promise, updatedNote, NoteKind::UpdatedNote);
                return;
            }

            if (std::holds_alternative<ConflictResolution::IgnoreMine>(
                    resolution)) {
                downloadFullNoteData(
                    context, promise, updatedNote, NoteKind::NewNote);
                return;
            }

            if (std::holds_alternative<ConflictResolution::UseMine>(resolution))
            {
                promise->addResult(ProcessNoteStatus::IgnoredNote);
                promise->finish();
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
                    std::move(updateLocalNoteFuture), m_threadPool.get(),
                    threading::TrackedTask{
                        selfWeak,
                        [this, promise, context,
                         updatedNote = std::move(updatedNote)]() mutable {
                            if (context->canceler->isCanceled()) {
                                const auto & guid = *updatedNote.guid();
                                if (const auto callback =
                                        context->callbackWeak.lock()) {
                                    callback->onNoteProcessingCancelled(
                                        updatedNote);
                                }

                                {
                                    const QMutexLocker locker{
                                        context->statusMutex.get()};

                                    context->status
                                        ->m_cancelledNoteGuidsAndUsns[guid] =
                                        updatedNote.updateSequenceNum().value();
                                }

                                promise->addResult(ProcessNoteStatus::Canceled);

                                promise->finish();
                                return;
                            }

                            downloadFullNoteData(
                                context, promise, updatedNote,
                                NoteKind::NewNote);
                        }});

                threading::onFailed(
                    std::move(thenFuture),
                    [promise, context,
                     note = mineResolution.mine](const QException & e) mutable {
                        if (const auto callback = context->callbackWeak.lock())
                        {
                            callback->onNoteFailedToProcess(note, e);
                        }

                        {
                            const QMutexLocker locker{
                                context->statusMutex.get()};

                            context->status->m_notesWhichFailedToProcess
                                << DownloadNotesStatus::NoteWithException{
                                       std::move(note),
                                       std::shared_ptr<QException>(e.clone())};
                        }

                        promise->addResult(
                            ProcessNoteStatus::FailedToPutNoteToLocalStorage);

                        promise->finish();
                    });
            }
        });

    threading::onFailed(
        std::move(thenFuture),
        [promise, context,
         note = std::move(updatedNote)](const QException & e) mutable {
            if (const auto callback = context->callbackWeak.lock()) {
                callback->onNoteFailedToProcess(note, e);
            }

            {
                const QMutexLocker locker{context->statusMutex.get()};
                context->status->m_notesWhichFailedToProcess
                    << DownloadNotesStatus::NoteWithException{
                           std::move(note),
                           std::shared_ptr<QException>(e.clone())};
            }

            promise->addResult(ProcessNoteStatus::FailedToResolveNoteConflict);

            promise->finish();
        });
}

void NotesProcessor::downloadFullNoteData(
    const ContextPtr & context,
    const std::shared_ptr<QPromise<ProcessNoteStatus>> & promise,
    const qevercloud::Note & note, NoteKind noteKind)
{
    Q_ASSERT(context);
    Q_ASSERT(note.guid());

    auto noteStoreFuture = m_noteStoreProvider->noteStore(
        note.notebookLocalId(), m_ctx, m_retryPolicy);

    const auto selfWeak = weak_from_this();

    auto downloadFullNoteDataFuture = threading::then(
        std::move(noteStoreFuture),
        [promise, context, noteFullDataDownloader = m_noteFullDataDownloader,
         noteGuid = *note.guid()](qevercloud::INoteStorePtr noteStore) {
            Q_ASSERT(noteStore);
            if (context->canceler->isCanceled()) {
                return threading::makeExceptionalFuture<qevercloud::Note>(
                    OperationCanceled{});
            }

            return noteFullDataDownloader->downloadFullNoteData(
                noteGuid, std::move(noteStore));
        });

    auto thenFuture = threading::then(
        std::move(downloadFullNoteDataFuture),
        threading::TrackedTask{
            selfWeak,
            [this, promise, context, noteKind](qevercloud::Note note) mutable {
                putNoteToLocalStorage(
                    context, promise, std::move(note), noteKind);
            }});

    threading::onFailed(
        std::move(thenFuture), [promise, context, note](const QException & e) {
            if (const auto callback = context->callbackWeak.lock()) {
                callback->onNoteFailedToDownload(note, e);
            }

            {
                const QMutexLocker locker{context->statusMutex.get()};
                context->status->m_notesWhichFailedToDownload
                    << DownloadNotesStatus::NoteWithException{
                           note, std::shared_ptr<QException>(e.clone())};
            }

            bool shouldCancelProcessing = false;
            try {
                e.raise();
            }
            catch (const qevercloud::EDAMSystemException & se) {
                if (se.errorCode() ==
                    qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED) {
                    context->status->m_stopSynchronizationError =
                        StopSynchronizationError{
                            RateLimitReachedError{se.rateLimitDuration()}};
                    shouldCancelProcessing = true;
                }
                else if (
                    se.errorCode() == qevercloud::EDAMErrorCode::AUTH_EXPIRED) {
                    context->status->m_stopSynchronizationError =
                        StopSynchronizationError{AuthenticationExpiredError{}};
                    shouldCancelProcessing = true;
                }
            }
            catch (...) {
            }

            if (shouldCancelProcessing) {
                context->manualCanceler->cancel();
            }

            promise->addResult(ProcessNoteStatus::FailedToDownloadFullNoteData);

            promise->finish();
        });
}

void NotesProcessor::putNoteToLocalStorage(
    const ContextPtr & context,
    const std::shared_ptr<QPromise<ProcessNoteStatus>> & promise,
    qevercloud::Note note, NoteKind putNoteKind)
{
    Q_ASSERT(context);

    auto putNoteFuture = m_localStorage->putNote(note);

    auto thenFuture = threading::then(
        std::move(putNoteFuture), m_threadPool.get(),
        [promise, context, putNoteKind, noteGuid = note.guid(),
         noteUsn = note.updateSequenceNum()] {
            if (noteGuid.has_value() && noteUsn.has_value()) {
                if (const auto callback = context->callbackWeak.lock()) {
                    callback->onProcessedNote(*noteGuid, *noteUsn);
                }

                const QMutexLocker locker{context->statusMutex.get()};
                context->status->m_processedNoteGuidsAndUsns[*noteGuid] =
                    *noteUsn;
            }

            if (putNoteKind == NoteKind::NewNote) {
                promise->addResult(ProcessNoteStatus::AddedNote);
            }
            else {
                promise->addResult(ProcessNoteStatus::UpdatedNote);
            }

            promise->finish();
        });

    threading::onFailed(
        std::move(thenFuture),
        [promise, context,
         note = std::move(note)](const QException & e) mutable {
            if (const auto callback = context->callbackWeak.lock()) {
                callback->onNoteFailedToProcess(note, e);
            }

            {
                const QMutexLocker locker{context->statusMutex.get()};
                context->status->m_notesWhichFailedToProcess
                    << DownloadNotesStatus::NoteWithException{
                           std::move(note),
                           std::shared_ptr<QException>(e.clone())};
            }

            promise->addResult(
                ProcessNoteStatus::FailedToPutNoteToLocalStorage);

            promise->finish();
        });
}

} // namespace quentier::synchronization
