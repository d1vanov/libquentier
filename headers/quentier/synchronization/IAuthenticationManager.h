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

#ifndef LIB_QUENTIER_SYNCHRONIZATION_I_AUTHENTICATION_MANAGER_H
#define LIB_QUENTIER_SYNCHRONIZATION_I_AUTHENTICATION_MANAGER_H

#include <quentier/synchronization/ForwardDeclarations.h>
#include <quentier/types/ErrorString.h>
#include <quentier/utility/Linkage.h>

#include <qt5qevercloud/QEverCloud.h>

#include <QHash>
#include <QList>
#include <QNetworkCookie>
#include <QObject>
#include <QVector>

namespace quentier {

class QUENTIER_EXPORT IAuthenticationManager : public QObject
{
    Q_OBJECT
protected:
    explicit IAuthenticationManager(QObject * parent = nullptr);

public:
    virtual ~IAuthenticationManager();

Q_SIGNALS:
    void sendAuthenticationResult(
        bool success, qevercloud::UserID userId, QString authToken,
        qevercloud::Timestamp authTokenExpirationTime, QString shardId,
        QString noteStoreUrl, QString webApiUrlPrefix,
        QList<QNetworkCookie> userStoreCookies, ErrorString errorDescription);

public Q_SLOTS:
    virtual void onAuthenticationRequest() = 0;
};

} // namespace quentier

#endif // LIB_QUENTIER_SYNCHRONIZATION_I_AUTHENTICATION_MANAGER_H
