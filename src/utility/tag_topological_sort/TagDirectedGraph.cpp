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

#include "TagDirectedGraph.h"

namespace quentier {

TagDirectedGraph::TagDirectedGraph() :
    m_childTagGuidsByParentTagGuid()
{}

bool TagDirectedGraph::isEmpty() const
{
    return m_childTagGuidsByParentTagGuid.isEmpty();
}

bool TagDirectedGraph::empty() const
{
    return m_childTagGuidsByParentTagGuid.empty();
}

void TagDirectedGraph::clear()
{
    m_childTagGuidsByParentTagGuid.clear();
}

void TagDirectedGraph::addChild(const QString & parentTagGuid, const QString & childTagGuid)
{
    QStringList & childTagGuids = m_childTagGuidsByParentTagGuid[parentTagGuid];
    if (!childTagGuids.contains(childTagGuid)) {
        childTagGuids << childTagGuid;
    }
}

QStringList TagDirectedGraph::childTagGuids(const QString & parentTagGuid) const
{
    auto it = m_childTagGuidsByParentTagGuid.find(parentTagGuid);
    if (it != m_childTagGuidsByParentTagGuid.end()) {
        return it.value();
    }

    return QStringList();
}

QStringList TagDirectedGraph::allTagGuids() const
{
    QStringList result;

    for(auto it = m_childTagGuidsByParentTagGuid.constBegin(),
        end = m_childTagGuidsByParentTagGuid.constEnd(); it != end; ++it)
    {
        result << it.key();

        const QStringList & childTagGuids = it.value();
        for(auto sit = childTagGuids.constBegin(), send = childTagGuids.constEnd(); sit != send; ++sit) {
            result << *sit;
        }
    }

    Q_UNUSED(result.removeDuplicates())
    return result;
}

} // namespace quentier
