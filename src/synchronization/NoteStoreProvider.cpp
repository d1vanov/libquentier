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

#include "INoteStoreFactory.h"
#include "NoteStoreProvider.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/RuntimeError.h>
#include <quentier/local_storage/ILocalStorage.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/synchronization/types/IAuthenticationInfo.h>
#include <quentier/threading/Future.h>
#include <quentier/threading/TrackedTask.h>

#include <synchronization/IAuthenticationInfoProvider.h>

#include <qevercloud/RequestContextBuilder.h>
#include <qevercloud/types/Notebook.h>

#include <QMutexLocker>

namespace quentier::synchronization {

namespace {

[[nodiscard]] bool isLinkedNotebookFutureReady(
    const QFuture<std::optional<qevercloud::LinkedNotebook>> & future)
{
    if (!future.isFinished()) {
        return false;
    }

    if (future.resultCount() != 1) {
        return false;
    }

    try {
        auto linkedNotebook = future.result();
        if (!linkedNotebook) {
            return false;
        }
    }
    catch (...) {
        return false;
    }

    return true;
}

} // namespace

NoteStoreProvider::NoteStoreProvider(
    local_storage::ILocalStoragePtr localStorage,
    IAuthenticationInfoProviderPtr authenticationInfoProvider,
    INoteStoreFactoryPtr noteStoreFactory, Account account) :
    m_localStorage{std::move(localStorage)},
    m_authenticationInfoProvider{std::move(authenticationInfoProvider)},
    m_noteStoreFactory{std::move(noteStoreFactory)},
    // clang-format off
    m_account{std::move(account)}
// clang-format on
{
    if (Q_UNLIKELY(!m_localStorage)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::NoteStoreProvider",
            "NoteStoreProvider ctor: local storage is null")}};
    }

    if (Q_UNLIKELY(!m_authenticationInfoProvider)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::NoteStoreProvider",
            "NoteStoreProvider ctor: authentication info provider is null")}};
    }

    if (Q_UNLIKELY(!m_noteStoreFactory)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::NoteStoreProvider",
            "NoteStoreProvider ctor: note store factory is null")}};
    }

    if (Q_UNLIKELY(m_account.isEmpty())) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::NoteStoreProvider",
            "NoteStoreProvider ctor: account is empty")}};
    }
}

QFuture<qevercloud::INoteStorePtr> NoteStoreProvider::noteStore(
    QString notebookLocalId, qevercloud::IRequestContextPtr ctx,
    qevercloud::IRetryPolicyPtr retryPolicy)
{
    auto promise = std::make_shared<QPromise<qevercloud::INoteStorePtr>>();
    auto future = promise->future();
    promise->start();

    const auto selfWeak = weak_from_this();

    QFuture<std::optional<qevercloud::LinkedNotebook>> linkedNotebookFuture =
        [&] {
            const QMutexLocker locker{&m_linkedNotebooksByNotebookLocalIdMutex};
            auto it =
                m_linkedNotebooksByNotebookLocalId.constFind(notebookLocalId);

            if (it != m_linkedNotebooksByNotebookLocalId.constEnd() &&
                isLinkedNotebookFutureReady(it.value()))
            {
                return it.value();
            }

            it = m_linkedNotebooksByNotebookLocalId.insert(
                notebookLocalId,
                findLinkedNotebookByNotebookLocalId(notebookLocalId));
            return it.value();
        }();

    threading::thenOrFailed(
        std::move(linkedNotebookFuture), promise,
        threading::TrackedTask{
            selfWeak,
            [this, ctx = std::move(ctx), retryPolicy = std::move(retryPolicy),
             promise](const std::optional<qevercloud::LinkedNotebook> &
                          linkedNotebook) mutable {
                onFindLinkedNotebookResult(
                    linkedNotebook, std::move(ctx), std::move(retryPolicy),
                    promise);
            }});

    return future;
}

QFuture<qevercloud::INoteStorePtr> NoteStoreProvider::linkedNotebookNoteStore(
    qevercloud::Guid linkedNotebookGuid, qevercloud::IRequestContextPtr ctx,
    qevercloud::IRetryPolicyPtr retryPolicy)
{
    auto promise = std::make_shared<QPromise<qevercloud::INoteStorePtr>>();
    auto future = promise->future();
    promise->start();

    auto linkedNotebookFuture =
        m_localStorage->findLinkedNotebookByGuid(linkedNotebookGuid);

    const auto selfWeak = weak_from_this();

    threading::thenOrFailed(
        std::move(linkedNotebookFuture), promise,
        threading::TrackedTask{
            selfWeak,
            [this, promise, linkedNotebookGuid = std::move(linkedNotebookGuid),
             ctx = std::move(ctx), retryPolicy = std::move(retryPolicy)](
                const std::optional<qevercloud::LinkedNotebook> &
                    linkedNotebook) mutable {
                if (Q_UNLIKELY(!linkedNotebook)) {
                    QNWARNING(
                        "synchronization::NoteStoreProvider",
                        "Could not find linked notebook by guid in the "
                            << "local storage: linked notebook guid = "
                            << linkedNotebookGuid);
                    promise->setException(
                        RuntimeError{ErrorString{QT_TRANSLATE_NOOP(
                            "synchronization::NoteStoreProvider",
                            "Could not find linked notebook by guid in the "
                            "local storage")}});
                    promise->finish();
                    return;
                }

                onFindLinkedNotebookResult(
                    linkedNotebook, std::move(ctx), std::move(retryPolicy),
                    promise);
            }});

    return future;
}

