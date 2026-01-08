/*
 * Copyright 2020-2025 Dmitry Ivanov
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

#include <quentier/utility/Linkage.h>

#include <QString>

namespace quentier::utility {

/**
 * humanReadableSize provides the human readable string denoting the size
 * of some piece of data
 *
 * @param bytes     The number of bytes for which the human readable size string
 *                  is required
 * @return          The human readable string corresponding to the passed in
 *                  number of bytes
 */
[[nodiscard]] QString QUENTIER_EXPORT humanReadableSize(quint64 bytes);

} // namespace quentier::utility
