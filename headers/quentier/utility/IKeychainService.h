/*
 * Copyright 2018-2022 Dmitry Ivanov
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

#pragma once

#include <quentier/exception/IQuentierException.h>
#include <quentier/types/ErrorString.h>
#include <quentier/utility/Fwd.h>
#include <quentier/utility/Linkage.h>

#include <QFuture>
#include <QObject>
#include <QUuid>

#include <memory>

class QDebug;

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
    ~IKeychainService() noexcept override = default;

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
        QTextStream & strm, ErrorCode errorCode);

    friend QDebug & operator<<(QDebug & dbg, ErrorCode errorCode);

    class QUENTIER_EXPORT Exception : public IQuentierException
    {
    public:
        explicit Exception(ErrorCode errorCode) noexcept;

        explicit Exception(
            ErrorCode errorCode, ErrorString errorDescription) noexcept;

        [[nodiscard]] ErrorCode errorCode() const noexcept;
        [[nodiscard]] QString exceptionDisplayName() const override;

    private:
        const ErrorCode m_errorCode;
    };

public:
    /**
     * writePassword method potentially asynchronously writes password to the
     * keychain.
     *
     * @param service                   Name of service within the keychain
     * @param key                       Key to store the password under
     * @param password                  Password to store in the keychain
     *
     * @return                          Future which becomes finished when the
     *                                  operation is comlete. If the operation
     *                                  fails, the future would contain an
     *                                  exception.
     */
    [[nodiscard]] virtual QFuture<void> writePassword(
        QString service, QString key, QString password) = 0;

    /**
     * readPassword method potentially asynchronously reads password from the
     * keychain.
     *
     * @param service                   Name of service within the keychain
     * @param key                       Key under which the password is stored
     *
     * @return                          Future which becomes finished when the
     *                                  operation is complete. The value inside
     *                                  the future would be the read password.
     *                                  If the operation fails, the future
     *                                  would contain an exception.
     */
    [[nodiscard]] virtual QFuture<QString> readPassword(
        QString service, QString key) = 0;

    /**
     * deletePassword potentially asynchronously deletes password from the
     * keychain.
     *
     * @param service                   Name of service within the keychain
     * @param key                       Key under which the password is stored
     *
     * @return                          Future which becomes finished when the
     *                                  operation is comlete. If the operation
     *                                  fails, the future would contain an
     *                                  exception.
     */
    [[nodiscard]] virtual QFuture<void> deletePassword(
        QString service, QString key) = 0;

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
    [[nodiscard]] virtual QUuid startWritePasswordJob(
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
    [[nodiscard]] virtual QUuid startReadPasswordJob(
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
    [[nodiscard]] virtual QUuid startDeletePasswordJob(
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

[[nodiscard]] QUENTIER_EXPORT IKeychainServicePtr
newQtKeychainService(QObject * parent = nullptr);

[[nodiscard]] QUENTIER_EXPORT IKeychainServicePtr
newObfuscatingKeychainService(QObject * parent = nullptr);

[[nodiscard]] QUENTIER_EXPORT IKeychainServicePtr newCompositeKeychainService(
    QString name, IKeychainServicePtr primaryKeychain,
    IKeychainServicePtr secondaryKeychain, QObject * parent = nullptr);

[[nodiscard]] QUENTIER_EXPORT IKeychainServicePtr newMigratingKeychainService(
    IKeychainServicePtr sourceKeychain, IKeychainServicePtr sinkKeychain,
    QObject * parent = nullptr);

} // namespace quentier
