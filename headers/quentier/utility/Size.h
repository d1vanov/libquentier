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

#ifndef LIB_QUENTIER_UTILITY_SIZE_H
#define LIB_QUENTIER_UTILITY_SIZE_H

#include <quentier/utility/Linkage.h>

#include <QString>

namespace quentier {

/**
 * humanReadableSize provides the human readable string denoting the size
 * of some piece of data
 *
 * @param bytes     The number of bytes for which the human readable size string
 *                  is required
 * @return          The human readable string corresponding to the passed in
 *                  number of bytes
 */
const QString QUENTIER_EXPORT humanReadableSize(const quint64 bytes);

} // namespace quentier

#endif // LIB_QUENTIER_UTILITY_SIZE_H
