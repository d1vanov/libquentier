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

#include "NoteStoreProvider.h"

#include "Utils.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/RuntimeError.h>
#include <quentier/local_storage/ILocalStorage.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/synchronization/INoteStoreFactory.h>
#include <quentier/synchronization/types/IAuthenticationInfo.h>
#include <quentier/threading/Future.h>
#include <quentier/threading/TrackedTask.h>

#include <synchronization/IAuthenticationInfoProvider.h>
#include <synchronization/ILinkedNotebookFinder.h>
#include <synchronization/INotebookFinder.h>

#include <qevercloud/RequestContextBuilder.h>
#include <qevercloud/types/Notebook.h>

#include <QMutexLocker>
#include <QThread>

namespace quentier::synchronization {

namespace {

[[nodiscard]] bool checkNoteStoreRequestContext(
    const qevercloud::IRequestContext & noteStoreCtx,
    const qevercloud::IRequestContext & ctx)
{
    return ctx.connectionTimeout() == noteStoreCtx.connectionTimeout() &&
        ctx.increaseConnectionTimeoutExponentially() ==
        noteStoreCtx.increaseConnectionTimeoutExponentially() &&
        ctx.maxConnectionTimeout() == noteStoreCtx.maxConnectionTimeout() &&
        ctx.maxRequestRetryCount() == noteStoreCtx.maxRequestRetryCount();
}

} // namespace

NoteStoreProvider::NoteStoreProvider(
    ILinkedNotebookFinderPtr linkedNotebookFinder,
    INotebookFinderPtr notebookFinder,
    IAuthenticationInfoProviderPtr authenticationInfoProvider,
    INoteStoreFactoryPtr noteStoreFactory, Account account) :
    m_linkedNotebookFinder{std::move(linkedNotebookFinder)},
    m_notebookFinder{std::move(notebookFinder)},
    m_authenticationInfoProvider{std::move(authenticationInfoProvider)},
    m_noteStoreFactory{std::move(noteStoreFactory)},
    // clang-format off
    m_account{std::move(account)}
// clang-format on
{
    if (Q_UNLIKELY(!m_linkedNotebookFinder)) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "NoteStoreProvider ctor: linked notebook finder is null")}};
    }

    if (Q_UNLIKELY(!m_notebookFinder)) {
        throw InvalidArgument{ErrorString{
            QStringLiteral("NoteStoreProvider ctor: notebook finder is null")}};
    }

    if (Q_UNLIKELY(!m_authenticationInfoProvider)) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "NoteStoreProvider ctor: authentication info provider is null")}};
    }

    if (Q_UNLIKELY(!m_noteStoreFactory)) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "NoteStoreProvider ctor: note store factory is null")}};
    }

    if (Q_UNLIKELY(m_account.isEmpty())) {
        throw InvalidArgument{ErrorString{
            QStringLiteral("NoteStoreProvider ctor: account is empty")}};
    }
}

QFuture<qevercloud::INoteStorePtr>
    NoteStoreProvider::noteStoreForNotebookLocalId(
        QString notebookLocalId, qevercloud::IRequestContextPtr ctx,
        qevercloud::IRetryPolicyPtr retryPolicy)
{
    QNDEBUG(
        "synchronization::NoteStoreProvider",
        "NoteStoreProvider::noteStoreForNotebookLocalId: notebook local id = "
            << notebookLocalId);

    auto promise = std::make_shared<QPromise<qevercloud::INoteStorePtr>>();
    auto future = promise->future();
    promise->start();

    const auto selfWeak = weak_from_this();
    auto * currentThread = QThread::currentThread();

    auto linkedNotebookFuture =
        m_linkedNotebookFinder->findLinkedNotebookByNotebookLocalId(
            notebookLocalId);

    threading::thenOrFailed(
        std::move(linkedNotebookFuture), currentThread, promise,
        threading::TrackedTask{
            selfWeak,
            [this, ctx = std::move(ctx), retryPolicy = std::move(retryPolicy),
             promise](const std::optional<qevercloud::LinkedNotebook> &
                          linkedNotebook) mutable {
                createNoteStore(
                    linkedNotebook, std::move(ctx), std::move(retryPolicy),
                    promise);
            }});

    return future;
}

