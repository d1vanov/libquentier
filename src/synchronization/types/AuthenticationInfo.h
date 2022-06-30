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

#include <quentier/synchronization/types/IAuthenticationInfo.h>

namespace quentier::synchronization {

struct AuthenticationInfo final : public IAuthenticationInfo
{
    QTextStream & print(QTextStream & strm) const override;

    [[nodiscard]] qevercloud::UserID userId() const noexcept override;
    [[nodiscard]] QString authToken() const override;
    [[nodiscard]] qevercloud::Timestamp authTokenExpirationTime()
        const noexcept override;

    [[nodiscard]] QString shardId() const override;
    [[nodiscard]] QString noteStoreUrl() const override;
    [[nodiscard]] QString webApiUrlPrefix() const override;
    [[nodiscard]] QList<QNetworkCookie> userStoreCookies() const override;

    qevercloud::UserID m_userId;
    QString m_authToken;
    qevercloud::Timestamp m_authTokenExpirationTime;
    QString m_shardId;
    QString m_noteStoreUrl;
    QString m_webApiUrlPrefix;
    QList<QNetworkCookie> m_userStoreCookies;
};

[[nodiscard]] bool operator==(
    const AuthenticationInfo & lhs, const AuthenticationInfo & rhs) noexcept;

[[nodiscard]] bool operator!=(
    const AuthenticationInfo & lhs, const AuthenticationInfo & rhs) noexcept;

} // namespace quentier::synchronization
