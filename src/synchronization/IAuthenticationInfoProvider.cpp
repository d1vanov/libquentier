/*
 * Copyright 2023 Dmitry Ivanov
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

#include "IAuthenticationInfoProvider.h"

namespace quentier::synchronization {

bool operator==(
    [[maybe_unused]] const IAuthenticationInfoProvider::ClearCacheOption::All &
        lhs,
    [[maybe_unused]] const IAuthenticationInfoProvider::ClearCacheOption::All &
        rhs) noexcept
{
    return true;
}

bool operator!=(
    const IAuthenticationInfoProvider::ClearCacheOption::All & lhs,
    const IAuthenticationInfoProvider::ClearCacheOption::All & rhs) noexcept
{
    return !(lhs == rhs);
}

bool operator==(
    const IAuthenticationInfoProvider::ClearCacheOption::User & lhs,
    const IAuthenticationInfoProvider::ClearCacheOption::User & rhs) noexcept
{
    return lhs.id == rhs.id;
}

bool operator!=(
    const IAuthenticationInfoProvider::ClearCacheOption::User & lhs,
    const IAuthenticationInfoProvider::ClearCacheOption::User & rhs) noexcept
{
    return !(lhs == rhs);
}

bool operator==(
    [[maybe_unused]] const IAuthenticationInfoProvider::ClearCacheOption::
        AllUsers & lhs,
    [[maybe_unused]] const IAuthenticationInfoProvider::ClearCacheOption::
        AllUsers & rhs) noexcept
{
    return true;
}

bool operator!=(
    const IAuthenticationInfoProvider::ClearCacheOption::AllUsers & lhs,
    const IAuthenticationInfoProvider::ClearCacheOption::AllUsers &
        rhs) noexcept
{
    return !(lhs == rhs);
}

bool operator==(
    const IAuthenticationInfoProvider::ClearCacheOption::LinkedNotebook & lhs,
    const IAuthenticationInfoProvider::ClearCacheOption::LinkedNotebook &
        rhs) noexcept
{
    return lhs.guid == rhs.guid;
}

bool operator!=(
    const IAuthenticationInfoProvider::ClearCacheOption::LinkedNotebook & lhs,
    const IAuthenticationInfoProvider::ClearCacheOption::LinkedNotebook &
        rhs) noexcept
{
    return !(lhs == rhs);
}

bool operator==(
    [[maybe_unused]] const IAuthenticationInfoProvider::ClearCacheOption::
        AllLinkedNotebooks & lhs,
    [[maybe_unused]] const IAuthenticationInfoProvider::ClearCacheOption::
        AllLinkedNotebooks & rhs) noexcept
{
    return true;
}

bool operator!=(
    [[maybe_unused]] const IAuthenticationInfoProvider::ClearCacheOption::
        AllLinkedNotebooks & lhs,
    [[maybe_unused]] const IAuthenticationInfoProvider::ClearCacheOption::
        AllLinkedNotebooks & rhs) noexcept
{
    return !(lhs == rhs);
}

} // namespace quentier::synchronization
