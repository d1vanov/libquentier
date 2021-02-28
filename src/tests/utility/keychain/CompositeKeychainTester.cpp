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

#include "../../TestMacros.h"

#include <quentier/utility/ApplicationSettings.h>

#include <QSignalSpy>
#include <QtTest/QTest>

#include <stdexcept>

namespace quentier {
namespace test {

CompositeKeychainTester::CompositeKeychainTester(QObject * parent) :
    QObject(parent)
{}

void CompositeKeychainTester::throwExceptionWhenGivenNullPrimaryKeychain()
{
    QVERIFY_EXCEPTION_THROWN(
        const auto compositeKeychain = newCompositeKeychainService(
            m_name, nullptr, std::make_shared<KeychainServiceMock>()),
        std::invalid_argument);
}

void CompositeKeychainTester::throwExceptionWhenGivenNullSecondaryKeychain()
{
    QVERIFY_EXCEPTION_THROWN(
        const auto compositeKeychain = newCompositeKeychainService(
            m_name, std::make_shared<KeychainServiceMock>(), nullptr),
        std::invalid_argument);
}

void CompositeKeychainTester::writePasswordToBothKeychains()
{
    const auto primaryKeychain = std::make_shared<KeychainServiceMock>();
    const auto secondaryKeychain = std::make_shared<KeychainServiceMock>();

    const auto compositeKeychain =
        newCompositeKeychainService(m_name, primaryKeychain, secondaryKeychain);

    bool writeToPrimaryKeychainCalled = false;
    const auto primaryKeychainWriteRequestId = QUuid::createUuid();

    // clang-format off
    primaryKeychain->setWritePasswordHandler(
        [&writeToPrimaryKeychainCalled, primaryKeychainWriteRequestId,
         expectedService=m_service, expectedKey=m_key,
         expectedPassword=m_password]
        (const QString & service, const QString & key, const QString & password)
        {
            VERIFY_THROW(service == expectedService)
            VERIFY_THROW(key == expectedKey)
            VERIFY_THROW(password == expectedPassword)

            writeToPrimaryKeychainCalled = true;

            return KeychainServiceMock::WritePasswordResult{
                primaryKeychainWriteRequestId,
                IKeychainService::ErrorCode::NoError, ErrorString()};
        });
    // clang-format on

    bool writeToSecondaryKeychainCalled = false;
    const auto secondaryKeychainWriteRequestId = QUuid::createUuid();

    // clang-format off
    secondaryKeychain->setWritePasswordHandler(
        [&writeToSecondaryKeychainCalled, secondaryKeychainWriteRequestId,
         expectedService=m_service, expectedKey=m_key,
         expectedPassword=m_password]
        (const QString & service, const QString & key, const QString & password)
        {
            VERIFY_THROW(service == expectedService)
            VERIFY_THROW(key == expectedKey)
            VERIFY_THROW(password == expectedPassword)

            writeToSecondaryKeychainCalled = true;

            return KeychainServiceMock::WritePasswordResult{
                secondaryKeychainWriteRequestId,
                IKeychainService::ErrorCode::NoError, ErrorString()};
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

    id = compositeKeychain->startWritePasswordJob(m_service, m_key, m_password);
    QVERIFY(id == primaryKeychainWriteRequestId);

    QVERIFY(spy.wait(10000));
    QVERIFY(compositeKeychainCallbackCalled);
    QVERIFY(writeToPrimaryKeychainCalled);
    QVERIFY(writeToSecondaryKeychainCalled);
}

void CompositeKeychainTester::readPasswordFromPrimaryKeychainFirst()
{
    const auto primaryKeychain = std::make_shared<KeychainServiceMock>();
    const auto secondaryKeychain = std::make_shared<KeychainServiceMock>();

    const auto compositeKeychain =
        newCompositeKeychainService(m_name, primaryKeychain, secondaryKeychain);

    bool readFromPrimaryKeychainCalled = false;
    const auto readFromPrimaryKeychainRequestId = QUuid::createUuid();

    // clang-format off
    primaryKeychain->setReadPasswordHandler(
        [&readFromPrimaryKeychainCalled, readFromPrimaryKeychainRequestId,
         expectedService=m_service, expectedKey=m_key, password=m_password]
        (const QString & service, const QString & key)
        {
            VERIFY_THROW(service == expectedService)
            VERIFY_THROW(key == expectedKey)

            readFromPrimaryKeychainCalled = true;

            return KeychainServiceMock::ReadPasswordResult{
                readFromPrimaryKeychainRequestId,
                IKeychainService::ErrorCode::NoError,
                ErrorString(),
                password};
        });
    // clang-format on

    bool readFromSecondaryKeychainCalled = false;
    const auto readFromSecondaryKeychainRequestId = QUuid::createUuid();

    // clang-format off
    secondaryKeychain->setReadPasswordHandler(
        [&readFromSecondaryKeychainCalled, readFromSecondaryKeychainRequestId,
         expectedService=m_service, expectedKey=m_key, password=m_password]
        (const QString & service, const QString & key)
        {
            VERIFY_THROW(service == expectedService)
            VERIFY_THROW(key == expectedKey)

            readFromSecondaryKeychainCalled = true;

            return KeychainServiceMock::ReadPasswordResult{
                readFromSecondaryKeychainRequestId,
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
    QVERIFY(id == readFromPrimaryKeychainRequestId);

    QVERIFY(readSpy.wait(10000));
    QVERIFY(readPasswordCallbackCalled);
    QVERIFY(readFromPrimaryKeychainCalled);
    QVERIFY(!readFromSecondaryKeychainCalled);
}

void CompositeKeychainTester::readPasswordFromSecondaryKeychainAsFallback()
{
    const auto primaryKeychain = std::make_shared<KeychainServiceMock>();
    const auto secondaryKeychain = std::make_shared<KeychainServiceMock>();

    const auto compositeKeychain =
        newCompositeKeychainService(m_name, primaryKeychain, secondaryKeychain);

    bool readFromPrimaryKeychainCalled = false;
    const auto readFromPrimaryKeychainRequestId = QUuid::createUuid();

    // clang-format off
    primaryKeychain->setReadPasswordHandler(
        [&readFromPrimaryKeychainCalled, readFromPrimaryKeychainRequestId,
         expectedService=m_service, expectedKey=m_key, password=m_password]
        (const QString & service, const QString & key)
        {
            VERIFY_THROW(service == expectedService)
            VERIFY_THROW(key == expectedKey)

            readFromPrimaryKeychainCalled = true;

            return KeychainServiceMock::ReadPasswordResult{
                readFromPrimaryKeychainRequestId,
                IKeychainService::ErrorCode::NoBackendAvailable,
                ErrorString(),
                password};
        });
    // clang-format on

    bool readFromSecondaryKeychainCalled = false;
    const auto readFromSecondaryKeychainRequestId = QUuid::createUuid();

    // clang-format off
    secondaryKeychain->setReadPasswordHandler(
        [&readFromSecondaryKeychainCalled, readFromSecondaryKeychainRequestId,
         expectedService=m_service, expectedKey=m_key, password=m_password]
        (const QString & service, const QString & key)
        {
            VERIFY_THROW(service == expectedService)
            VERIFY_THROW(key == expectedKey)

            readFromSecondaryKeychainCalled = true;

            return KeychainServiceMock::ReadPasswordResult{
                readFromSecondaryKeychainRequestId,
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
    QVERIFY(id == readFromPrimaryKeychainRequestId);

    QVERIFY(readSpy.wait(10000));
    QVERIFY(readPasswordCallbackCalled);
    QVERIFY(readFromPrimaryKeychainCalled);
    QVERIFY(readFromSecondaryKeychainCalled);
}

void CompositeKeychainTester::
    readPasswordFromSecondaryKeychainIfWritingToPrimaryFails()
{
    const auto primaryKeychain = std::make_shared<KeychainServiceMock>();
    const auto secondaryKeychain = std::make_shared<KeychainServiceMock>();

    const auto compositeKeychain =
        newCompositeKeychainService(m_name, primaryKeychain, secondaryKeychain);

    bool writeToPrimaryKeychainCalled = false;
    const auto primaryKeychainWriteRequestId = QUuid::createUuid();

    // clang-format off
    primaryKeychain->setWritePasswordHandler(
        [&writeToPrimaryKeychainCalled, primaryKeychainWriteRequestId,
         expectedService=m_service, expectedKey=m_key,
         expectedPassword=m_password]
        (const QString & service, const QString & key, const QString & password)
        {
            VERIFY_THROW(service == expectedService)
            VERIFY_THROW(key == expectedKey)
            VERIFY_THROW(password == expectedPassword)

            writeToPrimaryKeychainCalled = true;

            return KeychainServiceMock::WritePasswordResult{
                primaryKeychainWriteRequestId,
                IKeychainService::ErrorCode::AccessDenied, ErrorString()};
        });
    // clang-format on

    bool writeToSecondaryKeychainCalled = false;
    const auto secondaryKeychainWriteRequestId = QUuid::createUuid();

    // clang-format off
    secondaryKeychain->setWritePasswordHandler(
        [&writeToSecondaryKeychainCalled, secondaryKeychainWriteRequestId,
         expectedService=m_service, expectedKey=m_key,
         expectedPassword=m_password]
        (const QString & service, const QString & key, const QString & password)
        {
            VERIFY_THROW(service == expectedService)
            VERIFY_THROW(key == expectedKey)
            VERIFY_THROW(password == expectedPassword)

            writeToSecondaryKeychainCalled = true;

            return KeychainServiceMock::WritePasswordResult{
                secondaryKeychainWriteRequestId,
                IKeychainService::ErrorCode::NoError, ErrorString()};
        });
    // clang-format on

    QSignalSpy writeSpy(
        compositeKeychain.get(), &IKeychainService::writePasswordJobFinished);

    QUuid writeId;
    bool compositeKeychainWriteCallbackCalled = false;

    // clang-format off
    QObject::connect(
        compositeKeychain.get(),
        &IKeychainService::writePasswordJobFinished,
        compositeKeychain.get(),
        [&compositeKeychainWriteCallbackCalled, &writeId]
        (QUuid requestId, IKeychainService::ErrorCode errorCode,
         ErrorString errorDescription)
        {
            compositeKeychainWriteCallbackCalled = true;
            QVERIFY(writeId == requestId);
            QVERIFY(errorCode == IKeychainService::ErrorCode::NoError);
            QVERIFY(errorDescription.isEmpty());
        });
    // clang-format on

    writeId =
        compositeKeychain->startWritePasswordJob(m_service, m_key, m_password);

    QVERIFY(writeId == primaryKeychainWriteRequestId);

    QVERIFY(writeSpy.wait(10000));
    QVERIFY(compositeKeychainWriteCallbackCalled);
    QVERIFY(writeToPrimaryKeychainCalled);
    QVERIFY(writeToSecondaryKeychainCalled);

    bool readFromPrimaryKeychainCalled = false;
    const auto readFromPrimaryKeychainRequestId = QUuid::createUuid();

    // clang-format off
    primaryKeychain->setReadPasswordHandler(
        [&readFromPrimaryKeychainCalled, readFromPrimaryKeychainRequestId,
         expectedService=m_service, expectedKey=m_key, password=m_password]
        (const QString & service, const QString & key)
        {
            VERIFY_THROW(service == expectedService)
            VERIFY_THROW(key == expectedKey)

            readFromPrimaryKeychainCalled = true;

            return KeychainServiceMock::ReadPasswordResult{
                readFromPrimaryKeychainRequestId,
                IKeychainService::ErrorCode::NoBackendAvailable,
                ErrorString(),
                password};
        });
    // clang-format on

    bool readFromSecondaryKeychainCalled = false;
    const auto readFromSecondaryKeychainRequestId = QUuid::createUuid();

    // clang-format off
    secondaryKeychain->setReadPasswordHandler(
        [&readFromSecondaryKeychainCalled, readFromSecondaryKeychainRequestId,
         expectedService=m_service, expectedKey=m_key, password=m_password]
        (const QString & service, const QString & key)
        {
            VERIFY_THROW(service == expectedService)
            VERIFY_THROW(key == expectedKey)

            readFromSecondaryKeychainCalled = true;

            return KeychainServiceMock::ReadPasswordResult{
                readFromSecondaryKeychainRequestId,
                IKeychainService::ErrorCode::NoError,
                ErrorString(),
                password};
        });
    // clang-format on

    QUuid readId;
    bool readPasswordCallbackCalled = false;

    // clang-format off
    QObject::connect(
        compositeKeychain.get(),
        &IKeychainService::readPasswordJobFinished,
        compositeKeychain.get(),
        [&readPasswordCallbackCalled, &readId, expectedPassword=m_password]
        (QUuid requestId, IKeychainService::ErrorCode errorCode,
         ErrorString errorDescription, QString password)
        {
            readPasswordCallbackCalled = true;
            QVERIFY(requestId == readId);
            QVERIFY(errorCode == IKeychainService::ErrorCode::NoError);
            QVERIFY(errorDescription.isEmpty());
            QVERIFY(password == expectedPassword);
        });
    // clang-format on

    QSignalSpy readSpy(
        compositeKeychain.get(), &IKeychainService::readPasswordJobFinished);

    readId = compositeKeychain->startReadPasswordJob(m_service, m_key);
    QVERIFY(readId == readFromSecondaryKeychainRequestId);

    QVERIFY(readSpy.wait(10000));
    QVERIFY(readPasswordCallbackCalled);
    QVERIFY(!readFromPrimaryKeychainCalled);
    QVERIFY(readFromSecondaryKeychainCalled);
}

void CompositeKeychainTester::
    dontReadFromEitherKeychainIfWritingToBothKeychainsFails()
{
    const auto primaryKeychain = std::make_shared<KeychainServiceMock>();
    const auto secondaryKeychain = std::make_shared<KeychainServiceMock>();

    const auto compositeKeychain =
        newCompositeKeychainService(m_name, primaryKeychain, secondaryKeychain);

    bool writeToPrimaryKeychainCalled = false;
    const auto primaryKeychainWriteRequestId = QUuid::createUuid();

    auto primaryKeychainWriteErrorCode =
        IKeychainService::ErrorCode::NoBackendAvailable;

    // clang-format off
    primaryKeychain->setWritePasswordHandler(
        [&writeToPrimaryKeychainCalled, primaryKeychainWriteRequestId,
         &primaryKeychainWriteErrorCode, expectedService=m_service,
         expectedKey=m_key, expectedPassword=m_password]
        (const QString & service, const QString & key, const QString & password)
        {
            VERIFY_THROW(service == expectedService)
            VERIFY_THROW(key == expectedKey)
            VERIFY_THROW(password == expectedPassword)

            writeToPrimaryKeychainCalled = true;

            return KeychainServiceMock::WritePasswordResult{
                primaryKeychainWriteRequestId, primaryKeychainWriteErrorCode,
                ErrorString()};
        });
    // clang-format on

    bool writeToSecondaryKeychainCalled = false;
    const auto secondaryKeychainWriteRequestId = QUuid::createUuid();

    auto secondaryKeychainWriteErrorCode =
        IKeychainService::ErrorCode::AccessDenied;

    // clang-format off
    secondaryKeychain->setWritePasswordHandler(
        [&writeToSecondaryKeychainCalled, secondaryKeychainWriteRequestId,
         &secondaryKeychainWriteErrorCode, expectedService=m_service,
         expectedKey=m_key, expectedPassword=m_password]
        (const QString & service, const QString & key, const QString & password)
        {
            VERIFY_THROW(service == expectedService)
            VERIFY_THROW(key == expectedKey)
            VERIFY_THROW(password == expectedPassword)

            writeToSecondaryKeychainCalled = true;

            return KeychainServiceMock::WritePasswordResult{
                secondaryKeychainWriteRequestId,
                secondaryKeychainWriteErrorCode, ErrorString()};
        });
    // clang-format on

    QSignalSpy spy(
        compositeKeychain.get(), &IKeychainService::writePasswordJobFinished);

    QUuid writeId;
    bool compositeKeychainWriteCallbackCalled = false;

    auto expectedWriteErrorCode =
        IKeychainService::ErrorCode::NoBackendAvailable;

    // clang-format off
    QObject::connect(
        compositeKeychain.get(),
        &IKeychainService::writePasswordJobFinished,
        compositeKeychain.get(),
        [&compositeKeychainWriteCallbackCalled, &writeId,
         &expectedWriteErrorCode]
        (QUuid requestId, IKeychainService::ErrorCode errorCode,
         ErrorString errorDescription)
        {
            Q_UNUSED(errorDescription)

            compositeKeychainWriteCallbackCalled = true;
            QVERIFY(writeId == requestId);
            QVERIFY(errorCode == expectedWriteErrorCode);
        });
    // clang-format on

    writeId =
        compositeKeychain->startWritePasswordJob(m_service, m_key, m_password);

    QVERIFY(writeId == primaryKeychainWriteRequestId);

    QVERIFY(spy.wait(10000));
    QVERIFY(compositeKeychainWriteCallbackCalled);
    QVERIFY(writeToPrimaryKeychainCalled);
    QVERIFY(writeToSecondaryKeychainCalled);

    // Read password attempts should not touch either keychain now as we have
    // failed to write the password to both of them

    bool readFromPrimaryKeychainCalled = false;
    const auto readFromPrimaryKeychainRequestId = QUuid::createUuid();

    auto primaryKeychainReadErrorCode =
        IKeychainService::ErrorCode::EntryNotFound;

    // clang-format off
    primaryKeychain->setReadPasswordHandler(
        [&readFromPrimaryKeychainCalled, readFromPrimaryKeychainRequestId,
         &primaryKeychainReadErrorCode, expectedService=m_service,
         expectedKey=m_key, password=m_password]
        (const QString & service, const QString & key)
        {
            VERIFY_THROW(service == expectedService)
            VERIFY_THROW(key == expectedKey)

            readFromPrimaryKeychainCalled = true;

            return KeychainServiceMock::ReadPasswordResult{
                readFromPrimaryKeychainRequestId, primaryKeychainReadErrorCode,
                ErrorString(), password};
        });
    // clang-format on

    bool readFromSecondaryKeychainCalled = false;
    const auto readFromSecondaryKeychainRequestId = QUuid::createUuid();

    auto secondaryKeychainReadErrorCode =
        IKeychainService::ErrorCode::EntryNotFound;

    // clang-format off
    secondaryKeychain->setReadPasswordHandler(
        [&readFromSecondaryKeychainCalled, readFromSecondaryKeychainRequestId,
         &secondaryKeychainReadErrorCode, expectedService=m_service,
         expectedKey=m_key, password=m_password]
        (const QString & service, const QString & key)
        {
            VERIFY_THROW(service == expectedService)
            VERIFY_THROW(key == expectedKey)

            readFromSecondaryKeychainCalled = true;

            return KeychainServiceMock::ReadPasswordResult{
                readFromSecondaryKeychainRequestId,
                secondaryKeychainReadErrorCode,
                ErrorString(),
                password};
        });
    // clang-format on

    QUuid readId;
    bool readPasswordCallbackCalled = false;
    auto expectedReadErrorCode = IKeychainService::ErrorCode::EntryNotFound;
    QString expectedPassword;

    // clang-format off
    QObject::connect(
        compositeKeychain.get(),
        &IKeychainService::readPasswordJobFinished,
        compositeKeychain.get(),
        [&readPasswordCallbackCalled, &readId, &expectedReadErrorCode,
         &expectedPassword]
        (QUuid requestId, IKeychainService::ErrorCode errorCode,
         ErrorString errorDescription, QString password)
        {
            Q_UNUSED(errorDescription)

            readPasswordCallbackCalled = true;
            QVERIFY(requestId == readId);
            QVERIFY(errorCode == expectedReadErrorCode);
            QVERIFY(password == expectedPassword);
        });
    // clang-format on

    QSignalSpy readSpy(
        compositeKeychain.get(), &IKeychainService::readPasswordJobFinished);

    readId = compositeKeychain->startReadPasswordJob(m_service, m_key);
    QVERIFY(readId != readFromPrimaryKeychainRequestId);
    QVERIFY(readId != readFromSecondaryKeychainRequestId);

    QVERIFY(readSpy.wait(10000));
    QVERIFY(readPasswordCallbackCalled);
    QVERIFY(!readFromPrimaryKeychainCalled);
    QVERIFY(!readFromSecondaryKeychainCalled);

    // Successful writing to the primary keychain should enable reading from it
    // again

    writeToPrimaryKeychainCalled = false;
    primaryKeychainWriteErrorCode = IKeychainService::ErrorCode::NoError;

    writeToSecondaryKeychainCalled = false;
    secondaryKeychainWriteErrorCode = IKeychainService::ErrorCode::NoError;

    QSignalSpy writeSpy(
        compositeKeychain.get(), &IKeychainService::writePasswordJobFinished);

    compositeKeychainWriteCallbackCalled = false;
    expectedWriteErrorCode = IKeychainService::ErrorCode::NoError;

    writeId =
        compositeKeychain->startWritePasswordJob(m_service, m_key, m_password);

    QVERIFY(writeId == primaryKeychainWriteRequestId);

    QVERIFY(writeSpy.wait(10000));
    QVERIFY(compositeKeychainWriteCallbackCalled);
    QVERIFY(writeToPrimaryKeychainCalled);
    QVERIFY(writeToSecondaryKeychainCalled);

    readFromPrimaryKeychainCalled = false;
    readFromSecondaryKeychainCalled = false;
    readPasswordCallbackCalled = false;

    // clang-format off
    primaryKeychain->setReadPasswordHandler(
        [&readFromPrimaryKeychainCalled, readFromPrimaryKeychainRequestId,
         expectedService=m_service, expectedKey=m_key, password=m_password]
        (const QString & service, const QString & key)
        {
            VERIFY_THROW(service == expectedService)
            VERIFY_THROW(key == expectedKey)

            readFromPrimaryKeychainCalled = true;

            return KeychainServiceMock::ReadPasswordResult{
                readFromPrimaryKeychainRequestId,
                IKeychainService::ErrorCode::NoError, ErrorString(), password};
        });
    // clang-format on

    primaryKeychainReadErrorCode = IKeychainService::ErrorCode::NoError;
    secondaryKeychainReadErrorCode = IKeychainService::ErrorCode::NoError;

    expectedReadErrorCode = IKeychainService::ErrorCode::NoError;
    expectedPassword = m_password;

    readId = compositeKeychain->startReadPasswordJob(m_service, m_key);
    QVERIFY(readId == readFromPrimaryKeychainRequestId);

    QVERIFY(readSpy.wait(10000));
    QVERIFY(readPasswordCallbackCalled);
    QVERIFY(readFromPrimaryKeychainCalled);
    QVERIFY(!readFromSecondaryKeychainCalled);
}

void CompositeKeychainTester::deletePasswordFromBothKeychains()
{
    const auto primaryKeychain = std::make_shared<KeychainServiceMock>();
    const auto secondaryKeychain = std::make_shared<KeychainServiceMock>();

    const auto compositeKeychain =
        newCompositeKeychainService(m_name, primaryKeychain, secondaryKeychain);

    bool deleteFromPrimaryKeychainCalled = false;
    const auto primaryKeychainDeleteRequestId = QUuid::createUuid();

    // clang-format off
    primaryKeychain->setDeletePasswordHandler(
        [&deleteFromPrimaryKeychainCalled, primaryKeychainDeleteRequestId,
         expectedService=m_service, expectedKey=m_key]
        (const QString & service, const QString & key)
        {
            VERIFY_THROW(service == expectedService)
            VERIFY_THROW(key == expectedKey)

            deleteFromPrimaryKeychainCalled = true;

            return KeychainServiceMock::DeletePasswordResult{
                primaryKeychainDeleteRequestId,
                IKeychainService::ErrorCode::NoError, ErrorString()};
        });
    // clang-format on

    bool deleteFromSecondaryKeychainCalled = false;
    const auto secondaryKeychainDeleteRequestId = QUuid::createUuid();

    // clang-format off
    secondaryKeychain->setDeletePasswordHandler(
        [&deleteFromSecondaryKeychainCalled, secondaryKeychainDeleteRequestId,
         expectedService=m_service, expectedKey=m_key]
        (const QString & service, const QString & key)
        {
            VERIFY_THROW(service == expectedService)
            VERIFY_THROW(key == expectedKey)

            deleteFromSecondaryKeychainCalled = true;

            return KeychainServiceMock::DeletePasswordResult{
                secondaryKeychainDeleteRequestId,
                IKeychainService::ErrorCode::NoError, ErrorString()};
        });
    // clang-format on

    QUuid deleteId;
    bool deletePasswordCallbackCalled = false;

    // clang-format off
    QObject::connect(
        compositeKeychain.get(),
        &IKeychainService::deletePasswordJobFinished,
        compositeKeychain.get(),
        [&deletePasswordCallbackCalled, &deleteId]
        (QUuid requestId, IKeychainService::ErrorCode errorCode,
         ErrorString errorDescription)
        {
            deletePasswordCallbackCalled = true;
            QVERIFY(requestId == deleteId);
            QVERIFY(errorCode == IKeychainService::ErrorCode::NoError);
            QVERIFY(errorDescription.isEmpty());
        });
    // clang-format on

    QSignalSpy deleteSpy(
        compositeKeychain.get(), &IKeychainService::deletePasswordJobFinished);

    deleteId = compositeKeychain->startDeletePasswordJob(m_service, m_key);
    QVERIFY(deleteId == primaryKeychainDeleteRequestId);

    QVERIFY(deleteSpy.wait(10000));
    QVERIFY(deletePasswordCallbackCalled);
    QVERIFY(deleteFromPrimaryKeychainCalled);
    QVERIFY(deleteFromSecondaryKeychainCalled);
}

void CompositeKeychainTester::handleDeleteFromPrimaryKeychainError()
{
    const auto primaryKeychain = std::make_shared<KeychainServiceMock>();
    const auto secondaryKeychain = std::make_shared<KeychainServiceMock>();

    const auto compositeKeychain =
        newCompositeKeychainService(m_name, primaryKeychain, secondaryKeychain);

    bool deleteFromPrimaryKeychainCalled = false;
    const auto primaryKeychainDeleteRequestId = QUuid::createUuid();

    // clang-format off
    primaryKeychain->setDeletePasswordHandler(
        [&deleteFromPrimaryKeychainCalled, primaryKeychainDeleteRequestId,
         expectedService=m_service, expectedKey=m_key]
        (const QString & service, const QString & key)
        {
            VERIFY_THROW(service == expectedService)
            VERIFY_THROW(key == expectedKey)

            deleteFromPrimaryKeychainCalled = true;

            return KeychainServiceMock::DeletePasswordResult{
                primaryKeychainDeleteRequestId,
                IKeychainService::ErrorCode::NoBackendAvailable, ErrorString()};
        });
    // clang-format on

    bool deleteFromSecondaryKeychainCalled = false;
    const auto secondaryKeychainDeleteRequestId = QUuid::createUuid();

    // clang-format off
    secondaryKeychain->setDeletePasswordHandler(
        [&deleteFromSecondaryKeychainCalled, secondaryKeychainDeleteRequestId,
         expectedService=m_service, expectedKey=m_key]
        (const QString & service, const QString & key)
        {
            VERIFY_THROW(service == expectedService)
            VERIFY_THROW(key == expectedKey)

            deleteFromSecondaryKeychainCalled = true;

            return KeychainServiceMock::DeletePasswordResult{
                secondaryKeychainDeleteRequestId,
                IKeychainService::ErrorCode::NoError, ErrorString()};
        });
    // clang-format on

    QUuid deleteId;
    bool deletePasswordCallbackCalled = false;

    // clang-format off
    QObject::connect(
        compositeKeychain.get(),
        &IKeychainService::deletePasswordJobFinished,
        compositeKeychain.get(),
        [&deletePasswordCallbackCalled, &deleteId]
        (QUuid requestId, IKeychainService::ErrorCode errorCode,
         ErrorString errorDescription)
        {
            deletePasswordCallbackCalled = true;
            QVERIFY(requestId == deleteId);
            QVERIFY(errorCode == IKeychainService::ErrorCode::NoError);
            QVERIFY(errorDescription.isEmpty());
        });
    // clang-format on

    QSignalSpy deleteSpy(
        compositeKeychain.get(), &IKeychainService::deletePasswordJobFinished);

    deleteId = compositeKeychain->startDeletePasswordJob(m_service, m_key);
    QVERIFY(deleteId == primaryKeychainDeleteRequestId);

    QVERIFY(deleteSpy.wait(10000));
    QVERIFY(deletePasswordCallbackCalled);
    QVERIFY(deleteFromPrimaryKeychainCalled);
    QVERIFY(deleteFromSecondaryKeychainCalled);

    // Read password attempts should not touch the primary keychain now as
    // we have failed to delete the password from there

    bool readFromPrimaryKeychainCalled = false;
    const auto readFromPrimaryKeychainRequestId = QUuid::createUuid();

    // clang-format off
    primaryKeychain->setReadPasswordHandler(
        [&readFromPrimaryKeychainCalled, readFromPrimaryKeychainRequestId,
         expectedService=m_service, expectedKey=m_key, password=m_password]
        (const QString & service, const QString & key)
        {
            VERIFY_THROW(service == expectedService)
            VERIFY_THROW(key == expectedKey)

            readFromPrimaryKeychainCalled = true;

            return KeychainServiceMock::ReadPasswordResult{
                readFromPrimaryKeychainRequestId,
                IKeychainService::ErrorCode::NoBackendAvailable,
                ErrorString(),
                password};
        });
    // clang-format on

    bool readFromSecondaryKeychainCalled = false;
    const auto readFromSecondaryKeychainRequestId = QUuid::createUuid();

    // clang-format off
    secondaryKeychain->setReadPasswordHandler(
        [&readFromSecondaryKeychainCalled, readFromSecondaryKeychainRequestId,
         expectedService=m_service, expectedKey=m_key, password=m_password]
        (const QString & service, const QString & key)
        {
            VERIFY_THROW(service == expectedService)
            VERIFY_THROW(key == expectedKey)

            readFromSecondaryKeychainCalled = true;

            return KeychainServiceMock::ReadPasswordResult{
                readFromSecondaryKeychainRequestId,
                IKeychainService::ErrorCode::NoError,
                ErrorString(),
                password};
        });
    // clang-format on

    QUuid readId;
    bool readPasswordCallbackCalled = false;

    // clang-format off
    QObject::connect(
        compositeKeychain.get(),
        &IKeychainService::readPasswordJobFinished,
        compositeKeychain.get(),
        [&readPasswordCallbackCalled, &readId, expectedPassword=m_password]
        (QUuid requestId, IKeychainService::ErrorCode errorCode,
         ErrorString errorDescription, QString password)
        {
            readPasswordCallbackCalled = true;
            QVERIFY(requestId == readId);
            QVERIFY(errorCode == IKeychainService::ErrorCode::NoError);
            QVERIFY(errorDescription.isEmpty());
            QVERIFY(password == expectedPassword);
        });
    // clang-format on

    QSignalSpy readSpy(
        compositeKeychain.get(), &IKeychainService::readPasswordJobFinished);

    readId = compositeKeychain->startReadPasswordJob(m_service, m_key);
    QVERIFY(readId == readFromSecondaryKeychainRequestId);

    QVERIFY(readSpy.wait(10000));
    QVERIFY(readPasswordCallbackCalled);
    QVERIFY(!readFromPrimaryKeychainCalled);
    QVERIFY(readFromSecondaryKeychainCalled);

    // Successful writing to the primary keychain should enable reading from it
    // again

    bool writeToPrimaryKeychainCalled = false;
    const auto primaryKeychainWriteRequestId = QUuid::createUuid();

    // clang-format off
    primaryKeychain->setWritePasswordHandler(
        [&writeToPrimaryKeychainCalled, primaryKeychainWriteRequestId,
         expectedService=m_service, expectedKey=m_key,
         expectedPassword=m_password]
        (const QString & service, const QString & key, const QString & password)
        {
            VERIFY_THROW(service == expectedService)
            VERIFY_THROW(key == expectedKey)
            VERIFY_THROW(password == expectedPassword)

            writeToPrimaryKeychainCalled = true;

            return KeychainServiceMock::WritePasswordResult{
                primaryKeychainWriteRequestId,
                IKeychainService::ErrorCode::NoError, ErrorString()};
        });
    // clang-format on

    bool writeToSecondaryKeychainCalled = false;
    const auto secondaryKeychainWriteRequestId = QUuid::createUuid();

    // clang-format off
    secondaryKeychain->setWritePasswordHandler(
        [&writeToSecondaryKeychainCalled, secondaryKeychainWriteRequestId,
         expectedService=m_service, expectedKey=m_key,
         expectedPassword=m_password]
        (const QString & service, const QString & key, const QString & password)
        {
            VERIFY_THROW(service == expectedService)
            VERIFY_THROW(key == expectedKey)
            VERIFY_THROW(password == expectedPassword)

            writeToSecondaryKeychainCalled = true;

            return KeychainServiceMock::WritePasswordResult{
                secondaryKeychainWriteRequestId,
                IKeychainService::ErrorCode::NoError, ErrorString()};
        });
    // clang-format on

    QSignalSpy writeSpy(
        compositeKeychain.get(), &IKeychainService::writePasswordJobFinished);

    QUuid writeId;
    bool compositeKeychainWriteCallbackCalled = false;

    // clang-format off
    QObject::connect(
        compositeKeychain.get(),
        &IKeychainService::writePasswordJobFinished,
        compositeKeychain.get(),
        [&compositeKeychainWriteCallbackCalled, &writeId]
        (QUuid requestId, IKeychainService::ErrorCode errorCode,
         ErrorString errorDescription)
        {
            compositeKeychainWriteCallbackCalled = true;
            QVERIFY(writeId == requestId);
            QVERIFY(errorCode == IKeychainService::ErrorCode::NoError);
            QVERIFY(errorDescription.isEmpty());
        });
    // clang-format on

    writeId =
        compositeKeychain->startWritePasswordJob(m_service, m_key, m_password);

    QVERIFY(writeId == primaryKeychainWriteRequestId);

    QVERIFY(writeSpy.wait(10000));
    QVERIFY(compositeKeychainWriteCallbackCalled);
    QVERIFY(writeToPrimaryKeychainCalled);
    QVERIFY(writeToSecondaryKeychainCalled);

    readFromPrimaryKeychainCalled = false;
    readFromSecondaryKeychainCalled = false;
    readPasswordCallbackCalled = false;

    // clang-format off
    primaryKeychain->setReadPasswordHandler(
        [&readFromPrimaryKeychainCalled, readFromPrimaryKeychainRequestId,
         expectedService=m_service, expectedKey=m_key, password=m_password]
        (const QString & service, const QString & key)
        {
            VERIFY_THROW(service == expectedService)
            VERIFY_THROW(key == expectedKey)

            readFromPrimaryKeychainCalled = true;

            return KeychainServiceMock::ReadPasswordResult{
                readFromPrimaryKeychainRequestId,
                IKeychainService::ErrorCode::NoError, ErrorString(), password};
        });
    // clang-format on

    readId = compositeKeychain->startReadPasswordJob(m_service, m_key);
    QVERIFY(readId == readFromPrimaryKeychainRequestId);

    QVERIFY(readSpy.wait(10000));
    QVERIFY(readPasswordCallbackCalled);
    QVERIFY(readFromPrimaryKeychainCalled);
    QVERIFY(!readFromSecondaryKeychainCalled);
}

void CompositeKeychainTester::handleDeleteFromSecondaryKeychainError()
{
    const auto primaryKeychain = std::make_shared<KeychainServiceMock>();
    const auto secondaryKeychain = std::make_shared<KeychainServiceMock>();

    const auto compositeKeychain =
        newCompositeKeychainService(m_name, primaryKeychain, secondaryKeychain);

    bool deleteFromPrimaryKeychainCalled = false;
    const auto primaryKeychainDeleteRequestId = QUuid::createUuid();

    // clang-format off
    primaryKeychain->setDeletePasswordHandler(
        [&deleteFromPrimaryKeychainCalled, primaryKeychainDeleteRequestId,
         expectedService=m_service, expectedKey=m_key]
        (const QString & service, const QString & key)
        {
            VERIFY_THROW(service == expectedService)
            VERIFY_THROW(key == expectedKey)

            deleteFromPrimaryKeychainCalled = true;

            return KeychainServiceMock::DeletePasswordResult{
                primaryKeychainDeleteRequestId,
                IKeychainService::ErrorCode::NoError, ErrorString()};
        });
    // clang-format on

    bool deleteFromSecondaryKeychainCalled = false;
    const auto secondaryKeychainDeleteRequestId = QUuid::createUuid();

    // clang-format off
    secondaryKeychain->setDeletePasswordHandler(
        [&deleteFromSecondaryKeychainCalled, secondaryKeychainDeleteRequestId,
         expectedService=m_service, expectedKey=m_key]
        (const QString & service, const QString & key)
        {
            VERIFY_THROW(service == expectedService)
            VERIFY_THROW(key == expectedKey)

            deleteFromSecondaryKeychainCalled = true;

            return KeychainServiceMock::DeletePasswordResult{
                secondaryKeychainDeleteRequestId,
                IKeychainService::ErrorCode::NoBackendAvailable, ErrorString()};
        });
    // clang-format on

    QUuid deleteId;
    bool deletePasswordCallbackCalled = false;

    // clang-format off
    QObject::connect(
        compositeKeychain.get(),
        &IKeychainService::deletePasswordJobFinished,
        compositeKeychain.get(),
        [&deletePasswordCallbackCalled, &deleteId]
        (QUuid requestId, IKeychainService::ErrorCode errorCode,
         ErrorString errorDescription)
        {
            deletePasswordCallbackCalled = true;
            QVERIFY(requestId == deleteId);
            QVERIFY(errorCode == IKeychainService::ErrorCode::NoError);
            QVERIFY(errorDescription.isEmpty());
        });
    // clang-format on

    QSignalSpy deleteSpy(
        compositeKeychain.get(), &IKeychainService::deletePasswordJobFinished);

    deleteId = compositeKeychain->startDeletePasswordJob(m_service, m_key);
    QVERIFY(deleteId == primaryKeychainDeleteRequestId);

    QVERIFY(deleteSpy.wait(10000));
    QVERIFY(deletePasswordCallbackCalled);
    QVERIFY(deleteFromPrimaryKeychainCalled);
    QVERIFY(deleteFromSecondaryKeychainCalled);

    // Read password attempts should not touch the secondary keychain now as
    // we have failed to delete the password from there

    bool readFromPrimaryKeychainCalled = false;
    const auto readFromPrimaryKeychainRequestId = QUuid::createUuid();

    // clang-format off
    primaryKeychain->setReadPasswordHandler(
        [&readFromPrimaryKeychainCalled, readFromPrimaryKeychainRequestId,
         expectedService=m_service, expectedKey=m_key, password=m_password]
        (const QString & service, const QString & key)
        {
            VERIFY_THROW(service == expectedService)
            VERIFY_THROW(key == expectedKey)

            readFromPrimaryKeychainCalled = true;

            return KeychainServiceMock::ReadPasswordResult{
                readFromPrimaryKeychainRequestId,
                IKeychainService::ErrorCode::EntryNotFound,
                ErrorString(),
                password};
        });
    // clang-format on

    bool readFromSecondaryKeychainCalled = false;
    const auto readFromSecondaryKeychainRequestId = QUuid::createUuid();

    // clang-format off
    secondaryKeychain->setReadPasswordHandler(
        [&readFromSecondaryKeychainCalled, readFromSecondaryKeychainRequestId,
         expectedService=m_service, expectedKey=m_key, password=m_password]
        (const QString & service, const QString & key)
        {
            VERIFY_THROW(service == expectedService)
            VERIFY_THROW(key == expectedKey)

            readFromSecondaryKeychainCalled = true;

            return KeychainServiceMock::ReadPasswordResult{
                readFromSecondaryKeychainRequestId,
                IKeychainService::ErrorCode::NoError,
                ErrorString(),
                password};
        });
    // clang-format on

    QUuid readId;
    bool readPasswordCallbackCalled = false;
    auto expectedReadErrorCode = IKeychainService::ErrorCode::EntryNotFound;

    // clang-format off
    QObject::connect(
        compositeKeychain.get(),
        &IKeychainService::readPasswordJobFinished,
        compositeKeychain.get(),
        [&readPasswordCallbackCalled, &readId, &expectedReadErrorCode]
        (QUuid requestId, IKeychainService::ErrorCode errorCode,
         ErrorString errorDescription, QString password)
        {
            Q_UNUSED(errorDescription)
            Q_UNUSED(password)

            readPasswordCallbackCalled = true;

            QVERIFY(requestId == readId);
            QVERIFY(errorCode == expectedReadErrorCode);
        });
    // clang-format on

    QSignalSpy readSpy(
        compositeKeychain.get(), &IKeychainService::readPasswordJobFinished);

    readId = compositeKeychain->startReadPasswordJob(m_service, m_key);
    QVERIFY(readId == readFromPrimaryKeychainRequestId);

    QVERIFY(readSpy.wait(10000));
    QVERIFY(readPasswordCallbackCalled);
    QVERIFY(readFromPrimaryKeychainCalled);
    QVERIFY(!readFromSecondaryKeychainCalled);

    // Successful writing to the secondary keychain should enable reading from
    // it again

    bool writeToPrimaryKeychainCalled = false;
    const auto primaryKeychainWriteRequestId = QUuid::createUuid();

    // clang-format off
    primaryKeychain->setWritePasswordHandler(
        [&writeToPrimaryKeychainCalled, primaryKeychainWriteRequestId,
         expectedService=m_service, expectedKey=m_key,
         expectedPassword=m_password]
        (const QString & service, const QString & key, const QString & password)
        {
            VERIFY_THROW(service == expectedService)
            VERIFY_THROW(key == expectedKey)
            VERIFY_THROW(password == expectedPassword)

            writeToPrimaryKeychainCalled = true;

            return KeychainServiceMock::WritePasswordResult{
                primaryKeychainWriteRequestId,
                IKeychainService::ErrorCode::NoError, ErrorString()};
        });
    // clang-format on

    bool writeToSecondaryKeychainCalled = false;
    const auto secondaryKeychainWriteRequestId = QUuid::createUuid();

    // clang-format off
    secondaryKeychain->setWritePasswordHandler(
        [&writeToSecondaryKeychainCalled, secondaryKeychainWriteRequestId,
         expectedService=m_service, expectedKey=m_key,
         expectedPassword=m_password]
        (const QString & service, const QString & key, const QString & password)
        {
            VERIFY_THROW(service == expectedService)
            VERIFY_THROW(key == expectedKey)
            VERIFY_THROW(password == expectedPassword)

            writeToSecondaryKeychainCalled = true;

            return KeychainServiceMock::WritePasswordResult{
                secondaryKeychainWriteRequestId,
                IKeychainService::ErrorCode::NoError, ErrorString()};
        });
    // clang-format on

    QSignalSpy writeSpy(
        compositeKeychain.get(), &IKeychainService::writePasswordJobFinished);

    QUuid writeId;
    bool compositeKeychainWriteCallbackCalled = false;

    // clang-format off
    QObject::connect(
        compositeKeychain.get(),
        &IKeychainService::writePasswordJobFinished,
        compositeKeychain.get(),
        [&compositeKeychainWriteCallbackCalled, &writeId]
        (QUuid requestId, IKeychainService::ErrorCode errorCode,
         ErrorString errorDescription)
        {
            compositeKeychainWriteCallbackCalled = true;
            QVERIFY(writeId == requestId);
            QVERIFY(errorCode == IKeychainService::ErrorCode::NoError);
            QVERIFY(errorDescription.isEmpty());
        });
    // clang-format on

    writeId =
        compositeKeychain->startWritePasswordJob(m_service, m_key, m_password);

    QVERIFY(writeId == primaryKeychainWriteRequestId);

    QVERIFY(writeSpy.wait(10000));
    QVERIFY(compositeKeychainWriteCallbackCalled);
    QVERIFY(writeToPrimaryKeychainCalled);
    QVERIFY(writeToSecondaryKeychainCalled);

    readFromPrimaryKeychainCalled = false;
    readFromSecondaryKeychainCalled = false;
    readPasswordCallbackCalled = false;

    // clang-format off
    secondaryKeychain->setReadPasswordHandler(
        [&readFromSecondaryKeychainCalled, readFromSecondaryKeychainRequestId,
         expectedService=m_service, expectedKey=m_key, password=m_password]
        (const QString & service, const QString & key)
        {
            VERIFY_THROW(service == expectedService)
            VERIFY_THROW(key == expectedKey)

            readFromSecondaryKeychainCalled = true;

            return KeychainServiceMock::ReadPasswordResult{
                readFromSecondaryKeychainRequestId,
                IKeychainService::ErrorCode::NoError, ErrorString(), password};
        });
    // clang-format on

    expectedReadErrorCode = IKeychainService::ErrorCode::NoError;

    readId = compositeKeychain->startReadPasswordJob(m_service, m_key);
    QVERIFY(readId == readFromPrimaryKeychainRequestId);

    QVERIFY(readSpy.wait(10000));
    QVERIFY(readPasswordCallbackCalled);
    QVERIFY(readFromPrimaryKeychainCalled);
    QVERIFY(readFromSecondaryKeychainCalled);
}

void CompositeKeychainTester::handleDeleteFromBothKeychainsErrors()
{
    const auto primaryKeychain = std::make_shared<KeychainServiceMock>();
    const auto secondaryKeychain = std::make_shared<KeychainServiceMock>();

    const auto compositeKeychain =
        newCompositeKeychainService(m_name, primaryKeychain, secondaryKeychain);

    bool deleteFromPrimaryKeychainCalled = false;
    const auto primaryKeychainDeleteRequestId = QUuid::createUuid();

    // clang-format off
    primaryKeychain->setDeletePasswordHandler(
        [&deleteFromPrimaryKeychainCalled, primaryKeychainDeleteRequestId,
         expectedService=m_service, expectedKey=m_key]
        (const QString & service, const QString & key)
        {
            VERIFY_THROW(service == expectedService)
            VERIFY_THROW(key == expectedKey)

            deleteFromPrimaryKeychainCalled = true;

            return KeychainServiceMock::DeletePasswordResult{
                primaryKeychainDeleteRequestId,
                IKeychainService::ErrorCode::CouldNotDeleteEntry,
                ErrorString()};
        });
    // clang-format on

    bool deleteFromSecondaryKeychainCalled = false;
    const auto secondaryKeychainDeleteRequestId = QUuid::createUuid();

    // clang-format off
    secondaryKeychain->setDeletePasswordHandler(
        [&deleteFromSecondaryKeychainCalled, secondaryKeychainDeleteRequestId,
         expectedService=m_service, expectedKey=m_key]
        (const QString & service, const QString & key)
        {
            VERIFY_THROW(service == expectedService)
            VERIFY_THROW(key == expectedKey)

            deleteFromSecondaryKeychainCalled = true;

            return KeychainServiceMock::DeletePasswordResult{
                secondaryKeychainDeleteRequestId,
                IKeychainService::ErrorCode::NoBackendAvailable, ErrorString()};
        });
    // clang-format on

    QUuid deleteId;
    bool deletePasswordCallbackCalled = false;

    // clang-format off
    QObject::connect(
        compositeKeychain.get(),
        &IKeychainService::deletePasswordJobFinished,
        compositeKeychain.get(),
        [&deletePasswordCallbackCalled, &deleteId]
        (QUuid requestId, IKeychainService::ErrorCode errorCode,
         ErrorString errorDescription)
        {
            deletePasswordCallbackCalled = true;
            QVERIFY(requestId == deleteId);
            QVERIFY(errorCode == IKeychainService::ErrorCode::NoError);
            QVERIFY(errorDescription.isEmpty());
        });
    // clang-format on

    QSignalSpy deleteSpy(
        compositeKeychain.get(), &IKeychainService::deletePasswordJobFinished);

    deleteId = compositeKeychain->startDeletePasswordJob(m_service, m_key);
    QVERIFY(deleteId == primaryKeychainDeleteRequestId);

    QVERIFY(deleteSpy.wait(10000));
    QVERIFY(deletePasswordCallbackCalled);
    QVERIFY(deleteFromPrimaryKeychainCalled);
    QVERIFY(deleteFromSecondaryKeychainCalled);

    // Read password attempts should not touch either keychain now as we have
    // failed to delete the password from both of them

    bool readFromPrimaryKeychainCalled = false;
    const auto readFromPrimaryKeychainRequestId = QUuid::createUuid();

    // clang-format off
    primaryKeychain->setReadPasswordHandler(
        [&readFromPrimaryKeychainCalled, readFromPrimaryKeychainRequestId,
         expectedService=m_service, expectedKey=m_key, password=m_password]
        (const QString & service, const QString & key)
        {
            VERIFY_THROW(service == expectedService)
            VERIFY_THROW(key == expectedKey)

            readFromPrimaryKeychainCalled = true;

            return KeychainServiceMock::ReadPasswordResult{
                readFromPrimaryKeychainRequestId,
                IKeychainService::ErrorCode::NoError,
                ErrorString(),
                password};
        });
    // clang-format on

    bool readFromSecondaryKeychainCalled = false;
    const auto readFromSecondaryKeychainRequestId = QUuid::createUuid();

    // clang-format off
    secondaryKeychain->setReadPasswordHandler(
        [&readFromSecondaryKeychainCalled, readFromSecondaryKeychainRequestId,
         expectedService=m_service, expectedKey=m_key, password=m_password]
        (const QString & service, const QString & key)
        {
            VERIFY_THROW(service == expectedService)
            VERIFY_THROW(key == expectedKey)

            readFromSecondaryKeychainCalled = true;

            return KeychainServiceMock::ReadPasswordResult{
                readFromSecondaryKeychainRequestId,
                IKeychainService::ErrorCode::EntryNotFound,
                ErrorString(),
                password};
        });
    // clang-format on

    QUuid readId;
    bool readPasswordCallbackCalled = false;
    auto expectedReadErrorCode = IKeychainService::ErrorCode::EntryNotFound;
    QString expectedPassword;

    // clang-format off
    QObject::connect(
        compositeKeychain.get(),
        &IKeychainService::readPasswordJobFinished,
        compositeKeychain.get(),
        [&readPasswordCallbackCalled, &readId, &expectedReadErrorCode,
         &expectedPassword]
        (QUuid requestId, IKeychainService::ErrorCode errorCode,
         ErrorString errorDescription, QString password)
        {
            Q_UNUSED(errorDescription)

            readPasswordCallbackCalled = true;
            QVERIFY(requestId == readId);
            QVERIFY(errorCode == expectedReadErrorCode);
            QVERIFY(password == expectedPassword);
        });
    // clang-format on

    QSignalSpy readSpy(
        compositeKeychain.get(), &IKeychainService::readPasswordJobFinished);

    readId = compositeKeychain->startReadPasswordJob(m_service, m_key);
    QVERIFY(readId != readFromPrimaryKeychainRequestId);
    QVERIFY(readId != readFromSecondaryKeychainRequestId);

    QVERIFY(readSpy.wait(10000));
    QVERIFY(readPasswordCallbackCalled);
    QVERIFY(!readFromPrimaryKeychainCalled);
    QVERIFY(!readFromSecondaryKeychainCalled);

    // Successful writing to the primary keychain should enable reading from it
    // again

    bool writeToPrimaryKeychainCalled = false;
    const auto primaryKeychainWriteRequestId = QUuid::createUuid();

    // clang-format off
    primaryKeychain->setWritePasswordHandler(
        [&writeToPrimaryKeychainCalled, primaryKeychainWriteRequestId,
         expectedService=m_service, expectedKey=m_key,
         expectedPassword=m_password]
        (const QString & service, const QString & key, const QString & password)
        {
            VERIFY_THROW(service == expectedService)
            VERIFY_THROW(key == expectedKey)
            VERIFY_THROW(password == expectedPassword)

            writeToPrimaryKeychainCalled = true;

            return KeychainServiceMock::WritePasswordResult{
                primaryKeychainWriteRequestId,
                IKeychainService::ErrorCode::NoError, ErrorString()};
        });
    // clang-format on

    bool writeToSecondaryKeychainCalled = false;
    const auto secondaryKeychainWriteRequestId = QUuid::createUuid();

    // clang-format off
    secondaryKeychain->setWritePasswordHandler(
        [&writeToSecondaryKeychainCalled, secondaryKeychainWriteRequestId,
         expectedService=m_service, expectedKey=m_key,
         expectedPassword=m_password]
        (const QString & service, const QString & key, const QString & password)
        {
            VERIFY_THROW(service == expectedService)
            VERIFY_THROW(key == expectedKey)
            VERIFY_THROW(password == expectedPassword)

            writeToSecondaryKeychainCalled = true;

            return KeychainServiceMock::WritePasswordResult{
                secondaryKeychainWriteRequestId,
                IKeychainService::ErrorCode::NoError, ErrorString()};
        });
    // clang-format on

    QSignalSpy writeSpy(
        compositeKeychain.get(), &IKeychainService::writePasswordJobFinished);

    QUuid writeId;
    bool compositeKeychainWriteCallbackCalled = false;

    // clang-format off
    QObject::connect(
        compositeKeychain.get(),
        &IKeychainService::writePasswordJobFinished,
        compositeKeychain.get(),
        [&compositeKeychainWriteCallbackCalled, &writeId]
        (QUuid requestId, IKeychainService::ErrorCode errorCode,
         ErrorString errorDescription)
        {
            compositeKeychainWriteCallbackCalled = true;
            QVERIFY(writeId == requestId);
            QVERIFY(errorCode == IKeychainService::ErrorCode::NoError);
            QVERIFY(errorDescription.isEmpty());
        });
    // clang-format on

    writeId =
        compositeKeychain->startWritePasswordJob(m_service, m_key, m_password);

    QVERIFY(writeId == primaryKeychainWriteRequestId);

    QVERIFY(writeSpy.wait(10000));
    QVERIFY(compositeKeychainWriteCallbackCalled);
    QVERIFY(writeToPrimaryKeychainCalled);
    QVERIFY(writeToSecondaryKeychainCalled);

    readFromPrimaryKeychainCalled = false;
    readFromSecondaryKeychainCalled = false;
    readPasswordCallbackCalled = false;

    // clang-format off
    primaryKeychain->setReadPasswordHandler(
        [&readFromPrimaryKeychainCalled, readFromPrimaryKeychainRequestId,
         expectedService=m_service, expectedKey=m_key, password=m_password]
        (const QString & service, const QString & key)
        {
            VERIFY_THROW(service == expectedService)
            VERIFY_THROW(key == expectedKey)

            readFromPrimaryKeychainCalled = true;

            return KeychainServiceMock::ReadPasswordResult{
                readFromPrimaryKeychainRequestId,
                IKeychainService::ErrorCode::NoError, ErrorString(), password};
        });
    // clang-format on

    expectedReadErrorCode = IKeychainService::ErrorCode::NoError;
    expectedPassword = m_password;

    readId = compositeKeychain->startReadPasswordJob(m_service, m_key);
    QVERIFY(readId == readFromPrimaryKeychainRequestId);

    QVERIFY(readSpy.wait(10000));
    QVERIFY(readPasswordCallbackCalled);
    QVERIFY(readFromPrimaryKeychainCalled);
    QVERIFY(!readFromSecondaryKeychainCalled);
}

void CompositeKeychainTester::cleanup()
{
    // Remove persistence used by CompositeKeychainService from previous
    // test invokation
    ApplicationSettings settings{m_name};
    settings.beginGroup(m_service + QStringLiteral("/") + m_key);
    settings.remove(QStringLiteral(""));
    settings.endGroup();
}

} // namespace test
} // namespace quentier
