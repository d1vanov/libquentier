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

#include <quentier/synchronization/types/serialization/json/DownloadResourcesStatus.h>

#include <synchronization/types/DownloadResourcesStatus.h>

#include <quentier/exception/RuntimeError.h>
#include <quentier/utility/UidGenerator.h>

#include <qevercloud/types/builders/ResourceBuilder.h>

#include <gtest/gtest.h>

#include <array>

// clazy:excludeall=non-pod-global-static
// clazy:excludeall=returning-void-expression

namespace quentier::synchronization::tests {

class DownloadResourcesStatusJsonSerializationTest :
    public testing::TestWithParam<StopSynchronizationError>
{};

constexpr std::array gStopSynchronizationErrors{
    StopSynchronizationError{std::monostate{}},
    StopSynchronizationError{RateLimitReachedError{}},
    StopSynchronizationError{RateLimitReachedError{42}},
    StopSynchronizationError{AuthenticationExpiredError{}},
};

INSTANTIATE_TEST_SUITE_P(
    DownloadResourcesStatusJsonSerializationTestInstance,
    DownloadResourcesStatusJsonSerializationTest,
    testing::ValuesIn(gStopSynchronizationErrors));

TEST_P(DownloadResourcesStatusJsonSerializationTest, SerializeAndDeserialize)
{
    qint32 updateSequenceNumber = 300;

    const auto generateResource = [&] {
        return qevercloud::ResourceBuilder{}
            .setLocalId(UidGenerator::Generate())
            .setGuid(UidGenerator::Generate())
            .setUpdateSequenceNum(updateSequenceNumber++)
            .build();
    };

    auto status = std::make_shared<DownloadResourcesStatus>();
    status->m_totalNewResources = 42;
    status->m_totalUpdatedResources = 43;

    constexpr int itemCount = 3;

    status->m_resourcesWhichFailedToDownload.reserve(itemCount);
    for (int i = 0; i < itemCount; ++i) {
        status->m_resourcesWhichFailedToDownload
            << IDownloadResourcesStatus::ResourceWithException{
                   generateResource(),
                   std::make_shared<RuntimeError>(ErrorString{
                       QStringLiteral("Failed to download resource")})};
    }

    status->m_resourcesWhichFailedToProcess.reserve(itemCount);
    for (int i = 0; i < itemCount; ++i) {
        status->m_resourcesWhichFailedToProcess
            << IDownloadResourcesStatus::ResourceWithException{
                   generateResource(),
                   std::make_shared<RuntimeError>(ErrorString{
                       QStringLiteral("Failed to process resource")})};
    }

    status->m_processedResourceGuidsAndUsns.reserve(itemCount);
    for (int i = 0; i < itemCount; ++i) {
        status->m_processedResourceGuidsAndUsns[UidGenerator::Generate()] =
            updateSequenceNumber++;
    }

    status->m_cancelledResourceGuidsAndUsns.reserve(itemCount);
    for (int i = 0; i < itemCount; ++i) {
        status->m_cancelledResourceGuidsAndUsns[UidGenerator::Generate()] =
            updateSequenceNumber++;
    }

    status->m_stopSynchronizationError = GetParam();

    const auto serialized = serializeDownloadResourcesStatusToJson(*status);

    const auto deserialized =
        deserializeDownloadResourcesStatusFromJson(serialized);
    ASSERT_TRUE(deserialized);

    const auto concreteDeserializedDownloadResourcesStatus =
#ifdef Q_OS_MAC
        // NOTE: on macOS dynamic_cast across the shared library's boundary
        // is problematic, see
        // https://www.qt.io/blog/quality-assurance/one-way-dynamic_cast-across-library-boundaries-can-fail-and-how-to-fix-it
        // Using reinterpret_cast instead.
        std::reinterpret_pointer_cast<DownloadResourcesStatus>(deserialized);
#else
        std::dynamic_pointer_cast<DownloadResourcesStatus>(deserialized);
#endif
    ASSERT_TRUE(concreteDeserializedDownloadResourcesStatus);

    EXPECT_EQ(*concreteDeserializedDownloadResourcesStatus, *status);
}

} // namespace quentier::synchronization::tests
