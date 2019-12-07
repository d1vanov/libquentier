/*
 * Copyright 2016-2018 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_UTILITY_MACROS_H
#define LIB_QUENTIER_UTILITY_MACROS_H

#include <QtGlobal>
#include <QString>

#if QT_VERSION < QT_VERSION_CHECK(5, 7, 0)
#include <type_traits>
#endif

#ifndef Q_DECL_CONSTEXPR
#define Q_DECL_CONSTEXPR
#endif

#ifndef Q_DECL_NOTHROW
#define Q_DECL_NOTHROW throw()
#endif

#ifndef Q_DECL_EQ_DELETE
#ifdef CPP11_COMPLIANT
#define Q_DECL_EQ_DELETE = delete
#else
#define Q_DECL_EQ_DELETE
#endif
#endif // Q_DECL_EQ_DELETE

#if QT_VERSION < QT_VERSION_CHECK(5, 7, 0)

// this adds const to non-const objects (like std::as_const)
template <typename T>
Q_DECL_CONSTEXPR typename std::add_const<T>::type &qAsConst(T &t) Q_DECL_NOTHROW { return t; }
// prevent rvalue arguments:
template <typename T>
void qAsConst(const T &&) Q_DECL_EQ_DELETE;

#endif

#ifdef QNSIGNAL
#undef QNSIGNAL
#endif

#ifdef QNSLOT
#undef QNSLOT
#endif

#define QNSIGNAL(className, methodName, ...) &className::methodName
#define QNSLOT(className, methodName, ...) &className::methodName

#if defined(_MSC_VER) && (_MSC_VER <= 1800)
#ifdef QStringLiteral
#undef QStringLiteral
#define QStringLiteral(x) QString::fromUtf8(x, sizeof(x) - 1)
#endif
#endif

#endif // LIB_QUENTIER_UTILITY_MACROS_H
