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

#include "AuthenticationInfo.h"

#include <quentier/utility/DateTime.h>

#include <QTextStream>

#include <utility>

namespace quentier::synchronization {

QTextStream & AuthenticationInfo::print(QTextStream & strm) const
{
    strm << "AuthenticationInfo: userId = " << m_userId
         << ", authToken = " << m_authToken
         << ", authToken size = " << m_authToken.size()
         << ", authTokenExpirationTime = "
         << printableDateTimeFromTimestamp(m_authTokenExpirationTime)
         << ", authenticationTime = "
         << printableDateTimeFromTimestamp(m_authenticationTime)
         << ", shardId = " << m_shardId << ", noteStoreUrl = " << m_noteStoreUrl
         << ", webApiUrlPrefix = " << m_webApiUrlPrefix;

    strm << ", userStoreCookies: ";
    if (m_userStoreCookies.isEmpty()) {
        strm << "<empty>";
    }
    else {
        for (const auto & cookie: std::as_const(m_userStoreCookies)) {
            strm << "{" << QString::fromUtf8(cookie.toRawForm()) << "};";
        }
    }

    return strm;
}

qevercloud::UserID AuthenticationInfo::userId() const noexcept
{
    return m_userId;
}

QString AuthenticationInfo::authToken() const
{
    return m_authToken;
}

qevercloud::Timestamp AuthenticationInfo::authTokenExpirationTime()
    const noexcept
{
    return m_authTokenExpirationTime;
}

qevercloud::Timestamp AuthenticationInfo::authenticationTime() const noexcept
{
    return m_authenticationTime;
}

QString AuthenticationInfo::shardId() const
{
    return m_shardId;
}

QString AuthenticationInfo::noteStoreUrl() const
{
    return m_noteStoreUrl;
}

QString AuthenticationInfo::webApiUrlPrefix() const
{
    return m_webApiUrlPrefix;
}

QList<QNetworkCookie> AuthenticationInfo::userStoreCookies() const
{
    return m_userStoreCookies;
}

bool operator==(
    const AuthenticationInfo & lhs, const AuthenticationInfo & rhs) noexcept
{
    return lhs.m_userId == rhs.m_userId && lhs.m_authToken == rhs.m_authToken &&
        lhs.m_authTokenExpirationTime == rhs.m_authTokenExpirationTime &&
        lhs.m_authenticationTime == rhs.m_authenticationTime &&
        lhs.m_shardId == rhs.m_shardId &&
        lhs.m_noteStoreUrl == rhs.m_noteStoreUrl &&
        lhs.m_webApiUrlPrefix == rhs.m_webApiUrlPrefix &&
        lhs.m_userStoreCookies == rhs.m_userStoreCookies;
}

bool operator!=(
    const AuthenticationInfo & lhs, const AuthenticationInfo & rhs) noexcept
{
    return !(lhs == rhs);
}

} // namespace quentier::synchronization
