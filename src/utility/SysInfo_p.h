/*
 * Copyright 2016-2020 Dmitry Ivanov
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

#ifndef LIBQUENTIER_UTILITY_SYS_INFO_PRIVATE_H
#define LIBQUENTIER_UTILITY_SYS_INFO_PRIVATE_H

#include <QMutex>

namespace quentier {

class Q_DECL_HIDDEN SysInfoPrivate
{
public:
    SysInfoPrivate() = default;

    QMutex m_mutex;
};

} // namespace quentier

#endif // LIBQUENTIER_UTILITY_SYS_INFO_PRIVATE_H
