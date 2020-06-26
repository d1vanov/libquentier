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

#ifndef LIB_QUENTIER_PRIVATE_UTILITY_I_KEYCHAIN_SERVICE_H
#define LIB_QUENTIER_PRIVATE_UTILITY_I_KEYCHAIN_SERVICE_H

#include <quentier/utility/Linkage.h>
#include <quentier/utility/Macros.h>
#include <quentier/types/ErrorString.h>

#include <QObject>
#include <QUuid>

QT_FORWARD_DECLARE_CLASS(QDebug)

namespace quentier {

class QUENTIER_EXPORT IKeychainService: public QObject
{
    Q_OBJECT
protected:
    explicit IKeychainService(QObject * parent = nullptr) : QObject(parent) {}

public:
    virtual ~IKeychainService() {}

    /**
     * Enum determining the error codes from keychain service
     */
    enum class ErrorCode
    {
        /**
         * No error occurred, operation was successful
         */
        NoError = 0,
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
         * Access denied for other reasons
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
         * Something else went wrong, the textual error description
         */
        OtherError
    };

    friend QTextStream & operator<<(
        QTextStream & strm, const ErrorCode errorCode);

    friend QDebug & operator<<(QDebug & dbg, const ErrorCode errorCode);

public:
    virtual QUuid startWritePasswordJob(
        const QString & service, const QString & key,
        const QString & password) = 0;

    virtual QUuid startReadPasswordJob(
        const QString & service, const QString & key) = 0;

    virtual QUuid startDeletePasswordJob(
        const QString & service, const QString & key) = 0;

Q_SIGNALS:
    void writePasswordJobFinished(
        QUuid requestId, ErrorCode errorCode, ErrorString errorDescription);

    void readPasswordJobFinished(
        QUuid requestId, ErrorCode errorCode, ErrorString errorDescription,
        QString password);

    void deletePasswordJobFinished(
        QUuid requestId, ErrorCode errorCode, ErrorString errorDescription);

private:
    Q_DISABLE_COPY(IKeychainService);
};

} // namespace quentier

#endif // LIB_QUENTIER_PRIVATE_UTILITY_I_KEYCHAIN_SERVICE_H
