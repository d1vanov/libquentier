/*
 * Copyright 2017-2020 Dmitry Ivanov
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

#include "TagDirectedGraph.h"

#include <quentier/utility/Compat.h>

namespace quentier {

bool TagDirectedGraph::isEmpty() const
{
    return m_childTagIdsByParentTagId.isEmpty();
}

bool TagDirectedGraph::empty() const
{
    return m_childTagIdsByParentTagId.empty();
}

void TagDirectedGraph::clear()
{
    m_childTagIdsByParentTagId.clear();
}

void TagDirectedGraph::addChild(
    const QString & parentTagId, const QString & childTagId)
{
    auto & childTagIds = m_childTagIdsByParentTagId[parentTagId];
    if (!childTagIds.contains(childTagId)) {
        childTagIds << childTagId;
    }
}

QStringList TagDirectedGraph::childTagIds(const QString & parentTagId) const
{
    auto it = m_childTagIdsByParentTagId.find(parentTagId);
    if (it != m_childTagIdsByParentTagId.end()) {
        return it.value();
    }

    return QStringList();
}

QStringList TagDirectedGraph::allTagIds() const
{
    QStringList result;

    for (auto it = m_childTagIdsByParentTagId.constBegin(),
              end = m_childTagIdsByParentTagId.constEnd();
         it != end; ++it)
    {
        result << it.key();

        const auto & childTagIds = it.value();
        for (const auto & childTagId: ::qAsConst(childTagIds)) {
            result << childTagId;
        }
    }

    Q_UNUSED(result.removeDuplicates())
    return result;
}

} // namespace quentier
