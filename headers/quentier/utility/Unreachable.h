/*
 * Copyright 2022-2024 Dmitry Ivanov
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

#include <QtGlobal>

#ifdef _MSC_VER
#define UNREACHABLE                                                            \
    do {                                                                       \
        Q_ASSERT(false);                                                       \
        __assume(0);                                                           \
    } while (false)
#else
#define UNREACHABLE                                                            \
    do {                                                                       \
        Q_ASSERT(false);                                                       \
        __builtin_unreachable();                                               \
    } while (false)
#endif
