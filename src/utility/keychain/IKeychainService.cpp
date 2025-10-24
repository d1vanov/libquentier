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

#include <quentier/utility/Factory.h>
#include <quentier/utility/IKeychainService.h>

#include "CompositeKeychainService.h"
#include "MigratingKeychainService.h"
#include "ObfuscatingKeychainService.h"
#include "QtKeychainService.h"

#include <QDebug>
#include <QTextStream>

namespace quentier::utility {

namespace {

[[nodiscard]] ErrorString errorStringForErrorCode(
    const IKeychainService::ErrorCode errorCode)
{
    ErrorString error{QT_TRANSLATE_NOOP(
        "utility::keychain::IKeychainService", "Keychain job failed")};

    QString errorCodeStr;
    QTextStream strm{&errorCodeStr};
    strm << errorCode;

    error.details() = errorCodeStr;
    return error;
}

} // namespace

IKeychainService::~IKeychainService() noexcept = default;

QTextStream & operator<<(
    QTextStream & strm, const IKeychainService::ErrorCode errorCode)
{
    using ErrorCode = IKeychainService::ErrorCode;

    switch (errorCode) {
    case ErrorCode::NoError:
        strm << "No error";
        break;
    case ErrorCode::EntryNotFound:
        strm << "Entry not found";
        break;
    case ErrorCode::CouldNotDeleteEntry:
        strm << "Could not delete entry";
        break;
    case ErrorCode::AccessDeniedByUser:
        strm << "Access denied by user";
        break;
    case ErrorCode::AccessDenied:
        strm << "Access denied";
        break;
    case ErrorCode::NoBackendAvailable:
        strm << "No backend available";
        break;
    case ErrorCode::NotImplemented:
        strm << "Not implemented";
        break;
    case ErrorCode::OtherError:
        strm << "Other error";
        break;
    default:
        strm << "<unknown> (" << static_cast<qint64>(errorCode) << ")";
        break;
    }

    return strm;
}

IKeychainService::Exception::Exception(
    IKeychainService::ErrorCode errorCode) noexcept :
    IQuentierException{errorStringForErrorCode(errorCode)},
    m_errorCode{errorCode}
{}

IKeychainService::Exception::Exception(
    IKeychainService::ErrorCode errorCode,
    ErrorString errorDescription) noexcept :
    IQuentierException{std::move(errorDescription)}, m_errorCode{errorCode}
{}

IKeychainService::ErrorCode IKeychainService::Exception::errorCode()
    const noexcept
{
    return m_errorCode;
}

QString IKeychainService::Exception::exceptionDisplayName() const
{
    return QStringLiteral("IKeychainService::Exception");
}

void IKeychainService::Exception::raise() const
{
    throw *this;
}

IKeychainService::Exception * IKeychainService::Exception::clone() const
{
    return new Exception{m_errorCode, errorMessage()};
}

QDebug & operator<<(QDebug & dbg, const IKeychainService::ErrorCode errorCode)
{
    dbg << ToString(errorCode);
    return dbg;
}

IKeychainServicePtr newQtKeychainService()
{
    return std::make_shared<keychain::QtKeychainService>();
}

IKeychainServicePtr newObfuscatingKeychainService()
{
    return std::make_shared<keychain::ObfuscatingKeychainService>(
        utility::createOpenSslEncryptor());
}

IKeychainServicePtr newCompositeKeychainService(
    QString name, IKeychainServicePtr primaryKeychain,
    IKeychainServicePtr secondaryKeychain)
{
    return std::make_shared<keychain::CompositeKeychainService>(
        std::move(name), std::move(primaryKeychain),
        std::move(secondaryKeychain));
}

IKeychainServicePtr newMigratingKeychainService(
    IKeychainServicePtr sourceKeychain, IKeychainServicePtr sinkKeychain)
{
    return std::make_shared<keychain::MigratingKeychainService>(
        std::move(sourceKeychain), std::move(sinkKeychain));
}

} // namespace quentier::utility
