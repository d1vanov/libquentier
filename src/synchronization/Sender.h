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

#include "ISender.h"

#include <quentier/local_storage/Fwd.h>
#include <quentier/synchronization/Fwd.h>
#include <quentier/types/Account.h>

#include <synchronization/types/Fwd.h>

namespace quentier::synchronization {

class Sender final : public ISender
{
public:
    Sender(
        Account account,
        ISyncStateStoragePtr syncStateStorage,
        local_storage::ILocalStoragePtr localStorage);

    [[nodiscard]] QFuture<Result> send(
        utility::cancelers::ICancelerPtr canceler,
        ICallbackWeakPtr callbackWeak) override;

private:
    const Account m_account;
    const ISyncStateStoragePtr m_syncStateStorage;
    const local_storage::ILocalStoragePtr m_localStorage;
};

} // namespace quentier::synchronization
