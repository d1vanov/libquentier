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

#ifndef LIB_QUENTIER_SYNCHRONIZATION_AUTHENTICATION_MANAGER_PRIVATE_H
#define LIB_QUENTIER_SYNCHRONIZATION_AUTHENTICATION_MANAGER_PRIVATE_H

#include <quentier/synchronization/AuthenticationManager.h>

namespace quentier {

class Q_DECL_HIDDEN AuthenticationManagerPrivate final : public QObject
{
    Q_OBJECT
public:
    explicit AuthenticationManagerPrivate(
        const QString & consumerKey, const QString & consumerSecret,
        const QString & host, QObject * parent = nullptr);

Q_SIGNALS:
    void sendAuthenticationResult(
        bool success, qevercloud::UserID userId, QString authToken,
        qevercloud::Timestamp authTokenExpirationTime, QString shardId,
        QString noteStoreUrl, QString webApiUrlPrefix,
        QList<QNetworkCookie> cookies, ErrorString errorDescription);

public Q_SLOTS:
    void onAuthenticationRequest();

private:
    Q_DISABLE_COPY(AuthenticationManagerPrivate)

private:
    QString m_consumerKey;
    QString m_consumerSecret;
    QString m_host;
};

} // namespace quentier

#endif // LIB_QUENTIER_SYNCHRONIZATION_AUTHENTICATION_MANAGER_PRIVATE_H
