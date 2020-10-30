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

#ifndef LIB_QUENTIER_UTILITY_COMPAT_H
#define LIB_QUENTIER_UTILITY_COMPAT_H

#include <QHash>
#include <QString>
#include <QtGlobal>

#if QT_VERSION < QT_VERSION_CHECK(5, 7, 0)
#include <type_traits>
#endif

// Compatibility with older Qt versions

#if QT_VERSION < QT_VERSION_CHECK(5, 7, 0)

// this adds const to non-const objects (like std::as_const)
template <typename T>
Q_DECL_CONSTEXPR typename std::add_const<T>::type & qAsConst(T & t)
    Q_DECL_NOTHROW
{
    return t;
}

// prevent rvalue arguments:
template <typename T>
void qAsConst(const T &&) = delete;

#endif

// Compatibility with boost parts which require to take a hash of QString

inline std::size_t hash_value(QString x) noexcept
{
    return qHash(x);
}

#endif // LIB_QUENTIER_UTILITY_COMPAT_H
