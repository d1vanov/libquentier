/*
 * Copyright 2016-2021 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_EXCEPTION_I_QUENTIER_EXCEPTION_H
#define LIB_QUENTIER_EXCEPTION_I_QUENTIER_EXCEPTION_H

#include <exception>
#include <quentier/types/ErrorString.h>
#include <quentier/utility/Printable.h>

namespace quentier {

/**
 * @brief The IQuentierException class represents the interface for exceptions
 * specific to libquentier and applications based on it.
 *
 * In addition to standard exception features inherited from std::exception,
 * IQuentierException based exceptions can provide both localized and
 * non-localized error messages.
 */
class QUENTIER_EXPORT IQuentierException :
    public Printable,
    public std::exception
{
public:
    ~IQuentierException() noexcept override;

    [[nodiscard]] QString localizedErrorMessage() const;
    [[nodiscard]] QString nonLocalizedErrorMessage() const;

    [[nodiscard]] const char * what() const noexcept override;

    QTextStream & print(QTextStream & strm) const override;

protected:
    explicit IQuentierException(const ErrorString & message);
    IQuentierException(const IQuentierException & other);
    IQuentierException & operator=(const IQuentierException & other);

    [[nodiscard]] virtual QString exceptionDisplayName() const = 0;

private:
    ErrorString m_message;
    char * m_whatMessage = nullptr;
};

} // namespace quentier

#endif // LIB_QUENTIER_EXCEPTION_I_QUENTIER_EXCEPTION_H
