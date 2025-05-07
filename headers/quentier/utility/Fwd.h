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

#include <memory>

namespace quentier::utility {

struct IEncryptor;
using IEncryptorPtr = std::shared_ptr<IEncryptor>;

class IKeychainService;
using IKeychainServicePtr = std::shared_ptr<IKeychainService>;

} // namespace quentier::utility

namespace quentier {

// TODO: remove after adaptation to namespaced version in Quentier
using IKeychainService = utility::IKeychainService;
using IKeychainServicePtr = utility::IKeychainServicePtr;

} // namespace quentier
