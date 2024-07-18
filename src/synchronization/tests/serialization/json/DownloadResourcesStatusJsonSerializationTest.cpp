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

    constexpr int itemCount = 1;

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
        std::dynamic_pointer_cast<DownloadResourcesStatus>(deserialized);
    ASSERT_TRUE(concreteDeserializedDownloadResourcesStatus);

    EXPECT_EQ(*concreteDeserializedDownloadResourcesStatus, *status);
}

} // namespace quentier::synchronization::tests
