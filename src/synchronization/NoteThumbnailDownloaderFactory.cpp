/*
 * Copyright 2023-2024 Dmitry Ivanov
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

#include "NoteThumbnailDownloaderFactory.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/synchronization/types/IAuthenticationInfo.h>
#include <quentier/threading/Future.h>
#include <quentier/threading/TrackedTask.h>

#include <synchronization/IAuthenticationInfoProvider.h>
#include <synchronization/ILinkedNotebookFinder.h>

#include <qevercloud/RequestContextBuilder.h>

#include <QThread>

namespace quentier::synchronization {

namespace {

[[nodiscard]] qevercloud::IRequestContextPtr createRequestContextWithAuthToken(
    const qevercloud::IRequestContextPtr & sourceCtx, QString authToken)
{
    qevercloud::RequestContextBuilder builder;
    builder.setAuthenticationToken(std::move(authToken));

    if (sourceCtx) {
        builder.setConnectionTimeout(sourceCtx->connectionTimeout())
            .setMaxConnectionTimeout(sourceCtx->maxConnectionTimeout())
            .setIncreaseConnectionTimeoutExponentially(
                sourceCtx->increaseConnectionTimeoutExponentially())
            .setMaxRetryCount(sourceCtx->maxRequestRetryCount())
            .setCookies(sourceCtx->cookies());
    }

    return builder.build();
}

} // namespace

NoteThumbnailDownloaderFactory::NoteThumbnailDownloaderFactory(
    Account account, IAuthenticationInfoProviderPtr authenticationInfoProvider,
    ILinkedNotebookFinderPtr linkedNotebookFinder) :
    m_account{std::move(account)},
    m_authenticationInfoProvider{std::move(authenticationInfoProvider)},
    m_linkedNotebookFinder{std::move(linkedNotebookFinder)}
{
    if (Q_UNLIKELY(m_account.isEmpty())) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "NoteThumbnailDownloaderFactory ctor: account is empty")}};
    }

    if (Q_UNLIKELY(!m_authenticationInfoProvider)) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "NoteThumbnailDownloaderFactory ctor: authentication info provider "
            "is null")}};
    }

    if (Q_UNLIKELY(!m_linkedNotebookFinder)) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "NoteThumbnailDownloaderFactory ctor: linked notebook finder is "
            "null")}};
    }
}

QFuture<qevercloud::INoteThumbnailDownloaderPtr>
    NoteThumbnailDownloaderFactory::createNoteThumbnailDownloader(
        qevercloud::Guid notebookGuid, qevercloud::IRequestContextPtr ctx)
{
    auto promise =
        std::make_shared<QPromise<qevercloud::INoteThumbnailDownloaderPtr>>();
    auto future = promise->future();
    promise->start();

    auto linkedNotebookFuture =
        m_linkedNotebookFinder->findLinkedNotebookByNotebookGuid(notebookGuid);

    const auto selfWeak = weak_from_this();
    auto * currentThread = QThread::currentThread();

    threading::thenOrFailed(
        std::move(linkedNotebookFuture), currentThread, promise,
        threading::TrackedTask{
            selfWeak,
            [this, promise,
             ctx = std::move(ctx)](std::optional<qevercloud::LinkedNotebook>
                                       linkedNotebook) mutable {
                if (!linkedNotebook) {
                    createUserOwnNoteThumbnailDownloader(
                        promise, std::move(ctx));
                    return;
                }

                createLinkedNotebookNoteThumbnailDownloader(
                    promise, std::move(*linkedNotebook), std::move(ctx));
            }});

    return future;
}

void NoteThumbnailDownloaderFactory::createUserOwnNoteThumbnailDownloader(
    const std::shared_ptr<QPromise<qevercloud::INoteThumbnailDownloaderPtr>> &
        promise,
    qevercloud::IRequestContextPtr ctx)
{
    auto authenticationInfoFuture =
        m_authenticationInfoProvider->authenticateAccount(
            m_account, IAuthenticationInfoProvider::Mode::Cache);

    const auto selfWeak = weak_from_this();
    auto * currentThread = QThread::currentThread();

    threading::thenOrFailed(
        std::move(authenticationInfoFuture), currentThread, promise,
        threading::TrackedTask{
            selfWeak,
            [promise, account = m_account, ctx = std::move(ctx)](
                const IAuthenticationInfoPtr & authenticationInfo) mutable {
                Q_ASSERT(authenticationInfo);

                ctx = createRequestContextWithAuthToken(
                    ctx, authenticationInfo->authToken());

                auto downloader = qevercloud::newNoteThumbnailDownloader(
                    account.evernoteHost(), authenticationInfo->shardId(),
                    std::move(ctx));

                promise->addResult(std::move(downloader));
                promise->finish();
            }});
}

void NoteThumbnailDownloaderFactory::
    createLinkedNotebookNoteThumbnailDownloader(
        const std::shared_ptr<
            QPromise<qevercloud::INoteThumbnailDownloaderPtr>> & promise,
        qevercloud::LinkedNotebook linkedNotebook,
        qevercloud::IRequestContextPtr ctx)
{
    auto authenticationInfoFuture =
        m_authenticationInfoProvider->authenticateToLinkedNotebook(
            m_account, std::move(linkedNotebook),
            IAuthenticationInfoProvider::Mode::Cache);

    const auto selfWeak = weak_from_this();
    auto * currentThread = QThread::currentThread();

    threading::thenOrFailed(
        std::move(authenticationInfoFuture), currentThread, promise,
        threading::TrackedTask{
            selfWeak,
            [promise, account = m_account, ctx = std::move(ctx)](
                const IAuthenticationInfoPtr & authenticationInfo) mutable {
                Q_ASSERT(authenticationInfo);

                ctx = createRequestContextWithAuthToken(
                    ctx, authenticationInfo->authToken());

                auto downloader = qevercloud::newNoteThumbnailDownloader(
                    account.evernoteHost(), authenticationInfo->shardId(),
                    std::move(ctx));

                promise->addResult(std::move(downloader));
                promise->finish();
            }});
}

} // namespace quentier::synchronization