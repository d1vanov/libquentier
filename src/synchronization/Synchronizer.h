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

#pragma once

#include <quentier/synchronization/ISynchronizer.h>

#include <synchronization/Fwd.h>

#include <atomic>

namespace quentier::synchronization {

class Synchronizer final : public ISynchronizer
{
public:
    Synchronizer(
        IAccountSynchronizerFactoryPtr accountSynchronizerFactory,
        IAuthenticationInfoProviderPtr authenticationInfoProvider);

public: // ISynchronizer
    [[nodiscard]] QFuture<IAuthenticationInfoPtr> authenticateNewAccount()
        override;

    [[nodiscard]] QFuture<IAuthenticationInfoPtr> authenticateAccount(
        Account account) override;

    [[nodiscard]] SyncResult synchronizeAccount(
        Account account, ISyncConflictResolverPtr syncConflictResolver,
        local_storage::ILocalStoragePtr localStorage, ISyncOptionsPtr options,
        utility::cancelers::ICancelerPtr canceler) override;

    void revokeAuthentication(qevercloud::UserID userId) override;

private:
    const IAccountSynchronizerFactoryPtr m_accountSynchronizerFactory;
    const IAuthenticationInfoProviderPtr m_authenticationInfoProvider;
};

} // namespace quentier::synchronization
