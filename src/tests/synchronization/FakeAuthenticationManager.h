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

#ifndef LIB_QUENTIER_TESTS_SYNCHRONIZATION_FAKE_AUTHENTICATION_MANAGER_H
#define LIB_QUENTIER_TESTS_SYNCHRONIZATION_FAKE_AUTHENTICATION_MANAGER_H

#include <quentier/synchronization/IAuthenticationManager.h>

namespace quentier {

class FakeAuthenticationManager final: public IAuthenticationManager
{
    Q_OBJECT
public:
    FakeAuthenticationManager(QObject * parent = nullptr);
    ~FakeAuthenticationManager() override;

    [[nodiscard]] const QString & authToken() const noexcept;
    void setAuthToken(QString authToken);

    [[nodiscard]] qevercloud::UserID userId() const noexcept;
    void setUserId(const qevercloud::UserID userId);

    [[nodiscard]] QList<QNetworkCookie> userStoreCookies() const;
    void setUserStoreCookies(QList<QNetworkCookie> cookies);

    void failNextRequest();

public Q_SLOTS:
    virtual void onAuthenticationRequest() override;

private:
    qevercloud::UserID m_userId = 1;
    QString m_authToken;
    bool m_failNextRequest = false;

    QList<QNetworkCookie> m_userStoreCookies;
};

using FakeAuthenticationManagerPtr = std::shared_ptr<FakeAuthenticationManager>;

} // namespace quentier

#endif // LIB_QUENTIER_TESTS_SYNCHRONIZATION_FAKE_AUTHENTICATION_MANAGER_H
