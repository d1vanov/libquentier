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

#include "tag_topological_sort/TagDirectedGraphDepthFirstSearch.h"
#include <quentier/utility/TagSortByParentChildRelations.h>
#include <quentier/logging/QuentierLogger.h>

namespace quentier {

template <class T>
bool tagHasGuid(const T & tag);

template <>
bool tagHasGuid(const qevercloud::Tag & tag)
{
    return tag.guid.isSet();
}

template <>
bool tagHasGuid(const Tag & tag)
{
    return tag.hasGuid();
}

template <class T>
QString tagGuid(const T & tag);

template <>
QString tagGuid(const qevercloud::Tag & tag)
{
    return tag.guid.ref();
}

template <>
QString tagGuid(const Tag & tag)
{
    return tag.guid();
}

template <class T>
QString tagParentGuid(const T & tag);

template <>
QString tagParentGuid(const qevercloud::Tag & tag)
{
    if (tag.parentGuid.isSet()) {
        return tag.parentGuid.ref();
    }

    return QString();
}

template <>
QString tagParentGuid(const Tag & tag)
{
    if (tag.hasParentGuid()) {
        return tag.parentGuid();
    }

    return QString();
}

template <class T>
class CompareItemByGuid
{
public:
    CompareItemByGuid(const QString & guid) :
        m_guid(guid)
    {}

    bool operator()(const T & tag) const
    {
        if (!tagHasGuid(tag)) {
            return false;
        }

        return (m_guid == tagGuid(tag));
    }

private:
    QString     m_guid;
};

template <class T>
bool sortTagsByParentChildRelationsImpl(QList<T> & tagList, ErrorString & errorDescription)
{
    if (QuentierIsLogLevelActive(LogLevel::TraceLevel))
    {
        QTextStream strm;
        strm << QStringLiteral("Tags list after performing the topological sort: ");
        for(auto it = tagList.constBegin(), end = tagList.constEnd(); it != end; ++it) {
            strm << *it << QStringLiteral(", ");
        }
        QNTRACE(strm.readAll());
    }

    // The problem of sorting tags by parent-child relations can be viewed as a problem of performing
    // a topological sort in a directed acyclic graph; for parentless tags we can consider their parent guid
    // to be just an empty string i.e. the parent of parentless tags has empty guid

    TagDirectedGraph graph;
    for(auto it = tagList.constBegin(), end = tagList.constEnd(); it != end; ++it)
    {
        if (!tagHasGuid(*it)) {
            QNDEBUG(QStringLiteral("Skipping tag without guid: ") << *it);
            continue;
        }

        QString guid = tagGuid(*it);
        QString parentTagGuid = tagParentGuid(*it);
        graph.addChild(parentTagGuid, guid);
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

    QStack<QString> order = dfs.tagGuidsInReversePostOrder();
    QList<T> resultList;
    resultList.reserve(tagList.size());
    while(!order.isEmpty())
    {
        QString tagGuid = order.pop();
        if (tagGuid.isEmpty()) {
            continue;
        }

        auto it = std::find_if(tagList.begin(), tagList.end(), CompareItemByGuid<T>(tagGuid));
        if (Q_UNLIKELY(it == tagList.end())) {
            errorDescription.setBase(QT_TR_NOOP("Can't synchronize tags: internal error while sorting tags "
                                                "by parent-child relations"));
            return false;
        }

        resultList << *it;
        Q_UNUSED(tagList.erase(it))
    }

    tagList = resultList;

    if (QuentierIsLogLevelActive(LogLevel::TraceLevel))
    {
        QTextStream strm;
        strm << QStringLiteral("Tags list after performing the topological sort: ");
        for(auto it = tagList.constBegin(), end = tagList.constEnd(); it != end; ++it) {
            strm << *it << QStringLiteral(", ");
        }
        QNTRACE(strm.readAll());
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
