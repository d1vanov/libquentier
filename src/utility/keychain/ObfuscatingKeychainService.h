/*
 * Copyright 2020-2025 Dmitry Ivanov
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

#include <quentier/utility/Fwd.h>
#include <quentier/utility/IKeychainService.h>

namespace quentier {

/**
 * @brief The ObfuscatingKeychainService class implements IKeychainService
 * interface; it stores the data passed to it in ApplicationSettings in
 * obfuscated form. It is not really a secure storage and should not be used for
 * data which *must* be stored securely.
 */
class ObfuscatingKeychainService final : public IKeychainService
{
public:
    explicit ObfuscatingKeychainService(utility::IEncryptorPtr encryptor);

public:
    ~ObfuscatingKeychainService() noexcept override;

    [[nodiscard]] QFuture<void> writePassword(
        QString service, QString key, QString password) override;

    [[nodiscard]] QFuture<QString> readPassword(
        QString service, QString key) const override;

    [[nodiscard]] QFuture<void> deletePassword(
        QString service, QString key) override;

private:
    const utility::IEncryptorPtr m_encryptor;
};

} // namespace quentier
