/*
 * Copyright 2023-2024 Dmitry Ivanov
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

#include <quentier/enml/IDecryptedTextCache.h>
#include <quentier/utility/Fwd.h>

#include <QHash>
#include <QtGlobal>

namespace quentier::enml {

class DecryptedTextCache: public IDecryptedTextCache
{
public:
    explicit DecryptedTextCache(IEncryptorPtr encryptor);

public: // IDecryptedTextCache
    void addDecryptexTextInfo(
        const QString & encryptedText, const QString & decryptedText,
        const QString & passphrase, IEncryptor::Cipher cipher,
        RememberForSession rememberForSession) override;

    [[nodiscard]] std::optional<std::pair<QString, RememberForSession>>
        findDecryptedTextInfo(const QString & encryptedText) const override;

    [[nodiscard]] std::optional<QString> updateDecryptedTextInfo(
        const QString & originalEncryptedText,
        const QString & newDecryptedText) override;

    void removeDecryptedTextInfo(const QString & encryptedText) override;
    void clearNonRememberedForSessionEntries() override;

private:
    struct Data
    {
        QString m_decryptedText;
        QString m_passphrase;
        IEncryptor::Cipher m_cipher;
        RememberForSession m_rememberForSession = RememberForSession::No;
    };

    using DataHash = QHash<QString, Data>;

private:
    DataHash m_dataHash;
    DataHash m_staleDataHash;
    IEncryptorPtr m_encryptor;
};

} // namespace quentier::enml
