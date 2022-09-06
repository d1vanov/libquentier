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

#include "IFullSyncStaleDataExpunger.h"
#include "Fwd.h"

#include <quentier/local_storage/Fwd.h>
#include <quentier/utility/cancelers/Fwd.h>

namespace quentier::synchronization {

class FullSyncStaleDataExpunger final : public IFullSyncStaleDataExpunger
{
public:
    explicit FullSyncStaleDataExpunger(
        local_storage::ILocalStoragePtr localStorage,
        utility::cancelers::ICancelerPtr canceler);

    [[nodiscard]] QFuture<void> expungeStaleData(
        PreservedGuids preservedGuids,
        qevercloud::Guid linkedNotebookGuid = {}) override;

private:
    const local_storage::ILocalStoragePtr m_localStorage;
    const utility::cancelers::ICancelerPtr m_canceler;
};

} // namespace quentier::synchronization
