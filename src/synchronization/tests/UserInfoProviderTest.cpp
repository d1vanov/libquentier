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

#include <qevercloud/RequestContext.h>

#include <gtest/gtest.h>

// clazy:excludeall=non-pod-global-static
// clazy:excludeall=returning-void-expression

namespace quentier::synchronization::tests {

using testing::StrictMock;

class UserInfoProviderTest : public testing::Test
{
protected:
    const std::shared_ptr<mocks::qevercloud::MockIUserStore> m_mockUserStore =
        std::make_shared<StrictMock<mocks::qevercloud::MockIUserStore>>();

    const qevercloud::IRequestContextPtr m_ctx =
        qevercloud::newRequestContext();
};

TEST_F(UserInfoProviderTest, Ctor)
{
    EXPECT_NO_THROW(
        const auto userInfoProvider = std::make_shared<UserInfoProvider>(
            m_mockUserStore, m_ctx));
}

TEST_F(UserInfoProviderTest, CtorNullUserStore)
{
    EXPECT_THROW(
        const auto userInfoProvider = std::make_shared<UserInfoProvider>(
            nullptr, m_ctx),
        InvalidArgument);
}

TEST_F(UserInfoProviderTest, CtorNullRequestContext)
{
    EXPECT_THROW(
        const auto userInfoProvider = std::make_shared<UserInfoProvider>(
            m_mockUserStore, nullptr),
        InvalidArgument);
}

} // namespace quentier::synchronization::tests
