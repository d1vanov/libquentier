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

#include <quentier/synchronization/types/serialization/json/AuthenticationInfo.h>

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

class AuthenticationInfoJsonSerializationTest :
    public testing::TestWithParam<WithNetworkCookies>
{};

constexpr std::array gWithNetworkCookiesParams{
    WithNetworkCookies::Yes,
    WithNetworkCookies::No,
};

INSTANTIATE_TEST_SUITE_P(
    AuthenticationInfoSerializationJsonTestInstance,
    AuthenticationInfoJsonSerializationTest,
    testing::ValuesIn(gWithNetworkCookiesParams));

TEST_P(
    AuthenticationInfoJsonSerializationTest,
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

    const auto serialized =
        serializeAuthenticationInfoToJson(*authenticationInfo);

    const auto deserialized = deserializeAuthenticationInfoFromJson(serialized);
    ASSERT_TRUE(deserialized);

    const auto concreteDeserializedAuthenticationInfo =
        std::dynamic_pointer_cast<AuthenticationInfo>(deserialized);
    ASSERT_TRUE(concreteDeserializedAuthenticationInfo);

    EXPECT_EQ(
        *concreteDeserializedAuthenticationInfo, *concreteAuthenticationInfo);
}

} // namespace quentier::synchronization::tests
