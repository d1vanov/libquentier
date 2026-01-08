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

#pragma once

#include <quentier/synchronization/types/Fwd.h>
#include <quentier/utility/Linkage.h>

#include <qevercloud/types/TypeAliases.h>

#include <QList>
#include <QNetworkCookie>
#include <QString>

namespace quentier::synchronization {

class QUENTIER_EXPORT IAuthenticationInfoBuilder
{
public:
    virtual ~IAuthenticationInfoBuilder() noexcept;

    virtual IAuthenticationInfoBuilder & setUserId(
        qevercloud::UserID userId) = 0;

    virtual IAuthenticationInfoBuilder & setAuthToken(QString token) = 0;

    virtual IAuthenticationInfoBuilder & setAuthTokenExpirationTime(
        qevercloud::Timestamp expirationTime) = 0;

    virtual IAuthenticationInfoBuilder & setAuthenticationTime(
        qevercloud::Timestamp authenticationTime) = 0;

    virtual IAuthenticationInfoBuilder & setShardId(QString shardId) = 0;

    virtual IAuthenticationInfoBuilder & setNoteStoreUrl(
        QString noteStoreUrl) = 0;

    virtual IAuthenticationInfoBuilder & setWebApiUrlPrefix(
        QString webApiUrlPrefix) = 0;

    virtual IAuthenticationInfoBuilder & setUserStoreCookies(
        QList<QNetworkCookie> cookies) = 0;

    [[nodiscard]] virtual IAuthenticationInfoPtr build() = 0;
};

[[nodiscard]] QUENTIER_EXPORT IAuthenticationInfoBuilderPtr
    createAuthenticationInfoBuilder();

} // namespace quentier::synchronization
