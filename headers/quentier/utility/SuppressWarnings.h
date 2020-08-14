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

#ifndef LIB_QUENTIER_UTILITY_SUPPRESS_WARNINGS_H
#define LIB_QUENTIER_UTILITY_SUPPRESS_WARNINGS_H

////////////////////////////////////////////////////////////////////////////////
// Common macros
////////////////////////////////////////////////////////////////////////////////

#define STRINGIFY(a) #a

// Define empty macros doing nothing for supported compilers, they would be used
// as fallback when any of these compilers are not actually used

#define SAVE_WARNINGS

#define CLANG_SUPPRESS_WARNING(warning)
#define GCC_SUPPRESS_WARNING(warning)
#define MSVC_SUPPRESS_WARNING(warning)

#define RESTORE_WARNINGS

////////////////////////////////////////////////////////////////////////////////
// Clang implementation
////////////////////////////////////////////////////////////////////////////////

#if defined(__clang__)

#undef CLANG_SUPPRESS_WARNING

#define CLANG_SUPPRESS_WARNING(warning)                                        \
    _Pragma(                                                                   \
        STRINGIFY(clang diagnostic ignored #warning)) // CLANG_IGNORE_WARNING

#undef SAVE_WARNINGS

#define SAVE_WARNINGS _Pragma("clang diagnostic push") // SAVE_WARNINGS

#undef RESTORE_WARNINGS

#define RESTORE_WARNINGS _Pragma("clang diagnostic pop") // RESTORE_WARNINGS

#endif // clang

////////////////////////////////////////////////////////////////////////////////
// GCC implementation
////////////////////////////////////////////////////////////////////////////////

// Clang can mimic gcc so need to ensure it's indeed gcc
#if defined(__GNUC__) && !defined(__clang__)

#undef GCC_SUPPRESS_WARNING

#define GCC_SUPPRESS_WARNING(warning)                                          \
    _Pragma(STRINGIFY(GCC diagnostic ignored #warning)) // GCC_SUPPRESS_WARNING

#undef SAVE_WARNINGS

#define SAVE_WARNINGS _Pragma("GCC diagnostic push") // SAVE_WARNINGS

#undef RESTORE_WARNINGS

#define RESTORE_WARNINGS _Pragma("GCC diagnostic pop") // RESTORE_WARNINGS

#endif // GCC

////////////////////////////////////////////////////////////////////////////////
// MSVC implementation
////////////////////////////////////////////////////////////////////////////////

#if defined(_MSC_VER)

#undef MSVC_SUPPRESS_WARNING

#define MSVC_SUPPRESS_WARNING(number)                                          \
    __pragma(warning(disable : number)) // MSVC_SUPPRESS_WARNING

#undef SAVE_WARNINGS

#define SAVE_WARNINGS __pragma(warning(push)) // SAVE_WARNINGS

#undef RESTORE_WARNINGS

#define RESTORE_WARNINGS __pragma(warning(pop)) // RESTORE_WARNINGS

#endif // MSVC

#endif // LIB_QUENTIER_UTILITY_SUPPRESS_WARNINGS_H
