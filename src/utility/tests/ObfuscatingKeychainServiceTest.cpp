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

#include <utility/keychain/ObfuscatingKeychainService.h>

#include <quentier/threading/Future.h>
#include <quentier/utility/ApplicationSettings.h>

#include <gtest/gtest.h>

#include <memory>

// clazy:excludeall=non-pod-global-static
// clazy:excludeall=returning-void-expression

namespace quentier::utility::tests {

class ObfuscatingKeychainServiceTest : public testing::Test
{
protected:
    void TearDown() override
    {
        ApplicationSettings settings{settingsName()};
        settings.beginGroup(settingsGroupName());
        settings.remove(QStringLiteral(""));
        settings.endGroup();
    }

    [[nodiscard]] QString settingsName() const
    {
        return QStringLiteral("obfuscatingKeychainStorage");
    }

    [[nodiscard]] QString settingsGroupName() const
    {
        return m_service + QStringLiteral("/") + m_key;
    }

protected:
    const QString m_service = QStringLiteral("service");
    const QString m_key = QStringLiteral("key");
    const QString m_password = QStringLiteral("password");
};

TEST_F(ObfuscatingKeychainServiceTest, Ctor)
{
    EXPECT_NO_THROW(ObfuscatingKeychainService());
}

TEST_F(ObfuscatingKeychainServiceTest, WritePassword)
{
    ObfuscatingKeychainService obfuscatingKeychainService;

    auto writeFuture = obfuscatingKeychainService.writePassword(
        m_service, m_key, m_password);

    ASSERT_TRUE(writeFuture.isFinished());

    ApplicationSettings settings{settingsName()};
    settings.beginGroup(settingsGroupName());

    QString cipher = settings.value("Cipher").toString();
    EXPECT_EQ(cipher, QStringLiteral("AES"));

    bool conversionResult = false;
    std::size_t keyLength =
        settings.value("KeyLength").toULongLong(&conversionResult);
    EXPECT_TRUE(conversionResult);
    EXPECT_EQ(keyLength, 128);

    QString value = settings.value("Value").toString();
    EXPECT_FALSE(value.isEmpty());
    EXPECT_NE(value, m_password);

    settings.endGroup();
}

TEST_F(ObfuscatingKeychainServiceTest, ReadNonexistentPassword)
{
    ObfuscatingKeychainService obfuscatingKeychainService;

    auto readFuture = obfuscatingKeychainService.readPassword(m_service, m_key);
    ASSERT_TRUE(readFuture.isFinished());

    bool caughtException = false;
    try {
        readFuture.waitForFinished();
    }
    catch (const IKeychainService::Exception & e) {
        EXPECT_EQ(e.errorCode(), IKeychainService::ErrorCode::EntryNotFound);
        caughtException = true;
    }

    EXPECT_TRUE(caughtException);
}

TEST_F(ObfuscatingKeychainServiceTest, ReadWrittenPassword)
{
    ObfuscatingKeychainService obfuscatingKeychainService;

    auto writeFuture = obfuscatingKeychainService.writePassword(
        m_service, m_key, m_password);

    ASSERT_TRUE(writeFuture.isFinished());

    auto readFuture = obfuscatingKeychainService.readPassword(m_service, m_key);
    ASSERT_TRUE(readFuture.isFinished());
    ASSERT_EQ(readFuture.resultCount(), 1);
    EXPECT_EQ(readFuture.result(), m_password);
}

TEST_F(ObfuscatingKeychainServiceTest, DeleteNonexistentPassword)
{
    ObfuscatingKeychainService obfuscatingKeychainService;

    auto deleteFuture = obfuscatingKeychainService.deletePassword(
        m_service, m_key);

    ASSERT_TRUE(deleteFuture.isFinished());
}

TEST_F(ObfuscatingKeychainServiceTest, DeleteWrittenPassword)
{
    ObfuscatingKeychainService obfuscatingKeychainService;

    auto writeFuture = obfuscatingKeychainService.writePassword(
        m_service, m_key, m_password);

    ASSERT_TRUE(writeFuture.isFinished());

    auto deleteFuture = obfuscatingKeychainService.deletePassword(
        m_service, m_key);

    ASSERT_TRUE(deleteFuture.isFinished());

    ApplicationSettings settings{settingsName()};
    settings.beginGroup(settingsGroupName());
    EXPECT_FALSE(settings.contains("Cipher"));
    EXPECT_FALSE(settings.contains("KeyLength"));
    EXPECT_FALSE(settings.contains("Value"));
    settings.endGroup();
}

TEST_F(ObfuscatingKeychainServiceTest, ReadWrittenThenDeletedPassword)
{
    ObfuscatingKeychainService obfuscatingKeychainService;

    auto writeFuture = obfuscatingKeychainService.writePassword(
        m_service, m_key, m_password);

    ASSERT_TRUE(writeFuture.isFinished());

    auto deleteFuture = obfuscatingKeychainService.deletePassword(
        m_service, m_key);

    ASSERT_TRUE(deleteFuture.isFinished());

    auto readFuture = obfuscatingKeychainService.readPassword(m_service, m_key);
    ASSERT_TRUE(readFuture.isFinished());

    bool caughtException = false;
    try {
        readFuture.waitForFinished();
    }
    catch (const IKeychainService::Exception & e) {
        EXPECT_EQ(e.errorCode(), IKeychainService::ErrorCode::EntryNotFound);
        caughtException = true;
    }

    EXPECT_TRUE(caughtException);
}

} // namespace quentier::utility::tests
