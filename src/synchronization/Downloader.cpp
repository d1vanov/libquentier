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

#include "Downloader.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/OperationCanceled.h>
#include <quentier/local_storage/ILocalStorage.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/synchronization/ISyncStateStorage.h>
#include <quentier/threading/Future.h>
#include <quentier/threading/TrackedTask.h>
#include <quentier/utility/cancelers/ICanceler.h>

#include <synchronization/IAccountLimitsProvider.h>
#include <synchronization/IAuthenticationInfoProvider.h>
#include <synchronization/IFullSyncStaleDataExpunger.h>
#include <synchronization/IProtocolVersionChecker.h>
#include <synchronization/IUserInfoProvider.h>
#include <synchronization/SyncChunksDataCounters.h>
#include <synchronization/processors/IDurableNotesProcessor.h>
#include <synchronization/processors/IDurableResourcesProcessor.h>
#include <synchronization/processors/ILinkedNotebooksProcessor.h>
#include <synchronization/processors/INotebooksProcessor.h>
#include <synchronization/processors/ISavedSearchesProcessor.h>
#include <synchronization/processors/ITagsProcessor.h>
#include <synchronization/sync_chunks/ISyncChunksProvider.h>
#include <synchronization/types/DownloadNotesStatus.h>
#include <synchronization/types/DownloadResourcesStatus.h>

#include <qevercloud/RequestContextBuilder.h>
#include <qevercloud/services/INoteStore.h>

namespace quentier::synchronization {

namespace {

[[nodiscard]] IDownloader::Result createEmptyResult()
{
    return IDownloader::Result{
        std::make_shared<SyncChunksDataCounters>(),
        std::make_shared<DownloadNotesStatus>(),
        std::make_shared<DownloadResourcesStatus>()};
}

class SyncChunksProviderCallback final : public ISyncChunksProvider::ICallback
{
public:
    explicit SyncChunksProviderCallback(
        IDownloader::ICallbackWeakPtr callbackWeak) :
        m_callbackWeak{std::move(callbackWeak)}
    {}

    // ISyncChunksProvider::ICallback
    void onUserOwnSyncChunksDownloadProgress(
        qint32 highestDownloadedUsn, qint32 highestServerUsn,
        qint32 lastPreviousUsn) override
    {
        if (const auto callback = m_callbackWeak.lock()) {
            callback->onSyncChunksDownloadProgress(
                highestDownloadedUsn, highestServerUsn, lastPreviousUsn);
        }
    }

