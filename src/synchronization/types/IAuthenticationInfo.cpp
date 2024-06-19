/*
 * Copyright 2024 Dmitry Ivanov
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

#include <quentier/synchronization/types/IAuthenticationInfo.h>

#include "AuthenticationInfo.h"
#include "Utils.h"

#include <QJsonArray>

#include <string_view>
#include <utility>

namespace quentier::synchronization {

using namespace std::string_view_literals;

namespace {

constexpr auto gUserIdKey = "userId"sv;
constexpr auto gAuthTokenKey = "authToken"sv;
constexpr auto gAuthTokenExpirationTimeKey = "authTokenExpirationTime"sv;
constexpr auto gAuthenticationTimeKey = "authenticationTime"sv;
constexpr auto gShardIdKey = "shardId"sv;
constexpr auto gNoteStoreUrlKey = "noteStoreUrl"sv;
constexpr auto gWebApiUrlPrefixKey = "webApiUrlPrefix"sv;
constexpr auto gUserStoreCookiesKey = "userStoreCookies"sv;
constexpr auto gUserStoreCookieNameKey = "name"sv;
constexpr auto gUserStoreCookieValueKey = "value"sv;

[[nodiscard]] QString toStr(const std::string_view key)
{
    return synchronization::toString(key);
}

} // namespace

QJsonObject IAuthenticationInfo::serializeToJson() const
{
    QJsonObject object;

    object[toStr(gUserIdKey)] = userId();
    object[toStr(gAuthTokenKey)] = authToken();
    object[toStr(gAuthTokenExpirationTimeKey)] =
        QString::number(authTokenExpirationTime());
    object[toStr(gAuthenticationTimeKey)] =
        QString::number(authenticationTime());
    object[toStr(gShardIdKey)] = shardId();
    object[toStr(gNoteStoreUrlKey)] = noteStoreUrl();
    object[toStr(gWebApiUrlPrefixKey)] = webApiUrlPrefix();

    const auto & cookies = userStoreCookies();
    if (!cookies.isEmpty()) {
        QJsonArray cookiesArray;
        for (const auto & cookie: std::as_const(cookies)) {
            QJsonObject cookieObject;
            cookieObject[toStr(gUserStoreCookieNameKey)] =
                QString::fromUtf8(cookie.name().toBase64());
            cookieObject[toStr(gUserStoreCookieValueKey)] =
                QString::fromUtf8(cookie.value().toBase64());
            cookiesArray << cookieObject;
        }
        object[toStr(gUserStoreCookiesKey)] = cookiesArray;
    }

    return object;
}

IAuthenticationInfoPtr IAuthenticationInfo::deserializeFromJson(
    const QJsonObject & json)
{
    const auto userIdIt = json.constFind(toStr(gUserIdKey));
    if (userIdIt == json.constEnd() || !userIdIt->isDouble()) {
        return nullptr;
    }

    const auto authTokenIt = json.constFind(toStr(gAuthTokenKey));
    if (authTokenIt == json.constEnd() || !authTokenIt->isString()) {
        return nullptr;
    }

    const auto authTokenExpirationTimeIt =
        json.constFind(toStr(gAuthTokenExpirationTimeKey));
    if (authTokenExpirationTimeIt == json.constEnd() ||
        !authTokenExpirationTimeIt->isString())
    {
        return nullptr;
    }

    qevercloud::Timestamp authTokenExpirationTime = 0;
    {
        bool authTokenExpirationTimeOk = false;
        authTokenExpirationTime =
            authTokenExpirationTimeIt->toString().toLongLong(
                &authTokenExpirationTimeOk);
        if (!authTokenExpirationTimeOk) {
            return nullptr;
        }
    }

    const auto authenticationTimeIt =
        json.constFind(toStr(gAuthenticationTimeKey));
    if (authenticationTimeIt == json.constEnd() ||
        !authenticationTimeIt->isString())
    {
        return nullptr;
    }

    qevercloud::Timestamp authenticationTime = 0;
    {
        bool authenticationTimeOk = false;
        authenticationTime =
            authenticationTimeIt->toString().toLongLong(&authenticationTimeOk);
        if (!authenticationTimeOk) {
            return nullptr;
        }
    }

    const auto shardIdIt = json.constFind(toStr(gShardIdKey));
    if (shardIdIt == json.constEnd() || !shardIdIt->isString()) {
        return nullptr;
    }

    const auto noteStoreUrlIt = json.constFind(toStr(gNoteStoreUrlKey));
    if (noteStoreUrlIt == json.constEnd() || !noteStoreUrlIt->isString()) {
        return nullptr;
    }

    const auto webApiUrlPrefixIt = json.constFind(toStr(gWebApiUrlPrefixKey));
    if (webApiUrlPrefixIt == json.constEnd() || !webApiUrlPrefixIt->isString())
    {
        return nullptr;
    }

    QList<QNetworkCookie> userStoreCookies;
    const auto cookiesIt = json.constFind(toStr(gUserStoreCookiesKey));
    if (cookiesIt != json.constEnd()) {
        if (!cookiesIt->isArray()) {
            return nullptr;
        }

        const auto cookiesArray = cookiesIt->toArray();
        for (auto it = cookiesArray.constBegin(); it != cookiesArray.constEnd();
             ++it)
        {
            const auto & cookieItem = *it;
            if (!cookieItem.isObject()) {
                return nullptr;
            }

            const auto cookieObject = cookieItem.toObject();
            const auto nameIt =
                cookieObject.constFind(toStr(gUserStoreCookieNameKey));
            if (nameIt == cookieObject.constEnd() || !nameIt->isString()) {
                return nullptr;
            }

            const auto valueIt =
                cookieObject.constFind(toStr(gUserStoreCookieValueKey));
            if (valueIt == cookieObject.constEnd() || !valueIt->isString()) {
                return nullptr;
            }

            userStoreCookies << QNetworkCookie{
                QByteArray::fromBase64(nameIt.value().toString().toUtf8()),
                QByteArray::fromBase64(valueIt.value().toString().toUtf8())};
        }
    }

    auto authenticationInfo = std::make_shared<AuthenticationInfo>();
    authenticationInfo->m_userId = userIdIt->toInt();
    authenticationInfo->m_authToken = authTokenIt->toString();
    authenticationInfo->m_authTokenExpirationTime = authTokenExpirationTime;
    authenticationInfo->m_authenticationTime = authenticationTime;
    authenticationInfo->m_shardId = shardIdIt->toString();
    authenticationInfo->m_noteStoreUrl = noteStoreUrlIt->toString();
    authenticationInfo->m_webApiUrlPrefix = webApiUrlPrefixIt->toString();
    authenticationInfo->m_userStoreCookies = std::move(userStoreCookies);

    return authenticationInfo;
}

} // namespace quentier::synchronization
