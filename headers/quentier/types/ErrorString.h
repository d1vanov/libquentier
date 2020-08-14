/*
 * Copyright 2017-2020 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_TYPES_ERROR_STRING_H
#define LIB_QUENTIER_TYPES_ERROR_STRING_H

#include <quentier/utility/Printable.h>

#include <QSharedDataPointer>

namespace quentier {

QT_FORWARD_DECLARE_CLASS(ErrorStringData)

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
    ErrorString & operator=(const ErrorString & other);
    virtual ~ErrorString() override;

    const QString & base() const;
    QString & base();

    const QStringList & additionalBases() const;
    QStringList & additionalBases();

    const QString & details() const;
    QString & details();

    void setBase(const QString & error);
    void setBase(const char * error);

    void appendBase(const QString & error);
    void appendBase(const QStringList & errors);
    void appendBase(const char * error);

    void setDetails(const QString & error);
    void setDetails(const char * error);

    bool isEmpty() const;
    void clear();

    QString localizedString() const;
    QString nonLocalizedString() const;

    virtual QTextStream & print(QTextStream & strm) const override;

private:
    QSharedDataPointer<ErrorStringData> d;
};

} // namespace quentier

#endif // LIB_QUENTIER_TYPES_ERROR_STRING_H
