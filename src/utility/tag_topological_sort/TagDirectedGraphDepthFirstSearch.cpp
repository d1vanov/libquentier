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

#include "TagDirectedGraphDepthFirstSearch.h"

#include <quentier/utility/Compat.h>

namespace quentier {

TagDirectedGraphDepthFirstSearch::TagDirectedGraphDepthFirstSearch(
    const TagDirectedGraph & graph) :
    m_graph(graph)
{
    auto allTagIds = m_graph.allTagIds();
    for (const auto & tagId: qAsConst(allTagIds)) {
        if (!reached(tagId)) {
            depthFirstSearch(tagId);
        }
    }
}

const TagDirectedGraph & TagDirectedGraphDepthFirstSearch::graph() const
{
    return m_graph;
}

bool TagDirectedGraphDepthFirstSearch::reached(const QString & tagId) const
{
    return (m_reachedTagIds.find(tagId) != m_reachedTagIds.end());
}

bool TagDirectedGraphDepthFirstSearch::hasCycle() const
{
    return !m_cycle.isEmpty();
}

const QStack<QString> & TagDirectedGraphDepthFirstSearch::cycle() const
{
    return m_cycle;
}

void TagDirectedGraphDepthFirstSearch::depthFirstSearch(
    const QString & sourceTagId)
{
    auto stackIt = m_onStack.insert(sourceTagId).first;

    m_tagIdsInPreOrder.enqueue(sourceTagId);
    Q_UNUSED(m_reachedTagIds.insert(sourceTagId))

    auto childTagIds = m_graph.childTagIds(sourceTagId);
    for (const auto & childTagId: qAsConst(childTagIds)) {
        if (hasCycle()) {
            return;
        }

        if (!reached(childTagId)) {
            m_parentTagIdByChildTagId[childTagId] = sourceTagId;
            depthFirstSearch(childTagId);
        }
        else if (m_onStack.find(childTagId) != m_onStack.end()) {
            QString cycledId = childTagId;
            while (true) {
                if (cycledId == sourceTagId) {
                    break;
                }

                m_cycle.push(cycledId);

                auto pit = m_parentTagIdByChildTagId.find(cycledId);
                if (pit == m_parentTagIdByChildTagId.end()) {
                    break;
                }

                cycledId = pit.value();
            }

            m_cycle.push(sourceTagId);
            m_cycle.push(childTagId);
        }
    }

    m_tagIdsInPostOrder.enqueue(sourceTagId);
    m_tagIdsInReversePostOrder.push(sourceTagId);

    Q_UNUSED(m_onStack.erase(stackIt))
}

} // namespace quentier
