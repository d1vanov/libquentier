/*
 * Copyright 2018-2020 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_SYNCHRONIZATION_I_USER_STORE_H
#define LIB_QUENTIER_SYNCHRONIZATION_I_USER_STORE_H

#include <quentier/synchronization/ForwardDeclarations.h>
#include <quentier/types/ErrorString.h>
#include <quentier/utility/Linkage.h>

#include <QList>
#include <QNetworkCookie>

#include <qt5qevercloud/QEverCloud.h>

#include <memory>

namespace quentier {

QT_FORWARD_DECLARE_CLASS(User)

/**
 * @brief IUserStore is the interface which provides methods
 * required for the implementation of UserStore part of Evernote EDAM sync
 * protocol
 */
class QUENTIER_EXPORT IUserStore
{
public:
    virtual ~IUserStore() = default;

    /**
     * Set authentication data to be used by this IUserStore instance
     */
    virtual void setAuthData(
        QString authenticationToken, QList<QNetworkCookie> cookies) = 0;

    /**
     * Check the version of EDAM protocol
     *
     * @param clientName        Application name + application version +
     *                          platform name string
     * @param edamVersionMajor  The major version of EDAM protocol
     *                          the application wants to use to connect to
     *                          Evernote
     * @param edamVersionMinor  The minor version of EDAM protocol
     *                          the application wants to use to connect to
     *                          Evernote
     * @param errorDescription  The textual description of the error if
     *                          the supplied protocol version cannot be used to
     *                          connect to Evernote
     * @return                  True if protocol check was successful i.e.
     *                          the service can talk to the client using
     *                          the supplied protocol version, false otherwise
     */
    virtual bool checkVersion(
        const QString & clientName, qint16 edamVersionMajor,
        qint16 edamVersionMinor, ErrorString & errorDescription) = 0;

    /**
     * Retrieve full information about user (account)
     *
     * @param user              Input and output parameter; on input needs
     *                          to have user id set
     * @param errorDescription  The textual description of the error if full
     *                          user information could not be retrieved
     * @param rateLimitSeconds  Output parameter, the number of seconds
     *                          the client needs to wait before attempting
     *                          to call this method or any other method calling
     *                          Evernote API again; only meaningful if returned
     *                          value matches
     *                          qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED
     * @return                  Error code, 0 in case of successful retrieval
     *                          of full user information, other values
     *                          corresponding to qevercloud::EDAMErrorCode
     *                          enumeration instead
     */
    virtual qint32 getUser(
        User & user, ErrorString & errorDescription,
        qint32 & rateLimitSeconds) = 0;

    /**
     * Retrieve account limits corresponding to certain provided service level
     *
     * @param serviceLevel      The level of Evernote service for which account
     *                          limits are requested
     * @param limits            Output account limits
     * @param errorDescription  The textual description of the error if account
     *                          limits could not be retrieved
     * @param rateLimitSeconds  Output parameter, the number of seconds
     *                          the client needs to wait before attempting
     *                          to call this method or any other method calling
     *                          Evernote API again; only meaningful if returned
     *                          value matches
     *                          qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED
     * @return                  Error code, 0 in case of successful retrieval
     *                          of account limits for the given service level,
     *                          other values corresponding to
     *                          qevercoud::EDAMErrorCode::type enumeration
     *                          instead
     */
    virtual qint32 getAccountLimits(
        const qevercloud::ServiceLevel serviceLevel,
        qevercloud::AccountLimits & limits, ErrorString & errorDescription,
        qint32 & rateLimitSeconds) = 0;
};

QUENTIER_EXPORT IUserStorePtr newUserStore(QString evernoteHost);

} // namespace quentier

#endif // LIB_QUENTIER_SYNCHRONIZATION_I_USER_STORE_H
