/*
 * Copyright 2017-2020 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_SYNCHRONIZATION_AUTHENTICATION_MANAGER_H
#define LIB_QUENTIER_SYNCHRONIZATION_AUTHENTICATION_MANAGER_H

#include <quentier/synchronization/IAuthenticationManager.h>

namespace quentier {

QT_FORWARD_DECLARE_CLASS(AuthenticationManagerPrivate)

/**
 * @brief The AuthenticationManager class is libquentier's default
 * implementation of IAuthenticationManager interface; internally uses
 * QEverCloud's OAuth widget
 */
class QUENTIER_EXPORT AuthenticationManager : public IAuthenticationManager
{
    Q_OBJECT
public:
    explicit AuthenticationManager(
        const QString & consumerKey, const QString & consumerSecret,
        const QString & host, QObject * parent = nullptr);

    virtual ~AuthenticationManager();

public Q_SLOTS:
    virtual void onAuthenticationRequest() override;

private:
    AuthenticationManager() = delete;
    Q_DISABLE_COPY(AuthenticationManager)

private:
    AuthenticationManagerPrivate * const d_ptr;
    Q_DECLARE_PRIVATE(AuthenticationManager)
};

} // namespace quentier

#endif // LIB_QUENTIER_SYNCHRONIZATION_AUTHENTICATION_MANAGER_H
