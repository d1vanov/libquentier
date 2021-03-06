/*
 * Copyright 2016-2020 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_EXCEPTION_APPLICATION_SETTINGS_INITIALIZATION_EXCEPTION_H
#define LIB_QUENTIER_EXCEPTION_APPLICATION_SETTINGS_INITIALIZATION_EXCEPTION_H

#include <quentier/exception/IQuentierException.h>

namespace quentier {

/**
 * @brief The ApplicationSettingsInitializationException can be thrown from
 * methods of ApplicationSettings class if it's unable to locate the file with
 * persistent settings
 */
class QUENTIER_EXPORT ApplicationSettingsInitializationException :
    public IQuentierException
{
public:
    explicit ApplicationSettingsInitializationException(
        const ErrorString & message);

protected:
    virtual const QString exceptionDisplayName() const override;
};

} // namespace quentier

#endif // LIB_QUENTIER_EXCEPTION_APPLICATION_SETTINGS_INITIALIZATION_EXCEPTION_H
