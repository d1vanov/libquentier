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

#ifndef LIB_QUENTIER_TESTS_SYNCHRONIZATION_FAKE_AUTHENTICATION_MANAGER_H
#define LIB_QUENTIER_TESTS_SYNCHRONIZATION_FAKE_AUTHENTICATION_MANAGER_H

#include <quentier/synchronization/IAuthenticationManager.h>

namespace quentier {

class FakeAuthenticationManager: public IAuthenticationManager
{
    Q_OBJECT
public:
    FakeAuthenticationManager(QObject * parent = Q_NULLPTR);
    virtual ~FakeAuthenticationManager();

    qevercloud::UserID userId() const;
    void setUserId(const qevercloud::UserID userId);

    void failNextRequest();

public Q_SLOTS:
    virtual void onAuthenticationRequest() Q_DECL_OVERRIDE;

private:
    qevercloud::UserID  m_userId;
    bool                m_failNextRequest;
};

} // namespace quentier

#endif // LIB_QUENTIER_TESTS_SYNCHRONIZATION_FAKE_AUTHENTICATION_MANAGER_H
