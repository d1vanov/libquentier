/*
 * Copyright 2018-2020 Dmitry Ivanov
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

#include "FakeUserStore.h"

namespace quentier {

qint16 FakeUserStore::edamVersionMajor() const
{
    return m_edamVersionMajor;
}

void FakeUserStore::setEdamVersionMajor(const qint16 edamVersionMajor)
{
    m_edamVersionMajor = edamVersionMajor;
}

qint16 FakeUserStore::edamVersionMinor() const
{
    return m_edamVersionMinor;
}

void FakeUserStore::setEdamVersionMinor(const qint16 edamVersionMinor)
{
    m_edamVersionMinor = edamVersionMinor;
}

const qevercloud::AccountLimits * FakeUserStore::findAccountLimits(
    const qevercloud::ServiceLevel serviceLevel) const
{
    auto it = m_accountLimits.find(serviceLevel);
    if (it != m_accountLimits.end()) {
        return &(it.value());
    }

    return nullptr;
}

void FakeUserStore::setAccountLimits(
    const qevercloud::ServiceLevel serviceLevel,
    const qevercloud::AccountLimits & limits)
{
    m_accountLimits[serviceLevel] = limits;
}

const User * FakeUserStore::findUser(const qint32 id) const
{
    auto it = m_users.find(id);
    if (it != m_users.end()) {
        return &(it.value());
    }

    return nullptr;
}

void FakeUserStore::setUser(const qint32 id, const User & user)
{
    m_users[id] = user;
}

void FakeUserStore::setAuthData(
    QString authenticationToken, QList<QNetworkCookie> cookies)
{
    m_authenticationToken = std::move(authenticationToken);
    m_cookies = std::move(cookies);
}

bool FakeUserStore::checkVersion(
    const QString & clientName, qint16 edamVersionMajor,
    qint16 edamVersionMinor, ErrorString & errorDescription)
{
    Q_UNUSED(clientName);

    if (m_edamVersionMajor != edamVersionMajor) {
        errorDescription.setBase(QStringLiteral("EDAM major version mismatch"));
        return false;
    }

    if (m_edamVersionMinor != edamVersionMinor) {
        errorDescription.setBase(QStringLiteral("EDAM minor version mismatch"));
        return false;
    }

    return true;
}

qint32 FakeUserStore::getUser(
    User & user, ErrorString & errorDescription, qint32 & rateLimitSeconds)
{
    Q_UNUSED(rateLimitSeconds)

    if (!user.hasId()) {
        errorDescription.setBase(QStringLiteral("User has no id"));
        return static_cast<qint32>(qevercloud::EDAMErrorCode::DATA_REQUIRED);
    }

    auto it = m_users.find(user.id());
    if (it == m_users.end()) {
        errorDescription.setBase(QStringLiteral("User data was not found"));
        return static_cast<qint32>(qevercloud::EDAMErrorCode::DATA_REQUIRED);
    }

    user = it.value();
    return 0;
}

qint32 FakeUserStore::getAccountLimits(
    const qevercloud::ServiceLevel serviceLevel,
    qevercloud::AccountLimits & limits, ErrorString & errorDescription,
    qint32 & rateLimitSeconds)
{
    Q_UNUSED(rateLimitSeconds)

    auto it = m_accountLimits.find(serviceLevel);
    if (it == m_accountLimits.end()) {
        errorDescription.setBase(
            QStringLiteral("Account limits were not found"));

        return static_cast<qint32>(qevercloud::EDAMErrorCode::DATA_REQUIRED);
    }

    limits = it.value();
    return 0;
}

} // namespace quentier
