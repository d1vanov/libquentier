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

#pragma once

#include <quentier/types/ErrorString.h>
#include <quentier/types/Result.h>
#include <quentier/utility/Linkage.h>

#include <QString>

namespace quentier {

/**
 * @brief The IEncryptor interface provides encryption and decryption
 * functionality which is compatible with that used by Evernote service
 */
struct QUENTIER_EXPORT IEncryptor
{
    virtual ~IEncryptor() noexcept;

    /**
     * Cipher used for encryption/decryption
     */
    enum class Cipher
    {
        /**
         * RC2 64 bit block cipher
         */
        RC2,
        /**
         * AES 128 bit block cipher
         */
        AES
    };

    /**
     * Encrypt text fragment using AES cipher (RC2 cipher is only used for
     * decryption)
     * @param text Text to encrypt
     * @param passphrase Passphrase which can be used to decrypt the text
     * @return Result with either encrypted text or error message
     */
    [[nodiscard]] virtual Result<QString, ErrorString> encrypt(
        const QString & text, const QString & passphrase) = 0;

    /**
     * Decrypt previously encrypted text fragment
     * @param encryptedText Encrypted text to decrypt
     * @param passphrase Passhprase used to encrypt text
     * @param cipher Cipher used to encrypt text
     * @return Result with either decrypted text or error message
     */
    [[nodiscard]] virtual Result<QString, ErrorString> decrypt(
        const QString & encryptedText, const QString & passphrase,
        Cipher cipher) = 0;
};

} // namespace quentier
