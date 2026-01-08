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

#include <quentier/enml/conversion_rules/ISkipRule.h>

namespace quentier::enml::conversion_rules {

struct SkipRule: public ISkipRule
{
public: // ISkipRule
    [[nodiscard]] Target target() const noexcept override;
    [[nodiscard]] QString value() const override;
    [[nodiscard]] MatchMode matchMode() const noexcept override;
    [[nodiscard]] bool includeContents() const noexcept override;
    [[nodiscard]] Qt::CaseSensitivity caseSensitivity() const noexcept override;

public:
    Target m_target;
    QString m_value;
    MatchMode m_matchMode;
    bool m_includeContents = true;
    Qt::CaseSensitivity m_caseSensitivity = Qt::CaseSensitive;
};

} // namespace quentier::enml::conversion_rules
