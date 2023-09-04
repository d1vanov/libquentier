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

#include "FakeSyncStateStorage.h"

#include <quentier/logging/QuentierLogger.h>

#include <QMutexLocker>

#include <algorithm>

namespace quentier::synchronization::tests {

FakeSyncStateStorage::FakeSyncStateStorage(QObject * parent) :
    ISyncStateStorage(parent)
{}

ISyncStatePtr FakeSyncStateStorage::getSyncState(const Account & account)
{
    QNDEBUG(
        "synchronization::tests::FakeSyncStateStorage",
        "FakeSyncStateStorage::getSyncState: " << account.name() << " ("
                                               << account.id() << ")");

    const QMutexLocker lock{&m_mutex};

    const auto it = std::find_if(
        m_syncStateData.constBegin(), m_syncStateData.constEnd(),
        [&account](const SyncStateData & syncStateData) {
            return syncStateData.m_account == account;
        });
    if (it != m_syncStateData.constEnd()) {
        QNDEBUG(
            "synchronization::tests::FakeSyncStateStorage",
            "Found some sync state");
        return it->m_syncStateData;
    }

    QNDEBUG(
        "synchronization::tests::FakeSyncStateStorage", "Found no sync state");
    return nullptr;
}

void FakeSyncStateStorage::setSyncState(
    const Account & account, ISyncStatePtr syncState)
{
    QNDEBUG(
        "synchronization::tests::FakeSyncStateStorage",
        "FakeSyncStateStorage::setSyncState: "
            << account.name() << "(" << account.id()
            << "): " << (syncState ? "non-null" : "null") << " sync state");

    const QMutexLocker lock{&m_mutex};

    const auto it = std::find_if(
        m_syncStateData.begin(), m_syncStateData.end(),
        [&account](const SyncStateData & syncStateData) {
            return syncStateData.m_account == account;
        });
    if (it != m_syncStateData.end()) {
        QNDEBUG(
            "synchronization::tests::FakeSyncStateStorage",
            "Updated sync state for account");
        it->m_syncStateData = std::move(syncState);
        return;
    }

    QNDEBUG(
        "synchronization::tests::FakeSyncStateStorage",
        "Added new sync state for account");
    m_syncStateData << SyncStateData{account, std::move(syncState)};
}

} // namespace quentier::synchronization::tests
