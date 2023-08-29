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

#include "AuthenticationInfoBuilder.h"
#include "AuthenticationInfo.h"

namespace quentier::synchronization {

IAuthenticationInfoBuilder & AuthenticationInfoBuilder::setUserId(
    qevercloud::UserID userId) noexcept
{
    m_userId = userId;
    return *this;
}

IAuthenticationInfoBuilder & AuthenticationInfoBuilder::setAuthToken(
    QString authToken)
{
    m_authToken = std::move(authToken);
    return *this;
}

IAuthenticationInfoBuilder &
    AuthenticationInfoBuilder::setAuthTokenExpirationTime(
        qevercloud::Timestamp expirationTime) noexcept
{
    m_authTokenExpirationTime = expirationTime;
    return *this;
}

IAuthenticationInfoBuilder & AuthenticationInfoBuilder::setAuthenticationTime(
    qevercloud::Timestamp authenticationTime) noexcept
{
    m_authenticationTime = authenticationTime;
    return *this;
}

IAuthenticationInfoBuilder & AuthenticationInfoBuilder::setShardId(
    QString shardId)
{
    m_shardId = std::move(shardId);
    return *this;
}

IAuthenticationInfoBuilder & AuthenticationInfoBuilder::setNoteStoreUrl(
    QString noteStoreUrl)
{
    m_noteStoreUrl = std::move(noteStoreUrl);
    return *this;
}

IAuthenticationInfoBuilder & AuthenticationInfoBuilder::setWebApiUrlPrefix(
    QString webApiUrlPrefix)
{
    m_webApiUrlPrefix = std::move(webApiUrlPrefix);
    return *this;
}

IAuthenticationInfoBuilder & AuthenticationInfoBuilder::setUserStoreCookies(
    QList<QNetworkCookie> userStoreCookies)
{
    m_userStoreCookies = std::move(userStoreCookies);
    return *this;
}

IAuthenticationInfoPtr AuthenticationInfoBuilder::build()
{
    auto info = std::make_shared<AuthenticationInfo>();
    info->m_userId = m_userId;
    info->m_authToken = std::move(m_authToken);
    info->m_authTokenExpirationTime = m_authTokenExpirationTime;
    info->m_authenticationTime = m_authenticationTime;
    info->m_shardId = std::move(m_shardId);
    info->m_noteStoreUrl = std::move(m_noteStoreUrl);
    info->m_webApiUrlPrefix = std::move(m_webApiUrlPrefix);
    info->m_userStoreCookies = std::move(m_userStoreCookies);

    m_userId = 0;
    m_authToken = QString{};
    m_authTokenExpirationTime = 0;
    m_authenticationTime = 0;
    m_shardId = QString{};
    m_noteStoreUrl = QString{};
    m_webApiUrlPrefix = QString{};
    m_userStoreCookies = {};

    return info;
}

} // namespace quentier::synchronization
