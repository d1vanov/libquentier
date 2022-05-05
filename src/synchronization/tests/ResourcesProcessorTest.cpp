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

#include <synchronization/processors/ResourcesProcessor.h>
#include <synchronization/tests/mocks/MockIResourceFullDataDownloader.h>

#include <quentier/exception/InvalidArgument.h>

#include <quentier/local_storage/tests/mocks/MockILocalStorage.h>

#include <quentier/threading/Future.h>

#include <qevercloud/exceptions/builders/EDAMSystemExceptionBuilder.h>
#include <qevercloud/types/builders/NoteBuilder.h>
#include <qevercloud/types/builders/ResourceBuilder.h>
#include <qevercloud/types/builders/SyncChunkBuilder.h>

#include <QCoreApplication>

#include <gtest/gtest.h>

// clazy:excludeall=non-pod-global-static
// clazy:excludeall=returning-void-expression

namespace quentier::synchronization::tests {

using testing::StrictMock;

class ResourcesProcessorTest : public testing::Test
{
protected:
    const std::shared_ptr<local_storage::tests::mocks::MockILocalStorage>
        m_mockLocalStorage = std::make_shared<
            StrictMock<local_storage::tests::mocks::MockILocalStorage>>();

    const std::shared_ptr<mocks::MockIResourceFullDataDownloader>
        m_mockResourceFullDataDownloader = std::make_shared<
            StrictMock<mocks::MockIResourceFullDataDownloader>>();
};

TEST_F(ResourcesProcessorTest, Ctor)
{
    EXPECT_NO_THROW(
        const auto resourcesProcessor = std::make_shared<ResourcesProcessor>(
            m_mockLocalStorage, m_mockResourceFullDataDownloader));
}

TEST_F(ResourcesProcessorTest, CtorNullLocalStorage)
{
    EXPECT_THROW(
        const auto resourcesProcessor = std::make_shared<ResourcesProcessor>(
            nullptr, m_mockResourceFullDataDownloader),
        InvalidArgument);
}

TEST_F(ResourcesProcessorTest, CtorNullResourceFullDataDownloader)
{
    EXPECT_THROW(
        const auto resourcesProcessor = std::make_shared<ResourcesProcessor>(
            m_mockLocalStorage, nullptr),
        InvalidArgument);
}

} // namespace quentier::synchronization::tests
