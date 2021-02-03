/*
 * Copyright 2018-2021 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_TESTS_SYNCHRONIZATION_FAKE_USER_STORE_H
#define LIB_QUENTIER_TESTS_SYNCHRONIZATION_FAKE_USER_STORE_H

#include <quentier/synchronization/IUserStore.h>

#include <qevercloud/generated/types/User.h>

#include <QHash>

namespace quentier {

class FakeUserStore final : public IUserStore
{
public:
    ~FakeUserStore() override;

    [[nodiscard]] qint16 edamVersionMajor() const noexcept;
    void setEdamVersionMajor(const qint16 edamVersionMajor);

    [[nodiscard]] qint16 edamVersionMinor() const noexcept;
    void setEdamVersionMinor(const qint16 edamVersionMinor);

    [[nodiscard]] const qevercloud::AccountLimits * findAccountLimits(
        const qevercloud::ServiceLevel serviceLevel) const noexcept;

    void setAccountLimits(
        const qevercloud::ServiceLevel serviceLevel,
        const qevercloud::AccountLimits & limits);

    [[nodiscard]] const qevercloud::User * findUser(
        const qint32 id) const noexcept;

    void setUser(const qint32 id, const qevercloud::User & user);

public:
    // IUserStore interface

    void setAuthData(
        QString authenticationToken, QList<QNetworkCookie> cookies) override;

    [[nodiscard]] bool checkVersion(
        const QString & clientName, qint16 edamVersionMajor,
        qint16 edamVersionMinor, ErrorString & errorDescription) override;

    [[nodiscard]] qint32 getUser(
        qevercloud::User & user, ErrorString & errorDescription,
        qint32 & rateLimitSeconds) override;

    [[nodiscard]] qint32 getAccountLimits(
        const qevercloud::ServiceLevel serviceLevel,
        qevercloud::AccountLimits & limits, ErrorString & errorDescription,
        qint32 & rateLimitSeconds) override;

private:
    QString m_authenticationToken;
    QList<QNetworkCookie> m_cookies;

    qint16 m_edamVersionMajor = 0;
    qint16 m_edamVersionMinor = 0;

    QHash<qevercloud::ServiceLevel, qevercloud::AccountLimits> m_accountLimits;
    QHash<qint32, qevercloud::User> m_users;
};

using FakeUserStorePtr = std::shared_ptr<FakeUserStore>;

} // namespace quentier

#endif // LIB_QUENTIER_TESTS_SYNCHRONIZATION_FAKE_USER_STORE_H
