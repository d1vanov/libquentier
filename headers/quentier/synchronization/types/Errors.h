/*
 * Copyright 2022-2024 Dmitry Ivanov
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

#include <quentier/utility/Linkage.h>

#include <QtGlobal>

#include <optional>
#include <variant>

namespace quentier::synchronization {

/**
 * Information about "API rate limit reached" error which Evernote servers
 * might return if too much of their API calls were made recently. In case
 * of such error synchronization should be repeated later, after some time
 * passes.
 */
struct QUENTIER_EXPORT RateLimitReachedError
{
    /**
     * Number of seconds during which since the current moment during which
     * any call to Evernote API would again result in "API rate limit
     * reached" error i.e. the number of seconds to wait for before the next
     * attempt to run synchronization
     */
    std::optional<qint32> rateLimitDurationSec;
};

[[nodiscard]] QUENTIER_EXPORT bool operator==(
    const RateLimitReachedError & lhs,
    const RateLimitReachedError & rhs) noexcept;

[[nodiscard]] QUENTIER_EXPORT bool operator!=(
    const RateLimitReachedError & lhs,
    const RateLimitReachedError & rhs) noexcept;

/**
 * Authentication expired error indicates that used authentication token has
 * expired so authentication should be repeated before the next attempt to run
 * synchronization.
 */
struct QUENTIER_EXPORT AuthenticationExpiredError
{};

[[nodiscard]] QUENTIER_EXPORT bool operator==(
    const AuthenticationExpiredError & lhs,
    const AuthenticationExpiredError & rhs) noexcept;

[[nodiscard]] QUENTIER_EXPORT bool operator!=(
    const AuthenticationExpiredError & lhs,
    const AuthenticationExpiredError & rhs) noexcept;

/**
 * Possible errors which could lead to synchronization being stopped as
 * attempts to continue would be pointless before some kind of action is done.
 * If the hold alternative is std::monostate, it means there is no error.
 */
using StopSynchronizationError = std::variant<
    RateLimitReachedError, AuthenticationExpiredError, std::monostate>;

} // namespace quentier::synchronization
