/*
 * Copyright 2023 Dmitry Ivanov
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

#include "AccountSynchronizer.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/RuntimeError.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/synchronization/types/IDownloadNotesStatus.h>
#include <quentier/synchronization/types/IDownloadResourcesStatus.h>
#include <quentier/threading/Future.h>
#include <quentier/threading/TrackedTask.h>

#include <synchronization/IAuthenticationInfoProvider.h>
#include <synchronization/IDownloader.h>
#include <synchronization/ISender.h>
#include <synchronization/types/SyncResult.h>

#include <qevercloud/exceptions/EDAMSystemExceptionAuthExpired.h>
#include <qevercloud/exceptions/EDAMSystemExceptionRateLimitReached.h>
#include <qevercloud/utility/ToRange.h>

#include <exception>

namespace quentier::synchronization {

AccountSynchronizer::AccountSynchronizer(
    Account account, IDownloaderPtr downloader, ISenderPtr sender,
    IAuthenticationInfoProviderPtr authenticationInfoProvider,
    threading::QThreadPoolPtr threadPool) :
    m_account{std::move(account)},
    m_downloader{std::move(downloader)},
    // clang-format off
    m_sender{std::move(sender)},
    m_authenticationInfoProvider{std::move(authenticationInfoProvider)},
    // clang-format on
    m_threadPool{std::move(threadPool)}
{
    if (Q_UNLIKELY(m_account.isEmpty())) {
        throw InvalidArgument{ErrorString{
            QStringLiteral("AccountSynchronizer ctor: account is empty")}};
    }

    if (Q_UNLIKELY(m_downloader)) {
        throw InvalidArgument{ErrorString{
            QStringLiteral("AccountSynchronizer ctor: downloader is null")}};
    }

    if (Q_UNLIKELY(!m_sender)) {
        throw InvalidArgument{ErrorString{
            QStringLiteral("AccountSynchronizer ctor: sender is null")}};
    }

    if (Q_UNLIKELY(!m_authenticationInfoProvider)) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "AccountSynchronizer ctor: authentication info provider is null")}};
    }

    if (Q_UNLIKELY(!m_threadPool)) {
        throw InvalidArgument{ErrorString{
            QStringLiteral("AccountSynchronizer ctor: thread pool is null")}};
    }
}

QFuture<ISyncResultPtr> AccountSynchronizer::synchronize(
    ICallbackWeakPtr callbackWeak, utility::cancelers::ICancelerPtr canceler)
{
    Q_ASSERT(canceler);

    auto promise = std::make_shared<QPromise<ISyncResultPtr>>();
    auto future = promise->future();
    promise->start();

    auto context = std::make_shared<Context>();
    context->promise = std::move(promise);
    context->callbackWeak = std::move(callbackWeak);
    context->canceler = std::move(canceler);

    synchronizeImpl(std::move(context));
    return future;
}

void AccountSynchronizer::synchronizeImpl(ContextPtr context)
{
    Q_ASSERT(context);

    const auto selfWeak = weak_from_this();

    auto downloadFuture =
        m_downloader->download(context->canceler, context->callbackWeak);

    auto downloadThenFuture = threading::then(
        std::move(downloadFuture),
        threading::TrackedTask{
            selfWeak,
            [this, selfWeak, context = context](
                const IDownloader::Result & downloadResult) mutable {
                onDownloadFinished(std::move(context), downloadResult);
            }});

    threading::onFailed(
        std::move(downloadThenFuture),
        [this, selfWeak,
         context = std::move(context)](const QException & e) mutable {
            // This exception should only be possible to come from sync
            // chunks downloading as notes and resources downloading
            // reports separate errors per each note/resource and does
            // so via the sync result, not via the exception inside
            // QFuture.
            // TODO: should think of re-arranging error reporting from
            // sync chunks downloading as well, otherwise right now it's
            // not possible to figure out what part of sync chunks was
            // downloaded before the error.
            const auto self = selfWeak.lock();
            if (!self) {
                return;
            }

            try {
                e.raise();
            }
            catch (const qevercloud::EDAMSystemExceptionAuthExpired & ea) {
                QNINFO(
                    "synchronization::AccountSynchronizer",
                    "Detected authentication expiration during sync, "
                    "trying to "
                    "re-authenticate and restart sync");
                clearAuthenticationCachesAndRestartSync(std::move(context));
            }
            catch (const qevercloud::EDAMSystemExceptionRateLimitReached & er) {
                QNINFO(
                    "synchronization::AccountSynchronizer",
                    "Detected API rate limit exceeding, rate limit "
                    "duration = "
                        << (er.rateLimitDuration()
                                ? QString::number(*er.rateLimitDuration())
                                : QStringLiteral("<none>")));
                // Rate limit reaching means it's pointless to try to
                // continue the sync right now
                auto syncResult = context->previousSyncResult
                    ? context->previousSyncResult
                    : std::make_shared<SyncResult>();
                syncResult->m_stopSynchronizationError =
                    StopSynchronizationError{
                        RateLimitReachedError{er.rateLimitDuration()}};
                context->promise->addResult(std::move(syncResult));
                context->promise->finish();
            }
            catch (const QException & eq) {
                QNWARNING(
                    "synchronization::AccountSynchronizer",
                    "Caught exception on download attempt: " << e.what());
                context->promise->setException(e);
                context->promise->finish();
            }
            catch (const std::exception & es) {
                QNWARNING(
                    "synchronization::AccountSynchronizer",
                    "Caught unknown std::exception on download "
                    "attempt: "
                        << e.what());
                context->promise->setException(RuntimeError{
                    ErrorString{QString::fromStdString(e.what())}});
                context->promise->finish();
            }
            catch (...) {
                QNWARNING(
                    "synchronization::AccountSynchronizer",
                    "Caught unknown on download attempt");
                context->promise->setException(
                    RuntimeError{ErrorString{QStringLiteral("Unknown error")}});
                context->promise->finish();
            }
        });
}

void AccountSynchronizer::onDownloadFinished(
    ContextPtr context, const IDownloader::Result & downloadResult)
{
    if ((downloadResult.userOwnResult.downloadNotesStatus &&
         std::holds_alternative<AuthenticationExpiredError>(
             downloadResult.userOwnResult.downloadNotesStatus
                 ->stopSynchronizationError())) ||
        (downloadResult.userOwnResult.downloadResourcesStatus &&
         std::holds_alternative<AuthenticationExpiredError>(
             downloadResult.userOwnResult.downloadResourcesStatus
                 ->stopSynchronizationError())))
    {
        m_authenticationInfoProvider->clearCaches(
            IAuthenticationInfoProvider::ClearCacheOptions{
                IAuthenticationInfoProvider::ClearCacheOption::User{
                    m_account.id()}});

        appendToPreviousSyncResult(*context, downloadResult);
        synchronizeImpl(std::move(context));
        return;
    }

    for (const auto it:
         qevercloud::toRange(qAsConst(downloadResult.linkedNotebookResults)))
    {
        const auto & linkedNotebookGuid = it.key();
        const auto & result = it.value();

        if ((result.downloadNotesStatus &&
             std::holds_alternative<AuthenticationExpiredError>(
                 result.downloadNotesStatus->stopSynchronizationError())) ||
            (result.downloadResourcesStatus &&
             std::holds_alternative<AuthenticationExpiredError>(
                 result.downloadResourcesStatus->stopSynchronizationError())))
        {
            m_authenticationInfoProvider->clearCaches(
                IAuthenticationInfoProvider::ClearCacheOptions{
                    IAuthenticationInfoProvider::ClearCacheOption::
                        LinkedNotebook{linkedNotebookGuid}});

            appendToPreviousSyncResult(*context, downloadResult);
            synchronizeImpl(std::move(context));
            return;
        }
    }

    // TODO: continue from here
}

void AccountSynchronizer::appendToPreviousSyncResult(
    Context & context, const IDownloader::Result & downloadResult) const
{
    // TODO: implement
    Q_UNUSED(context)
    Q_UNUSED(downloadResult)
}

void AccountSynchronizer::clearAuthenticationCachesAndRestartSync(
    ContextPtr context)
{
    m_authenticationInfoProvider->clearCaches(
        IAuthenticationInfoProvider::ClearCacheOptions{
            IAuthenticationInfoProvider::ClearCacheOption::All{}});

    synchronizeImpl(std::move(context));
}

} // namespace quentier::synchronization
