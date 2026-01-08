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

#include <quentier/utility/IKeychainService.h>

namespace quentier::utility::keychain {

/**
 * @brief The MigratingKeychainService class implements IKeychainService
 * interface and works as follows: it tries to gradually migrate the password
 * data from the source keychain into the sink keychain.
 *
 * This class can be used for keychain migrations from some old one to some new
 * one. First the old keychain is switched to the migrating keychain and then,
 * one-two releases later, migrating keychain can be replaced with just the new
 * keychain. Presumably by this time all existing users passwords should have
 * been migrated from the old keychain to the new one via the migrating
 * keychain.
 */
class MigratingKeychainService final : public IKeychainService
{
public:
    MigratingKeychainService(
        IKeychainServicePtr sourceKeychain, IKeychainServicePtr sinkKeychain);

    ~MigratingKeychainService() noexcept override;

    /**
     * Passwords are written only to the sink keychain.
     */
    [[nodiscard]] QFuture<void> writePassword(
        QString service, QString key, QString password) override;

    /**
     * Passwords are first attempted to read from the sink keychain.
     * If the resulting error code is "entry not found", the attempt is made
     * to read from the source keychain. If reading from the source keychain
     * succeeds, the password is written to the sink keychain and then
     * returned to the user. After successful writing to the sink keychain
     * the attempt to delete the password from the source keychain is being
     * made.
     */
    [[nodiscard]] QFuture<QString> readPassword(
        QString service, QString key) const override;

    /**
     * Passwords are attempted to be deleted from both sink and source
     * keychains.
     */
    [[nodiscard]] QFuture<void> deletePassword(
        QString service, QString key) override;

private:
    const IKeychainServicePtr m_sourceKeychain;
    const IKeychainServicePtr m_sinkKeychain;
};

} // namespace quentier::utility::keychain
