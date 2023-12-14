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

#include <quentier/enml/conversion_rules/ISkipRuleBuilder.h>

namespace quentier::enml::conversion_rules {

class SkipRuleBuilder: public ISkipRuleBuilder
{
public: // ISkipRuleBuilder
    ISkipRuleBuilder & setTarget(ISkipRule::Target target) noexcept override;
    ISkipRuleBuilder & setValue(QString value) override;
    ISkipRuleBuilder & setMatchMode(MatchMode matchMode) noexcept override;
    ISkipRuleBuilder & setIncludeContents(
        bool includeContents) noexcept override;

    ISkipRuleBuilder & setCaseSensitivity(
        Qt::CaseSensitivity caseSensitivity) noexcept override;

    [[nodiscard]] ISkipRulePtr build() override;

private:
    ISkipRule::Target m_target = ISkipRule::Target::Element;
    QString m_value;
    MatchMode m_matchMode = MatchMode::Equals;
    bool m_includeContents = true;
    Qt::CaseSensitivity m_caseSensitivity = Qt::CaseSensitive;
};

} // namespace quentier::enml::conversion_rules
