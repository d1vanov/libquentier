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

#include <quentier/utility/Size.h>

#include <QStringList>

namespace quentier {

const QString humanReadableSize(const quint64 bytes)
{
    QStringList list;
    list << QStringLiteral("Kb") << QStringLiteral("Mb") << QStringLiteral("Gb")
         << QStringLiteral("Tb");

    QStringListIterator it(list);
    QString unit = QStringLiteral("bytes");

    double num = static_cast<double>(bytes);
    while (num >= 1024.0 && it.hasNext()) {
        unit = it.next();
        num /= 1024.0;
    }

    QString result = QString::number(num, 'f', 2);
    result += QStringLiteral(" ");
    result += unit;

    return result;
}

} // namespace quentier
