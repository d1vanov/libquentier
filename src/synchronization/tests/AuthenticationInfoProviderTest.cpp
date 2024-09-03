/*
 * Copyright 2022-2024 Dmitry Ivanov
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

#include "Utils.h"

#include <synchronization/AuthenticationInfoProvider.h>
#include <synchronization/tests/mocks/MockINoteStoreFactory.h>
#include <synchronization/tests/mocks/MockIUserInfoProvider.h>
#include <synchronization/tests/mocks/qevercloud/services/MockINoteStore.h>
#include <synchronization/types/AuthenticationInfo.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/RuntimeError.h>
#include <quentier/synchronization/tests/mocks/MockIAuthenticator.h>
#include <quentier/threading/Future.h>
#include <quentier/utility/ApplicationSettings.h>
#include <quentier/utility/UidGenerator.h>
#include <quentier/utility/Unreachable.h>
#include <quentier/utility/tests/mocks/MockIKeychainService.h>

#include <qevercloud/DurableService.h>
#include <qevercloud/IRequestContext.h>
#include <qevercloud/types/builders/AuthenticationResultBuilder.h>
#include <qevercloud/types/builders/LinkedNotebookBuilder.h>
#include <qevercloud/types/builders/UserBuilder.h>
#include <qevercloud/types/builders/UserUrlsBuilder.h>

#include <QCoreApplication>
#include <QDateTime>

#include <gtest/gtest.h>

#include <array>
#include <limits>

// clazy:excludeall=non-pod-global-static
// clazy:excludeall=returning-void-expression

namespace quentier::synchronization::tests {

using testing::_;
using testing::InSequence;
using testing::Ne;
using testing::Return;
using testing::StrictMock;

namespace {

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

    const ApplicationSettings::GroupCloser groupCloser{appSettings};

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

void checkLinkedNotebookAuthenticationInfoPartPersistence(
    const IAuthenticationInfoPtr & authenticationInfo, const Account & account,
    const QString & host, const qevercloud::Guid & linkedNotebookGuid)
{
    ApplicationSettings appSettings{
        account, QStringLiteral("SynchronizationPersistence")};

    appSettings.beginGroup(
        QStringLiteral("Authentication/") + host + QStringLiteral("/") +
        QString::number(authenticationInfo->userId()));

    ApplicationSettings::GroupCloser groupCloser{appSettings};

    EXPECT_GE(appSettings.allKeys().size(), 2);

    EXPECT_EQ(
        appSettings
            .value(
                QStringLiteral("LinkedNotebookExpirationTimestamp_") +
                linkedNotebookGuid)
            .toLongLong(),
        authenticationInfo->authTokenExpirationTime());

    EXPECT_EQ(
        appSettings
            .value(
                QStringLiteral("LinkedNotebookAuthenticationTimestamp_") +
                linkedNotebookGuid)
            .toLongLong(),
        authenticationInfo->authenticationTime());
}

void checkNoLinkedNotebookAuthenticationInfoPartPersistence(
    const IAuthenticationInfoPtr & authenticationInfo, const Account & account,
    const QString & host, const qevercloud::Guid & linkedNotebookGuid)
{
    ApplicationSettings appSettings{
        account, QStringLiteral("SynchronizationPersistence")};

    appSettings.beginGroup(
        QStringLiteral("Authentication/") + host + QStringLiteral("/") +
        QString::number(authenticationInfo->userId()));

    ApplicationSettings::GroupCloser groupCloser{appSettings};

    EXPECT_FALSE(appSettings.contains(
        QStringLiteral("LinkedNotebookExpirationTimestamp_") +
        linkedNotebookGuid));

    EXPECT_FALSE(appSettings.contains(
        QStringLiteral("LinkedNotebookAuthenticationTimestamp_") +
        linkedNotebookGuid));
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

void setupLinkedNotebookAuthenticationInfoPartPersistence(
    const IAuthenticationInfoPtr & authenticationInfo, const Account & account,
    const QString & host, const qevercloud::Guid & linkedNotebookGuid)
{
    ApplicationSettings appSettings{
        account, QStringLiteral("SynchronizationPersistence")};

    appSettings.beginGroup(
        QStringLiteral("Authentication/") + host + QStringLiteral("/") +
        QString::number(authenticationInfo->userId()));

    ApplicationSettings::GroupCloser groupCloser{appSettings};

    appSettings.setValue(
        QStringLiteral("LinkedNotebookExpirationTimestamp_") +
            linkedNotebookGuid,
        authenticationInfo->authTokenExpirationTime());

    appSettings.setValue(
        QStringLiteral("LinkedNotebookAuthenticationTimestamp_") +
            linkedNotebookGuid,
        authenticationInfo->authenticationTime());
}

void checkNoAuthenticationInfoPartPersistence(
    const IAuthenticationInfoPtr & authenticationInfo, const Account & account,
    const QString & host)
{
    ApplicationSettings appSettings{
        account, QStringLiteral("SynchronizationPersistence")};

    appSettings.beginGroup(
        QStringLiteral("Authentication/") + host + QStringLiteral("/") +
        QString::number(authenticationInfo->userId()));

    const ApplicationSettings::GroupCloser groupCloser{appSettings};

    EXPECT_FALSE(appSettings.contains(QStringLiteral("NoteStoreUrl")));
    EXPECT_FALSE(appSettings.contains(QStringLiteral("ExpirationTimestamp")));
    EXPECT_FALSE(appSettings.contains(QStringLiteral("WebApiUrlPrefix")));
    EXPECT_FALSE(appSettings.contains(QStringLiteral("UserStoreCookie")));
    EXPECT_FALSE(
        appSettings.contains(QStringLiteral("AuthenticationTimestamp")));
}

} // namespace

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
            QStringLiteral("username"),
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

    const QString m_host = QStringLiteral("www.evernote.com");

    const std::shared_ptr<AuthenticationInfo> m_authenticationInfo =
        createSampleAuthenticationInfo();

    const std::shared_ptr<mocks::qevercloud::MockINoteStore> m_mockNoteStore =
        std::make_shared<StrictMock<mocks::qevercloud::MockINoteStore>>();
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
    EXPECT_NO_THROW(
        const auto authenticationInfoProvider =
            std::make_shared<AuthenticationInfoProvider>(
                m_mockAuthenticator, m_mockKeychainService,
                m_mockUserInfoProvider, m_mockNoteStoreFactory, nullptr,
                qevercloud::nullRetryPolicy(), m_host));
}

TEST_F(AuthenticationInfoProviderTest, CtorNullRetryPolicy)
{
    EXPECT_NO_THROW(
        const auto authenticationInfoProvider =
            std::make_shared<AuthenticationInfoProvider>(
                m_mockAuthenticator, m_mockKeychainService,
                m_mockUserInfoProvider, m_mockNoteStoreFactory,
                qevercloud::newRequestContext(), nullptr, m_host));
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

    const auto user = qevercloud::UserBuilder{}
                          .setId(m_authenticationInfo->userId())
                          .setUsername(QStringLiteral("username"))
                          .setName(QStringLiteral("Full Name"))
                          .setPrivilege(qevercloud::PrivilegeLevel::NORMAL)
                          .setServiceLevel(qevercloud::ServiceLevel::BASIC)
                          .setActive(true)
                          .setShardId(m_authenticationInfo->shardId())
                          .build();

    Account account{
        *user.username(),
        Account::Type::Evernote,
        m_authenticationInfo->userId(),
        Account::EvernoteAccountType::Free,
        m_host,
        m_authenticationInfo->shardId()};
    account.setDisplayName(*user.name());

    InSequence s;

    EXPECT_CALL(*m_mockAuthenticator, authenticateNewAccount)
        .WillOnce(Return(threading::makeReadyFuture<IAuthenticationInfoPtr>(
            m_authenticationInfo)));

    EXPECT_CALL(*m_mockUserInfoProvider, userInfo)
        .WillOnce([&](const qevercloud::IRequestContextPtr & ctx) {
            EXPECT_TRUE(ctx);
            EXPECT_EQ(
                ctx->authenticationToken(), m_authenticationInfo->authToken());
            EXPECT_EQ(ctx->cookies(), m_authenticationInfo->userStoreCookies());
            return threading::makeReadyFuture<qevercloud::User>(user);
        });

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

    waitForFuture(future);
    ASSERT_EQ(future.resultCount(), 1);

    const auto pair = future.result();
    EXPECT_EQ(
        pair.first.toString().toStdString(), account.toString().toStdString());
    EXPECT_EQ(pair.second.get(), m_authenticationInfo.get());

    checkAuthenticationInfoPartPersistence(
        m_authenticationInfo, account, m_host);
}

TEST_F(AuthenticationInfoProviderTest, PropagateErrorWhenAuthNewAccount)
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
    waitForFuture(future);

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
    ReturnErrorIfFailingToFindUserInfoWhenAuthenticatingNewAccount)
{
    const auto authenticationInfoProvider =
        std::make_shared<AuthenticationInfoProvider>(
            m_mockAuthenticator, m_mockKeychainService, m_mockUserInfoProvider,
            m_mockNoteStoreFactory, qevercloud::newRequestContext(),
            qevercloud::nullRetryPolicy(), m_host);

    const Account account{
        QStringLiteral("username"),
        Account::Type::Evernote,
        m_authenticationInfo->userId(),
        Account::EvernoteAccountType::Free,
        m_host,
        m_authenticationInfo->shardId()};

    InSequence s;

    EXPECT_CALL(*m_mockAuthenticator, authenticateNewAccount)
        .WillOnce(Return(threading::makeReadyFuture<IAuthenticationInfoPtr>(
            m_authenticationInfo)));

    const ErrorString exceptionMessage =
        ErrorString{QStringLiteral("some error")};

    EXPECT_CALL(*m_mockUserInfoProvider, userInfo)
        .WillOnce([&](const qevercloud::IRequestContextPtr & ctx) {
            EXPECT_TRUE(ctx);
            EXPECT_EQ(
                ctx->authenticationToken(), m_authenticationInfo->authToken());
            EXPECT_EQ(ctx->cookies(), m_authenticationInfo->userStoreCookies());
            return threading::makeExceptionalFuture<qevercloud::User>(
                RuntimeError{exceptionMessage});
        });

    auto future = authenticationInfoProvider->authenticateNewAccount();
    waitForFuture(future);

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
    TolerateErrorOfSavingAuthTokenToKeychainWhenAuthenticatingNewAccount)
{
    const auto authenticationInfoProvider =
        std::make_shared<AuthenticationInfoProvider>(
            m_mockAuthenticator, m_mockKeychainService, m_mockUserInfoProvider,
            m_mockNoteStoreFactory, qevercloud::newRequestContext(),
            qevercloud::nullRetryPolicy(), m_host);

    const auto user = qevercloud::UserBuilder{}
                          .setId(m_authenticationInfo->userId())
                          .setUsername(QStringLiteral("username"))
                          .setName(QStringLiteral("Full Name"))
                          .setPrivilege(qevercloud::PrivilegeLevel::NORMAL)
                          .setServiceLevel(qevercloud::ServiceLevel::BASIC)
                          .setActive(true)
                          .setShardId(m_authenticationInfo->shardId())
                          .build();

    Account account{
        *user.username(),
        Account::Type::Evernote,
        m_authenticationInfo->userId(),
        Account::EvernoteAccountType::Free,
        m_host,
        m_authenticationInfo->shardId()};
    account.setDisplayName(*user.name());

    InSequence s;

    EXPECT_CALL(*m_mockAuthenticator, authenticateNewAccount)
        .WillOnce(Return(threading::makeReadyFuture<IAuthenticationInfoPtr>(
            m_authenticationInfo)));

    EXPECT_CALL(*m_mockUserInfoProvider, userInfo)
        .WillOnce([&](const qevercloud::IRequestContextPtr & ctx) {
            EXPECT_TRUE(ctx);
            EXPECT_EQ(
                ctx->authenticationToken(), m_authenticationInfo->authToken());
            EXPECT_EQ(ctx->cookies(), m_authenticationInfo->userStoreCookies());
            return threading::makeReadyFuture<qevercloud::User>(user);
        });

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
    waitForFuture(future);
    ASSERT_EQ(future.resultCount(), 1);

    const auto pair = future.result();
    EXPECT_EQ(pair.first, account);
    EXPECT_EQ(pair.second.get(), m_authenticationInfo.get());

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

    const auto user = qevercloud::UserBuilder{}
                          .setId(m_authenticationInfo->userId())
                          .setUsername(QStringLiteral("username"))
                          .setName(QStringLiteral("Full Name"))
                          .setPrivilege(qevercloud::PrivilegeLevel::NORMAL)
                          .setServiceLevel(qevercloud::ServiceLevel::BASIC)
                          .setActive(true)
                          .setShardId(m_authenticationInfo->shardId())
                          .build();

    Account account{
        *user.username(),
        Account::Type::Evernote,
        m_authenticationInfo->userId(),
        Account::EvernoteAccountType::Free,
        m_host,
        m_authenticationInfo->shardId()};
    account.setDisplayName(*user.name());

    InSequence s;

    EXPECT_CALL(*m_mockAuthenticator, authenticateNewAccount)
        .WillOnce(Return(threading::makeReadyFuture<IAuthenticationInfoPtr>(
            m_authenticationInfo)));

    EXPECT_CALL(*m_mockUserInfoProvider, userInfo)
        .WillOnce([&](const qevercloud::IRequestContextPtr & ctx) {
            EXPECT_TRUE(ctx);
            EXPECT_EQ(
                ctx->authenticationToken(), m_authenticationInfo->authToken());
            EXPECT_EQ(ctx->cookies(), m_authenticationInfo->userStoreCookies());
            return threading::makeReadyFuture<qevercloud::User>(user);
        });

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
    waitForFuture(future);
    ASSERT_EQ(future.resultCount(), 1);

    const auto pair = future.result();
    EXPECT_EQ(pair.first, account);
    EXPECT_EQ(pair.second.get(), m_authenticationInfo.get());

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
        QStringLiteral("username"),
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

    waitForFuture(future);
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
        QStringLiteral("username"),
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

    waitForFuture(future);
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
        QStringLiteral("username"),
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

    waitForFuture(future);
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

    waitForFuture(future);
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

    const Account account{QStringLiteral("username"), Account::Type::Local};

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
        QStringLiteral("username"),
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
        account, AuthenticationInfoProvider::Mode::Cache);

    waitForFuture(future);
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
        QStringLiteral("username"),
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
        account, AuthenticationInfoProvider::Mode::Cache);

    waitForFuture(future);
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
        QStringLiteral("username"),
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

    waitForFuture(future);
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

    const Account account{QStringLiteral("username"), Account::Type::Local};

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
        QStringLiteral("username"),
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

TEST_F(AuthenticationInfoProviderTest, AuthenticateToPublicLinkedNotebook)
{
    const auto authenticationInfoProvider =
        std::make_shared<AuthenticationInfoProvider>(
            m_mockAuthenticator, m_mockKeychainService, m_mockUserInfoProvider,
            m_mockNoteStoreFactory, qevercloud::newRequestContext(),
            qevercloud::nullRetryPolicy(), m_host);

    const Account account{
        QStringLiteral("username"),
        Account::Type::Evernote,
        m_authenticationInfo->userId(),
        Account::EvernoteAccountType::Free,
        m_host,
        m_authenticationInfo->shardId()};

    const qevercloud::LinkedNotebook linkedNotebook =
        qevercloud::LinkedNotebookBuilder{}
            .setGuid(UidGenerator::Generate())
            .setUsername(QStringLiteral("username"))
            .setUri(QStringLiteral("uri"))
            .setNoteStoreUrl(m_authenticationInfo->noteStoreUrl())
            .setWebApiUrlPrefix(m_authenticationInfo->webApiUrlPrefix())
            .setShardId(m_authenticationInfo->shardId())
            .build();

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

    auto future = authenticationInfoProvider->authenticateToLinkedNotebook(
        account, linkedNotebook, AuthenticationInfoProvider::Mode::NoCache);

    waitForFuture(future);
    ASSERT_EQ(future.resultCount(), 1);

    const auto authenticationInfo = future.result();
    EXPECT_EQ(authenticationInfo->userId(), account.id());
    EXPECT_EQ(
        authenticationInfo->authTokenExpirationTime(),
        m_authenticationInfo->authTokenExpirationTime());

    EXPECT_EQ(authenticationInfo->shardId(), linkedNotebook.shardId().value());
    EXPECT_EQ(
        authenticationInfo->noteStoreUrl(),
        linkedNotebook.noteStoreUrl().value());

    EXPECT_EQ(
        authenticationInfo->webApiUrlPrefix(),
        linkedNotebook.webApiUrlPrefix().value());

    EXPECT_EQ(
        authenticationInfo->authToken(), m_authenticationInfo->authToken());
}

TEST_F(
    AuthenticationInfoProviderTest,
    AuthenticateToLinkedNotebookWithoutCacheExplicitly)
{
    const auto requestContext = qevercloud::newRequestContext();
    const auto retryPolicy = qevercloud::nullRetryPolicy();

    const auto authenticationInfoProvider =
        std::make_shared<AuthenticationInfoProvider>(
            m_mockAuthenticator, m_mockKeychainService, m_mockUserInfoProvider,
            m_mockNoteStoreFactory, requestContext, retryPolicy, m_host);

    const Account account{
        QStringLiteral("username"),
        Account::Type::Evernote,
        m_authenticationInfo->userId(),
        Account::EvernoteAccountType::Free,
        m_host,
        m_authenticationInfo->shardId()};

    const qevercloud::LinkedNotebook linkedNotebook =
        qevercloud::LinkedNotebookBuilder{}
            .setGuid(UidGenerator::Generate())
            .setUsername(QStringLiteral("username"))
            .setSharedNotebookGlobalId(UidGenerator::Generate())
            .setNoteStoreUrl(m_authenticationInfo->noteStoreUrl())
            .setShardId(m_authenticationInfo->shardId())
            .build();

    setupAuthenticationInfoPartPersistence(
        m_authenticationInfo, account, m_host);

    InSequence s;

    // NOTE: preventing the leak of m_mockNoteStore below and corresponding
    // warning from gtest
    EXPECT_CALL(
        *m_mockNoteStoreFactory,
        noteStore(
            linkedNotebook.noteStoreUrl().value(), linkedNotebook.guid(),
            requestContext, retryPolicy))
        .WillOnce(
            [noteStoreWeak =
                 std::weak_ptr<mocks::qevercloud::MockINoteStore>{
                     m_mockNoteStore}](
                const QString & noteStoreUrl,
                const std::optional<qevercloud::Guid> & linkedNotebookGuid,
                const qevercloud::IRequestContextPtr & ctx,
                const qevercloud::IRetryPolicyPtr & retryPolicy) {
                Q_UNUSED(noteStoreUrl)
                Q_UNUSED(linkedNotebookGuid)
                Q_UNUSED(ctx)
                Q_UNUSED(retryPolicy)
                return noteStoreWeak.lock();
            });

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

    EXPECT_CALL(
        *m_mockNoteStore,
        authenticateToSharedNotebookAsync(
            linkedNotebook.sharedNotebookGlobalId().value(),
            Ne(requestContext)))
        .WillOnce([this](
                      [[maybe_unused]] const QString & shareKeyOrGlobalId,
                      const qevercloud::IRequestContextPtr & ctx) {
            EXPECT_TRUE(ctx);
            if (ctx) {
                EXPECT_EQ(
                    ctx->authenticationToken(),
                    m_authenticationInfo->authToken());
            }

            return threading::makeReadyFuture<qevercloud::AuthenticationResult>(
                qevercloud::AuthenticationResultBuilder{}
                    .setAuthenticationToken(m_authenticationInfo->authToken())
                    .setExpiration(
                        m_authenticationInfo->authTokenExpirationTime())
                    .setCurrentTime(m_authenticationInfo->authenticationTime())
                    .setUrls(qevercloud::UserUrlsBuilder{}
                                 .setNoteStoreUrl(
                                     m_authenticationInfo->noteStoreUrl())
                                 .setWebApiUrlPrefix(
                                     m_authenticationInfo->webApiUrlPrefix())
                                 .build())
                    .build());
        });

    EXPECT_CALL(*m_mockKeychainService, writePassword)
        .WillOnce([&](const QString & service, const QString & key, // NOLINT
                      const QString & password) {
            EXPECT_EQ(
                service,
                appName + QStringLiteral("_linked_notebook_auth_token"));

            EXPECT_EQ(
                key,
                appName + QStringLiteral("_linked_notebook_auth_token_") +
                    m_host + QStringLiteral("_") +
                    QString::number(m_authenticationInfo->userId()) +
                    QStringLiteral("_") + linkedNotebook.guid().value());

            EXPECT_EQ(password, m_authenticationInfo->authToken());

            return threading::makeReadyFuture();
        });

    auto future = authenticationInfoProvider->authenticateToLinkedNotebook(
        account, linkedNotebook, AuthenticationInfoProvider::Mode::NoCache);

    waitForFuture(future);
    ASSERT_EQ(future.resultCount(), 1);

    EXPECT_NE(future.result().get(), m_authenticationInfo.get());

    const auto * authenticationInfo =
        dynamic_cast<const AuthenticationInfo *>(future.result().get());
    ASSERT_TRUE(authenticationInfo);

    m_authenticationInfo->m_userStoreCookies.clear();
    EXPECT_EQ(*authenticationInfo, *m_authenticationInfo);

    checkLinkedNotebookAuthenticationInfoPartPersistence(
        m_authenticationInfo, account, m_host, linkedNotebook.guid().value());
}

TEST_F(
    AuthenticationInfoProviderTest,
    AuthenticateToLinkedNotebookWithoutCacheImplicitly)
{
    const auto requestContext = qevercloud::newRequestContext();
    const auto retryPolicy = qevercloud::nullRetryPolicy();

    const auto authenticationInfoProvider =
        std::make_shared<AuthenticationInfoProvider>(
            m_mockAuthenticator, m_mockKeychainService, m_mockUserInfoProvider,
            m_mockNoteStoreFactory, requestContext, retryPolicy, m_host);

    const Account account{
        QStringLiteral("username"),
        Account::Type::Evernote,
        m_authenticationInfo->userId(),
        Account::EvernoteAccountType::Free,
        m_host,
        m_authenticationInfo->shardId()};

    const qevercloud::LinkedNotebook linkedNotebook =
        qevercloud::LinkedNotebookBuilder{}
            .setGuid(UidGenerator::Generate())
            .setUsername(QStringLiteral("username"))
            .setSharedNotebookGlobalId(UidGenerator::Generate())
            .setNoteStoreUrl(m_authenticationInfo->noteStoreUrl())
            .setShardId(m_authenticationInfo->shardId())
            .build();

    setupAuthenticationInfoPartPersistence(
        m_authenticationInfo, account, m_host);

    InSequence s;

    // NOTE: preventing the leak of m_mockNoteStore below and corresponding
    // warning from gtest
    EXPECT_CALL(
        *m_mockNoteStoreFactory,
        noteStore(
            linkedNotebook.noteStoreUrl().value(), linkedNotebook.guid(),
            requestContext, retryPolicy))
        .WillOnce(
            [noteStoreWeak =
                 std::weak_ptr<mocks::qevercloud::MockINoteStore>{
                     m_mockNoteStore}](
                const QString & noteStoreUrl,
                const std::optional<qevercloud::Guid> & linkedNotebookGuid,
                const qevercloud::IRequestContextPtr & ctx,
                const qevercloud::IRetryPolicyPtr & retryPolicy) {
                Q_UNUSED(noteStoreUrl)
                Q_UNUSED(linkedNotebookGuid)
                Q_UNUSED(ctx)
                Q_UNUSED(retryPolicy)
                return noteStoreWeak.lock();
            });

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

    EXPECT_CALL(
        *m_mockNoteStore,
        authenticateToSharedNotebookAsync(
            linkedNotebook.sharedNotebookGlobalId().value(),
            Ne(requestContext)))
        .WillOnce([this](
                      [[maybe_unused]] const QString & shareKeyOrGlobalId,
                      const qevercloud::IRequestContextPtr & ctx) {
            EXPECT_TRUE(ctx);
            if (ctx) {
                EXPECT_EQ(
                    ctx->authenticationToken(),
                    m_authenticationInfo->authToken());
            }

            return threading::makeReadyFuture<qevercloud::AuthenticationResult>(
                qevercloud::AuthenticationResultBuilder{}
                    .setAuthenticationToken(m_authenticationInfo->authToken())
                    .setExpiration(
                        m_authenticationInfo->authTokenExpirationTime())
                    .setCurrentTime(m_authenticationInfo->authenticationTime())
                    .setUrls(qevercloud::UserUrlsBuilder{}
                                 .setNoteStoreUrl(
                                     m_authenticationInfo->noteStoreUrl())
                                 .setWebApiUrlPrefix(
                                     m_authenticationInfo->webApiUrlPrefix())
                                 .build())
                    .build());
        });

    EXPECT_CALL(*m_mockKeychainService, writePassword)
        .WillOnce([&](const QString & service, const QString & key, // NOLINT
                      const QString & password) {
            EXPECT_EQ(
                service,
                appName + QStringLiteral("_linked_notebook_auth_token"));

            EXPECT_EQ(
                key,
                appName + QStringLiteral("_linked_notebook_auth_token_") +
                    m_host + QStringLiteral("_") +
                    QString::number(m_authenticationInfo->userId()) +
                    QStringLiteral("_") + linkedNotebook.guid().value());

            EXPECT_EQ(password, m_authenticationInfo->authToken());

            return threading::makeReadyFuture();
        });

    auto future = authenticationInfoProvider->authenticateToLinkedNotebook(
        account, linkedNotebook, AuthenticationInfoProvider::Mode::Cache);

    waitForFuture(future);
    ASSERT_EQ(future.resultCount(), 1);

    EXPECT_NE(future.result().get(), m_authenticationInfo.get());

    const auto * authenticationInfo =
        dynamic_cast<const AuthenticationInfo *>(future.result().get());
    ASSERT_TRUE(authenticationInfo);

    m_authenticationInfo->m_userStoreCookies.clear();
    EXPECT_EQ(*authenticationInfo, *m_authenticationInfo);

    checkLinkedNotebookAuthenticationInfoPartPersistence(
        m_authenticationInfo, account, m_host, linkedNotebook.guid().value());
}

TEST_F(AuthenticationInfoProviderTest, AuthenticateToLinkedNotebookWithCache)
{
    const auto requestContext = qevercloud::newRequestContext();
    const auto retryPolicy = qevercloud::nullRetryPolicy();

    const auto authenticationInfoProvider =
        std::make_shared<AuthenticationInfoProvider>(
            m_mockAuthenticator, m_mockKeychainService, m_mockUserInfoProvider,
            m_mockNoteStoreFactory, requestContext, retryPolicy, m_host);

    const Account account{
        QStringLiteral("username"),
        Account::Type::Evernote,
        m_authenticationInfo->userId(),
        Account::EvernoteAccountType::Free,
        m_host,
        m_authenticationInfo->shardId()};

    const qevercloud::LinkedNotebook linkedNotebook =
        qevercloud::LinkedNotebookBuilder{}
            .setGuid(UidGenerator::Generate())
            .setUsername(QStringLiteral("username"))
            .setSharedNotebookGlobalId(UidGenerator::Generate())
            .setNoteStoreUrl(m_authenticationInfo->noteStoreUrl())
            .setWebApiUrlPrefix(m_authenticationInfo->webApiUrlPrefix())
            .setShardId(m_authenticationInfo->shardId())
            .build();

    setupLinkedNotebookAuthenticationInfoPartPersistence(
        m_authenticationInfo, account, m_host, linkedNotebook.guid().value());

    InSequence s;

    static const QString appName = QCoreApplication::applicationName();

    EXPECT_CALL(*m_mockKeychainService, readPassword)
        .WillOnce([&](const QString & service, const QString & key) {
            EXPECT_EQ(
                service,
                appName + QStringLiteral("_linked_notebook_auth_token"));

            EXPECT_EQ(
                key,
                appName + QStringLiteral("_linked_notebook_auth_token_") +
                    m_host + QStringLiteral("_") +
                    QString::number(m_authenticationInfo->userId()) +
                    QStringLiteral("_") + linkedNotebook.guid().value());

            return threading::makeReadyFuture<QString>(
                m_authenticationInfo->authToken());
        });

    auto future = authenticationInfoProvider->authenticateToLinkedNotebook(
        account, linkedNotebook, AuthenticationInfoProvider::Mode::Cache);

    waitForFuture(future);
    ASSERT_EQ(future.resultCount(), 1);

    EXPECT_NE(future.result().get(), m_authenticationInfo.get());

    const auto * authenticationInfo =
        dynamic_cast<const AuthenticationInfo *>(future.result().get());
    ASSERT_TRUE(authenticationInfo);

    m_authenticationInfo->m_userStoreCookies.clear();
    EXPECT_EQ(*authenticationInfo, *m_authenticationInfo);

    checkLinkedNotebookAuthenticationInfoPartPersistence(
        m_authenticationInfo, account, m_host, linkedNotebook.guid().value());

    // The second attempt should not go to the keychain as the token for this
    // linked notebook would now be cached inside AuthenticationInfoProvider
    future = authenticationInfoProvider->authenticateToLinkedNotebook(
        account, linkedNotebook, AuthenticationInfoProvider::Mode::Cache);

    waitForFuture(future);
    ASSERT_EQ(future.resultCount(), 1);

    EXPECT_NE(future.result().get(), m_authenticationInfo.get());

    authenticationInfo =
        dynamic_cast<const AuthenticationInfo *>(future.result().get());
    ASSERT_TRUE(authenticationInfo);

    m_authenticationInfo->m_userStoreCookies.clear();
    EXPECT_EQ(*authenticationInfo, *m_authenticationInfo);

    checkLinkedNotebookAuthenticationInfoPartPersistence(
        m_authenticationInfo, account, m_host, linkedNotebook.guid().value());
}

TEST_F(
    AuthenticationInfoProviderTest,
    AuthenticateToLinkedNotebookWithCacheWhenCannotReadAuthTokenFromKeychain)
{
    const auto requestContext = qevercloud::newRequestContext();
    const auto retryPolicy = qevercloud::nullRetryPolicy();

    const auto authenticationInfoProvider =
        std::make_shared<AuthenticationInfoProvider>(
            m_mockAuthenticator, m_mockKeychainService, m_mockUserInfoProvider,
            m_mockNoteStoreFactory, requestContext, retryPolicy, m_host);

    const Account account{
        QStringLiteral("username"),
        Account::Type::Evernote,
        m_authenticationInfo->userId(),
        Account::EvernoteAccountType::Free,
        m_host,
        m_authenticationInfo->shardId()};

    const qevercloud::LinkedNotebook linkedNotebook =
        qevercloud::LinkedNotebookBuilder{}
            .setGuid(UidGenerator::Generate())
            .setUsername(QStringLiteral("username"))
            .setSharedNotebookGlobalId(UidGenerator::Generate())
            .setNoteStoreUrl(m_authenticationInfo->noteStoreUrl())
            .setWebApiUrlPrefix(m_authenticationInfo->webApiUrlPrefix())
            .setShardId(m_authenticationInfo->shardId())
            .build();

    setupAuthenticationInfoPartPersistence(
        m_authenticationInfo, account, m_host);

    setupLinkedNotebookAuthenticationInfoPartPersistence(
        m_authenticationInfo, account, m_host, linkedNotebook.guid().value());

    InSequence s;

    static const QString appName = QCoreApplication::applicationName();

    EXPECT_CALL(*m_mockKeychainService, readPassword)
        .WillOnce([&](const QString & service, const QString & key) {
            EXPECT_EQ(
                service,
                appName + QStringLiteral("_linked_notebook_auth_token"));

            EXPECT_EQ(
                key,
                appName + QStringLiteral("_linked_notebook_auth_token_") +
                    m_host + QStringLiteral("_") +
                    QString::number(m_authenticationInfo->userId()) +
                    QStringLiteral("_") + linkedNotebook.guid().value());

            return threading::makeExceptionalFuture<QString>(
                RuntimeError{ErrorString{QStringLiteral("some error")}});
        });

    // NOTE: preventing the leak of m_mockNoteStore below and corresponding
    // warning from gtest
    EXPECT_CALL(
        *m_mockNoteStoreFactory,
        noteStore(
            linkedNotebook.noteStoreUrl().value(), linkedNotebook.guid(),
            requestContext, retryPolicy))
        .WillOnce(
            [noteStoreWeak =
                 std::weak_ptr<mocks::qevercloud::MockINoteStore>{
                     m_mockNoteStore}](
                const QString & noteStoreUrl,
                const std::optional<qevercloud::Guid> & linkedNotebookGuid,
                const qevercloud::IRequestContextPtr & ctx,
                const qevercloud::IRetryPolicyPtr & retryPolicy) {
                Q_UNUSED(noteStoreUrl)
                Q_UNUSED(linkedNotebookGuid)
                Q_UNUSED(ctx)
                Q_UNUSED(retryPolicy)
                return noteStoreWeak.lock();
            });

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

    EXPECT_CALL(
        *m_mockNoteStore,
        authenticateToSharedNotebookAsync(
            linkedNotebook.sharedNotebookGlobalId().value(),
            Ne(requestContext)))
        .WillOnce([this](
                      [[maybe_unused]] const QString & shareKeyOrGlobalId,
                      const qevercloud::IRequestContextPtr & ctx) {
            EXPECT_TRUE(ctx);
            if (ctx) {
                EXPECT_EQ(
                    ctx->authenticationToken(),
                    m_authenticationInfo->authToken());
            }

            return threading::makeReadyFuture<qevercloud::AuthenticationResult>(
                qevercloud::AuthenticationResultBuilder{}
                    .setAuthenticationToken(m_authenticationInfo->authToken())
                    .setExpiration(
                        m_authenticationInfo->authTokenExpirationTime())
                    .setCurrentTime(m_authenticationInfo->authenticationTime())
                    .setUrls(qevercloud::UserUrlsBuilder{}
                                 .setNoteStoreUrl(
                                     m_authenticationInfo->noteStoreUrl())
                                 .setWebApiUrlPrefix(
                                     m_authenticationInfo->webApiUrlPrefix())
                                 .build())
                    .build());
        });

    EXPECT_CALL(*m_mockKeychainService, writePassword)
        .WillOnce([&](const QString & service, const QString & key, // NOLINT
                      const QString & password) {
            EXPECT_EQ(
                service,
                appName + QStringLiteral("_linked_notebook_auth_token"));

            EXPECT_EQ(
                key,
                appName + QStringLiteral("_linked_notebook_auth_token_") +
                    m_host + QStringLiteral("_") +
                    QString::number(m_authenticationInfo->userId()) +
                    QStringLiteral("_") + linkedNotebook.guid().value());

            EXPECT_EQ(password, m_authenticationInfo->authToken());

            return threading::makeReadyFuture();
        });

    auto future = authenticationInfoProvider->authenticateToLinkedNotebook(
        account, linkedNotebook, AuthenticationInfoProvider::Mode::Cache);

    waitForFuture(future);
    ASSERT_EQ(future.resultCount(), 1);

    EXPECT_NE(future.result().get(), m_authenticationInfo.get());

    const auto * authenticationInfo =
        dynamic_cast<const AuthenticationInfo *>(future.result().get());
    ASSERT_TRUE(authenticationInfo);

    m_authenticationInfo->m_userStoreCookies.clear();
    EXPECT_EQ(*authenticationInfo, *m_authenticationInfo);

    checkLinkedNotebookAuthenticationInfoPartPersistence(
        m_authenticationInfo, account, m_host, linkedNotebook.guid().value());
}

TEST_F(
    AuthenticationInfoProviderTest,
    AuthenticateToLinkedNotebookWhenExpirationTimestampIsClose)
{
    const auto requestContext = qevercloud::newRequestContext();
    const auto retryPolicy = qevercloud::nullRetryPolicy();

    const auto authenticationInfoProvider =
        std::make_shared<AuthenticationInfoProvider>(
            m_mockAuthenticator, m_mockKeychainService, m_mockUserInfoProvider,
            m_mockNoteStoreFactory, requestContext, retryPolicy, m_host);

    const Account account{
        QStringLiteral("username"),
        Account::Type::Evernote,
        m_authenticationInfo->userId(),
        Account::EvernoteAccountType::Free,
        m_host,
        m_authenticationInfo->shardId()};

    const qevercloud::LinkedNotebook linkedNotebook =
        qevercloud::LinkedNotebookBuilder{}
            .setGuid(UidGenerator::Generate())
            .setUsername(QStringLiteral("username"))
            .setSharedNotebookGlobalId(UidGenerator::Generate())
            .setNoteStoreUrl(m_authenticationInfo->noteStoreUrl())
            .setWebApiUrlPrefix(m_authenticationInfo->webApiUrlPrefix())
            .setShardId(m_authenticationInfo->shardId())
            .build();

    setupAuthenticationInfoPartPersistence(
        m_authenticationInfo, account, m_host);

    const auto originalAuthTokenExpirationTime =
        m_authenticationInfo->m_authTokenExpirationTime;

    m_authenticationInfo->m_authTokenExpirationTime =
        QDateTime::currentMSecsSinceEpoch() + 100000;

    setupLinkedNotebookAuthenticationInfoPartPersistence(
        m_authenticationInfo, account, m_host, linkedNotebook.guid().value());

    m_authenticationInfo->m_authTokenExpirationTime =
        originalAuthTokenExpirationTime;

    InSequence s;

    // NOTE: preventing the leak of m_mockNoteStore below and corresponding
    // warning from gtest
    EXPECT_CALL(
        *m_mockNoteStoreFactory,
        noteStore(
            linkedNotebook.noteStoreUrl().value(), linkedNotebook.guid(),
            requestContext, retryPolicy))
        .WillOnce(
            [noteStoreWeak =
                 std::weak_ptr<mocks::qevercloud::MockINoteStore>{
                     m_mockNoteStore}](
                const QString & noteStoreUrl,
                const std::optional<qevercloud::Guid> & linkedNotebookGuid,
                const qevercloud::IRequestContextPtr & ctx,
                const qevercloud::IRetryPolicyPtr & retryPolicy) {
                Q_UNUSED(noteStoreUrl)
                Q_UNUSED(linkedNotebookGuid)
                Q_UNUSED(ctx)
                Q_UNUSED(retryPolicy)
                return noteStoreWeak.lock();
            });

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

    EXPECT_CALL(
        *m_mockNoteStore,
        authenticateToSharedNotebookAsync(
            linkedNotebook.sharedNotebookGlobalId().value(),
            Ne(requestContext)))
        .WillOnce([this](
                      [[maybe_unused]] const QString & shareKeyOrGlobalId,
                      const qevercloud::IRequestContextPtr & ctx) {
            EXPECT_TRUE(ctx);
            if (ctx) {
                EXPECT_EQ(
                    ctx->authenticationToken(),
                    m_authenticationInfo->authToken());
            }

            return threading::makeReadyFuture<qevercloud::AuthenticationResult>(
                qevercloud::AuthenticationResultBuilder{}
                    .setAuthenticationToken(m_authenticationInfo->authToken())
                    .setExpiration(
                        m_authenticationInfo->authTokenExpirationTime())
                    .setCurrentTime(m_authenticationInfo->authenticationTime())
                    .setUrls(qevercloud::UserUrlsBuilder{}
                                 .setNoteStoreUrl(
                                     m_authenticationInfo->noteStoreUrl())
                                 .setWebApiUrlPrefix(
                                     m_authenticationInfo->webApiUrlPrefix())
                                 .build())
                    .build());
        });

    EXPECT_CALL(*m_mockKeychainService, writePassword)
        .WillOnce([&](const QString & service, const QString & key, // NOLINT
                      const QString & password) {
            EXPECT_EQ(
                service,
                appName + QStringLiteral("_linked_notebook_auth_token"));

            EXPECT_EQ(
                key,
                appName + QStringLiteral("_linked_notebook_auth_token_") +
                    m_host + QStringLiteral("_") +
                    QString::number(m_authenticationInfo->userId()) +
                    QStringLiteral("_") + linkedNotebook.guid().value());

            EXPECT_EQ(password, m_authenticationInfo->authToken());

            return threading::makeReadyFuture();
        });

    auto future = authenticationInfoProvider->authenticateToLinkedNotebook(
        account, linkedNotebook, AuthenticationInfoProvider::Mode::Cache);

    waitForFuture(future);
    ASSERT_EQ(future.resultCount(), 1);

    EXPECT_NE(future.result().get(), m_authenticationInfo.get());

    const auto * authenticationInfo =
        dynamic_cast<const AuthenticationInfo *>(future.result().get());
    ASSERT_TRUE(authenticationInfo);

    m_authenticationInfo->m_userStoreCookies.clear();
    EXPECT_EQ(*authenticationInfo, *m_authenticationInfo);

    checkLinkedNotebookAuthenticationInfoPartPersistence(
        m_authenticationInfo, account, m_host, linkedNotebook.guid().value());
}

enum class ClearUserCacheOption
{
    User,
    AllUser,
    All
};

class AuthenticationInfoProviderUserCacheTest :
    public AuthenticationInfoProviderTest,
    public testing::WithParamInterface<ClearUserCacheOption>
{};

const std::array gClearUserCacheOptions{
    ClearUserCacheOption::User,
    ClearUserCacheOption::AllUser,
    ClearUserCacheOption::All,
};

INSTANTIATE_TEST_SUITE_P(
    AuthenticationInfoProviderUserCacheTestInstance,
    AuthenticationInfoProviderUserCacheTest,
    testing::ValuesIn(gClearUserCacheOptions));

TEST_P(
    AuthenticationInfoProviderUserCacheTest,
    AuthenticateAccountAfterCacheRemoval)
{
    const auto authenticationInfoProvider =
        std::make_shared<AuthenticationInfoProvider>(
            m_mockAuthenticator, m_mockKeychainService, m_mockUserInfoProvider,
            m_mockNoteStoreFactory, qevercloud::newRequestContext(),
            qevercloud::nullRetryPolicy(), m_host);

    const Account account{
        QStringLiteral("username"),
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

    waitForFuture(future);
    ASSERT_EQ(future.resultCount(), 1);

    EXPECT_NE(future.result().get(), m_authenticationInfo.get());
    EXPECT_EQ(
        dynamic_cast<const AuthenticationInfo &>(*future.result()),
        *m_authenticationInfo);

    checkAuthenticationInfoPartPersistence(
        m_authenticationInfo, account, m_host);

    EXPECT_CALL(*m_mockKeychainService, deletePassword)
        .WillOnce([&](const QString & service, const QString & key) {
            EXPECT_EQ(service, appName + QStringLiteral("_auth_token"));

            EXPECT_EQ(
                key,
                appName + QStringLiteral("_auth_token_") + m_host +
                    QStringLiteral("_") +
                    QString::number(m_authenticationInfo->userId()));

            return threading::makeReadyFuture();
        });

    EXPECT_CALL(*m_mockKeychainService, deletePassword)
        .WillOnce([&](const QString & service, const QString & key) {
            EXPECT_EQ(service, appName + QStringLiteral("_shard_id"));

            EXPECT_EQ(
                key,
                appName + QStringLiteral("_shard_id_") + m_host +
                    QStringLiteral("_") +
                    QString::number(m_authenticationInfo->userId()));

            return threading::makeReadyFuture();
        });

    const auto clearCacheOptions = [&] {
        switch (GetParam()) {
        case ClearUserCacheOption::User:
            return IAuthenticationInfoProvider::ClearCacheOptions{
                IAuthenticationInfoProvider::ClearCacheOption::User{
                    account.id()}};
        case ClearUserCacheOption::AllUser:
            return IAuthenticationInfoProvider::ClearCacheOptions{
                IAuthenticationInfoProvider::ClearCacheOption::AllUsers{}};
        case ClearUserCacheOption::All:
            return IAuthenticationInfoProvider::ClearCacheOptions{
                IAuthenticationInfoProvider::ClearCacheOption::All{}};
        }

        UNREACHABLE;
    }();

    authenticationInfoProvider->clearCaches(clearCacheOptions);

    checkNoAuthenticationInfoPartPersistence(
        m_authenticationInfo, account, m_host);

    // The second attempt will now call authenticator as the token and shard
    // id for this account would are no longer cached inside
    // AuthenticationInfoProvider

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

    future = authenticationInfoProvider->authenticateAccount(
        account, AuthenticationInfoProvider::Mode::Cache);

    waitForFuture(future);
    ASSERT_EQ(future.resultCount(), 1);

    EXPECT_EQ(future.result().get(), m_authenticationInfo.get());

    checkAuthenticationInfoPartPersistence(
        m_authenticationInfo, account, m_host);
}

enum class ClearLinkedNotebookCacheOption
{
    LinkedNotebook,
    AllLinkedNotebooks,
    All
};

class AuthenticationInfoProviderLinkedNotebookCacheTest :
    public AuthenticationInfoProviderTest,
    public testing::WithParamInterface<ClearLinkedNotebookCacheOption>
{};

const std::array gClearLinkedNotebookCacheOptions{
    ClearLinkedNotebookCacheOption::LinkedNotebook,
    ClearLinkedNotebookCacheOption::AllLinkedNotebooks,
    ClearLinkedNotebookCacheOption::All,
};

INSTANTIATE_TEST_SUITE_P(
    AuthenticationInfoProviderLinkedNotebookCacheTestInstance,
    AuthenticationInfoProviderLinkedNotebookCacheTest,
    testing::ValuesIn(gClearLinkedNotebookCacheOptions));

TEST_P(
    AuthenticationInfoProviderLinkedNotebookCacheTest,
    AuthenticateToLinkedNotebookAfterCacheRemoval)
{
    const auto authenticationInfoProvider =
        std::make_shared<AuthenticationInfoProvider>(
            m_mockAuthenticator, m_mockKeychainService, m_mockUserInfoProvider,
            m_mockNoteStoreFactory, qevercloud::newRequestContext(),
            qevercloud::nullRetryPolicy(), m_host);

    const Account account{
        QStringLiteral("username"),
        Account::Type::Evernote,
        m_authenticationInfo->userId(),
        Account::EvernoteAccountType::Free,
        m_host,
        m_authenticationInfo->shardId()};

    const qevercloud::LinkedNotebook linkedNotebook =
        qevercloud::LinkedNotebookBuilder{}
            .setGuid(UidGenerator::Generate())
            .setUsername(QStringLiteral("username"))
            .setSharedNotebookGlobalId(UidGenerator::Generate())
            .setNoteStoreUrl(m_authenticationInfo->noteStoreUrl())
            .setWebApiUrlPrefix(m_authenticationInfo->webApiUrlPrefix())
            .setShardId(m_authenticationInfo->shardId())
            .build();

    setupAuthenticationInfoPartPersistence(
        m_authenticationInfo, account, m_host);

    setupLinkedNotebookAuthenticationInfoPartPersistence(
        m_authenticationInfo, account, m_host, linkedNotebook.guid().value());

    InSequence s;

    static const QString appName = QCoreApplication::applicationName();

    EXPECT_CALL(*m_mockKeychainService, readPassword)
        .WillOnce([&](const QString & service, const QString & key) {
            EXPECT_EQ(
                service,
                appName + QStringLiteral("_linked_notebook_auth_token"));

            EXPECT_EQ(
                key,
                appName + QStringLiteral("_linked_notebook_auth_token_") +
                    m_host + QStringLiteral("_") +
                    QString::number(m_authenticationInfo->userId()) +
                    QStringLiteral("_") + linkedNotebook.guid().value());

            return threading::makeReadyFuture<QString>(
                m_authenticationInfo->authToken());
        });

    auto future = authenticationInfoProvider->authenticateToLinkedNotebook(
        account, linkedNotebook, AuthenticationInfoProvider::Mode::Cache);

    waitForFuture(future);
    ASSERT_EQ(future.resultCount(), 1);

    EXPECT_NE(future.result().get(), m_authenticationInfo.get());

    const auto * authenticationInfo =
        dynamic_cast<const AuthenticationInfo *>(future.result().get());
    ASSERT_TRUE(authenticationInfo);

    m_authenticationInfo->m_userStoreCookies.clear();
    EXPECT_EQ(*authenticationInfo, *m_authenticationInfo);

    checkLinkedNotebookAuthenticationInfoPartPersistence(
        m_authenticationInfo, account, m_host, linkedNotebook.guid().value());

    EXPECT_CALL(*m_mockKeychainService, deletePassword)
        .WillOnce([&](const QString & service, const QString & key) {
            EXPECT_EQ(
                service,
                appName + QStringLiteral("_linked_notebook_auth_token"));

            EXPECT_EQ(
                key,
                appName + QStringLiteral("_linked_notebook_auth_token_") +
                    m_host + QStringLiteral("_") +
                    QString::number(m_authenticationInfo->userId()) +
                    QStringLiteral("_") + linkedNotebook.guid().value());

            return threading::makeReadyFuture();
        });

    const auto clearCacheOptions = [&] {
        switch (GetParam()) {
        case ClearLinkedNotebookCacheOption::LinkedNotebook:
            return IAuthenticationInfoProvider::ClearCacheOptions{
                IAuthenticationInfoProvider::ClearCacheOption::LinkedNotebook{
                    linkedNotebook.guid().value()}};
        case ClearLinkedNotebookCacheOption::AllLinkedNotebooks:
            return IAuthenticationInfoProvider::ClearCacheOptions{
                IAuthenticationInfoProvider::ClearCacheOption::
                    AllLinkedNotebooks{}};
        case ClearLinkedNotebookCacheOption::All:
            return IAuthenticationInfoProvider::ClearCacheOptions{
                IAuthenticationInfoProvider::ClearCacheOption::All{}};
        }

        UNREACHABLE;
    }();

    authenticationInfoProvider->clearCaches(clearCacheOptions);

    checkNoLinkedNotebookAuthenticationInfoPartPersistence(
        m_authenticationInfo, account, m_host, linkedNotebook.guid().value());

    // The second attempt will now call authenticator as the token and shard
    // id for this linked notebook would are no longer cached inside
    // AuthenticationInfoProvider

    EXPECT_CALL(
        *m_mockNoteStoreFactory,
        noteStore(
            linkedNotebook.noteStoreUrl().value(), linkedNotebook.guid(), _, _))
        .WillOnce(
            [noteStoreWeak =
                 std::weak_ptr<mocks::qevercloud::MockINoteStore>{
                     m_mockNoteStore}](
                const QString & noteStoreUrl,
                const std::optional<qevercloud::Guid> & linkedNotebookGuid,
                const qevercloud::IRequestContextPtr & ctx,
                const qevercloud::IRetryPolicyPtr & retryPolicy) {
                Q_UNUSED(noteStoreUrl)
                Q_UNUSED(linkedNotebookGuid)
                Q_UNUSED(ctx)
                Q_UNUSED(retryPolicy)
                return noteStoreWeak.lock();
            });

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

    EXPECT_CALL(
        *m_mockNoteStore,
        authenticateToSharedNotebookAsync(
            linkedNotebook.sharedNotebookGlobalId().value(), _))
        .WillOnce([this](
                      [[maybe_unused]] const QString & shareKeyOrGlobalId,
                      const qevercloud::IRequestContextPtr & ctx) {
            EXPECT_TRUE(ctx);
            if (ctx) {
                EXPECT_EQ(
                    ctx->authenticationToken(),
                    m_authenticationInfo->authToken());
            }

            return threading::makeReadyFuture<qevercloud::AuthenticationResult>(
                qevercloud::AuthenticationResultBuilder{}
                    .setAuthenticationToken(m_authenticationInfo->authToken())
                    .setExpiration(
                        m_authenticationInfo->authTokenExpirationTime())
                    .setCurrentTime(m_authenticationInfo->authenticationTime())
                    .setUrls(qevercloud::UserUrlsBuilder{}
                                 .setNoteStoreUrl(
                                     m_authenticationInfo->noteStoreUrl())
                                 .setWebApiUrlPrefix(
                                     m_authenticationInfo->webApiUrlPrefix())
                                 .build())
                    .build());
        });

    EXPECT_CALL(*m_mockKeychainService, writePassword)
        .WillOnce([&](const QString & service, const QString & key, // NOLINT
                      const QString & password) {
            EXPECT_EQ(
                service,
                appName + QStringLiteral("_linked_notebook_auth_token"));

            EXPECT_EQ(
                key,
                appName + QStringLiteral("_linked_notebook_auth_token_") +
                    m_host + QStringLiteral("_") +
                    QString::number(m_authenticationInfo->userId()) +
                    QStringLiteral("_") + linkedNotebook.guid().value());

            EXPECT_EQ(password, m_authenticationInfo->authToken());

            return threading::makeReadyFuture();
        });

    future = authenticationInfoProvider->authenticateToLinkedNotebook(
        account, linkedNotebook, AuthenticationInfoProvider::Mode::Cache);

    waitForFuture(future);
    ASSERT_EQ(future.resultCount(), 1);

    EXPECT_NE(future.result().get(), m_authenticationInfo.get());

    authenticationInfo =
        dynamic_cast<const AuthenticationInfo *>(future.result().get());
    ASSERT_TRUE(authenticationInfo);

    m_authenticationInfo->m_userStoreCookies.clear();
    EXPECT_EQ(*authenticationInfo, *m_authenticationInfo);

    checkLinkedNotebookAuthenticationInfoPartPersistence(
        m_authenticationInfo, account, m_host, linkedNotebook.guid().value());
}

} // namespace quentier::synchronization::tests
