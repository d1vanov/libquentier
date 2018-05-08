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

#ifndef LIB_QUENTIER_PRIVATE_SYNCHRONIZATION_I_USER_STORE_H
#define LIB_QUENTIER_PRIVATE_SYNCHRONIZATION_I_USER_STORE_H

#include <quentier/types/ErrorString.h>
#include <quentier/utility/Linkage.h>
#include <QSharedPointer>

#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
#include <qt5qevercloud/QEverCloud.h>
#else
#include <qt4qevercloud/QEverCloud.h>
#endif

namespace quentier {

QT_FORWARD_DECLARE_CLASS(User)

/**
 * @brief IUserStore is the interface for UserStore used by
 * SynchronizationManager: it provides signatures of methods
 * required for the implementation of Evernote EDAM sync protocol
 *
 * By default SynchronizationManager within libquentier uses its own
 * private implementation of IUSerStore interface but another implementation
 * can be injected at SynchronizationManager construction time. For one thing,
 * such injection is used for testing of libquentier's synchronization logic,
 * for other things, it can be used to implement custom synchronization
 * with some alternative backends.
 */
class QUENTIER_EXPORT IUserStore
{
protected:
    IUserStore(const QSharedPointer<qevercloud::UserStore> & pQecUserStore);

public:
    QSharedPointer<qevercloud::UserStore> getQecUserStore() const;
    void setQecUserStore(const QSharedPointer<qevercloud::UserStore> & pQecUserStore);

    QString authenticationToken() const;
    void setAuthenticationToken(const QString & authToken);

    /**
     * Factory method, create a new IUserStore subclass object
     */
    virtual IUserStore * create(const QString & host) const = 0;

    /**
     * Check the version of EDAM protocol
     *
     * @param clientName - application name + application version + platform name string
     * @param edamVersionMajor - the major version of EDAM protocol the application wants to use to connect to Evernote
     * @param edamVersionMinor - the minor version of EDAM protocol the application wants to use to connect to Evernote
     * @param errorDescription - the textual description of the error if the supplied protocol version cannot be used
     *                           to connect to Evernote
     * @return true if protocol check was successful i.e. the service can talk to the client using the supplied protocol version,
     *         false otherwise
     */
    virtual bool checkVersion(const QString & clientName, qint16 edamVersionMajor, qint16 edamVersionMinor,
                              ErrorString & errorDescription) = 0;

    /**
     * Retrieve full information about user (account)
     *
     * @param user - input and output parameter; on input needs to have user id set
     * @param errorDescription - the textual description of the error if full user information could not be retrieved
     * @param rateLimitSeconds - output parameter, the number of seconds the client needs to wait before attempting
     *                           to call this method or any other method calling Evernote API again; only meaningful
     *                           if returned value matches qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED
     * @return error code, 0 in case of successful retrieval of full user information, other values corresponding
     *         to qevercloud::EDAMErrorCode::type enumeration instead
     */
    virtual qint32 getUser(User & user, ErrorString & errorDescription, qint32 & rateLimitSeconds) = 0;

    /**
     * Retrieve account limits corresponding to certain provided service level
     *
     * @param serviceLevel - the level of Evernote service for which account limits are requested
     * @param limits - output account limits
     * @param errorDescription - the textual description of the error if account limits could not be retrieved
     * @param rateLimitSeconds - output parameter, the number of seconds the client needs to wait before attempting
     *                           to call this method or any other method calling Evernote API again; only meaningful
     *                           if returned value matches qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED
     * @return error code, 0 in case of successful retrieval of account limits for the given service level,
     *         other values corresponding to qevercoud::EDAMErrorCode::type enumeration instead
     */
    virtual qint32 getAccountLimits(const qevercloud::ServiceLevel::type serviceLevel, qevercloud::AccountLimits & limits,
                                    ErrorString & errorDescription, qint32 & rateLimitSeconds) = 0;

protected:
    QSharedPointer<qevercloud::UserStore> m_pQecUserStore;
};

} // namespace quentier

#endif // LIB_QUENTIER_PRIVATE_SYNCHRONIZATION_I_USER_STORE_H
