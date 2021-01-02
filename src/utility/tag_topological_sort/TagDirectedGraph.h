/*
 * Copyright 2017-2021 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_UTILITY_TAG_TOPOLOGICAL_SORT_TAG_DIGRAPH_H
#define LIB_QUENTIER_UTILITY_TAG_TOPOLOGICAL_SORT_TAG_DIGRAPH_H

#include <QHash>
#include <QStringList>

namespace quentier {

class Q_DECL_HIDDEN TagDirectedGraph
{
public:
    explicit TagDirectedGraph() = default;

    [[nodiscard]] bool isEmpty() const noexcept;
    [[nodiscard]] bool empty() const noexcept;

    void clear();

    void addChild(const QString & parentTagId, const QString & childTagId);

    [[nodiscard]] QStringList childTagIds(
        const QString & parentTagId) const noexcept;

    [[nodiscard]] QStringList allTagIds() const;

private:
    QHash<QString, QStringList> m_childTagIdsByParentTagId;
};

} // namespace quentier

#endif // LIB_QUENTIER_UTILITY_TAG_TOPOLOGICAL_SORT_TAG_DIGRAPH_H
