/*
 * Copyright 2022-2024 Dmitry Ivanov
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

#include <synchronization/IFullSyncStaleDataExpunger.h>

#include <QDebug>
#include <QTextStream>

#include <utility>

namespace quentier::synchronization {

namespace {

template <class T>
void printPreservedGuids(
    T & t, const IFullSyncStaleDataExpunger::PreservedGuids & preservedGuids)
{
    const auto printGuids =
        [&t](const QString & typeName, const QSet<qevercloud::Guid> & guids) {
            t << "    " << typeName << " guids (" << guids.size() << "):\n";
            for (const auto & guid: std::as_const(guids)) {
                t << "        [" << guid << "];\n";
            }
        };

    t << "Preserved guids:\n";
    printGuids(QStringLiteral("Notebook"), preservedGuids.notebookGuids);
    printGuids(QStringLiteral("Tag"), preservedGuids.tagGuids);
    printGuids(QStringLiteral("Note"), preservedGuids.noteGuids);
    printGuids(QStringLiteral("Saved search"), preservedGuids.savedSearchGuids);
}

} // namespace

bool operator==(
    const IFullSyncStaleDataExpunger::PreservedGuids & lhs,
    const IFullSyncStaleDataExpunger::PreservedGuids & rhs) noexcept
{
    return lhs.notebookGuids == rhs.notebookGuids &&
        lhs.noteGuids == rhs.noteGuids &&
        lhs.savedSearchGuids == rhs.savedSearchGuids &&
        lhs.tagGuids == rhs.tagGuids;
}

bool operator!=(
    const IFullSyncStaleDataExpunger::PreservedGuids & lhs,
    const IFullSyncStaleDataExpunger::PreservedGuids & rhs) noexcept
{
    return !(lhs == rhs);
}

QTextStream & operator<<(
    QTextStream & strm,
    const IFullSyncStaleDataExpunger::PreservedGuids & preservedGuids)
{
    printPreservedGuids(strm, preservedGuids);
    return strm;
}

QDebug & operator<<(
    QDebug & dbg,
    const IFullSyncStaleDataExpunger::PreservedGuids & preservedGuids)
{
    printPreservedGuids(dbg, preservedGuids);
    return dbg;
}

} // namespace quentier::synchronization
