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

#include <synchronization/processors/NotebooksProcessor.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/local_storage/tests/mocks/MockILocalStorage.h>
#include <quentier/synchronization/tests/mocks/MockISyncConflictResolver.h>

#include <gtest/gtest.h>

namespace quentier::synchronization::tests {

using testing::StrictMock;

class NotebooksProcessorTest : public testing::Test
{
protected:
    std::shared_ptr<local_storage::tests::mocks::MockILocalStorage>
        m_mockLocalStorage = std::make_shared<
            StrictMock<local_storage::tests::mocks::MockILocalStorage>>();

    std::shared_ptr<mocks::MockISyncConflictResolver>
        m_mockSyncConflictResolver = std::make_shared<
            StrictMock<mocks::MockISyncConflictResolver>>();
};

TEST_F(NotebooksProcessorTest, Ctor)
{
    EXPECT_NO_THROW(
        const auto notebooksProcessor = std::make_shared<NotebooksProcessor>(
            m_mockLocalStorage, m_mockSyncConflictResolver));
}

TEST_F(NotebooksProcessorTest, CtorNullLocalStorage)
{
    EXPECT_THROW(
        const auto notebooksProcessor = std::make_shared<NotebooksProcessor>(
            nullptr, m_mockSyncConflictResolver),
        InvalidArgument);
}

TEST_F(NotebooksProcessorTest, CtorNullSyncConflictResolver)
{
    EXPECT_THROW(
        const auto notebooksProcessor = std::make_shared<NotebooksProcessor>(
            m_mockLocalStorage, nullptr),
        InvalidArgument);
}

} // namespace quentier::synchronization::tests
