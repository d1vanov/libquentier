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

#include <utility/keychain/CompositeKeychainService.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/threading/Future.h>
#include <quentier/utility/tests/mocks/MockIKeychainService.h>

#include <gtest/gtest.h>

#include <memory>

// clazy:excludeall=non-pod-global-static
// clazy:excludeall=returning-void-expression

namespace quentier::utility::tests {

using testing::Return;
using testing::StrictMock;

class CompositeKeychainServiceTest : public testing::Test
{
protected:
    void SetUp() override
    {
        m_logLevel = QuentierMinLogLevel();
        QuentierSetMinLogLevel(LogLevel::Error);
    }

    void TearDown() override
    {
        QuentierSetMinLogLevel(m_logLevel);
    }

protected:
    const QString m_name = QStringLiteral("test_composite_keychain");

    const std::shared_ptr<mocks::MockIKeychainService> m_mockPrimaryKeychain =
        std::make_shared<StrictMock<mocks::MockIKeychainService>>();

    const std::shared_ptr<mocks::MockIKeychainService> m_mockSecondaryKeychain =
        std::make_shared<StrictMock<mocks::MockIKeychainService>>();

    LogLevel m_logLevel = LogLevel::Info;
};

TEST_F(CompositeKeychainServiceTest, Ctor)
{
    EXPECT_NO_THROW(
        const auto compositeKeychainService =
            std::make_shared<CompositeKeychainService>(
                m_name, m_mockPrimaryKeychain, m_mockSecondaryKeychain));
}

TEST_F(CompositeKeychainServiceTest, CtorEmptyName)
{
    EXPECT_THROW(
        const auto compositeKeychainService =
            std::make_shared<CompositeKeychainService>(
                QString{}, m_mockPrimaryKeychain, m_mockSecondaryKeychain),
        InvalidArgument);
}

TEST_F(CompositeKeychainServiceTest, CtorNullPrimaryKeychain)
{
    EXPECT_THROW(
        const auto compositeKeychainService =
            std::make_shared<CompositeKeychainService>(
                m_name, nullptr, m_mockSecondaryKeychain),
        InvalidArgument);
}

TEST_F(CompositeKeychainServiceTest, CtorNullSecondaryKeychain)
{
    EXPECT_THROW(
        const auto compositeKeychainService =
            std::make_shared<CompositeKeychainService>(
                m_name, m_mockPrimaryKeychain, nullptr),
        InvalidArgument);
}

TEST_F(CompositeKeychainServiceTest, WritePasswordToBothKeychains)
{
    const auto compositeKeychainService =
        std::make_shared<CompositeKeychainService>(
            m_name, m_mockPrimaryKeychain, m_mockSecondaryKeychain);

    const QString service = QStringLiteral("service");
    const QString key = QStringLiteral("key");
    const QString password = QStringLiteral("password");

    EXPECT_CALL(*m_mockPrimaryKeychain, writePassword(service, key, password))
        .WillOnce(Return(threading::makeReadyFuture()));

    EXPECT_CALL(*m_mockSecondaryKeychain, writePassword(service, key, password))
        .WillOnce(Return(threading::makeReadyFuture()));

    auto future =
        compositeKeychainService->writePassword(service, key, password);
    ASSERT_TRUE(future.isFinished());
    EXPECT_NO_THROW(future.waitForFinished());
}

TEST_F(
    CompositeKeychainServiceTest, HandleFailureToWritePasswordToPrimaryKeychain)
{
    const auto compositeKeychainService =
        std::make_shared<CompositeKeychainService>(
            m_name, m_mockPrimaryKeychain, m_mockSecondaryKeychain);

    const QString service = QStringLiteral("service");
    const QString key = QStringLiteral("key");
    const QString password = QStringLiteral("password");

    EXPECT_CALL(*m_mockPrimaryKeychain, writePassword(service, key, password))
        .WillOnce(Return(
            threading::makeExceptionalFuture<void>(IKeychainService::Exception{
                IKeychainService::ErrorCode::AccessDenied})));

    EXPECT_CALL(*m_mockSecondaryKeychain, writePassword(service, key, password))
        .WillOnce(Return(threading::makeReadyFuture()));

    auto writePasswordFuture =
        compositeKeychainService->writePassword(service, key, password);
    ASSERT_TRUE(writePasswordFuture.isFinished());
    EXPECT_NO_THROW(writePasswordFuture.waitForFinished());

    // The following attempt to read password should go to the secondary
    // keychain only
    EXPECT_CALL(*m_mockSecondaryKeychain, readPassword(service, key))
        .WillOnce(
            Return(threading::makeReadyFuture<QString>(QString{password})));

    auto readPasswordFuture =
        compositeKeychainService->readPassword(service, key);
    ASSERT_TRUE(readPasswordFuture.isFinished());
    ASSERT_NO_THROW(readPasswordFuture.waitForFinished());
    ASSERT_EQ(readPasswordFuture.resultCount(), 1);
    EXPECT_EQ(readPasswordFuture.result(), password);
}

TEST_F(
    CompositeKeychainServiceTest,
    HandleFailureToWritePasswordToSecondaryKeychain)
{
    const auto compositeKeychainService =
        std::make_shared<CompositeKeychainService>(
            m_name, m_mockPrimaryKeychain, m_mockSecondaryKeychain);

    const QString service = QStringLiteral("service");
    const QString key = QStringLiteral("key");
    const QString password = QStringLiteral("password");

    EXPECT_CALL(*m_mockPrimaryKeychain, writePassword(service, key, password))
        .WillOnce(Return(threading::makeReadyFuture()));

    EXPECT_CALL(*m_mockSecondaryKeychain, writePassword(service, key, password))
        .WillOnce(Return(
            threading::makeExceptionalFuture<void>(IKeychainService::Exception{
                IKeychainService::ErrorCode::AccessDenied})));

    auto writePasswordFuture =
        compositeKeychainService->writePassword(service, key, password);
    ASSERT_TRUE(writePasswordFuture.isFinished());
    EXPECT_NO_THROW(writePasswordFuture.waitForFinished());

    // The following attempt to read password should go to the primary keychain
    // only
    EXPECT_CALL(*m_mockPrimaryKeychain, readPassword(service, key))
        .WillOnce(Return(threading::makeExceptionalFuture<QString>(
            IKeychainService::Exception{
                IKeychainService::ErrorCode::NoBackendAvailable})));

    auto readPasswordFuture =
        compositeKeychainService->readPassword(service, key);
    ASSERT_TRUE(readPasswordFuture.isFinished());
    EXPECT_THROW(
        readPasswordFuture.waitForFinished(), IKeychainService::Exception);
}

TEST_F(
    CompositeKeychainServiceTest, HandleFailureToWritePasswordToBothKeychains)
{
    const auto compositeKeychainService =
        std::make_shared<CompositeKeychainService>(
            m_name, m_mockPrimaryKeychain, m_mockSecondaryKeychain);

    const QString service = QStringLiteral("service");
    const QString key = QStringLiteral("key");
    const QString password = QStringLiteral("password");

    EXPECT_CALL(*m_mockPrimaryKeychain, writePassword(service, key, password))
        .WillOnce(Return(
            threading::makeExceptionalFuture<void>(IKeychainService::Exception{
                IKeychainService::ErrorCode::AccessDenied})));

    EXPECT_CALL(*m_mockSecondaryKeychain, writePassword(service, key, password))
        .WillOnce(Return(
            threading::makeExceptionalFuture<void>(IKeychainService::Exception{
                IKeychainService::ErrorCode::NoBackendAvailable})));

    auto writePasswordFuture =
        compositeKeychainService->writePassword(service, key, password);
    ASSERT_TRUE(writePasswordFuture.isFinished());
    EXPECT_THROW(
        writePasswordFuture.waitForFinished(), IKeychainService::Exception);

    // The following attempt to read password should not go to either primary or
    // secondary keychain
    auto readPasswordFuture =
        compositeKeychainService->readPassword(service, key);
    ASSERT_TRUE(readPasswordFuture.isFinished());

    bool caughtException = false;
    try {
        readPasswordFuture.waitForFinished();
    }
    catch (const IKeychainService::Exception & e) {
        caughtException = true;
        EXPECT_EQ(e.errorCode(), IKeychainService::ErrorCode::EntryNotFound);
    }

    EXPECT_TRUE(caughtException);
}

TEST_F(CompositeKeychainServiceTest, ReadPasswordFromPrimaryKeychainFirst)
{
    const auto compositeKeychainService =
        std::make_shared<CompositeKeychainService>(
            m_name, m_mockPrimaryKeychain, m_mockSecondaryKeychain);

    const QString service = QStringLiteral("service");
    const QString key = QStringLiteral("key");
    const QString password = QStringLiteral("password");

    EXPECT_CALL(*m_mockPrimaryKeychain, readPassword(service, key))
        .WillOnce(
            Return(threading::makeReadyFuture<QString>(QString{password})));

    auto readPasswordFuture =
        compositeKeychainService->readPassword(service, key);
    ASSERT_TRUE(readPasswordFuture.isFinished());
    ASSERT_NO_THROW(readPasswordFuture.waitForFinished());
    ASSERT_EQ(readPasswordFuture.resultCount(), 1);
    EXPECT_EQ(readPasswordFuture.result(), password);
}

TEST_F(
    CompositeKeychainServiceTest, ReadPasswordFromSecondaryKeychainAsFallback)
{
    const auto compositeKeychainService =
        std::make_shared<CompositeKeychainService>(
            m_name, m_mockPrimaryKeychain, m_mockSecondaryKeychain);

    const QString service = QStringLiteral("service");
    const QString key = QStringLiteral("key");
    const QString password = QStringLiteral("password");

    EXPECT_CALL(*m_mockPrimaryKeychain, readPassword(service, key))
        .WillOnce(Return(threading::makeExceptionalFuture<QString>(
            IKeychainService::Exception{
                IKeychainService::ErrorCode::NoBackendAvailable})));

    EXPECT_CALL(*m_mockSecondaryKeychain, readPassword(service, key))
        .WillOnce(
            Return(threading::makeReadyFuture<QString>(QString{password})));

    auto readPasswordFuture =
        compositeKeychainService->readPassword(service, key);
    ASSERT_TRUE(readPasswordFuture.isFinished());
    ASSERT_NO_THROW(readPasswordFuture.waitForFinished());
    ASSERT_EQ(readPasswordFuture.resultCount(), 1);
    EXPECT_EQ(readPasswordFuture.result(), password);
}

TEST_F(
    CompositeKeychainServiceTest,
    DontReadPasswordFromEitherKeychainIfWritingToBothFails)
{
    const auto compositeKeychainService =
        std::make_shared<CompositeKeychainService>(
            m_name, m_mockPrimaryKeychain, m_mockSecondaryKeychain);

    const QString service = QStringLiteral("service");
    const QString key = QStringLiteral("key");
    const QString password = QStringLiteral("password");

    EXPECT_CALL(*m_mockPrimaryKeychain, writePassword(service, key, password))
        .WillOnce(Return(
            threading::makeExceptionalFuture<void>(IKeychainService::Exception{
                IKeychainService::ErrorCode::NoBackendAvailable})));

    EXPECT_CALL(*m_mockSecondaryKeychain, writePassword(service, key, password))
        .WillOnce(Return(
            threading::makeExceptionalFuture<void>(IKeychainService::Exception{
                IKeychainService::ErrorCode::AccessDenied})));

    auto writePasswordFuture =
        compositeKeychainService->writePassword(service, key, password);
    ASSERT_TRUE(writePasswordFuture.isFinished());
    EXPECT_THROW(
        writePasswordFuture.waitForFinished(), IKeychainService::Exception);

    auto readPasswordFuture =
        compositeKeychainService->readPassword(service, key);
    ASSERT_TRUE(readPasswordFuture.isFinished());

    bool caughtException = false;
    try {
        readPasswordFuture.waitForFinished();
    }
    catch (const IKeychainService::Exception & e) {
        caughtException = true;
        EXPECT_EQ(e.errorCode(), IKeychainService::ErrorCode::EntryNotFound);
    }

    EXPECT_TRUE(caughtException);
}

TEST_F(CompositeKeychainServiceTest, DeletePasswordFromBothKeychains)
{
    const auto compositeKeychainService =
        std::make_shared<CompositeKeychainService>(
            m_name, m_mockPrimaryKeychain, m_mockSecondaryKeychain);

    const QString service = QStringLiteral("service");
    const QString key = QStringLiteral("key");

    EXPECT_CALL(*m_mockPrimaryKeychain, deletePassword(service, key))
        .WillOnce(Return(threading::makeReadyFuture()));

    EXPECT_CALL(*m_mockSecondaryKeychain, deletePassword(service, key))
        .WillOnce(Return(threading::makeReadyFuture()));

    auto deletePasswordFuture =
        compositeKeychainService->deletePassword(service, key);
    ASSERT_TRUE(deletePasswordFuture.isFinished());
    EXPECT_NO_THROW(deletePasswordFuture.waitForFinished());
}

TEST_F(
    CompositeKeychainServiceTest,
    HandleFailureToDeletePasswordFromPrimaryKeychain)
{
    const auto compositeKeychainService =
        std::make_shared<CompositeKeychainService>(
            m_name, m_mockPrimaryKeychain, m_mockSecondaryKeychain);

    const QString service = QStringLiteral("service");
    const QString key = QStringLiteral("key");

    EXPECT_CALL(*m_mockPrimaryKeychain, deletePassword(service, key))
        .WillOnce(Return(
            threading::makeExceptionalFuture<void>(IKeychainService::Exception{
                IKeychainService::ErrorCode::NoBackendAvailable})));

    EXPECT_CALL(*m_mockSecondaryKeychain, deletePassword(service, key))
        .WillOnce(Return(threading::makeReadyFuture()));

    auto deletePasswordFuture =
        compositeKeychainService->deletePassword(service, key);
    ASSERT_TRUE(deletePasswordFuture.isFinished());
    EXPECT_NO_THROW(deletePasswordFuture.waitForFinished());

    // The following attempt to read password should go to the secondary
    // keychain only
    EXPECT_CALL(*m_mockSecondaryKeychain, readPassword(service, key))
        .WillOnce(Return(threading::makeExceptionalFuture<QString>(
            IKeychainService::Exception{
                IKeychainService::ErrorCode::EntryNotFound})));

    auto readPasswordFuture =
        compositeKeychainService->readPassword(service, key);
    ASSERT_TRUE(readPasswordFuture.isFinished());

    bool caughtException = false;
    try {
        readPasswordFuture.waitForFinished();
    }
    catch (const IKeychainService::Exception & e) {
        caughtException = true;
        EXPECT_EQ(e.errorCode(), IKeychainService::ErrorCode::EntryNotFound);
    }

    EXPECT_TRUE(caughtException);
}

TEST_F(
    CompositeKeychainServiceTest,
    HandleFailureToDeletePasswordFromSecondaryKeychain)
{
    const auto compositeKeychainService =
        std::make_shared<CompositeKeychainService>(
            m_name, m_mockPrimaryKeychain, m_mockSecondaryKeychain);

    const QString service = QStringLiteral("service");
    const QString key = QStringLiteral("key");

    EXPECT_CALL(*m_mockPrimaryKeychain, deletePassword(service, key))
        .WillOnce(Return(threading::makeReadyFuture()));

    EXPECT_CALL(*m_mockSecondaryKeychain, deletePassword(service, key))
        .WillOnce(Return(
            threading::makeExceptionalFuture<void>(IKeychainService::Exception{
                IKeychainService::ErrorCode::NoBackendAvailable})));

    auto deletePasswordFuture =
        compositeKeychainService->deletePassword(service, key);
    ASSERT_TRUE(deletePasswordFuture.isFinished());
    EXPECT_NO_THROW(deletePasswordFuture.waitForFinished());

    // The following attempt to read password should go to the primary
    // keychain only
    EXPECT_CALL(*m_mockPrimaryKeychain, readPassword(service, key))
        .WillOnce(Return(threading::makeExceptionalFuture<QString>(
            IKeychainService::Exception{
                IKeychainService::ErrorCode::EntryNotFound})));

    auto readPasswordFuture =
        compositeKeychainService->readPassword(service, key);
    ASSERT_TRUE(readPasswordFuture.isFinished());

    bool caughtException = false;
    try {
        readPasswordFuture.waitForFinished();
    }
    catch (const IKeychainService::Exception & e) {
        caughtException = true;
        EXPECT_EQ(e.errorCode(), IKeychainService::ErrorCode::EntryNotFound);
    }

    EXPECT_TRUE(caughtException);
}

TEST_F(
    CompositeKeychainServiceTest,
    HandleFailureToDeletePasswordFromBothKeychains)
{
    const auto compositeKeychainService =
        std::make_shared<CompositeKeychainService>(
            m_name, m_mockPrimaryKeychain, m_mockSecondaryKeychain);

    const QString service = QStringLiteral("service");
    const QString key = QStringLiteral("key");

    EXPECT_CALL(*m_mockPrimaryKeychain, deletePassword(service, key))
        .WillOnce(Return(
            threading::makeExceptionalFuture<void>(IKeychainService::Exception{
                IKeychainService::ErrorCode::NoBackendAvailable})));

    EXPECT_CALL(*m_mockSecondaryKeychain, deletePassword(service, key))
        .WillOnce(Return(
            threading::makeExceptionalFuture<void>(IKeychainService::Exception{
                IKeychainService::ErrorCode::AccessDenied})));

    auto deletePasswordFuture =
        compositeKeychainService->deletePassword(service, key);
    ASSERT_TRUE(deletePasswordFuture.isFinished());
    EXPECT_NO_THROW(deletePasswordFuture.waitForFinished());

    // The following attempt to read password should not go to either primary or
    // secondary keychain
    auto readPasswordFuture =
        compositeKeychainService->readPassword(service, key);
    ASSERT_TRUE(readPasswordFuture.isFinished());

    bool caughtException = false;
    try {
        readPasswordFuture.waitForFinished();
    }
    catch (const IKeychainService::Exception & e) {
        caughtException = true;
        EXPECT_EQ(e.errorCode(), IKeychainService::ErrorCode::EntryNotFound);
    }

    EXPECT_TRUE(caughtException);
}

} // namespace quentier::utility::tests
