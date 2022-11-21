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
#include <quentier/local_storage/ILocalStorage.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/synchronization/ISyncStateStorage.h>
#include <quentier/synchronization/types/IAuthenticationInfo.h>
#include <quentier/threading/Future.h>
#include <quentier/threading/TrackedTask.h>
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

    // TODO: continue from here
    Q_UNUSED(tagsFuture)
    Q_UNUSED(notebooksFuture)
    Q_UNUSED(savedSearchesFuture)

    return future;
}

void Sender::cancel(QPromise<ISender::Result> & promise)
{
    promise.setException(OperationCanceled{});
    promise.finish();
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

    auto listLocallyModifiedTagsThenFuture = threading::then(
        std::move(listLocallyModifiedTagsFuture),
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

    threading::onFailed(
        std::move(listLocallyModifiedTagsThenFuture),
        [promise](const QException & e) {
            promise->setException(e);
            promise->finish();
        });

    return future;
}

void Sender::sendTags(
    [[maybe_unused]] SendContextPtr sendContext,              // NOLINT
    [[maybe_unused]] QList<qevercloud::Tag> tags,             // NOLINT
    [[maybe_unused]] std::shared_ptr<QPromise<void>> promise) // NOLINT
    const
{
    // TODO: implement
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

    auto listLocallyModifiedNotebooksThenFuture = threading::then(
        std::move(listLocallyModifiedNotebooksFuture),
        [selfWeak, this, promise, sendContext = std::move(sendContext)](
            QList<qevercloud::Notebook> && notebooks) mutable {
            const auto self = selfWeak.lock();
            if (!self) {
                return;
            }

            if (sendContext->canceler->isCanceled()) {
                return;
            }

            sendNotebooks(
                std::move(sendContext), std::move(notebooks),
                std::move(promise));
        });

    threading::onFailed(
        std::move(listLocallyModifiedNotebooksThenFuture),
        [promise](const QException & e) {
            promise->setException(e);
            promise->finish();
        });

    return future;
}

void Sender::sendNotebooks(
    [[maybe_unused]] SendContextPtr sendContext,              // NOLINT
    [[maybe_unused]] QList<qevercloud::Notebook> notebooks,   // NOLINT
    [[maybe_unused]] std::shared_ptr<QPromise<void>> promise) // NOLINT
    const
{
    // TODO: implement
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

    auto listLocallyModifiedSavedSearchesThenFuture = threading::then(
        std::move(listLocallyModifiedSavedSearchesFuture),
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

    threading::onFailed(
        std::move(listLocallyModifiedSavedSearchesThenFuture),
        [promise](const QException & e) {
            promise->setException(e);
            promise->finish();
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

    for (const auto & savedSearch: savedSearches) {
        QFuture<qevercloud::SavedSearch> savedSearchFuture = [&] {
            QString originalLocalId = savedSearch.localId();
            auto originalLocalData = savedSearch.localData();
            const bool originalLocallyFavorited =
                savedSearch.isLocallyFavorited();

            auto savedSearchPromise =
                std::make_shared<QPromise<qevercloud::SavedSearch>>();

            auto future = savedSearchPromise->future();
            savedSearchPromise->start();

            const bool newSavedSearch =
                !savedSearch.updateSequenceNum().has_value();

            if (newSavedSearch) {
                auto createFuture = m_noteStore->createSearchAsync(
                    savedSearch, sendContext->ctx);

                auto createThenFuture = threading::then(
                    std::move(createFuture),
                    [savedSearchPromise, savedSearch,
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
                    [savedSearchPromise, savedSearch = savedSearch,
                     originalLocalId = std::move(originalLocalId),
                     originalLocallyFavorited,
                     originalLocalData = std::move(originalLocalData)](
                        const qint32 newUpdateSequenceNum) mutable {
                        savedSearch.setUpdateSequenceNum(newUpdateSequenceNum);
                        savedSearch.setLocalId(std::move(originalLocalId));
                        savedSearch.setLocallyFavorited(
                            originalLocallyFavorited);
                        savedSearch.setLocalData(std::move(originalLocalData));
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

        auto savedSearchProcessedPromise = std::make_shared<QPromise<void>>();
        savedSearchProcessingFutures << savedSearchProcessedPromise->future();
        savedSearchProcessedPromise->start();

        auto savedSearchThenFuture = threading::then(
            std::move(savedSearchFuture),
            [sendContext, selfWeak, this,
             savedSearchProcessedPromise](qevercloud::SavedSearch search) {
                if (sendContext->canceler->isCanceled()) {
                    return;
                }

                const auto self = selfWeak.lock();
                if (!self) {
                    return;
                }

                auto putSavedSearchFuture =
                    m_localStorage->putSavedSearch(search);

                auto putSavedSearchThenFuture = threading::then(
                    std::move(putSavedSearchFuture),
                    [sendContext, savedSearchProcessedPromise] {
                        if (sendContext->canceler->isCanceled()) {
                            return;
                        }

                        {
                            const QMutexLocker locker{
                                sendContext->sendStatusMutex.get()};

                            ++sendContext->sendStatus
                                  ->m_totalSuccessfullySentSavedSearches;

                            if (const auto callback =
                                    sendContext->callbackWeak.lock()) {
                                auto sendStatus = std::make_shared<SendStatus>(
                                    *sendContext->sendStatus);
                                callback->onUserOwnSendStatusUpdate(
                                    std::move(sendStatus));
                            }
                        }

                        savedSearchProcessedPromise->finish();
                    });

                threading::onFailed(
                    std::move(putSavedSearchFuture),
                    [sendContext, savedSearchProcessedPromise,
                     search = std::move(search)](const QException & e) mutable {
                        if (sendContext->canceler->isCanceled()) {
                            return;
                        }

                        {
                            const QMutexLocker locker{
                                sendContext->sendStatusMutex.get()};

                            sendContext->sendStatus->m_failedToSendSavedSearches
                                << ISendStatus::SavedSearchWithException{
                                       std::move(search),
                                       std::shared_ptr<QException>(e.clone())};

                            if (const auto callback =
                                    sendContext->callbackWeak.lock()) {
                                auto sendStatus = std::make_shared<SendStatus>(
                                    *sendContext->sendStatus);
                                callback->onUserOwnSendStatusUpdate(
                                    std::move(sendStatus));
                            }
                        }
                        savedSearchProcessedPromise->finish();
                    });
            });

        threading::onFailed(
            std::move(savedSearchThenFuture),
            [sendContext, savedSearch = savedSearch,
             savedSearchProcessedPromise](const QException & e) mutable {
                if (sendContext->canceler->isCanceled()) {
                    return;
                }

                {
                    const QMutexLocker locker{
                        sendContext->sendStatusMutex.get()};

                    sendContext->sendStatus->m_failedToSendSavedSearches
                        << ISendStatus::SavedSearchWithException{
                               std::move(savedSearch),
                               std::shared_ptr<QException>(e.clone())};

                    if (const auto callback = sendContext->callbackWeak.lock())
                    {
                        auto sendStatus = std::make_shared<SendStatus>(
                            *sendContext->sendStatus);
                        callback->onUserOwnSendStatusUpdate(
                            std::move(sendStatus));
                    }
                }
                savedSearchProcessedPromise->finish();
            });
    }

    auto allSavedSearchesProcessingFuture =
        threading::whenAll(std::move(savedSearchProcessingFutures));

    threading::thenOrFailed(
        std::move(allSavedSearchesProcessingFuture), std::move(promise));
}

} // namespace quentier::synchronization
