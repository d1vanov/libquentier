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

#include "TagSortByParentChildRelationsHelpers.hpp"
#include "tag_topological_sort/TagDirectedGraphDepthFirstSearch.h"
#include <quentier/logging/QuentierLogger.h>

namespace quentier {

template <class T>
bool sortTagsByParentChildRelationsImpl(QList<T> & tagList, ErrorString & errorDescription)
{
    if (QuentierIsLogLevelActive(LogLevel::TraceLevel))
    {
        QString log;
        QTextStream strm(&log);
        strm << QStringLiteral("Tags list before performing the topological sort: ");
        for(auto it = tagList.constBegin(), end = tagList.constEnd(); it != end; ++it) {
            strm << *it << QStringLiteral(", ");
        }
        strm.flush();
        QNTRACE(log);
    }

    if (tagList.isEmpty() || (tagList.size() == 1)) {
        // Don't need to process the single item list in any way
        return true;
    }

    // The problem of sorting tags by parent-child relations can be viewed as a problem of performing
    // a topological sort in a directed acyclic graph; for parentless tags we can consider their parent guid
    // to be just an empty string i.e. the parent of parentless tags has empty guid

    TagDirectedGraph graph;
    for(auto it = tagList.constBegin(), end = tagList.constEnd(); it != end; ++it)
    {
        if (tagHasGuid(*it)) {
            QString guid = tagGuid(*it);
            QString parentTagGuid = tagParentGuid(*it);
            graph.addChild(parentTagGuid, guid);
        }
        else if (tagHasLocalUid(*it)) {
            QString localUid = tagLocalUid(*it);
            QString parentTagLocalUid = tagParentLocalUid(*it);
            graph.addChild(parentTagLocalUid, localUid);
        }
        else {
            QNTRACE(QStringLiteral("Skipping tag without either guid or local uid: ") << *it);
        }
    }

    TagDirectedGraphDepthFirstSearch dfs(graph);
    if (Q_UNLIKELY(dfs.hasCycle()))
    {
        errorDescription.setBase(QT_TR_NOOP("Can't synchronize tags: detected cycle of parent-child relations between tags"));
        errorDescription.details() = QStringLiteral("cycled tag guids: ");
        QStack<QString> stack = dfs.cycle();
        while(!stack.isEmpty()) {
            errorDescription.details() += stack.pop() + QStringLiteral(", ");
            stack.pop();
        }

        return false;
    }

    QStack<QString> order = dfs.tagIdsInReversePostOrder();
    QList<T> resultList;
    resultList.reserve(tagList.size());
    while(!order.isEmpty())
    {
        QString id = order.pop();
        if (id.isEmpty()) {
            continue;
        }

        auto it = std::find_if(tagList.begin(), tagList.end(), CompareItemByGuidOrLocalUid<T>(id));
        if (Q_UNLIKELY(it == tagList.end()))
        {
            errorDescription.setBase(QT_TR_NOOP("Can't synchronize tags: internal error while sorting tags "
                                                "by parent-child relations"));

            QString details;
            QTextStream strm(&details);

            strm << QStringLiteral("original tags: ");
            for(auto iit = tagList.constBegin(), iend = tagList.constEnd(); iit != iend; ++iit) {
                strm << QStringLiteral("guid = ") << tagGuid(*iit) << QStringLiteral(", parent guid = ")
                     << tagParentGuid(*iit) << QStringLiteral(", local uid = ") << tagLocalUid(*iit)
                     << QStringLiteral(", parent local uid = ") << tagParentLocalUid(*iit)
                     << QStringLiteral(", ");
            }

            strm << QStringLiteral("tag guid (or local uid) from ordered set not found within the original list: ") << *it
                 << QStringLiteral(", other guids (or local uids) from ordered set: ");
            int counter = 0;
            while(!order.isEmpty()) {
                strm << order.pop();
                if (counter > 0) {
                    strm << QStringLiteral(", ");
                }
                ++counter;
            }

            strm.flush();
            QNWARNING(details);
            return false;
        }

        resultList << *it;
        Q_UNUSED(tagList.erase(it))
    }

    tagList = resultList;

    if (QuentierIsLogLevelActive(LogLevel::TraceLevel))
    {
        QString log;
        QTextStream strm(&log);
        strm << QStringLiteral("Tags list after performing the topological sort: ");
        for(auto it = tagList.constBegin(), end = tagList.constEnd(); it != end; ++it) {
            strm << *it << QStringLiteral(", ");
        }
        strm.flush();
        QNTRACE(log);
    }

    return true;
}

bool sortTagsByParentChildRelations(QList<qevercloud::Tag> & tagList, ErrorString & errorDescription)
{
    return sortTagsByParentChildRelationsImpl(tagList, errorDescription);
}

bool sortTagsByParentChildRelations(QList<Tag> & tagList, ErrorString errorDescription)
{
    return sortTagsByParentChildRelationsImpl(tagList, errorDescription);
}

}