QFuture<qevercloud::INoteStorePtr> NoteStoreProvider::noteStoreForNotebookGuid(
    qevercloud::Guid notebookGuid, qevercloud::IRequestContextPtr ctx,
    qevercloud::IRetryPolicyPtr retryPolicy)
{
    QNDEBUG(
        "synchronization::NoteStoreProvider",
        "NoteStoreProvider::noteStoreForNotebookGuid: notebook guid = "
            << notebookGuid);

    auto promise = std::make_shared<QPromise<qevercloud::INoteStorePtr>>();
    auto future = promise->future();
    promise->start();

    const auto selfWeak = weak_from_this();
    auto * currentThread = QThread::currentThread();

    auto linkedNotebookFuture =
        m_linkedNotebookFinder->findLinkedNotebookByNotebookGuid(notebookGuid);

    threading::thenOrFailed(
        std::move(linkedNotebookFuture), currentThread, promise,
        threading::TrackedTask{
            selfWeak,
            [this, ctx = std::move(ctx), retryPolicy = std::move(retryPolicy),
             promise](const std::optional<qevercloud::LinkedNotebook> &
                          linkedNotebook) mutable {
                createNoteStore(
                    linkedNotebook, std::move(ctx), std::move(retryPolicy),
                    promise);
            }});

    return future;
}

QFuture<qevercloud::INoteStorePtr> NoteStoreProvider::noteStoreForNoteLocalId(
    QString noteLocalId, qevercloud::IRequestContextPtr ctx,
    qevercloud::IRetryPolicyPtr retryPolicy)
{
    QNDEBUG(
        "synchronization::NoteStoreProvider",
        "NoteStoreProvider::noteStoreForNoteLocalId: note local id = "
            << noteLocalId);

    auto promise = std::make_shared<QPromise<qevercloud::INoteStorePtr>>();
    auto future = promise->future();
    promise->start();

    const auto selfWeak = weak_from_this();
    auto * currentThread = QThread::currentThread();

    auto notebookFuture =
        m_notebookFinder->findNotebookByNoteLocalId(noteLocalId);

    threading::thenOrFailed(
        std::move(notebookFuture), currentThread, promise,
        threading::TrackedTask{
            selfWeak,
            [this, ctx = std::move(ctx), retryPolicy = std::move(retryPolicy),
             promise](
                const std::optional<qevercloud::Notebook> & notebook) mutable {
                onFindNotebookResult(
                    notebook, std::move(ctx), std::move(retryPolicy), promise);
            }});

    return future;
}

QFuture<qevercloud::INoteStorePtr> NoteStoreProvider::noteStoreForNoteGuid(
    qevercloud::Guid noteGuid, qevercloud::IRequestContextPtr ctx,
    qevercloud::IRetryPolicyPtr retryPolicy)
{
    QNDEBUG(
        "synchronization::NoteStoreProvider",
        "NoteStoreProvider::noteStoreForNoteGuid: note guid = " << noteGuid);

    auto promise = std::make_shared<QPromise<qevercloud::INoteStorePtr>>();
    auto future = promise->future();
    promise->start();

    const auto selfWeak = weak_from_this();
    auto * currentThread = QThread::currentThread();

    auto notebookFuture = m_notebookFinder->findNotebookByNoteGuid(noteGuid);

    threading::thenOrFailed(
        std::move(notebookFuture), currentThread, promise,
        threading::TrackedTask{
            selfWeak,
            [this, ctx = std::move(ctx), retryPolicy = std::move(retryPolicy),
             promise](
                const std::optional<qevercloud::Notebook> & notebook) mutable {
                onFindNotebookResult(
                    notebook, std::move(ctx), std::move(retryPolicy), promise);
            }});

    return future;
}

