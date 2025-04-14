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

#include <quentier/utility/IEncryptor.h>
#include <quentier/utility/Linkage.h>

#include <QString>

#include <optional>
#include <utility>

class QDebug;
class QTextStream;

namespace quentier::enml {

class QUENTIER_EXPORT IDecryptedTextCache
{
public:
    virtual ~IDecryptedTextCache();

    enum class RememberForSession
    {
        Yes,
        No
    };

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & dbg, RememberForSession rememberForSession);

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, RememberForSession rememberForSession);

    virtual void addDecryptexTextInfo(
        const QString & encryptedText, const QString & decryptedText,
        const QString & passphrase, IEncryptor::Cipher cipher,
        RememberForSession rememberForSession) = 0;

    [[nodiscard]] virtual std::optional<std::pair<QString, RememberForSession>>
        findDecryptedTextInfo(const QString & encryptedText) const = 0;

    [[nodiscard]] virtual std::optional<QString> updateDecryptedTextInfo(
        const QString & originalEncryptedText,
        const QString & newDecryptedText) = 0;

    virtual void removeDecryptedTextInfo(const QString & encryptedText) = 0;
    virtual void clearNonRememberedForSessionEntries() = 0;
};

} // namespace quentier::enml
