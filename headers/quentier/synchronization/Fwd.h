/*
 * Copyright 2020-2023 Dmitry Ivanov
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

namespace quentier::synchronization {

class IAuthenticator;
using IAuthenticatorPtr = std::shared_ptr<IAuthenticator>;

class ISyncConflictResolver;
using ISyncConflictResolverPtr = std::shared_ptr<ISyncConflictResolver>;

class ISynchronizer;
using ISynchronizerPtr = std::shared_ptr<ISynchronizer>;

class ISyncEventsNotifier;

class ISyncOptions;
using ISyncOptionsPtr = std::shared_ptr<ISyncOptions>;

class ISyncStateStorage;
using ISyncStateStoragePtr = std::shared_ptr<ISyncStateStorage>;

} // namespace quentier::synchronization
