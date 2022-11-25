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

#include "Sender.h"
#include "Utils.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/OperationCanceled.h>
#include <quentier/exception/RuntimeError.h>
#include <quentier/local_storage/ILocalStorage.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/synchronization/ISyncStateStorage.h>
#include <quentier/synchronization/types/IAuthenticationInfo.h>
#include <quentier/threading/Future.h>
#include <quentier/threading/TrackedTask.h>
#include <quentier/utility/TagSortByParentChildRelations.h>
#include <quentier/utility/cancelers/ICanceler.h>

#include <synchronization/IAuthenticationInfoProvider.h>
#include <synchronization/types/SyncState.h>

#include <qevercloud/RequestContextBuilder.h>
#include <qevercloud/services/INoteStore.h>

#include <QMutex>
#include <QMutexLocker>

namespace quentier::synchronization {

Sender::Sender(
    Account account, IAuthenticationInfoProviderPtr authenticationInfoProvider,
    ISyncStateStoragePtr syncStateStorage, qevercloud::IRequestContextPtr ctx,
    qevercloud::INoteStorePtr noteStore,
    local_storage::ILocalStoragePtr localStorage) :
    m_account{std::move(account)},
    m_authenticationInfoProvider{std::move(authenticationInfoProvider)},
    m_syncStateStorage{std::move(syncStateStorage)}, m_ctx{std::move(ctx)},
    m_noteStore{std::move(noteStore)}, m_localStorage{std::move(localStorage)}
{
    if (Q_UNLIKELY(m_account.isEmpty())) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::Sender", "Sender ctor: account is empty")}};
    }

    if (Q_UNLIKELY(!m_authenticationInfoProvider)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::Sender",
            "Sender ctor: authentication info provider is null")}};
    }

    if (Q_UNLIKELY(!m_syncStateStorage)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::Sender",
            "Sender ctor: sync state storage is null")}};
    }

    if (Q_UNLIKELY(!m_ctx)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::Sender",
            "Sender ctor: request context is null")}};
    }

    if (Q_UNLIKELY(!m_noteStore)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::Sender", "Sender ctor: note store is null")}};
    }

    if (Q_UNLIKELY(!m_localStorage)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::Sender", "Sender ctor: local storage is null")}};
    }
}

QFuture<ISender::Result> Sender::send(
    [[maybe_unused]] utility::cancelers::ICancelerPtr canceler, // NOLINT
    [[maybe_unused]] ICallbackWeakPtr callbackWeak)             // NOLINT
{
    QNDEBUG("synchronization::Sender", "Sender::send");

    auto lastSyncState = readLastSyncState(m_syncStateStorage, m_account);
    Q_ASSERT(lastSyncState);

    QNDEBUG("synchronization::Sender", "Last sync state: " << *lastSyncState);

    auto promise = std::make_shared<QPromise<Result>>();
    auto future = promise->future();
    promise->start();

    if (Q_UNLIKELY(canceler->isCanceled())) {
        cancel(*promise);
        return promise->future();
    }

    auto authenticationInfoFuture =
        m_authenticationInfoProvider->authenticateAccount(
            m_account, IAuthenticationInfoProvider::Mode::Cache);

    threading::bindCancellation(future, authenticationInfoFuture);

    const auto selfWeak = weak_from_this();

    threading::thenOrFailed(
        std::move(authenticationInfoFuture), promise,
        threading::TrackedTask{
            selfWeak,
            [this, selfWeak, callbackWeak = std::move(callbackWeak),
             canceler = std::move(canceler),
             lastSyncState = std::move(lastSyncState), promise](
                const IAuthenticationInfoPtr & authenticationInfo) mutable {
                Q_ASSERT(authenticationInfo);

                if (canceler->isCanceled()) {
                    cancel(*promise);
                    return;
                }

                auto sendFuture = launchSend(
                    *authenticationInfo, std::move(lastSyncState),
                    std::move(canceler), std::move(callbackWeak));

                threading::bindCancellation(promise->future(), sendFuture);

                threading::mapFutureProgress(sendFuture, promise);

                threading::thenOrFailed(
                    std::move(sendFuture), promise, [promise](Result result) {
                        promise->addResult(std::move(result));
                        promise->finish();
                    });
            }});

    return future;
}

