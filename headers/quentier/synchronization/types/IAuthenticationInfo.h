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

#include <quentier/utility/Linkage.h>
#include <quentier/utility/Printable.h>

#include <qevercloud/types/TypeAliases.h>

#include <QList>
#include <QNetworkCookie>
#include <QString>

namespace quentier::synchronization {

/**
 * @brief The IAuthenticationInfo interface represents the information
 * obtained through OAuth and necessary to access Evernote API
 */
class QUENTIER_EXPORT IAuthenticationInfo : public Printable
{
public:
    /**
     * Identifier of the authenticated user
     */
    [[nodiscard]] virtual qevercloud::UserID userId() const = 0;

    /**
     * Authentication token which needs to be used for access to Evernote API
     */
    [[nodiscard]] virtual QString authToken() const = 0;

    /**
     * Expiration timestamp for the authentication token
     */
    [[nodiscard]] virtual qevercloud::Timestamp authTokenExpirationTime() const = 0;

    /**
     * Timestamp at which authentication info was received from Evernote
     */
    [[nodiscard]] virtual qevercloud::Timestamp authenticationTime() const = 0;

    /**
     * Shard identifier which needs to be used for access to Evernote API
     * along with the authentication token
     */
    [[nodiscard]] virtual QString shardId() const = 0;

    /**
     * Url of the note store service for this user
     */
    [[nodiscard]] virtual QString noteStoreUrl() const = 0;

    /**
     * Url prefix for Evernote Web API.
     * @see qevercloud::PublicUserInfo::webApiUrlPrefix
     */
    [[nodiscard]] virtual QString webApiUrlPrefix() const = 0;

    /**
     * The list of network cookies received during OAuth procedure. Although
     * is is not mentioned anywhere in Evernote docs, these cookies might have
     * to be used for access to user store. See this discussion for reference:
     * https://discussion.evernote.com/forums/topic/124257-calls-to-userstore-from-evernote-api-stopped-working/#comment-562695
     */
    [[nodiscard]] virtual QList<QNetworkCookie> userStoreCookies() const = 0;
};

} // namespace quentier::synchronization
