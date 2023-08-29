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

#include <quentier/synchronization/types/IAuthenticationInfoBuilder.h>

namespace quentier::synchronization {

class AuthenticationInfoBuilder final : public IAuthenticationInfoBuilder
{
public:
    IAuthenticationInfoBuilder & setUserId(
        qevercloud::UserID userId) noexcept override;

    IAuthenticationInfoBuilder & setAuthToken(QString token) override;

    IAuthenticationInfoBuilder & setAuthTokenExpirationTime(
        qevercloud::Timestamp expirationTime) noexcept override;

    IAuthenticationInfoBuilder & setAuthenticationTime(
        qevercloud::Timestamp authenticationTime) noexcept override;

    IAuthenticationInfoBuilder & setShardId(QString shardId) override;

    IAuthenticationInfoBuilder & setNoteStoreUrl(
        QString noteStoreUrl) override;

    IAuthenticationInfoBuilder & setWebApiUrlPrefix(
        QString webApiUrlPrefix) override;

    IAuthenticationInfoBuilder & setUserStoreCookies(
        QList<QNetworkCookie> cookies) override;

    [[nodiscard]] IAuthenticationInfoPtr build() override;

private:
    qevercloud::UserID m_userId = 0;
    QString m_authToken;
    qevercloud::Timestamp m_authTokenExpirationTime = 0;
    qevercloud::Timestamp m_authenticationTime = 0;
    QString m_shardId;
    QString m_noteStoreUrl;
    QString m_webApiUrlPrefix;
    QList<QNetworkCookie> m_userStoreCookies;
};

} // namespace quentier::synchronization
