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

#include <synchronization/IAccountLimitsProvider.h>

#include <qevercloud/Fwd.h>
#include <qevercloud/services/Fwd.h>

#include <QHash>
#include <QReadWriteLock>

#include <memory>

namespace quentier::synchronization {

class AccountLimitsProvider final :
    public IAccountLimitsProvider,
    public std::enable_shared_from_this<AccountLimitsProvider>
{
public:
    explicit AccountLimitsProvider(
        qevercloud::IUserStorePtr userStore,
        qevercloud::IRequestContextPtr ctx);

    [[nodiscard]] QFuture<qevercloud::AccountLimits> accountLimits(
        qevercloud::ServiceLevel serviceLevel) override;

private:
    const qevercloud::IUserStorePtr m_userStore;
    const qevercloud::IRequestContextPtr m_ctx;

    QReadWriteLock m_accountLimitsCacheReadWriteLock;
    QHash<qevercloud::ServiceLevel, qevercloud::AccountLimits>
        m_accountLimitsCache;
};

} // namespace quentier::synchronization