    void onLinkedNotebookSyncChunksDownloadProgress(
        qint32 highestDownloadedUsn, qint32 highestServerUsn,
        qint32 lastPreviousUsn,
        qevercloud::LinkedNotebook linkedNotebook) override
    {
        if (const auto callback = m_callbackWeak.lock()) {
            callback->onLinkedNotebookSyncChunksDownloadProgress(
                highestDownloadedUsn, highestServerUsn, lastPreviousUsn,
                std::move(linkedNotebook));
        }
    }

private:
    const IDownloader::ICallbackWeakPtr m_callbackWeak;
};

[[nodiscard]] IFullSyncStaleDataExpunger::PreservedGuids collectPreservedGuids(
    const QList<qevercloud::SyncChunk> & syncChunks)
{
    IFullSyncStaleDataExpunger::PreservedGuids preservedGuids;

    for (const auto & syncChunk: qAsConst(syncChunks)) {
        const auto & notebooks = syncChunk.notebooks();
        if (notebooks) {
            for (const auto & notebook: qAsConst(*notebooks)) {
                if (notebook.guid()) {
                    preservedGuids.notebookGuids.insert(*notebook.guid());
                }
            }
        }

        const auto & tags = syncChunk.tags();
        if (tags) {
            for (const auto & tag: qAsConst(*tags)) {
                if (tag.guid()) {
                    preservedGuids.tagGuids.insert(*tag.guid());
                }
            }
        }

        const auto & notes = syncChunk.notes();
        if (notes) {
            for (const auto & note: qAsConst(*notes)) {
                if (note.guid()) {
                    preservedGuids.noteGuids.insert(*note.guid());
                }
            }
        }

        const auto & savedSearches = syncChunk.searches();
        if (savedSearches) {
            for (const auto & savedSearch: qAsConst(*savedSearches)) {
                if (savedSearch.guid()) {
                    preservedGuids.savedSearchGuids.insert(*savedSearch.guid());
                }
            }
        }
    }

    return preservedGuids;
}

} // namespace

Downloader::Downloader(
    Account account, IAuthenticationInfoProviderPtr authenticationInfoProvider,
    IProtocolVersionCheckerPtr protocolVersionChecker,
    IUserInfoProviderPtr userInfoProvider,
    IAccountLimitsProviderPtr accountLimitsProvider,
    ISyncStateStoragePtr syncStateStorage,
    ISyncChunksProviderPtr syncChunksProvider,
    ISyncChunksStoragePtr syncChunksStorage,
    ILinkedNotebooksProcessorPtr linkedNotebooksProcessor,
    INotebooksProcessorPtr notebooksProcessor,
    IDurableNotesProcessorPtr notesProcessor,
    IDurableResourcesProcessorPtr resourcesProcessor,
    ISavedSearchesProcessorPtr savedSearchesProcessor,
    ITagsProcessorPtr tagsProcessor,
    IFullSyncStaleDataExpungerPtr fullSyncStaleDataExpunger,
    qevercloud::IRequestContextPtr ctx,
    qevercloud::INoteStorePtr noteStore,
    local_storage::ILocalStoragePtr localStorage,
    utility::cancelers::ICancelerPtr canceler,
    const QDir & syncPersistentStorageDir) :
    m_account{std::move(account)},
    m_authenticationInfoProvider{std::move(authenticationInfoProvider)},
    m_protocolVersionChecker{std::move(protocolVersionChecker)},
    m_userInfoProvider{std::move(userInfoProvider)},
    m_accountLimitsProvider{std::move(accountLimitsProvider)},
    m_syncStateStorage{std::move(syncStateStorage)},
    m_syncChunksProvider{std::move(syncChunksProvider)},
    m_syncChunksStorage{std::move(syncChunksStorage)},
    m_linkedNotebooksProcessor{std::move(linkedNotebooksProcessor)},
    m_notebooksProcessor{std::move(notebooksProcessor)},
    // clang-format does some weird crap here, working around
    // clang-format off
    m_notesProcessor{std::move(notesProcessor)},
    m_resourcesProcessor{std::move(resourcesProcessor)},
    m_savedSearchesProcessor{std::move(savedSearchesProcessor)},
    m_tagsProcessor{std::move(tagsProcessor)},
    m_fullSyncStaleDataExpunger{std::move(fullSyncStaleDataExpunger)},
    m_ctx{std::move(ctx)}, m_noteStore{std::move(noteStore)},
    m_localStorage{std::move(localStorage)}, m_canceler{std::move(canceler)},
    m_syncPersistentStorageDir{syncPersistentStorageDir}
    // clang-format on
{
    if (Q_UNLIKELY(m_account.isEmpty())) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::Downloader",
            "Downloader ctor: account is empty")}};
    }

    if (Q_UNLIKELY(m_account.type() != Account::Type::Evernote)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::Downloader",
            "Downloader ctor: account is not of Evernote type")}};
    }

    if (Q_UNLIKELY(!m_authenticationInfoProvider)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::Downloader",
            "Downloader ctor: authentication info provider is null")}};
    }

    if (Q_UNLIKELY(!m_protocolVersionChecker)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::Downloader",
            "Downloader ctor: protocol version checker is null")}};
    }

    if (Q_UNLIKELY(!m_userInfoProvider)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::Downloader",
            "Downloader ctor: user info provider is null")}};
    }

    if (Q_UNLIKELY(!m_accountLimitsProvider)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::Downloader",
            "Downloader ctor: account limits provider is null")}};
    }

    if (Q_UNLIKELY(!m_syncStateStorage)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::Downloader",
            "Downloader ctor: sync state storage is null")}};
    }

    if (Q_UNLIKELY(!m_syncChunksProvider)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::Downloader",
            "Downloader ctor: sync chunks provider is null")}};
    }

    if (Q_UNLIKELY(!m_syncChunksStorage)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::Downloader",
            "Downloader ctor: sync chunks storage is null")}};
    }

    if (Q_UNLIKELY(!m_linkedNotebooksProcessor)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::Downloader",
            "Downloader ctor: linked notebooks processor is null")}};
    }

    if (Q_UNLIKELY(!m_notebooksProcessor)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::Downloader",
            "Downloader ctor: notebooks processor is null")}};
    }

    if (Q_UNLIKELY(!m_notesProcessor)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::Downloader",
            "Downloader ctor: notes processor is null")}};
    }

    if (Q_UNLIKELY(!m_resourcesProcessor)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::Downloader",
            "Downloader ctor: resources processor is null")}};
    }

    if (Q_UNLIKELY(!m_savedSearchesProcessor)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::Downloader",
            "Downloader ctor: saved searches processor is null")}};
    }

    if (Q_UNLIKELY(!m_tagsProcessor)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::Downloader",
            "Downloader ctor: tags processor is null")}};
    }

    if (Q_UNLIKELY(!m_fullSyncStaleDataExpunger)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::Downloader",
            "Downloader ctor: full sync stale data expunger is null")}};
    }

    if (Q_UNLIKELY(!m_ctx)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::Downloader",
            "Downloader ctor: request context is null")}};
    }

    if (Q_UNLIKELY(!m_noteStore)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::Downloader",
            "Downloader ctor: note store is null")}};
    }

    if (Q_UNLIKELY(!m_localStorage)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::Downloader",
            "Downloader ctor: local storage is null")}};
    }

    if (Q_UNLIKELY(!m_canceler)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::Downloader",
            "Downloader ctor: canceler is null")}};
    }
}

