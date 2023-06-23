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

#include "ExceptionUtils.h"

#include <qevercloud/exceptions/builders/EDAMNotFoundExceptionBuilder.h>
#include <qevercloud/exceptions/builders/EDAMSystemExceptionBuilder.h>
#include <qevercloud/exceptions/builders/EDAMUserExceptionBuilder.h>

namespace quentier::synchronization::tests::utils {

qevercloud::EDAMNotFoundException createNotFoundException(
    QString identifier, std::optional<QString> key)
{
    return qevercloud::EDAMNotFoundExceptionBuilder{}
        .setIdentifier(std::move(identifier))
        .setKey(std::move(key))
        .build();
}

qevercloud::EDAMUserException createUserException(
    const qevercloud::EDAMErrorCode errorCode, QString parameter)
{
    return qevercloud::EDAMUserExceptionBuilder{}
        .setErrorCode(errorCode)
        .setParameter(std::move(parameter))
        .build();
}

qevercloud::EDAMSystemException createStopSyncException(
    const StopSynchronizationError & error)
{
    qevercloud::EDAMSystemExceptionBuilder builder;

    if (std::holds_alternative<RateLimitReachedError>(error)) {
        builder.setErrorCode(qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED)
            .setRateLimitDuration(300)
            .setMessage(QStringLiteral("Rate limit reached"));
    }
    else if (std::holds_alternative<AuthenticationExpiredError>(error)) {
        builder.setErrorCode(qevercloud::EDAMErrorCode::AUTH_EXPIRED)
            .setMessage(QStringLiteral("Authentication expired"));
    }

    return builder.build();
}

} // namespace quentier::synchronization::tests::utils
