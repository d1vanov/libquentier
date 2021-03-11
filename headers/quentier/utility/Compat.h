/*
 * Copyright 2020-2021 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_UTILITY_COMPAT_H
#define LIB_QUENTIER_UTILITY_COMPAT_H

#include <QHash>
#include <QString>

// Compatibility with boost parts which require to take a hash of QString

inline std::size_t hash_value(const QString & x) noexcept
{
    return qHash(x);
}

#endif // LIB_QUENTIER_UTILITY_COMPAT_H
