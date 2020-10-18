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

#ifndef LIB_QENTIER_TYPES_RESULT_H
#define LIB_QENTIER_TYPES_RESULT_H

#include <Error.h>
#include <ErrorString.h>

namespace quentier::types {

struct ResultBase
{
    error::ErrorCode m_errorCode;
    ErrorString m_errorMessage;
};

template <class T>
struct Result : ResultBase
{
    T m_value;
};

template <>
struct Result<void> : ResultBase
{};

} // namespace quentier::types

#endif // LIB_QENTIER_TYPES_RESULT_H
