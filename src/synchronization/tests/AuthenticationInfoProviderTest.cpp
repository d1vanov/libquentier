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
#include <quentier/utility/UidGenerator.h>
#include <quentier/utility/tests/mocks/MockIKeychainService.h>

#include <qevercloud/DurableService.h>
#include <qevercloud/RequestContext.h>
#include <qevercloud/types/builders/LinkedNotebookBuilder.h>
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

[[nodiscard]] std::shared_ptr<AuthenticationInfo>
    createSampleAuthenticationInfo()
{
    auto authenticationInfo = std::make_shared<AuthenticationInfo>();
    authenticationInfo->m_userId = qevercloud::UserID{42};
    authenticationInfo->m_authToken = QStringLiteral("token");

    authenticationInfo->m_authenticationTime =
        QDateTime::currentMSecsSinceEpoch();

    authenticationInfo->m_authTokenExpirationTime =
        authenticationInfo->m_authenticationTime + 10000000;

    authenticationInfo->m_shardId = QStringLiteral("shard_id");
    authenticationInfo->m_noteStoreUrl = QStringLiteral("note_store_url");

    authenticationInfo->m_webApiUrlPrefix =
        QStringLiteral("web_api_url_prefix");

    authenticationInfo->m_userStoreCookies = QList<QNetworkCookie>{}
        << QNetworkCookie{
               QStringLiteral("webCookiePreUserGuid").toUtf8(),
               QStringLiteral("value").toUtf8()};

    return authenticationInfo;
}

void checkAuthenticationInfoPartPersistence(
    const IAuthenticationInfoPtr & authenticationInfo, const Account & account,
    const QString & host)
{
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
        appSettings.value(QStringLiteral("AuthenticationTimestamp"))
            .toLongLong(),
        authenticationInfo->authenticationTime());

    EXPECT_EQ(
        appSettings.value(QStringLiteral("WebApiUrlPrefix")).toString(),
        authenticationInfo->webApiUrlPrefix());

    if (!authenticationInfo->userStoreCookies().isEmpty()) {
        EXPECT_EQ(
            appSettings.value(QStringLiteral("UserStoreCookie")).toString(),
            authenticationInfo->userStoreCookies().at(0).toRawForm());
    }
}

void setupAuthenticationInfoPartPersistence(
    const IAuthenticationInfoPtr & authenticationInfo, const Account & account,
    const QString & host)
{
    ApplicationSettings appSettings{
        account, QStringLiteral("SynchronizationPersistence")};

    appSettings.beginGroup(
        QStringLiteral("Authentication/") + host + QStringLiteral("/") +
        QString::number(authenticationInfo->userId()));

    ApplicationSettings::GroupCloser groupCloser{appSettings};

    appSettings.setValue(
        QStringLiteral("NoteStoreUrl"), authenticationInfo->noteStoreUrl());

    appSettings.setValue(
        QStringLiteral("ExpirationTimestamp"),
        authenticationInfo->authTokenExpirationTime());

    appSettings.setValue(
        QStringLiteral("AuthenticationTimestamp"),
        authenticationInfo->authenticationTime());

    appSettings.setValue(
        QStringLiteral("WebApiUrlPrefix"),
        authenticationInfo->webApiUrlPrefix());

    if (!authenticationInfo->userStoreCookies().isEmpty()) {
        appSettings.setValue(
            QStringLiteral("UserStoreCookie"),
            authenticationInfo->userStoreCookies().at(0).toRawForm());
    }
}

class AuthenticationInfoProviderTest : public testing::Test
{
protected:
    void SetUp() override
    {
        clearPersistence();
    }

    void TearDown() override
    {
        clearPersistence();
    };

private:
    void clearPersistence()
    {
        const Account account{
            QStringLiteral("Full Name"),
            Account::Type::Evernote,
            m_authenticationInfo->userId(),
            Account::EvernoteAccountType::Free,
            m_host,
            m_authenticationInfo->shardId()};

        ApplicationSettings appSettings{
            account, QStringLiteral("SynchronizationPersistence")};

        appSettings.remove(QString::fromUtf8(""));
        appSettings.sync();
    }

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

    const QString m_host = QStringLiteral("https://www.evernote.com");

    const std::shared_ptr<AuthenticationInfo> m_authenticationInfo =
        createSampleAuthenticationInfo();
};

TEST_F(AuthenticationInfoProviderTest, Ctor)
{
    EXPECT_NO_THROW(
        const auto authenticationInfoProvider =
            std::make_shared<AuthenticationInfoProvider>(
                m_mockAuthenticator, m_mockKeychainService,
                m_mockUserInfoProvider, m_mockNoteStoreFactory,
                qevercloud::newRequestContext(), qevercloud::nullRetryPolicy(),
                m_host));
}

