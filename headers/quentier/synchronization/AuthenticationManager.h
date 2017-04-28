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

#ifndef LIB_QUENTIER_SYNCHRONIZATION_AUTHENTICATION_MANAGER_H
#define LIB_QUENTIER_SYNCHRONIZATION_AUTHENTICATION_MANAGER_H

#include <quentier/synchronization/IAuthenticationManager.h>
#include <quentier/types/Account.h>

namespace quentier {

QT_FORWARD_DECLARE_CLASS(AuthenticationManagerPrivate)

class AuthenticationManager: public IAuthenticationManager
{
    Q_OBJECT
public:
    explicit AuthenticationManager(const QString & consumerKey, const QString & consumerSecret,
                                   const QString & host, const Account & account, QObject * parent = Q_NULLPTR);
    virtual ~AuthenticationManager();

public:
    virtual bool isInProgress() const Q_DECL_OVERRIDE;

public Q_SLOTS:
    virtual void onRequestAuthenticationToken() Q_DECL_OVERRIDE;
    virtual void onRequestAuthenticationTokensForLinkedNotebooks(QVector<QPair<QString,QString> > linkedNotebookGuidsAndShareKeys) Q_DECL_OVERRIDE;
    virtual void onRequestAuthenticationRevoke(qevercloud::UserID userId) Q_DECL_OVERRIDE;

private:
    AuthenticationManager() Q_DECL_EQ_DELETE;
    Q_DISABLE_COPY(AuthenticationManager)

private:
    AuthenticationManagerPrivate * const    d_ptr;
    Q_DECLARE_PRIVATE(AuthenticationManager)
};

} // namespace quentier

#endif // LIB_QUENTIER_SYNCHRONIZATION_AUTHENTICATION_MANAGER_H
