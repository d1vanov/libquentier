/*
 * Copyright 2024-2025 Dmitry Ivanov
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

[[nodiscard]] QString sslLibErrorDescription()
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

Result<QString, ErrorString> Encryptor::encrypt(
    const QString & text, const QString & passphrase)
{
    constexpr std::string_view ident = "ENC0"sv;
    QByteArray encryptedTextData(ident.data(), static_cast<int>(ident.size()));

    ErrorString errorDescription;
    const auto makeError = [&] {
        return Result<QString, ErrorString>{std::move(errorDescription)};
    };

    const std::lock_guard lock{m_mutex};

    if (!generateSalt(SaltKind::SALT, s_aesKeySize, errorDescription)) {
        return makeError();
    }

    if (!generateSalt(SaltKind::SALTMAC, s_aesKeySize, errorDescription)) {
        return makeError();
    }

    if (!generateSalt(SaltKind::IV, s_aesKeySize, errorDescription)) {
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
            passphraseData, m_salt.data(), s_aesKeySize, errorDescription))
    {
        return makeError();
    }

    QByteArray textToEncryptData = text.toUtf8();

    if (!encyptWithAes(textToEncryptData, encryptedTextData, errorDescription))
    {
        return makeError();
    }

    if (!calculateHmac(
            passphraseData, m_saltmac.data(), encryptedTextData, s_aesKeySize,
            errorDescription))
    {
        return makeError();
    }

    encryptedTextData.append(
        reinterpret_cast<const char *>(m_hmac.data()), s_aesHmacSize);

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

    const std::lock_guard lock{m_mutex};

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

template <class T>
void Encryptor::printSaltKind(T & t, const SaltKind saltKind)
{
    switch (saltKind) {
    case SaltKind::SALT:
        t << "SALT";
        break;
    case SaltKind::SALTMAC:
        t << "SALTMAC";
        break;
    case SaltKind::IV:
        t << "IV";
        break;
    default:
        t << "Unknown (" << static_cast<qint64>(saltKind) << ")";
        break;
    }
}

QDebug & operator<<(QDebug & dbg, const Encryptor::SaltKind kind)
{
    Encryptor::printSaltKind(dbg, kind);
    return dbg;
}

QTextStream & operator<<(QTextStream & strm, const Encryptor::SaltKind kind)
{
    Encryptor::printSaltKind(strm, kind);
    return strm;
}

bool Encryptor::generateSalt(
    const Encryptor::SaltKind saltKind, const std::size_t saltSize,
    ErrorString & errorDescription)
{
    unsigned char * salt = nullptr;
    const char * saltText = nullptr;

    switch (saltKind) {
    case SaltKind::SALT:
        salt = m_salt.data();
        saltText = "salt";
        break;
    case SaltKind::SALTMAC:
        salt = m_saltmac.data();
        saltText = "saltmac";
        break;
    case SaltKind::IV:
        salt = m_iv.data();
        saltText = "iv";
        break;
    }

    if (RAND_bytes(salt, static_cast<int>(saltSize)) != 1) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "utility::Encryptor",
            "can't generate cryptographically strong bytes for encryption"));

        QNWARNING(
            "utility::encryption",
            errorDescription << "; salt = " << saltText << ", OpenSSL error: "
                             << sslLibErrorDescription());
        return false;
    }

    return true;
}

bool Encryptor::generateKey(
    const QByteArray & passphraseData, const unsigned char * salt,
    const std::size_t keySize, ErrorString & errorDescription)
{
    const char * rawPassphraseData = passphraseData.constData();

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    if (Q_UNLIKELY(
            (passphraseData.size() < std::numeric_limits<int>::min()) ||
            (passphraseData.size() > std::numeric_limits<int>::max())))
    {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "utility::Encryptor",
            "can't generate cryptographic key: invalid password length"));
        errorDescription.details() = QString::number(passphraseData.size());
        return false;
    }
#endif

    constexpr int maxIterations = 50000;
    int res = PKCS5_PBKDF2_HMAC(
        rawPassphraseData, static_cast<int>(passphraseData.size()), // NOLINT
        salt,                                                       // NOLINT
        static_cast<int>(keySize), maxIterations, EVP_sha256(),
        static_cast<int>(keySize), m_key.data());

    if (res != 1) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "utility::Encryptor", "can't generate cryptographic key"));

        QNWARNING(
            "utility::encryption",
            errorDescription << ", PKCS5_PBKDF2_HMAC failed: "
                             << sslLibErrorDescription());
        return false;
    }

    return true;
}

bool Encryptor::calculateHmac(
    const QByteArray & passphraseData, const unsigned char * salt,
    const QByteArray & encryptedTextData, const std::size_t keySize,
    ErrorString & errorDescription)
{
    if (!generateKey(passphraseData, salt, keySize, errorDescription)) {
        return false;
    }

    const auto * data =
        reinterpret_cast<const unsigned char *>(encryptedTextData.constData());

    unsigned char * digest = HMAC(
        EVP_sha256(), m_key.data(), static_cast<int>(keySize), data,
        static_cast<std::size_t>(encryptedTextData.size()), nullptr, nullptr);

    for (std::size_t i = 0; i < s_aesHmacSize; ++i) {
        m_hmac[i] = digest[i];
    }

    return true;
}

bool Encryptor::encyptWithAes(
    const QByteArray & textToEncryptData, QByteArray & encryptedTextData,
    ErrorString & errorDescription)
{
    const auto * rawTextToEncrypt =
        reinterpret_cast<const unsigned char *>(textToEncryptData.constData());

    const auto rawTextToEncryptSize = textToEncryptData.size();
    if (Q_UNLIKELY(rawTextToEncryptSize < 0)) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "utility::Encryptor",
            "can't generate cryptographic key: invalid length of text to "
            "encrypt"));
        errorDescription.details() = QString::number(rawTextToEncryptSize);
        QNWARNING("utility::encryption", errorDescription);
        return false;
    }

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    if (Q_UNLIKELY(rawTextToEncryptSize > std::numeric_limits<int>::max())) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "utility::Encryptor",
            "can't generate cryptographic key: text to encrypt is too long"));
        errorDescription.details() = QString::number(rawTextToEncryptSize);
        QNWARNING("utility::encryption", errorDescription);
        return false;
    }
#endif

    constexpr std::size_t maxPadding = 16;
    auto * cipherText = reinterpret_cast<unsigned char *>(
        malloc(static_cast<std::size_t>(rawTextToEncryptSize) + maxPadding));

    int bytesWritten = 0;
    int cipherTextSize = 0;

    EVP_CIPHER_CTX * context = EVP_CIPHER_CTX_new();
    int res =
        EVP_EncryptInit(context, EVP_aes_128_cbc(), m_key.data(), m_iv.data());
    if (res != 1) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "utility::Encryptor",
            "can't encrypt the text using AES algorithm"));

        QNWARNING(
            "utility::encryption",
            errorDescription << ", EVP_EnryptInit failed: "
                             << sslLibErrorDescription());
        free(cipherText);
        EVP_CIPHER_CTX_free(context);
        return false;
    }

    res = EVP_EncryptUpdate(
        context, cipherText, &bytesWritten, rawTextToEncrypt,
        static_cast<int>(rawTextToEncryptSize));

    if (res != 1) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "utility::Encryptor",
            "can't encrypt the text using AES algorithm"));

        QNWARNING(
            "utility::encryption",
            errorDescription << ", EVP_CipherUpdate failed: "
                             << sslLibErrorDescription());
        free(cipherText);
        EVP_CIPHER_CTX_free(context);
        return false;
    }

    cipherTextSize += bytesWritten;

    res = EVP_EncryptFinal(context, cipherText + bytesWritten, &bytesWritten);
    if (res != 1) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "utility::Encryptor",
            "can't encrypt the text using AES algorithm"));

        QNWARNING(
            "utility::encryption",
            errorDescription << ", EVP_CipherFinal failed: "
                             << sslLibErrorDescription());
        free(cipherText);
        EVP_CIPHER_CTX_free(context);
        return false;
    }

    cipherTextSize += bytesWritten;

    encryptedTextData.append(
        reinterpret_cast<const char *>(cipherText), cipherTextSize);

    free(cipherText);
    EVP_CIPHER_CTX_free(context);
    return true;
}

bool Encryptor::decryptAes(
    const QString & encryptedText, const QString & passphrase,
    QByteArray & decryptedText, ErrorString & errorDescription)
{
    QNDEBUG("utility::encryption", "Encryptor::decryptAes");

    QByteArray cipherText;
    if (!splitEncryptedData(
            encryptedText, s_aesKeySize, s_aesHmacSize, cipherText,
            errorDescription))
    {
        return false;
    }

    const auto rawCipherTextSize = cipherText.size();
    if (Q_UNLIKELY(rawCipherTextSize < 0)) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "utility::Encryptor",
            "can't decrypt text: invalid cipher text length"));
        errorDescription.details() = QString::number(rawCipherTextSize);
        QNWARNING("utility::encryption", errorDescription);
        return false;
    }

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    if (Q_UNLIKELY(rawCipherTextSize > std::numeric_limits<int>::max())) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "utility::Encryptor",
            "can't decrypt text: cipher text is too large"));
        errorDescription.details() = QString::number(rawCipherTextSize);
        QNWARNING("utility::encryption", errorDescription);
        return false;
    }
#endif

    QByteArray passphraseData = passphrase.toUtf8();

    // Validate HMAC
    std::array<unsigned char, s_aesHmacSize> parsedHmac;
    for (std::size_t i = 0; i < s_aesHmacSize; ++i) {
        parsedHmac[i] = m_hmac[i];
    }

    QByteArray saltWithCipherText =
        QByteArray::fromBase64(encryptedText.toUtf8());

    saltWithCipherText.remove(
        saltWithCipherText.size() - static_cast<int>(s_aesHmacSize),
        static_cast<int>(s_aesHmacSize));

    if (!calculateHmac(
            passphraseData, m_saltmac.data(), saltWithCipherText, s_aesKeySize,
            errorDescription))
    {
        return false;
    }

    for (std::size_t i = 0; i < s_aesHmacSize; ++i) {
        if (parsedHmac[i] != m_hmac[i]) {
            errorDescription.setBase(QT_TRANSLATE_NOOP(
                "utility::Encryptor", "can't decrypt text: invalid checksum"));

            QNWARNING(
                "utility::encryption",
                errorDescription
                    << ", parsed hmac: "
                    << QByteArray(
                           reinterpret_cast<const char *>(parsedHmac.data()),
                           s_aesHmacSize)
                           .toHex()
                    << ", expected hmac: "
                    << QByteArray(
                           reinterpret_cast<const char *>(m_hmac.data()),
                           s_aesHmacSize)
                           .toHex());

            return false;
        }
    }

    if (!generateKey(
            passphraseData, m_salt.data(), s_aesKeySize, errorDescription))
    {
        return false;
    }

    const auto * rawCipherText =
        reinterpret_cast<const unsigned char *>(cipherText.constData());

    auto * decipheredText = reinterpret_cast<unsigned char *>(
        malloc(static_cast<std::size_t>(rawCipherTextSize)));

    int bytesWritten = 0;
    int decipheredTextSize = 0;

    EVP_CIPHER_CTX * context = EVP_CIPHER_CTX_new();
    int res =
        EVP_DecryptInit(context, EVP_aes_128_cbc(), m_key.data(), m_iv.data());
    if (res != 1) {
        errorDescription.setBase(
            QT_TRANSLATE_NOOP("utility::Encryptor", "can't decrypt the text"));

        QNWARNING(
            "utility::encryption",
            errorDescription << ", openssl EVP_DecryptInit failed: "
                             << sslLibErrorDescription());
        free(decipheredText);
        EVP_CIPHER_CTX_free(context);
        return false;
    }

    res = EVP_DecryptUpdate(
        context, decipheredText, &bytesWritten, rawCipherText,
        static_cast<int>(rawCipherTextSize));

    if (res != 1) {
        errorDescription.setBase(
            QT_TRANSLATE_NOOP("utility::Encryptor", "can't decrypt the text"));

        QNWARNING(
            "utility::encryption",
            errorDescription << ", openssl EVP_DecryptUpdate failed: "
                             << sslLibErrorDescription());
        free(decipheredText);
        EVP_CIPHER_CTX_free(context);
        return false;
    }

    decipheredTextSize += bytesWritten;

    res =
        EVP_DecryptFinal(context, decipheredText + bytesWritten, &bytesWritten);

    if (res != 1) {
        errorDescription.setBase(
            QT_TRANSLATE_NOOP("utility::Encryptor", "can't decrypt the text"));

        QNWARNING(
            "utility::encryption",
            errorDescription << ", openssl EVP_DecryptFinal failed: "
                             << sslLibErrorDescription());
        free(decipheredText);
        EVP_CIPHER_CTX_free(context);
        return false;
    }

    decipheredTextSize += bytesWritten;

    // HACK: it appears that with OpenSSL 3.x the decryption suddenly adds
    // a null byte at the end of the deciphered text. For now I don't quite
    // understand why it does that but will just remove this null byte.
    if (decipheredTextSize != 0 &&
        reinterpret_cast<const char *>(
            decipheredText)[decipheredTextSize - 1] == '\u0000')
    {
        --decipheredTextSize;
    }

    decryptedText = QByteArray(
        reinterpret_cast<const char *>(decipheredText), decipheredTextSize);

    free(decipheredText);
    EVP_CIPHER_CTX_free(context);
    return true;
}

bool Encryptor::splitEncryptedData(
    const QString & encryptedData, const std::size_t saltSize,
    const std::size_t hmacSize, QByteArray & encryptedText,
    ErrorString & errorDescription)
{
    const QByteArray decodedEncryptedData =
        QByteArray::fromBase64(encryptedData.toUtf8());

    const int minLength = static_cast<int>(4 + 3 * saltSize + hmacSize);

    const auto encryptedDataSize = decodedEncryptedData.size();
    if (encryptedDataSize <= minLength) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "utility::Encryptor",
            "encrypted data is too short for being valid"));
        errorDescription.details() = QString::number(encryptedDataSize);

        QNWARNING(
            "utility::encryption",
            errorDescription << ": " << encryptedDataSize
                             << " bytes while should be at least " << minLength
                             << " bytes");
        return false;
    }

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    if (Q_UNLIKELY(encryptedDataSize > std::numeric_limits<int>::max())) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "utility::Encryptor", "encrypted data is too large"));
        errorDescription.details() = QString::number(encryptedDataSize);
        QNWARNING("utility::encryption", errorDescription);
        return false;
    }
#endif

    const auto * decodedEncryptedDataPtr =
        reinterpret_cast<const unsigned char *>(
            decodedEncryptedData.constData());

    std::size_t cursor = 4;
    for (std::size_t i = 0; i < saltSize; ++i) {
        m_salt[i] = decodedEncryptedDataPtr[i + cursor];
    }

    cursor += saltSize;
    for (std::size_t i = 0; i < saltSize; ++i) {
        m_saltmac[i] = decodedEncryptedDataPtr[i + cursor];
    }

    cursor += saltSize;
    for (std::size_t i = 0; i < saltSize; ++i) {
        m_iv[i] = decodedEncryptedDataPtr[i + cursor];
    }

    cursor += saltSize;
    encryptedText.resize(0);

    int encryptedDataWithoutHmacSize =
        static_cast<int>(encryptedDataSize) - static_cast<int>(hmacSize);

    for (int i = static_cast<int>(cursor); i < encryptedDataWithoutHmacSize;
         ++i)
    {
        encryptedText += decodedEncryptedData.at(i);
    }

    cursor = static_cast<std::size_t>(encryptedDataWithoutHmacSize);
    for (std::size_t i = 0; i < hmacSize; ++i) {
        m_hmac[i] = decodedEncryptedDataPtr[i + cursor];
    }

    return true;
}

// WARNING: the implementation was ported from JavaScript taken from Evernote
// site so it contains dangerous magic. Don't touch that unless you know what
// you're doing!
bool Encryptor::decryptRc2(
    const QString & encryptedText, const QString & passphrase,
    QString & decryptedText, ErrorString & errorDescription)
{
    QByteArray encryptedTextData =
        QByteArray::fromBase64(encryptedText.toUtf8());

    decryptedText.resize(0);

    rc2KeyCodesFromPassphrase(passphrase);

    while (encryptedTextData.size() > 0) {
        QByteArray chunk;
        chunk.reserve(8);
        for (int i = 0; i < 8; ++i) {
            chunk.push_back(encryptedTextData[i]);
        }
        Q_UNUSED(encryptedTextData.remove(0, 8));
        QString decryptedChunk = decryptRc2Chunk(chunk, m_cachedKey);
        decryptedText += decryptedChunk;
    }

    // First 4 chars of the string is a HEX-representation of the upper-byte
    // of the CRC32 of the string. If CRC32 is valid, we return the decoded
    // string, otherwise return with error
    QString crc = decryptedText.left(4);
    decryptedText.remove(0, 4);

    qint32 realCrc = crc32(decryptedText);
    realCrc ^= (-1);

    auto unsignedRealCrc = static_cast<quint32>(realCrc);
    unsignedRealCrc >>= 0;

    QString realCrcStr = QString::number(unsignedRealCrc, 16);
    realCrcStr = realCrcStr.left(4);
    realCrcStr = realCrcStr.toUpper();

    if (realCrcStr != crc) {
        errorDescription.setBase(
            QT_TRANSLATE_NOOP("utility::Encryptor", "CRC32 checksum mismatch"));

        errorDescription.details() = QStringLiteral("Decrypted string has ");
        errorDescription.details() += crc;
        errorDescription.details() += QStringLiteral(", calculated CRC32 is ");
        errorDescription.details() += realCrcStr;
        return false;
    }

    // Get rid of zero symbols at the end of the string, if any
    while ((decryptedText.size() > 0) &&
           (decryptedText.at(decryptedText.size() - 1) ==
            QChar::fromLatin1(char(0))))
    {
        decryptedText.remove(decryptedText.size() - 1, 1);
    }

    return true;
}

void Encryptor::rc2KeyCodesFromPassphrase(const QString & passphrase) const
{
    QByteArray keyData =
        QCryptographicHash::hash(passphrase.toUtf8(), QCryptographicHash::Md5);

    const auto keyDataSize = keyData.size();

    // 256-entry permutation table, probably derived somehow from pi
    constexpr std::array<int, 256> rc2_permute{
        217, 120, 249, 196, 25,  221, 181, 237, 40,  233, 253, 121, 74,  160,
        216, 157, 198, 126, 55,  131, 43,  118, 83,  142, 98,  76,  100, 136,
        68,  139, 251, 162, 23,  154, 89,  245, 135, 179, 79,  19,  97,  69,
        109, 141, 9,   129, 125, 50,  189, 143, 64,  235, 134, 183, 123, 11,
        240, 149, 33,  34,  92,  107, 78,  130, 84,  214, 101, 147, 206, 96,
        178, 28,  115, 86,  192, 20,  167, 140, 241, 220, 18,  117, 202, 31,
        59,  190, 228, 209, 66,  61,  212, 48,  163, 60,  182, 38,  111, 191,
        14,  218, 70,  105, 7,   87,  39,  242, 29,  155, 188, 148, 67,  3,
        248, 17,  199, 246, 144, 239, 62,  231, 6,   195, 213, 47,  200, 102,
        30,  215, 8,   232, 234, 222, 128, 82,  238, 247, 132, 170, 114, 172,
        53,  77,  106, 42,  150, 26,  210, 113, 90,  21,  73,  116, 75,  159,
        208, 94,  4,   24,  164, 236, 194, 224, 65,  110, 15,  81,  203, 204,
        36,  145, 175, 80,  161, 244, 112, 57,  153, 124, 58,  133, 35,  184,
        180, 122, 252, 2,   54,  91,  37,  85,  151, 49,  45,  93,  250, 152,
        227, 138, 146, 174, 5,   223, 41,  16,  103, 108, 186, 201, 211, 0,
        230, 207, 225, 158, 168, 44,  99,  22,  1,   63,  88,  226, 137, 169,
        13,  56,  52,  27,  171, 51,  255, 176, 187, 72,  12,  95,  185, 177,
        205, 46,  197, 243, 219, 71,  229, 165, 156, 119, 10,  166, 32,  104,
        254, 127, 193, 173};

    // Convert the input data into the array
    m_cachedXkey.clear();
    m_cachedXkey.reserve(static_cast<std::size_t>(
        std::max<decltype(keyDataSize)>(keyDataSize, 0)));
    for (int i = 0; i < keyDataSize; ++i) {
        m_cachedXkey.push_back(static_cast<int>(keyData[i]));
    }

    // Phase 1: Expand input key to 128 bytes
    int len = static_cast<int>(m_cachedXkey.size());
    m_cachedXkey.resize(128);
    for (auto i = static_cast<std::size_t>(len); i < 128; ++i) {
        m_cachedXkey[i] = rc2_permute
            [(m_cachedXkey[i - 1] +
              m_cachedXkey[i - static_cast<std::size_t>(len)]) &
             255];
    }

    // Phase 2: Reduce effective key size to 64 bits
    const int bits = 64;

    len = (bits + 7) >> 3;
    int i = 128 - len;
    int x = rc2_permute
        [m_cachedXkey[static_cast<std::size_t>(i)] & (255 >> (7 & -bits))];
    m_cachedXkey[static_cast<std::size_t>(i)] = x;
    while (i--) {
        x = rc2_permute[static_cast<std::size_t>(
            x ^ m_cachedXkey[static_cast<std::size_t>(i + len)])]; // NOLINT
        m_cachedXkey[static_cast<std::size_t>(i)] = x;
    }

    // Phase 3: copy to key array of words in little-endian order
    m_cachedKey.resize(64);
    i = 63;
    do {
        m_cachedKey[static_cast<std::size_t>(i)] =
            (m_cachedXkey[static_cast<std::size_t>(2 * i)] & 255) +   // NOLINT
            (m_cachedXkey[static_cast<std::size_t>(2 * i + 1)] << 8); // NOLINT
    } while (i--);
}

QString Encryptor::decryptRc2Chunk(
    const QByteArray & inputCharCodes, const std::vector<int> & key) const
{
    int x76, x54, x32, x10, i;

    for (std::size_t i = 0; i < m_decryptRc2ChunkKeyCodes.size(); ++i) {
        int & code = m_decryptRc2ChunkKeyCodes[i];
        code = static_cast<int>(
            static_cast<unsigned char>(inputCharCodes.at(static_cast<int>(i))));
        if (code < 0) {
            code += 256;
        }
    }

    x76 = (m_decryptRc2ChunkKeyCodes[7] << 8) + m_decryptRc2ChunkKeyCodes[6];

    x54 = (m_decryptRc2ChunkKeyCodes[5] << 8) + m_decryptRc2ChunkKeyCodes[4];

    x32 = (m_decryptRc2ChunkKeyCodes[3] << 8) + m_decryptRc2ChunkKeyCodes[2];

    x10 = (m_decryptRc2ChunkKeyCodes[1] << 8) + m_decryptRc2ChunkKeyCodes[0];

    i = 15;
    do {
        x76 &= 65535;
        x76 = (x76 << 11) + (x76 >> 5);
        x76 -= (x10 & ~x54) + (x32 & x54) +
            key[static_cast<std::size_t>(4 * i + 3)]; // NOLINT

        x54 &= 65535;
        x54 = (x54 << 13) + (x54 >> 3);
        x54 -= (x76 & ~x32) + (x10 & x32) +
            key[static_cast<std::size_t>(4 * i + 2)]; // NOLINT

        x32 &= 65535;
        x32 = (x32 << 14) + (x32 >> 2);
        x32 -= (x54 & ~x10) + (x76 & x10) +
            key[static_cast<std::size_t>(4 * i + 1)]; // NOLINT

        x10 &= 65535;
        x10 = (x10 << 15) + (x10 >> 1);
        x10 -= (x32 & ~x76) + (x54 & x76) +
            key[static_cast<std::size_t>(4 * i + 0)]; // NOLINT

        if (i == 5 || i == 11) {
            x76 -= key[static_cast<std::size_t>(x54 & 63)];
            x54 -= key[static_cast<std::size_t>(x32 & 63)];
            x32 -= key[static_cast<std::size_t>(x10 & 63)];
            x10 -= key[static_cast<std::size_t>(x76 & 63)];
        }
    } while (i--);

    m_rc2ChunkOut.resize(8);
    m_rc2ChunkOut[0] = QChar{x10 & 255};
    m_rc2ChunkOut[1] = QChar{(x10 >> 8) & 255};
    m_rc2ChunkOut[2] = QChar{x32 & 255};
    m_rc2ChunkOut[3] = QChar{(x32 >> 8) & 255};
    m_rc2ChunkOut[4] = QChar{x54 & 255};
    m_rc2ChunkOut[5] = QChar{(x54 >> 8) & 255};
    m_rc2ChunkOut[6] = QChar{x76 & 255};
    m_rc2ChunkOut[7] = QChar{(x76 >> 8) & 255};

    QByteArray outData = m_rc2ChunkOut.toUtf8();
    QString out = QString::fromUtf8(outData.constData(), outData.size());
    return out;
}

// WARNING: the implementation was ported from JavaScript taken from Evernote
// site so it contains dangerous magic. Don't touch that unless you know what
// you're doing!
qint32 Encryptor::crc32(const QString & str) const
{
    const QString crc32table = QStringLiteral(
        "00000000 77073096 EE0E612C 990951BA 076DC419 "
        "706AF48F E963A535 9E6495A3 0EDB8832 79DCB8A4 "
        "E0D5E91E 97D2D988 09B64C2B 7EB17CBD E7B82D07 "
        "90BF1D91 1DB71064 6AB020F2 F3B97148 84BE41DE "
        "1ADAD47D 6DDDE4EB F4D4B551 83D385C7 136C9856 "
        "646BA8C0 FD62F97A 8A65C9EC 14015C4F 63066CD9 "
        "FA0F3D63 8D080DF5 3B6E20C8 4C69105E D56041E4 "
        "A2677172 3C03E4D1 4B04D447 D20D85FD A50AB56B "
        "35B5A8FA 42B2986C DBBBC9D6 ACBCF940 32D86CE3 "
        "45DF5C75 DCD60DCF ABD13D59 26D930AC 51DE003A "
        "C8D75180 BFD06116 21B4F4B5 56B3C423 CFBA9599 "
        "B8BDA50F 2802B89E 5F058808 C60CD9B2 B10BE924 "
        "2F6F7C87 58684C11 C1611DAB B6662D3D 76DC4190 "
        "01DB7106 98D220BC EFD5102A 71B18589 06B6B51F "
        "9FBFE4A5 E8B8D433 7807C9A2 0F00F934 9609A88E "
        "E10E9818 7F6A0DBB 086D3D2D 91646C97 E6635C01 "
        "6B6B51F4 1C6C6162 856530D8 F262004E 6C0695ED "
        "1B01A57B 8208F4C1 F50FC457 65B0D9C6 12B7E950 "
        "8BBEB8EA FCB9887C 62DD1DDF 15DA2D49 8CD37CF3 "
        "FBD44C65 4DB26158 3AB551CE A3BC0074 D4BB30E2 "
        "4ADFA541 3DD895D7 A4D1C46D D3D6F4FB 4369E96A "
        "346ED9FC AD678846 DA60B8D0 44042D73 33031DE5 "
        "AA0A4C5F DD0D7CC9 5005713C 270241AA BE0B1010 "
        "C90C2086 5768B525 206F85B3 B966D409 CE61E49F "
        "5EDEF90E 29D9C998 B0D09822 C7D7A8B4 59B33D17 "
        "2EB40D81 B7BD5C3B C0BA6CAD EDB88320 9ABFB3B6 "
        "03B6E20C 74B1D29A EAD54739 9DD277AF 04DB2615 "
        "73DC1683 E3630B12 94643B84 0D6D6A3E 7A6A5AA8 "
        "E40ECF0B 9309FF9D 0A00AE27 7D079EB1 F00F9344 "
        "8708A3D2 1E01F268 6906C2FE F762575D 806567CB "
        "196C3671 6E6B06E7 FED41B76 89D32BE0 10DA7A5A "
        "67DD4ACC F9B9DF6F 8EBEEFF9 17B7BE43 60B08ED5 "
        "D6D6A3E8 A1D1937E 38D8C2C4 4FDFF252 D1BB67F1 "
        "A6BC5767 3FB506DD 48B2364B D80D2BDA AF0A1B4C "
        "36034AF6 41047A60 DF60EFC3 A867DF55 316E8EEF "
        "4669BE79 CB61B38C BC66831A 256FD2A0 5268E236 "
        "CC0C7795 BB0B4703 220216B9 5505262F C5BA3BBE "
        "B2BD0B28 2BB45A92 5CB36A04 C2D7FFA7 B5D0CF31 "
        "2CD99E8B 5BDEAE1D 9B64C2B0 EC63F226 756AA39C "
        "026D930A 9C0906A9 EB0E363F 72076785 05005713 "
        "95BF4A82 E2B87A14 7BB12BAE 0CB61B38 92D28E9B "
        "E5D5BE0D 7CDCEFB7 0BDBDF21 86D3D2D4 F1D4E242 "
        "68DDB3F8 1FDA836E 81BE16CD F6B9265B 6FB077E1 "
        "18B74777 88085AE6 FF0F6A70 66063BCA 11010B5C "
        "8F659EFF F862AE69 616BFFD3 166CCF45 A00AE278 "
        "D70DD2EE 4E048354 3903B3C2 A7672661 D06016F7 "
        "4969474D 3E6E77DB AED16A4A D9D65ADC 40DF0B66 "
        "37D83BF0 A9BCAE53 DEBB9EC5 47B2CF7F 30B5FFE9 "
        "BDBDF21C CABAC28A 53B39330 24B4A3A6 BAD03605 "
        "CDD70693 54DE5729 23D967BF B3667A2E C4614AB8 "
        "5D681B02 2A6F2B94 B40BBE37 C30C8EA1 5A05DF1B "
        "2D02EF8D");

    qint32 crc = 0;
    qint32 x = 0;
    qint32 y = 0;

    crc ^= (-1);

    const QByteArray strData = str.toUtf8();
    const auto size = strData.size();

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    Q_ASSERT(size <= std::numeric_limits<int>::max());
#endif

    std::vector<int> convertedCharCodes;
    convertedCharCodes.resize(
        static_cast<std::size_t>(std::max<decltype(size)>(size, 0)));
    for (int i = 0; i < size; ++i) {
        int & code = convertedCharCodes[static_cast<std::size_t>(i)];
        code = static_cast<int>(static_cast<unsigned char>(strData[i]));
        if (code < 0) {
            code += 256;
        }
    }

    QString x_str;
    for (int i = 0; i < size; ++i) {
        y = (crc ^ convertedCharCodes[static_cast<std::size_t>(i)]) & 0xFF;
        x_str = crc32table.mid(y * 9, 8);
        bool conversionResult = false;
        x = static_cast<qint32>(x_str.toUInt(&conversionResult, 16));
        if (Q_UNLIKELY(!conversionResult)) {
            QNERROR(
                "utility::encryption",
                "Can't convert string representation "
                    << "of hex number " << x_str << " to unsigned int!");
            crc = 0;
            return crc;
        }

        auto unsignedCrc = static_cast<quint32>(crc);
        unsignedCrc >>= 8;
        crc = static_cast<qint32>(unsignedCrc);
        crc ^= x;
    }

    return (crc ^ (-1));
}

} // namespace quentier
