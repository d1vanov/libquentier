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

#pragma once

#include <synchronization/IUserInfoProvider.h>

#include <qevercloud/Fwd.h>
#include <qevercloud/services/Fwd.h>

#include <QHash>
#include <QReadWriteLock>

#include <memory>

namespace quentier::synchronization {

class UserInfoProvider final :
    public IUserInfoProvider,
    public std::enable_shared_from_this<UserInfoProvider>
{
public:
    explicit UserInfoProvider(qevercloud::IUserStorePtr userStore);

    [[nodiscard]] QFuture<qevercloud::User> userInfo(
        qevercloud::IRequestContextPtr ctx) override;

private:
    const qevercloud::IUserStorePtr m_userStore;

    QReadWriteLock m_userInfoCacheReadWriteLock;
    QHash<QString, qevercloud::User> m_userInfoCache;
};

} // namespace quentier::synchronization