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

#ifndef LIB_QUENTIER_SYNCHRONIZATION_I_AUTHENTICATION_MANAGER_H
#define LIB_QUENTIER_SYNCHRONIZATION_I_AUTHENTICATION_MANAGER_H

#include <quentier/utility/Macros.h>
#include <quentier/types/ErrorString.h>
#include <QObject>
#include <QHash>
#include <QVector>

#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
#include <qt5qevercloud/QEverCloud.h>
#else
#include <qt4qevercloud/QEverCloud.h>
#endif

namespace quentier {

class IAuthenticationManager: public QObject
{
    Q_OBJECT
protected:
    explicit IAuthenticationManager(QObject * parent = Q_NULLPTR);

public:
    virtual ~IAuthenticationManager();

Q_SIGNALS:
    void sendAuthenticationResult(bool success, qevercloud::UserID userId, QString authToken,
                                  qevercloud::Timestamp authTokenExpirationTime, QString shardId,
                                  QString noteStoreUrl, QString webApiUrlPrefix, ErrorString errorDescription);

public Q_SLOTS:
    virtual void onAuthenticationRequest() = 0;
};

} // namespace quentier

#endif // LIB_QUENTIER_SYNCHRONIZATION_I_AUTHENTICATION_MANAGER_H