QFuture<qevercloud::INoteStorePtr> NoteStoreProvider::userOwnNoteStore(
    qevercloud::IRequestContextPtr ctx, qevercloud::IRetryPolicyPtr retryPolicy)
{
    auto promise = std::make_shared<QPromise<qevercloud::INoteStorePtr>>();
    auto future = promise->future();
    promise->start();

    createNoteStore(
        std::nullopt, std::move(ctx), std::move(retryPolicy), promise);

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
        m_linkedNotebookFinder->findLinkedNotebookByGuid(linkedNotebookGuid);

    const auto selfWeak = weak_from_this();
    auto * currentThread = QThread::currentThread();

    threading::thenOrFailed(
        std::move(linkedNotebookFuture), currentThread, promise,
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
                        RuntimeError{ErrorString{QStringLiteral(
                            "Could not find linked notebook by guid in the "
                            "local storage")}});
                    promise->finish();
                    return;
                }

                createNoteStore(
                    linkedNotebook, std::move(ctx), std::move(retryPolicy),
                    promise);
            }});

    return future;
}

qevercloud::INoteStorePtr NoteStoreProvider::cachedUserOwnNoteStore(
    const qevercloud::IRequestContextPtr & ctx)
{
    const QMutexLocker locker{&m_userOwnNoteStoreDataMutex};
    if (!m_userOwnNoteStoreData.m_noteStore) {
        return nullptr;
    }

    if (isAuthenticationTokenAboutToExpire(
            m_userOwnNoteStoreData.m_authTokenExpirationTime))
    {
        return nullptr;
    }

    if (!ctx) {
        return m_userOwnNoteStoreData.m_noteStore;
    }

    const auto noteStoreCtx =
        m_userOwnNoteStoreData.m_noteStore->defaultRequestContext();

    Q_ASSERT(noteStoreCtx);

    if (checkNoteStoreRequestContext(*noteStoreCtx, *ctx)) {
        return m_userOwnNoteStoreData.m_noteStore;
    }

    return nullptr;
}

qevercloud::INoteStorePtr NoteStoreProvider::cachedLinkedNotebookNoteStore(
    const qevercloud::LinkedNotebook & linkedNotebook,
    const qevercloud::IRequestContextPtr & ctx)
{
    Q_ASSERT(linkedNotebook.guid());

    const QMutexLocker locker{&m_linkedNotebooksNoteStoreDataMutex};

    const auto it =
        m_linkedNotebooksNoteStoreData.constFind(*linkedNotebook.guid());
    if (it == m_linkedNotebooksNoteStoreData.constEnd()) {
        return nullptr;
    }

    if (isAuthenticationTokenAboutToExpire(it->m_authTokenExpirationTime)) {
        return nullptr;
    }

    if (!ctx) {
        return it->m_noteStore;
    }

    const auto noteStoreCtx = it->m_noteStore->defaultRequestContext();
    Q_ASSERT(noteStoreCtx);

    if (checkNoteStoreRequestContext(*noteStoreCtx, *ctx)) {
        return it->m_noteStore;
    }

    return nullptr;
}

