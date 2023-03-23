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
#include <quentier/threading/Future.h>
#include <quentier/threading/TrackedTask.h>

#include <synchronization/IAuthenticationInfoProvider.h>
#include <synchronization/IDownloader.h>
#include <synchronization/ISender.h>
#include <synchronization/types/SyncResult.h>

#include <qevercloud/exceptions/EDAMSystemExceptionAuthExpired.h>
#include <qevercloud/exceptions/EDAMSystemExceptionRateLimitReached.h>

#include <exception>

namespace quentier::synchronization {

AccountSynchronizer::AccountSynchronizer(
    IDownloaderPtr downloader, ISenderPtr sender,
    IAuthenticationInfoProviderPtr authenticationInfoProvider,
    threading::QThreadPoolPtr threadPool) :
    m_downloader{std::move(downloader)},
    m_sender{std::move(sender)}, m_authenticationInfoProvider{std::move(
                                     authenticationInfoProvider)},
    m_threadPool{std::move(threadPool)}
{
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
            [this, selfWeak,
             context](const IDownloader::Result & downloadResult) {
                // TODO: implement
                Q_UNUSED(downloadResult)
            }});

    threading::onFailed(
        std::move(downloadThenFuture),
        [this, selfWeak, context = std::move(context)](const QException & e) {
            // This exception should only be possible to come from sync chunks
            // downloading as notes and resources downloading reports separate
            // errors per each note/resource and does so via the sync result,
            // not via the exception inside QFuture.
            // TODO: should think of re-arranging error reporting from sync
            // chunks downloading as well, otherwise right now it's not possible
            // to figure out what part of sync chunks was downloaded before the
            // error.
            const auto self = selfWeak.lock();
            if (!self) {
                return;
            }

            try {
                e.raise();
            }
            catch (const qevercloud::EDAMSystemExceptionAuthExpired & ea) {
                // TODO: re-authenticate and restart sync
            }
            catch (const qevercloud::EDAMSystemExceptionRateLimitReached & er) {
                // Rate limit reaching means it's pointless to try to continue
                // the sync right now
                auto syncResult = std::make_shared<SyncResult>();
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
                    "Caught unknown std::exception on download attempt: "
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

} // namespace quentier::synchronization
