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

#include "ObfuscatingStorage.h"

#include <quentier/utility/ApplicationSettings.h>

#include <QMetaObject>

namespace quentier {

namespace keys {

constexpr const char * cipher = "Cipher";
constexpr const char * keyLength = "KeyLength";
constexpr const char * value = "Value";

} // namespace keys

namespace {

constexpr const char * settingsFileName = "obfuscatedDataStorage";

} // namespace

////////////////////////////////////////////////////////////////////////////////

ObfuscatingStorage::ObfuscatingStorage(QObject * parent) :
    IKeychainService(parent)
{}

ObfuscatingStorage::~ObfuscatingStorage() {}

QUuid ObfuscatingStorage::startWritePasswordJob(
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

    ApplicationSettings obfuscatedStorageSettings{
        QString::fromUtf8(settingsFileName)};

    obfuscatedStorageSettings.beginGroup(service + QStringLiteral("/") + key);
    obfuscatedStorageSettings.setValue(keys::cipher, cipher);

    obfuscatedStorageSettings.setValue(
        keys::keyLength, QVariant::fromValue(keyLength));

    obfuscatedStorageSettings.setValue(
        keys::value, encryptedString.toUtf8().toBase64());

    obfuscatedStorageSettings.endGroup();

    QMetaObject::invokeMethod(
        this, "writePasswordJobFinished", Qt::QueuedConnection,
        Q_ARG(QUuid, requestId), Q_ARG(ErrorCode, ErrorCode::NoError),
        Q_ARG(ErrorString, ErrorString()));

    return requestId;
}

QUuid ObfuscatingStorage::startReadPasswordJob(
    const QString & service, const QString & key)
{
    QUuid requestId = QUuid::createUuid();

    ApplicationSettings obfuscatedStorageSettings{
        QString::fromUtf8(settingsFileName)};

    obfuscatedStorageSettings.beginGroup(service + QStringLiteral("/") + key);
    QString cipher = obfuscatedStorageSettings.value(keys::cipher).toString();

    size_t keyLength = 0;
    bool conversionResult = false;
    keyLength = obfuscatedStorageSettings.value(keys::keyLength)
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
        obfuscatedStorageSettings.value(keys::value).toByteArray()));

    obfuscatedStorageSettings.endGroup();

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

QUuid ObfuscatingStorage::startDeletePasswordJob(
    const QString & service, const QString & key)
{
    QUuid requestId = QUuid::createUuid();

    ApplicationSettings obfuscatedStorageSettings{
        QString::fromUtf8(settingsFileName)};

    const QString compositeKey = service + QStringLiteral("/") + key;

    if (!obfuscatedStorageSettings.contains(compositeKey)) {
        QMetaObject::invokeMethod(
            this, "deletePasswordJobFinished", Q_ARG(QUuid, requestId),
            Q_ARG(ErrorCode, ErrorCode::EntryNotFound),
            Q_ARG(
                ErrorString,
                ErrorString(QT_TR_NOOP("could not find entry to delete"))));

        return requestId;
    }

    obfuscatedStorageSettings.remove(compositeKey);

    QMetaObject::invokeMethod(
        this, "deletePasswordJobFinished", Q_ARG(QUuid, requestId),
        Q_ARG(ErrorCode, ErrorCode::NoError),
        Q_ARG(ErrorString, ErrorString()));

    return requestId;
}

} // namespace quentier
