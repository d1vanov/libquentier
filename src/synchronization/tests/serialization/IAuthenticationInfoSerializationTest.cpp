#include <synchronization/types/AuthenticationInfo.h>
#include <synchronization/types/AuthenticationInfoBuilder.h>

#include <gtest/gtest.h>

#include <array>

// clazy:excludeall=non-pod-global-static
// clazy:excludeall=returning-void-expression

namespace quentier::synchronization::tests {

enum class WithNetworkCookies
{
    Yes,
    No
};

class IAuthenticationInfoSerializationTest :
    public testing::TestWithParam<WithNetworkCookies>
{};

constexpr std::array gWithNetworkCookiesParams{
    WithNetworkCookies::Yes,
    WithNetworkCookies::No,
};

INSTANTIATE_TEST_SUITE_P(
    IAuthenticationInfoSerializationTestInstance,
    IAuthenticationInfoSerializationTest,
    testing::ValuesIn(gWithNetworkCookiesParams));

TEST_P(
    IAuthenticationInfoSerializationTest,
    SerializeAndDeserializeWithoutNetworkCookies)
{
    const auto testData = GetParam();

    AuthenticationInfoBuilder builder;
    builder.setUserId(qevercloud::UserID{42})
        .setAuthToken(QStringLiteral("AuthToken"))
        .setAuthTokenExpirationTime(qevercloud::Timestamp{1718949494000})
        .setAuthenticationTime(qevercloud::Timestamp{1718949484000})
        .setShardId(QStringLiteral("ShardId"))
        .setNoteStoreUrl(QStringLiteral("NoteStoreUrl"))
        .setWebApiUrlPrefix(QStringLiteral("WebApiUrlPrefix"));

    if (testData == WithNetworkCookies::Yes) {
        builder.setUserStoreCookies(QList<QNetworkCookie>{
            QNetworkCookie{
                QStringLiteral("name1").toUtf8(),
                QStringLiteral("value1").toUtf8()},
            QNetworkCookie{
                QStringLiteral("name2").toUtf8(),
                QStringLiteral("value2").toUtf8()},
            QNetworkCookie{
                QStringLiteral("name3").toUtf8(),
                QStringLiteral("value3").toUtf8()},
        });
    }

    auto authenticationInfo = builder.build();
    ASSERT_TRUE(authenticationInfo);

    const auto concreteAuthenticationInfo =
        std::dynamic_pointer_cast<AuthenticationInfo>(authenticationInfo);
    ASSERT_TRUE(concreteAuthenticationInfo);

    const auto serialized = authenticationInfo->serializeToJson();

    const auto deserialized =
        IAuthenticationInfo::deserializeFromJson(serialized);
    ASSERT_TRUE(deserialized);

    const auto concreteDeserializedAuthenticationInfo =
        std::dynamic_pointer_cast<AuthenticationInfo>(deserialized);
    ASSERT_TRUE(concreteDeserializedAuthenticationInfo);

    EXPECT_EQ(
        *concreteDeserializedAuthenticationInfo, *concreteAuthenticationInfo);
}

} // namespace quentier::synchronization::tests