Downloader::~Downloader() = default;

QFuture<IDownloader::Result> Downloader::download(ICallbackWeakPtr callbackWeak)
{
    QNDEBUG("synchronization::Downloader", "Downloader::download");

    std::shared_ptr<QPromise<Result>> promise;
    {
        const QMutexLocker locker{&m_mutex};
        if (m_future) {
            QNDEBUG(
                "synchronization::Downloader",
                "Download is already in progress");
            return *m_future;
        }

        if (!m_lastSyncState) {
            readLastSyncState();
            Q_ASSERT(m_lastSyncState);
        }

        QNDEBUG(
            "synchronization::Downloader",
            "Last sync state: " << *m_lastSyncState);

        promise = std::make_shared<QPromise<Result>>();
        m_future = promise->future();
    }

    promise->start();

    if (m_canceler->isCanceled()) {
        cancel(*promise);
        // NOTE: m_future is already reset inside cancel()
        return promise->future();
    }

    auto authenticationInfoFuture =
        m_authenticationInfoProvider->authenticateAccount(
            m_account, IAuthenticationInfoProvider::Mode::Cache);

    threading::bindCancellation(*m_future, authenticationInfoFuture);

    const auto selfWeak = weak_from_this();

    threading::thenOrFailed(
        std::move(authenticationInfoFuture), promise,
        threading::TrackedTask{
            selfWeak,
            [this, selfWeak, callbackWeak = std::move(callbackWeak),
             promise](IAuthenticationInfoPtr authenticationInfo) mutable {
                Q_ASSERT(authenticationInfo);

                if (m_canceler->isCanceled()) {
                    cancel(*promise);
                    return;
                }

                auto protocolVersionFuture =
                    m_protocolVersionChecker->checkProtocolVersion(
                        *authenticationInfo);

                threading::thenOrFailed(
                    std::move(protocolVersionFuture), promise,
                    threading::TrackedTask{
                        selfWeak,
                        [this, selfWeak, promise,
                         callbackWeak = std::move(callbackWeak),
                         authenticationInfo =
                             std::move(authenticationInfo)]() mutable {
                            if (m_canceler->isCanceled()) {
                                cancel(*promise);
                                return;
                            }

                            auto downloadFuture = launchDownload(
                                *authenticationInfo, std::move(callbackWeak));

                            threading::bindCancellation(
                                promise->future(), downloadFuture);

                            threading::mapFutureProgress(
                                downloadFuture, promise);

                            threading::thenOrFailed(
                                std::move(downloadFuture), promise,
                                [promise](Result result) {
                                    promise->addResult(std::move(result));
                                    promise->finish();
                                });
                        }});
            }});

    return *m_future;
}

