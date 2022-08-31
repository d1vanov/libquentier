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
#include <quentier/logging/QuentierLogger.h>
#include <quentier/synchronization/ISyncStateStorage.h>
#include <quentier/threading/Future.h>
#include <quentier/threading/TrackedTask.h>
#include <quentier/utility/cancelers/ICanceler.h>

#include <synchronization/IAccountLimitsProvider.h>
#include <synchronization/IAuthenticationInfoProvider.h>
#include <synchronization/IProtocolVersionChecker.h>
#include <synchronization/IUserInfoProvider.h>

#include <qevercloud/RequestContextBuilder.h>

namespace quentier::synchronization {

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
    INotesProcessorPtr notesProcessor,
    IResourcesProcessorPtr resourcesProcessor,
    ISavedSearchesProcessorPtr savedSearchesProcessor,
    ITagsProcessorPtr tagsProcessor, qevercloud::IRequestContextPtr ctx,
    qevercloud::INoteStorePtr noteStore,
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
    m_tagsProcessor{std::move(tagsProcessor)}, m_ctx{std::move(ctx)},
    m_noteStore{std::move(noteStore)}, m_canceler{std::move(canceler)},
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

    if (Q_UNLIKELY(!m_canceler)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::Downloader",
            "Downloader ctor: canceler is null")}};
    }
}

QFuture<IDownloader::Result> Downloader::download()
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
            [this, selfWeak,
             promise](IAuthenticationInfoPtr authenticationInfo) {
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
                         authenticationInfo =
                             std::move(authenticationInfo)]() mutable {
                            if (m_canceler->isCanceled()) {
                                cancel(*promise);
                                return;
                            }

                            auto downloadFuture =
                                launchDownload(std::move(authenticationInfo));

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
    IAuthenticationInfoPtr authenticationInfo) // NOLINT
{
    Q_ASSERT(authenticationInfo);

    const auto ctx =
        qevercloud::RequestContextBuilder{}
            .setAuthenticationToken(authenticationInfo->authToken())
            .setCookies(authenticationInfo->userStoreCookies())
            .setRequestTimeout(m_ctx->requestTimeout())
            .setIncreaseRequestTimeoutExponentially(
                m_ctx->increaseRequestTimeoutExponentially())
            .setMaxRequestTimeout(m_ctx->maxRequestTimeout())
            .setMaxRetryCount(m_ctx->maxRequestRetryCount())
            .build();

    auto userFuture = syncUser(ctx);

    const auto selfWeak = weak_from_this();

    auto accountLimitsFuture = threading::then(
        std::move(userFuture), [selfWeak, ctx](qevercloud::User && user) {
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

                return self->syncAccountLimits(serviceLevel, ctx);
            }

            return threading::makeExceptionalFuture<qevercloud::AccountLimits>(
                OperationCanceled{});
        });

    // TODO: implement further
    Q_UNUSED(accountLimitsFuture)
    return threading::makeReadyFuture<IDownloader::Result>({});
}

QFuture<qevercloud::User> Downloader::syncUser(
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

QFuture<qevercloud::AccountLimits> Downloader::syncAccountLimits(
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
