/*
 * Copyright 2020-2021 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_SYNCHRONIZATION_SYNC_STATE_STORAGE_H
#define LIB_QUENTIER_SYNCHRONIZATION_SYNC_STATE_STORAGE_H

#include <quentier/synchronization/ISyncStateStorage.h>

namespace quentier {

class Q_DECL_HIDDEN SyncStateStorage final : public ISyncStateStorage
{
    Q_OBJECT
public:
    explicit SyncStateStorage(QObject * parent);

    ~SyncStateStorage() override = default;

    [[nodiscard]] ISyncStatePtr getSyncState(const Account & account) override;

    void setSyncState(
        const Account & account, ISyncStatePtr syncState) override;
};

} // namespace quentier

#endif // LIB_QUENTIER_SYNCHRONIZATION_SYNC_STATE_STORAGE_H
