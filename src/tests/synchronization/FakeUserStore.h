/*
 * Copyright 2018 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_TESTS_FAKE_USER_STORE_H
#define LIB_QUENTIER_TESTS_FAKE_USER_STORE_H

#include <quentier_private/synchronization/IUserStore.h>
#include <quentier/types/User.h>
#include <quentier/utility/Macros.h>
#include <QHash>

namespace quentier {

class FakeUserStore: public IUserStore
{
public:
    FakeUserStore();

    qint16 edamVersionMajor() const;
    void setEdamVersionMajor(const qint16 edamVersionMajor);

    qint16 edamVersionMinor() const;
    void setEdamVersionMinor(const qint16 edamVersionMinor);

    const qevercloud::AccountLimits * findAccountLimits(const qevercloud::ServiceLevel::type serviceLevel) const;
    void setAccountLimits(const qevercloud::ServiceLevel::type serviceLevel,
                          const qevercloud::AccountLimits & limits);

    const User * findUser(const qint32 id) const;
    void setUser(const qint32 id, const User & user);

    void triggerRateLimitReachOnNextCall();

public:
    // IUserStore interface
    virtual IUserStore * create(const QString & host) const Q_DECL_OVERRIDE;
    virtual bool checkVersion(const QString & clientName, qint16 edamVersionMajor, qint16 edamVersionMinor,
                              ErrorString & errorDescription) Q_DECL_OVERRIDE;
    virtual qint32 getUser(User & user, ErrorString & errorDescription, qint32 & rateLimitSeconds) Q_DECL_OVERRIDE;
    virtual qint32 getAccountLimits(const qevercloud::ServiceLevel::type serviceLevel, qevercloud::AccountLimits & limits,
                                    ErrorString & errorDescription, qint32 & rateLimitSeconds) Q_DECL_OVERRIDE;

private:
    qint16      m_edamVersionMajor;
    qint16      m_edamVersionMinor;

    typedef QHash<qevercloud::ServiceLevel::type, qevercloud::AccountLimits> AccountLimitsByServiceLevel;
    AccountLimitsByServiceLevel     m_accountLimits;

    typedef QHash<qint32, User> UsersById;
    UsersById   m_users;

    bool        m_shouldTriggerRateLimitReachOnNextCall;
};

} // namespace quentier

#endif // LIB_QUENTIER_TESTS_FAKE_USER_STORE_H
