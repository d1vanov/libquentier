/*
 * Copyright 2022 Dmitry Ivanov
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

#include <qevercloud/Fwd.h>
#include <qevercloud/types/TypeAliases.h>
#include <qevercloud/types/User.h>

#include <QFuture>

namespace quentier::synchronization {

/**
 * @brief The IUserInfoProvider interface provides information about
 * Evernote users.
 */
class IUserInfoProvider
{
public:
    virtual ~IUserInfoProvider() = default;

    /**
     * Find full information about current authenticated Evernote user.
     * @param ctx           Request context with authentication token
     *                      identifyng the user the information about which
     *                      is being requested.
     * @return              Future with full user info or exception if no user
     *                      info is found
     */
    [[nodiscard]] virtual QFuture<qevercloud::User> userInfo(
        qevercloud::IRequestContextPtr ctx) = 0;
};

} // namespace quentier::synchronization
