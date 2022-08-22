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

#include "AccountLimitsProvider.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/threading/Future.h>

#include <qevercloud/RequestContext.h>
#include <qevercloud/services/IUserStore.h>

#include <QReadLocker>
#include <QWriteLocker>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include <quentier/threading/Qt5Promise.h>
#endif

namespace quentier::synchronization {

AccountLimitsProvider::AccountLimitsProvider(
    qevercloud::IUserStorePtr userStore,
    qevercloud::IRequestContextPtr ctx) :
    m_userStore{std::move(userStore)},
    m_ctx{std::move(ctx)}
{
    if (Q_UNLIKELY(!m_userStore)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::AccountLimitsProvider",
            "AccountLimitsProvider ctor: user store is null")}};
    }

    if (Q_UNLIKELY(!m_ctx)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::AccountLimitsProvider",
            "AccountLimitsProvider ctor: request context is null")}};
    }
}

QFuture<qevercloud::AccountLimits> AccountLimitsProvider::accountLimits(
    const qevercloud::ServiceLevel serviceLevel)
{
    {
        const QReadLocker locker{&m_accountLimitsCacheReadWriteLock};
        const auto it = m_accountLimitsCache.constFind(serviceLevel);
        if (it != m_accountLimitsCache.constEnd()) {
            return threading::makeReadyFuture(it.value());
        }
    }
    
    auto ctx = qevercloud::IRequestContextPtr{m_ctx->clone()};

    auto promise = std::make_shared<QPromise<qevercloud::AccountLimits>>();
    auto future = promise->future();
    promise->start();

    auto selfWeak = weak_from_this();

    auto accountLimitsFuture = m_userStore->getAccountLimitsAsync(
        serviceLevel, std::move(ctx));

    threading::thenOrFailed(
        std::move(accountLimitsFuture), promise,
        [promise, selfWeak, serviceLevel, ctx = std::move(ctx)](
            qevercloud::AccountLimits accountLimits)
        {
            if (const auto self = selfWeak.lock()) {
                const QWriteLocker locker{
                    &self->m_accountLimitsCacheReadWriteLock};

                // First let's check whether we are too late and another call
                // managed to bring the account limits to the local cache faster
                const auto it =
                    self->m_accountLimitsCache.constFind(serviceLevel);
                if (it != self->m_accountLimitsCache.constEnd()) {
                    promise->addResult(it.value());
                    promise->finish();
                    return;
                }

                self->m_accountLimitsCache[serviceLevel] = accountLimits;
            }

            promise->addResult(std::move(accountLimits));
            promise->finish();
        });

    return future;
}

} // namespace quentier::synchronization
