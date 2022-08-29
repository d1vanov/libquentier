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

#include <qevercloud/EDAMErrorCode.h>
#include <qevercloud/Fwd.h>
#include <qevercloud/types/AccountLimits.h>

#include <QFuture>

namespace quentier::synchronization {

/**
 * @brief The IAccountLimitsProvider provides information about account limits
 */
class IAccountLimitsProvider
{
public:
    virtual ~IAccountLimitsProvider() = default;

    /**
     * Find account limits for particular Evernote service level
     * @param serviceLevel  Service level for which account limits are requested
     * @param ctx           Request context for fetching of account limits
     * @return              Future with account limits or exception in case of
     *                      error
     */
    [[nodiscard]] virtual QFuture<qevercloud::AccountLimits> accountLimits(
        qevercloud::ServiceLevel serviceLevel,
        qevercloud::IRequestContextPtr ctx) = 0;
};

} // namespace quentier::synchronization
