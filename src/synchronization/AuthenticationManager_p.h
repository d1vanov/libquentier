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

#ifndef LIB_QUENTIER_SYNCHRONIZATION_AUTHENTICATION_MANAGER_PRIVATE_H
#define LIB_QUENTIER_SYNCHRONIZATION_AUTHENTICATION_MANAGER_PRIVATE_H

#include <quentier/synchronization/AuthenticationManager.h>

namespace quentier {

class AuthenticationManagerPrivate: public QObject
{
    Q_OBJECT
public:
    explicit AuthenticationManagerPrivate(const QString & consumerKey, const QString & consumerSecret,
                                          const QString & host, QObject * parent = Q_NULLPTR);

    bool isInProgress() const;

Q_SIGNALS:
    void sendAuthenticationTokenAndShardId(QString authToken, QString shardId, qevercloud::Timestamp expirationTime);
    void sendAuthenticationTokensForLinkedNotebooks(QHash<QString,QPair<QString,QString> > authenticationTokensAndShardIdsByLinkedNotebookGuids,
                                                    QHash<QString,qevercloud::Timestamp> authenticatonTokenExpirationTimesByLinkedNotebookGuids);
    void notifyError(ErrorString errorDescription);

public Q_SLOTS:
    void onRequestAuthenticationToken();
    void onRequestAuthenticationTokensForLinkedNotebooks(QVector<QPair<QString,QString> > linkedNotebookGuidsAndShareKeys);

private:
    Q_DISABLE_COPY(AuthenticationManagerPrivate)

private:
    QString         m_consumerKey;
    QString         m_consumerSecret;
    QString         m_host;
};

} // namespace quentier

#endif // LIB_QUENTIER_SYNCHRONIZATION_AUTHENTICATION_MANAGER_PRIVATE_H
