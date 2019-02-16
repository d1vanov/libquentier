/*
 * Copyright 2017-2019 Dmitry Ivanov
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

#include <quentier/synchronization/AuthenticationManager.h>
#include "AuthenticationManager_p.h"

namespace quentier {

AuthenticationManager::AuthenticationManager(const QString & consumerKey,
                                             const QString & consumerSecret,
                                             const QString & host,
                                             QObject * parent) :
    IAuthenticationManager(parent),
    d_ptr(new AuthenticationManagerPrivate(consumerKey, consumerSecret, host, this))
{
    QObject::connect(d_ptr,
                     QNSIGNAL(AuthenticationManagerPrivate,
                              sendAuthenticationResult,
                              bool,qevercloud::UserID,QString,qevercloud::Timestamp,
                              QString,QString,QString,ErrorString),
                     this,
                     QNSIGNAL(AuthenticationManager,
                              sendAuthenticationResult,bool,qevercloud::UserID,
                              QString,qevercloud::Timestamp,QString,QString,
                              QString,ErrorString));
}

AuthenticationManager::~AuthenticationManager()
{}

void AuthenticationManager::onAuthenticationRequest()
{
    Q_D(AuthenticationManager);
    d->onAuthenticationRequest();
}

} // namespace quentier
