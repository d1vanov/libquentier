/*
 * Copyright 2023-2025 Dmitry Ivanov
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

#include "DecryptedTextCache.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/IEncryptor.h>

namespace quentier::enml {

DecryptedTextCache::DecryptedTextCache(utility::IEncryptorPtr encryptor) :
    m_encryptor{std::move(encryptor)}
{
    if (Q_UNLIKELY(!m_encryptor)) {
        throw InvalidArgument{ErrorString{
            QStringLiteral("DecryptedTextCache ctor: encryptor is null")}};
    }
}

void DecryptedTextCache::addDecryptexTextInfo(
    const QString & encryptedText, const QString & decryptedText,
    const QString & passphrase, const utility::IEncryptor::Cipher cipher,
    RememberForSession rememberForSession)
{
    QNDEBUG(
        "enml::DecryptedTextCache",
        "DecryptedTextCache::addDecryptexTextInfo: encryptedText = "
            << encryptedText << ", rememberForSession = " << rememberForSession
            << ", this = " << static_cast<const void *>(this));

    if (passphrase.isEmpty()) {
        QNWARNING(
            "enml::DecryptedTextCache",
            "Detected attempt to add decrypted text for "
                << "empty passphrase to decrypted text manager");
        return;
    }

    const std::lock_guard lock{m_mutex};

    Data & entry = m_dataHash[encryptedText];
    entry.m_decryptedText = decryptedText;
    entry.m_passphrase = passphrase;
    entry.m_cipher = cipher;
    entry.m_rememberForSession = rememberForSession;
}

std::optional<std::pair<QString, IDecryptedTextCache::RememberForSession>>
    DecryptedTextCache::findDecryptedTextInfo(
        const QString & encryptedText) const
{
    QNDEBUG(
        "enml::DecryptedTextCache",
        "DecryptedTextCache::findDecryptedTextInfo: "
            << encryptedText << ", this = " << static_cast<const void *>(this));

    const std::lock_guard lock{m_mutex};

    auto dataIt = m_dataHash.find(encryptedText);
    if (dataIt == m_dataHash.end()) {
        QNTRACE(
            "enml::DecryptedTextCache",
            "Can't find entry in the up to date data hash, trying stale hash");

        // Try the stale data hash
        dataIt = m_staleDataHash.find(encryptedText);
        if (dataIt == m_staleDataHash.end()) {
            QNTRACE(
                "enml::DecryptedTextCache",
                "Can't find entry in the stale data hash as well");
            return std::nullopt;
        }
    }

    QNTRACE("enml::DecryptedTextCache", "Found decrypted text");
    const auto & data = dataIt.value();
    return std::pair{data.m_decryptedText, data.m_rememberForSession};
}

bool DecryptedTextCache::containsRememberedForSessionEntries() const
{
    const std::lock_guard lock{m_mutex};

    const auto checkHash = [](const DataHash & dataHash) {
        for (auto it = dataHash.constBegin(), end = dataHash.constEnd();
             it != end; ++it)
        {
            if (it.value().m_rememberForSession == RememberForSession::Yes) {
                return true;
            }
        }

        return false;
    };

    if (checkHash(m_dataHash)) {
        return true;
    }

    if (checkHash(m_staleDataHash)) {
        return true;
    }

    return false;
}

void DecryptedTextCache::removeDecryptedTextInfo(const QString & encryptedText)
{
    QNDEBUG(
        "enml::DecryptedTextCache",
        "DecryptedTextCache::removeDecryptedTextInfo: encryptedText = "
            << encryptedText);

    const std::lock_guard lock{m_mutex};

    auto it = m_dataHash.find(encryptedText);
    if (it != m_dataHash.end()) {
        m_dataHash.erase(it);
        return;
    }

    it = m_staleDataHash.find(encryptedText);
    m_staleDataHash.erase(it);
}

void DecryptedTextCache::clearNonRememberedForSessionEntries()
{
    QNDEBUG(
        "enml::DecryptedTextCache",
        "DecryptedTextCache::clearNonRememberedForSessionEntries");

    const std::lock_guard lock{m_mutex};

    for (auto it = m_dataHash.begin(); it != m_dataHash.end();) {
        const Data & data = it.value();
        if (data.m_rememberForSession == RememberForSession::No) {
            it = m_dataHash.erase(it);
        }
        else {
            ++it;
        }
    }

    // Also clear the stale data hash here as it shouldn't be needed
    // after this call
    m_staleDataHash.clear();
}

std::optional<QString> DecryptedTextCache::updateDecryptedTextInfo(
    const QString & originalEncryptedText, const QString & newDecryptedText)
{
    QNDEBUG(
        "enml::DecryptedTextCache",
        "DecryptedTextCache::updateDecryptedTextInfo: "
            << "original encrypted text = " << originalEncryptedText);

    const std::lock_guard lock{m_mutex};

    bool foundInDataHash = true;
    auto it = m_dataHash.find(originalEncryptedText);
    if (it == m_dataHash.end()) {
        foundInDataHash = false;
        // Try the stale data hash instead
        it = m_staleDataHash.find(originalEncryptedText);
        if (it == m_staleDataHash.end()) {
            QNDEBUG(
                "enml::DecryptedTextCache",
                "Could not find original encrypted text");
            return std::nullopt;
        }
    }

    Data & entry = it.value();
    const QString & passphrase = entry.m_passphrase;

    const auto res = m_encryptor->encrypt(newDecryptedText, passphrase);
    if (!res.isValid()) {
        QNWARNING(
            "enml::DecryptedTextCache",
            "Could not re-encrypt the decrypted text: " << res.error());
        return std::nullopt;
    }

    const auto & newEncryptedText = res.get();
    if (foundInDataHash) {
        // Copy the previous entry's stale data to the stale data hash
        // in case it would be needed further
        auto & staleEntry = m_staleDataHash[originalEncryptedText];
        staleEntry.m_cipher = entry.m_cipher;
        staleEntry.m_rememberForSession = entry.m_rememberForSession;
        staleEntry.m_decryptedText = entry.m_decryptedText;
        staleEntry.m_passphrase = entry.m_passphrase;

        m_dataHash.erase(it);

        auto & newEntry = m_dataHash[newEncryptedText];
        newEntry.m_cipher = utility::IEncryptor::Cipher::AES;
        newEntry.m_rememberForSession = staleEntry.m_rememberForSession;
        newEntry.m_decryptedText = newDecryptedText;
        newEntry.m_passphrase = staleEntry.m_passphrase;

        return newEncryptedText;
    }

    auto & dataEntry = m_dataHash[newEncryptedText];
    dataEntry.m_cipher = entry.m_cipher;
    dataEntry.m_rememberForSession = entry.m_rememberForSession;
    dataEntry.m_decryptedText = newDecryptedText;
    dataEntry.m_passphrase = entry.m_passphrase;

    return newEncryptedText;
}

} // namespace quentier::enml