TEST_F(AuthenticationInfoProviderTest, CtorNullAuthenticator)
{
    EXPECT_THROW(
        const auto authenticationInfoProvider =
            std::make_shared<AuthenticationInfoProvider>(
                nullptr, m_mockKeychainService, m_mockUserInfoProvider,
                m_mockNoteStoreFactory, qevercloud::newRequestContext(),
                qevercloud::nullRetryPolicy(), m_host),
        InvalidArgument);
}

TEST_F(AuthenticationInfoProviderTest, CtorNullKeychainService)
{
    EXPECT_THROW(
        const auto authenticationInfoProvider =
            std::make_shared<AuthenticationInfoProvider>(
                m_mockAuthenticator, nullptr, m_mockUserInfoProvider,
                m_mockNoteStoreFactory, qevercloud::newRequestContext(),
                qevercloud::nullRetryPolicy(), m_host),
        InvalidArgument);
}

TEST_F(AuthenticationInfoProviderTest, CtorNullUserInfoProvider)
{
    EXPECT_THROW(
        const auto authenticationInfoProvider =
            std::make_shared<AuthenticationInfoProvider>(
                m_mockAuthenticator, m_mockKeychainService, nullptr,
                m_mockNoteStoreFactory, qevercloud::newRequestContext(),
                qevercloud::nullRetryPolicy(), m_host),
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
                m_host),
        InvalidArgument);
}

TEST_F(AuthenticationInfoProviderTest, CtorNullRequestContext)
{
    EXPECT_THROW(
        const auto authenticationInfoProvider =
            std::make_shared<AuthenticationInfoProvider>(
                m_mockAuthenticator, m_mockKeychainService,
                m_mockUserInfoProvider, m_mockNoteStoreFactory, nullptr,
                qevercloud::nullRetryPolicy(), m_host),
        InvalidArgument);
}

