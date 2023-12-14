/*
 * Copyright 2023 Dmitry Ivanov
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

class QDebug;
class QTextStream;

namespace quentier::enml::conversion_rules {

/**
 * MatchMode represents different match modes used in ENML conversion rules
 */
enum class MatchMode
{
    /**
     * Element is considered matching if it exactly equals another element
     */
    Equals,
    /**
     * Element is considered matching if it starts with another element
     */
    StartsWith,
    /**
     * Element is considered matching if it ends with another element
     */
    EndsWith,
    /**
     * Element is considered matching if it contains another element
     */
    Contains
};

QUENTIER_EXPORT QTextStream & operator<<(
    QTextStream & strm, MatchMode matchMode);

QUENTIER_EXPORT QDebug & operator<<(QDebug & dbg, MatchMode matchMode);

} // namespace quentier::enml::conversion_rules