void Downloader::readLastSyncState()
{
    const auto syncState = m_syncStateStorage->getSyncState(m_account);
    m_lastSyncState = SyncState{
        syncState->userDataUpdateCount(), syncState->userDataLastSyncTime(),
        syncState->linkedNotebookUpdateCounts(),
        syncState->linkedNotebookLastSyncTimes()};
}

QFuture<IDownloader::Result> Downloader::launchDownload(
    const IAuthenticationInfo & authenticationInfo,
    ICallbackWeakPtr callbackWeak) // NOLINT
{
    auto promise = std::make_shared<QPromise<Result>>();
    auto future = promise->future();

    promise->start();

    const auto ctx = qevercloud::RequestContextBuilder{}
                         .setAuthenticationToken(authenticationInfo.authToken())
                         .setCookies(authenticationInfo.userStoreCookies())
                         .setRequestTimeout(m_ctx->requestTimeout())
                         .setIncreaseRequestTimeoutExponentially(
                             m_ctx->increaseRequestTimeoutExponentially())
                         .setMaxRequestTimeout(m_ctx->maxRequestTimeout())
                         .setMaxRetryCount(m_ctx->maxRequestRetryCount())
                         .build();

    auto userFuture = fetchUser(ctx);

    const auto selfWeak = weak_from_this();

    auto accountLimitsFuture = threading::then(
        std::move(userFuture),
        [selfWeak, ctx = ctx](qevercloud::User && user) mutable {
            if (const auto self = selfWeak.lock()) {
                if (self->m_canceler->isCanceled()) {
                    return threading::makeExceptionalFuture<
                        qevercloud::AccountLimits>(OperationCanceled{});
                }

                const qevercloud::ServiceLevel serviceLevel = [&] {
                    auto level = user.serviceLevel();
                    if (level) {
                        return *level;
                    }

                    QNWARNING(
                        "synchronization::Downloader",
                        "No service level set for user: " << user);
                    return qevercloud::ServiceLevel::BASIC;
                }();

                return self->fetchAccountLimits(serviceLevel, std::move(ctx));
            }

            return threading::makeExceptionalFuture<qevercloud::AccountLimits>(
                OperationCanceled{});
        });

    auto syncStateFuture = threading::then(
        std::move(accountLimitsFuture),
        [selfWeak,
         ctx = ctx](qevercloud::AccountLimits && accountLimits) mutable {
            Q_UNUSED(accountLimits)
            if (const auto self = selfWeak.lock()) {
                return self->m_noteStore->getSyncStateAsync(std::move(ctx));
            }

            return threading::makeExceptionalFuture<qevercloud::SyncState>(
                OperationCanceled{});
        });

    threading::thenOrFailed(
        std::move(syncStateFuture), promise,
        [promise = promise, selfWeak, ctx = ctx,
         callbackWeak = std::move(callbackWeak)](
            qevercloud::SyncState && syncState) mutable {
            if (const auto self = selfWeak.lock()) {
                QNDEBUG(
                    "synchronization::Downloader",
                    "Sync state from Evernote: " << syncState);

                Q_ASSERT(self->m_lastSyncState);

                if (syncState.fullSyncBefore() >
                    self->m_lastSyncState->m_userDataLastSyncTime) {
                    QNDEBUG(
                        "synchronization::Downloader",
                        "Performing full synchronization instead of "
                        "incremental one");

                    self->launchUserOwnDataDownload(
                        std::move(promise), std::move(ctx),
                        std::move(callbackWeak), SyncMode::Full);
                }
                else if (
                    syncState.updateCount() ==
                    self->m_lastSyncState->m_userDataUpdateCount)
                {
                    QNDEBUG(
                        "synchronization::Downloader",
                        "Evernote has no updates for user own data");

                    self->launchLinkedNotebooksDataDownload(
                        std::move(promise), std::move(ctx),
                        std::move(callbackWeak));
                }
                else {
                    QNDEBUG(
                        "synchronization::Downloader",
                        "Launching incremental sync of user own data");

                    self->launchUserOwnDataDownload(
                        std::move(promise), std::move(ctx),
                        std::move(callbackWeak), SyncMode::Incremental);
                }

                return;
            }

            promise->setException(OperationCanceled{});
            promise->finish();
        });

    return future;
}

