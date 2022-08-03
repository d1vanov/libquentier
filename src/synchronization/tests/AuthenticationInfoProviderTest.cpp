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
#include <synchronization/types/AuthenticationInfo.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/synchronization/tests/mocks/MockIAuthenticator.h>
#include <quentier/threading/Future.h>
#include <quentier/utility/ApplicationSettings.h>
#include <quentier/utility/tests/mocks/MockIKeychainService.h>

#include <qevercloud/DurableService.h>
#include <qevercloud/RequestContext.h>
#include <qevercloud/types/builders/UserBuilder.h>

#include <QCoreApplication>
#include <QDateTime>

#include <gtest/gtest.h>

// clazy:excludeall=non-pod-global-static
// clazy:excludeall=returning-void-expression

namespace quentier::synchronization::tests {

using testing::InSequence;
using testing::Return;
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

TEST_F(AuthenticationInfoProviderTest, AuthenticateNewAccount)
{
    const auto host = QStringLiteral("https://www.evernote.com");

    const auto authenticationInfoProvider =
        std::make_shared<AuthenticationInfoProvider>(
            m_mockAuthenticator, m_mockKeychainService,
            m_mockUserInfoProvider, m_mockNoteStoreFactory,
            qevercloud::newRequestContext(), qevercloud::nullRetryPolicy(),
            host);

    auto authenticationInfo = std::make_shared<AuthenticationInfo>();
    authenticationInfo->m_userId = qevercloud::UserID{42};
    authenticationInfo->m_authToken = QStringLiteral("token");

    authenticationInfo->m_authenticationTime =
        QDateTime::currentMSecsSinceEpoch();

    authenticationInfo->m_authTokenExpirationTime =
        authenticationInfo->m_authenticationTime + 10000;

    authenticationInfo->m_shardId = QStringLiteral("shard_id");
    authenticationInfo->m_noteStoreUrl = QStringLiteral("note_store_url");

    authenticationInfo->m_webApiUrlPrefix =
        QStringLiteral("web_api_url_prefix");

    authenticationInfo->m_userStoreCookies = QList<QNetworkCookie>{}
        << QNetworkCookie{
            QStringLiteral("webCookiePreUserGuid").toUtf8(),
            QStringLiteral("value").toUtf8()};

    InSequence s;

    EXPECT_CALL(*m_mockAuthenticator, authenticateNewAccount)
        .WillOnce(Return(threading::makeReadyFuture<IAuthenticationInfoPtr>(
            authenticationInfo)));

    const auto user = qevercloud::UserBuilder{}
                          .setId(authenticationInfo->userId())
                          .setUsername(QStringLiteral("username"))
                          .setName(QStringLiteral("Full Name"))
                          .setPrivilege(qevercloud::PrivilegeLevel::NORMAL)
                          .setServiceLevel(qevercloud::ServiceLevel::BASIC)
                          .setActive(true)
                          .setShardId(authenticationInfo->shardId())
                          .build();

    EXPECT_CALL(*m_mockUserInfoProvider, userInfo(authenticationInfo->userId()))
        .WillOnce(Return(threading::makeReadyFuture<qevercloud::User>(
            qevercloud::User{user})));

    static const QString appName = QCoreApplication::applicationName();

    EXPECT_CALL(*m_mockKeychainService, writePassword)
        .WillOnce([&](const QString & service, const QString & key,
                      const QString & password) {
            EXPECT_EQ(service, appName + QStringLiteral("_auth_token"));

            EXPECT_EQ(
                key,
                appName + QStringLiteral("_auth_token_") + host +
                    QStringLiteral("_") +
                    QString::number(authenticationInfo->userId()));

            EXPECT_EQ(password, authenticationInfo->authToken());

            return threading::makeReadyFuture();
        });

    EXPECT_CALL(*m_mockKeychainService, writePassword)
        .WillOnce([&](const QString & service, const QString & key,
                      const QString & password) {
            EXPECT_EQ(service, appName + QStringLiteral("_shard_id"));

            EXPECT_EQ(
                key,
                appName + QStringLiteral("_shard_id_") + host +
                    QStringLiteral("_") +
                    QString::number(authenticationInfo->userId()));

            EXPECT_EQ(password, authenticationInfo->shardId());

            return threading::makeReadyFuture();
        });

    auto future = authenticationInfoProvider->authenticateNewAccount();
    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    EXPECT_EQ(future.result().get(), authenticationInfo.get());

    const Account account{
        *user.username(),
        Account::Type::Evernote,
        authenticationInfo->userId(),
        Account::EvernoteAccountType::Free,
        host,
        authenticationInfo->shardId()};

    ApplicationSettings appSettings{
        account, QStringLiteral("SynchronizationPersistence")};

    appSettings.beginGroup(
        QStringLiteral("Authentication/") + host + QStringLiteral("/") +
        QString::number(authenticationInfo->userId()));

    ApplicationSettings::GroupCloser groupCloser{appSettings};

    EXPECT_EQ(
        appSettings.value(QStringLiteral("NoteStoreUrl")).toString(),
        authenticationInfo->noteStoreUrl());

    EXPECT_EQ(
        appSettings.value(QStringLiteral("ExpirationTimestamp")).toLongLong(),
        authenticationInfo->authTokenExpirationTime());

    EXPECT_EQ(
        appSettings.value(
            QStringLiteral("AuthenticationTimestamp")).toLongLong(),
        authenticationInfo->authenticationTime());

    EXPECT_EQ(
        appSettings.value(QStringLiteral("WebApiUrlPrefix")).toString(),
        authenticationInfo->webApiUrlPrefix());

    EXPECT_EQ(
        appSettings.value(QStringLiteral("UserStoreCookie")).toString(),
        authenticationInfo->userStoreCookies().at(0).toRawForm());
}

} // namespace quentier::synchronization::tests
