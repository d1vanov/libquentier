/*
 * Copyright 2021 Dmitry Ivanov
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

#include <quentier/exception/RuntimeError.h>

namespace quentier {

RuntimeError::RuntimeError(ErrorString message) :
    IQuentierException(std::move(message))
{}

RuntimeError::~RuntimeError() noexcept = default;

RuntimeError * RuntimeError::clone() const
{
    return new RuntimeError{errorMessage()};
}

void RuntimeError::raise() const
{
    throw *this;
}

QString RuntimeError::exceptionDisplayName() const
{
    return QStringLiteral("RuntimeError");
}

} // namespace quentier
