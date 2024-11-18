/*
 * Copyright 2024 Dmitry Ivanov
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

#include "Encryptor.h"

#include <quentier/logging/QuentierLogger.h>

#include <openssl/conf.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <openssl/ssl.h>

#include <QCryptographicHash>
#include <QDebug>
#include <QTextStream>

#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string_view>

namespace quentier {

using namespace std::string_view_literals;

#ifdef _MSC_VER
#pragma warning(disable : 4351)
#endif

[[nodiscard]] QString getSslLibErrorDescription()
{
    const auto errorCode = ERR_get_error();
    if (errorCode == 0) {
        return {};
    }

    std::string buf;
    buf.resize(1024);
    ERR_error_string_n(errorCode, buf.data(), buf.size());

    return QString::fromStdString(buf);
}

Encryptor::Encryptor() // NOLINT
{
#if OPENSSL_VERSION_NUMBER < 0x10100003L
    OPENSSL_config(NULL);
    ERR_load_crypto_strings();
    OpenSSL_add_all_algorithms();
#endif
}

Encryptor::~Encryptor() noexcept // NOLINT
{
#if OPENSSL_VERSION_NUMBER < 0x10100003L
    EVP_cleanup();
    ERR_free_strings();
#endif
}

Result<QString, ErrorString> Encryptor::encrypt(
    const QString & text, const QString & passphrase)
{
    constexpr std::string_view ident = "ENC0"sv;
    QByteArray encryptedTextData(ident.data(), static_cast<int>(ident.size()));

    ErrorString errorDescription;
    const auto makeError = [&] {
        return Result<QString, ErrorString>{std::move(errorDescription)};
    };

    if (!generateSalt(SaltKind::SALT, s_aes_keysize, errorDescription)) {
        return makeError();
    }

    if (!generateSalt(SaltKind::SALTMAC, s_aes_keysize, errorDescription)) {
        return makeError();
    }

    if (!generateSalt(SaltKind::IV, s_aes_keysize, errorDescription)) {
        return makeError();
    }

    encryptedTextData.append(
        reinterpret_cast<const char *>(m_salt.data()),
        static_cast<int>(m_salt.size()));

    encryptedTextData.append(
        reinterpret_cast<const char *>(m_saltmac.data()),
        static_cast<int>(m_saltmac.size()));

    encryptedTextData.append(
        reinterpret_cast<const char *>(m_iv.data()),
        static_cast<int>(m_iv.size()));

    QByteArray passphraseData = passphrase.toUtf8();

    if (!generateKey(
            passphraseData, m_salt.data(), s_aes_keysize, errorDescription))
    {
        return makeError();
    }

    QByteArray textToEncryptData = text.toUtf8();

    if (!encyptWithAes(textToEncryptData, encryptedTextData, errorDescription))
    {
        return makeError();
    }

    if (!calculateHmac(
            passphraseData, m_saltmac.data(), encryptedTextData, s_aes_keysize,
            errorDescription))
    {
        return makeError();
    }

    encryptedTextData.append(
        reinterpret_cast<const char *>(m_hmac.data()), s_aes_hmacsize);

    return Result<QString, ErrorString>{
        QString::fromUtf8(encryptedTextData.toBase64())};
}

Result<QString, ErrorString> Encryptor::decrypt(
    const QString & encryptedText, const QString & passphrase,
    const Cipher cipher)
{
    ErrorString errorDescription;
    const auto makeError = [&] {
        return Result<QString, ErrorString>{std::move(errorDescription)};
    };

    switch (cipher) {
    case Cipher::RC2:
    {
        QString decryptedText;
        if (!decryptRc2(
                encryptedText, passphrase, decryptedText, errorDescription))
        {
            QNWARNING("utility::encryption", errorDescription);
            return makeError();
        }

        return Result<QString, ErrorString>{std::move(decryptedText)};
    } break;
    case Cipher::AES:
    {
        QByteArray decryptedByteArray;
        if (!decryptAes(
                encryptedText, passphrase, decryptedByteArray,
                errorDescription))
        {
            QNWARNING("utility::encryption", errorDescription);
            return makeError();
        }

        return Result<QString, ErrorString>{
            QString::fromUtf8(decryptedByteArray)};
    } break;
    }

    errorDescription.setBase(
        QT_TRANSLATE_NOOP("utility::Encryptor", "unidentified cipher"));
    QNWARNING(
        "utility::encryption",
        errorDescription << ", cipher = " << static_cast<int>(cipher));

    return makeError();
}

} // namespace quentier
