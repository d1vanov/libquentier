/*
 * Copyright 2020-2024 Dmitry Ivanov
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

#include <quentier/exception/InvalidArgument.h>
#include <quentier/threading/Future.h>
#include <quentier/utility/ApplicationSettings.h>
#include <quentier/utility/IEncryptor.h>

#include <cstddef>
#include <cstdint>

namespace quentier {

namespace keys {

constexpr const char * value = "Value";

} // namespace keys

namespace {

constexpr const char * settingsFileName = "obfuscatingKeychainStorage";

[[nodiscard]] bool writePasswordImpl(
    IEncryptor & encryptor, const QString & service, // NOLINT
    const QString & key, const QString & password,
    ErrorString & errorDescription)
{
    const auto res = encryptor.encrypt(password, key);
    if (!res.isValid()) {
        errorDescription = res.error();
        return false;
    }

    ApplicationSettings obfuscatedKeychainStorage{
        QString::fromUtf8(settingsFileName)};

    obfuscatedKeychainStorage.beginGroup(service + QStringLiteral("/") + key);

    obfuscatedKeychainStorage.setValue(
        keys::value, res.get().toUtf8().toBase64());

    obfuscatedKeychainStorage.endGroup();
    obfuscatedKeychainStorage.sync();

    return true;
}

[[nodiscard]] IKeychainService::ErrorCode readPasswordImpl(
    IEncryptor & encryptor, const QString & service, // NOLINT
    const QString & key, QString & password, ErrorString & errorDescription)
{
    ApplicationSettings obfuscatedKeychainStorage{
        QString::fromUtf8(settingsFileName)};

    QString encryptedText;
    obfuscatedKeychainStorage.beginGroup(service + QStringLiteral("/") + key);

    if (obfuscatedKeychainStorage.contains(keys::value)) {
        encryptedText = QString::fromUtf8(QByteArray::fromBase64(
                obfuscatedKeychainStorage.value(keys::value).toByteArray()));
    }

    obfuscatedKeychainStorage.endGroup();

    if (encryptedText.isEmpty()) {
        return IKeychainService::ErrorCode::EntryNotFound;
    }

    const auto res =
        encryptor.decrypt(encryptedText, key, IEncryptor::Cipher::AES);
    if (!res.isValid()) {
        errorDescription = res.error();
        return IKeychainService::ErrorCode::OtherError;
    }

    password = res.get();
    return IKeychainService::ErrorCode::NoError;
}

[[nodiscard]] IKeychainService::ErrorCode deletePasswordImpl(
    const QString & service, const QString & key) // NOLINT
{
    ApplicationSettings obfuscatedKeychainStorage{
        QString::fromUtf8(settingsFileName)};

    obfuscatedKeychainStorage.beginGroup(service + QStringLiteral("/") + key);
    ApplicationSettings::GroupCloser groupCloser{obfuscatedKeychainStorage};

    if (!obfuscatedKeychainStorage.contains(keys::value)) {
        return IKeychainService::ErrorCode::EntryNotFound;
    }

    obfuscatedKeychainStorage.remove(QStringLiteral(""));
    return IKeychainService::ErrorCode::NoError;
}

} // namespace

////////////////////////////////////////////////////////////////////////////////

ObfuscatingKeychainService::ObfuscatingKeychainService(
    IEncryptorPtr encryptor) : m_encryptor{std::move(encryptor)}
{
    if (Q_UNLIKELY(!m_encryptor)) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "ObfuscatingKeychainService ctor: encryptor is null")}};
    }
}

ObfuscatingKeychainService::~ObfuscatingKeychainService() noexcept = default;

QFuture<void> ObfuscatingKeychainService::writePassword(
    QString service, QString key, QString password)
{
    ErrorString errorDescription;
    if (writePasswordImpl(
            *m_encryptor, service, key, password, errorDescription))
    {
        return threading::makeReadyFuture();
    }

    return threading::makeExceptionalFuture<void>(
        Exception{ErrorCode::OtherError, std::move(errorDescription)});
}

QFuture<QString> ObfuscatingKeychainService::readPassword(
    QString service, QString key) const
{
    QString password;
    ErrorString errorDescription;
    const auto errorCode = readPasswordImpl(
        *m_encryptor, service, key, password, errorDescription);
    if (errorCode == IKeychainService::ErrorCode::NoError) {
        return threading::makeReadyFuture<QString>(std::move(password));
    }

    return threading::makeExceptionalFuture<QString>(
        Exception{errorCode, std::move(errorDescription)});
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

} // namespace quentier