QFuture<std::optional<qevercloud::LinkedNotebook>>
    NoteStoreProvider::findLinkedNotebookByNotebookLocalId(
        const QString & notebookLocalId)
{
    auto promise =
        std::make_shared<QPromise<std::optional<qevercloud::LinkedNotebook>>>();

    auto future = promise->future();
    promise->start();

    auto notebookFuture =
        m_localStorage->findNotebookByLocalId(notebookLocalId);

    const auto selfWeak = weak_from_this();

    threading::thenOrFailed(
        std::move(notebookFuture), promise,
        [selfWeak, this, promise, notebookLocalId](
            const std::optional<qevercloud::Notebook> & notebook) {
            const auto self = selfWeak.lock();
            if (!self) {
                return;
            }

            if (Q_UNLIKELY(!notebook)) {
                QNWARNING(
                    "synchronization::NoteStoreProvider",
                    "Could not find notebook by local id in the local storage: "
                        << notebookLocalId);
                promise->setException(
                    RuntimeError{ErrorString{QT_TRANSLATE_NOOP(
                        "synchronization::NoteStoreProvider",
                        "Could not find notebook by local id in the local "
                        "storage")}});
                promise->finish();
                return;
            }

            if (!notebook->linkedNotebookGuid()) {
                promise->addResult(std::nullopt);
                promise->finish();
                return;
            }

            auto linkedNotebookGuid = *notebook->linkedNotebookGuid();

            QFuture<std::optional<qevercloud::LinkedNotebook>>
                linkedNotebookFuture = [&] {
                    const QMutexLocker locker{&m_linkedNotebooksByGuidMutex};

                    auto it =
                        m_linkedNotebooksByGuid.constFind(linkedNotebookGuid);

                    if (it != m_linkedNotebooksByGuid.constEnd() &&
                        isLinkedNotebookFutureReady(it.value()))
                    {
                        return it.value();
                    }

                    it = m_linkedNotebooksByGuid.insert(
                        linkedNotebookGuid,
                        m_localStorage->findLinkedNotebookByGuid(
                            linkedNotebookGuid));
                    return it.value();
                }();

            const auto selfWeak = weak_from_this();

            threading::thenOrFailed(
                std::move(linkedNotebookFuture), promise,
                [selfWeak, this, promise,
                 linkedNotebookGuid = std::move(linkedNotebookGuid)](
                    std::optional<qevercloud::LinkedNotebook> linkedNotebook) {
                    const auto self = selfWeak.lock();
                    if (!self) {
                        return;
                    }

                    if (Q_UNLIKELY(!linkedNotebook)) {
                        QNWARNING(
                            "synchronization::NoteStoreProvider",
                            "Could not find linked notebook by guid in the "
                                << "local storage: linked notebook guid = "
                                << linkedNotebookGuid);
                        promise->setException(
                            RuntimeError{ErrorString{QT_TRANSLATE_NOOP(
                                "synchronization::NoteStoreProvider",
                                "Could not find linked notebook by guid in the "
                                "local storage")}});
                        promise->finish();
                        return;
                    }

                    {
                        const QMutexLocker locker{
                            &m_linkedNotebooksByGuidMutex};

                        m_linkedNotebooksByGuid[linkedNotebookGuid] =
                            threading::makeReadyFuture<
                                std::optional<qevercloud::LinkedNotebook>>(
                                linkedNotebook);
                    }

                    promise->addResult(std::move(linkedNotebook));
                    promise->finish();
                });
        });

    return future;
}

void NoteStoreProvider::onFindLinkedNotebookResult(
    const std::optional<qevercloud::LinkedNotebook> & linkedNotebook,
    qevercloud::IRequestContextPtr ctx, qevercloud::IRetryPolicyPtr retryPolicy,
    const std::shared_ptr<QPromise<qevercloud::INoteStorePtr>> & promise)
{
    auto authInfoFuture =
        (linkedNotebook
             ? m_authenticationInfoProvider->authenticateToLinkedNotebook(
                   m_account, *linkedNotebook,
                   IAuthenticationInfoProvider::Mode::Cache)
             : m_authenticationInfoProvider->authenticateAccount(
                   m_account, IAuthenticationInfoProvider::Mode::Cache));

    const auto selfWeak = weak_from_this();

    threading::thenOrFailed(
        std::move(authInfoFuture), promise,
        [selfWeak, this, ctx = std::move(ctx),
         retryPolicy = std::move(retryPolicy),
         linkedNotebookGuid =
             (linkedNotebook ? linkedNotebook->guid() : std::nullopt),
         promise](const IAuthenticationInfoPtr & authInfo) mutable {
            const auto self = selfWeak.lock();
            if (!self) {
                return;
            }

            Q_ASSERT(authInfo);
            ctx = qevercloud::RequestContextBuilder{}
                      .setAuthenticationToken(authInfo->authToken())
                      .setCookies(authInfo->userStoreCookies())
                      .setRequestTimeout(ctx->requestTimeout())
                      .setIncreaseRequestTimeoutExponentially(
                          ctx->increaseRequestTimeoutExponentially())
                      .setMaxRequestTimeout(ctx->maxRequestTimeout())
                      .setMaxRetryCount(ctx->maxRequestRetryCount())
                      .build();

            auto noteStore = m_noteStoreFactory->noteStore(
                authInfo->noteStoreUrl(), linkedNotebookGuid, ctx, retryPolicy);

            Q_ASSERT(noteStore);
            promise->addResult(std::move(noteStore));
            promise->finish();
        });
}

} // namespace quentier::synchronization
