/*
 * Copyright 2018-2025 Dmitry Ivanov
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

// TODO: remove after adaptation in Quentier
#include <quentier/utility/Factory.h>

#include <quentier/utility/Fwd.h>
#include <quentier/utility/Linkage.h>

#include <QFuture>

class QDebug;

namespace quentier::utility {

/**
 * @brief The IKeychainService interface provides the ability to interact with
 * the storage of sensitive data - read, write and delete it.
 */
class QUENTIER_EXPORT IKeychainService
{
public:
    virtual ~IKeychainService() noexcept;

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

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, ErrorCode errorCode);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & dbg, ErrorCode errorCode);

    /**
     * @brief The IKeychainService::Exception class is the base class for
     * exceptions returned inside QFutures from methods of IKeychainService
     */
    class QUENTIER_EXPORT Exception : public IQuentierException
    {
    public:
        explicit Exception(ErrorCode errorCode) noexcept;

        explicit Exception(
            ErrorCode errorCode, ErrorString errorDescription) noexcept;

        [[nodiscard]] ErrorCode errorCode() const noexcept;
        [[nodiscard]] QString exceptionDisplayName() const override;

        void raise() const override;
        [[nodiscard]] Exception * clone() const override;

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
        QString service, QString key) const = 0;

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
};

} // namespace quentier::utility
