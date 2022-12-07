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

#include "Utils.h"

#include <quentier/synchronization/ISyncStateStorage.h>
#include <quentier/types/Account.h>

#include <synchronization/types/SyncState.h>

namespace quentier::synchronization {

SyncStateConstPtr readLastSyncState(
    const ISyncStateStoragePtr & syncStateStorage, const Account & account)
{
    Q_ASSERT(syncStateStorage);

    const auto syncState = syncStateStorage->getSyncState(account);
    Q_ASSERT(syncState);

    return std::make_shared<SyncState>(
        syncState->userDataUpdateCount(), syncState->userDataLastSyncTime(),
        syncState->linkedNotebookUpdateCounts(),
        syncState->linkedNotebookLastSyncTimes());
}

[[nodiscard]] bool isAuthenticationTokenAboutToExpire(
    const qevercloud::Timestamp authenticationTokenExpirationTimestamp)
{
    const qevercloud::Timestamp currentTimestamp =
        QDateTime::currentMSecsSinceEpoch();

    constexpr qint64 halfAnHourMsec = 1800000;

    return (authenticationTokenExpirationTimestamp - currentTimestamp) <
        halfAnHourMsec;
}

} // namespace quentier::synchronization
