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

#ifndef LIB_QUENTIER_UTILITY_I_KEYCHAIN_SERVICE_H
#define LIB_QUENTIER_UTILITY_I_KEYCHAIN_SERVICE_H

#include <quentier/types/ErrorString.h>
#include <quentier/utility/ForwardDeclarations.h>
#include <quentier/utility/Linkage.h>

#include <QObject>
#include <QUuid>

#include <memory>

QT_FORWARD_DECLARE_CLASS(QDebug)

namespace quentier {

/**
 * @brief The IKeychainService interface provides methods intended to start
 * potentially asynchronous interaction with the keychain and signals intended
 * to notify listeners about the completion of asynchronous interactions.
 */
class QUENTIER_EXPORT IKeychainService : public QObject
{
    Q_OBJECT
protected:
    explicit IKeychainService(QObject * parent = nullptr);

public:
    virtual ~IKeychainService() {}

    /**
     * Error codes for results of operations with the keychain service
     */
    enum class ErrorCode
    {
        /**
         * No error occurred, operation was successful
         */
        NoError,
        /**
         * For the given key no data was found
         */
        EntryNotFound,
        /**
         * Could not delete existing secret data
         */
        CouldNotDeleteEntry,
        /**
         * User denied access to keychain
         */
        AccessDeniedByUser,
        /**
         * Access denied for some reason
         */
        AccessDenied,
        /**
         * No platform-specific keychain service available
         */
        NoBackendAvailable,
        /**
         * Not implemented on platform
         */
        NotImplemented,
        /**
         * Something else went wrong, the error description specifies what
         */
        OtherError
    };

    friend QTextStream & operator<<(
        QTextStream & strm, const ErrorCode errorCode);

    friend QDebug & operator<<(QDebug & dbg, const ErrorCode errorCode);

public:
    /**
     * startWritePasswordJob slot should start the potentially asynchronous
     * process of storing the password in the keychain. When ready, this slot
     * is expected to emit writePasswordJobFinished signal.
     *
     * @param service                   Name of service within the keychain
     * @param key                       Key to store the password under
     * @param password                  Password to store in the keychain
     *
     * @return                          Unique identifier assigned to this
     *                                  write password request
     */
    virtual QUuid startWritePasswordJob(
        const QString & service, const QString & key,
        const QString & password) = 0;

    /**
     * startReadPasswordJob slot should start the potentially asynchronous
     * process of reading the password from the keychain. When ready, this slot
     * is expected to emit readPasswordJobFinished signal.
     *
     * @param service                   Name of service within the keychain
     * @param key                       Key under which the password is stored
     *
     * @return                          Unique identifier assigned to this
     *                                  read password request
     */
    virtual QUuid startReadPasswordJob(
        const QString & service, const QString & key) = 0;

    /**
     * startDeletePasswordJob slot should start the potentially asynchronous
     * process of deleting the password from the keychain. When ready, this slot
     * is expected to emit deletePasswordJobFinished signal.
     *
     * @param service                   Name of service within the keychain
     * @param key                       Key under which the password is stored
     *
     * @return                          Unique identifier assigned to this
     *                                  delete password request
     */
    virtual QUuid startDeletePasswordJob(
        const QString & service, const QString & key) = 0;

Q_SIGNALS:
    /**
     * writePasswordJobFinished signal should be emitted in response to
     * the call of startWritePasswordJob method
     *
     * @param requestId                 Request id returned from
     *                                  startWritePasswordJob method
     * @param errorCode                 Error code determining whether
     *                                  the operation was successful or some
     *                                  error has occurred
     * @param errorDescription          Textual description of error in case
     *                                  of unsuccessful execution
     */
    void writePasswordJobFinished(
        QUuid requestId, ErrorCode errorCode, ErrorString errorDescription);

    /**
     * readPasswordJobFinished signal should be emitted in response to
     * the call of startReadPasswordJob method
     *
     * @param requestId                 Request id returned from
     *                                  startReadPasswordJob method
     * @param errorCode                 Error code determining whether
     *                                  the operation was successful or some
     *                                  error has occurred
     * @param errorDescription          Textual description of error in case
     *                                  of unsuccessful execution
     * @param password                  Password read from the keychain
     */
    void readPasswordJobFinished(
        QUuid requestId, ErrorCode errorCode, ErrorString errorDescription,
        QString password);

    /**
     * deletePasswordJobFinished signal should be emitted in response to
     * the call of startDeletePasswordJob method
     *
     * @param requestId                 Request id returned from
     *                                  startDeletePasswordJob method
     * @param errorCode                 Error code determining whether
     *                                  the operation was successful or some
     *                                  error has occurred
     * @param errorDescription          Textual description of error in case
     *                                  of unsuccessful execution
     */
    void deletePasswordJobFinished(
        QUuid requestId, ErrorCode errorCode, ErrorString errorDescription);

private:
    Q_DISABLE_COPY(IKeychainService);
};

QUENTIER_EXPORT IKeychainServicePtr
newQtKeychainService(QObject * parent = nullptr);

QUENTIER_EXPORT IKeychainServicePtr
newObfuscatingKeychainService(QObject * parent = nullptr);

QUENTIER_EXPORT IKeychainServicePtr newCompositeKeychainService(
    QString name, IKeychainServicePtr primaryKeychain,
    IKeychainServicePtr secondaryKeychain, QObject * parent = nullptr);

QUENTIER_EXPORT IKeychainServicePtr newMigratingKeychainService(
    IKeychainServicePtr sourceKeychain, IKeychainServicePtr sinkKeychain,
    QObject * parent = nullptr);

} // namespace quentier

#endif // LIB_QUENTIER_UTILITY_I_KEYCHAIN_SERVICE_H
