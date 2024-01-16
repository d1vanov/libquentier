/*
 * Copyright 2017-2024 Dmitry Ivanov
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

#include <quentier/utility/Printable.h>

#include <QSharedDataPointer>

namespace quentier {

class ErrorStringData;

/**
 * @brief The ErrorString class encapsulates two (or more) strings which are
 * meant to contain translatable (base) and non-translatable (details) parts
 * of the error description
 *
 * 1. base() methods return const and non-const links to the primary
 *    translatable string
 * 2. details() methods ruturn const and non-const links to non-translatable
 *    string (coming from some third party library etc)
 * 3. additionalBases() methods return const and non-const links to additional
 *    translatable strings; one translatable string is not always enough because
 *    the error message might be composed from different parts
 */
class QUENTIER_EXPORT ErrorString : public Printable
{
public:
    explicit ErrorString(const char * error = nullptr);
    explicit ErrorString(const QString & error);

    ErrorString(const ErrorString & other);
    ErrorString(ErrorString && other) noexcept;

    ErrorString & operator=(const ErrorString & other);
    ErrorString & operator=(ErrorString && other) noexcept;

    ~ErrorString() override;

    [[nodiscard]] const QString & base() const noexcept;
    [[nodiscard]] QString & base();

    [[nodiscard]] const QStringList & additionalBases() const noexcept;
    [[nodiscard]] QStringList & additionalBases();

    [[nodiscard]] const QString & details() const noexcept;
    [[nodiscard]] QString & details();

    void setBase(QString error);
    void setBase(const char * error);

    void appendBase(const QString & error);
    void appendBase(const QStringList & errors);
    void appendBase(const char * error);

    void setDetails(const QString & error);
    void setDetails(const char * error);

    [[nodiscard]] bool isEmpty() const;
    void clear();

    [[nodiscard]] QString localizedString() const;
    [[nodiscard]] QString nonLocalizedString() const;

    QTextStream & print(QTextStream & strm) const override;

private:
    QSharedDataPointer<ErrorStringData> d;
};

[[nodiscard]] QUENTIER_EXPORT bool operator==(
    const ErrorString & lhs, const ErrorString & rhs) noexcept;

[[nodiscard]] QUENTIER_EXPORT bool operator!=(
    const ErrorString & lhs, const ErrorString & rhs) noexcept;

} // namespace quentier
