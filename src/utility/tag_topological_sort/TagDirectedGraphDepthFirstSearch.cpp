/*
 * Copyright 2017 Dmitry Ivanov
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

namespace quentier {

TagDirectedGraphDepthFirstSearch::TagDirectedGraphDepthFirstSearch(const TagDirectedGraph & graph) :
    m_graph(graph),
    m_reachedTagGuids(),
    m_parentTagGuidByChildTagGuid(),
    m_cycle(),
    m_onStack(),
    m_tagGuidsInPreOrder(),
    m_tagGuidsInPostOrder(),
    m_tagGuidsInReversePostOrder()
{
    QStringList allTagGuids = m_graph.allTagGuids();
    for(auto it = allTagGuids.constBegin(), end = allTagGuids.constEnd(); it != end; ++it)
    {
        if (!reached(*it)) {
            depthFirstSearch(*it);
        }
    }
}

const TagDirectedGraph & TagDirectedGraphDepthFirstSearch::graph() const
{
    return m_graph;
}

bool TagDirectedGraphDepthFirstSearch::reached(const QString & tagGuid) const
{
    return (m_reachedTagGuids.find(tagGuid) != m_reachedTagGuids.end());
}

bool TagDirectedGraphDepthFirstSearch::hasCycle() const
{
    return !m_cycle.isEmpty();
}

const QStack<QString> & TagDirectedGraphDepthFirstSearch::cycle() const
{
    return m_cycle;
}

void TagDirectedGraphDepthFirstSearch::depthFirstSearch(const QString & sourceTagGuid)
{
    auto stackIt = m_onStack.insert(sourceTagGuid).first;

    m_tagGuidsInPreOrder.enqueue(sourceTagGuid);
    Q_UNUSED(m_reachedTagGuids.insert(sourceTagGuid))

    QStringList childTagGuids = m_graph.childTagGuids(sourceTagGuid);
    for(auto it = childTagGuids.constBegin(), end = childTagGuids.constEnd(); it != end; ++it)
    {
        if (hasCycle()) {
            return;
        }

        if (!reached(*it)) {
            m_parentTagGuidByChildTagGuid[*it] = sourceTagGuid;
            depthFirstSearch(*it);
        }
        else if (m_onStack.find(*it) != m_onStack.end())
        {
            QString cycledGuid = *it;
            while(true)
            {
                if (cycledGuid == sourceTagGuid) {
                    break;
                }

                m_cycle.push(cycledGuid);

                auto pit = m_parentTagGuidByChildTagGuid.find(cycledGuid);
                if (pit == m_parentTagGuidByChildTagGuid.end()) {
                    break;
                }

                cycledGuid = pit.value();
            }
            m_cycle.push(sourceTagGuid);
            m_cycle.push(*it);
        }
    }

    m_tagGuidsInPostOrder.enqueue(sourceTagGuid);
    m_tagGuidsInReversePostOrder.push(sourceTagGuid);

    Q_UNUSED(m_onStack.erase(stackIt))
}

} // namespace quentier
