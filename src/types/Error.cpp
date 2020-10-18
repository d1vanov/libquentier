/*
 * Copyright 2020 Dmitry Ivanov
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

#include <quentier/types/Error.h>

#include <QDebug>
#include <QTextStream>

namespace quentier::types::error {

namespace {

template <class T>
void printCode(T & strm, const Code code)
{
    switch(code)
    {
    case Code::Ok:
        strm << "Ok";
        break;
    case Code::Already:
        strm << "Already";
        break;
    case Code::Canceled:
        strm << "Canceled";
        break;
    case Code::InProgress:
        strm << "In progress";
        break;
    case Code::DataUnavailable:
        strm << "Data unavailable";
        break;
    case Code::ConditionsUnmet:
        strm << "Conditions unmet";
        break;
    case Code::PermissionDenied:
        strm << "Permission denied";
        break;
    case Code::NetworkError:
        strm << "Network error";
        break;
    case Code::IOError:
        strm << "I/O error";
        break;
    case Code::RangeError:
        strm << "Range error";
        break;
    case Code::Timeout:
        strm << "Timeout";
        break;
    default:
        strm << "Unknown (" << static_cast<qint64>(code) << ")";
        break;
    }
}

template <class T>
void printFacility(T & strm, const Facility facility)
{
    switch(facility)
    {
    case Facility::LocalStorage:
        strm << "Local storage";
        break;
    case Facility::Synchronization:
        strm << "Synchronization";
        break;
    case Facility::NoteEditor:
        strm << "Note editor";
        break;
    case Facility::Other:
        strm << "Other";
        break;
    case Facility::User:
        strm << "User";
        break;
    default:
        strm << "Unknown (" << static_cast<qint64>(facility) << ")";
        break;
    }
}

template <class T>
void printErrorCode(T & strm, const ErrorCode errorCode)
{
    strm << "ErrorCode: code = ";
    printCode(strm, code(errorCode));

    strm << ", facility = ";
    printCode(strm, facility(errorCode));
}

} // namespace

ErrorCode makeOkErrorCode(const Facility facility) noexcept
{
    return makeErrorCode(Code::Ok, facility);
}

ErrorCode makeErrorCode(const Code code, const Facility facility) noexcept
{
    // Set lower 32 bits - the code
    ErrorCode errorCode = static_cast<ErrorCode>(code);

    // Set upper 32 bits - the facility
    errorCode =
        (static_cast<qint64>(facility) << 32) | (errorCode & 0xffffffff);

    return errorCode;
}

Facility facility(const ErrorCode errorCode) noexcept
{
    return static_cast<Facility>(static_cast<qint32>(errorCode >> 32));
}

Code code(const ErrorCode errorCode) noexcept
{
    return static_cast<Code>(static_cast<qint32>(errorCode & 0xffffffff));
}

bool isSuccess(const ErrorCode errorCode) noexcept
{
    switch(code(errorCode))
    {
    case Code::Ok:
    case Code::Already:
        return true;
    default:
        return false;
    }
}

bool isFailure(const ErrorCode errorCode) noexcept
{
    return !isSuccess(errorCode);
}

bool isRetriable(const ErrorCode errorCode)
{
    switch(code(errorCode))
    {
    case Code::Canceled:
    case Code::InProgress:
    case Code::NetworkError:
    case Code::Timeout:
        return true;
    default:
        return false;
    }
}

} // namespace quentier::types::error
