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

#include "TestData.h"

#include <QFlags>

#include <optional>

////////////////////////////////////////////////////////////////////////////////

namespace quentier {

class Account;
class ISyncStateStorage;

} // namespace quentier

namespace quentier::local_storage {

class ILocalStorage;

} // namespace quentier::local_storage

namespace quentier::synchronization::tests {

class FakeSyncStateStorage;
class NoteStoreServer;

} // namespace quentier::synchronization::tests

////////////////////////////////////////////////////////////////////////////////

namespace quentier::synchronization::tests {

enum class DataItemType
{
    SavedSearch = 1 << 0,
    Tag = 1 << 1,
    Notebook = 1 << 2,
    Note = 1 << 3,
    Resource = 1 << 4,
};

Q_DECLARE_FLAGS(DataItemTypes, DataItemType);
Q_DECLARE_OPERATORS_FOR_FLAGS(DataItemTypes);

enum class ItemGroup
{
    Base = 1 << 0,
    New = 1 << 1,
    Modified = 1 << 2,
};

Q_DECLARE_FLAGS(ItemGroups, ItemGroup);
Q_DECLARE_OPERATORS_FOR_FLAGS(ItemGroups);

enum class ItemSource
{
    UserOwnAccount = 1 << 0,
    LinkedNotebook = 1 << 1,
};

Q_DECLARE_FLAGS(ItemSources, ItemSource);
Q_DECLARE_OPERATORS_FOR_FLAGS(ItemSources);

void setupTestData(
    DataItemTypes dataItemTypes,  ItemGroups itemGroups, ItemSources itemSources,
    DataItemTypes expungedDataItemTypes, ItemSources expungedItemSources,
    TestData & testData);

void setupNoteStoreServer(
    TestData & testData, NoteStoreServer & noteStoreServer);

void setupLocalStorage(
    const TestData & testData, DataItemTypes dataItemTypes,
    ItemGroups itemGroups, ItemSources itemSources,
    local_storage::ILocalStorage & localStorage);

void setupSyncState(
    const TestData & testData, const Account & testAccount,
    DataItemTypes dataItemTypes, ItemGroups itemGroups, ItemSources itemSources,
    ISyncStateStorage & syncStateStorage,
    std::optional<qint32> lastUpdateTimestamp = std::nullopt);

} // namespace quentier::synchronization::tests
