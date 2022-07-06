/*
 * Copyright 2021-2022 Dmitry Ivanov
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
#include <quentier/synchronization/ISyncChunksDataCounters.h>
#include <quentier/synchronization/types/Fwd.h>
#include <quentier/synchronization/types/IAuthenticationInfo.h>
#include <quentier/synchronization/types/ISyncOptions.h>
#include <quentier/synchronization/types/ISyncResult.h>
#include <quentier/utility/Fwd.h>
#include <quentier/utility/Linkage.h>
#include <quentier/utility/Printable.h>

#include <qevercloud/types/Note.h>
#include <qevercloud/types/TypeAliases.h>

#include <QDir>
#include <QException>
#include <QFuture>
#include <QHash>
#include <QList>
#include <QNetworkCookie>

#include <memory>
#include <optional>

namespace quentier {

class Account;

} // namespace quentier

namespace quentier::synchronization {

class QUENTIER_EXPORT ISynchronizer
{
public:
    virtual ~ISynchronizer() noexcept;

    /**
     * @return true if synchronization is being performed at the moment,
     *         false otherwise
     */
    [[nodiscard]] virtual bool isSyncRunning() const = 0;

    /**
     * @return options passed to ISynchronizer on the last sync
     */
    [[nodiscard]] virtual ISyncOptionsPtr options() const = 0;

    [[nodiscard]] virtual QFuture<IAuthenticationInfoPtr>
        authenticateNewAccount() = 0;

    [[nodiscard]] virtual QFuture<IAuthenticationInfoPtr> authenticateAccount(
        Account account) = 0;

    [[nodiscard]] virtual QFuture<ISyncResultPtr> synchronizeAccount(
        Account account, ISyncConflictResolverPtr syncConflictResolver,
        local_storage::ILocalStoragePtr localStorage,
        ISyncOptionsPtr options) = 0;

    [[nodiscard]] virtual QFuture<void> revokeAuthentication(
        qevercloud::UserID userId) = 0;

    [[nodiscard]] virtual ISyncEventsNotifier * notifier() const = 0;
};

} // namespace quentier::synchronization
