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

#pragma once

#include <quentier/exception/IQuentierException.h>

namespace quentier::local_storage {

/**
 * @brief The LocalStorageOpenException is thrown on failure to open the local
 * storage database.
 */
class QUENTIER_EXPORT LocalStorageOpenException : public IQuentierException
{
public:
    explicit LocalStorageOpenException(const ErrorString & message);

    [[nodiscard]] LocalStorageOpenException * clone() const override;
    void raise() const override;

protected:
    [[nodiscard]] QString exceptionDisplayName() const override;
};

} // namespace quentier::local_storage
