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

#include <synchronization/UserInfoProvider.h>
#include <synchronization/tests/mocks/qevercloud/services/MockIUserStore.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/threading/Future.h>

#include <qevercloud/RequestContext.h>
#include <qevercloud/types/builders/UserBuilder.h>

#include <gtest/gtest.h>

// clazy:excludeall=non-pod-global-static
// clazy:excludeall=returning-void-expression

namespace quentier::synchronization::tests {

using testing::Return;
using testing::StrictMock;

class UserInfoProviderTest : public testing::Test
{
protected:
    const std::shared_ptr<mocks::qevercloud::MockIUserStore> m_mockUserStore =
        std::make_shared<StrictMock<mocks::qevercloud::MockIUserStore>>();

    const qevercloud::IRequestContextPtr m_ctx =
        qevercloud::newRequestContext(QStringLiteral("authToken"));
};

TEST_F(UserInfoProviderTest, Ctor)
{
    EXPECT_NO_THROW(
        const auto userInfoProvider = std::make_shared<UserInfoProvider>(
            m_mockUserStore));
}

TEST_F(UserInfoProviderTest, CtorNullUserStore)
{
    EXPECT_THROW(
        const auto userInfoProvider = std::make_shared<UserInfoProvider>(
            nullptr),
        InvalidArgument);
}

TEST_F(UserInfoProviderTest, GetUserNullRequestContext)
{
    const auto userInfoProvider = std::make_shared<UserInfoProvider>(
        m_mockUserStore);

    auto future = userInfoProvider->userInfo(nullptr);
    ASSERT_TRUE(future.isFinished());
    EXPECT_THROW(future.waitForFinished(), InvalidArgument);
}

TEST_F(UserInfoProviderTest, GetUser)
{
    const auto userInfoProvider = std::make_shared<UserInfoProvider>(
        m_mockUserStore);

    const auto user = qevercloud::UserBuilder{}
        .setId(qevercloud::UserID{42})
        .setUsername(QStringLiteral("username"))
        .setName(QStringLiteral("Full name"))
        .build();

    EXPECT_CALL(*m_mockUserStore, getUserAsync).WillOnce(
        [&](const qevercloud::IRequestContextPtr & ctx)
        {
            EXPECT_EQ(ctx.get(), m_ctx.get());
            return threading::makeReadyFuture(user);
        });

    auto future = userInfoProvider->userInfo(m_ctx);
    ASSERT_TRUE(future.isFinished());

    ASSERT_EQ(future.resultCount(), 1);
    auto result = future.result();
    EXPECT_EQ(result, user);

    // The second call should not trigger the call of IUserStore as the result
    // of the first call should be cached
    future = userInfoProvider->userInfo(m_ctx);
    ASSERT_TRUE(future.isFinished());

    ASSERT_EQ(future.resultCount(), 1);
    result = future.result();
    EXPECT_EQ(result, user);
}

} // namespace quentier::synchronization::tests
