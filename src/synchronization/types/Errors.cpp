/*
 * Copyright 2022-2023 Dmitry Ivanov
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

#include <quentier/synchronization/types/Errors.h>

namespace quentier::synchronization {

bool operator==(
    const RateLimitReachedError & lhs,
    const RateLimitReachedError & rhs) noexcept
{
    return lhs.rateLimitDurationSec == rhs.rateLimitDurationSec;
}

bool operator!=(
    const RateLimitReachedError & lhs,
    const RateLimitReachedError & rhs) noexcept
{
    return !(lhs == rhs);
}

bool operator==(
    [[maybe_unused]] const AuthenticationExpiredError & lhs,
    [[maybe_unused]] const AuthenticationExpiredError & rhs) noexcept
{
    return true;
}

bool operator!=(
    const AuthenticationExpiredError & lhs,
    const AuthenticationExpiredError & rhs) noexcept
{
    return !(lhs == rhs);
}

} // namespace quentier::synchronization