QFuture<ISender::Result> Sender::launchSend(
    const IAuthenticationInfo & authenticationInfo,
    SyncStateConstPtr lastSyncState, utility::cancelers::ICancelerPtr canceler,
    ICallbackWeakPtr callbackWeak)
{
    Q_ASSERT(lastSyncState);

    auto promise = std::make_shared<QPromise<Result>>();
    auto future = promise->future();

    promise->start();

    auto ctx = qevercloud::RequestContextBuilder{}
                   .setAuthenticationToken(authenticationInfo.authToken())
                   .setCookies(authenticationInfo.userStoreCookies())
                   .setRequestTimeout(m_ctx->requestTimeout())
                   .setIncreaseRequestTimeoutExponentially(
                       m_ctx->increaseRequestTimeoutExponentially())
                   .setMaxRequestTimeout(m_ctx->maxRequestTimeout())
                   .setMaxRetryCount(m_ctx->maxRequestRetryCount())
                   .build();

    auto sendContext = std::make_shared<SendContext>();
    sendContext->lastSyncState = std::move(lastSyncState);
    sendContext->promise = promise;
    sendContext->ctx = std::move(ctx);
    sendContext->canceler = std::move(canceler);
    sendContext->callbackWeak = std::move(callbackWeak);

    // The whole sending process would be done in two steps:
    // 1. First send locally modified tags, notebooks and saved searches
    // 2. Only when tags and notebooks are sent, send locally modified notes
    //    but:
    //    a) don't attempt to send notes which belong to new notebooks which
    //       failed to be sent
    //    b) do send notes containing tags which failed to be sent but filter
    //       out tags which failed to be sent and leave the notes marked as
    //       locally modified (because they are - the modification is the
    //       addition of a tag not yet sent).
    QFuture<void> tagsFuture = processTags(sendContext);
    QFuture<void> notebooksFuture = processNotebooks(sendContext);
    QFuture<void> savedSearchesFuture = processSavedSearches(sendContext);

    QFuture<void> notesFuture = [&] {
        auto notesPromise = std::make_shared<QPromise<void>>();
        auto notesFuture = notesPromise->future();
        notesPromise->start();

        auto tagsAndNotebooksFuture = threading::whenAll(
            QList<QFuture<void>>{} << tagsFuture << notebooksFuture);

        const auto selfWeak = weak_from_this();

        auto tagsAndNotebooksThenFuture = threading::then(
            std::move(tagsAndNotebooksFuture),
            threading::TrackedTask{
                selfWeak, [this, sendContext, notesPromise]() mutable {
                    auto f = processNotes(sendContext);
                    threading::thenOrFailed(
                        std::move(f), std::move(notesPromise));
                }});

        return notesFuture;
    }();

    // Can skip checking for tagsFuture and notebooksFuture here because
    // it's known that processing of notes would start only after tags and
    // notebooks are fully processed.
    auto allFutures = threading::whenAll(
        QList<QFuture<void>>{} << notesFuture << savedSearchesFuture);

    threading::thenOrFailed(
        std::move(allFutures), promise, [promise, sendContext] {
            const QMutexLocker locker{sendContext->sendStatusMutex.get()};
            ISender::Result result;
            result.userOwnResult = sendContext->userOwnSendStatus;
            result.linkedNotebookResults.reserve(
                sendContext->linkedNotebookSendStatuses.size());
            for (const auto it: qevercloud::toRange(
                     qAsConst(sendContext->linkedNotebookSendStatuses)))
            {
                result.linkedNotebookResults[it.key()] = it.value();
            }

            promise->addResult(std::move(result));
            promise->finish();
        });

    return future;
}

void Sender::cancel(QPromise<ISender::Result> & promise)
{
    promise.setException(OperationCanceled{});
    promise.finish();
}

