/*
 * Copyright 2017 Dmitry Ivanov
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

AuthenticationManager::AuthenticationManager(const QString & consumerKey, const QString & consumerSecret,
                                             const QString & host, const Account & account, QObject * parent) :
    IAuthenticationManager(parent),
    d_ptr(new AuthenticationManagerPrivate(consumerKey, consumerSecret, host, account, this))
{
    QObject::connect(d_ptr, QNSIGNAL(AuthenticationManagerPrivate,sendAuthenticationTokenAndShardId,QString,QString,qevercloud::Timestamp),
                     this, QNSIGNAL(AuthenticationManager,sendAuthenticationTokenAndShardId,QString,QString,qevercloud::Timestamp));
    QObject::connect(d_ptr, QNSIGNAL(AuthenticationManagerPrivate,sendAuthenticationTokensForLinkedNotebooks,QHash<QString,QPair<QString,QString> >,QHash<QString,qevercloud::Timestamp>),
                     this, QNSIGNAL(AuthenticationManager,sendAuthenticationTokensForLinkedNotebooks,QHash<QString,QPair<QString,QString> >,QHash<QString,qevercloud::Timestamp>));
    QObject::connect(d_ptr, QNSIGNAL(AuthenticationManagerPrivate,authenticationRevokeReply,bool,ErrorString,qevercloud::UserID),
                     this, QNSIGNAL(AuthenticationManager,authenticationRevokeReply,bool,ErrorString,qevercloud::UserID));
    QObject::connect(d_ptr, QNSIGNAL(AuthenticationManagerPrivate,notifyError,ErrorString),
                     this, QNSIGNAL(AuthenticationManager,notifyError,ErrorString));
}

AuthenticationManager::~AuthenticationManager()
{}

bool AuthenticationManager::isInProgress() const
{
    Q_D(const AuthenticationManager);
    return d->isInProgress();
}

void AuthenticationManager::onRequestAuthenticationToken()
{
    Q_D(AuthenticationManager);
    d->onRequestAuthenticationToken();
}

void AuthenticationManager::onRequestAuthenticationTokensForLinkedNotebooks(QVector<QPair<QString,QString> > linkedNotebookGuidsAndShareKeys)
{
    Q_D(AuthenticationManager);
    d->onRequestAuthenticationTokensForLinkedNotebooks(linkedNotebookGuidsAndShareKeys);
}

void AuthenticationManager::onRequestAuthenticationRevoke(qevercloud::UserID userId)
{
    Q_D(AuthenticationManager);
    d->onRequestAuthenticationRevoke(userId);
}

} // namespace quentier
