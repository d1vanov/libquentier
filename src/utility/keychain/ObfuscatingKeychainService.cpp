/*
 * Copyright 2020-2022 Dmitry Ivanov
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

#include <quentier/threading/Future.h>
#include <quentier/utility/ApplicationSettings.h>

#include <QMetaObject>

#include <cstddef>
#include <cstdint>

namespace quentier {

namespace keys {

constexpr const char * cipher = "Cipher";
constexpr const char * keyLength = "KeyLength";
constexpr const char * value = "Value";

} // namespace keys

namespace {

constexpr const char * settingsFileName = "obfuscatingKeychainStorage";

[[nodiscard]] bool writePasswordImpl(
    EncryptionManager & encryptionManager, const QString & service,
    const QString & key, const QString & password,
    ErrorString & errorDescription)
{
    QString encryptedString;
    QString cipher;
    std::size_t keyLength = 0;

    if (!encryptionManager.encrypt(
            password, key, cipher, keyLength, encryptedString,
            errorDescription))
    {
        return false;
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

    return true;
}

[[nodiscard]] bool readPasswordImpl(
    EncryptionManager & encryptionManager, const QString & service,
    const QString & key, QString & password, ErrorString & errorDescription)
{
    ApplicationSettings obfuscatedKeychainStorage{
        QString::fromUtf8(settingsFileName)};

    obfuscatedKeychainStorage.beginGroup(service + QStringLiteral("/") + key);
    QString cipher = obfuscatedKeychainStorage.value(keys::cipher).toString();

    bool conversionResult = false;
    std::size_t keyLength = obfuscatedKeychainStorage.value(keys::keyLength)
                                .toULongLong(&conversionResult);
    if (!conversionResult) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "utility::keychain::ObfuscatingKeychainService",
            "Could not convert key length to unsigned long"));
        return false;
    }

    QString encryptedText = QString::fromUtf8(QByteArray::fromBase64(
        obfuscatedKeychainStorage.value(keys::value).toByteArray()));

    obfuscatedKeychainStorage.endGroup();

    return encryptionManager.decrypt(
        encryptedText, key, cipher, keyLength, password, errorDescription);
}

[[nodiscard]] IKeychainService::ErrorCode deletePasswordImpl(
    const QString & service, const QString & key)
{
    ApplicationSettings obfuscatedKeychainStorage{
        QString::fromUtf8(settingsFileName)};

    obfuscatedKeychainStorage.beginGroup(service + QStringLiteral("/") + key);

    if (obfuscatedKeychainStorage.allKeys().isEmpty()) {
        obfuscatedKeychainStorage.endGroup();
        return IKeychainService::ErrorCode::EntryNotFound;
    }

    obfuscatedKeychainStorage.remove(QStringLiteral(""));
    obfuscatedKeychainStorage.endGroup();
    return IKeychainService::ErrorCode::NoError;
}

} // namespace

////////////////////////////////////////////////////////////////////////////////

ObfuscatingKeychainService::ObfuscatingKeychainService(QObject * parent) :
    IKeychainService(parent)
{}

ObfuscatingKeychainService::~ObfuscatingKeychainService() noexcept = default;

QFuture<void> ObfuscatingKeychainService::writePassword(
    QString service, QString key, QString password)
{
    ErrorString errorDescription;
    if (writePasswordImpl(
            m_encryptionManager, service, key, password, errorDescription))
    {
        return threading::makeReadyFuture();
    }

    return threading::makeExceptionalFuture<void>(
        Exception{ErrorCode::OtherError, std::move(errorDescription)});
}

QFuture<QString> ObfuscatingKeychainService::readPassword(
    QString service, QString key)
{
    QString password;
    ErrorString errorDescription;
    if (readPasswordImpl(
            m_encryptionManager, service, key, password, errorDescription))
    {
        return threading::makeReadyFuture<QString>(std::move(password));
    }

    return threading::makeExceptionalFuture<QString>(
        Exception{ErrorCode::OtherError, std::move(errorDescription)});
}

QFuture<void> ObfuscatingKeychainService::deletePassword(
    QString service, QString key)
{
    const auto errorCode = deletePasswordImpl(service, key);
    if (errorCode == ErrorCode::NoError) {
        return threading::makeReadyFuture();
    }

    return threading::makeExceptionalFuture<void>(Exception{errorCode});
}

QUuid ObfuscatingKeychainService::startWritePasswordJob(
    const QString & service, const QString & key, const QString & password)
{
    const QUuid requestId = QUuid::createUuid();

    ErrorString errorDescription;
    if (writePasswordImpl(
            m_encryptionManager, service, key, password, errorDescription))
    {
        QMetaObject::invokeMethod(
            this, "writePasswordJobFinished", Qt::QueuedConnection,
            Q_ARG(QUuid, requestId), Q_ARG(ErrorCode, ErrorCode::NoError),
            Q_ARG(ErrorString, ErrorString()));
    }
    else {
        QMetaObject::invokeMethod(
            this, "writePasswordJobFinished", Qt::QueuedConnection,
            Q_ARG(QUuid, requestId), Q_ARG(ErrorCode, ErrorCode::OtherError),
            Q_ARG(ErrorString, errorDescription));
    }

    return requestId;
}

QUuid ObfuscatingKeychainService::startReadPasswordJob(
    const QString & service, const QString & key)
{
    const QUuid requestId = QUuid::createUuid();

    QString password;
    ErrorString errorDescription;
    if (readPasswordImpl(
            m_encryptionManager, service, key, password, errorDescription))
    {
        QMetaObject::invokeMethod(
            this, "readPasswordJobFinished", Qt::QueuedConnection,
            Q_ARG(QUuid, requestId), Q_ARG(ErrorCode, ErrorCode::NoError),
            Q_ARG(ErrorString, ErrorString()), Q_ARG(QString, password));
    }
    else {
        QMetaObject::invokeMethod(
            this, "readPasswordJobFinished", Qt::QueuedConnection,
            Q_ARG(QUuid, requestId), Q_ARG(ErrorCode, ErrorCode::OtherError),
            Q_ARG(ErrorString, errorDescription), Q_ARG(QString, QString()));
    }

    return requestId;
}

QUuid ObfuscatingKeychainService::startDeletePasswordJob(
    const QString & service, const QString & key)
{
    const QUuid requestId = QUuid::createUuid();

    const auto errorCode = deletePasswordImpl(service, key);
    if (errorCode == ErrorCode::NoError) {
        QMetaObject::invokeMethod(
            this, "deletePasswordJobFinished", Qt::QueuedConnection,
            Q_ARG(QUuid, requestId), Q_ARG(ErrorCode, ErrorCode::NoError),
            Q_ARG(ErrorString, ErrorString()));
    }
    else if (errorCode == ErrorCode::EntryNotFound) {
        QMetaObject::invokeMethod(
            this, "deletePasswordJobFinished", Qt::QueuedConnection,
            Q_ARG(QUuid, requestId), Q_ARG(ErrorCode, ErrorCode::EntryNotFound),
            Q_ARG(
                ErrorString,
                ErrorString(QT_TR_NOOP("could not find entry to delete"))));
    }
    else {
        // This branch must be unreachable
        Q_ASSERT(false);
    }

    return requestId;
}

} // namespace quentier
