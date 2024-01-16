/*
 * Copyright 2016-2024 Dmitry Ivanov
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

namespace quentier {

QT_FORWARD_DECLARE_CLASS(SysInfoPrivate)

class QUENTIER_EXPORT SysInfo
{
public:
    SysInfo();
    ~SysInfo() noexcept;

    [[nodiscard]] qint64 pageSize();
    [[nodiscard]] qint64 totalMemory();
    [[nodiscard]] qint64 freeMemory();

    [[nodiscard]] QString stackTrace();

    [[nodiscard]] QString platformName();

private:
    Q_DISABLE_COPY(SysInfo)

private:
    SysInfoPrivate * const d_ptr;
    Q_DECLARE_PRIVATE(SysInfo)
};

} // namespace quentier
