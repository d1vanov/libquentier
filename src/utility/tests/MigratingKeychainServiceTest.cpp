/*
 * Copyright 2022-2025 Dmitry Ivanov
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

#include <utility/keychain/MigratingKeychainService.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/threading/Future.h>
#include <quentier/utility/tests/mocks/MockIKeychainService.h>

#include <gtest/gtest.h>

#include <memory>

// clazy:excludeall=non-pod-global-static
// clazy:excludeall=returning-void-expression

namespace quentier::utility::keychain::tests {

using testing::Return;
using testing::StrictMock;

class MigratingKeychainServiceTest : public testing::Test
{
protected:
    const std::shared_ptr<utility::tests::mocks::MockIKeychainService>
        m_mockSourceKeychain = std::make_shared<
            StrictMock<utility::tests::mocks::MockIKeychainService>>();

    const std::shared_ptr<utility::tests::mocks::MockIKeychainService>
        m_mockSinkKeychain = std::make_shared<
            StrictMock<utility::tests::mocks::MockIKeychainService>>();
};

TEST_F(MigratingKeychainServiceTest, Ctor)
{
    EXPECT_NO_THROW(
        const auto migratingKeychainService =
            std::make_shared<MigratingKeychainService>(
                m_mockSourceKeychain, m_mockSinkKeychain));
}

TEST_F(MigratingKeychainServiceTest, CtorNullSourceKeychain)
{
    EXPECT_THROW(
        const auto migratingKeychainService =
            std::make_shared<MigratingKeychainService>(
                nullptr, m_mockSinkKeychain),
        InvalidArgument);
}

TEST_F(MigratingKeychainServiceTest, CtorNullSinkKeychain)
{
    EXPECT_THROW(
        const auto migratingKeychainService =
            std::make_shared<MigratingKeychainService>(
                m_mockSourceKeychain, nullptr),
        InvalidArgument);
}

TEST_F(MigratingKeychainServiceTest, WritePasswordOnlyToSinkKeychain)
{
    const auto migratingKeychainService =
        std::make_shared<MigratingKeychainService>(
            m_mockSourceKeychain, m_mockSinkKeychain);

    const QString service = QStringLiteral("service");
    const QString key = QStringLiteral("key");
    const QString password = QStringLiteral("password");

    EXPECT_CALL(*m_mockSinkKeychain, writePassword(service, key, password))
        .WillOnce(Return(threading::makeReadyFuture()));

    auto writeFuture =
        migratingKeychainService->writePassword(service, key, password);

    ASSERT_TRUE(writeFuture.isFinished());
}

TEST_F(MigratingKeychainServiceTest, ReadPasswordFromSinkKeychainFirst)
{
    const auto migratingKeychainService =
        std::make_shared<MigratingKeychainService>(
            m_mockSourceKeychain, m_mockSinkKeychain);

    const QString service = QStringLiteral("service");
    const QString key = QStringLiteral("key");
    const QString password = QStringLiteral("password");

    EXPECT_CALL(*m_mockSinkKeychain, readPassword(service, key))
        .WillOnce(
            Return(threading::makeReadyFuture<QString>(QString{password})));

    auto readPasswordFuture =
        migratingKeychainService->readPassword(service, key);
    ASSERT_TRUE(readPasswordFuture.isFinished());
    ASSERT_NO_THROW(readPasswordFuture.waitForFinished());
    ASSERT_EQ(readPasswordFuture.resultCount(), 1);
    EXPECT_EQ(readPasswordFuture.result(), password);
}

TEST_F(MigratingKeychainServiceTest, ReadPasswordFromSourceKeychainAsFallback)
{
    const auto migratingKeychainService =
        std::make_shared<MigratingKeychainService>(
            m_mockSourceKeychain, m_mockSinkKeychain);

    const QString service = QStringLiteral("service");
    const QString key = QStringLiteral("key");
    const QString password = QStringLiteral("password");

    EXPECT_CALL(*m_mockSinkKeychain, readPassword(service, key))
        .WillOnce(Return(threading::makeExceptionalFuture<QString>(
            IKeychainService::Exception{
                IKeychainService::ErrorCode::EntryNotFound})));

    EXPECT_CALL(*m_mockSourceKeychain, readPassword(service, key))
        .WillOnce(
            Return(threading::makeReadyFuture<QString>(QString{password})));

    EXPECT_CALL(*m_mockSinkKeychain, writePassword(service, key, password))
        .WillOnce(Return(threading::makeReadyFuture()));

    EXPECT_CALL(*m_mockSourceKeychain, deletePassword(service, key))
        .WillOnce(Return(threading::makeReadyFuture()));

    auto readPasswordFuture =
        migratingKeychainService->readPassword(service, key);
    ASSERT_TRUE(readPasswordFuture.isFinished());
    ASSERT_NO_THROW(readPasswordFuture.waitForFinished());
    ASSERT_EQ(readPasswordFuture.resultCount(), 1);
    EXPECT_EQ(readPasswordFuture.result(), password);
}

TEST_F(
    MigratingKeychainServiceTest,
    DontReadPasswordFromSourceKeychainIfReadingFromSinkKeychainFailsWithOtherReasonThanNotFound)
{
    const auto migratingKeychainService =
        std::make_shared<MigratingKeychainService>(
            m_mockSourceKeychain, m_mockSinkKeychain);

    const QString service = QStringLiteral("service");
    const QString key = QStringLiteral("key");

    EXPECT_CALL(*m_mockSinkKeychain, readPassword(service, key))
        .WillOnce(Return(threading::makeExceptionalFuture<QString>(
            IKeychainService::Exception{
                IKeychainService::ErrorCode::NoBackendAvailable})));

    auto readPasswordFuture =
        migratingKeychainService->readPassword(service, key);
    ASSERT_TRUE(readPasswordFuture.isFinished());

    bool caughtException = false;
    try {
        readPasswordFuture.waitForFinished();
    }
    catch (const IKeychainService::Exception & e) {
        caughtException = true;
        EXPECT_EQ(
            e.errorCode(), IKeychainService::ErrorCode::NoBackendAvailable);
    }

    EXPECT_TRUE(caughtException);
}

TEST_F(MigratingKeychainServiceTest, DeletePasswordFromBothKeychains)
{
    const auto migratingKeychainService =
        std::make_shared<MigratingKeychainService>(
            m_mockSourceKeychain, m_mockSinkKeychain);

    const QString service = QStringLiteral("service");
    const QString key = QStringLiteral("key");

    EXPECT_CALL(*m_mockSinkKeychain, deletePassword(service, key))
        .WillOnce(Return(threading::makeReadyFuture()));

    EXPECT_CALL(*m_mockSourceKeychain, deletePassword(service, key))
        .WillOnce(Return(threading::makeReadyFuture()));

    auto deleteFuture = migratingKeychainService->deletePassword(service, key);

    ASSERT_TRUE(deleteFuture.isFinished());
    EXPECT_NO_THROW(deleteFuture.waitForFinished());
}

TEST_F(
    MigratingKeychainServiceTest,
    HandleEntryNotFoundOnDeletePasswordFromSinkKeychain)
{
    const auto migratingKeychainService =
        std::make_shared<MigratingKeychainService>(
            m_mockSourceKeychain, m_mockSinkKeychain);

    const QString service = QStringLiteral("service");
    const QString key = QStringLiteral("key");

    EXPECT_CALL(*m_mockSinkKeychain, deletePassword(service, key))
        .WillOnce(Return(
            threading::makeExceptionalFuture<void>(IKeychainService::Exception{
                IKeychainService::ErrorCode::EntryNotFound})));

    EXPECT_CALL(*m_mockSourceKeychain, deletePassword(service, key))
        .WillOnce(Return(threading::makeReadyFuture()));

    auto deleteFuture = migratingKeychainService->deletePassword(service, key);

    ASSERT_TRUE(deleteFuture.isFinished());
    EXPECT_NO_THROW(deleteFuture.waitForFinished());
}

TEST_F(
    MigratingKeychainServiceTest,
    HandleEntryNotFoundOnDeletePasswordFromSourceKeychain)
{
    const auto migratingKeychainService =
        std::make_shared<MigratingKeychainService>(
            m_mockSourceKeychain, m_mockSinkKeychain);

    const QString service = QStringLiteral("service");
    const QString key = QStringLiteral("key");

    EXPECT_CALL(*m_mockSinkKeychain, deletePassword(service, key))
        .WillOnce(Return(threading::makeReadyFuture()));

    EXPECT_CALL(*m_mockSourceKeychain, deletePassword(service, key))
        .WillOnce(Return(
            threading::makeExceptionalFuture<void>(IKeychainService::Exception{
                IKeychainService::ErrorCode::EntryNotFound})));

    auto deleteFuture = migratingKeychainService->deletePassword(service, key);

    ASSERT_TRUE(deleteFuture.isFinished());
    EXPECT_NO_THROW(deleteFuture.waitForFinished());
}

TEST_F(
    MigratingKeychainServiceTest,
    HandleEntryNotFoundOnDeletePasswordFromBothKeychains)
{
    const auto migratingKeychainService =
        std::make_shared<MigratingKeychainService>(
            m_mockSourceKeychain, m_mockSinkKeychain);

    const QString service = QStringLiteral("service");
    const QString key = QStringLiteral("key");

    EXPECT_CALL(*m_mockSinkKeychain, deletePassword(service, key))
        .WillOnce(Return(
            threading::makeExceptionalFuture<void>(IKeychainService::Exception{
                IKeychainService::ErrorCode::EntryNotFound})));

    EXPECT_CALL(*m_mockSourceKeychain, deletePassword(service, key))
        .WillOnce(Return(
            threading::makeExceptionalFuture<void>(IKeychainService::Exception{
                IKeychainService::ErrorCode::EntryNotFound})));

    auto deleteFuture = migratingKeychainService->deletePassword(service, key);

    ASSERT_TRUE(deleteFuture.isFinished());
    EXPECT_NO_THROW(deleteFuture.waitForFinished());
}

TEST_F(
    MigratingKeychainServiceTest,
    PropagateErrorOnDeletePasswordFromSinkKeychain)
{
    const auto migratingKeychainService =
        std::make_shared<MigratingKeychainService>(
            m_mockSourceKeychain, m_mockSinkKeychain);

    const QString service = QStringLiteral("service");
    const QString key = QStringLiteral("key");

    EXPECT_CALL(*m_mockSinkKeychain, deletePassword(service, key))
        .WillOnce(Return(
            threading::makeExceptionalFuture<void>(IKeychainService::Exception{
                IKeychainService::ErrorCode::NoBackendAvailable})));

    EXPECT_CALL(*m_mockSourceKeychain, deletePassword(service, key))
        .WillOnce(Return(threading::makeReadyFuture()));

    auto deleteFuture = migratingKeychainService->deletePassword(service, key);

    ASSERT_TRUE(deleteFuture.isFinished());

    bool caughtException = false;
    try {
        deleteFuture.waitForFinished();
    }
    catch (const IKeychainService::Exception & e) {
        EXPECT_EQ(
            e.errorCode(), IKeychainService::ErrorCode::NoBackendAvailable);
        caughtException = true;
    }

    EXPECT_TRUE(caughtException);
}

TEST_F(
    MigratingKeychainServiceTest,
    PropagateErrorOnDeletePasswordFromSourceKeychain)
{
    const auto migratingKeychainService =
        std::make_shared<MigratingKeychainService>(
            m_mockSourceKeychain, m_mockSinkKeychain);

    const QString service = QStringLiteral("service");
    const QString key = QStringLiteral("key");

    EXPECT_CALL(*m_mockSinkKeychain, deletePassword(service, key))
        .WillOnce(Return(threading::makeReadyFuture()));

    EXPECT_CALL(*m_mockSourceKeychain, deletePassword(service, key))
        .WillOnce(Return(
            threading::makeExceptionalFuture<void>(IKeychainService::Exception{
                IKeychainService::ErrorCode::AccessDenied})));

    auto deleteFuture = migratingKeychainService->deletePassword(service, key);

    ASSERT_TRUE(deleteFuture.isFinished());

    bool caughtException = false;
    try {
        deleteFuture.waitForFinished();
    }
    catch (const IKeychainService::Exception & e) {
        EXPECT_EQ(e.errorCode(), IKeychainService::ErrorCode::AccessDenied);
        caughtException = true;
    }

    EXPECT_TRUE(caughtException);
}

} // namespace quentier::utility::tests
