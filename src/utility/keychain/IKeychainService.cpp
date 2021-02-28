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

#include <quentier/utility/IKeychainService.h>

#include "CompositeKeychainService.h"
#include "MigratingKeychainService.h"
#include "ObfuscatingKeychainService.h"
#include "QtKeychainService.h"

#include <quentier/utility/Printable.h>

#include <QDebug>
#include <QTextStream>

namespace quentier {

IKeychainService::IKeychainService(QObject * parent) : QObject(parent) {}

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

QDebug & operator<<(QDebug & dbg, const IKeychainService::ErrorCode errorCode)
{
    dbg << ToString(errorCode);
    return dbg;
}

IKeychainServicePtr newQtKeychainService(QObject * parent)
{
    return std::make_shared<QtKeychainService>(parent);
}

IKeychainServicePtr newObfuscatingKeychainService(QObject * parent)
{
    return std::make_shared<ObfuscatingKeychainService>(parent);
}

IKeychainServicePtr newCompositeKeychainService(
    QString name, IKeychainServicePtr primaryKeychain,
    IKeychainServicePtr secondaryKeychain, QObject * parent)
{
    return std::make_shared<CompositeKeychainService>(
        std::move(name), std::move(primaryKeychain),
        std::move(secondaryKeychain), parent);
}

IKeychainServicePtr newMigratingKeychainService(
    IKeychainServicePtr sourceKeychain, IKeychainServicePtr sinkKeychain,
    QObject * parent)
{
    return std::make_shared<MigratingKeychainService>(
        std::move(sourceKeychain), std::move(sinkKeychain), parent);
}

} // namespace quentier
