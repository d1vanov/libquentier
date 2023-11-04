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

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include <quentier/threading/Qt5Promise.h>
#endif

#include <memory>

namespace quentier::synchronization {

class Synchronizer final :
    public ISynchronizer,
    public std::enable_shared_from_this<Synchronizer>
{
public:
    Synchronizer(
        IAccountSynchronizerFactoryPtr accountSynchronizerFactory,
        IAuthenticationInfoProviderPtr authenticationInfoProvider,
        IProtocolVersionCheckerPtr protocolVersionChecker);

public: // ISynchronizer
    [[nodiscard]] QFuture<IAuthenticationInfoPtr> authenticateNewAccount()
        override;

    [[nodiscard]] QFuture<IAuthenticationInfoPtr> authenticateAccount(
        Account account) override;

    [[nodiscard]] SyncResult synchronizeAccount(
        Account account, local_storage::ILocalStoragePtr localStorage,
        utility::cancelers::ICancelerPtr canceler,
        ISyncOptionsPtr options = nullptr,
        ISyncConflictResolverPtr syncConflictResolver = nullptr) override;

    void revokeAuthentication(qevercloud::UserID userId) override;

private:
    void doSynchronizeAccount(
        Account account, local_storage::ILocalStoragePtr localStorage,
        utility::cancelers::ICancelerPtr canceler, ISyncOptionsPtr options,
        ISyncConflictResolverPtr syncConflictResolver,
        SyncEventsNotifierPtr syncEventsNotifier,
        const std::shared_ptr<QPromise<ISyncResultPtr>> & promise);

private:
    const IAccountSynchronizerFactoryPtr m_accountSynchronizerFactory;
    const IAuthenticationInfoProviderPtr m_authenticationInfoProvider;
    const IProtocolVersionCheckerPtr m_protocolVersionChecker;
};

} // namespace quentier::synchronization
