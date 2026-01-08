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

#include <quentier/enml/conversion_rules/ISkipRule.h>

namespace quentier::enml::conversion_rules {

namespace {

template <class T>
void printTarget(T & t, const ISkipRule::Target target)
{
    switch (target) {
    case ISkipRule::Target::Element:
        t << "[Element]";
        break;
    case ISkipRule::Target::AttibuteName:
        t << "[AttributeName]";
        break;
    case ISkipRule::Target::AttributeValue:
        t << "[AttributeValue]";
        break;
    }
}

} // namespace

ISkipRule::~ISkipRule() = default;

QTextStream & ISkipRule::print(QTextStream & strm) const
{
    printTarget(strm, target());
    strm << " " << value() << ": " << matchMode() << ", " << caseSensitivity()
         << ", include contents = " << (includeContents() ? "true" : "false");
    return strm;
}

QTextStream & operator<<(QTextStream & strm, const ISkipRule::Target target)
{
    printTarget(strm, target);
    return strm;
}

QDebug & operator<<(QDebug & dbg, const ISkipRule::Target target)
{
    printTarget(dbg, target);
    return dbg;
}

} // namespace quentier::enml::conversion_rules
