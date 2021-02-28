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

#ifndef LIB_QUENTIER_UTILITY_INITIALIZE_H
#define LIB_QUENTIER_UTILITY_INITIALIZE_H

#include <quentier/utility/Linkage.h>

namespace quentier {

/**
 * initializeLibquentier - the function that needs to be called during
 * the initial stages of application startup for the library to work properly
 */
void QUENTIER_EXPORT initializeLibquentier();

} // namespace quentier

#endif // LIB_QUENTIER_UTILITY_INITIALIZE_H
