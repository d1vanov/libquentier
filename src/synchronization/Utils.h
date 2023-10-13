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

#pragma once

#include <quentier/synchronization/Fwd.h>

#include <synchronization/types/Fwd.h>

#include <qevercloud/types/Fwd.h>
#include <qevercloud/types/TypeAliases.h>

namespace quentier {

class Account;

} // namespace quentier

namespace quentier::synchronization {

[[nodiscard]] SyncStatePtr readLastSyncState(
    const ISyncStateStoragePtr & syncStateStorage, const Account & account);

[[nodiscard]] bool isAuthenticationTokenAboutToExpire(
    qevercloud::Timestamp authenticationTokenExpirationTimestamp);

[[nodiscard]] QString linkedNotebookInfo(
    const qevercloud::LinkedNotebook & linkedNotebook);

} // namespace quentier::synchronization
