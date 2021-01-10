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

#ifndef LIB_QUENTIER_TYPES_RESOURCE_UTILS_H
#define LIB_QUENTIER_TYPES_RESOURCE_UTILS_H

#include <quentier/utility/Linkage.h>

#include <QString>

namespace qevercloud {

class Resource;

} // namespace qevercloud

namespace quentier {

[[nodiscard]] QUENTIER_EXPORT QString resourceDisplayName(
    const qevercloud::Resource & resource);

[[nodiscard]] QUENTIER_EXPORT QString preferredFileSuffix(
    const qevercloud::Resource & resource);

} // namespace quentier

#endif // LIB_QUENTIER_TYPES_RESOURCE_UTILS_H