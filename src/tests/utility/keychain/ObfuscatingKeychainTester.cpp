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

#include "ObfuscatingKeychainTester.h"

#include <quentier/utility/ApplicationSettings.h>
#include <quentier/utility/IKeychainService.h>

#include <QSignalSpy>
#include <QtTest/QTest>

namespace quentier {
namespace test {

ObfuscatingKeychainTester::ObfuscatingKeychainTester(QObject * parent) :
    QObject(parent)
{}

void ObfuscatingKeychainTester::checkWriteReadPassword()
{
    const auto keychain = newObfuscatingKeychainService();

    QSignalSpy writeSpy(
        keychain.get(), &IKeychainService::writePasswordJobFinished);

    keychain->startWritePasswordJob(m_service, m_key, m_password);
    QVERIFY(writeSpy.wait(10000));

    ApplicationSettings settings{QStringLiteral("obfuscatingKeychainStorage")};
    settings.beginGroup(settingsGroupName());

    QString cipher = settings.value("Cipher").toString();
    QVERIFY(cipher == QStringLiteral("AES"));

    bool conversionResult = false;
    size_t keyLength =
        settings.value("KeyLength").toULongLong(&conversionResult);
    QVERIFY(conversionResult);
    QVERIFY(keyLength == 128);

    QString value = settings.value("Value").toString();
    QVERIFY(!value.isEmpty());
    QVERIFY(value != m_password);

    settings.endGroup();

    bool callbackCalled = false;
    QObject::connect(
        keychain.get(), &IKeychainService::readPasswordJobFinished,
        keychain.get(),
        [password = m_password, &callbackCalled](
            QUuid requestId, IKeychainService::ErrorCode errorCode,
            ErrorString errorDescription, QString readPassword) {
            Q_UNUSED(requestId)

            QVERIFY(errorCode == IKeychainService::ErrorCode::NoError);
            QVERIFY(errorDescription.isEmpty());
            QVERIFY(readPassword == password);

            callbackCalled = true;
        });

    QSignalSpy readSpy(
        keychain.get(), &IKeychainService::readPasswordJobFinished);

    keychain->startReadPasswordJob(m_service, m_key);
    QVERIFY(readSpy.wait(10000));
    QVERIFY(callbackCalled);
}

void ObfuscatingKeychainTester::checkWriteDeletePassword()
{
    const auto keychain = newObfuscatingKeychainService();

    QSignalSpy writeSpy(
        keychain.get(), &IKeychainService::writePasswordJobFinished);

    keychain->startWritePasswordJob(m_service, m_key, m_password);
    QVERIFY(writeSpy.wait(10000));

    ApplicationSettings settings{QStringLiteral("obfuscatingKeychainStorage")};
    settings.beginGroup(settingsGroupName());
    QVERIFY(settings.contains("Cipher"));
    QVERIFY(settings.contains("KeyLength"));
    QVERIFY(settings.contains("Value"));
    settings.endGroup();

    bool callbackCalled = false;
    QObject::connect(
        keychain.get(), &IKeychainService::deletePasswordJobFinished,
        keychain.get(),
        [&callbackCalled](
            QUuid requestId, IKeychainService::ErrorCode errorCode,
            ErrorString errorDescription) {
            Q_UNUSED(requestId)

            QVERIFY(errorCode == IKeychainService::ErrorCode::NoError);
            QVERIFY(errorDescription.isEmpty());

            callbackCalled = true;
        });

    QSignalSpy deleteSpy(
        keychain.get(), &IKeychainService::deletePasswordJobFinished);

    keychain->startDeletePasswordJob(m_service, m_key);
    QVERIFY(deleteSpy.wait(10000));
    QVERIFY(callbackCalled);

    settings.beginGroup(settingsGroupName());
    QVERIFY(!settings.contains("Cipher"));
    QVERIFY(!settings.contains("KeyLength"));
    QVERIFY(!settings.contains("Value"));
    settings.endGroup();
}

void ObfuscatingKeychainTester::checkDeletePasswordWithoutWriting()
{
    const auto keychain = newObfuscatingKeychainService();

    bool callbackCalled = false;
    QObject::connect(
        keychain.get(), &IKeychainService::deletePasswordJobFinished,
        keychain.get(),
        [&callbackCalled](
            QUuid requestId, IKeychainService::ErrorCode errorCode,
            ErrorString errorDescription) {
            Q_UNUSED(requestId)

            QVERIFY(errorCode == IKeychainService::ErrorCode::EntryNotFound);
            QVERIFY(!errorDescription.isEmpty());

            callbackCalled = true;
        });

    QSignalSpy deleteSpy(
        keychain.get(), &IKeychainService::deletePasswordJobFinished);

    keychain->startDeletePasswordJob(m_service, m_key);
    QVERIFY(deleteSpy.wait(10000));
    QVERIFY(callbackCalled);
}

QString ObfuscatingKeychainTester::settingsGroupName() const
{
    return m_service + QStringLiteral("/") + m_key;
}

} // namespace test
} // namespace quentier
