/*
 * Copyright 2022-2023 Dmitry Ivanov
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

#include <synchronization/ProtocolVersionChecker.h>
#include <synchronization/tests/mocks/qevercloud/services/MockIUserStore.h>
#include <synchronization/types/AuthenticationInfo.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/RuntimeError.h>
#include <quentier/threading/Future.h>
#include <quentier/utility/SysInfo.h>

#include <qevercloud/IRequestContext.h>

#include <QCoreApplication>

#include <gtest/gtest.h>

// clazy:excludeall=non-pod-global-static
// clazy:excludeall=returning-void-expression

namespace quentier::synchronization::tests {

using testing::Return;
using testing::StrictMock;

class ProtocolVersionCheckerTest : public testing::Test
{
protected:
    const std::shared_ptr<mocks::qevercloud::MockIUserStore> m_mockUserStore =
        std::make_shared<StrictMock<mocks::qevercloud::MockIUserStore>>();
};

TEST_F(ProtocolVersionCheckerTest, Ctor)
{
    EXPECT_NO_THROW(ProtocolVersionChecker{m_mockUserStore});
}

TEST_F(ProtocolVersionCheckerTest, CtorNullUserStore)
{
    EXPECT_THROW(ProtocolVersionChecker{nullptr}, InvalidArgument);
}

TEST_F(ProtocolVersionCheckerTest, CheckProtocolVersionSuccess)
{
    ProtocolVersionChecker checker{m_mockUserStore};

    AuthenticationInfo authenticationInfo;
    authenticationInfo.m_authToken = QStringLiteral("authToken");
    authenticationInfo.m_userStoreCookies = QList<QNetworkCookie>{}
        << QNetworkCookie{
               QStringLiteral("webCookiePreUserGuid").toUtf8(),
               QStringLiteral("value").toUtf8()};

    EXPECT_CALL(*m_mockUserStore, checkVersionAsync)
        .WillOnce([&](const QString & clientName, qint16 versionMajor,
                      qint16 versionMinor,
                      const qevercloud::IRequestContextPtr & ctx) {
            SysInfo sysInfo;

            EXPECT_EQ(
                clientName,
                QCoreApplication::applicationName() + QStringLiteral("/") +
                    QCoreApplication::applicationVersion() +
                    QStringLiteral("; ") + sysInfo.platformName());

            EXPECT_EQ(versionMajor, qevercloud::EDAM_VERSION_MAJOR);
            EXPECT_EQ(versionMinor, qevercloud::EDAM_VERSION_MINOR);
            EXPECT_EQ(
                ctx->authenticationToken(), authenticationInfo.authToken());
            EXPECT_EQ(ctx->cookies(), authenticationInfo.userStoreCookies());

            return threading::makeReadyFuture<bool>(true);
        });

    auto future = checker.checkProtocolVersion(authenticationInfo);
    waitForFuture(future);

    EXPECT_NO_THROW(future.waitForFinished());
}

TEST_F(ProtocolVersionCheckerTest, CheckProtocolVersionImplicitFailure)
{
    ProtocolVersionChecker checker{m_mockUserStore};

    AuthenticationInfo authenticationInfo;
    authenticationInfo.m_authToken = QStringLiteral("authToken");
    authenticationInfo.m_userStoreCookies = QList<QNetworkCookie>{}
        << QNetworkCookie{
               QStringLiteral("webCookiePreUserGuid").toUtf8(),
               QStringLiteral("value").toUtf8()};

    const QString errorString = QStringLiteral("some error");

    EXPECT_CALL(*m_mockUserStore, checkVersionAsync)
        .WillOnce([&](const QString & clientName, qint16 versionMajor,
                      qint16 versionMinor,
                      const qevercloud::IRequestContextPtr & ctx) {
            SysInfo sysInfo;

            EXPECT_EQ(
                clientName,
                QCoreApplication::applicationName() + QStringLiteral("/") +
                    QCoreApplication::applicationVersion() +
                    QStringLiteral("; ") + sysInfo.platformName());

            EXPECT_EQ(versionMajor, qevercloud::EDAM_VERSION_MAJOR);
            EXPECT_EQ(versionMinor, qevercloud::EDAM_VERSION_MINOR);
            EXPECT_EQ(
                ctx->authenticationToken(), authenticationInfo.authToken());
            EXPECT_EQ(ctx->cookies(), authenticationInfo.userStoreCookies());

            return threading::makeExceptionalFuture<bool>(
                RuntimeError{ErrorString{errorString}});
        });

    auto future = checker.checkProtocolVersion(authenticationInfo);
    waitForFuture(future);

    bool caughtException = false;
    try {
        future.waitForFinished();
    }
    catch (const IQuentierException & e) {
        EXPECT_EQ(e.errorMessage().nonLocalizedString(), errorString);
        caughtException = true;
    }

    EXPECT_TRUE(caughtException);
}

TEST_F(ProtocolVersionCheckerTest, CheckProtocolVersionExplicitFailure)
{
    ProtocolVersionChecker checker{m_mockUserStore};

    AuthenticationInfo authenticationInfo;
    authenticationInfo.m_authToken = QStringLiteral("authToken");
    authenticationInfo.m_userStoreCookies = QList<QNetworkCookie>{}
        << QNetworkCookie{
               QStringLiteral("webCookiePreUserGuid").toUtf8(),
               QStringLiteral("value").toUtf8()};

    EXPECT_CALL(*m_mockUserStore, checkVersionAsync)
        .WillOnce([&](const QString & clientName, qint16 versionMajor,
                      qint16 versionMinor,
                      const qevercloud::IRequestContextPtr & ctx) {
            SysInfo sysInfo;

            EXPECT_EQ(
                clientName,
                QCoreApplication::applicationName() + QStringLiteral("/") +
                    QCoreApplication::applicationVersion() +
                    QStringLiteral("; ") + sysInfo.platformName());

            EXPECT_EQ(versionMajor, qevercloud::EDAM_VERSION_MAJOR);
            EXPECT_EQ(versionMinor, qevercloud::EDAM_VERSION_MINOR);
            EXPECT_EQ(
                ctx->authenticationToken(), authenticationInfo.authToken());
            EXPECT_EQ(ctx->cookies(), authenticationInfo.userStoreCookies());

            return threading::makeReadyFuture<bool>(false);
        });

    auto future = checker.checkProtocolVersion(authenticationInfo);
    waitForFuture(future);

    bool caughtException = false;
    try {
        future.waitForFinished();
    }
    catch ([[maybe_unused]] const IQuentierException & e) {
        caughtException = true;
    }

    EXPECT_TRUE(caughtException);
}

} // namespace quentier::synchronization::tests
