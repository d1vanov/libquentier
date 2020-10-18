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

#ifndef LIB_QUENTIER_TYPES_ERROR_H
#define LIB_QUENTIER_TYPES_ERROR_H

#include <quentier/utility/Linkage.h>

#include <QtGlobal>

QT_FORWARD_DECLARE_CLASS(QDebug)
QT_FORWARD_DECLARE_CLASS(QTextStream)

namespace quentier::types::error {

using ErrorCode = qint64;

////////////////////////////////////////////////////////////////////////////////

enum class Code
{
    Ok,
    Already,
    Canceled,
    InProgress,
    DataUnavailable,
    ConditionsUnmet,
    PermissionDenied,
    NetworkError,
    IOError,
    RangeError,
    Timeout
};

QUENTIER_EXPORT QTextStream & operator<<(QTextStream & strm, const Code code);

QUENTIER_EXPORT QDebug & operator<<(QDebug & dbg, const Code code);

////////////////////////////////////////////////////////////////////////////////

enum class Facility
{
    LocalStorage,
    Synchronization,
    NoteEditor,
    Other,
    User
};

QUENTIER_EXPORT QTextStream & operator<<(
    QTextStream & strm, const Facility facility);

QUENTIER_EXPORT QDebug & operator<<(QDebug & dbg, const Facility facility);

////////////////////////////////////////////////////////////////////////////////

ErrorCode QUENTIER_EXPORT
    makeOkErrorCode(const Facility facility = Facility::Other) noexcept;

ErrorCode QUENTIER_EXPORT makeErrorCode(
    const Code code = Code::Ok,
    const Facility facility = Facility::Other) noexcept;

Facility QUENTIER_EXPORT facility(const ErrorCode code) noexcept;

Code QUENTIER_EXPORT code(const ErrorCode code) noexcept;

bool QUENTIER_EXPORT isSuccess(const ErrorCode code) noexcept;

bool QUENTIER_EXPORT isFailure(const ErrorCode code) noexcept;

bool QUENTIER_EXPORT isRetriable(const ErrorCode code) noexcept;

} // namespace quentier::types::error

#endif // LIB_QUENTIER_TYPES_ERROR_H
