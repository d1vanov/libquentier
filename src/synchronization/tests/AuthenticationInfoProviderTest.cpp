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

#include <synchronization/AuthenticationInfoProvider.h>
#include <synchronization/tests/mocks/MockINoteStoreFactory.h>
#include <synchronization/tests/mocks/MockIUserInfoProvider.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/synchronization/tests/mocks/MockIAuthenticator.h>
#include <quentier/utility/tests/mocks/MockIKeychainService.h>

#include <qevercloud/DurableService.h>
#include <qevercloud/RequestContext.h>

#include <gtest/gtest.h>

// clazy:excludeall=non-pod-global-static
// clazy:excludeall=returning-void-expression

namespace quentier::synchronization::tests {

using testing::StrictMock;

class AuthenticationInfoProviderTest : public testing::Test
{
protected:
    const std::shared_ptr<mocks::MockIAuthenticator> m_mockAuthenticator =
        std::make_shared<StrictMock<mocks::MockIAuthenticator>>();

    const std::shared_ptr<utility::tests::mocks::MockIKeychainService>
        m_mockKeychainService = std::make_shared<
            StrictMock<utility::tests::mocks::MockIKeychainService>>();

    const std::shared_ptr<mocks::MockIUserInfoProvider> m_mockUserInfoProvider =
        std::make_shared<StrictMock<mocks::MockIUserInfoProvider>>();

    const std::shared_ptr<mocks::MockINoteStoreFactory> m_mockNoteStoreFactory =
        std::make_shared<StrictMock<mocks::MockINoteStoreFactory>>();
};

TEST_F(AuthenticationInfoProviderTest, Ctor)
{
    EXPECT_NO_THROW(
        const auto authenticationInfoProvider =
            std::make_shared<AuthenticationInfoProvider>(
                m_mockAuthenticator, m_mockKeychainService,
                m_mockUserInfoProvider, m_mockNoteStoreFactory,
                qevercloud::newRequestContext(), qevercloud::nullRetryPolicy(),
                QStringLiteral("https://www.evernote.com")));
}

TEST_F(AuthenticationInfoProviderTest, CtorNullAuthenticator)
{
    EXPECT_THROW(
        const auto authenticationInfoProvider =
            std::make_shared<AuthenticationInfoProvider>(
                nullptr, m_mockKeychainService, m_mockUserInfoProvider,
                m_mockNoteStoreFactory, qevercloud::newRequestContext(),
                qevercloud::nullRetryPolicy(),
                QStringLiteral("https://www.evernote.com")),
        InvalidArgument);
}

TEST_F(AuthenticationInfoProviderTest, CtorNullKeychainService)
{
    EXPECT_THROW(
        const auto authenticationInfoProvider =
            std::make_shared<AuthenticationInfoProvider>(
                m_mockAuthenticator, nullptr, m_mockUserInfoProvider,
                m_mockNoteStoreFactory, qevercloud::newRequestContext(),
                qevercloud::nullRetryPolicy(),
                QStringLiteral("https://www.evernote.com")),
        InvalidArgument);
}

TEST_F(AuthenticationInfoProviderTest, CtorNullUserInfoProvider)
{
    EXPECT_THROW(
        const auto authenticationInfoProvider =
            std::make_shared<AuthenticationInfoProvider>(
                m_mockAuthenticator, m_mockKeychainService, nullptr,
                m_mockNoteStoreFactory, qevercloud::newRequestContext(),
                qevercloud::nullRetryPolicy(),
                QStringLiteral("https://www.evernote.com")),
        InvalidArgument);
}

TEST_F(AuthenticationInfoProviderTest, CtorNullNoteStoreFactory)
{
    EXPECT_THROW(
        const auto authenticationInfoProvider =
            std::make_shared<AuthenticationInfoProvider>(
                m_mockAuthenticator, m_mockKeychainService,
                m_mockUserInfoProvider, nullptr,
                qevercloud::newRequestContext(), qevercloud::nullRetryPolicy(),
                QStringLiteral("https://www.evernote.com")),
        InvalidArgument);
}

TEST_F(AuthenticationInfoProviderTest, CtorNullRequestContext)
{
    EXPECT_THROW(
        const auto authenticationInfoProvider =
            std::make_shared<AuthenticationInfoProvider>(
                m_mockAuthenticator, m_mockKeychainService,
                m_mockUserInfoProvider, m_mockNoteStoreFactory,
                nullptr, qevercloud::nullRetryPolicy(),
                QStringLiteral("https://www.evernote.com")),
        InvalidArgument);
}

TEST_F(AuthenticationInfoProviderTest, CtorNullRetryPolicy)
{
    EXPECT_THROW(
        const auto authenticationInfoProvider =
            std::make_shared<AuthenticationInfoProvider>(
                m_mockAuthenticator, m_mockKeychainService,
                m_mockUserInfoProvider, m_mockNoteStoreFactory,
                qevercloud::newRequestContext(), nullptr,
                QStringLiteral("https://www.evernote.com")),
        InvalidArgument);
}

TEST_F(AuthenticationInfoProviderTest, CtorEmptyHost)
{
    EXPECT_THROW(
        const auto authenticationInfoProvider =
            std::make_shared<AuthenticationInfoProvider>(
                m_mockAuthenticator, m_mockKeychainService,
                m_mockUserInfoProvider, m_mockNoteStoreFactory,
                qevercloud::newRequestContext(), qevercloud::nullRetryPolicy(),
                QString{}),
        InvalidArgument);
}

} // namespace quentier::synchronization::tests