QFuture<void> Sender::processNotes(SendContextPtr sendContext) const
{
    Q_ASSERT(sendContext);

    auto promise = std::make_shared<QPromise<void>>();
    promise->start();
    auto future = promise->future();

    const auto selfWeak = weak_from_this();

    const auto listNotesOptions = [] {
        local_storage::ILocalStorage::ListNotesOptions options;
        options.m_filters.m_locallyModifiedFilter =
            local_storage::ILocalStorage::ListObjectsFilter::Include;
        return options;
    }();

    const auto fetchNoteOptions =
        local_storage::ILocalStorage::FetchNoteOptions{} |
        local_storage::ILocalStorage::FetchNoteOption::WithResourceMetadata |
        local_storage::ILocalStorage::FetchNoteOption::WithResourceBinaryData;

    auto listLocallyModifiedNotesFuture =
        m_localStorage->listNotes(fetchNoteOptions, listNotesOptions);

    threading::thenOrFailed(
        std::move(listLocallyModifiedNotesFuture), promise,
        [selfWeak, this, promise, sendContext = std::move(sendContext)](
            QList<qevercloud::Note> && notes) mutable {
            const auto self = selfWeak.lock();
            if (!self) {
                return;
            }

            if (sendContext->canceler->isCanceled()) {
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
    QList<QFuture<void>> noteProcessingFutures;
    noteProcessingFutures.reserve(notes.size());

    for (const auto & note: qAsConst(notes)) {
        std::optional<qevercloud::Note> modifiedNote;
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
                    std::make_shared<RuntimeError>(
                        ErrorString{QT_TRANSLATE_NOOP(
                            "synchronization::Sender",
                            "Cannot send note which notebook could not be "
                            "sent")})};

                Sender::sendUpdate(sendContext, status, std::nullopt);
                continue;
            }

            const QStringList & tagLocalIds = note.tagLocalIds();
            std::optional<QStringList> filteredTagLocalIds;
            for (const QString & tagLocalId: qAsConst(tagLocalIds)) {
                if (sendContext->failedToSendNewTagLocalIds.contains(
                        tagLocalId)) {
                    if (!filteredTagLocalIds) {
                        filteredTagLocalIds.emplace(tagLocalIds);
                    }

                    filteredTagLocalIds->removeOne(tagLocalId);
                }
            }

            if (filteredTagLocalIds) {
                modifiedNote.emplace(note);
                modifiedNote->setTagLocalIds(*filteredTagLocalIds);
            }
        }

        QFuture<qevercloud::Note> noteFuture = [&] {
            const bool noteTagListModified = modifiedNote.has_value();

            const qevercloud::Note & noteToSend =
                (modifiedNote ? *modifiedNote : note);

            auto notePromise = std::make_shared<QPromise<qevercloud::Note>>();

            auto future = notePromise->future();
            notePromise->start();

            // Unfiltered tag local ids, even those which failed to be sent
            // to Evernote
            auto originalTagLocalIds = note.tagLocalIds();
            QString originalLocalId = noteToSend.localId();
            auto originalLocalData = noteToSend.localData();
            const bool originalLocallyFavorited =
                noteToSend.isLocallyFavorited();

            const bool newNote = !noteToSend.updateSequenceNum().has_value();

            auto noteFuture =
                (newNote ? m_noteStore->createNoteAsync(
                               noteToSend, sendContext->ctx)
                         : m_noteStore->updateNoteAsync(
                               noteToSend, sendContext->ctx));

            auto noteThenFuture = threading::then(
                std::move(noteFuture),
                [notePromise, noteTagListModified,
                 originalLocalId = std::move(originalLocalId),
                 originalLocallyFavorited,
                 originalLocalData = std::move(originalLocalData),
                 originalTagLocalIds = std::move(originalTagLocalIds)](
                    qevercloud::Note n) mutable {
                    n.setLocalId(std::move(originalLocalId));
                    n.setLocallyFavorited(originalLocallyFavorited);
                    n.setLocalData(std::move(originalLocalData));
                    if (noteTagListModified) {
                        n.setTagLocalIds(std::move(originalTagLocalIds));
                    }
                    n.setLocallyModified(!noteTagListModified);
                    notePromise->addResult(std::move(n));
                    notePromise->finish();
                });

            threading::onFailed(
                std::move(noteThenFuture), [notePromise](const QException & e) {
                    notePromise->setException(e);
                    notePromise->finish();
                });

            return future;
        }();

        auto noteProcessingPromise = std::make_shared<QPromise<void>>();
        noteProcessingFutures << noteProcessingPromise->future();
        noteProcessingPromise->start();

        auto noteThenFuture = threading::then(
            std::move(noteFuture),
            [sendContext, selfWeak, this,
             noteProcessingPromise](qevercloud::Note note) {
                if (sendContext->canceler->isCanceled()) {
                    return;
                }

                const auto self = selfWeak.lock();
                if (!self) {
                    return;
                }

                processNote(
                    sendContext, std::move(note), noteProcessingPromise);
            });

        threading::onFailed(
            std::move(noteThenFuture),
            [selfWeak, this, sendContext, note = note,
             noteProcessingPromise](const QException & e) mutable {
                if (sendContext->canceler->isCanceled()) {
                    return;
                }

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
        std::move(allNotesProcessingFuture), std::move(promise));
}

void Sender::processNote(
    const SendContextPtr & sendContext, qevercloud::Note note,
    const std::shared_ptr<QPromise<void>> & promise) const
{
    auto putNoteFuture = m_localStorage->putNote(note);
    auto putNoteThenFuture = threading::then(
        std::move(putNoteFuture),
        [sendContext, promise, notebookLocalId = note.notebookLocalId()] {
            if (sendContext->canceler->isCanceled()) {
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
                    auto status =
                        sendStatus(sendContext, linkedNotebookGuidIt.value());

                    ++status->m_totalSuccessfullySentNotes;
                    Sender::sendUpdate(
                        sendContext, status, linkedNotebookGuidIt.value());
                    return;
                }

                // We don't have cached info on linked notebook guid for this
                // notebook yet, need to find it out
                // TODO: actually find it out
            }

            promise->finish();
        });

    const auto selfWeak = weak_from_this();
    threading::onFailed(
        std::move(putNoteThenFuture),
        [selfWeak, this, sendContext, promise,
         note = std::move(note)](const QException & e) mutable {
            if (sendContext->canceler->isCanceled()) {
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
    [[maybe_unused]] const SendContextPtr & sendContext,
    [[maybe_unused]] qevercloud::Note note, // NOLINT
    [[maybe_unused]] const QException & e,
    [[maybe_unused]] const std::shared_ptr<QPromise<void>> & promise) const
{
    // TODO: implement
}

QFuture<void> Sender::processTags(SendContextPtr sendContext) const
{
    Q_ASSERT(sendContext);

    auto promise = std::make_shared<QPromise<void>>();
    promise->start();
    auto future = promise->future();

    const auto selfWeak = weak_from_this();

    const auto listTagsOptions = [] {
        local_storage::ILocalStorage::ListTagsOptions options;
        options.m_filters.m_locallyModifiedFilter =
            local_storage::ILocalStorage::ListObjectsFilter::Include;
        return options;
    }();

    auto listLocallyModifiedTagsFuture =
        m_localStorage->listTags(listTagsOptions);

    threading::thenOrFailed(
        std::move(listLocallyModifiedTagsFuture), promise,
        [selfWeak, this, promise, sendContext = std::move(sendContext)](
            QList<qevercloud::Tag> && tags) mutable {
            const auto self = selfWeak.lock();
            if (!self) {
                return;
            }

            if (sendContext->canceler->isCanceled()) {
                return;
            }

            sendTags(
                std::move(sendContext), std::move(tags), std::move(promise));
        });

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
    // some tag can be children of some other tag. Due to that, we need to
    // process tags strictly sequentially, one by one, and in proper order:
    // parent tags go first, child tags go next.

    ErrorString errorDescription;
    if (!sortTagsByParentChildRelations(tags, errorDescription)) {
        promise->setException(RuntimeError{std::move(errorDescription)});
        promise->finish();
        return;
    }

    const auto selfWeak = weak_from_this();
    QList<QFuture<void>> tagProcessingFutures;
    tagProcessingFutures.reserve(tags.size());

    QFuture<void> previousTagFuture = threading::makeReadyFuture();
    for (int index = 0, size = tags.size(); index < size; ++index) {
        const auto & tag = tags[index];

        auto tagParentLocalId = tag.parentTagLocalId();
        if (!tagParentLocalId.isEmpty()) {
            const QMutexLocker locker{sendContext->sendStatusMutex.get()};
            if (sendContext->failedToSendNewTagLocalIds.contains(
                    tagParentLocalId)) {
                if (!tag.guid()) {
                    sendContext->failedToSendNewTagLocalIds.insert(
                        tag.localId());
                }

                auto status = sendStatus(sendContext, tag.linkedNotebookGuid());

                status->m_failedToSendTags << ISendStatus::TagWithException{
                    tag,
                    std::make_shared<RuntimeError>(
                        ErrorString{QT_TRANSLATE_NOOP(
                            "synchronization::Sender",
                            "Cannot send tag which parent also could not be "
                            "sent")})};

                Sender::sendUpdate(
                    sendContext, status, tag.linkedNotebookGuid());

                continue;
            }
        }

        if (index != 0) {
            Q_ASSERT(!tagProcessingFutures.isEmpty());
            previousTagFuture = tagProcessingFutures.constLast();
        }

        QFuture<qevercloud::Tag> tagFuture = [&] {
            auto tagPromise = std::make_shared<QPromise<qevercloud::Tag>>();
            auto future = tagPromise->future();
            tagPromise->start();

            const bool newTag = !tag.updateSequenceNum().has_value();

            if (newTag) {
                QString originalLocalId = tag.localId();
                auto originalLocalData = tag.localData();
                const bool originalLocallyFavorited = tag.isLocallyFavorited();

                auto createTagThenFuture = threading::then(
                    std::move(previousTagFuture),
                    threading::TrackedTask{
                        selfWeak,
                        [this, tagPromise, tag, sendContext,
                         originalLocalId = std::move(originalLocalId),
                         originalLocallyFavorited,
                         originalLocalData = std::move(originalLocalData),
                         originalParentTagLocalId =
                             std::move(tagParentLocalId)]() mutable {
                            if (sendContext->canceler->isCanceled()) {
                                return;
                            }

                            auto createFuture = m_noteStore->createTagAsync(
                                tag, sendContext->ctx);

                            auto createThenFuture = threading::then(
                                std::move(createFuture),
                                [tagPromise,
                                 originalLocalId = std::move(originalLocalId),
                                 originalLocallyFavorited,
                                 originalLocalData =
                                     std::move(originalLocalData),
                                 originalParentTagLocalId =
                                     std::move(originalParentTagLocalId)](
                                    qevercloud::Tag t) mutable {
                                    t.setLocalId(std::move(originalLocalId));
                                    t.setLocallyFavorited(
                                        originalLocallyFavorited);
                                    t.setLocalData(
                                        std::move(originalLocalData));
                                    t.setParentTagLocalId(
                                        std::move(originalParentTagLocalId));
                                    t.setLocallyModified(false);
                                    tagPromise->addResult(std::move(t));
                                });

                            threading::onFailed(
                                std::move(createThenFuture),
                                [tagPromise](const QException & e) {
                                    tagPromise->setException(e);
                                    tagPromise->finish();
                                });
                        }});

                threading::onFailed(
                    std::move(createTagThenFuture),
                    [tagPromise](const QException & e) {
                        tagPromise->setException(e);
                        tagPromise->finish();
                    });
            }
            else {
                auto updateTagThenFuture = threading::then(
                    std::move(previousTagFuture),
                    threading::TrackedTask{
                        selfWeak,
                        [this, tag, tagPromise, sendContext]() mutable {
                            if (sendContext->canceler->isCanceled()) {
                                return;
                            }

                            auto updateFuture = m_noteStore->updateTagAsync(
                                tag, sendContext->ctx);

                            auto updateThenFuture = threading::then(
                                std::move(updateFuture),
                                [tagPromise, tag = tag](
                                    const qint32 newUpdateSequenceNum) mutable {
                                    tag.setUpdateSequenceNum(
                                        newUpdateSequenceNum);
                                    tag.setLocallyModified(false);
                                    tagPromise->addResult(std::move(tag));
                                    tagPromise->finish();
                                });

                            threading::onFailed(
                                std::move(updateThenFuture),
                                [tagPromise](const QException & e) {
                                    tagPromise->setException(e);
                                    tagPromise->finish();
                                });
                        }});

                threading::onFailed(
                    std::move(updateTagThenFuture),
                    [tagPromise](const QException & e) {
                        tagPromise->setException(e);
                        tagPromise->finish();
                    });
            }

            return future;
        }();

        auto tagProcessingPromise = std::make_shared<QPromise<void>>();
        tagProcessingFutures << tagProcessingPromise->future();
        tagProcessingPromise->start();

        auto tagThenFuture = threading::then(
            std::move(tagFuture),
            [sendContext, selfWeak, this,
             tagProcessingPromise](qevercloud::Tag tag) {
                if (sendContext->canceler->isCanceled()) {
                    return;
                }

                const auto self = selfWeak.lock();
                if (!self) {
                    return;
                }

                processTag(sendContext, std::move(tag), tagProcessingPromise);
            });

        threading::onFailed(
            std::move(tagThenFuture),
            [sendContext, tag = tag,
             tagProcessingPromise](const QException & e) mutable {
                if (sendContext->canceler->isCanceled()) {
                    return;
                }

                Sender::processTagFailure(
                    sendContext, std::move(tag), e, tagProcessingPromise);
            });
    }

    auto allTagsProcessingFuture =
        threading::whenAll(std::move(tagProcessingFutures));

    threading::thenOrFailed(
        std::move(allTagsProcessingFuture), std::move(promise));
}

void Sender::processTag(
    const SendContextPtr & sendContext, qevercloud::Tag tag,
    const std::shared_ptr<QPromise<void>> & promise) const
{
    auto putTagFuture = m_localStorage->putTag(tag);
    auto putTagThenFuture = threading::then(
        std::move(putTagFuture),
        [sendContext, promise, linkedNotebookGuid = tag.linkedNotebookGuid()] {
            if (sendContext->canceler->isCanceled()) {
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
        std::move(putTagThenFuture),
        [sendContext, promise,
         tag = std::move(tag)](const QException & e) mutable {
            if (sendContext->canceler->isCanceled()) {
                return;
            }

            Sender::processTagFailure(sendContext, std::move(tag), e, promise);
        });
}

void Sender::processTagFailure(
    const SendContextPtr & sendContext, qevercloud::Tag tag,
    const QException & e, const std::shared_ptr<QPromise<void>> & promise)
{
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

    const auto listNotebooksOptions = [] {
        local_storage::ILocalStorage::ListNotebooksOptions options;
        options.m_filters.m_locallyModifiedFilter =
            local_storage::ILocalStorage::ListObjectsFilter::Include;
        return options;
    }();

    auto listLocallyModifiedNotebooksFuture =
        m_localStorage->listNotebooks(listNotebooksOptions);

    threading::thenOrFailed(
        std::move(listLocallyModifiedNotebooksFuture), promise,
        [selfWeak, this, promise, sendContext = std::move(sendContext)](
            const QList<qevercloud::Notebook> & notebooks) mutable {
            const auto self = selfWeak.lock();
            if (!self) {
                return;
            }

            if (sendContext->canceler->isCanceled()) {
                return;
            }

            sendNotebooks(
                std::move(sendContext), notebooks, std::move(promise));
        });

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
    QList<QFuture<void>> notebookProcessingFutures;
    notebookProcessingFutures.reserve(notebooks.size());

    for (const auto & notebook: qAsConst(notebooks)) {
        QFuture<qevercloud::Notebook> notebookFuture = [&] {
            auto notebookPromise =
                std::make_shared<QPromise<qevercloud::Notebook>>();

            auto future = notebookPromise->future();
            notebookPromise->start();

            const bool newNotebook = !notebook.updateSequenceNum().has_value();

            if (newNotebook) {
                QString originalLocalId = notebook.localId();
                auto originalLocalData = notebook.localData();
                const bool originalLocallyFavorited =
                    notebook.isLocallyFavorited();

                auto createFuture = m_noteStore->createNotebookAsync(
                    notebook, sendContext->ctx);

                auto createThenFuture = threading::then(
                    std::move(createFuture),
                    [notebookPromise,
                     originalLocalId = std::move(originalLocalId),
                     originalLocallyFavorited,
                     originalLocalData = std::move(originalLocalData)](
                        qevercloud::Notebook n) mutable {
                        n.setLocalId(std::move(originalLocalId));
                        n.setLocallyFavorited(originalLocallyFavorited);
                        n.setLocalData(std::move(originalLocalData));
                        n.setLocallyModified(false);
                        notebookPromise->addResult(std::move(n));
                        notebookPromise->finish();
                    });

                threading::onFailed(
                    std::move(createThenFuture),
                    [notebookPromise](const QException & e) {
                        notebookPromise->setException(e);
                        notebookPromise->finish();
                    });
            }
            else {
                auto updateFuture = m_noteStore->updateNotebookAsync(
                    notebook, sendContext->ctx);

                auto updateThenFuture = threading::then(
                    std::move(updateFuture),
                    [notebookPromise, notebook = notebook](
                        const qint32 newUpdateSequenceNum) mutable {
                        notebook.setUpdateSequenceNum(newUpdateSequenceNum);
                        notebook.setLocallyModified(false);
                        notebookPromise->addResult(std::move(notebook));
                        notebookPromise->finish();
                    });

                threading::onFailed(
                    std::move(updateThenFuture),
                    [notebookPromise](const QException & e) {
                        notebookPromise->setException(e);
                        notebookPromise->finish();
                    });
            }

            return future;
        }();

        auto notebookProcessingPromise = std::make_shared<QPromise<void>>();
        notebookProcessingFutures << notebookProcessingPromise->future();
        notebookProcessingPromise->start();

        auto notebookThenFuture = threading::then(
            std::move(notebookFuture),
            [sendContext, selfWeak, this,
             notebookProcessingPromise](qevercloud::Notebook notebook) {
                if (sendContext->canceler->isCanceled()) {
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
            std::move(notebookThenFuture),
            [sendContext, notebook = notebook,
             notebookProcessingPromise](const QException & e) mutable {
                if (sendContext->canceler->isCanceled()) {
                    return;
                }

                Sender::processNotebookFailure(
                    sendContext, std::move(notebook), e,
                    notebookProcessingPromise);
            });
    }

    auto allNotebooksProcessingFuture =
        threading::whenAll(std::move(notebookProcessingFutures));

    threading::thenOrFailed(
        std::move(allNotebooksProcessingFuture), std::move(promise));
}

void Sender::processNotebook(
    const SendContextPtr & sendContext, qevercloud::Notebook notebook,
    const std::shared_ptr<QPromise<void>> & promise) const
{
    auto putNotebookFuture = m_localStorage->putNotebook(notebook);
    auto putNotebookThenFuture = threading::then(
        std::move(putNotebookFuture),
        [sendContext, promise,
         linkedNotebookGuid = notebook.linkedNotebookGuid()] {
            if (sendContext->canceler->isCanceled()) {
                return;
            }

            {
                const QMutexLocker locker{sendContext->sendStatusMutex.get()};
                auto status = sendStatus(sendContext, linkedNotebookGuid);
                ++status->m_totalSuccessfullySentNotebooks;
                Sender::sendUpdate(sendContext, status, linkedNotebookGuid);
            }

            promise->finish();
        });

    threading::onFailed(
        std::move(putNotebookThenFuture),
        [sendContext, promise,
         notebook = std::move(notebook)](const QException & e) mutable {
            if (sendContext->canceler->isCanceled()) {
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

    const auto listSavedSearchesOptions = [] {
        local_storage::ILocalStorage::ListSavedSearchesOptions options;
        options.m_filters.m_locallyModifiedFilter =
            local_storage::ILocalStorage::ListObjectsFilter::Include;
        return options;
    }();

    auto listLocallyModifiedSavedSearchesFuture =
        m_localStorage->listSavedSearches(listSavedSearchesOptions);

    threading::thenOrFailed(
        std::move(listLocallyModifiedSavedSearchesFuture), promise,
        [selfWeak, this, promise, sendContext = std::move(sendContext)](
            const QList<qevercloud::SavedSearch> & savedSearches) mutable {
            const auto self = selfWeak.lock();
            if (!self) {
                return;
            }

            if (sendContext->canceler->isCanceled()) {
                return;
            }

            sendSavedSearches(
                std::move(sendContext), savedSearches, std::move(promise));
        });

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
    QList<QFuture<void>> savedSearchProcessingFutures;
    savedSearchProcessingFutures.reserve(savedSearches.size());

    for (const auto & savedSearch: qAsConst(savedSearches)) {
        QFuture<qevercloud::SavedSearch> savedSearchFuture = [&] {
            auto savedSearchPromise =
                std::make_shared<QPromise<qevercloud::SavedSearch>>();

            auto future = savedSearchPromise->future();
            savedSearchPromise->start();

            const bool newSavedSearch =
                !savedSearch.updateSequenceNum().has_value();

            if (newSavedSearch) {
                QString originalLocalId = savedSearch.localId();
                auto originalLocalData = savedSearch.localData();
                const bool originalLocallyFavorited =
                    savedSearch.isLocallyFavorited();

                auto createFuture = m_noteStore->createSearchAsync(
                    savedSearch, sendContext->ctx);

                auto createThenFuture = threading::then(
                    std::move(createFuture),
                    [savedSearchPromise,
                     originalLocalId = std::move(originalLocalId),
                     originalLocallyFavorited,
                     originalLocalData = std::move(originalLocalData)](
                        qevercloud::SavedSearch search) mutable {
                        search.setLocalId(std::move(originalLocalId));
                        search.setLocallyFavorited(originalLocallyFavorited);
                        search.setLocalData(std::move(originalLocalData));
                        search.setLocallyModified(false);
                        savedSearchPromise->addResult(std::move(search));
                        savedSearchPromise->finish();
                    });

                threading::onFailed(
                    std::move(createThenFuture),
                    [savedSearchPromise](const QException & e) {
                        savedSearchPromise->setException(e);
                        savedSearchPromise->finish();
                    });
            }
            else {
                auto updateFuture = m_noteStore->updateSearchAsync(
                    savedSearch, sendContext->ctx);

                auto updateThenFuture = threading::then(
                    std::move(updateFuture),
                    [savedSearchPromise, savedSearch = savedSearch](
                        const qint32 newUpdateSequenceNum) mutable {
                        savedSearch.setUpdateSequenceNum(newUpdateSequenceNum);
                        savedSearch.setLocallyModified(false);
                        savedSearchPromise->addResult(std::move(savedSearch));
                        savedSearchPromise->finish();
                    });

                threading::onFailed(
                    std::move(updateThenFuture),
                    [savedSearchPromise](const QException & e) {
                        savedSearchPromise->setException(e);
                        savedSearchPromise->finish();
                    });
            }

            return future;
        }();

        auto savedSearchProcessingPromise = std::make_shared<QPromise<void>>();
        savedSearchProcessingFutures << savedSearchProcessingPromise->future();
        savedSearchProcessingPromise->start();

        auto savedSearchThenFuture = threading::then(
            std::move(savedSearchFuture),
            [sendContext, selfWeak, this,
             savedSearchProcessingPromise](qevercloud::SavedSearch search) {
                if (sendContext->canceler->isCanceled()) {
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
            std::move(savedSearchThenFuture),
            [sendContext, savedSearch = savedSearch,
             savedSearchProcessingPromise](const QException & e) mutable {
                if (sendContext->canceler->isCanceled()) {
                    return;
                }

                Sender::processSavedSearchFailure(
                    sendContext, std::move(savedSearch), e,
                    savedSearchProcessingPromise);
            });
    }

    auto allSavedSearchesProcessingFuture =
        threading::whenAll(std::move(savedSearchProcessingFutures));

    threading::thenOrFailed(
        std::move(allSavedSearchesProcessingFuture), std::move(promise));
}

void Sender::processSavedSearch(
    const SendContextPtr & sendContext, qevercloud::SavedSearch savedSearch,
    const std::shared_ptr<QPromise<void>> & promise) const
{
    auto putSavedSearchFuture = m_localStorage->putSavedSearch(savedSearch);
    auto putSavedSearchThenFuture = threading::then(
        std::move(putSavedSearchFuture), [sendContext, promise] {
            if (sendContext->canceler->isCanceled()) {
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
        std::move(putSavedSearchThenFuture),
        [sendContext, promise,
         savedSearch = std::move(savedSearch)](const QException & e) mutable {
            if (sendContext->canceler->isCanceled()) {
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
    return linkedNotebookGuid
        ? sendContext->linkedNotebookSendStatuses[*linkedNotebookGuid]
        : sendContext->userOwnSendStatus;
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

} // namespace quentier::synchronization
