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

#include <quentier/logging/QuentierLogger.h>
#include <quentier/types/ErrorString.h>
#include <quentier/utility/TagSortByParentChildRelations.h>

#include <qevercloud/generated/types/Tag.h>

#include "tag_topological_sort/TagDirectedGraphDepthFirstSearch.h"

namespace quentier {

bool sortTagsByParentChildRelations(
    QList<qevercloud::Tag> & tagList, ErrorString & errorDescription)
{
    if (QuentierIsLogLevelActive(LogLevel::Trace)) {
        QString log;
        QTextStream strm(&log);
        strm << "Tags list before performing the topological sort: ";

        for (const auto & tag: ::qAsConst(tagList)) {
            strm << tag << ", ";
        }
        strm.flush();
        QNTRACE("utility:tar_sort", log);
    }

    if (tagList.isEmpty() || (tagList.size() == 1)) {
        // Don't need to process the single item list in any way
        return true;
    }

    // The problem of sorting tags by parent-child relations can be viewed as
    // a problem of performing a topological sort in a directed acyclic graph;
    // for parentless tags we can consider their parent guid to be just an empty
    // string i.e. the parent of parentless tags has empty guid

    bool allTagsHaveGuids = true;

    for (const auto & tag: ::qAsConst(tagList)) {
        if (!tag.guid()) {
            allTagsHaveGuids = false;
            QNDEBUG(
                "utility:tar_sort",
                "Not all tags have guids, won't use "
                    << "guids to track parent-child relations");
            break;
        }
    }

    if (!allTagsHaveGuids) {
        bool allTagsHaveLocalUids = true;

        for (const auto & tag: ::qAsConst(tagList)) {
            if (tag.localId().isEmpty()) {
                allTagsHaveLocalUids = false;
                break;
            }
        }

        if (!allTagsHaveLocalUids) {
            errorDescription.setBase(QT_TRANSLATE_NOOP(
                "sortTagsByParentChildRelationsImpl",
                "Can't synchronize tags: all tags must have "
                "either guids or local uids to be sorted by "
                "parent-child relations"));

            return false;
        }
    }

    TagDirectedGraph graph;

    for (const auto & tag: ::qAsConst(tagList)) {
        if (allTagsHaveGuids && tag.guid()) {
            const QString & guid = *tag.guid();
            const QString parentTagGuid =
                tag.parentGuid() ? *tag.parentGuid() : QString();
            graph.addChild(parentTagGuid, guid);
        }
        else if (!tag.localId().isEmpty()) {
            const QString localId = tag.localId();
            const QString parentTagLocalId = tag.parentLocalId();
            QNTRACE(
                "utility:tar_sort",
                "Adding tag local id " << localId << " and tag parent local id "
                                       << parentTagLocalId << " to the graph");
            graph.addChild(parentTagLocalId, localId);
        }
        else {
            QNTRACE(
                "utility:tar_sort",
                "Skipping tag without either guid or local id: " << tag);
        }
    }

    TagDirectedGraphDepthFirstSearch dfs(graph);
    if (Q_UNLIKELY(dfs.hasCycle())) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "sortTagsByParentChildRelationsImpl",
            "Can't synchronize tags: detected cycle of "
            "parent-child relations between tags"));

        errorDescription.details() = QStringLiteral("cycled tag guids: ");
        auto stack = dfs.cycle();
        while (!stack.isEmpty()) {
            errorDescription.details() += stack.pop() + QStringLiteral(", ");
            stack.pop();
        }

        return false;
    }

    QStack<QString> order = dfs.tagIdsInReversePostOrder();
    QList<qevercloud::Tag> resultList;
    resultList.reserve(tagList.size());
    while (!order.isEmpty()) {
        QString id = order.pop();
        if (id.isEmpty()) {
            continue;
        }

        auto it = tagList.end();
        if (allTagsHaveGuids) {
            it = std::find_if(
                tagList.begin(), tagList.end(),
                [&id](const qevercloud::Tag & tag) {
                    return *tag.guid() == id;
                });
        }
        else {
            it = std::find_if(
                tagList.begin(), tagList.end(),
                [&id](const qevercloud::Tag & tag) {
                    return tag.localId() == id;
                });
        }

        if (Q_UNLIKELY(it == tagList.end())) {
            QNDEBUG(
                "utility:tar_sort",
                "Skipping the tag guid or local id not found within "
                    << "the original set (probably the guid of some parent tag "
                    << "not present within the sorted subset): " << id);
            continue;
        }

        resultList << *it;
        Q_UNUSED(tagList.erase(it))
    }

    tagList = resultList;

    if (QuentierIsLogLevelActive(LogLevel::Trace)) {
        QString log;
        QTextStream strm(&log);
        strm << "Tags list after performing the topological sort: ";

        for (const auto & tag: ::qAsConst(tagList)) {
            strm << tag << "\n";
        }
        strm.flush();
        QNTRACE("utility:tar_sort", log);
    }

    return true;
}

} // namespace quentier
