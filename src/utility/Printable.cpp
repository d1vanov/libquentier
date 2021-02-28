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

#include <quentier/utility/Printable.h>

namespace quentier {

const QString Printable::toString() const
{
    QString str;
    QTextStream strm(&str, QIODevice::WriteOnly);
    strm << *this;
    return str;
}

Printable::Printable() = default;

Printable::Printable(const Printable &) = default;

Printable & Printable::operator=(const Printable &) = default;

Printable::~Printable() = default;

QDebug & operator<<(QDebug & debug, const Printable & printable)
{
    debug << printable.toString();
    return debug;
}

QTextStream & operator<<(QTextStream & strm, const Printable & printable)
{
    return printable.print(strm);
}

} // namespace quentier
