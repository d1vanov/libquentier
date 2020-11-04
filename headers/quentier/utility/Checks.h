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

#ifndef LIB_QUENTIER_UTILITY_CHECKS_H
#define LIB_QUENTIER_UTILITY_CHECKS_H

#include <quentier/utility/Linkage.h>

#include <QString>

namespace quentier {

/**
 * checkGuid checks the valitidy of the input guid
 *
 * @param guid      The guid to be checked for validity
 * @return          True if the passed in guid is valid, false otherwise
 */
bool QUENTIER_EXPORT checkGuid(const QString & guid);

/**
 * checkUpdateSequenceNumber checks the passed in update sequence number
 * for validity
 *
 * @param updateSequenceNumber  The update sequence number to be checked
 *                              for validity
 * @return                      True if the passed in update sequence number
 *                              is valid, false otherwise
 */
bool QUENTIER_EXPORT
checkUpdateSequenceNumber(const int32_t updateSequenceNumber);

} // namespace quentier

#endif // LIB_QUENTIER_UTILITY_CHECKS_H
