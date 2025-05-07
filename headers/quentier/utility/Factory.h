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

#pragma once

#include <quentier/utility/Fwd.h>
#include <quentier/utility/Linkage.h>

#include <QString>

namespace quentier::utility {

/**
 * Create IEncryptor instance using OpenSSL algorithms for
 * data encryption and decryption.
 */
[[nodiscard]] QUENTIER_EXPORT IEncryptorPtr createOpenSslEncryptor();

/**
 * Create IKeychainService instance based on QtKeychain library
 */
[[nodiscard]] QUENTIER_EXPORT IKeychainServicePtr newQtKeychainService();

/**
 * Create IKeychainService instance storing passwords in obfuscated form (not
 * secure!)
 */
[[nodiscard]] QUENTIER_EXPORT IKeychainServicePtr
    newObfuscatingKeychainService();

/**
 * Create IKeychainService instance which uses two keychains: primary and
 * secondary.
 *
 * When looking up a password for a key, the search goes to primary keychain
 * first, if the password is found there, it is returned. Otherwise the search
 * goes to secondary keychain. If the password is found there, it is put into
 * primary keychain and then returned.
 */
[[nodiscard]] QUENTIER_EXPORT IKeychainServicePtr newCompositeKeychainService(
    QString name, IKeychainServicePtr primaryKeychain,
    IKeychainServicePtr secondaryKeychain);

/**
 * Create IKeychainService instance which is used for gradual migration of
 * passwords between two other keychains.
 *
 * All put passwords go to sink keychain immediately. When some password is
 * read, the search would try to find it in the sink keychain and then as a
 * fallback in the source keychain. If the password is found in the source
 * keychain, it is put in the sink keychain and deleted from the source
 * keychain.
 */
[[nodiscard]] QUENTIER_EXPORT IKeychainServicePtr newMigratingKeychainService(
    IKeychainServicePtr sourceKeychain, IKeychainServicePtr sinkKeychain);

} // namespace quentier::utility

// TODO: remove after adaptation in Quentier
namespace quentier {

using utility::newQtKeychainService;
using utility::newObfuscatingKeychainService;
using utility::newCompositeKeychainService;
using utility::newMigratingKeychainService;

} // namespace quentier

