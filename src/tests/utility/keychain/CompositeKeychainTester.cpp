/*
 * Copyright 2020 Dmitry Ivanov
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

#include "CompositeKeychainTester.h"
#include "KeychainServiceMock.h"

#include <QSignalSpy>
#include <QtTest/QTest>

namespace quentier {
namespace test {

CompositeKeychainTester::CompositeKeychainTester(QObject * parent) :
    QObject(parent)
{}

void CompositeKeychainTester::checkWritePasswordGoesToBothKeychains()
{
    const auto primaryKeychain = std::make_shared<KeychainServiceMock>();
    const auto secondaryKeychain = std::make_shared<KeychainServiceMock>();

    const auto compositeKeychain = newCompositeKeychainService(
        QStringLiteral("compositeKeychainTest"), primaryKeychain,
        secondaryKeychain);

    bool writeToPrimaryKeychainCalled = false;

    // clang-format off
    primaryKeychain->setWritePasswordHandler(
        [&writeToPrimaryKeychainCalled]
        (const QString & service,
         const QString & key,
         const QString & password)
        {
            Q_UNUSED(service)
            Q_UNUSED(key)
            Q_UNUSED(password)

            writeToPrimaryKeychainCalled = true;

            return KeychainServiceMock::WritePasswordResult{
                QUuid::createUuid(), IKeychainService::ErrorCode::NoError,
                ErrorString()};
        });
    // clang-format on

    bool writeToSecondaryKeychainCalled = false;

    // clang-format off
    secondaryKeychain->setWritePasswordHandler(
        [&writeToSecondaryKeychainCalled]
        (const QString & service,
         const QString & key,
         const QString & password)
        {
            Q_UNUSED(service)
            Q_UNUSED(key)
            Q_UNUSED(password)

            writeToSecondaryKeychainCalled = true;

            return KeychainServiceMock::WritePasswordResult{
                QUuid::createUuid(), IKeychainService::ErrorCode::NoError,
                ErrorString()};
        });
    // clang-format on

    QSignalSpy spy(
        compositeKeychain.get(), &IKeychainService::writePasswordJobFinished);

    QUuid id;
    bool compositeKeychainCallbackCalled = false;

    // clang-format off
    QObject::connect(
        compositeKeychain.get(),
        &IKeychainService::writePasswordJobFinished,
        compositeKeychain.get(),
        [&compositeKeychainCallbackCalled, &id]
        (QUuid requestId, IKeychainService::ErrorCode errorCode,
         ErrorString errorDescription)
        {
            compositeKeychainCallbackCalled = true;
            QVERIFY(id == requestId);
            QVERIFY(errorCode == IKeychainService::ErrorCode::NoError);
            QVERIFY(errorDescription.isEmpty());
        });
    // clang-format on

    id = compositeKeychain->startWritePasswordJob(
        m_service, m_key, m_password);

    QVERIFY(spy.wait(10000));
    QVERIFY(compositeKeychainCallbackCalled);
    QVERIFY(writeToPrimaryKeychainCalled);
    QVERIFY(writeToSecondaryKeychainCalled);
}

void CompositeKeychainTester::checkReadPasswordFromPrimaryKeychainSuccessfully()
{
    const auto primaryKeychain = std::make_shared<KeychainServiceMock>();
    const auto secondaryKeychain = std::make_shared<KeychainServiceMock>();

    const auto compositeKeychain = newCompositeKeychainService(
        QStringLiteral("compositeKeychainTest"), primaryKeychain,
        secondaryKeychain);

    // clang-format off
    auto nullWritePasswordHandler =
        [](const QString & service, const QString & key,
           const QString & password)
        {
            Q_UNUSED(service)
            Q_UNUSED(key)
            Q_UNUSED(password)

            return KeychainServiceMock::WritePasswordResult{
                QUuid::createUuid(),
                IKeychainService::ErrorCode::NoError,
                ErrorString()};
        };
    // clang-format on

    primaryKeychain->setWritePasswordHandler(nullWritePasswordHandler);
    secondaryKeychain->setWritePasswordHandler(nullWritePasswordHandler);

    QSignalSpy writeSpy(
        compositeKeychain.get(), &IKeychainService::writePasswordJobFinished);

    compositeKeychain->startWritePasswordJob(
        m_service, m_key, m_password);

    QVERIFY(writeSpy.wait(10000));

    bool readFromPrimaryKeychainCalled = false;

    // clang-format off
    primaryKeychain->setReadPasswordHandler(
        [&readFromPrimaryKeychainCalled, password=m_password]
        (const QString & service,
         const QString & key)
        {
            Q_UNUSED(service)
            Q_UNUSED(key)

            readFromPrimaryKeychainCalled = true;

            return KeychainServiceMock::ReadPasswordResult{
                QUuid::createUuid(),
                IKeychainService::ErrorCode::NoError,
                ErrorString(),
                password};
        });
    // clang-format on

    bool readFromSecondaryKeychainCalled = false;

    // clang-format off
    secondaryKeychain->setReadPasswordHandler(
        [&readFromSecondaryKeychainCalled, password=m_password]
        (const QString & service,
         const QString & key)
        {
            Q_UNUSED(service)
            Q_UNUSED(key)

            readFromSecondaryKeychainCalled = true;

            return KeychainServiceMock::ReadPasswordResult{
                QUuid::createUuid(),
                IKeychainService::ErrorCode::NoError,
                ErrorString(),
                password};
        });
    // clang-format on

    QUuid id;
    bool readPasswordCallbackCalled = false;

    // clang-format off
    QObject::connect(
        compositeKeychain.get(),
        &IKeychainService::readPasswordJobFinished,
        compositeKeychain.get(),
        [&readPasswordCallbackCalled, &id, expectedPassword=m_password]
        (QUuid requestId, IKeychainService::ErrorCode errorCode,
         ErrorString errorDescription, QString password)
        {
            readPasswordCallbackCalled = true;
            QVERIFY(requestId == id);
            QVERIFY(errorCode == IKeychainService::ErrorCode::NoError);
            QVERIFY(errorDescription.isEmpty());
            QVERIFY(password == expectedPassword);
        });
    // clang-format on

    QSignalSpy readSpy(
        compositeKeychain.get(), &IKeychainService::readPasswordJobFinished);

    id = compositeKeychain->startReadPasswordJob(m_service, m_key);

    QVERIFY(readSpy.wait(10000));
    QVERIFY(readPasswordCallbackCalled);
    QVERIFY(readFromPrimaryKeychainCalled);
    QVERIFY(!readFromSecondaryKeychainCalled);
}

} // namespace test
} // namespace quentier
