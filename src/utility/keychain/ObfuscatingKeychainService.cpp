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

#include "ObfuscatingKeychainService.h"

#include <quentier/utility/ApplicationSettings.h>

#include <QMetaObject>

namespace quentier {

namespace keys {

constexpr const char * cipher = "Cipher";
constexpr const char * keyLength = "KeyLength";
constexpr const char * value = "Value";

} // namespace keys

namespace {

constexpr const char * settingsFileName = "obfuscatingKeychainStorage";

} // namespace

////////////////////////////////////////////////////////////////////////////////

ObfuscatingKeychainService::ObfuscatingKeychainService(QObject * parent) :
    IKeychainService(parent)
{}

ObfuscatingKeychainService::~ObfuscatingKeychainService() {}

QUuid ObfuscatingKeychainService::startWritePasswordJob(
    const QString & service, const QString & key, const QString & password)
{
    QUuid requestId = QUuid::createUuid();

    ErrorString errorDescription;
    QString encryptedString;
    QString cipher;
    size_t keyLength = 0;

    bool res = m_encryptionManager.encrypt(
        password, key, cipher, keyLength, encryptedString, errorDescription);

    if (!res) {
        QMetaObject::invokeMethod(
            this, "writePasswordJobFinished", Qt::QueuedConnection,
            Q_ARG(QUuid, requestId), Q_ARG(ErrorCode, ErrorCode::OtherError),
            Q_ARG(ErrorString, errorDescription));

        return requestId;
    }

    ApplicationSettings obfuscatedKeychainStorage{
        QString::fromUtf8(settingsFileName)};

    obfuscatedKeychainStorage.beginGroup(service + QStringLiteral("/") + key);
    obfuscatedKeychainStorage.setValue(keys::cipher, cipher);

    obfuscatedKeychainStorage.setValue(
        keys::keyLength, QVariant::fromValue(keyLength));

    obfuscatedKeychainStorage.setValue(
        keys::value, encryptedString.toUtf8().toBase64());

    obfuscatedKeychainStorage.endGroup();
    obfuscatedKeychainStorage.sync();

    QMetaObject::invokeMethod(
        this, "writePasswordJobFinished", Qt::QueuedConnection,
        Q_ARG(QUuid, requestId), Q_ARG(ErrorCode, ErrorCode::NoError),
        Q_ARG(ErrorString, ErrorString()));

    return requestId;
}

QUuid ObfuscatingKeychainService::startReadPasswordJob(
    const QString & service, const QString & key)
{
    QUuid requestId = QUuid::createUuid();

    ApplicationSettings obfuscatedKeychainStorage{
        QString::fromUtf8(settingsFileName)};

    obfuscatedKeychainStorage.beginGroup(service + QStringLiteral("/") + key);
    QString cipher = obfuscatedKeychainStorage.value(keys::cipher).toString();

    size_t keyLength = 0;
    bool conversionResult = false;
    keyLength = obfuscatedKeychainStorage.value(keys::keyLength)
                    .toULongLong(&conversionResult);
    if (!conversionResult) {
        QMetaObject::invokeMethod(
            this, "readPasswordJobFinished", Qt::QueuedConnection,
            Q_ARG(QUuid, requestId), Q_ARG(ErrorCode, ErrorCode::EntryNotFound),
            Q_ARG(
                ErrorString,
                ErrorString(QT_TR_NOOP(
                    "could not convert key length to unsigned long"))),
            Q_ARG(QString, QString()));

        return requestId;
    }

    QString encryptedText = QString::fromUtf8(QByteArray::fromBase64(
        obfuscatedKeychainStorage.value(keys::value).toByteArray()));

    obfuscatedKeychainStorage.endGroup();

    QString decryptedText;
    ErrorString errorDescription;
    bool res = m_encryptionManager.decrypt(
        encryptedText, key, cipher, keyLength, decryptedText, errorDescription);

    if (!res) {
        QMetaObject::invokeMethod(
            this, "readPasswordJobFinished", Qt::QueuedConnection,
            Q_ARG(QUuid, requestId), Q_ARG(ErrorCode, ErrorCode::OtherError),
            Q_ARG(
                ErrorString, ErrorString(QT_TR_NOOP("failed to decrypt text"))),
            Q_ARG(QString, QString()));

        return requestId;
    }

    QMetaObject::invokeMethod(
        this, "readPasswordJobFinished", Qt::QueuedConnection,
        Q_ARG(QUuid, requestId), Q_ARG(ErrorCode, ErrorCode::NoError),
        Q_ARG(ErrorString, ErrorString()), Q_ARG(QString, decryptedText));

    return requestId;
}

QUuid ObfuscatingKeychainService::startDeletePasswordJob(
    const QString & service, const QString & key)
{
    QUuid requestId = QUuid::createUuid();

    ApplicationSettings obfuscatedKeychainStorage{
        QString::fromUtf8(settingsFileName)};

    obfuscatedKeychainStorage.beginGroup(service + QStringLiteral("/") + key);

    if (obfuscatedKeychainStorage.allKeys().isEmpty()) {
        QMetaObject::invokeMethod(
            this, "deletePasswordJobFinished", Qt::QueuedConnection,
            Q_ARG(QUuid, requestId), Q_ARG(ErrorCode, ErrorCode::EntryNotFound),
            Q_ARG(
                ErrorString,
                ErrorString(QT_TR_NOOP("could not find entry to delete"))));

        obfuscatedKeychainStorage.endGroup();
        return requestId;
    }

    obfuscatedKeychainStorage.remove(QStringLiteral(""));
    obfuscatedKeychainStorage.endGroup();

    QMetaObject::invokeMethod(
        this, "deletePasswordJobFinished", Qt::QueuedConnection,
        Q_ARG(QUuid, requestId), Q_ARG(ErrorCode, ErrorCode::NoError),
        Q_ARG(ErrorString, ErrorString()));

    return requestId;
}

} // namespace quentier
