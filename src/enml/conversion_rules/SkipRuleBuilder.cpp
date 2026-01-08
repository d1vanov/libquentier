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

#include "SkipRuleBuilder.h"
#include "SkipRule.h"

namespace quentier::enml::conversion_rules {

ISkipRuleBuilder & SkipRuleBuilder::setTarget(
    const ISkipRule::Target target) noexcept
{
    m_target = target;
    return *this;
}

ISkipRuleBuilder & SkipRuleBuilder::setValue(QString value)
{
    m_value = std::move(value);
    return *this;
}

ISkipRuleBuilder & SkipRuleBuilder::setMatchMode(
    const MatchMode matchMode) noexcept
{
    m_matchMode = matchMode;
    return *this;
}

ISkipRuleBuilder & SkipRuleBuilder::setIncludeContents(
    const bool includeContents) noexcept
{
    m_includeContents = includeContents;
    return *this;
}

ISkipRuleBuilder & SkipRuleBuilder::setCaseSensitivity(
    const Qt::CaseSensitivity caseSensitivity) noexcept
{
    m_caseSensitivity = caseSensitivity;
    return *this;
}

ISkipRulePtr SkipRuleBuilder::build()
{
    auto skipRule = std::make_shared<SkipRule>();
    skipRule->m_target = m_target;
    skipRule->m_value = std::move(m_value);
    skipRule->m_matchMode = m_matchMode;
    skipRule->m_includeContents = m_includeContents;
    skipRule->m_caseSensitivity = m_caseSensitivity;

    m_target = ISkipRule::Target::Element;
    m_value = QString{};
    m_matchMode = MatchMode::Equals;
    m_includeContents = true;
    m_caseSensitivity = Qt::CaseSensitive;

    return skipRule;
}

} // namespace quentier::enml::conversion_rules
