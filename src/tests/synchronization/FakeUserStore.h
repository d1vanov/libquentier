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

#ifndef LIB_QUENTIER_TESTS_SYNCHRONIZATION_FAKE_USER_STORE_H
#define LIB_QUENTIER_TESTS_SYNCHRONIZATION_FAKE_USER_STORE_H

#include <quentier/synchronization/IUserStore.h>

#include <quentier/types/User.h>

#include <QHash>

namespace quentier {

class FakeUserStore final : public IUserStore
{
public:
    qint16 edamVersionMajor() const;
    void setEdamVersionMajor(const qint16 edamVersionMajor);

    qint16 edamVersionMinor() const;
    void setEdamVersionMinor(const qint16 edamVersionMinor);

    const qevercloud::AccountLimits * findAccountLimits(
        const qevercloud::ServiceLevel serviceLevel) const;

    void setAccountLimits(
        const qevercloud::ServiceLevel serviceLevel,
        const qevercloud::AccountLimits & limits);

    const User * findUser(const qint32 id) const;
    void setUser(const qint32 id, const User & user);

public:
    // IUserStore interface

    virtual void setAuthData(
        QString authenticationToken, QList<QNetworkCookie> cookies) override;

    virtual bool checkVersion(
        const QString & clientName, qint16 edamVersionMajor,
        qint16 edamVersionMinor, ErrorString & errorDescription) override;

    virtual qint32 getUser(
        User & user, ErrorString & errorDescription,
        qint32 & rateLimitSeconds) override;

    virtual qint32 getAccountLimits(
        const qevercloud::ServiceLevel serviceLevel,
        qevercloud::AccountLimits & limits, ErrorString & errorDescription,
        qint32 & rateLimitSeconds) override;

private:
    QString m_authenticationToken;
    QList<QNetworkCookie> m_cookies;

    qint16 m_edamVersionMajor = 0;
    qint16 m_edamVersionMinor = 0;

    QHash<qevercloud::ServiceLevel, qevercloud::AccountLimits> m_accountLimits;
    QHash<qint32, User> m_users;
};

using FakeUserStorePtr = std::shared_ptr<FakeUserStore>;

} // namespace quentier

#endif // LIB_QUENTIER_TESTS_SYNCHRONIZATION_FAKE_USER_STORE_H
