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

#include <quentier/synchronization/types/Fwd.h>
#include <quentier/types/Account.h>

#include <qevercloud/types/LinkedNotebook.h>

#include <QFuture>

#include <variant>
#include <utility>

namespace quentier::synchronization {

/**
 * @brief The IAuthenticationInfoProvider interface provides the means to
 * receive authentication info for particular accounts.
 */
class IAuthenticationInfoProvider
{
public:
    virtual ~IAuthenticationInfoProvider() = default;

    /**
     * Mode determining the potential source of authentication info returned
     * by the provider
     */
    enum class Mode
    {
        /**
         * In Cache mode the returned authentication info might be the info
         * already received from Evernote earlier and fetched from the local
         * cache if the info has not expired yet.
         */
        Cache,
        /**
         * In NoCache mode the authentication info is requested from Evernote
         * bypassing the local cache - typically it is for cases in which
         * the previous attempt to interact with Evernote API has resulted
         * in error saying that previously received authentication info has
         * expired.
         */
        NoCache
    };

    /**
     * Provides authentication info for a new account. The authentication
     * info is requested from Evernote directy as there is no local cache.
     *
     * @return                          QFuture with account and its
     *                                  authentication info or with exception
     *                                  in case of error.
     */
    [[nodiscard]] virtual QFuture<std::pair<Account, IAuthenticationInfoPtr>>
        authenticateNewAccount() = 0;

    /**
     * Provides authentication info for an existing account. The authentication
     * info source depends on the mode parameter which determines whether the
     * info is requested from Evernote or whether local cache is tried first.
     *
     * @param account                   Account for which authentication info is
     *                                  requested.
     * @param mode                      Mode determining the source of
     *                                  authentication info.
     * @return                          QFuture with authentication info for the
     *                                  provided account or with exception in
     *                                  case of error.
     */
    [[nodiscard]] virtual QFuture<IAuthenticationInfoPtr> authenticateAccount(
        Account account, Mode mode = Mode::Cache) = 0;

    /**
     * Provides authentication info for a linked notebook within an existing
     * account. The authentication info source depends on the mode parameter
     * which determines whether the info is requested from Evernote or whether
     * local cache is tried first.
     *
     * @param account                   Account for which linked notebook the
     *                                  authentication info is requested.
     * @param linkedNotebook            Linked notebook for which
     *                                  authentication info is required.
     * @param mode                      Mode determining the source of
     *                                  authentication info.
     * @return                          QFuture with authentication info for the
     *                                  provided account and linked notebook or
     *                                  with exception in case of error.
     */
    [[nodiscard]] virtual QFuture<IAuthenticationInfoPtr>
        authenticateToLinkedNotebook(
            Account account, qevercloud::LinkedNotebook linkedNotebook,
            Mode mode = Mode::Cache) = 0;

    struct ClearCacheOption
    {
        struct All
        {};

        struct User
        {
            qevercloud::UserID id;
        };

        struct AllUsers
        {};

        struct LinkedNotebook
        {
            qevercloud::Guid guid;
        };

        struct AllLinkedNotebooks
        {};
    };

    using ClearCacheOptions = std::variant<
        ClearCacheOption::All, ClearCacheOption::User,
        ClearCacheOption::AllUsers, ClearCacheOption::LinkedNotebook,
        ClearCacheOption::AllLinkedNotebooks>;

    virtual void clearCaches(
        const ClearCacheOptions & clearCacheOptions = ClearCacheOptions{
            ClearCacheOption::All{}}) = 0;
};

[[nodiscard]] bool operator==(
    const IAuthenticationInfoProvider::ClearCacheOption::All & lhs,
    const IAuthenticationInfoProvider::ClearCacheOption::All & rhs) noexcept;

[[nodiscard]] bool operator!=(
    const IAuthenticationInfoProvider::ClearCacheOption::All & lhs,
    const IAuthenticationInfoProvider::ClearCacheOption::All & rhs) noexcept;

[[nodiscard]] bool operator==(
    const IAuthenticationInfoProvider::ClearCacheOption::User & lhs,
    const IAuthenticationInfoProvider::ClearCacheOption::User & rhs) noexcept;

[[nodiscard]] bool operator!=(
    const IAuthenticationInfoProvider::ClearCacheOption::User & lhs,
    const IAuthenticationInfoProvider::ClearCacheOption::User & rhs) noexcept;

[[nodiscard]] bool operator==(
    const IAuthenticationInfoProvider::ClearCacheOption::AllUsers & lhs,
    const IAuthenticationInfoProvider::ClearCacheOption::AllUsers &
        rhs) noexcept;

[[nodiscard]] bool operator!=(
    const IAuthenticationInfoProvider::ClearCacheOption::AllUsers & lhs,
    const IAuthenticationInfoProvider::ClearCacheOption::AllUsers &
        rhs) noexcept;

[[nodiscard]] bool operator==(
    const IAuthenticationInfoProvider::ClearCacheOption::LinkedNotebook & lhs,
    const IAuthenticationInfoProvider::ClearCacheOption::LinkedNotebook &
        rhs) noexcept;

[[nodiscard]] bool operator!=(
    const IAuthenticationInfoProvider::ClearCacheOption::LinkedNotebook & lhs,
    const IAuthenticationInfoProvider::ClearCacheOption::LinkedNotebook &
        rhs) noexcept;

[[nodiscard]] bool operator==(
    const IAuthenticationInfoProvider::ClearCacheOption::AllLinkedNotebooks &
        lhs,
    const IAuthenticationInfoProvider::ClearCacheOption::AllLinkedNotebooks &
        rhs) noexcept;

[[nodiscard]] bool operator!=(
    const IAuthenticationInfoProvider::ClearCacheOption::AllLinkedNotebooks &
        lhs,
    const IAuthenticationInfoProvider::ClearCacheOption::AllLinkedNotebooks &
        rhs) noexcept;

} // namespace quentier::synchronization
