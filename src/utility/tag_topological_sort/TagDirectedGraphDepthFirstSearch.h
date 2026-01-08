/*
 * Copyright 2017-2024 Dmitry Ivanov
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

#include "TagDirectedGraph.h"

#include <QQueue>
#include <QStack>

#include <set>

namespace quentier {

class TagDirectedGraphDepthFirstSearch
{
public:
    TagDirectedGraphDepthFirstSearch(TagDirectedGraph graph);

    [[nodiscard]] const TagDirectedGraph & graph() const noexcept;
    [[nodiscard]] bool reached(const QString & tagId) const noexcept;

    [[nodiscard]] bool hasCycle() const noexcept;
    [[nodiscard]] const QStack<QString> & cycle() const noexcept;

    [[nodiscard]] const QQueue<QString> & tagIdsInPreOrder() const noexcept
    {
        return m_tagIdsInPreOrder;
    }

    [[nodiscard]] const QQueue<QString> & tagIdsInPostOrder() const noexcept
    {
        return m_tagIdsInPostOrder;
    }

    [[nodiscard]] const QStack<QString> & tagIdsInReversePostOrder()
        const noexcept
    {
        return m_tagIdsInReversePostOrder;
    }

private:
    void depthFirstSearch(const QString & sourceTagId);

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
