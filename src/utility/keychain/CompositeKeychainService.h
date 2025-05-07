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

#include <QHash>
#include <QSet>

#include <memory>

namespace quentier::utility::keychain {

/**
 * @brief The CompositeKeychainService class implements IKeychainService
 * interface; it uses two other keychain services to implement its functionality
 */
class CompositeKeychainService final :
    public IKeychainService,
    public std::enable_shared_from_this<CompositeKeychainService>
{
public:
    /**
     * CompositeKeychainService constructor
     * @param name                  Name of the composite keychain, required to
     *                              distinguish one composite keychain from
     *                              the other
     * @param primaryKeychain       Primary keychain
     * @param secondaryKeychain     Secondary keychain
     */
    CompositeKeychainService(
        QString name, IKeychainServicePtr primaryKeychain,
        IKeychainServicePtr secondaryKeychain);

    ~CompositeKeychainService() noexcept override;

    /**
     * Passwords are written to both primary and secondary keychains. The
     * results are handled as follows:
     * 1. If writing to both primary and secondary keychain succeeds, the status
     *    is reported to the user.
     * 2. If writing fails for the primary keychain but succeeds for the
     *    secondary one, service + key pair is stored persistently as the one
     *    for which the password is available only in the secondary keychain.
     *    Successful status of writing is reported to the user.
     * 3. If writing fails for the secondary keychain but succeeds for the
     *    primary one, service + key pair is stored persistently as the one
     *    for which the password is available only in the primary keychain.
     *    Succesful status of writing is reported to the user.
     * 4. If writing to both keychains fails, the error from the primary
     *    keychain is returned to the user.
     */
    [[nodiscard]] QFuture<void> writePassword(
        QString service, QString key, QString password) override;

    /**
     * Passwords are read as follows:
     * 1. If service and key pair is not marked as the one for which the
     *    password is not available in the primary keychain, the read is first
     *    attempted from the primary keychain.
     * 2. If reading from the primary keychain fails or if it is skipped
     *    altogether due to the reasons from p. 1, an attempt is made to read
     *    the password from the secondary keychain unless service and key pair
     *    is marked as the one for which the password is not available in the
     *    secondary keychain.
     * 3. If the password is not available in either keychain, reading fails.
     *    Otherwise the first successful result (if any) is set into the future.
     */
    [[nodiscard]] QFuture<QString> readPassword(
        QString service, QString key) const override;

    /**
     * Passwords are deleted from both primary and secondary keychains unless
     * service and key pair is marked as unavailable in either of these
     * keychains. The mark is stored persistently. If deletion fails for either
     * keychain, service and key pair is marked as the one for which the
     * password is not available in the corresponding keychain.
     */
    [[nodiscard]] QFuture<void> deletePassword(
        QString service, QString key) override;

private:
    void markServiceKeyPairAsUnavailableInPrimaryKeychain(
        const QString & service, const QString & key);

    void unmarkServiceKeyPairAsUnavailableInPrimaryKeychain(
        const QString & service, const QString & key);

    [[nodiscard]] bool isServiceKeyPairAvailableInPrimaryKeychain(
        const QString & service, const QString & key) const;

    void markServiceKeyPairAsUnavailableInSecondaryKeychain(
        const QString & service, const QString & key);

    void unmarkServiceKeyPairAsUnavailableInSecondaryKeychain(
        const QString & service, const QString & key);

    [[nodiscard]] bool isServiceKeyPairAvailableInSecondaryKeychain(
        const QString & service, const QString & key) const;

    void persistUnavailableServiceKeyPairs(
        const char * groupName, const QString & service, const QString & key);

    void checkAndInitializeServiceKeysCaches() const;

    using ServiceKeyPairsCache = QHash<QString, QSet<QString>>;

    [[nodiscard]] ServiceKeyPairsCache
    readServiceKeyPairsUnavailableInPrimaryKeychain() const;

    [[nodiscard]] ServiceKeyPairsCache
    readServiceKeyPairsUnavailableInSecondaryKeychain() const;

    [[nodiscard]] ServiceKeyPairsCache
    readServiceKeyPairsUnavailableInKeychainImpl(const char * groupName) const;

private:
    const QString m_name;
    const IKeychainServicePtr m_primaryKeychain;
    const IKeychainServicePtr m_secondaryKeychain;

    mutable ServiceKeyPairsCache m_serviceKeysUnavailableInPrimaryKeychain;
    mutable ServiceKeyPairsCache m_serviceKeysUnavailableInSecondaryKeychain;
    mutable bool m_serviceKeysCachesInitialized = false;
};

} // namespace quentier::utility::keychain
