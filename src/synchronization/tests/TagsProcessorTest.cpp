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

#include <synchronization/SyncChunksDataCounters.h>
#include <synchronization/processors/TagsProcessor.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/RuntimeError.h>
#include <quentier/local_storage/tests/mocks/MockILocalStorage.h>
#include <quentier/synchronization/tests/mocks/MockISyncConflictResolver.h>
#include <quentier/threading/Future.h>
#include <quentier/utility/UidGenerator.h>

#include <qevercloud/types/builders/SyncChunkBuilder.h>
#include <qevercloud/types/builders/TagBuilder.h>

#include <QSet>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>

// clazy:excludeall=non-pod-global-static
// clazy:excludeall=returning-void-expression

namespace quentier::synchronization::tests {

using testing::StrictMock;

class TagsProcessorTest : public testing::Test
{
protected:
    const std::shared_ptr<local_storage::tests::mocks::MockILocalStorage>
        m_mockLocalStorage = std::make_shared<
            StrictMock<local_storage::tests::mocks::MockILocalStorage>>();

    const std::shared_ptr<mocks::MockISyncConflictResolver>
        m_mockSyncConflictResolver =
            std::make_shared<StrictMock<mocks::MockISyncConflictResolver>>();

    const SyncChunksDataCountersPtr m_syncChunksDataCounters =
        std::make_shared<SyncChunksDataCounters>();
};

TEST_F(TagsProcessorTest, Ctor)
{
    EXPECT_NO_THROW(
        const auto tagsProcessor = std::make_shared<TagsProcessor>(
            m_mockLocalStorage, m_mockSyncConflictResolver,
            m_syncChunksDataCounters));
}

TEST_F(TagsProcessorTest, CtorNullLocalStorage)
{
    EXPECT_THROW(
        const auto tagsProcessor = std::make_shared<TagsProcessor>(
            nullptr, m_mockSyncConflictResolver, m_syncChunksDataCounters),
        InvalidArgument);
}

TEST_F(TagsProcessorTest, CtorNullSyncConflictResolver)
{
    EXPECT_THROW(
        const auto tagsProcessor = std::make_shared<TagsProcessor>(
            m_mockLocalStorage, nullptr, m_syncChunksDataCounters),
        InvalidArgument);
}

TEST_F(TagsProcessorTest, CtorNullSyncChunksDataCounters)
{
    EXPECT_THROW(
        const auto tagsProcessor = std::make_shared<TagsProcessor>(
            m_mockLocalStorage, m_mockSyncConflictResolver, nullptr),
        InvalidArgument);
}

} // namespace quentier::synchronization::tests
