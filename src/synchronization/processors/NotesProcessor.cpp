/*
 * Copyright 2022-2024 Dmitry Ivanov
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

#include <synchronization/IInkNoteImageDownloaderFactory.h>
#include <synchronization/INoteStoreProvider.h>
#include <synchronization/INoteThumbnailDownloaderFactory.h>
#include <synchronization/processors/INoteFullDataDownloader.h>
#include <synchronization/sync_chunks/Utils.h>
#include <synchronization/types/DownloadNotesStatus.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/OperationCanceled.h>
#include <quentier/exception/RuntimeError.h>
#include <quentier/local_storage/ILocalStorage.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/synchronization/ISyncConflictResolver.h>
#include <quentier/synchronization/types/ISyncOptions.h>
#include <quentier/threading/Factory.h>
#include <quentier/threading/Future.h>
#include <quentier/threading/TrackedTask.h>
#include <quentier/types/NoteUtils.h>
#include <quentier/utility/cancelers/AnyOfCanceler.h>
#include <quentier/utility/cancelers/ManualCanceler.h>

#include <qevercloud/IInkNoteImageDownloader.h>
#include <qevercloud/INoteThumbnailDownloader.h>
#include <qevercloud/services/INoteStore.h>
#include <qevercloud/types/SyncChunk.h>

#include <QDebug>
#include <QFile>
#include <QFutureWatcher>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QTextStream>
#include <QThread>

#include <algorithm>
#include <utility>

namespace quentier::synchronization {

namespace {

[[nodiscard]] std::optional<qevercloud::Resource> inkNoteResource(
    const qevercloud::Note & note)
{
    if (!note.resources()) {
        return std::nullopt;
    }

    for (const auto & resource: std::as_const(*note.resources())) {
        if (resource.guid() && resource.mime() && resource.width() &&
            resource.height() &&
            (*resource.mime() ==
             QStringLiteral("application/vnd.evernote.ink")))
        {
            return resource;
        }
    }

    return std::nullopt;
}

} // namespace

NotesProcessor::NotesProcessor(
    local_storage::ILocalStoragePtr localStorage,
    ISyncConflictResolverPtr syncConflictResolver,
    INoteFullDataDownloaderPtr noteFullDataDownloader,
    INoteStoreProviderPtr noteStoreProvider,
    IInkNoteImageDownloaderFactoryPtr inkNoteImageDownloaderFactory,
    INoteThumbnailDownloaderFactoryPtr noteThumbnailDownloaderFactory,
    ISyncOptionsPtr syncOptions, qevercloud::IRetryPolicyPtr retryPolicy) :
    m_localStorage{std::move(localStorage)},
    m_syncConflictResolver{std::move(syncConflictResolver)},
    m_noteFullDataDownloader{std::move(noteFullDataDownloader)},
    m_noteStoreProvider{std::move(noteStoreProvider)},
    m_inkNoteImageDownloaderFactory{std::move(inkNoteImageDownloaderFactory)},
    m_noteThumbnailDownloaderFactory{std::move(noteThumbnailDownloaderFactory)},
    m_syncOptions{std::move(syncOptions)}, m_retryPolicy{std::move(retryPolicy)}
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

    if (Q_UNLIKELY(!m_inkNoteImageDownloaderFactory)) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "NotesProcessor ctor: ink note image downloader factory is null")}};
    }

    if (Q_UNLIKELY(!m_noteThumbnailDownloaderFactory)) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "NotesProcessor ctor: note thumbnail downloader factory is null")}};
    }

    if (Q_UNLIKELY(!m_syncOptions)) {
        throw InvalidArgument{ErrorString{
            QStringLiteral("NotesProcessor ctor: sync options are null")}};
    }
}

QFuture<DownloadNotesStatusPtr> NotesProcessor::processNotes(
    const QList<qevercloud::SyncChunk> & syncChunks,
    utility::cancelers::ICancelerPtr canceler,
    qevercloud::IRequestContextPtr ctx, ICallbackWeakPtr callbackWeak)
{
    QNDEBUG("synchronization::NotesProcessor", "NotesProcessor::processNotes");

    Q_ASSERT(canceler);

    QList<qevercloud::Note> notes;
    QList<qevercloud::Guid> expungedNotes;
    for (const auto & syncChunk: std::as_const(syncChunks)) {
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

    const auto noteCount = notes.size();
    const auto expungedNoteCount = expungedNotes.size();

    QNDEBUG(
        "synchronization::NotesProcessor",
        "NotesProcessor::processNotes: " << noteCount << " notes to process, "
                                         << expungedNoteCount
                                         << " notes to expunge");

    const auto totalItemCount = noteCount + expungedNoteCount;
    Q_ASSERT(totalItemCount > 0);

    const auto selfWeak = weak_from_this();
    auto * currentThread = QThread::currentThread();

    QList<QFuture<ProcessNoteStatus>> noteFutures;
    noteFutures.reserve(noteCount + expungedNoteCount);

    using FetchNoteOptions = local_storage::ILocalStorage::FetchNoteOptions;
    using FetchNoteOption = local_storage::ILocalStorage::FetchNoteOption;

    auto context = std::make_shared<Context>();
    context->ctx = std::move(ctx);

    context->status = std::make_shared<DownloadNotesStatus>();
    context->status->m_totalExpungedNotes =
        static_cast<quint64>(std::max<qint64>(expungedNoteCount, 0));

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

    for (const auto & note: std::as_const(notes)) {
        auto notePromise = std::make_shared<QPromise<ProcessNoteStatus>>();
        noteFutures << notePromise->future();
        notePromise->start();

        Q_ASSERT(note.guid());
        Q_ASSERT(note.updateSequenceNum());

        auto findNoteByGuidFuture = m_localStorage->findNoteByGuid(
            *note.guid(),
            FetchNoteOptions{} | FetchNoteOption::WithResourceMetadata);

        auto thenFuture = threading::then(
            std::move(findNoteByGuidFuture), currentThread,
            threading::TrackedTask{
                selfWeak,
                [this, selfWeak, updatedNote = note, notePromise, context](
                    const std::optional<qevercloud::Note> & note) mutable {
                    if (context->canceler->isCanceled()) {
                        NotesProcessor::cancelNoteProcessing(
                            context, notePromise, updatedNote);
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
            std::move(thenFuture), currentThread,
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

    for (const auto & guid: std::as_const(expungedNotes)) {
        auto promise = std::make_shared<QPromise<ProcessNoteStatus>>();
        noteFutures << promise->future();
        promise->start();

        auto expungeNoteByGuidFuture = m_localStorage->expungeNoteByGuid(guid);

        auto thenFuture = threading::then(
            std::move(expungeNoteByGuidFuture), currentThread,
            [guid, context, promise] {
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
            std::move(thenFuture), currentThread,
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
        std::move(allNotesFuture), currentThread, promise,
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
    auto * currentThread = QThread::currentThread();

    Q_ASSERT(updatedNote.guid());
    Q_ASSERT(updatedNote.updateSequenceNum());

    QNDEBUG(
        "synchronization::NotesProcessor",
        "NotesProcessor::onFoundDuplicate: found local note which matches "
            << "with the updated note by guid: " << *updatedNote.guid()
            << ", local id = " << localNoteLocalId);

    auto thenFuture = threading::then(
        std::move(statusFuture), currentThread,
        [this, selfWeak, promise, context, updatedNote, localNoteLocalId,
         localNoteLocallyFavorited, updatedNoteGuid = *updatedNote.guid(),
         updatedNoteUsn = *updatedNote.updateSequenceNum(),
         currentThread](const NoteConflictResolution & resolution) mutable {
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

            QNDEBUG(
                "synchronization::NotesProcessor",
                "Notes conflict resolution: " << resolution);

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
                    std::move(updateLocalNoteFuture), currentThread,
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
                    std::move(thenFuture), currentThread,
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
        std::move(thenFuture), currentThread,
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
    qevercloud::Note note, const NoteKind noteKind)
{
    Q_ASSERT(context);
    Q_ASSERT(note.guid());
    Q_ASSERT(note.notebookGuid());

    QNDEBUG(
        "synchronization::NotesProcessor",
        "NotesProcessor::downloadFullNoteData: note guid = "
            << *note.guid() << ", notebook guid = " << *note.notebookGuid()
            << ", note kind = " << noteKind);

    auto noteStoreFuture = m_noteStoreProvider->noteStoreForNotebookGuid(
        *note.notebookGuid(), context->ctx, m_retryPolicy);

    const auto selfWeak = weak_from_this();
    auto * currentThread = QThread::currentThread();

    auto thenFuture = threading::then(
        std::move(noteStoreFuture), currentThread,
        threading::TrackedTask{
            selfWeak,
            [this, context, promise, note, noteKind,
             noteFullDataDownloader = m_noteFullDataDownloader](
                qevercloud::INoteStorePtr noteStore) mutable {
                downloadFullNoteDataImpl(
                    context, promise, note, noteKind, std::move(noteStore));
            }});

    threading::onFailed(
        std::move(thenFuture), currentThread,
        [promise, context, note = std::move(note)](const QException & e) {
            NotesProcessor::processNoteDownloadingError(
                context, promise, note, e);
        });
}

void NotesProcessor::downloadFullNoteDataImpl(
    const ContextPtr & context,
    const std::shared_ptr<QPromise<ProcessNoteStatus>> & promise,
    const qevercloud::Note & note, const NoteKind noteKind,
    qevercloud::INoteStorePtr noteStore)
{
    Q_ASSERT(noteStore);
    Q_ASSERT(note.guid());
    Q_ASSERT(note.notebookGuid());
    Q_ASSERT(note.updateSequenceNum());

    const auto & noteGuid = *note.guid();

    if (context->canceler->isCanceled()) {
        NotesProcessor::cancelNoteProcessing(context, promise, note);
        return;
    }

    QNDEBUG(
        "synchronization::NotesProcessor",
        "NotesProcessor::downloadFullNoteDataImpl: note guid = "
            << noteGuid << ", note usn = " << *note.updateSequenceNum()
            << ", note local id = " << note.localId() << ", note store url = "
            << noteStore->noteStoreUrl() << ", linked notebook guid = "
            << noteStore->linkedNotebookGuid().value_or(
                   QStringLiteral("<none>")));

    auto noteFuture = m_noteFullDataDownloader->downloadFullNoteData(
        noteGuid, std::move(noteStore), context->ctx);

    const auto selfWeak = weak_from_this();

    // Need to preserve local ids of note itself and all of its resources as
    // note full data downloader would substitute its own auto-generated local
    // ids.
    QString noteLocalId = note.localId();
    QHash<qevercloud::Guid, QString> resourceLocalIdsByGuids = [&] {
        QHash<qevercloud::Guid, QString> result;
        if (note.resources() && !note.resources()->isEmpty()) {
            for (const auto & resource: std::as_const(*note.resources())) {
                Q_ASSERT(resource.guid());
                result[*resource.guid()] = resource.localId();
            }
        }
        return result;
    }();

    auto thenFuture = threading::then(
        std::move(noteFuture),
        threading::TrackedTask{
            selfWeak,
            [this, selfWeak, promise, context, noteKind,
             noteLocalId = std::move(noteLocalId),
             resourceLocalIdsByGuids = std::move(resourceLocalIdsByGuids)](
                qevercloud::Note note) mutable {
                QNDEBUG(
                    "synchronization::NotesProcessor",
                    "Successfully downloaded note: guid = "
                        << note.guid().value_or(QStringLiteral("<none>"))
                        << ", local id = " << noteLocalId);

                note.setLocalId(std::move(noteLocalId));
                if (note.resources() && !note.resources()->isEmpty()) {
                    for (auto & resource: *note.mutableResources()) {
                        resource.setNoteLocalId(note.localId());
                        Q_ASSERT(resource.guid());
                        const auto it =
                            resourceLocalIdsByGuids.constFind(*resource.guid());
                        if (it != resourceLocalIdsByGuids.constEnd()) {
                            resource.setLocalId(it.value());
                        }
                        else {
                            QNWARNING(
                                "synchronization::NotesProcessor",
                                "Detected note resource which metadata wasn't "
                                    << "present in the note before note's full "
                                    << "data was downloaded: " << resource);
                        }
                    }
                }
                processDownloadedFullNoteData(
                    context, promise, std::move(note), noteKind);
            }});

    threading::onFailed(
        std::move(thenFuture), [promise, context, note](const QException & e) {
            NotesProcessor::processNoteDownloadingError(
                context, promise, note, e);
        });
}

void NotesProcessor::processDownloadedFullNoteData(
    const ContextPtr & context,
    const std::shared_ptr<QPromise<ProcessNoteStatus>> & promise,
    qevercloud::Note note, const NoteKind noteKind)
{
    Q_ASSERT(note.guid());
    Q_ASSERT(note.notebookGuid());

    QNDEBUG(
        "synchronization::NotesProcessor",
        "NotesProcessor::processDownloadedFullNoteData: note guid = "
            << *note.guid() << ", notebook guid = " << *note.notebookGuid());

    auto * currentThread = QThread::currentThread();

    QFuture<qevercloud::Note> noteFuture = [&] {
        if (!m_syncOptions->downloadNoteThumbnails() || !note.resources() ||
            note.resources()->isEmpty())
        {
            return threading::makeReadyFuture<qevercloud::Note>(
                std::move(note));
        }

        auto notePromise = std::make_shared<QPromise<qevercloud::Note>>();
        notePromise->start();
        auto future = notePromise->future();

        auto noteFuture = downloadNoteThumbnail(context, note);
        auto thenFuture = threading::then(
            std::move(noteFuture), currentThread,
            [notePromise](qevercloud::Note noteWithThumbnail) {
                notePromise->addResult(std::move(noteWithThumbnail));
                notePromise->finish();
            });

        // Even if note thumbnail downloading fails, we tolerate
        // this error and just have this note without thumbnail
        threading::onFailed(
            std::move(thenFuture), currentThread,
            [notePromise,
             note = std::move(note)](const QException & e) mutable {
                QNWARNING(
                    "synchronization::NotesProcessor",
                    "Failed to download thumbnail for note with guid "
                        << *note.guid() << ": " << e.what());
                notePromise->addResult(std::move(note));
                notePromise->finish();
            });

        return future;
    }();

    const auto selfWeak = weak_from_this();
    threading::thenOrFailed(
        std::move(noteFuture), currentThread, promise,
        threading::TrackedTask{
            selfWeak,
            [this, selfWeak, context, promise, currentThread,
             noteKind](qevercloud::Note note) mutable {
                Q_ASSERT(note.guid());
                Q_ASSERT(note.notebookGuid());
                const std::optional<QDir> & inkNoteImagesStorageDir =
                    m_syncOptions->inkNoteImagesStorageDir();
                if (inkNoteImagesStorageDir) {
                    auto inkResource = inkNoteResource(note);
                    if (inkResource) {
                        auto future = downloadInkNoteImage(
                            context, *note.notebookGuid(),
                            std::move(*inkResource), *inkNoteImagesStorageDir);

                        auto thenFuture = threading::then(
                            std::move(future), currentThread,
                            threading::TrackedTask{
                                selfWeak,
                                [this, context, promise, note,
                                 noteKind]() mutable {
                                    putNoteToLocalStorage(
                                        context, promise, std::move(note),
                                        noteKind);
                                }});

                        threading::onFailed(
                            std::move(thenFuture), currentThread,
                            [this, selfWeak, context, promise, note,
                             noteKind](const QException & e) mutable {
                                const auto self = selfWeak.lock();
                                if (!self) {
                                    return;
                                }

                                QNWARNING(
                                    "synchronization::NotesProcessor",
                                    "Failed to download ink note image for note"
                                        << " with guid "
                                        << note.guid().value_or(
                                               QStringLiteral("<empty>"))
                                        << ": " << e.what());

                                // Ignoring this error and saving the note to
                                // the local storage anyway
                                putNoteToLocalStorage(
                                    context, promise, std::move(note),
                                    noteKind);
                            });

                        return;
                    }
                }

                putNoteToLocalStorage(
                    context, promise, std::move(note), noteKind);
            }});
}

void NotesProcessor::cancelNoteProcessing(
    const ContextPtr & context,
    const std::shared_ptr<QPromise<ProcessNoteStatus>> & promise,
    const qevercloud::Note & note)
{
    Q_ASSERT(note.guid());
    Q_ASSERT(note.updateSequenceNum());

    QNDEBUG(
        "synchonization::NotesProcessor",
        "NotesProcessor::cancelNoteProcessing: note guid = "
            << *note.guid() << ", usn = " << *note.updateSequenceNum());

    if (const auto callback = context->callbackWeak.lock()) {
        callback->onNoteProcessingCancelled(note);
    }

    {
        const QMutexLocker locker{context->statusMutex.get()};

        context->status->m_cancelledNoteGuidsAndUsns[*note.guid()] =
            *note.updateSequenceNum();
    }

    promise->addResult(ProcessNoteStatus::Canceled);
    promise->finish();
}

void NotesProcessor::processNoteDownloadingError(
    const ContextPtr & context,
    const std::shared_ptr<QPromise<ProcessNoteStatus>> & promise,
    const qevercloud::Note & note, const QException & e)
{
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
        if (se.errorCode() == qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED) {
            QNINFO(
                "synchronization::NotesProcessor",
                "Caught rate limit reached exception: duration = "
                    << (se.rateLimitDuration()
                            ? QString::number(*se.rateLimitDuration())
                            : QStringLiteral("<none>")));
            context->status->m_stopSynchronizationError =
                StopSynchronizationError{
                    RateLimitReachedError{se.rateLimitDuration()}};
            shouldCancelProcessing = true;
        }
        else if (se.errorCode() == qevercloud::EDAMErrorCode::AUTH_EXPIRED) {
            QNINFO(
                "synchronization::NotesProcessor",
                "Caught authentication expired error");
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
}

QFuture<void> NotesProcessor::downloadInkNoteImage(
    const ContextPtr & context, const qevercloud::Guid & notebookGuid,
    qevercloud::Resource resource, const QDir & inkNoteImagesStorageDir)
{
    Q_ASSERT(resource.guid());
    Q_ASSERT(resource.height());
    Q_ASSERT(resource.width());

    QNDEBUG(
        "synchronization::NotesProcessor",
        "NotesProcessor::downloadInkNoteImage: resource guid = "
            << *resource.guid() << ", height = " << *resource.height()
            << ", width = " << *resource.width()
            << ", notebook guid = " << notebookGuid);

    if (!inkNoteImagesStorageDir.exists()) {
        if (!inkNoteImagesStorageDir.mkpath(
                inkNoteImagesStorageDir.absolutePath()))
        {
            return threading::makeExceptionalFuture<void>(
                RuntimeError{ErrorString{
                    QStringLiteral("Failed to create directory for ink note "
                                   "images storage: ") +
                    inkNoteImagesStorageDir.absolutePath()}});
        }
    }

    auto promise = std::make_shared<QPromise<void>>();
    auto future = promise->future();
    promise->start();

    auto downloaderFuture =
        m_inkNoteImageDownloaderFactory->createInkNoteImageDownloader(
            notebookGuid, context->ctx);

    const auto selfWeak = weak_from_this();
    auto * currentThread = QThread::currentThread();

    threading::thenOrFailed(
        std::move(downloaderFuture), currentThread, promise,
        [selfWeak, promise, context, inkNoteImagesStorageDir, currentThread,
         resource = std::move(resource)](
            const qevercloud::IInkNoteImageDownloaderPtr & downloader) {
            if (context->canceler->isCanceled()) {
                promise->setException(OperationCanceled{});
                promise->finish();
                return;
            }

            const auto self = selfWeak.lock();
            if (!self) {
                return;
            }

            Q_ASSERT(downloader);
            auto imageDataFuture = downloader->downloadAsync(
                *resource.guid(), QSize{*resource.width(), *resource.height()},
                context->ctx);

            threading::thenOrFailed(
                std::move(imageDataFuture), currentThread, promise,
                [selfWeak, promise, resourceGuid = *resource.guid(), context,
                 inkNoteImagesStorageDir](const QByteArray & imageData) {
                    if (context->canceler->isCanceled()) {
                        promise->setException(OperationCanceled{});
                        promise->finish();
                    }

                    const auto self = selfWeak.lock();
                    if (!self) {
                        return;
                    }

                    QNDEBUG(
                        "synchronization::NotesProcessor",
                        "Successfully downloaded in note image: resource guid "
                            << "= " << resourceGuid);

                    QFile inkNoteImageFile{inkNoteImagesStorageDir.filePath(
                        resourceGuid + QStringLiteral(".png"))};

                    if (!inkNoteImageFile.open(QIODevice::WriteOnly)) {
                        promise->setException(
                            RuntimeError{ErrorString{QStringLiteral(
                                "Failed to open file for writing "
                                "to write downloaded ink note image")}});
                        promise->finish();
                        return;
                    }

                    inkNoteImageFile.write(imageData);
                    inkNoteImageFile.close();

                    promise->finish();
                });
        });

    return future;
}

QFuture<qevercloud::Note> NotesProcessor::downloadNoteThumbnail(
    const ContextPtr & context, qevercloud::Note note)
{
    Q_ASSERT(context);
    Q_ASSERT(note.guid());
    Q_ASSERT(note.notebookGuid());

    QNDEBUG(
        "synchronization::NotesProcessor",
        "NotesProcessor::downloadNoteThumbnail: note guid = "
            << *note.guid() << ", notebook guid = " << *note.notebookGuid());

    auto promise = std::make_shared<QPromise<qevercloud::Note>>();
    auto future = promise->future();
    promise->start();

    auto noteThumbnailDownloaderFuture =
        m_noteThumbnailDownloaderFactory->createNoteThumbnailDownloader(
            *note.notebookGuid(), context->ctx);

    auto * currentThread = QThread::currentThread();

    threading::thenOrFailed(
        std::move(noteThumbnailDownloaderFuture), currentThread, promise,
        [note = std::move(note), promise, currentThread,
         ctx = context->ctx](const qevercloud::INoteThumbnailDownloaderPtr &
                                 noteThumbnailDownloader) mutable {
            Q_ASSERT(noteThumbnailDownloader);

            auto noteThumbnailFuture =
                noteThumbnailDownloader->downloadNoteThumbnailAsync(
                    *note.guid(), 300,
                    qevercloud::INoteThumbnailDownloader::ImageType::PNG, ctx);

            threading::thenOrFailed(
                std::move(noteThumbnailFuture), currentThread, promise,
                [note = std::move(note), promise](QByteArray data) mutable {
                    note.setThumbnailData(std::move(data));
                    promise->addResult(std::move(note));
                    promise->finish();
                });
        });

    return future;
}

void NotesProcessor::putNoteToLocalStorage(
    const ContextPtr & context,
    const std::shared_ptr<QPromise<ProcessNoteStatus>> & promise,
    qevercloud::Note note, NoteKind putNoteKind)
{
    Q_ASSERT(context);
    Q_ASSERT(note.guid());
    Q_ASSERT(note.updateSequenceNum());

    QNDEBUG(
        "synchronization:NotesProcessor",
        "NotesProcessor::putNoteToLocalStorage: note guid = "
            << *note.guid() << ", usn = " << *note.updateSequenceNum());

    auto putNoteFuture = m_localStorage->putNote(note);

    auto * currentThread = QThread::currentThread();
    auto thenFuture = threading::then(
        std::move(putNoteFuture), currentThread,
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
        std::move(thenFuture), currentThread,
        [promise, context,
         note = std::move(note)](const QException & e) mutable {
            QNWARNING(
                "synchronization::NotesProcessor",
                "Failed to put note with guid "
                    << *note.guid() << " to local storage: " << e.what());

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

template <class T>
void NotesProcessor::printNoteKind(T & t, const NoteKind noteKind)
{
    switch (noteKind) {
    case NoteKind::NewNote:
        t << "New note";
        break;
    case NoteKind::UpdatedNote:
        t << "Updated note";
        break;
    }
}

QTextStream & operator<<(
    QTextStream & strm, const NotesProcessor::NoteKind noteKind)
{
    NotesProcessor::printNoteKind(strm, noteKind);
    return strm;
}

QDebug & operator<<(QDebug & dbg, const NotesProcessor::NoteKind noteKind)
{
    NotesProcessor::printNoteKind(dbg, noteKind);
    return dbg;
}

} // namespace quentier::synchronization
