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

#ifndef LIB_QUENTIER_UTILITY_TAG_TOPOLOGICAL_SORT_TAG_DIRECTED_GRAPH_DEPTH_FIRST_SEARCH_H
#define LIB_QUENTIER_UTILITY_TAG_TOPOLOGICAL_SORT_TAG_DIRECTED_GRAPH_DEPTH_FIRST_SEARCH_H

#include "TagDirectedGraph.h"
#include <QStack>
#include <QQueue>
#include <set>

namespace quentier {

class Q_DECL_HIDDEN TagDirectedGraphDepthFirstSearch
{
public:
    TagDirectedGraphDepthFirstSearch(const TagDirectedGraph & graph);

    const TagDirectedGraph & graph() const;
    bool reached(const QString & tagGuid) const;

    bool hasCycle() const;
    const QStack<QString> & cycle() const;

    const QQueue<QString> & tagGuidsInPreOrder() const { return m_tagGuidsInPreOrder; }
    const QQueue<QString> & tagGuidsInPostOrder() const { return m_tagGuidsInPostOrder; }
    const QStack<QString> & tagGuidsInReversePostOrder() const { return m_tagGuidsInReversePostOrder; }

private:
    void depthFirstSearch(const QString & sourceTagGuid);

private:
    TagDirectedGraph            m_graph;
    std::set<QString>           m_reachedTagGuids;
    QHash<QString, QString>     m_parentTagGuidByChildTagGuid;
    QStack<QString>             m_cycle;
    std::set<QString>           m_onStack;

    QQueue<QString>             m_tagGuidsInPreOrder;
    QQueue<QString>             m_tagGuidsInPostOrder;
    QStack<QString>             m_tagGuidsInReversePostOrder;
};

} // namespace quentier

#endif // LIB_QUENTIER_UTILITY_TAG_TOPOLOGICAL_SORT_TAG_DIRECTED_GRAPH_DEPTH_FIRST_SEARCH_H
