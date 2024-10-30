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

#include <quentier/synchronization/types/serialization/json/SyncChunksDataCounters.h>

#include <synchronization/types/SyncChunksDataCounters.h>

#include <gtest/gtest.h>

// clazy:excludeall=non-pod-global-static
// clazy:excludeall=returning-void-expression

namespace quentier::synchronization::tests {

TEST(SyncChunksDataCountersTest, SerializeAndDeserializeSyncChunksDataCounters)
{
    quint64 counterValue = 42;

    auto syncChunksDataCounters = std::make_shared<SyncChunksDataCounters>();
    syncChunksDataCounters->m_totalSavedSearches = counterValue++;
    syncChunksDataCounters->m_totalExpungedSavedSearches = counterValue++;
    syncChunksDataCounters->m_addedSavedSearches = counterValue++;
    syncChunksDataCounters->m_updatedSavedSearches = counterValue++;
    syncChunksDataCounters->m_expungedSavedSearches = counterValue++;

    syncChunksDataCounters->m_totalTags = counterValue++;
    syncChunksDataCounters->m_totalExpungedTags = counterValue++;
    syncChunksDataCounters->m_addedTags = counterValue++;
    syncChunksDataCounters->m_updatedTags = counterValue++;
    syncChunksDataCounters->m_expungedTags = counterValue++;

    syncChunksDataCounters->m_totalLinkedNotebooks = counterValue++;
    syncChunksDataCounters->m_totalExpungedLinkedNotebooks = counterValue++;
    syncChunksDataCounters->m_addedLinkedNotebooks = counterValue++;
    syncChunksDataCounters->m_updatedLinkedNotebooks = counterValue++;
    syncChunksDataCounters->m_expungedLinkedNotebooks = counterValue++;

    syncChunksDataCounters->m_totalNotebooks = counterValue++;
    syncChunksDataCounters->m_totalExpungedNotebooks = counterValue++;
    syncChunksDataCounters->m_addedNotebooks = counterValue++;
    syncChunksDataCounters->m_updatedNotebooks = counterValue++;
    syncChunksDataCounters->m_expungedNotebooks = counterValue++;

    const auto serialized =
        serializeSyncChunksDataCountersToJson(*syncChunksDataCounters);

    const auto deserialized =
        deserializeSyncChunksDataCountersFromJson(serialized);
    ASSERT_TRUE(deserialized);

    const auto concreteDeserializedSyncChunksDataCounters =
        std::dynamic_pointer_cast<SyncChunksDataCounters>(deserialized);
    ASSERT_TRUE(concreteDeserializedSyncChunksDataCounters);

    EXPECT_EQ(
        *concreteDeserializedSyncChunksDataCounters, *syncChunksDataCounters);
}

} // namespace quentier::synchronization::tests