void Downloader::launchUserOwnDataDownload(
    std::shared_ptr<QPromise<Result>> promise,
    qevercloud::IRequestContextPtr ctx, ICallbackWeakPtr callbackWeak,
    const SyncMode syncMode)
{
    Q_ASSERT(m_lastSyncState);

    const qint32 afterUsn =
        (syncMode == SyncMode::Full ? 0
                                    : m_lastSyncState->m_userDataUpdateCount);

    auto syncChunksFuture = m_syncChunksProvider->fetchSyncChunks(
        afterUsn, ctx, m_canceler,
        std::make_shared<SyncChunksProviderCallback>(callbackWeak));

    auto selfWeak = weak_from_this();

    threading::thenOrFailed(
        std::move(syncChunksFuture), promise,
        threading::TrackedTask{
            selfWeak,
            [promise, this, ctx = std::move(ctx), syncMode,
             callbackWeak = std::move(callbackWeak)](
                QList<qevercloud::SyncChunk> syncChunks) mutable { // NOLINT
                if (m_canceler->isCanceled()) {
                    cancel(*promise);
                    return;
                }

                if (const auto callback = callbackWeak.lock()) {
                    callback->onSyncChunksDownloaded();
                }

                processUserOwnSyncChunks(
                    std::move(syncChunks), std::move(promise), std::move(ctx),
                    std::move(callbackWeak), syncMode);
             }});
}

void Downloader::launchLinkedNotebooksDataDownload(
    std::shared_ptr<QPromise<Result>> promise, // NOLINT
    qevercloud::IRequestContextPtr ctx,        // NOLINT
    ICallbackWeakPtr callbackWeak)             // NOLINT
{
    // TODO: implement
    Q_UNUSED(promise)
    Q_UNUSED(ctx)
    Q_UNUSED(callbackWeak)
}

QFuture<qevercloud::User> Downloader::fetchUser(
    qevercloud::IRequestContextPtr ctx)
{
    std::shared_ptr<QPromise<qevercloud::User>> promise;
    {
        const QMutexLocker locker{&m_mutex};
        if (m_userFuture) {
            return *m_userFuture;
        }

        promise = std::make_shared<QPromise<qevercloud::User>>();
        m_userFuture = promise->future();
    }

    promise->start();

    auto userFuture = m_userInfoProvider->userInfo(std::move(ctx));
    threading::thenOrFailed(
        std::move(userFuture), promise, [promise](qevercloud::User user) {
            promise->addResult(std::move(user));
            promise->finish();
        });

    return promise->future();
}

