/*
 * Copyright 2016-2024 Dmitry Ivanov
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

#pragma once

#include <quentier/types/ErrorString.h>

#include <QList>
#include <QVector>

#include <array>
#include <cstddef>

class QDebug;
class QTextStream;

namespace quentier {

class EncryptionManagerPrivate
{
public:
    EncryptionManagerPrivate();
    ~EncryptionManagerPrivate() noexcept;

    [[nodiscard]] bool decrypt(
        const QString & encryptedText, const QString & passphrase,
        const QString & cipher, std::size_t keyLength, QString & decryptedText,
        ErrorString & errorDescription);

    [[nodiscard]] bool encrypt(
        const QString & textToEncrypt, const QString & passphrase,
        QString & cipher, std::size_t & keyLength, QString & encryptedText,
        ErrorString & errorDescription);

private:
    // AES encryption/decryption routines
    enum class SaltKind
    {
        SALT = 0,
        SALTMAC,
        IV
    };

    friend QDebug & operator<<(QDebug & dbg, SaltKind kind);
    friend QTextStream & operator<<(QTextStream & strm, SaltKind kind);

    template <class T>
    static void printSaltKind(T & t, SaltKind saltKind);

    [[nodiscard]] bool generateSalt(
        SaltKind saltKind, std::size_t saltSize,
        ErrorString & errorDescription);

    [[nodiscard]] bool generateKey(
        const QByteArray & passphraseData, const unsigned char * salt,
        std::size_t keySize, ErrorString & errorDescription);

    [[nodiscard]] bool calculateHmac(
        const QByteArray & passphraseData, const unsigned char * salt,
        const QByteArray & encryptedTextData, std::size_t keySize,
        ErrorString & errorDescription);

    [[nodiscard]] bool encyptWithAes(
        const QByteArray & textToEncrypt, QByteArray & encryptedText,
        ErrorString & errorDescription);

    [[nodiscard]] bool decryptAes(
        const QString & encryptedText, const QString & passphrase,
        QByteArray & decryptedText, ErrorString & errorDescription);

    [[nodiscard]] bool splitEncryptedData(
        const QString & encryptedData, std::size_t saltSize,
        std::size_t hmacSize, QByteArray & encryptedText,
        ErrorString & errorDescription);

private:
    // RC2 decryption routines
    [[nodiscard]] bool decryptRc2(
        const QString & encryptedText, const QString & passphrase,
        QString & decryptedText, ErrorString & errorDescription);

    void rc2KeyCodesFromPassphrase(const QString & passphrase) const;

    [[nodiscard]] QString decryptRc2Chunk(
        const QByteArray & inputCharCodes, const QVector<int> & key) const;

    [[nodiscard]] qint32 crc32(const QString & str) const;

private:
    // Evernote service defined constants
    static constexpr std::size_t s_aes_keysize = 16;
    static constexpr std::size_t s_aes_hmacsize = 32;

    std::array<unsigned char, s_aes_keysize> m_salt;
    std::array<unsigned char, s_aes_keysize> m_saltmac;
    std::array<unsigned char, s_aes_keysize> m_iv;

    std::array<unsigned char, s_aes_keysize> m_key;
    std::array<unsigned char, s_aes_hmacsize> m_hmac;

    // Cache helpers
    mutable QVector<int> m_cached_xkey;
    mutable QVector<int> m_cached_key;
    mutable std::array<int, 8> m_decrypt_rc2_chunk_key_codes;
    mutable QString m_rc2_chunk_out;
};

} // namespace quentier
