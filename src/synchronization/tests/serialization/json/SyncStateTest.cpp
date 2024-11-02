/*
 * Copyright 2024 Dmitry Ivanov
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

#include <quentier/synchronization/types/serialization/json/SyncState.h>
#include <quentier/utility/UidGenerator.h>

#include <synchronization/types/SyncState.h>
#include <synchronization/types/SyncStateBuilder.h>

#include <gtest/gtest.h>

#include <array>

// clazy:excludeall=non-pod-global-static
// clazy:excludeall=returning-void-expression

namespace quentier::synchronization::tests {

class SyncStateJsonSerializationTest :
    public testing::TestWithParam<ISyncStatePtr>
{};

const std::array gSyncStates{
    SyncStateBuilder{}
        .setUserDataUpdateCount(42)
        .setUserDataLastSyncTime(qevercloud::Timestamp{1721405554000})
        .build(),
    SyncStateBuilder{}
        .setUserDataUpdateCount(43)
        .setUserDataLastSyncTime(qevercloud::Timestamp{1721405555000})
        .setLinkedNotebookUpdateCounts(QHash<qevercloud::Guid, qint32>{
            {UidGenerator::Generate(), 44},
            {UidGenerator::Generate(), 45},
            {UidGenerator::Generate(), 46},
        })
        .build(),
    SyncStateBuilder{}
        .setUserDataUpdateCount(43)
        .setUserDataLastSyncTime(qevercloud::Timestamp{1721405555000})
        .setLinkedNotebookUpdateCounts(QHash<qevercloud::Guid, qint32>{
            {UidGenerator::Generate(), 44},
            {UidGenerator::Generate(), 45},
            {UidGenerator::Generate(), 46},
        })
        .setLinkedNotebookLastSyncTimes(
            QHash<qevercloud::Guid, qevercloud::Timestamp>{
                {UidGenerator::Generate(),
                 qevercloud::Timestamp{1721405556000}},
                {UidGenerator::Generate(),
                 qevercloud::Timestamp{1721405557000}},
                {UidGenerator::Generate(),
                 qevercloud::Timestamp{1721405558000}},
            })
        .build(),
};

INSTANTIATE_TEST_SUITE_P(
    SyncStateJsonSerializationTestInstance, SyncStateJsonSerializationTest,
    testing::ValuesIn(gSyncStates));

TEST_P(SyncStateJsonSerializationTest, SerializeAndDeserializeSyncState)
{
    const auto syncState = GetParam();
    ASSERT_TRUE(syncState);

    const auto serialized = serializeSyncStateToJson(*syncState);

    const auto deserialized = deserializeSyncStateFromJson(serialized);
    ASSERT_TRUE(deserialized);

    const auto concreteSyncState =
        std::dynamic_pointer_cast<SyncState>(syncState);
    ASSERT_TRUE(concreteSyncState);

    const auto concreteDeserializedSyncState =
#ifdef Q_OS_MAC
        // NOTE: on macOS dynamic_cast across the shared library's boundary
        // is problematic, see
        // https://www.qt.io/blog/quality-assurance/one-way-dynamic_cast-across-library-boundaries-can-fail-and-how-to-fix-it
        // Using reinterpret_cast instead.
        std::reinterpret_pointer_cast<SyncState>(deserialized);
#else
        std::dynamic_pointer_cast<SyncState>(deserialized);
#endif

    EXPECT_EQ(*concreteSyncState, *concreteDeserializedSyncState);
}

} // namespace quentier::synchronization::tests
