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

#ifndef LIB_QUENTIER_UTILITY_TAG_TOPOLOGICAL_SORT_TAG_DIRECTED_GRAPH_DEPTH_FIRST_SEARCH_H
#define LIB_QUENTIER_UTILITY_TAG_TOPOLOGICAL_SORT_TAG_DIRECTED_GRAPH_DEPTH_FIRST_SEARCH_H

#include "TagDirectedGraph.h"

#include <QQueue>
#include <QStack>

#include <set>

namespace quentier {

class Q_DECL_HIDDEN TagDirectedGraphDepthFirstSearch
{
public:
    TagDirectedGraphDepthFirstSearch(const TagDirectedGraph & graph);

    const TagDirectedGraph & graph() const;
    bool reached(const QString & tagId) const;

    bool hasCycle() const;
    const QStack<QString> & cycle() const;

    const QQueue<QString> & tagIdsInPreOrder() const
    {
        return m_tagIdsInPreOrder;
    }

    const QQueue<QString> & tagIdsInPostOrder() const
    {
        return m_tagIdsInPostOrder;
    }

    const QStack<QString> & tagIdsInReversePostOrder() const
    {
        return m_tagIdsInReversePostOrder;
    }

private:
    void depthFirstSearch(const QString & sourceTagGuid);

private:
    TagDirectedGraph m_graph;
    std::set<QString> m_reachedTagIds;
    QHash<QString, QString> m_parentTagIdByChildTagId;
    QStack<QString> m_cycle;
    std::set<QString> m_onStack;

    QQueue<QString> m_tagIdsInPreOrder;
    QQueue<QString> m_tagIdsInPostOrder;
    QStack<QString> m_tagIdsInReversePostOrder;
};

} // namespace quentier

#endif // LIB_QUENTIER_UTILITY_TAG_TOPOLOGICAL_SORT_TAG_DIRECTED_GRAPH_DEPTH_FIRST_SEARCH_H