void NoteStoreProvider::createNoteStore(
    const std::optional<qevercloud::LinkedNotebook> & linkedNotebook,
    qevercloud::IRequestContextPtr ctx, qevercloud::IRetryPolicyPtr retryPolicy,
    const std::shared_ptr<QPromise<qevercloud::INoteStorePtr>> & promise)
{
    if (!linkedNotebook) {
        if (auto userOwnNoteStore = cachedUserOwnNoteStore(ctx)) {
            promise->addResult(std::move(userOwnNoteStore));
            promise->finish();
            return;
        }
    }
    else if (
        auto linkedNotebookNoteStore =
            cachedLinkedNotebookNoteStore(*linkedNotebook, ctx))
    {
        promise->addResult(std::move(linkedNotebookNoteStore));
        promise->finish();
        return;
    }

    auto authInfoFuture =
        (linkedNotebook
             ? m_authenticationInfoProvider->authenticateToLinkedNotebook(
                   m_account, *linkedNotebook,
                   IAuthenticationInfoProvider::Mode::Cache)
             : m_authenticationInfoProvider->authenticateAccount(
                   m_account, IAuthenticationInfoProvider::Mode::Cache));

    const auto selfWeak = weak_from_this();
    auto * currentThread = QThread::currentThread();

    threading::thenOrFailed(
        std::move(authInfoFuture), currentThread, promise,
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

            qevercloud::RequestContextBuilder ctxBuilder;
            ctxBuilder.setAuthenticationToken(authInfo->authToken())
                .setCookies(authInfo->userStoreCookies());

            if (ctx) {
                ctxBuilder.setConnectionTimeout(ctx->connectionTimeout())
                    .setIncreaseConnectionTimeoutExponentially(
                        ctx->increaseConnectionTimeoutExponentially())
                    .setMaxConnectionTimeout(ctx->maxConnectionTimeout())
                    .setMaxRetryCount(ctx->maxRequestRetryCount());
            }

            ctx = ctxBuilder.build();

            auto noteStore = m_noteStoreFactory->createNoteStore(
                authInfo->noteStoreUrl(), linkedNotebookGuid, ctx, retryPolicy);

            Q_ASSERT(noteStore);

            if (!linkedNotebookGuid) {
                const QMutexLocker locker{&m_userOwnNoteStoreDataMutex};

                m_userOwnNoteStoreData.m_noteStore = noteStore;
                m_userOwnNoteStoreData.m_authTokenExpirationTime =
                    authInfo->authTokenExpirationTime();
            }
            else {
                const QMutexLocker locker{&m_linkedNotebooksNoteStoreDataMutex};

                m_linkedNotebooksNoteStoreData.insert(
                    *linkedNotebookGuid,
                    NoteStoreData{
                        noteStore, authInfo->authTokenExpirationTime()});
            }

            promise->addResult(std::move(noteStore));
            promise->finish();
        });
}

void NoteStoreProvider::onFindNotebookResult(
    const std::optional<qevercloud::Notebook> & notebook,
    qevercloud::IRequestContextPtr ctx, qevercloud::IRetryPolicyPtr retryPolicy,
    const std::shared_ptr<QPromise<qevercloud::INoteStorePtr>> & promise)
{
    if (Q_UNLIKELY(!notebook)) {
        promise->setException(RuntimeError{ErrorString{
            QStringLiteral("Could not find notebook corresponding to the "
                           "note")}});
        promise->finish();
        return;
    }

    auto f =
        (notebook->linkedNotebookGuid()
             ? linkedNotebookNoteStore(
                   *notebook->linkedNotebookGuid(), std::move(ctx),
                   std::move(retryPolicy))
             : userOwnNoteStore(std::move(ctx), std::move(retryPolicy)));
    threading::thenOrFailed(
        std::move(f), promise, [promise](qevercloud::INoteStorePtr noteStore) {
            Q_ASSERT(noteStore);
            promise->addResult(std::move(noteStore));
            promise->finish();
        });
}

void NoteStoreProvider::clearCaches()
{
    QNDEBUG(
        "synchronization::NoteStoreProvider", "NoteStoreProvider::clearCaches");

    {
        const QMutexLocker locker{&m_userOwnNoteStoreDataMutex};
        m_userOwnNoteStoreData = {};
    }

    {
        const QMutexLocker locker{&m_linkedNotebooksNoteStoreDataMutex};
        m_linkedNotebooksNoteStoreData.clear();
    }
}

} // namespace quentier::synchronization
