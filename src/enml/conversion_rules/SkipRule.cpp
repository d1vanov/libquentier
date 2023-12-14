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

#include "SkipRule.h"

namespace quentier::enml::conversion_rules {

ISkipRule::Target SkipRule::target() const noexcept
{
    return m_target;
}

QString SkipRule::value() const
{
    return m_value;
}

MatchMode SkipRule::matchMode() const noexcept
{
    return m_matchMode;
}

bool SkipRule::includeContents() const noexcept
{
    return m_includeContents;
}

Qt::CaseSensitivity SkipRule::caseSensitivity() const noexcept
{
    return m_caseSensitivity;
}

} // namespace quentier::enml::conversion_rules
