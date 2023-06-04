/*
 * Copyright 2022-2023 Dmitry Ivanov
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

#include "UserInfoProvider.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/threading/Future.h>

#include <qevercloud/IRequestContext.h>
#include <qevercloud/services/IUserStore.h>

#include <QReadLocker>
#include <QWriteLocker>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include <quentier/threading/Qt5Promise.h>
#endif

namespace quentier::synchronization {

UserInfoProvider::UserInfoProvider(qevercloud::IUserStorePtr userStore) :
    m_userStore{std::move(userStore)}
{
    if (Q_UNLIKELY(!m_userStore)) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "UserInfoProvider ctor: user store is null")}};
    }
}

QFuture<qevercloud::User> UserInfoProvider::userInfo(
    qevercloud::IRequestContextPtr ctx)
{
    if (Q_UNLIKELY(!ctx)) {
        return threading::makeExceptionalFuture<qevercloud::User>(
            InvalidArgument{ErrorString{QStringLiteral(
                "Request context is null")}});
    }

    QString authToken = ctx->authenticationToken();

    {
        const QReadLocker locker{&m_userInfoCacheReadWriteLock};
        const auto it = m_userInfoCache.constFind(authToken);
        if (it != m_userInfoCache.constEnd()) {
            return threading::makeReadyFuture(qevercloud::User{it.value()});
        }
    }

    auto promise = std::make_shared<QPromise<qevercloud::User>>();
    auto future = promise->future();
    promise->start();

    auto selfWeak = weak_from_this();

    auto userFuture = m_userStore->getUserAsync(std::move(ctx));
    threading::thenOrFailed(
        std::move(userFuture), promise,
        [promise, selfWeak, authToken = std::move(authToken)](
            qevercloud::User user)
        {
            if (const auto self = selfWeak.lock()) {
                const QWriteLocker locker{&self->m_userInfoCacheReadWriteLock};

                // First let's check whether we are too late and another call
                // managed to bring the user to the local cache faster
                const auto it = self->m_userInfoCache.find(authToken);
                if (it != self->m_userInfoCache.end()) {
                    promise->addResult(it.value());
                    promise->finish();
                    return;
                }

                self->m_userInfoCache[authToken] = user;
            }

            promise->addResult(std::move(user));
            promise->finish();
        });

    return future;
}

} // namespace quentier::synchronization
