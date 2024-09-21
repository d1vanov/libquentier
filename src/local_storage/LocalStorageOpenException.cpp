/*
 * Copyright 2016-2024 Dmitry Ivanov
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

#include <quentier/local_storage/LocalStorageOpenException.h>

namespace quentier::local_storage {

LocalStorageOpenException::LocalStorageOpenException(
    const ErrorString & message) :
    IQuentierException(message)
{}

QString LocalStorageOpenException::exceptionDisplayName() const
{
    return QStringLiteral("LocalStorageOpenException");
}

LocalStorageOpenException * LocalStorageOpenException::clone() const
{
    return new LocalStorageOpenException{errorMessage()};
}

void LocalStorageOpenException::raise() const
{
    throw *this;
}

} // namespace quentier::local_storage