QFuture<qevercloud::AccountLimits> Downloader::fetchAccountLimits(
    const qevercloud::ServiceLevel serviceLevel,
    qevercloud::IRequestContextPtr ctx)
{
    std::shared_ptr<QPromise<qevercloud::AccountLimits>> promise;
    {
        const QMutexLocker locker{&m_mutex};
        if (m_accountLimitsFuture) {
            return *m_accountLimitsFuture;
        }

        promise = std::make_shared<QPromise<qevercloud::AccountLimits>>();
        m_accountLimitsFuture = promise->future();
    }

    promise->start();

    auto accountLimitsFuture =
        m_accountLimitsProvider->accountLimits(serviceLevel, std::move(ctx));

    threading::thenOrFailed(
        std::move(accountLimitsFuture), promise,
        [promise](qevercloud::AccountLimits accountLimits) {
            promise->addResult(std::move(accountLimits));
            promise->finish();
        });

    return promise->future();
}

void Downloader::processUserOwnSyncChunks(
    QList<qevercloud::SyncChunk> syncChunks,
    std::shared_ptr<QPromise<Result>> promise,
    qevercloud::IRequestContextPtr ctx,
    ICallbackWeakPtr callbackWeak,
    SyncMode syncMode, CheckForFirstSync checkForFirstSync)
{
    Q_ASSERT(m_lastSyncState);

    if (syncChunks.isEmpty()) {
        QNINFO(
            "synchronization::Downloader",
            "No new data found in Evernote for user's own account");
        launchLinkedNotebooksDataDownload(
            std::move(promise), std::move(ctx),
            std::move(callbackWeak));
        return;
    }

    const auto selfWeak = weak_from_this();

    if (checkForFirstSync == CheckForFirstSync::Yes) {
        const bool isFirstSync = (m_lastSyncState->userDataUpdateCount() == 0);
        if (!isFirstSync && (syncMode == SyncMode::Full)) {
            auto preservedGuids = collectPreservedGuids(syncChunks);
            auto future = m_fullSyncStaleDataExpunger->expungeStaleData(
                std::move(preservedGuids));

            threading::thenOrFailed(
                std::move(future), promise,
                threading::TrackedTask{
                    selfWeak,
                    [this, syncChunks = std::move(syncChunks),
                     promise = std::move(promise), ctx = std::move(ctx),
                     callbackWeak = std::move(callbackWeak),
                     syncMode]() mutable {
                        processUserOwnSyncChunks(
                            std::move(syncChunks), std::move(promise),
                            std::move(ctx), std::move(callbackWeak), syncMode,
                            CheckForFirstSync::No);
                    }});
            return;
        }
    }

    auto notebooksFuture =
        m_notebooksProcessor->processNotebooks(syncChunks, {});

    auto tagsFuture = m_tagsProcessor->processTags(syncChunks);
    auto savedSearchesFuture = m_savedSearchesProcessor->processSavedSearches(
        syncChunks, {});

    auto allFirstStageFuture = threading::whenAll(
        QList<QFuture<void>>{}
            << notebooksFuture << tagsFuture << savedSearchesFuture);

    auto notesFuture = threading::then(
        std::move(allFirstStageFuture),
        threading::TrackedTask{
            selfWeak,
            [this, syncChunks = std::move(syncChunks),
             promise = std::move(promise), ctx = std::move(ctx),
             callbackWeak = std::move(callbackWeak), syncMode]() mutable {
                // TODO: download notes and find a way to propagate the result
            }});

    // TODO: implement
    Q_UNUSED(syncChunks)
    Q_UNUSED(promise)
    Q_UNUSED(ctx)
    Q_UNUSED(callbackWeak)
    Q_UNUSED(syncMode)
}

void Downloader::cancel(QPromise<IDownloader::Result> & promise)
{
    promise.setException(OperationCanceled{});
    promise.finish();

    const QMutexLocker locker{&m_mutex};
    m_future.reset();

    if (m_userFuture) {
        m_userFuture->cancel();
        m_userFuture.reset();
    }

    if (m_accountLimitsFuture) {
        m_accountLimitsFuture->cancel();
        m_accountLimitsFuture.reset();
    }
}

} // namespace quentier::synchronization
