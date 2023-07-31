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

#include <quentier/synchronization/ISyncStateStorage.h>

#include <QList>
#include <QMutex>

namespace quentier::synchronization::tests {

class FakeSyncStateStorage : public ISyncStateStorage
{
    Q_OBJECT
public:
    explicit FakeSyncStateStorage(QObject * parent = nullptr);

public: // ISyncStateStorage
    [[nodiscard]] ISyncStatePtr getSyncState(const Account & account) override;

    void setSyncState(
        const Account & account, ISyncStatePtr syncState) override;

private:
    struct SyncStateData
    {
        Account m_account;
        ISyncStatePtr m_syncStateData;
    };

    QList<SyncStateData> m_syncStateData;
    QMutex m_mutex;
};

} // namespace quentier::synchronization::tests
