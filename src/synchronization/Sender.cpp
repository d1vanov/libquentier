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

#include "Sender.h"
#include "Utils.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/OperationCanceled.h>
#include <quentier/exception/RuntimeError.h>
#include <quentier/local_storage/ILocalStorage.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/synchronization/ISyncStateStorage.h>
#include <quentier/threading/Factory.h>
#include <quentier/threading/Future.h>
#include <quentier/threading/TrackedTask.h>
#include <quentier/utility/TagSortByParentChildRelations.h>
#include <quentier/utility/cancelers/AnyOfCanceler.h>
#include <quentier/utility/cancelers/ICanceler.h>
#include <quentier/utility/cancelers/ManualCanceler.h>

#include <synchronization/INoteStoreProvider.h>
#include <synchronization/types/SyncState.h>

#include <qevercloud/RequestContextBuilder.h>
#include <qevercloud/exceptions/EDAMSystemException.h>
#include <qevercloud/services/INoteStore.h>

#include <QCoreApplication>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>

#include <utility>

namespace quentier::synchronization {

Sender::Sender(
    Account account, local_storage::ILocalStoragePtr localStorage,
    ISyncStateStoragePtr syncStateStorage,
    INoteStoreProviderPtr noteStoreProvider, qevercloud::IRequestContextPtr ctx,
    qevercloud::IRetryPolicyPtr retryPolicy) :
    m_account{std::move(account)},
    m_localStorage{std::move(localStorage)},
    // clang-format off
    m_syncStateStorage{std::move(syncStateStorage)},
    m_noteStoreProvider{std::move(noteStoreProvider)}, m_ctx{std::move(ctx)},
    m_retryPolicy{std::move(retryPolicy)}
// clang-format on
{
    if (Q_UNLIKELY(m_account.isEmpty())) {
        throw InvalidArgument{
            ErrorString{QStringLiteral("Sender ctor: account is empty")}};
    }

    if (Q_UNLIKELY(!m_localStorage)) {
        throw InvalidArgument{
            ErrorString{QStringLiteral("Sender ctor: local storage is null")}};
    }

    if (Q_UNLIKELY(!m_syncStateStorage)) {
        throw InvalidArgument{ErrorString{
            QStringLiteral("Sender ctor: sync state storage is null")}};
    }

    if (Q_UNLIKELY(!m_noteStoreProvider)) {
        throw InvalidArgument{ErrorString{
            QStringLiteral("Sender ctor: note store provider is null")}};
    }
}

QFuture<ISender::Result> Sender::send(
    utility::cancelers::ICancelerPtr canceler, ICallbackWeakPtr callbackWeak)
{
    QNDEBUG("synchronization::Sender", "Sender::send");

    auto lastSyncState = readLastSyncState(m_syncStateStorage, m_account);
    Q_ASSERT(lastSyncState);

    QNDEBUG("synchronization::Sender", "Last sync state: " << *lastSyncState);

    auto promise = std::make_shared<QPromise<Result>>();
    auto future = promise->future();

    promise->start();

    auto sendContext = std::make_shared<SendContext>();
    sendContext->lastSyncState = std::move(lastSyncState);
    sendContext->promise = promise;

    sendContext->manualCanceler =
        std::make_shared<utility::cancelers::ManualCanceler>();
    sendContext->canceler = std::make_shared<utility::cancelers::AnyOfCanceler>(
        QList<utility::cancelers::ICancelerPtr>{}
        << canceler << sendContext->manualCanceler);

    sendContext->callbackWeak = std::move(callbackWeak);
    sendContext->userOwnSendStatus = std::make_shared<SendStatus>();
    sendContext->sendStatusMutex = std::make_shared<QMutex>();

    const auto selfWeak = weak_from_this();
    auto * currentThread = QThread::currentThread();

    QFuture<void> tagsFuture = processTags(sendContext);

    QFuture<void> notebooksFuture = [&] {
        if (sendContext->manualCanceler->isCanceled()) {
            return threading::makeReadyFuture();
        }

        auto notebooksPromise = std::make_shared<QPromise<void>>();
        auto notebooksFuture = notebooksPromise->future();
        notebooksPromise->start();

        threading::thenOrFailed(
            std::move(tagsFuture), currentThread, notebooksPromise,
            threading::TrackedTask{
                selfWeak,
                [this, sendContext, notebooksPromise, currentThread]() mutable {
                    auto f = processNotebooks(sendContext);
                    threading::thenOrFailed(
                        std::move(f), currentThread,
                        std::move(notebooksPromise));
                }});

        return notebooksFuture;
    }();

    QFuture<void> savedSearchesFuture = [&] {
        if (sendContext->manualCanceler->isCanceled()) {
            return threading::makeReadyFuture();
        }

        auto savedSearchesPromise = std::make_shared<QPromise<void>>();
        auto savedSearchesFuture = savedSearchesPromise->future();
        savedSearchesPromise->start();

        threading::thenOrFailed(
            std::move(notebooksFuture), currentThread, savedSearchesPromise,
            threading::TrackedTask{
                selfWeak,
                [this, sendContext, savedSearchesPromise,
                 currentThread]() mutable {
                    auto f = processSavedSearches(sendContext);
                    threading::thenOrFailed(
                        std::move(f), currentThread,
                        std::move(savedSearchesPromise));
                }});

        return savedSearchesFuture;
    }();

    QFuture<void> notesFuture = [&] {
        if (sendContext->manualCanceler->isCanceled()) {
            return threading::makeReadyFuture();
        }

        auto notesPromise = std::make_shared<QPromise<void>>();
        auto notesFuture = notesPromise->future();
        notesPromise->start();

        threading::thenOrFailed(
            std::move(savedSearchesFuture), currentThread, notesPromise,
            threading::TrackedTask{
                selfWeak,
                [this, sendContext, notesPromise, currentThread]() mutable {
                    auto f = processNotes(sendContext);
                    threading::thenOrFailed(
                        std::move(f), currentThread, std::move(notesPromise));
                }});
        return notesFuture;
    }();

    threading::thenOrFailed(
        std::move(notesFuture), currentThread, promise, [promise, sendContext] {
            const QMutexLocker locker{sendContext->sendStatusMutex.get()};
            ISender::Result result;
            result.userOwnResult = sendContext->userOwnSendStatus;
            result.linkedNotebookResults.reserve(
                sendContext->linkedNotebookSendStatuses.size());
            for (const auto it: qevercloud::toRange(
                     std::as_const(sendContext->linkedNotebookSendStatuses)))
            {
                result.linkedNotebookResults[it.key()] = it.value();
            }
            const auto now = QDateTime::currentMSecsSinceEpoch();
            sendContext->lastSyncState->m_userDataLastSyncTime = now;
            for (const auto it: qevercloud::toRange(
                     sendContext->lastSyncState->m_linkedNotebookLastSyncTimes))
            {
                it.value() = now;
            }
            result.syncState = sendContext->lastSyncState;
            promise->addResult(std::move(result));
            promise->finish();
        });

    return future;
}

QFuture<void> Sender::processNotes(SendContextPtr sendContext) const
{
    Q_ASSERT(sendContext);

    auto promise = std::make_shared<QPromise<void>>();
    promise->start();
    auto future = promise->future();

    const auto selfWeak = weak_from_this();
    auto * currentThread = QThread::currentThread();

    const auto listNotesOptions = [] {
        local_storage::ILocalStorage::ListNotesOptions options;
        options.m_filters.m_locallyModifiedFilter =
            local_storage::ILocalStorage::ListObjectsFilter::Include;
        options.m_filters.m_localOnlyFilter =
            local_storage::ILocalStorage::ListObjectsFilter::Exclude;
        return options;
    }();

    const auto fetchNoteOptions =
        local_storage::ILocalStorage::FetchNoteOptions{} |
        local_storage::ILocalStorage::FetchNoteOption::WithResourceMetadata |
        local_storage::ILocalStorage::FetchNoteOption::WithResourceBinaryData;

    auto listLocallyModifiedNotesFuture =
        m_localStorage->listNotes(fetchNoteOptions, listNotesOptions);

    threading::thenOrFailed(
        std::move(listLocallyModifiedNotesFuture), currentThread, promise,
        [selfWeak, this, promise, sendContext = std::move(sendContext)](
            QList<qevercloud::Note> && notes) mutable {
            const auto self = selfWeak.lock();
            if (!self) {
                return;
            }

            if (sendContext->manualCanceler->isCanceled()) {
                promise->finish();
                return;
            }

            if (sendContext->canceler->isCanceled()) {
                promise->setException(OperationCanceled{});
                promise->finish();
                return;
            }

            sendNotes(
                std::move(sendContext), std::move(notes), std::move(promise));
        });

    return future;
}

void Sender::sendNotes(
    SendContextPtr sendContext, QList<qevercloud::Note> notes,
    std::shared_ptr<QPromise<void>> promise) const
{
    if (notes.isEmpty()) {
        promise->finish();
        return;
    }

    // There are two details making processing of notes special compared to
    // other kinds of data items:
    // 1. If some note is linked with tags which we failed to send, we need
    //    to understand whether this tag is new. If it is, we will send the note
    //    but without linkage to this tag. And we won't clear the locally
    //    modified flag from such note so that the next sync would attempt
    //    to send the offending tag and the note linked with it again.
    // 2. If some note belongs to a notebook which we failed to send, if this
    //    notebook was new, we cannot send the note because Evernote has no
    //    counterpart for that notebook and thus has no notebook to put the
    //    note into. So we skip sending this note and don't clear the locally
    //    modified flag from it so that the next sync would attempt to send
    //    the notebook and its notes again.

    const auto selfWeak = weak_from_this();
    auto * currentThread = QThread::currentThread();

    QList<QFuture<void>> noteProcessingFutures;
    noteProcessingFutures.reserve(notes.size());

    QFuture<void> previousNoteFuture = threading::makeReadyFuture();
    for (auto it = notes.begin(), end = notes.end(); it != end; ++it) {
        auto & note = *it;

        bool containsFailedToSendTags = false;
        {
            const QMutexLocker locked{sendContext->sendStatusMutex.get()};

            if (sendContext->failedToSendNewNotebookLocalIds.contains(
                    note.notebookLocalId()))
            {
                // This note cannot be sent to Evernote because we could not
                // send the notebook which it resides in to Evernote.
                // Since the notebook is new, we can be sure it doesn't come
                // from a linked notebook.
                auto status = sendStatus(sendContext, std::nullopt);

                status->m_failedToSendNotes << ISendStatus::NoteWithException{
                    note,
                    std::make_shared<RuntimeError>(ErrorString{QStringLiteral(
                        "Cannot send note which notebook could not be "
                        "sent")})};

                Sender::sendUpdate(sendContext, status, std::nullopt);
                continue;
            }

            const QStringList & tagLocalIds = note.tagLocalIds();
            for (const QString & tagLocalId: std::as_const(tagLocalIds)) {
                if (sendContext->failedToSendNewTagLocalIds.contains(
                        tagLocalId)) {
                    containsFailedToSendTags = true;
                    break;
                }
            }

            QList<qevercloud::Guid> tagGuids =
                note.tagGuids().value_or(QList<qevercloud::Guid>{});
            bool foundSomeNewTagGuid = false;
            for (const QString & tagLocalId: std::as_const(tagLocalIds)) {
                if (const auto it =
                        sendContext->newTagLocalIdsToGuids.constFind(
                            tagLocalId);
                    it != sendContext->newTagLocalIdsToGuids.constEnd() &&
                    !tagGuids.contains(it.value()))
                {
                    tagGuids << it.value();
                    foundSomeNewTagGuid = true;
                }
            }
            if (foundSomeNewTagGuid) {
                note.setTagGuids(std::move(tagGuids));
            }
        }

        if (it != notes.begin() && !noteProcessingFutures.isEmpty()) {
            previousNoteFuture = noteProcessingFutures.constLast();
        }

        QFuture<qevercloud::Note> noteFuture = [&, this] {
            auto notePromise = std::make_shared<QPromise<qevercloud::Note>>();

            auto future = notePromise->future();
            notePromise->start();

            sendNote(
                sendContext, note, containsFailedToSendTags,
                std::move(previousNoteFuture), notePromise);

            return future;
        }();

        auto noteProcessingPromise = std::make_shared<QPromise<void>>();
        noteProcessingFutures << noteProcessingPromise->future();
        noteProcessingPromise->start();

        auto noteThenFuture = threading::then(
            std::move(noteFuture), currentThread,
            threading::TrackedTask{
                selfWeak,
                [sendContext, this,
                 noteProcessingPromise](qevercloud::Note note) {
                    if (sendContext->canceler->isCanceled()) {
                        const auto e = std::make_shared<OperationCanceled>();
                        processNoteFailure(
                            sendContext, std::move(note), *e,
                            noteProcessingPromise);
                        return;
                    }

                    processNote(
                        sendContext, std::move(note), noteProcessingPromise);
                }});

        threading::onFailed(
            std::move(noteThenFuture), currentThread,
            [selfWeak, this, sendContext, note = std::move(note),
             noteProcessingPromise](const QException & e) mutable {
                const auto self = selfWeak.lock();
                if (!self) {
                    return;
                }

                processNoteFailure(
                    sendContext, std::move(note), e, noteProcessingPromise);
            });
    }

    auto allNotesProcessingFuture =
        threading::whenAll(std::move(noteProcessingFutures));

    threading::thenOrFailed(
        std::move(allNotesProcessingFuture), currentThread, std::move(promise));
}

void Sender::sendNote(
    SendContextPtr sendContext, qevercloud::Note note,
    const bool containsFailedToSendTags, QFuture<void> previousNoteFuture,
    const std::shared_ptr<QPromise<qevercloud::Note>> & notePromise) const
{
    const auto selfWeak = weak_from_this();
    auto * currentThread = QThread::currentThread();

    threading::thenOrFailed(
        std::move(previousNoteFuture), currentThread, notePromise,
        threading::TrackedTask{
            selfWeak,
            [selfWeak, this, notePromise = notePromise, note = std::move(note),
             containsFailedToSendTags, currentThread,
             sendContext = std::move(sendContext)]() mutable {
                if (sendContext->canceler->isCanceled()) {
                    notePromise->setException(OperationCanceled{});
                    notePromise->finish();
                    return;
                }

                auto noteStoreFuture =
                    m_noteStoreProvider->noteStoreForNotebookLocalId(
                        note.notebookLocalId(), m_ctx, m_retryPolicy);

                threading::thenOrFailed(
                    std::move(noteStoreFuture), currentThread, notePromise,
                    threading::TrackedTask{
                        selfWeak,
                        [this, notePromise = notePromise,
                         note = std::move(note), containsFailedToSendTags,
                         sendContext = std::move(sendContext)](
                            const qevercloud::INoteStorePtr &
                                noteStore) mutable {
                            Q_ASSERT(noteStore);
                            if (sendContext->canceler->isCanceled()) {
                                notePromise->setException(OperationCanceled{});
                                notePromise->finish();
                                return;
                            }

                            {
                                const QMutexLocker locker{
                                    sendContext->sendStatusMutex.get()};

                                auto status = sendStatus(
                                    sendContext,
                                    noteStore->linkedNotebookGuid());
                                ++status->m_totalAttemptedToSendNotes;
                            }

                            sendNoteImpl(
                                std::move(sendContext), std::move(note),
                                containsFailedToSendTags, noteStore,
                                std::move(notePromise));
                        }});
            }});
}

void Sender::sendNoteImpl(
    SendContextPtr sendContext, qevercloud::Note note,
    const bool containsFailedToSendTags,
    const qevercloud::INoteStorePtr & noteStore,
    std::shared_ptr<QPromise<qevercloud::Note>> notePromise) const
{
    QNDEBUG(
        "synchronization::Sender",
        "Sender::sendNoteImpl: " << note);

    Q_ASSERT(noteStore);

    const bool newNote = !note.updateSequenceNum().has_value();
    std::optional<qevercloud::Guid> linkedNotebookGuid =
        noteStore->linkedNotebookGuid();

    auto noteFuture =
        (newNote ? noteStore->createNoteAsync(note)
                 : noteStore->updateNoteAsync(note));

    auto * currentThread = QThread::currentThread();

    auto thenFuture = threading::then(
        std::move(noteFuture), currentThread,
        [notePromise, containsFailedToSendTags, note = std::move(note)](
            const qevercloud::Note & noteMetadata) mutable {
            note.setGuid(noteMetadata.guid());
            note.setUpdateSequenceNum(noteMetadata.updateSequenceNum());
            note.setLocallyModified(containsFailedToSendTags);
            note.setTagGuids(noteMetadata.tagGuids());
            note.setNotebookGuid(noteMetadata.notebookGuid());
            if (note.resources()) {
                Q_ASSERT(noteMetadata.resources());
                Q_ASSERT(
                    noteMetadata.resources()->size() ==
                    note.resources()->size());
                for (int i = 0; i < note.resources()->size(); ++i) {
                    auto & resource = (*note.mutableResources())[i];
                    const auto & serverNoteResource =
                        (*noteMetadata.resources())[i];

                    resource.setGuid(serverNoteResource.guid());
                    resource.setUpdateSequenceNum(serverNoteResource.updateSequenceNum());
                    resource.setNoteGuid(note.guid());
                    resource.setLocallyModified(false);
                }
            }
            QNDEBUG(
                "synchronization::Sender",
                "Created or updated note on the server: " << note);

            notePromise->addResult(std::move(note));
            notePromise->finish();
        });

    threading::onFailed(
        std::move(thenFuture), currentThread,
        [notePromise = std::move(notePromise),
         sendContext = std::move(sendContext),
         linkedNotebookGuid =
             std::move(linkedNotebookGuid)](const QException & e) {
            QNWARNING(
                "synchronization::Sender",
                "Failed to create or update note on the server: " << e.what());
            Sender::checkForStopSynchronizationException(
                sendContext, linkedNotebookGuid, e);
            notePromise->setException(e);
            notePromise->finish();
        });
}

void Sender::processNote(
    const SendContextPtr & sendContext, qevercloud::Note note,
    const std::shared_ptr<QPromise<void>> & promise) const
{
    const auto selfWeak = weak_from_this();
    auto * currentThread = QThread::currentThread();

    auto putNoteFuture = m_localStorage->putNote(note);
    auto putNoteThenFuture = threading::then(
        std::move(putNoteFuture), currentThread,
        [selfWeak, this, sendContext, promise, currentThread,
         notebookLocalId = note.notebookLocalId(),
         noteUsn = note.updateSequenceNum()]() mutable {
            if (sendContext->canceler->isCanceled()) {
                promise->setException(OperationCanceled{});
                promise->finish();
                return;
            }

            {
                QMutexLocker locker{sendContext->sendStatusMutex.get()};

                const auto linkedNotebookGuidIt =
                    sendContext->notebookLocalIdsToLinkedNotebookGuids
                        .constFind(notebookLocalId);
                if (linkedNotebookGuidIt !=
                    sendContext->notebookLocalIdsToLinkedNotebookGuids
                        .constEnd())
                {
                    Q_ASSERT(noteUsn);
                    if (noteUsn) {
                        if (const auto self = selfWeak.lock()) {
                            checkUpdateSequenceNumber(
                                *noteUsn, sendContext,
                                linkedNotebookGuidIt.value());
                        }
                    }

                    auto status =
                        sendStatus(sendContext, linkedNotebookGuidIt.value());

                    ++status->m_totalSuccessfullySentNotes;
                    Sender::sendUpdate(
                        sendContext, status, linkedNotebookGuidIt.value());

                    locker.unlock();
                    promise->finish();
                    return;
                }
            }

            // We don't have cached info on linked notebook guid for this
            // notebook yet, need to find it out
            const auto self = selfWeak.lock();
            if (!self) {
                return;
            }

            auto notebookFuture =
                m_localStorage->findNotebookByLocalId(notebookLocalId);

            auto notebookThenFuture = threading::then(
                std::move(notebookFuture), currentThread,
                [sendContext, promise, noteUsn, selfWeak, this,
                 notebookLocalId = std::move(notebookLocalId)](
                    const std::optional<qevercloud::Notebook> & notebook) {
                    if (sendContext->canceler->isCanceled()) {
                        promise->setException(OperationCanceled{});
                        promise->finish();
                        return;
                    }

                    if (Q_UNLIKELY(!notebook)) {
                        // Impossible situation indicating of some serious
                        // internal error - we got this notebook local id from
                        // a locally modified note which cannot exist without
                        // a notebook.
                        // Just complaining to the log and doing nothing else.
                        QNWARNING(
                            "synchronization::Sender",
                            "Could not find notebook by local id in the local "
                                << "storage: " << notebookLocalId);
                        promise->finish();
                        return;
                    }

                    {
                        const auto & linkedNotebookGuid =
                            notebook->linkedNotebookGuid();

                        const QMutexLocker locker{
                            sendContext->sendStatusMutex.get()};

                        Q_ASSERT(noteUsn);
                        if (noteUsn) {
                            if (const auto self = selfWeak.lock()) {
                                checkUpdateSequenceNumber(
                                    *noteUsn, sendContext, linkedNotebookGuid);
                            }
                        }

                        sendContext->notebookLocalIdsToLinkedNotebookGuids
                            [notebookLocalId] = linkedNotebookGuid;

                        auto status =
                            sendStatus(sendContext, linkedNotebookGuid);

                        ++status->m_totalSuccessfullySentNotes;
                        Sender::sendUpdate(
                            sendContext, status, linkedNotebookGuid);
                    }
                    promise->finish();
                });

            threading::onFailed(
                std::move(notebookThenFuture), currentThread,
                [sendContext, promise, notebookLocalId](const QException & e) {
                    if (sendContext->canceler->isCanceled()) {
                        promise->setException(OperationCanceled{});
                        promise->finish();
                        return;
                    }

                    QNWARNING(
                        "synchronization::Sender",
                        "Failed to find notebook by local id in the local "
                            << "storage: local id = " << notebookLocalId
                            << ", error: " << e.what());
                    promise->finish();
                });
        });

    threading::onFailed(
        std::move(putNoteThenFuture), currentThread,
        [selfWeak, this, sendContext, promise,
         note = std::move(note)](const QException & e) mutable {
            if (sendContext->canceler->isCanceled()) {
                promise->setException(OperationCanceled{});
                promise->finish();
                return;
            }

            const auto self = selfWeak.lock();
            if (!self) {
                return;
            }

            processNoteFailure(sendContext, std::move(note), e, promise);
        });
}

void Sender::processNoteFailure(
    const SendContextPtr & sendContext, qevercloud::Note note,
    const QException & e, const std::shared_ptr<QPromise<void>> & promise) const
{
    const auto & notebookLocalId = note.notebookLocalId();
    auto exc = std::shared_ptr<QException>(e.clone());

    {
        QMutexLocker locker{sendContext->sendStatusMutex.get()};

        const auto linkedNotebookGuidIt =
            sendContext->notebookLocalIdsToLinkedNotebookGuids.constFind(
                notebookLocalId);
        if (linkedNotebookGuidIt !=
            sendContext->notebookLocalIdsToLinkedNotebookGuids.constEnd())
        {
            const std::optional<qevercloud::Guid> linkedNotebookGuid = // NOLINT
                linkedNotebookGuidIt.value();

            auto status = sendStatus(sendContext, linkedNotebookGuid);
            status->m_failedToSendNotes << ISendStatus::NoteWithException{
                std::move(note), std::move(exc)};

            Sender::sendUpdate(
                sendContext, status, linkedNotebookGuidIt.value());

            locker.unlock();

            Sender::checkForStopSynchronizationException(
                sendContext, linkedNotebookGuid, e);

            promise->finish();
            return;
        }
    }

    // We don't have cached info on linked notebook guid for this
    // notebook yet, need to find it out
    auto notebookFuture =
        m_localStorage->findNotebookByLocalId(notebookLocalId);

    auto * currentThread = QThread::currentThread();

    auto notebookThenFuture = threading::then(
        std::move(notebookFuture), currentThread,
        [sendContext, promise, notebookLocalId, exc = std::move(exc),
         note = std::move(note)](
            const std::optional<qevercloud::Notebook> & notebook) mutable {
            if (Q_UNLIKELY(!notebook)) {
                // Impossible situation indicating of some serious
                // internal error - we got this notebook local id from
                // a locally modified note which cannot exist without
                // a notebook.
                // Just complaining to the log and doing nothing else.
                QNWARNING(
                    "synchronization::Sender",
                    "Could not find notebook by local id in the local "
                        << "storage: " << notebookLocalId);
                promise->finish();
                return;
            }

            {
                const auto & linkedNotebookGuid =
                    notebook->linkedNotebookGuid();

                Sender::checkForStopSynchronizationException(
                    sendContext, linkedNotebookGuid, *exc);

                const QMutexLocker locker{sendContext->sendStatusMutex.get()};
                sendContext
                    ->notebookLocalIdsToLinkedNotebookGuids[notebookLocalId] =
                    linkedNotebookGuid;

                auto status = sendStatus(sendContext, linkedNotebookGuid);

                status->m_failedToSendNotes << ISendStatus::NoteWithException{
                    std::move(note), std::move(exc)};

                Sender::sendUpdate(sendContext, status, linkedNotebookGuid);
            }
            promise->finish();
        });

    threading::onFailed(
        std::move(notebookThenFuture), currentThread,
        [sendContext, promise, notebookLocalId](const QException & e) {
            QNWARNING(
                "synchronization::Sender",
                "Failed to find notebook by local id in the local "
                    << "storage: local id = " << notebookLocalId
                    << ", error: " << e.what());
            promise->finish();
        });
}

QFuture<void> Sender::processTags(SendContextPtr sendContext) const
{
    Q_ASSERT(sendContext);

    auto promise = std::make_shared<QPromise<void>>();
    promise->start();
    auto future = promise->future();

    const auto selfWeak = weak_from_this();
    auto * currentThread = QThread::currentThread();

    const auto listTagsOptions = [] {
        local_storage::ILocalStorage::ListTagsOptions options;
        options.m_filters.m_locallyModifiedFilter =
            local_storage::ILocalStorage::ListObjectsFilter::Include;
        options.m_filters.m_localOnlyFilter =
            local_storage::ILocalStorage::ListObjectsFilter::Exclude;
        return options;
    }();

    auto listLocallyModifiedTagsFuture =
        m_localStorage->listTags(listTagsOptions);

    threading::thenOrFailed(
        std::move(listLocallyModifiedTagsFuture), currentThread, promise,
        threading::TrackedTask{
            selfWeak,
            [this, promise, sendContext = std::move(sendContext)](
                QList<qevercloud::Tag> && tags) mutable {
                if (sendContext->manualCanceler->isCanceled()) {
                    promise->finish();
                    return;
                }

                if (sendContext->canceler->isCanceled()) {
                    promise->setException(OperationCanceled{});
                    promise->finish();
                    return;
                }

                sendTags(
                    std::move(sendContext), std::move(tags),
                    std::move(promise));
            }});

    return future;
}

void Sender::sendTags(
    SendContextPtr sendContext, QList<qevercloud::Tag> tags,
    std::shared_ptr<QPromise<void>> promise) const
{
    if (tags.isEmpty()) {
        promise->finish();
        return;
    }

    // Processing of tags is special compared to processing of notebooks or
    // saved searches in one crucial aspect: tags may depend on each other,
    // some tag can be a child of some other tag. Due to that, we need to
    // process tags in the proper order: topologically sorted i.e. sorted by
    // their dependencies on one another. If some tag could not be sent for some
    // reason, its child tag cannot be sent either. Also, if some tag being
    // a parent to other tags was sent to Evernote for the first time, need to
    // set parentGuid property to its child tags which are already inside "tags"
    // list.
    ErrorString errorDescription;
    if (!sortTagsByParentChildRelations(tags, errorDescription)) {
        promise->setException(RuntimeError{std::move(errorDescription)});
        promise->finish();
        return;
    }

    const auto selfWeak = weak_from_this();
    auto * currentThread = QThread::currentThread();

    QList<QFuture<void>> tagProcessingFutures;
    tagProcessingFutures.reserve(tags.size());

    QFuture<void> previousTagFuture = threading::makeReadyFuture();
    for (auto it = tags.begin(), end = tags.end(); it != end; ++it) {
        auto & tag = *it;

        auto tagParentLocalId = tag.parentTagLocalId();
        if (!tagParentLocalId.isEmpty()) {
            const QMutexLocker locker{sendContext->sendStatusMutex.get()};
            if (sendContext->failedToSendNewTagLocalIds.contains(
                    tagParentLocalId)) {
                sendContext->failedToSendNewTagLocalIds.insert(tag.localId());
                auto status = sendStatus(sendContext, tag.linkedNotebookGuid());

                status->m_failedToSendTags << ISendStatus::TagWithException{
                    tag,
                    std::make_shared<RuntimeError>(ErrorString{QStringLiteral(
                        "Cannot send tag which parent also could not be "
                        "sent")})};

                Sender::sendUpdate(
                    sendContext, status, tag.linkedNotebookGuid());

                continue;
            }

            const auto parentTagGuidIt =
                sendContext->newTagLocalIdsToGuids.constFind(
                    tag.parentTagLocalId());
            if (parentTagGuidIt !=
                sendContext->newTagLocalIdsToGuids.constEnd()) {
                tag.setParentGuid(parentTagGuidIt.value());
            }
        }

        if (it != tags.begin() && !tagProcessingFutures.isEmpty()) {
            previousTagFuture = tagProcessingFutures.constLast();
        }

        QFuture<qevercloud::Tag> tagFuture = [&] {
            auto tagPromise = std::make_shared<QPromise<qevercloud::Tag>>();
            auto future = tagPromise->future();
            tagPromise->start();

            sendTag(sendContext, tag, std::move(previousTagFuture), tagPromise);
            return future;
        }();

        auto tagProcessingPromise = std::make_shared<QPromise<void>>();
        tagProcessingFutures << tagProcessingPromise->future();
        tagProcessingPromise->start();

        // NOTE: linkedNotebookGuid field of qevercloud::Tag is not
        // serialized on thrift level and thus in the lambda right below
        // the sent tag would not contain linkedNotebookGuid. Hence
        // passing it to the lambda manually.
        auto linkedNotebookGuid = tag.linkedNotebookGuid();

        auto tagThenFuture = threading::then(
            std::move(tagFuture), currentThread,
            [sendContext, selfWeak, this,
             linkedNotebookGuid = std::move(linkedNotebookGuid),
             tagProcessingPromise](qevercloud::Tag tag) {
                tag.setLinkedNotebookGuid(linkedNotebookGuid);
                if (sendContext->canceler->isCanceled()) {
                    {
                        const QMutexLocker locker{
                            sendContext->sendStatusMutex.get()};
                        auto status =
                            sendStatus(sendContext, linkedNotebookGuid);
                        status->m_failedToSendTags
                            << ISendStatus::TagWithException{
                                   std::move(tag),
                                   std::make_shared<OperationCanceled>()};
                        Sender::sendUpdate(
                            sendContext, status, linkedNotebookGuid);
                    }
                    tagProcessingPromise->finish();
                    return;
                }

                const auto self = selfWeak.lock();
                if (!self) {
                    return;
                }

                processTag(sendContext, std::move(tag), tagProcessingPromise);
            });

        threading::onFailed(
            std::move(tagThenFuture), currentThread,
            [sendContext, tag = tag,
             tagProcessingPromise](const QException & e) mutable {
                Sender::processTagFailure(
                    sendContext, std::move(tag), e, tagProcessingPromise);
            });
    }

    auto allTagsProcessingFuture =
        threading::whenAll(std::move(tagProcessingFutures));

    threading::thenOrFailed(
        std::move(allTagsProcessingFuture), currentThread, std::move(promise));
}

void Sender::sendTag(
    SendContextPtr sendContext, qevercloud::Tag tag,
    QFuture<void> previousTagFuture,
    const std::shared_ptr<QPromise<qevercloud::Tag>> & tagPromise) const
{
    const auto selfWeak = weak_from_this();
    auto * currentThread = QThread::currentThread();

    threading::thenOrFailed(
        std::move(previousTagFuture), currentThread, tagPromise,
        threading::TrackedTask{
            selfWeak,
            [selfWeak, this, tagPromise, currentThread, tag = std::move(tag),
             sendContext = std::move(sendContext)]() mutable {
                if (sendContext->canceler->isCanceled()) {
                    tagPromise->setException(OperationCanceled{});
                    tagPromise->finish();
                    return;
                }

                auto tagParentLocalId = tag.parentTagLocalId();
                if (!tagParentLocalId.isEmpty()) {
                    QMutexLocker locker{sendContext->sendStatusMutex.get()};
                    if (sendContext->failedToSendNewTagLocalIds.contains(
                            tagParentLocalId)) {
                        locker.unlock();

                        tagPromise->setException(RuntimeError{ErrorString{
                            QStringLiteral("Cannot send tag which parent also "
                                           "could not be sent")}});
                        tagPromise->finish();
                        return;
                    }

                    const auto parentTagGuidIt =
                        sendContext->newTagLocalIdsToGuids.constFind(
                            tag.parentTagLocalId());
                    if (parentTagGuidIt !=
                        sendContext->newTagLocalIdsToGuids.constEnd()) {
                        tag.setParentGuid(parentTagGuidIt.value());
                    }
                }

                auto noteStoreFuture =
                    (tag.linkedNotebookGuid()
                         ? m_noteStoreProvider->linkedNotebookNoteStore(
                               *tag.linkedNotebookGuid(), m_ctx, m_retryPolicy)
                         : m_noteStoreProvider->userOwnNoteStore(
                               m_ctx, m_retryPolicy));

                threading::thenOrFailed(
                    std::move(noteStoreFuture), currentThread, tagPromise,
                    threading::TrackedTask{
                        selfWeak,
                        [this, tagPromise = tagPromise,
                         sendContext = std::move(sendContext),
                         tag = std::move(tag)](const qevercloud::INoteStorePtr &
                                                   noteStore) mutable {
                            Q_ASSERT(noteStore);

                            if (sendContext->canceler->isCanceled()) {
                                tagPromise->setException(OperationCanceled{});
                                tagPromise->finish();
                                return;
                            }

                            {
                                const QMutexLocker locker{
                                    sendContext->sendStatusMutex.get()};

                                auto status = sendStatus(
                                    sendContext, tag.linkedNotebookGuid());
                                ++status->m_totalAttemptedToSendTags;
                            }

                            sendTagImpl(
                                std::move(sendContext), std::move(tag),
                                noteStore, std::move(tagPromise));
                        }});
            }});
}

void Sender::sendTagImpl(
    SendContextPtr sendContext, qevercloud::Tag tag,
    const qevercloud::INoteStorePtr & noteStore,
    std::shared_ptr<QPromise<qevercloud::Tag>> tagPromise) const
{
    QNDEBUG("synchronization::Sender", "Sender::sendTagImpl: " << tag);

    const bool newTag = !tag.updateSequenceNum().has_value();
    std::optional<qevercloud::Guid> linkedNotebookGuid =
        tag.linkedNotebookGuid();

    auto * currentThread = QThread::currentThread();

    QFuture<void> thenFuture;
    if (newTag) {
        auto createTagFuture = noteStore->createTagAsync(tag);
        thenFuture = threading::then(
            std::move(createTagFuture), currentThread,
            [tagPromise, sendContext,
             tag = std::move(tag)](qevercloud::Tag t) mutable {
                {
                    // This tag was sent to Evernote
                    // for the first time so it has
                    // acquired guid for the first
                    // time. Will cache this local
                    // id to guid mapping in order
                    // to be able to set parent guid
                    // to its child tags (if there
                    // are any).
                    Q_ASSERT(t.guid());
                    const QMutexLocker locker{
                        sendContext->sendStatusMutex.get()};
                    sendContext->newTagLocalIdsToGuids[tag.localId()] =
                        *t.guid();
                }
                t.setLocalId(tag.localId());
                t.setLocallyFavorited(tag.isLocallyFavorited());
                t.setLocalData(std::move(tag.mutableLocalData()));
                t.setParentTagLocalId(tag.parentTagLocalId());
                t.setLocallyModified(false);
                QNDEBUG(
                    "synchronization::Sender",
                    "Created new tag on the server: " << t);

                tagPromise->addResult(std::move(t));
                tagPromise->finish();
            });
    }
    else {
        auto updateTagFuture = noteStore->updateTagAsync(tag);
        thenFuture = threading::then(
            std::move(updateTagFuture), currentThread,
            [tagPromise,
             tag = std::move(tag)](const qint32 newUpdateSequenceNum) mutable {
                tag.setUpdateSequenceNum(newUpdateSequenceNum);
                tag.setLocallyModified(false);
                QNDEBUG(
                    "synchronization::Sender",
                    "Updated tag on the server: " << tag);

                tagPromise->addResult(std::move(tag));
                tagPromise->finish();
            });
    }

    threading::onFailed(
        std::move(thenFuture), currentThread,
        [tagPromise = std::move(tagPromise),
         sendContext = std::move(sendContext),
         linkedNotebookGuid =
             std::move(linkedNotebookGuid)](const QException & e) {
            QNWARNING(
                "synchronization::Sender",
                "Failed to create or update tag on the server: " << e.what());
            Sender::checkForStopSynchronizationException(
                sendContext, linkedNotebookGuid, e);
            tagPromise->setException(e);
            tagPromise->finish();
        });
}

void Sender::processTag(
    const SendContextPtr & sendContext, qevercloud::Tag tag,
    const std::shared_ptr<QPromise<void>> & promise) const
{
    {
        Q_ASSERT(tag.updateSequenceNum());
        const QMutexLocker locker{sendContext->sendStatusMutex.get()};
        if (tag.updateSequenceNum()) {
            checkUpdateSequenceNumber(
                *tag.updateSequenceNum(), sendContext,
                tag.linkedNotebookGuid());
        }
    }

    auto * currentThread = QThread::currentThread();

    auto putTagFuture = m_localStorage->putTag(tag);
    auto putTagThenFuture = threading::then(
        std::move(putTagFuture), currentThread,
        [sendContext, tag, promise,
         linkedNotebookGuid = tag.linkedNotebookGuid()] {
            if (sendContext->canceler->isCanceled()) {
                promise->setException(OperationCanceled{});
                promise->finish();
                return;
            }

            {
                const QMutexLocker locker{sendContext->sendStatusMutex.get()};
                auto status = sendStatus(sendContext, linkedNotebookGuid);
                ++status->m_totalSuccessfullySentTags;
                Sender::sendUpdate(sendContext, status, linkedNotebookGuid);
            }

            promise->finish();
        });

    threading::onFailed(
        std::move(putTagThenFuture), currentThread,
        [sendContext, promise,
         tag = std::move(tag)](const QException & e) mutable {
            QNWARNING(
                "synchronization::Sender",
                "Failed to put tag to local storage: " << e.what());
            if (sendContext->canceler->isCanceled()) {
                promise->setException(OperationCanceled{});
                promise->finish();
                return;
            }

            Sender::processTagFailure(sendContext, std::move(tag), e, promise);
        });
}

void Sender::processTagFailure(
    const SendContextPtr & sendContext, qevercloud::Tag tag,
    const QException & e, const std::shared_ptr<QPromise<void>> & promise)
{
    Sender::checkForStopSynchronizationException(
        sendContext, tag.linkedNotebookGuid(), e);

    {
        const QMutexLocker locker{sendContext->sendStatusMutex.get()};

        if (!tag.guid()) {
            sendContext->failedToSendNewTagLocalIds.insert(tag.localId());
        }

        const auto & linkedNotebookGuid = tag.linkedNotebookGuid();
        auto status = sendStatus(sendContext, linkedNotebookGuid);

        status->m_failedToSendTags << ISendStatus::TagWithException{
            std::move(tag), std::shared_ptr<QException>(e.clone())};

        Sender::sendUpdate(sendContext, status, linkedNotebookGuid);
    }
    promise->finish();
}

QFuture<void> Sender::processNotebooks(SendContextPtr sendContext) const
{
    Q_ASSERT(sendContext);

    auto promise = std::make_shared<QPromise<void>>();
    promise->start();
    auto future = promise->future();

    const auto selfWeak = weak_from_this();
    auto * currentThread = QThread::currentThread();

    const auto listNotebooksOptions = [] {
        local_storage::ILocalStorage::ListNotebooksOptions options;
        options.m_filters.m_locallyModifiedFilter =
            local_storage::ILocalStorage::ListObjectsFilter::Include;
        options.m_filters.m_localOnlyFilter =
            local_storage::ILocalStorage::ListObjectsFilter::Exclude;
        return options;
    }();

    auto listLocallyModifiedNotebooksFuture =
        m_localStorage->listNotebooks(listNotebooksOptions);

    threading::thenOrFailed(
        std::move(listLocallyModifiedNotebooksFuture), currentThread, promise,
        threading::TrackedTask{
            selfWeak,
            [this, promise, sendContext = std::move(sendContext)](
                const QList<qevercloud::Notebook> & notebooks) mutable {
                if (sendContext->manualCanceler->isCanceled()) {
                    promise->finish();
                    return;
                }

                if (sendContext->canceler->isCanceled()) {
                    promise->setException(OperationCanceled{});
                    promise->finish();
                    return;
                }

                sendNotebooks(
                    std::move(sendContext), notebooks, std::move(promise));
            }});

    return future;
}

void Sender::sendNotebooks(
    SendContextPtr sendContext, const QList<qevercloud::Notebook> & notebooks,
    std::shared_ptr<QPromise<void>> promise) const
{
    if (notebooks.isEmpty()) {
        promise->finish();
        return;
    }

    const auto selfWeak = weak_from_this();
    auto * currentThread = QThread::currentThread();

    QList<QFuture<void>> notebookProcessingFutures;
    notebookProcessingFutures.reserve(notebooks.size());

    QFuture<void> previousNotebookFuture = threading::makeReadyFuture();
    for (auto it = notebooks.begin(), end = notebooks.end(); it != end; ++it) {
        const auto & notebook = *it;
        if (it != notebooks.begin()) {
            Q_ASSERT(!notebookProcessingFutures.isEmpty());
            previousNotebookFuture = notebookProcessingFutures.constLast();
        }

        QFuture<qevercloud::Notebook> notebookFuture = [&] {
            auto notebookPromise =
                std::make_shared<QPromise<qevercloud::Notebook>>();

            auto future = notebookPromise->future();
            notebookPromise->start();

            sendNotebook(
                sendContext, notebook, std::move(previousNotebookFuture),
                notebookPromise);

            return future;
        }();

        auto notebookProcessingPromise = std::make_shared<QPromise<void>>();
        notebookProcessingFutures << notebookProcessingPromise->future();
        notebookProcessingPromise->start();

        // NOTE: linkedNotebookGuid field of qevercloud::Notebook is not
        // serialized on thrift level and thus in the lambda right below
        // the sent notebook would not contain linkedNotebookGuid. Hence
        // passing it to the lambda manually.
        auto linkedNotebookGuid = notebook.linkedNotebookGuid();

        auto notebookThenFuture = threading::then(
            std::move(notebookFuture), currentThread,
            [sendContext, selfWeak, this,
             linkedNotebookGuid = std::move(linkedNotebookGuid),
             notebookProcessingPromise](qevercloud::Notebook notebook) {
                notebook.setLinkedNotebookGuid(linkedNotebookGuid);
                if (sendContext->canceler->isCanceled()) {
                    {
                        const QMutexLocker locker{
                            sendContext->sendStatusMutex.get()};
                        auto status =
                            sendStatus(sendContext, linkedNotebookGuid);
                        status->m_failedToSendNotebooks
                            << ISendStatus::NotebookWithException{
                                   std::move(notebook),
                                   std::make_shared<OperationCanceled>()};
                        Sender::sendUpdate(
                            sendContext, status, linkedNotebookGuid);
                    }
                    notebookProcessingPromise->finish();
                    return;
                }

                const auto self = selfWeak.lock();
                if (!self) {
                    return;
                }

                processNotebook(
                    sendContext, std::move(notebook),
                    notebookProcessingPromise);
            });

        threading::onFailed(
            std::move(notebookThenFuture), currentThread,
            [sendContext, notebook = notebook,
             notebookProcessingPromise](const QException & e) mutable {
                Sender::processNotebookFailure(
                    sendContext, std::move(notebook), e,
                    notebookProcessingPromise);
            });
    }

    auto allNotebooksProcessingFuture =
        threading::whenAll(std::move(notebookProcessingFutures));

    threading::thenOrFailed(
        std::move(allNotebooksProcessingFuture), currentThread,
        std::move(promise));
}

void Sender::sendNotebook(
    SendContextPtr sendContext, qevercloud::Notebook notebook,
    QFuture<void> previousNotebookFuture,
    const std::shared_ptr<QPromise<qevercloud::Notebook>> & notebookPromise)
    const
{
    const auto selfWeak = weak_from_this();
    auto * currentThread = QThread::currentThread();

    threading::thenOrFailed(
        std::move(previousNotebookFuture), currentThread, notebookPromise,
        threading::TrackedTask{
            selfWeak,
            [selfWeak, this, currentThread, notebookPromise,
             notebook = std::move(notebook),
             sendContext = std::move(sendContext)]() mutable {
                if (sendContext->canceler->isCanceled()) {
                    notebookPromise->setException(OperationCanceled{});
                    notebookPromise->finish();
                    return;
                }

                auto noteStoreFuture = notebook.linkedNotebookGuid()
                    ? m_noteStoreProvider->linkedNotebookNoteStore(
                          *notebook.linkedNotebookGuid(), m_ctx, m_retryPolicy)
                    : m_noteStoreProvider->userOwnNoteStore(
                          m_ctx, m_retryPolicy);

                threading::thenOrFailed(
                    std::move(noteStoreFuture), currentThread, notebookPromise,
                    threading::TrackedTask{
                        selfWeak,
                        [this, notebookPromise = notebookPromise,
                         notebook = std::move(notebook),
                         sendContext = std::move(sendContext)](
                            const qevercloud::INoteStorePtr &
                                noteStore) mutable {
                            Q_ASSERT(noteStore);
                            if (sendContext->canceler->isCanceled()) {
                                notebookPromise->setException(
                                    OperationCanceled{});
                                notebookPromise->finish();
                                return;
                            }

                            {
                                const QMutexLocker locker{
                                    sendContext->sendStatusMutex.get()};

                                auto status = sendStatus(
                                    sendContext, notebook.linkedNotebookGuid());
                                ++status->m_totalAttemptedToSendNotebooks;
                            }

                            sendNotebookImpl(
                                std::move(sendContext), std::move(notebook),
                                noteStore, std::move(notebookPromise));
                        }});
            }});
}

void Sender::sendNotebookImpl(
    SendContextPtr sendContext, qevercloud::Notebook notebook,
    const qevercloud::INoteStorePtr & noteStore,
    std::shared_ptr<QPromise<qevercloud::Notebook>> notebookPromise) const
{
    QNDEBUG(
        "synchronization::Sender", "Sender::sendNotebookImpl: " << notebook);

    const bool newNotebook = !notebook.updateSequenceNum().has_value();
    std::optional<qevercloud::Guid> linkedNotebookGuid =
        notebook.linkedNotebookGuid();

    auto * currentThread = QThread::currentThread();

    QFuture<void> thenFuture;
    if (newNotebook) {
        auto createNotebookFuture = noteStore->createNotebookAsync(notebook);
        thenFuture = threading::then(
            std::move(createNotebookFuture), currentThread,
            [notebookPromise,
             notebook = std::move(notebook)](qevercloud::Notebook n) mutable {
                n.setLocalId(notebook.localId());
                n.setLocallyFavorited(notebook.isLocallyFavorited());
                n.setLocalData(std::move(notebook.mutableLocalData()));
                n.setLocallyModified(false);
                QNDEBUG(
                    "synchronization::Sender",
                    "Created new notebook on the server: " << n);

                notebookPromise->addResult(std::move(n));
                notebookPromise->finish();
            });
    }
    else {
        auto updateNotebookFuture = noteStore->updateNotebookAsync(notebook);
        thenFuture = threading::then(
            std::move(updateNotebookFuture), currentThread,
            [notebookPromise, notebook = std::move(notebook)](
                const qint32 newUpdateSequenceNum) mutable {
                notebook.setUpdateSequenceNum(newUpdateSequenceNum);
                notebook.setLocallyModified(false);
                QNDEBUG(
                    "synchronization::Sender",
                    "Updated notebook on the server: " << notebook);

                notebookPromise->addResult(std::move(notebook));
                notebookPromise->finish();
            });
    }

    threading::onFailed(
        std::move(thenFuture), currentThread,
        [notebookPromise = std::move(notebookPromise),
         sendContext = std::move(sendContext),
         linkedNotebookGuid =
             std::move(linkedNotebookGuid)](const QException & e) {
            QNWARNING(
                "synchronization::Sender",
                "Failed to create or update notebook on the server: "
                    << e.what());
            Sender::checkForStopSynchronizationException(
                sendContext, linkedNotebookGuid, e);
            notebookPromise->setException(e);
            notebookPromise->finish();
        });
}

void Sender::processNotebook(
    const SendContextPtr & sendContext, qevercloud::Notebook notebook,
    const std::shared_ptr<QPromise<void>> & promise) const
{
    {
        Q_ASSERT(notebook.updateSequenceNum());
        const QMutexLocker locker{sendContext->sendStatusMutex.get()};
        if (notebook.updateSequenceNum()) {
            checkUpdateSequenceNumber(
                *notebook.updateSequenceNum(), sendContext,
                notebook.linkedNotebookGuid());
        }
    }

    auto * currentThread = QThread::currentThread();

    auto putNotebookFuture = m_localStorage->putNotebook(notebook);
    auto putNotebookThenFuture = threading::then(
        std::move(putNotebookFuture), currentThread,
        [sendContext, promise, notebookLocalId = notebook.localId(),
         linkedNotebookGuid = notebook.linkedNotebookGuid()] {
            if (sendContext->canceler->isCanceled()) {
                promise->setException(OperationCanceled{});
                promise->finish();
                return;
            }

            {
                const QMutexLocker locker{sendContext->sendStatusMutex.get()};

                // Will cache this notebook's local id to linked notebook guid
                // mapping until the end of the sending step of the sync just
                // in case this mapping would be needed when sending locally
                // modified notes from this notebook.
                sendContext
                    ->notebookLocalIdsToLinkedNotebookGuids[notebookLocalId] =
                    linkedNotebookGuid;

                auto status = sendStatus(sendContext, linkedNotebookGuid);
                ++status->m_totalSuccessfullySentNotebooks;
                Sender::sendUpdate(sendContext, status, linkedNotebookGuid);
            }

            promise->finish();
        });

    threading::onFailed(
        std::move(putNotebookThenFuture), currentThread,
        [sendContext, promise,
         notebook = std::move(notebook)](const QException & e) mutable {
            if (sendContext->canceler->isCanceled()) {
                promise->setException(OperationCanceled{});
                promise->finish();
                return;
            }

            Sender::processNotebookFailure(
                sendContext, std::move(notebook), e, promise);
        });
}

void Sender::processNotebookFailure(
    const SendContextPtr & sendContext, qevercloud::Notebook notebook,
    const QException & e, const std::shared_ptr<QPromise<void>> & promise)
{
    Sender::checkForStopSynchronizationException(
        sendContext, notebook.linkedNotebookGuid(), e);

    {
        const QMutexLocker locker{sendContext->sendStatusMutex.get()};

        if (!notebook.guid()) {
            sendContext->failedToSendNewNotebookLocalIds.insert(
                notebook.localId());
        }

        const auto & linkedNotebookGuid = notebook.linkedNotebookGuid();
        auto status = sendStatus(sendContext, linkedNotebookGuid);

        status->m_failedToSendNotebooks << ISendStatus::NotebookWithException{
            std::move(notebook), std::shared_ptr<QException>(e.clone())};

        Sender::sendUpdate(sendContext, status, linkedNotebookGuid);
    }
    promise->finish();
}

QFuture<void> Sender::processSavedSearches(SendContextPtr sendContext) const
{
    Q_ASSERT(sendContext);

    auto promise = std::make_shared<QPromise<void>>();
    promise->start();
    auto future = promise->future();

    const auto selfWeak = weak_from_this();
    auto * currentThread = QThread::currentThread();

    const auto listSavedSearchesOptions = [] {
        local_storage::ILocalStorage::ListSavedSearchesOptions options;
        options.m_filters.m_locallyModifiedFilter =
            local_storage::ILocalStorage::ListObjectsFilter::Include;
        options.m_filters.m_localOnlyFilter =
            local_storage::ILocalStorage::ListObjectsFilter::Exclude;
        return options;
    }();

    auto listLocallyModifiedSavedSearchesFuture =
        m_localStorage->listSavedSearches(listSavedSearchesOptions);

    threading::thenOrFailed(
        std::move(listLocallyModifiedSavedSearchesFuture), currentThread,
        promise,
        threading::TrackedTask{
            selfWeak,
            [this, promise, sendContext = std::move(sendContext)](
                const QList<qevercloud::SavedSearch> & savedSearches) mutable {
                if (sendContext->manualCanceler->isCanceled()) {
                    promise->finish();
                    return;
                }

                if (sendContext->canceler->isCanceled()) {
                    promise->setException(OperationCanceled{});
                    promise->finish();
                    return;
                }

                sendSavedSearches(
                    std::move(sendContext), savedSearches, std::move(promise));
            }});

    return future;
}

void Sender::sendSavedSearches(
    SendContextPtr sendContext,
    const QList<qevercloud::SavedSearch> & savedSearches,
    std::shared_ptr<QPromise<void>> promise) const
{
    if (savedSearches.isEmpty()) {
        promise->finish();
        return;
    }

    const auto selfWeak = weak_from_this();
    auto * currentThread = QThread::currentThread();

    QList<QFuture<void>> savedSearchProcessingFutures;
    savedSearchProcessingFutures.reserve(savedSearches.size());

    QFuture<void> previousSavedSearchFuture = threading::makeReadyFuture();
    for (auto it = savedSearches.begin(), end = savedSearches.end(); it != end;
         ++it)
    {
        const auto & savedSearch = *it;
        if (it != savedSearches.begin()) {
            Q_ASSERT(!savedSearchProcessingFutures.isEmpty());
            previousSavedSearchFuture =
                savedSearchProcessingFutures.constLast();
        }

        QFuture<qevercloud::SavedSearch> savedSearchFuture = [&] {
            auto savedSearchPromise =
                std::make_shared<QPromise<qevercloud::SavedSearch>>();

            auto future = savedSearchPromise->future();
            savedSearchPromise->start();

            sendSavedSearch(
                sendContext, savedSearch, std::move(previousSavedSearchFuture),
                savedSearchPromise);

            return future;
        }();

        auto savedSearchProcessingPromise = std::make_shared<QPromise<void>>();
        savedSearchProcessingFutures << savedSearchProcessingPromise->future();
        savedSearchProcessingPromise->start();

        auto savedSearchThenFuture = threading::then(
            std::move(savedSearchFuture), currentThread,
            [sendContext, selfWeak, this,
             savedSearchProcessingPromise](qevercloud::SavedSearch search) {
                if (sendContext->canceler->isCanceled()) {
                    {
                        const QMutexLocker locker{
                            sendContext->sendStatusMutex.get()};
                        sendContext->userOwnSendStatus
                                ->m_failedToSendSavedSearches
                            << ISendStatus::SavedSearchWithException{
                                   std::move(search),
                                   std::make_shared<OperationCanceled>()};
                        Sender::sendUpdate(
                            sendContext, sendContext->userOwnSendStatus,
                            std::nullopt);
                    }
                    savedSearchProcessingPromise->finish();
                    return;
                }

                const auto self = selfWeak.lock();
                if (!self) {
                    return;
                }

                processSavedSearch(
                    sendContext, std::move(search),
                    savedSearchProcessingPromise);
            });

        threading::onFailed(
            std::move(savedSearchThenFuture), currentThread,
            [sendContext, savedSearch = savedSearch,
             savedSearchProcessingPromise](const QException & e) mutable {
                Sender::processSavedSearchFailure(
                    sendContext, std::move(savedSearch), e,
                    savedSearchProcessingPromise);
            });
    }

    auto allSavedSearchesProcessingFuture =
        threading::whenAll(std::move(savedSearchProcessingFutures));

    threading::thenOrFailed(
        std::move(allSavedSearchesProcessingFuture), currentThread,
        std::move(promise));
}

void Sender::sendSavedSearch(
    SendContextPtr sendContext, qevercloud::SavedSearch savedSearch,
    QFuture<void> previousSavedSearchFuture,
    const std::shared_ptr<QPromise<qevercloud::SavedSearch>> &
        savedSearchPromise) const
{
    const auto selfWeak = weak_from_this();
    auto * currentThread = QThread::currentThread();

    threading::thenOrFailed(
        std::move(previousSavedSearchFuture), currentThread, savedSearchPromise,
        threading::TrackedTask{
            selfWeak,
            [selfWeak, this, currentThread,
             savedSearchPromise = savedSearchPromise,
             savedSearch = std::move(savedSearch),
             sendContext = std::move(sendContext)]() mutable {
                if (sendContext->canceler->isCanceled()) {
                    savedSearchPromise->setException(OperationCanceled{});
                    savedSearchPromise->finish();
                    return;
                }

                auto noteStoreFuture =
                    m_noteStoreProvider->userOwnNoteStore(m_ctx, m_retryPolicy);

                threading::thenOrFailed(
                    std::move(noteStoreFuture), currentThread,
                    savedSearchPromise,
                    threading::TrackedTask{
                        selfWeak,
                        [this, savedSearchPromise,
                         savedSearch = std::move(savedSearch),
                         sendContext = std::move(sendContext)](
                            const qevercloud::INoteStorePtr &
                                noteStore) mutable {
                            Q_ASSERT(noteStore);
                            if (sendContext->canceler->isCanceled()) {
                                savedSearchPromise->setException(
                                    OperationCanceled{});
                                savedSearchPromise->finish();
                                return;
                            }

                            {
                                const QMutexLocker locker{
                                    sendContext->sendStatusMutex.get()};

                                ++sendContext->userOwnSendStatus
                                      ->m_totalAttemptedToSendSavedSearches;
                            }

                            sendSavedSearchImpl(
                                std::move(sendContext), std::move(savedSearch),
                                noteStore, std::move(savedSearchPromise));
                        }});
            }});
}

void Sender::sendSavedSearchImpl(
    SendContextPtr sendContext, qevercloud::SavedSearch savedSearch,
    const qevercloud::INoteStorePtr & noteStore,
    std::shared_ptr<QPromise<qevercloud::SavedSearch>> savedSearchPromise) const
{
    QNDEBUG(
        "synchronization::Sender",
        "Sender::sendSavedSearchImpl: " << savedSearch);

    const bool newSavedSearch = !savedSearch.updateSequenceNum().has_value();
    QFuture<void> thenFuture;

    auto * currentThread = QThread::currentThread();

    if (newSavedSearch) {
        auto createSavedSearchFuture =
            noteStore->createSearchAsync(savedSearch);

        thenFuture = threading::then(
            std::move(createSavedSearchFuture), currentThread,
            [savedSearchPromise, savedSearch = std::move(savedSearch)](
                qevercloud::SavedSearch s) mutable {
                s.setLocalId(savedSearch.localId());
                s.setLocallyFavorited(savedSearch.isLocallyFavorited());
                s.setLocalData(std::move(savedSearch.mutableLocalData()));
                s.setLocallyModified(false);
                QNDEBUG(
                    "synchronization::Sender",
                    "Created new saved search on the server: " << s);

                savedSearchPromise->addResult(std::move(s));
                savedSearchPromise->finish();
            });
    }
    else {
        auto updateSavedSearchFuture =
            noteStore->updateSearchAsync(savedSearch);

        thenFuture = threading::then(
            std::move(updateSavedSearchFuture), currentThread,
            [savedSearchPromise, savedSearch = std::move(savedSearch)](
                const qint32 newUpdateSequenceNum) mutable {
                savedSearch.setUpdateSequenceNum(newUpdateSequenceNum);
                savedSearch.setLocallyModified(false);
                QNDEBUG(
                    "synchronization::Sender",
                    "Updated saved search on the server: " << savedSearch);

                savedSearchPromise->addResult(std::move(savedSearch));
                savedSearchPromise->finish();
            });
    }

    threading::onFailed(
        std::move(thenFuture), currentThread,
        [savedSearchPromise = std::move(savedSearchPromise),
         sendContext = std::move(sendContext)](const QException & e) {
            QNWARNING(
                "synchronization::Sender",
                "Failed to create or update saved search on the server: "
                    << e.what());
            Sender::checkForStopSynchronizationException(
                sendContext, std::nullopt, e);
            savedSearchPromise->setException(e);
            savedSearchPromise->finish();
        });
}

void Sender::processSavedSearch(
    const SendContextPtr & sendContext, qevercloud::SavedSearch savedSearch,
    const std::shared_ptr<QPromise<void>> & promise) const
{
    {
        Q_ASSERT(savedSearch.updateSequenceNum());
        const QMutexLocker locker{sendContext->sendStatusMutex.get()};
        if (savedSearch.updateSequenceNum()) {
            checkUpdateSequenceNumber(
                *savedSearch.updateSequenceNum(), sendContext);
        }
    }

    auto * currentThread = QThread::currentThread();

    auto putSavedSearchFuture = m_localStorage->putSavedSearch(savedSearch);
    auto putSavedSearchThenFuture = threading::then(
        std::move(putSavedSearchFuture), currentThread, [sendContext, promise] {
            if (sendContext->canceler->isCanceled()) {
                promise->setException(OperationCanceled{});
                promise->finish();
                return;
            }

            {
                const QMutexLocker locker{sendContext->sendStatusMutex.get()};

                ++sendContext->userOwnSendStatus
                      ->m_totalSuccessfullySentSavedSearches;

                if (const auto callback = sendContext->callbackWeak.lock()) {
                    auto sendStatus = std::make_shared<SendStatus>(
                        *sendContext->userOwnSendStatus);
                    callback->onUserOwnSendStatusUpdate(std::move(sendStatus));
                }
            }

            promise->finish();
        });

    threading::onFailed(
        std::move(putSavedSearchThenFuture), currentThread,
        [sendContext, promise,
         savedSearch = std::move(savedSearch)](const QException & e) mutable {
            if (sendContext->canceler->isCanceled()) {
                promise->setException(OperationCanceled{});
                promise->finish();
                return;
            }

            Sender::processSavedSearchFailure(
                sendContext, std::move(savedSearch), e, promise);
        });
}

void Sender::processSavedSearchFailure(
    const SendContextPtr & sendContext, qevercloud::SavedSearch savedSearch,
    const QException & e, const std::shared_ptr<QPromise<void>> & promise)
{
    Sender::checkForStopSynchronizationException(sendContext, std::nullopt, e);

    {
        const QMutexLocker locker{sendContext->sendStatusMutex.get()};

        sendContext->userOwnSendStatus->m_failedToSendSavedSearches
            << ISendStatus::SavedSearchWithException{
                   std::move(savedSearch),
                   std::shared_ptr<QException>(e.clone())};

        if (const auto callback = sendContext->callbackWeak.lock()) {
            auto sendStatus =
                std::make_shared<SendStatus>(*sendContext->userOwnSendStatus);
            callback->onUserOwnSendStatusUpdate(std::move(sendStatus));
        }
    }
    promise->finish();
}

SendStatusPtr Sender::sendStatus(
    const SendContextPtr & sendContext,
    const std::optional<qevercloud::Guid> & linkedNotebookGuid)
{
    Q_ASSERT(sendContext);
    if (!linkedNotebookGuid) {
        return sendContext->userOwnSendStatus;
    }

    auto & sendStatus =
        sendContext->linkedNotebookSendStatuses[*linkedNotebookGuid];

    if (!sendStatus) {
        sendStatus = std::make_shared<SendStatus>();
    }

    return sendStatus;
}

void Sender::checkForStopSynchronizationException(
    const SendContextPtr & sendContext,
    const std::optional<qevercloud::Guid> & linkedNotebookGuid,
    const QException & e)
{
    try {
        e.raise();
    }
    catch (const qevercloud::EDAMSystemException & es) {
        if (es.errorCode() == qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED) {
            {
                const QMutexLocker locker{sendContext->sendStatusMutex.get()};

                const auto status =
                    Sender::sendStatus(sendContext, linkedNotebookGuid);
                status->m_stopSynchronizationError = StopSynchronizationError{
                    RateLimitReachedError{es.rateLimitDuration()}};
            }
            sendContext->manualCanceler->cancel();
        }
        else if (es.errorCode() == qevercloud::EDAMErrorCode::AUTH_EXPIRED) {
            {
                const QMutexLocker locker{sendContext->sendStatusMutex.get()};

                const auto status =
                    Sender::sendStatus(sendContext, linkedNotebookGuid);
                status->m_stopSynchronizationError =
                    StopSynchronizationError{AuthenticationExpiredError{}};
            }
            sendContext->manualCanceler->cancel();
        }
    }
    catch (...) {
    }
}

void Sender::sendUpdate(
    const SendContextPtr & sendContext, const SendStatusPtr & sendStatus,
    const std::optional<qevercloud::Guid> & linkedNotebookGuid)
{
    Q_ASSERT(sendContext);
    if (const auto callback = sendContext->callbackWeak.lock()) {
        auto sendStatusCopy = std::make_shared<SendStatus>(*sendStatus);
        if (linkedNotebookGuid) {
            callback->onLinkedNotebookSendStatusUpdate(
                *linkedNotebookGuid, std::move(sendStatusCopy));
        }
        else {
            callback->onUserOwnSendStatusUpdate(std::move(sendStatusCopy));
        }
    }
}

std::optional<qint32> Sender::lastUpdateCount(
    const SendContext & sendContext,
    const std::optional<qevercloud::Guid> & linkedNotebookGuid) const
{
    if (linkedNotebookGuid) {
        const auto & updateCounts =
            sendContext.lastSyncState->linkedNotebookUpdateCounts();

        const auto it = updateCounts.constFind(*linkedNotebookGuid);
        if (it == updateCounts.constEnd()) {
            QNWARNING(
                "synchronization::Sender",
                "Cannot determine whether account is in sync with Evernote: "
                    << "no update count for linked notebook with guid "
                    << *linkedNotebookGuid);
            return std::nullopt;
        }

        return it.value();
    }

    return sendContext.lastSyncState->userDataUpdateCount();
}

void Sender::updateLastUpdateCount(
    qint32 updateCount, SendContext & sendContext,
    const std::optional<qevercloud::Guid> & linkedNotebookGuid) const
{
    auto & lastSyncState = sendContext.lastSyncState;
    if (linkedNotebookGuid) {
        lastSyncState->m_linkedNotebookUpdateCounts[*linkedNotebookGuid] =
            updateCount;
    }
    else {
        lastSyncState->m_userDataUpdateCount = updateCount;
    }
}

void Sender::checkUpdateSequenceNumber(
    const qint32 updateSequenceNumber, const SendContextPtr & sendContext,
    const std::optional<qevercloud::Guid> & linkedNotebookGuid) const
{
    const auto status = Sender::sendStatus(sendContext, linkedNotebookGuid);

    if (!status->needToRepeatIncrementalSync()) {
        const auto previousUpdateCount =
            lastUpdateCount(*sendContext, linkedNotebookGuid);

        if (previousUpdateCount &&
            (updateSequenceNumber != *previousUpdateCount + 1)) {
            status->m_needToRepeatIncrementalSync = true;
            QNDEBUG(
                "synchronization::Sender",
                "Detected the need to repeat incremental sync after sending: "
                    << "previous update count = " << *previousUpdateCount
                    << ", USN = " << updateSequenceNumber);
        }
    }

    updateLastUpdateCount(
        updateSequenceNumber, *sendContext, linkedNotebookGuid);
}

} // namespace quentier::synchronization