TEST_F(AuthenticationInfoProviderTest, CtorNullRetryPolicy)
{
    EXPECT_THROW(
        const auto authenticationInfoProvider =
            std::make_shared<AuthenticationInfoProvider>(
                m_mockAuthenticator, m_mockKeychainService,
                m_mockUserInfoProvider, m_mockNoteStoreFactory,
                qevercloud::newRequestContext(), nullptr, m_host),
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
    const auto authenticationInfoProvider =
        std::make_shared<AuthenticationInfoProvider>(
            m_mockAuthenticator, m_mockKeychainService, m_mockUserInfoProvider,
            m_mockNoteStoreFactory, qevercloud::newRequestContext(),
            qevercloud::nullRetryPolicy(), m_host);

    InSequence s;

    EXPECT_CALL(*m_mockAuthenticator, authenticateNewAccount)
        .WillOnce(Return(threading::makeReadyFuture<IAuthenticationInfoPtr>(
            m_authenticationInfo)));

    const auto user = qevercloud::UserBuilder{}
                          .setId(m_authenticationInfo->userId())
                          .setUsername(QStringLiteral("username"))
                          .setName(QStringLiteral("Full Name"))
                          .setPrivilege(qevercloud::PrivilegeLevel::NORMAL)
                          .setServiceLevel(qevercloud::ServiceLevel::BASIC)
                          .setActive(true)
                          .setShardId(m_authenticationInfo->shardId())
                          .build();

    EXPECT_CALL(
        *m_mockUserInfoProvider, userInfo(m_authenticationInfo->userId()))
        .WillOnce(Return(threading::makeReadyFuture<qevercloud::User>(
            qevercloud::User{user})));

    static const QString appName = QCoreApplication::applicationName();

    EXPECT_CALL(*m_mockKeychainService, writePassword)
        .WillOnce([&](const QString & service, const QString & key,
                      const QString & password) {
            EXPECT_EQ(service, appName + QStringLiteral("_auth_token"));

            EXPECT_EQ(
                key,
                appName + QStringLiteral("_auth_token_") + m_host +
                    QStringLiteral("_") +
                    QString::number(m_authenticationInfo->userId()));

            EXPECT_EQ(password, m_authenticationInfo->authToken());

            return threading::makeReadyFuture();
        });

    EXPECT_CALL(*m_mockKeychainService, writePassword)
        .WillOnce([&](const QString & service, const QString & key,
                      const QString & password) {
            EXPECT_EQ(service, appName + QStringLiteral("_shard_id"));

            EXPECT_EQ(
                key,
                appName + QStringLiteral("_shard_id_") + m_host +
                    QStringLiteral("_") +
                    QString::number(m_authenticationInfo->userId()));

            EXPECT_EQ(password, m_authenticationInfo->shardId());

            return threading::makeReadyFuture();
        });

    auto future = authenticationInfoProvider->authenticateNewAccount();
    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    EXPECT_EQ(future.result().get(), m_authenticationInfo.get());

    const Account account{
        *user.name(),
        Account::Type::Evernote,
        m_authenticationInfo->userId(),
        Account::EvernoteAccountType::Free,
        m_host,
        m_authenticationInfo->shardId()};

    checkAuthenticationInfoPartPersistence(
        m_authenticationInfo, account, m_host);
}

TEST_F(
    AuthenticationInfoProviderTest, PropagateErrorWhenAuthenticatingNewAccount)
{
    const auto authenticationInfoProvider =
        std::make_shared<AuthenticationInfoProvider>(
            m_mockAuthenticator, m_mockKeychainService, m_mockUserInfoProvider,
            m_mockNoteStoreFactory, qevercloud::newRequestContext(),
            qevercloud::nullRetryPolicy(), m_host);

    const ErrorString exceptionMessage =
        ErrorString{QStringLiteral("some error")};

    EXPECT_CALL(*m_mockAuthenticator, authenticateNewAccount)
        .WillOnce(
            Return(threading::makeExceptionalFuture<IAuthenticationInfoPtr>(
                RuntimeError{exceptionMessage})));

    auto future = authenticationInfoProvider->authenticateNewAccount();
    ASSERT_TRUE(future.isFinished());

    bool caughtException = false;
    try {
        future.waitForFinished();
    }
    catch (const RuntimeError & e) {
        caughtException = true;
        EXPECT_EQ(e.errorMessage(), exceptionMessage);
    }

    EXPECT_TRUE(caughtException);
}

TEST_F(
    AuthenticationInfoProviderTest,
    TolerateErrorOfFindingUserInfoWhenAuthenticatingNewAccount)
{
    const auto authenticationInfoProvider =
        std::make_shared<AuthenticationInfoProvider>(
            m_mockAuthenticator, m_mockKeychainService, m_mockUserInfoProvider,
            m_mockNoteStoreFactory, qevercloud::newRequestContext(),
            qevercloud::nullRetryPolicy(), m_host);

    InSequence s;

    EXPECT_CALL(*m_mockAuthenticator, authenticateNewAccount)
        .WillOnce(Return(threading::makeReadyFuture<IAuthenticationInfoPtr>(
            m_authenticationInfo)));

    EXPECT_CALL(
        *m_mockUserInfoProvider, userInfo(m_authenticationInfo->userId()))
        .WillOnce(Return(threading::makeExceptionalFuture<qevercloud::User>(
            RuntimeError{ErrorString{QStringLiteral("some error")}})));

    auto future = authenticationInfoProvider->authenticateNewAccount();
    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    EXPECT_EQ(future.result().get(), m_authenticationInfo.get());
}

TEST_F(
    AuthenticationInfoProviderTest,
    TolerateErrorOfSavingAuthTokenToKeychainWhenAuthenticatingNewAccount)
{
    const auto authenticationInfoProvider =
        std::make_shared<AuthenticationInfoProvider>(
            m_mockAuthenticator, m_mockKeychainService, m_mockUserInfoProvider,
            m_mockNoteStoreFactory, qevercloud::newRequestContext(),
            qevercloud::nullRetryPolicy(), m_host);

    InSequence s;

    EXPECT_CALL(*m_mockAuthenticator, authenticateNewAccount)
        .WillOnce(Return(threading::makeReadyFuture<IAuthenticationInfoPtr>(
            m_authenticationInfo)));

    const auto user = qevercloud::UserBuilder{}
                          .setId(m_authenticationInfo->userId())
                          .setUsername(QStringLiteral("username"))
                          .setName(QStringLiteral("Full Name"))
                          .setPrivilege(qevercloud::PrivilegeLevel::NORMAL)
                          .setServiceLevel(qevercloud::ServiceLevel::BASIC)
                          .setActive(true)
                          .setShardId(m_authenticationInfo->shardId())
                          .build();

    EXPECT_CALL(
        *m_mockUserInfoProvider, userInfo(m_authenticationInfo->userId()))
        .WillOnce(Return(threading::makeReadyFuture<qevercloud::User>(
            qevercloud::User{user})));

    static const QString appName = QCoreApplication::applicationName();

    EXPECT_CALL(*m_mockKeychainService, writePassword)
        .WillOnce([&](const QString & service, const QString & key,
                      const QString & password) {
            EXPECT_EQ(service, appName + QStringLiteral("_auth_token"));

            EXPECT_EQ(
                key,
                appName + QStringLiteral("_auth_token_") + m_host +
                    QStringLiteral("_") +
                    QString::number(m_authenticationInfo->userId()));

            EXPECT_EQ(password, m_authenticationInfo->authToken());

            return threading::makeExceptionalFuture<void>(
                RuntimeError{ErrorString{QStringLiteral("some error")}});
        });

    EXPECT_CALL(*m_mockKeychainService, writePassword)
        .WillOnce([&](const QString & service, const QString & key,
                      const QString & password) {
            EXPECT_EQ(service, appName + QStringLiteral("_shard_id"));

            EXPECT_EQ(
                key,
                appName + QStringLiteral("_shard_id_") + m_host +
                    QStringLiteral("_") +
                    QString::number(m_authenticationInfo->userId()));

            EXPECT_EQ(password, m_authenticationInfo->shardId());

            return threading::makeReadyFuture();
        });

    auto future = authenticationInfoProvider->authenticateNewAccount();
    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    EXPECT_EQ(future.result().get(), m_authenticationInfo.get());

    const Account account{
        *user.name(),
        Account::Type::Evernote,
        m_authenticationInfo->userId(),
        Account::EvernoteAccountType::Free,
        m_host,
        m_authenticationInfo->shardId()};

    ApplicationSettings appSettings{
        account, QStringLiteral("SynchronizationPersistence")};

    appSettings.beginGroup(
        QStringLiteral("Authentication/") + m_host + QStringLiteral("/") +
        QString::number(m_authenticationInfo->userId()));

    ApplicationSettings::GroupCloser groupCloser{appSettings};

    EXPECT_TRUE(appSettings.allKeys().isEmpty());
}

TEST_F(
    AuthenticationInfoProviderTest,
    TolerateErrorOfSavingShardIdToKeychainWhenAuthenticatingNewAccount)
{
    const auto authenticationInfoProvider =
        std::make_shared<AuthenticationInfoProvider>(
            m_mockAuthenticator, m_mockKeychainService, m_mockUserInfoProvider,
            m_mockNoteStoreFactory, qevercloud::newRequestContext(),
            qevercloud::nullRetryPolicy(), m_host);

    InSequence s;

    EXPECT_CALL(*m_mockAuthenticator, authenticateNewAccount)
        .WillOnce(Return(threading::makeReadyFuture<IAuthenticationInfoPtr>(
            m_authenticationInfo)));

    const auto user = qevercloud::UserBuilder{}
                          .setId(m_authenticationInfo->userId())
                          .setUsername(QStringLiteral("username"))
                          .setName(QStringLiteral("Full Name"))
                          .setPrivilege(qevercloud::PrivilegeLevel::NORMAL)
                          .setServiceLevel(qevercloud::ServiceLevel::BASIC)
                          .setActive(true)
                          .setShardId(m_authenticationInfo->shardId())
                          .build();

    EXPECT_CALL(
        *m_mockUserInfoProvider, userInfo(m_authenticationInfo->userId()))
        .WillOnce(Return(threading::makeReadyFuture<qevercloud::User>(
            qevercloud::User{user})));

    static const QString appName = QCoreApplication::applicationName();

    EXPECT_CALL(*m_mockKeychainService, writePassword)
        .WillOnce([&](const QString & service, const QString & key,
                      const QString & password) {
            EXPECT_EQ(service, appName + QStringLiteral("_auth_token"));

            EXPECT_EQ(
                key,
                appName + QStringLiteral("_auth_token_") + m_host +
                    QStringLiteral("_") +
                    QString::number(m_authenticationInfo->userId()));

            EXPECT_EQ(password, m_authenticationInfo->authToken());

            return threading::makeReadyFuture();
        });

    EXPECT_CALL(*m_mockKeychainService, writePassword)
        .WillOnce([&](const QString & service, const QString & key,
                      const QString & password) {
            EXPECT_EQ(service, appName + QStringLiteral("_shard_id"));

            EXPECT_EQ(
                key,
                appName + QStringLiteral("_shard_id_") + m_host +
                    QStringLiteral("_") +
                    QString::number(m_authenticationInfo->userId()));

            EXPECT_EQ(password, m_authenticationInfo->shardId());

            return threading::makeExceptionalFuture<void>(
                RuntimeError{ErrorString{QStringLiteral("some error")}});
        });

    auto future = authenticationInfoProvider->authenticateNewAccount();
    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    EXPECT_EQ(future.result().get(), m_authenticationInfo.get());

    const Account account{
        *user.name(),
        Account::Type::Evernote,
        m_authenticationInfo->userId(),
        Account::EvernoteAccountType::Free,
        m_host,
        m_authenticationInfo->shardId()};

    ApplicationSettings appSettings{
        account, QStringLiteral("SynchronizationPersistence")};

    appSettings.beginGroup(
        QStringLiteral("Authentication/") + m_host + QStringLiteral("/") +
        QString::number(m_authenticationInfo->userId()));

    ApplicationSettings::GroupCloser groupCloser{appSettings};

    EXPECT_TRUE(appSettings.allKeys().isEmpty());
}

TEST_F(
    AuthenticationInfoProviderTest, AuthenticateAccountWithoutCacheExplicitly)
{
    const auto authenticationInfoProvider =
        std::make_shared<AuthenticationInfoProvider>(
            m_mockAuthenticator, m_mockKeychainService, m_mockUserInfoProvider,
            m_mockNoteStoreFactory, qevercloud::newRequestContext(),
            qevercloud::nullRetryPolicy(), m_host);

    const Account account{
        QStringLiteral("Full Name"),
        Account::Type::Evernote,
        m_authenticationInfo->userId(),
        Account::EvernoteAccountType::Free,
        m_host,
        m_authenticationInfo->shardId()};

    InSequence s;

    EXPECT_CALL(*m_mockAuthenticator, authenticateAccount(account))
        .WillOnce(Return(threading::makeReadyFuture<IAuthenticationInfoPtr>(
            m_authenticationInfo)));

    static const QString appName = QCoreApplication::applicationName();

    EXPECT_CALL(*m_mockKeychainService, writePassword)
        .WillOnce([&](const QString & service, const QString & key,
                      const QString & password) {
            EXPECT_EQ(service, appName + QStringLiteral("_auth_token"));

            EXPECT_EQ(
                key,
                appName + QStringLiteral("_auth_token_") + m_host +
                    QStringLiteral("_") +
                    QString::number(m_authenticationInfo->userId()));

            EXPECT_EQ(password, m_authenticationInfo->authToken());

            return threading::makeReadyFuture();
        });

    EXPECT_CALL(*m_mockKeychainService, writePassword)
        .WillOnce([&](const QString & service, const QString & key,
                      const QString & password) {
            EXPECT_EQ(service, appName + QStringLiteral("_shard_id"));

            EXPECT_EQ(
                key,
                appName + QStringLiteral("_shard_id_") + m_host +
                    QStringLiteral("_") +
                    QString::number(m_authenticationInfo->userId()));

            EXPECT_EQ(password, m_authenticationInfo->shardId());

            return threading::makeReadyFuture();
        });

    auto future = authenticationInfoProvider->authenticateAccount(
        account, AuthenticationInfoProvider::Mode::NoCache);

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    EXPECT_EQ(future.result().get(), m_authenticationInfo.get());

    checkAuthenticationInfoPartPersistence(
        m_authenticationInfo, account, m_host);
}

TEST_F(
    AuthenticationInfoProviderTest, AuthenticateAccountWithoutCacheImplicitly)
{
    const auto authenticationInfoProvider =
        std::make_shared<AuthenticationInfoProvider>(
            m_mockAuthenticator, m_mockKeychainService, m_mockUserInfoProvider,
            m_mockNoteStoreFactory, qevercloud::newRequestContext(),
            qevercloud::nullRetryPolicy(), m_host);

    const Account account{
        QStringLiteral("Full Name"),
        Account::Type::Evernote,
        m_authenticationInfo->userId(),
        Account::EvernoteAccountType::Free,
        m_host,
        m_authenticationInfo->shardId()};

    InSequence s;

    EXPECT_CALL(*m_mockAuthenticator, authenticateAccount(account))
        .WillOnce(Return(threading::makeReadyFuture<IAuthenticationInfoPtr>(
            m_authenticationInfo)));

    static const QString appName = QCoreApplication::applicationName();

    EXPECT_CALL(*m_mockKeychainService, writePassword)
        .WillOnce([&](const QString & service, const QString & key,
                      const QString & password) {
            EXPECT_EQ(service, appName + QStringLiteral("_auth_token"));

            EXPECT_EQ(
                key,
                appName + QStringLiteral("_auth_token_") + m_host +
                    QStringLiteral("_") +
                    QString::number(m_authenticationInfo->userId()));

            EXPECT_EQ(password, m_authenticationInfo->authToken());

            return threading::makeReadyFuture();
        });

    EXPECT_CALL(*m_mockKeychainService, writePassword)
        .WillOnce([&](const QString & service, const QString & key,
                      const QString & password) {
            EXPECT_EQ(service, appName + QStringLiteral("_shard_id"));

            EXPECT_EQ(
                key,
                appName + QStringLiteral("_shard_id_") + m_host +
                    QStringLiteral("_") +
                    QString::number(m_authenticationInfo->userId()));

            EXPECT_EQ(password, m_authenticationInfo->shardId());

            return threading::makeReadyFuture();
        });

    auto future = authenticationInfoProvider->authenticateAccount(
        account, AuthenticationInfoProvider::Mode::Cache);

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    EXPECT_EQ(future.result().get(), m_authenticationInfo.get());

    checkAuthenticationInfoPartPersistence(
        m_authenticationInfo, account, m_host);
}

TEST_F(AuthenticationInfoProviderTest, AuthenticateAccountWithCache)
{
    const auto authenticationInfoProvider =
        std::make_shared<AuthenticationInfoProvider>(
            m_mockAuthenticator, m_mockKeychainService, m_mockUserInfoProvider,
            m_mockNoteStoreFactory, qevercloud::newRequestContext(),
            qevercloud::nullRetryPolicy(), m_host);

    const Account account{
        QStringLiteral("Full Name"),
        Account::Type::Evernote,
        m_authenticationInfo->userId(),
        Account::EvernoteAccountType::Free,
        m_host,
        m_authenticationInfo->shardId()};

    setupAuthenticationInfoPartPersistence(
        m_authenticationInfo, account, m_host);

    InSequence s;

    static const QString appName = QCoreApplication::applicationName();

    EXPECT_CALL(*m_mockKeychainService, readPassword)
        .WillOnce([&](const QString & service, const QString & key) {
            EXPECT_EQ(service, appName + QStringLiteral("_auth_token"));

            EXPECT_EQ(
                key,
                appName + QStringLiteral("_auth_token_") + m_host +
                    QStringLiteral("_") +
                    QString::number(m_authenticationInfo->userId()));

            return threading::makeReadyFuture<QString>(
                m_authenticationInfo->authToken());
        });

    EXPECT_CALL(*m_mockKeychainService, readPassword)
        .WillOnce([&](const QString & service, const QString & key) {
            EXPECT_EQ(service, appName + QStringLiteral("_shard_id"));

            EXPECT_EQ(
                key,
                appName + QStringLiteral("_shard_id_") + m_host +
                    QStringLiteral("_") +
                    QString::number(m_authenticationInfo->userId()));

            return threading::makeReadyFuture<QString>(
                m_authenticationInfo->shardId());
        });

    auto future = authenticationInfoProvider->authenticateAccount(
        account, AuthenticationInfoProvider::Mode::Cache);

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    EXPECT_NE(future.result().get(), m_authenticationInfo.get());
    EXPECT_EQ(
        dynamic_cast<const AuthenticationInfo &>(*future.result()),
        *m_authenticationInfo);

    checkAuthenticationInfoPartPersistence(
        m_authenticationInfo, account, m_host);

    // The second attempt should not go to the keychain as the token and shard
    // id for this account would now be cached inside AuthenticationInfoProvider
    future = authenticationInfoProvider->authenticateAccount(
        account, AuthenticationInfoProvider::Mode::Cache);

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    EXPECT_NE(future.result().get(), m_authenticationInfo.get());
    EXPECT_EQ(
        dynamic_cast<const AuthenticationInfo &>(*future.result()),
        *m_authenticationInfo);

    checkAuthenticationInfoPartPersistence(
        m_authenticationInfo, account, m_host);
}

TEST_F(AuthenticationInfoProviderTest, RefuseToAuthenticateNonEvernoteAccount)
{
    const auto authenticationInfoProvider =
        std::make_shared<AuthenticationInfoProvider>(
            m_mockAuthenticator, m_mockKeychainService, m_mockUserInfoProvider,
            m_mockNoteStoreFactory, qevercloud::newRequestContext(),
            qevercloud::nullRetryPolicy(), m_host);

    const Account account{QStringLiteral("Full Name"), Account::Type::Local};

    auto future = authenticationInfoProvider->authenticateAccount(
        account, AuthenticationInfoProvider::Mode::Cache);
    EXPECT_TRUE(future.isFinished());
    EXPECT_THROW(future.waitForFinished(), InvalidArgument);

    future = authenticationInfoProvider->authenticateAccount(
        account, AuthenticationInfoProvider::Mode::NoCache);
    EXPECT_TRUE(future.isFinished());
    EXPECT_THROW(future.waitForFinished(), InvalidArgument);
}

TEST_F(
    AuthenticationInfoProviderTest,
    AuthenticateAccountWithCacheWhenCannotReadAuthTokenFromKeychain)
{
    const auto authenticationInfoProvider =
        std::make_shared<AuthenticationInfoProvider>(
            m_mockAuthenticator, m_mockKeychainService, m_mockUserInfoProvider,
            m_mockNoteStoreFactory, qevercloud::newRequestContext(),
            qevercloud::nullRetryPolicy(), m_host);

    const Account account{
        QStringLiteral("Full Name"),
        Account::Type::Evernote,
        m_authenticationInfo->userId(),
        Account::EvernoteAccountType::Free,
        m_host,
        m_authenticationInfo->shardId()};

    setupAuthenticationInfoPartPersistence(
        m_authenticationInfo, account, m_host);

    InSequence s;

    static const QString appName = QCoreApplication::applicationName();

    // First will try to read auth token and shard if from the keychain
    EXPECT_CALL(*m_mockKeychainService, readPassword)
        .WillOnce([&](const QString & service, const QString & key) {
            EXPECT_EQ(service, appName + QStringLiteral("_auth_token"));

            EXPECT_EQ(
                key,
                appName + QStringLiteral("_auth_token_") + m_host +
                    QStringLiteral("_") +
                    QString::number(m_authenticationInfo->userId()));

            return threading::makeExceptionalFuture<QString>(
                RuntimeError{ErrorString{QStringLiteral("some error")}});
        });

    EXPECT_CALL(*m_mockKeychainService, readPassword)
        .WillOnce([&](const QString & service, const QString & key) {
            EXPECT_EQ(service, appName + QStringLiteral("_shard_id"));

            EXPECT_EQ(
                key,
                appName + QStringLiteral("_shard_id_") + m_host +
                    QStringLiteral("_") +
                    QString::number(m_authenticationInfo->userId()));

            return threading::makeReadyFuture<QString>(
                m_authenticationInfo->shardId());
        });

    // Next will fallback to OAuth and then write the acquired authentication
    // info to the keychain
    EXPECT_CALL(*m_mockAuthenticator, authenticateAccount(account))
        .WillOnce(Return(threading::makeReadyFuture<IAuthenticationInfoPtr>(
            m_authenticationInfo)));

    EXPECT_CALL(*m_mockKeychainService, writePassword)
        .WillOnce([&](const QString & service, const QString & key,
                      const QString & password) {
            EXPECT_EQ(service, appName + QStringLiteral("_auth_token"));

            EXPECT_EQ(
                key,
                appName + QStringLiteral("_auth_token_") + m_host +
                    QStringLiteral("_") +
                    QString::number(m_authenticationInfo->userId()));

            EXPECT_EQ(password, m_authenticationInfo->authToken());

            return threading::makeReadyFuture();
        });

    EXPECT_CALL(*m_mockKeychainService, writePassword)
        .WillOnce([&](const QString & service, const QString & key,
                      const QString & password) {
            EXPECT_EQ(service, appName + QStringLiteral("_shard_id"));

            EXPECT_EQ(
                key,
                appName + QStringLiteral("_shard_id_") + m_host +
                    QStringLiteral("_") +
                    QString::number(m_authenticationInfo->userId()));

            EXPECT_EQ(password, m_authenticationInfo->shardId());

            return threading::makeReadyFuture();
        });

    auto future = authenticationInfoProvider->authenticateAccount(
        account, AuthenticationInfoProvider::Mode::NoCache);

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    EXPECT_EQ(future.result().get(), m_authenticationInfo.get());

    checkAuthenticationInfoPartPersistence(
        m_authenticationInfo, account, m_host);
}

TEST_F(
    AuthenticationInfoProviderTest,
    AuthenticateAccountWithCacheWhenCannotReadShardIdFromKeychain)
{
    const auto authenticationInfoProvider =
        std::make_shared<AuthenticationInfoProvider>(
            m_mockAuthenticator, m_mockKeychainService, m_mockUserInfoProvider,
            m_mockNoteStoreFactory, qevercloud::newRequestContext(),
            qevercloud::nullRetryPolicy(), m_host);

    const Account account{
        QStringLiteral("Full Name"),
        Account::Type::Evernote,
        m_authenticationInfo->userId(),
        Account::EvernoteAccountType::Free,
        m_host,
        m_authenticationInfo->shardId()};

    setupAuthenticationInfoPartPersistence(
        m_authenticationInfo, account, m_host);

    InSequence s;

    static const QString appName = QCoreApplication::applicationName();

    // First will try to read auth token and shard if from the keychain
    EXPECT_CALL(*m_mockKeychainService, readPassword)
        .WillOnce([&](const QString & service, const QString & key) {
            EXPECT_EQ(service, appName + QStringLiteral("_auth_token"));

            EXPECT_EQ(
                key,
                appName + QStringLiteral("_auth_token_") + m_host +
                    QStringLiteral("_") +
                    QString::number(m_authenticationInfo->userId()));

            return threading::makeReadyFuture<QString>(
                m_authenticationInfo->shardId());
        });

    EXPECT_CALL(*m_mockKeychainService, readPassword)
        .WillOnce([&](const QString & service, const QString & key) {
            EXPECT_EQ(service, appName + QStringLiteral("_shard_id"));

            EXPECT_EQ(
                key,
                appName + QStringLiteral("_shard_id_") + m_host +
                    QStringLiteral("_") +
                    QString::number(m_authenticationInfo->userId()));

            return threading::makeExceptionalFuture<QString>(
                RuntimeError{ErrorString{QStringLiteral("some error")}});
        });

    // Next will fallback to OAuth and then write the acquired authentication
    // info to the keychain
    EXPECT_CALL(*m_mockAuthenticator, authenticateAccount(account))
        .WillOnce(Return(threading::makeReadyFuture<IAuthenticationInfoPtr>(
            m_authenticationInfo)));

    EXPECT_CALL(*m_mockKeychainService, writePassword)
        .WillOnce([&](const QString & service, const QString & key,
                      const QString & password) {
            EXPECT_EQ(service, appName + QStringLiteral("_auth_token"));

            EXPECT_EQ(
                key,
                appName + QStringLiteral("_auth_token_") + m_host +
                    QStringLiteral("_") +
                    QString::number(m_authenticationInfo->userId()));

            EXPECT_EQ(password, m_authenticationInfo->authToken());

            return threading::makeReadyFuture();
        });

    EXPECT_CALL(*m_mockKeychainService, writePassword)
        .WillOnce([&](const QString & service, const QString & key,
                      const QString & password) {
            EXPECT_EQ(service, appName + QStringLiteral("_shard_id"));

            EXPECT_EQ(
                key,
                appName + QStringLiteral("_shard_id_") + m_host +
                    QStringLiteral("_") +
                    QString::number(m_authenticationInfo->userId()));

            EXPECT_EQ(password, m_authenticationInfo->shardId());

            return threading::makeReadyFuture();
        });

    auto future = authenticationInfoProvider->authenticateAccount(
        account, AuthenticationInfoProvider::Mode::NoCache);

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    EXPECT_EQ(future.result().get(), m_authenticationInfo.get());

    checkAuthenticationInfoPartPersistence(
        m_authenticationInfo, account, m_host);
}

TEST_F(
    AuthenticationInfoProviderTest,
    AuthenticateAccountWhenExpirationTimestampIsClose)
{
    const auto authenticationInfoProvider =
        std::make_shared<AuthenticationInfoProvider>(
            m_mockAuthenticator, m_mockKeychainService, m_mockUserInfoProvider,
            m_mockNoteStoreFactory, qevercloud::newRequestContext(),
            qevercloud::nullRetryPolicy(), m_host);

    const Account account{
        QStringLiteral("Full Name"),
        Account::Type::Evernote,
        m_authenticationInfo->userId(),
        Account::EvernoteAccountType::Free,
        m_host,
        m_authenticationInfo->shardId()};

    const auto originalAuthTokenExpirationTime =
        m_authenticationInfo->m_authTokenExpirationTime;

    m_authenticationInfo->m_authTokenExpirationTime =
        QDateTime::currentMSecsSinceEpoch() + 100000;

    setupAuthenticationInfoPartPersistence(
        m_authenticationInfo, account, m_host);

    m_authenticationInfo->m_authTokenExpirationTime =
        originalAuthTokenExpirationTime;

    EXPECT_CALL(*m_mockAuthenticator, authenticateAccount(account))
        .WillOnce(Return(threading::makeReadyFuture<IAuthenticationInfoPtr>(
            m_authenticationInfo)));

    static const QString appName = QCoreApplication::applicationName();

    EXPECT_CALL(*m_mockKeychainService, writePassword)
        .WillOnce([&](const QString & service, const QString & key,
                      const QString & password) {
            EXPECT_EQ(service, appName + QStringLiteral("_auth_token"));

            EXPECT_EQ(
                key,
                appName + QStringLiteral("_auth_token_") + m_host +
                    QStringLiteral("_") +
                    QString::number(m_authenticationInfo->userId()));

            EXPECT_EQ(password, m_authenticationInfo->authToken());

            return threading::makeReadyFuture();
        });

    EXPECT_CALL(*m_mockKeychainService, writePassword)
        .WillOnce([&](const QString & service, const QString & key,
                      const QString & password) {
            EXPECT_EQ(service, appName + QStringLiteral("_shard_id"));

            EXPECT_EQ(
                key,
                appName + QStringLiteral("_shard_id_") + m_host +
                    QStringLiteral("_") +
                    QString::number(m_authenticationInfo->userId()));

            EXPECT_EQ(password, m_authenticationInfo->shardId());

            return threading::makeReadyFuture();
        });

    auto future = authenticationInfoProvider->authenticateAccount(
        account, AuthenticationInfoProvider::Mode::NoCache);

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);

    EXPECT_EQ(future.result().get(), m_authenticationInfo.get());

    checkAuthenticationInfoPartPersistence(
        m_authenticationInfo, account, m_host);
}

