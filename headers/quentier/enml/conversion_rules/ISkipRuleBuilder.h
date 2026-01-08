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

#include <quentier/enml/conversion_rules/Fwd.h>
#include <quentier/enml/conversion_rules/ISkipRule.h>
#include <quentier/utility/Linkage.h>

namespace quentier::enml::conversion_rules {

class QUENTIER_EXPORT ISkipRuleBuilder
{
public:
    virtual ~ISkipRuleBuilder();

    virtual ISkipRuleBuilder & setTarget(ISkipRule::Target target) = 0;
    virtual ISkipRuleBuilder & setValue(QString value) = 0;
    virtual ISkipRuleBuilder & setMatchMode(MatchMode matchMode) = 0;
    virtual ISkipRuleBuilder & setIncludeContents(bool includeContents) = 0;
    virtual ISkipRuleBuilder & setCaseSensitivity(
        Qt::CaseSensitivity caseSensitivity) = 0;

    [[nodiscard]] virtual ISkipRulePtr build() = 0;
};

} // namespace quentier::enml::conversion_rules
