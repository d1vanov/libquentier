/*
 * Copyright 2021-2023 Dmitry Ivanov
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

#include <quentier/local_storage/Fwd.h>
#include <quentier/synchronization/Fwd.h>
#include <quentier/synchronization/types/Fwd.h>
#include <quentier/utility/Linkage.h>
#include <quentier/utility/cancelers/Fwd.h>

#include <qevercloud/types/TypeAliases.h>

#include <QFuture>

#include <memory>
#include <utility>

namespace quentier {

class Account;

} // namespace quentier

namespace quentier::synchronization {

class QUENTIER_EXPORT ISynchronizer
{
public:
    virtual ~ISynchronizer() noexcept;

    [[nodiscard]] virtual QFuture<IAuthenticationInfoPtr>
        authenticateNewAccount() = 0;

    [[nodiscard]] virtual QFuture<IAuthenticationInfoPtr> authenticateAccount(
        Account account) = 0;

    using SyncResult =
        std::pair<QFuture<ISyncResultPtr>, ISyncEventsNotifier *>;

    [[nodiscard]] virtual SyncResult synchronizeAccount(
        Account account, ISyncConflictResolverPtr syncConflictResolver,
        local_storage::ILocalStoragePtr localStorage,
        ISyncOptionsPtr options, utility::cancelers::ICancelerPtr canceler) = 0;

    virtual void revokeAuthentication(qevercloud::UserID userId) = 0;
};

} // namespace quentier::synchronization