TEST_F(
    AuthenticationInfoProviderTest,
    RefuseToAuthenticateToLinkedNotebookWithNonEvernoteAccount)
{
    const auto authenticationInfoProvider =
        std::make_shared<AuthenticationInfoProvider>(
            m_mockAuthenticator, m_mockKeychainService, m_mockUserInfoProvider,
            m_mockNoteStoreFactory, qevercloud::newRequestContext(),
            qevercloud::nullRetryPolicy(), m_host);

    const Account account{QStringLiteral("Full Name"), Account::Type::Local};

    auto future = authenticationInfoProvider->authenticateToLinkedNotebook(
        account,
        qevercloud::LinkedNotebookBuilder{}
            .setGuid(UidGenerator::Generate())
            .build(),
        AuthenticationInfoProvider::Mode::Cache);
    EXPECT_TRUE(future.isFinished());
    EXPECT_THROW(future.waitForFinished(), InvalidArgument);
}

TEST_F(
    AuthenticationInfoProviderTest,
    RefuseToAuthenticateToLinkedNotebookWithEmptyLinkedNotebookGuid)
{
    const auto authenticationInfoProvider =
        std::make_shared<AuthenticationInfoProvider>(
            m_mockAuthenticator, m_mockKeychainService, m_mockUserInfoProvider,
            m_mockNoteStoreFactory, qevercloud::newRequestContext(),
            qevercloud::nullRetryPolicy(), m_host);

    const Account account{
        QStringLiteral("Full Name"),
        Account::Type::Evernote,
        m_authenticationInfo->userId(),
        Account::EvernoteAccountType::Free,
        m_host,
        m_authenticationInfo->shardId()};

    auto future = authenticationInfoProvider->authenticateToLinkedNotebook(
        account, qevercloud::LinkedNotebook{},
        AuthenticationInfoProvider::Mode::Cache);
    EXPECT_TRUE(future.isFinished());
    EXPECT_THROW(future.waitForFinished(), InvalidArgument);
}

} // namespace quentier::synchronization::tests
