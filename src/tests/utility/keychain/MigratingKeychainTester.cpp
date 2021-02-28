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

#include "MigratingKeychainTester.h"
#include "KeychainServiceMock.h"

#include "../../TestMacros.h"

#include <quentier/utility/IKeychainService.h>

#include <QSignalSpy>
#include <QtTest/QTest>

#include <stdexcept>

namespace quentier {
namespace test {

MigratingKeychainTester::MigratingKeychainTester(QObject * parent) :
    QObject(parent)
{}

void MigratingKeychainTester::throwExceptionWhenGivenNullSourceKeychain()
{
    QVERIFY_EXCEPTION_THROWN(
        const auto migratingKeychain = newMigratingKeychainService(
            nullptr, std::make_shared<KeychainServiceMock>()),
        std::invalid_argument);
}

void MigratingKeychainTester::throwExceptionWhenGivenNullSinkKeychain()
{
    QVERIFY_EXCEPTION_THROWN(
        const auto migratingKeychain = newMigratingKeychainService(
            std::make_shared<KeychainServiceMock>(), nullptr),
        std::invalid_argument);
}

void MigratingKeychainTester::writePasswordToSinkKeychainOnly()
{
    const auto sourceKeychain = std::make_shared<KeychainServiceMock>();
    const auto sinkKeychain = std::make_shared<KeychainServiceMock>();

    const auto migratingKeychain =
        newMigratingKeychainService(sourceKeychain, sinkKeychain);

    bool writeToSourceKeychainCalled = false;
    const auto writeToSourceKeychainRequestId = QUuid::createUuid();

    // clang-format off
    sourceKeychain->setWritePasswordHandler(
        [&writeToSourceKeychainCalled, writeToSourceKeychainRequestId,
         expectedService=m_service, expectedKey=m_key,
         expectedPassword=m_password]
        (const QString & service, const QString & key, const QString & password)
        {
            VERIFY_THROW(service == expectedService)
            VERIFY_THROW(key == expectedKey)
            VERIFY_THROW(password == expectedPassword)

            writeToSourceKeychainCalled = true;

            return KeychainServiceMock::WritePasswordResult{
                writeToSourceKeychainRequestId,
                IKeychainService::ErrorCode::NoError, ErrorString()};
        });
    // clang-format off

    bool writeToSinkKeychainCalled = false;
    const auto writeToSinkKeychainRequestId = QUuid::createUuid();

    // clang-format off
    sinkKeychain->setWritePasswordHandler(
        [&writeToSinkKeychainCalled, writeToSinkKeychainRequestId,
         expectedService=m_service, expectedKey=m_key,
         expectedPassword=m_password]
        (const QString & service, const QString & key, const QString & password)
        {
            VERIFY_THROW(service == expectedService)
            VERIFY_THROW(key == expectedKey)
            VERIFY_THROW(password == expectedPassword)

            writeToSinkKeychainCalled = true;

            return KeychainServiceMock::WritePasswordResult{
                writeToSinkKeychainRequestId,
                IKeychainService::ErrorCode::NoError, ErrorString()};
        });
    // clang-format off

    QSignalSpy spy(
        migratingKeychain.get(), &IKeychainService::writePasswordJobFinished);

    QUuid id;
    bool migratingKeychainCallbackCalled = false;

    // clang-format off
    QObject::connect(
        migratingKeychain.get(),
        &IKeychainService::writePasswordJobFinished,
        migratingKeychain.get(),
        [&migratingKeychainCallbackCalled, &id]
        (QUuid requestId, IKeychainService::ErrorCode errorCode,
         ErrorString errorDescription)
        {
            migratingKeychainCallbackCalled = true;
            QVERIFY(id == requestId);
            QVERIFY(errorCode == IKeychainService::ErrorCode::NoError);
            QVERIFY(errorDescription.isEmpty());
        });
    // clang-format on

    id = migratingKeychain->startWritePasswordJob(m_service, m_key, m_password);
    QVERIFY(id == writeToSinkKeychainRequestId);

    QVERIFY(spy.wait(10000));
    QVERIFY(migratingKeychainCallbackCalled);
    QVERIFY(writeToSinkKeychainCalled);
    QVERIFY(!writeToSourceKeychainCalled);
}

void MigratingKeychainTester::readPasswordFromSinkKeychainFirst()
{
    const auto sourceKeychain = std::make_shared<KeychainServiceMock>();
    const auto sinkKeychain = std::make_shared<KeychainServiceMock>();

    const auto migratingKeychain =
        newMigratingKeychainService(sourceKeychain, sinkKeychain);

    bool readFromSourceKeychainCalled = false;
    const auto readFromSourceKeychainRequestId = QUuid::createUuid();

    // clang-format off
    sourceKeychain->setReadPasswordHandler(
        [&readFromSourceKeychainCalled, readFromSourceKeychainRequestId,
         expectedService=m_service, expectedKey=m_key, password=m_password]
        (const QString & service, const QString & key)
        {
            VERIFY_THROW(service == expectedService)
            VERIFY_THROW(key == expectedKey)

            readFromSourceKeychainCalled = true;

            return KeychainServiceMock::ReadPasswordResult{
                readFromSourceKeychainRequestId,
                IKeychainService::ErrorCode::NoError,
                ErrorString(),
                password};
        });
    // clang-format on

    bool readFromSinkKeychainCalled = false;
    const auto readFromSinkKeychainRequestId = QUuid::createUuid();

    // clang-format off
    sinkKeychain->setReadPasswordHandler(
        [&readFromSinkKeychainCalled, readFromSinkKeychainRequestId,
         expectedService=m_service, expectedKey=m_key, password=m_password]
        (const QString & service, const QString & key)
        {
            VERIFY_THROW(service == expectedService)
            VERIFY_THROW(key == expectedKey)

            readFromSinkKeychainCalled = true;

            return KeychainServiceMock::ReadPasswordResult{
                readFromSinkKeychainRequestId,
                IKeychainService::ErrorCode::NoError,
                ErrorString(),
                password};
        });
    // clang-format on

    QUuid id;
    bool readPasswordCallbackCalled = false;

    // clang-format off
    QObject::connect(
        migratingKeychain.get(),
        &IKeychainService::readPasswordJobFinished,
        migratingKeychain.get(),
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
        migratingKeychain.get(), &IKeychainService::readPasswordJobFinished);

    id = migratingKeychain->startReadPasswordJob(m_service, m_key);
    QVERIFY(id == readFromSinkKeychainRequestId);

    QVERIFY(readSpy.wait(10000));
    QVERIFY(readPasswordCallbackCalled);
    QVERIFY(!readFromSourceKeychainCalled);
    QVERIFY(readFromSinkKeychainCalled);
}

void MigratingKeychainTester::readPasswordFromSourceKeychainAsFallback()
{
    const auto sourceKeychain = std::make_shared<KeychainServiceMock>();
    const auto sinkKeychain = std::make_shared<KeychainServiceMock>();

    const auto migratingKeychain =
        newMigratingKeychainService(sourceKeychain, sinkKeychain);

    bool readFromSourceKeychainCalled = false;
    const auto readFromSourceKeychainRequestId = QUuid::createUuid();

    bool readFromSinkKeychainCalled = false;
    const auto readFromSinkKeychainRequestId = QUuid::createUuid();

    // clang-format off
    sourceKeychain->setReadPasswordHandler(
        [&readFromSourceKeychainCalled, &readFromSinkKeychainCalled,
         readFromSourceKeychainRequestId, expectedService=m_service,
         expectedKey=m_key, password=m_password]
        (const QString & service, const QString & key)
        {
            VERIFY_THROW(service == expectedService)
            VERIFY_THROW(key == expectedKey)

            VERIFY_THROW(readFromSinkKeychainCalled);
            readFromSourceKeychainCalled = true;

            return KeychainServiceMock::ReadPasswordResult{
                readFromSourceKeychainRequestId,
                IKeychainService::ErrorCode::NoError,
                ErrorString(),
                password};
        });
    // clang-format on

    // clang-format off
    sinkKeychain->setReadPasswordHandler(
        [&readFromSinkKeychainCalled, &readFromSourceKeychainCalled,
         readFromSinkKeychainRequestId, expectedService=m_service,
         expectedKey=m_key, password=m_password]
        (const QString & service, const QString & key)
        {
            VERIFY_THROW(service == expectedService)
            VERIFY_THROW(key == expectedKey)

            VERIFY_THROW(!readFromSourceKeychainCalled);
            readFromSinkKeychainCalled = true;

            return KeychainServiceMock::ReadPasswordResult{
                readFromSinkKeychainRequestId,
                IKeychainService::ErrorCode::EntryNotFound,
                ErrorString(),
                password};
        });
    // clang-format on

    bool writeToSinkKeychainCalled = false;
    const auto writeToSinkKeychainRequestId = QUuid::createUuid();

    bool deleteFromSourceKeychainCalled = false;
    const auto deleteFromSourceKeychainRequestId = QUuid::createUuid();

    // clang-format off
    sinkKeychain->setWritePasswordHandler(
        [&writeToSinkKeychainCalled, &deleteFromSourceKeychainCalled,
         writeToSinkKeychainRequestId, expectedService=m_service,
         expectedKey=m_key, expectedPassword=m_password]
        (const QString & service, const QString & key, const QString & password)
        {
            VERIFY_THROW(service == expectedService)
            VERIFY_THROW(key == expectedKey)
            VERIFY_THROW(password == expectedPassword)

            VERIFY_THROW(!deleteFromSourceKeychainCalled);
            writeToSinkKeychainCalled = true;

            return KeychainServiceMock::WritePasswordResult{
                writeToSinkKeychainRequestId,
                IKeychainService::ErrorCode::NoError, ErrorString()};
        });
    // clang-format off

    // clang-format off
    sourceKeychain->setDeletePasswordHandler(
        [&deleteFromSourceKeychainCalled, &writeToSinkKeychainCalled,
         deleteFromSourceKeychainRequestId, expectedService=m_service,
         expectedKey=m_key]
        (const QString & service, const QString & key)
        {
            VERIFY_THROW(service == expectedService)
            VERIFY_THROW(key == expectedKey)

            VERIFY_THROW(writeToSinkKeychainCalled);
            deleteFromSourceKeychainCalled = true;

            return KeychainServiceMock::DeletePasswordResult{
                deleteFromSourceKeychainRequestId,
                IKeychainService::ErrorCode::NoError, ErrorString()};
        });
    // clang-format off

    QUuid id;
    bool readPasswordCallbackCalled = false;

    // clang-format off
    QObject::connect(
        migratingKeychain.get(),
        &IKeychainService::readPasswordJobFinished,
        migratingKeychain.get(),
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
        migratingKeychain.get(), &IKeychainService::readPasswordJobFinished);

    QSignalSpy sinkWriteSpy(
        sinkKeychain.get(), &IKeychainService::writePasswordJobFinished);

    QSignalSpy sourceDeleteSpy(
        sourceKeychain.get(), &IKeychainService::deletePasswordJobFinished);

    id = migratingKeychain->startReadPasswordJob(m_service, m_key);
    QVERIFY(id == readFromSinkKeychainRequestId);

    QVERIFY(readSpy.wait(10000));
    QVERIFY(readPasswordCallbackCalled);
    QVERIFY(readFromSinkKeychainCalled);
    QVERIFY(readFromSourceKeychainCalled);

    QVERIFY(sinkWriteSpy.wait(10000));
    QVERIFY(writeToSinkKeychainCalled);

    QVERIFY(sourceDeleteSpy.wait(10000));
    QVERIFY(deleteFromSourceKeychainCalled);
}

void MigratingKeychainTester::dontFallbackReadOnSeriousSinkKeychainError()
{
    const auto sourceKeychain = std::make_shared<KeychainServiceMock>();
    const auto sinkKeychain = std::make_shared<KeychainServiceMock>();

    const auto migratingKeychain =
        newMigratingKeychainService(sourceKeychain, sinkKeychain);

    bool readFromSourceKeychainCalled = false;
    const auto readFromSourceKeychainRequestId = QUuid::createUuid();

    bool readFromSinkKeychainCalled = false;
    const auto readFromSinkKeychainRequestId = QUuid::createUuid();

    // clang-format off
    sourceKeychain->setReadPasswordHandler(
        [&readFromSourceKeychainCalled, &readFromSinkKeychainCalled,
         readFromSourceKeychainRequestId, expectedService=m_service,
         expectedKey=m_key, password=m_password]
        (const QString & service, const QString & key)
        {
            VERIFY_THROW(service == expectedService)
            VERIFY_THROW(key == expectedKey)

            VERIFY_THROW(readFromSinkKeychainCalled);
            readFromSourceKeychainCalled = true;

            return KeychainServiceMock::ReadPasswordResult{
                readFromSourceKeychainRequestId,
                IKeychainService::ErrorCode::NoError,
                ErrorString(),
                password};
        });
    // clang-format on

    // clang-format off
    sinkKeychain->setReadPasswordHandler(
        [&readFromSinkKeychainCalled, &readFromSourceKeychainCalled,
         readFromSinkKeychainRequestId, expectedService=m_service,
         expectedKey=m_key, password=m_password]
        (const QString & service, const QString & key)
        {
            VERIFY_THROW(service == expectedService)
            VERIFY_THROW(key == expectedKey)

            VERIFY_THROW(!readFromSourceKeychainCalled);
            readFromSinkKeychainCalled = true;

            return KeychainServiceMock::ReadPasswordResult{
                readFromSinkKeychainRequestId,
                IKeychainService::ErrorCode::AccessDenied,
                ErrorString(),
                password};
        });
    // clang-format on

    QUuid id;
    bool readPasswordCallbackCalled = false;

    // clang-format off
    QObject::connect(
        migratingKeychain.get(),
        &IKeychainService::readPasswordJobFinished,
        migratingKeychain.get(),
        [&readPasswordCallbackCalled, &id]
        (QUuid requestId, IKeychainService::ErrorCode errorCode,
         ErrorString errorDescription, QString password)
        {
            Q_UNUSED(password)
            Q_UNUSED(errorDescription)
            readPasswordCallbackCalled = true;
            QVERIFY(requestId == id);
            QVERIFY(errorCode == IKeychainService::ErrorCode::AccessDenied);
        });
    // clang-format on

    QSignalSpy readSpy(
        migratingKeychain.get(), &IKeychainService::readPasswordJobFinished);

    id = migratingKeychain->startReadPasswordJob(m_service, m_key);
    QVERIFY(id == readFromSinkKeychainRequestId);

    QVERIFY(readSpy.wait(10000));
    QVERIFY(readPasswordCallbackCalled);
    QVERIFY(readFromSinkKeychainCalled);
    QVERIFY(!readFromSourceKeychainCalled);
}

void MigratingKeychainTester::attemptToDeletePasswordFromBothKeychains()
{
    const auto sourceKeychain = std::make_shared<KeychainServiceMock>();
    const auto sinkKeychain = std::make_shared<KeychainServiceMock>();

    const auto migratingKeychain =
        newMigratingKeychainService(sourceKeychain, sinkKeychain);

    bool deleteFromSourceKeychainCalled = false;
    const auto deleteFromSourceKeychainRequestId = QUuid::createUuid();

    auto deleteFromSourceKeychainExpectedErrorCode =
        IKeychainService::ErrorCode::NoError;

    // clang-format off
    sourceKeychain->setDeletePasswordHandler(
        [&deleteFromSourceKeychainCalled,
         &deleteFromSourceKeychainExpectedErrorCode,
         deleteFromSourceKeychainRequestId, expectedService=m_service,
         expectedKey=m_key]
        (const QString & service, const QString & key)
        {
            VERIFY_THROW(service == expectedService)
            VERIFY_THROW(key == expectedKey)

            deleteFromSourceKeychainCalled = true;

            return KeychainServiceMock::DeletePasswordResult{
                deleteFromSourceKeychainRequestId,
                deleteFromSourceKeychainExpectedErrorCode,
                ErrorString()};
        });
    // clang-format on

    bool deleteFromSinkKeychainCalled = false;
    const auto deleteFromSinkKeychainRequestId = QUuid::createUuid();

    auto deleteFromSinkKeychainExpectedErrorCode =
        IKeychainService::ErrorCode::NoError;

    // clang-format off
    sinkKeychain->setDeletePasswordHandler(
        [&deleteFromSinkKeychainCalled,
         &deleteFromSinkKeychainExpectedErrorCode,
         deleteFromSinkKeychainRequestId, expectedService=m_service,
         expectedKey=m_key]
        (const QString & service, const QString & key)
        {
            VERIFY_THROW(service == expectedService)
            VERIFY_THROW(key == expectedKey)

            deleteFromSinkKeychainCalled = true;

            return KeychainServiceMock::DeletePasswordResult{
                deleteFromSinkKeychainRequestId,
                deleteFromSinkKeychainExpectedErrorCode,
                ErrorString()};
        });
    // clang-format on

    QUuid id;
    bool deletePasswordCallbackCalled = false;

    auto deletePasswordExpectedErrorCode = IKeychainService::ErrorCode::NoError;

    // clang-format off
    QObject::connect(
        migratingKeychain.get(),
        &IKeychainService::deletePasswordJobFinished,
        migratingKeychain.get(),
        [&deletePasswordCallbackCalled, &id, &deletePasswordExpectedErrorCode]
        (QUuid requestId, IKeychainService::ErrorCode errorCode,
         ErrorString errorDescription)
        {
            Q_UNUSED(errorDescription)
            deletePasswordCallbackCalled = true;
            QVERIFY(requestId == id);
            QVERIFY(errorCode == deletePasswordExpectedErrorCode);
        });

    QSignalSpy deleteSpy(
        migratingKeychain.get(), &IKeychainService::deletePasswordJobFinished);

    id = migratingKeychain->startDeletePasswordJob(m_service, m_key);
    QVERIFY(id == deleteFromSinkKeychainRequestId);

    QVERIFY(deleteSpy.wait(10000));
    QVERIFY(deletePasswordCallbackCalled);
    QVERIFY(deleteFromSinkKeychainCalled);
    QVERIFY(deleteFromSourceKeychainCalled);

    deletePasswordCallbackCalled = false;
    deleteFromSinkKeychainCalled = false;
    deleteFromSourceKeychainCalled = false;

    deleteFromSinkKeychainExpectedErrorCode =
        IKeychainService::ErrorCode::EntryNotFound;

    deleteFromSourceKeychainExpectedErrorCode =
        IKeychainService::ErrorCode::NoError;

    deletePasswordExpectedErrorCode =
        IKeychainService::ErrorCode::NoError;

    id = migratingKeychain->startDeletePasswordJob(m_service, m_key);
    QVERIFY(id == deleteFromSinkKeychainRequestId);

    QVERIFY(deleteSpy.wait(10000));
    QVERIFY(deletePasswordCallbackCalled);
    QVERIFY(deleteFromSinkKeychainCalled);
    QVERIFY(deleteFromSourceKeychainCalled);

    deletePasswordCallbackCalled = false;
    deleteFromSinkKeychainCalled = false;
    deleteFromSourceKeychainCalled = false;

    deleteFromSinkKeychainExpectedErrorCode =
        IKeychainService::ErrorCode::EntryNotFound;

    deleteFromSourceKeychainExpectedErrorCode =
        IKeychainService::ErrorCode::AccessDenied;

    deletePasswordExpectedErrorCode =
        IKeychainService::ErrorCode::EntryNotFound;

    id = migratingKeychain->startDeletePasswordJob(m_service, m_key);
    QVERIFY(id == deleteFromSinkKeychainRequestId);

    QVERIFY(deleteSpy.wait(10000));
    QVERIFY(deletePasswordCallbackCalled);
    QVERIFY(deleteFromSinkKeychainCalled);
    QVERIFY(deleteFromSourceKeychainCalled);

    deletePasswordCallbackCalled = false;
    deleteFromSinkKeychainCalled = false;
    deleteFromSourceKeychainCalled = false;

    deleteFromSinkKeychainExpectedErrorCode =
        IKeychainService::ErrorCode::CouldNotDeleteEntry;

    deleteFromSourceKeychainExpectedErrorCode =
        IKeychainService::ErrorCode::NoError;

    deletePasswordExpectedErrorCode =
        IKeychainService::ErrorCode::CouldNotDeleteEntry;

    id = migratingKeychain->startDeletePasswordJob(m_service, m_key);
    QVERIFY(id == deleteFromSinkKeychainRequestId);

    QVERIFY(deleteSpy.wait(10000));
    QVERIFY(deletePasswordCallbackCalled);
    QVERIFY(deleteFromSinkKeychainCalled);
    QVERIFY(deleteFromSourceKeychainCalled);
}

} // namespace test
} // namespace quentier
