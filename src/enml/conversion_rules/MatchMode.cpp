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

#include <quentier/enml/conversion_rules/MatchMode.h>

#include <QDebug>
#include <QTextStream>

namespace quentier::enml::conversion_rules {

namespace {

template <class T>
void printMatchMode(T & t, const MatchMode matchMode)
{
    switch (matchMode) {
    case MatchMode::Equals:
        t << "Equals";
        break;
    case MatchMode::StartsWith:
        t << "StartsWith";
        break;
    case MatchMode::EndsWith:
        t << "EndsWith";
        break;
    case MatchMode::Contains:
        t << "Contains";
        break;
    }
}

} // namespace

QTextStream & operator<<(QTextStream & strm, const MatchMode matchMode)
{
    printMatchMode(strm, matchMode);
    return strm;
}

QDebug & operator<<(QDebug & dbg, const MatchMode matchMode)
{
    printMatchMode(dbg, matchMode);
    return dbg;
}

} // namespace quentier::enml::conversion_rules
