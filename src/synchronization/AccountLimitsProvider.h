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

#include <quentier/types/Account.h>

#include <synchronization/Fwd.h>
#include <synchronization/IAccountLimitsProvider.h>

#include <qevercloud/Fwd.h>
#include <qevercloud/services/Fwd.h>

#include <QHash>
#include <QMutex>

#include <memory>

namespace quentier::synchronization {

class AccountLimitsProvider final :
    public IAccountLimitsProvider,
    public std::enable_shared_from_this<AccountLimitsProvider>
{
public:
    explicit AccountLimitsProvider(
        Account account, qevercloud::IUserStorePtr userStore);

    [[nodiscard]] QFuture<qevercloud::AccountLimits> accountLimits(
        qevercloud::ServiceLevel serviceLevel,
        qevercloud::IRequestContextPtr ctx) override;

private:
    [[nodiscard]] std::optional<qevercloud::AccountLimits>
        readPersistentAccountLimits(
            qevercloud::ServiceLevel serviceLevel) const;

    void writePersistentAccountLimits(
        qevercloud::ServiceLevel serviceLevel,
        const qevercloud::AccountLimits & accountLimits);

private:
    const Account m_account;
    const qevercloud::IUserStorePtr m_userStore;

    QMutex m_accountLimitsCacheMutex;
    QHash<qevercloud::ServiceLevel, qevercloud::AccountLimits>
        m_accountLimitsCache;
};

} // namespace quentier::synchronization