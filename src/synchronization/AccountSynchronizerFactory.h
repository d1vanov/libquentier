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

#include <quentier/threading/Fwd.h>

#include <synchronization/IAccountSynchronizerFactory.h>

#include <qevercloud/Fwd.h>

#include <QDir>

namespace quentier::synchronization {

class AccountSynchronizerFactory: public IAccountSynchronizerFactory
{
public:
    AccountSynchronizerFactory(
        ISyncStateStoragePtr syncStateStorage,
        IAuthenticationInfoProviderPtr authenticationInfoProvider,
        const QDir & synchronizationPersistenceDir);

public: // IAccountSynchronizerFactory
    [[nodiscard]] IAccountSynchronizerPtr createAccountSynchronizer(
        Account account,
        ISyncConflictResolverPtr syncConflictResolver,
        local_storage::ILocalStoragePtr localStorage,
        ISyncOptionsPtr options) override;

private:
    const ISyncStateStoragePtr m_syncStateStorage;
    const IAuthenticationInfoProviderPtr m_authenticationInfoProvider;
    const QDir m_synchronizationPersistenceDir;
};

} // namespace quentier::synchronization
