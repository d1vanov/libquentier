/*
 * Copyright 2022-2025 Dmitry Ivanov
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

#include "Utils.h"

#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/DateTime.h>

#include <qevercloud/types/SyncChunk.h>

#include <QTextStream>

#include <utility>

namespace quentier::synchronization::utils {

[[nodiscard]] std::optional<qint32> syncChunkLowUsn(
    const qevercloud::SyncChunk & syncChunk)
{
    std::optional<qint32> lowUsn;

    const auto checkLowUsn = [&](const auto & items) {
        for (const auto & item: std::as_const(items)) {
            if (item.updateSequenceNum() &&
                (!lowUsn || *lowUsn > *item.updateSequenceNum()))
            {
                lowUsn = *item.updateSequenceNum();
            }
        }
    };

    if (syncChunk.notes()) {
        checkLowUsn(*syncChunk.notes());
    }

    if (syncChunk.notebooks()) {
        checkLowUsn(*syncChunk.notebooks());
    }

    if (syncChunk.tags()) {
        checkLowUsn(*syncChunk.tags());
    }

    if (syncChunk.searches()) {
        checkLowUsn(*syncChunk.searches());
    }

    if (syncChunk.resources()) {
        checkLowUsn(*syncChunk.resources());
    }

    if (syncChunk.linkedNotebooks()) {
        checkLowUsn(*syncChunk.linkedNotebooks());
    }

    return lowUsn;
}

void setLinkedNotebookGuidToSyncChunkEntries(
    const qevercloud::Guid & linkedNotebookGuid,
    qevercloud::SyncChunk & syncChunk)
{
    if (syncChunk.notebooks()) {
        for (auto & notebook: *syncChunk.mutableNotebooks()) {
            notebook.setLinkedNotebookGuid(linkedNotebookGuid);
        }
    }

    if (syncChunk.tags()) {
        for (auto & tag: *syncChunk.mutableTags()) {
            tag.setLinkedNotebookGuid(linkedNotebookGuid);
        }
    }
}

QList<qevercloud::Notebook> collectNotebooksFromSyncChunk(
    const qevercloud::SyncChunk & syncChunk)
{
    if (!syncChunk.notebooks() || syncChunk.notebooks()->isEmpty()) {
        return {};
    }

    QList<qevercloud::Notebook> notebooks;
    notebooks.reserve(syncChunk.notebooks()->size());
    for (const auto & notebook: std::as_const(*syncChunk.notebooks())) {
        if (Q_UNLIKELY(!notebook.guid())) {
            QNWARNING(
                "synchronization::utils",
                "Detected notebook without guid, skipping it: " << notebook);
            continue;
        }

        if (Q_UNLIKELY(!notebook.updateSequenceNum())) {
            QNWARNING(
                "synchronization::utils",
                "Detected notebook without update sequence number, "
                    << "skipping it: " << notebook);
            continue;
        }

        if (Q_UNLIKELY(!notebook.name())) {
            QNWARNING(
                "synchronization::utils",
                "Detected notebook without name, skipping it: " << notebook);
            continue;
        }

        notebooks << notebook;
    }

    return notebooks;
}

QList<qevercloud::Guid> collectExpungedNotebookGuidsFromSyncChunk(
    const qevercloud::SyncChunk & syncChunk)
{
    return syncChunk.expungedNotebooks().value_or(QList<qevercloud::Guid>{});
}

QList<qevercloud::LinkedNotebook> collectLinkedNotebooksFromSyncChunk(
    const qevercloud::SyncChunk & syncChunk)
{
    if (!syncChunk.linkedNotebooks() || syncChunk.linkedNotebooks()->isEmpty())
    {
        return {};
    }

    QList<qevercloud::LinkedNotebook> linkedNotebooks;
    linkedNotebooks.reserve(syncChunk.linkedNotebooks()->size());
    for (const auto & linkedNotebook:
         std::as_const(*syncChunk.linkedNotebooks()))
    {
        if (Q_UNLIKELY(!linkedNotebook.guid())) {
            QNWARNING(
                "synchronization::utils",
                "Detected linked notebook without guid, skipping it: "
                    << linkedNotebook);
            continue;
        }

        if (Q_UNLIKELY(!linkedNotebook.updateSequenceNum())) {
            QNWARNING(
                "synchronization::utils",
                "Detected linked notebook without update sequence number, "
                    << "skipping it: " << linkedNotebook);
            continue;
        }

        linkedNotebooks << linkedNotebook;
    }

    return linkedNotebooks;
}

QList<qevercloud::Guid> collectExpungedLinkedNotebookGuidsFromSyncChunk(
    const qevercloud::SyncChunk & syncChunk)
{
    return syncChunk.expungedLinkedNotebooks().value_or(
        QList<qevercloud::Guid>{});
}

QList<qevercloud::Note> collectNotesFromSyncChunk(
    const qevercloud::SyncChunk & syncChunk)
{
    if (!syncChunk.notes() || syncChunk.notes()->isEmpty()) {
        return {};
    }

    QList<qevercloud::Note> notes;
    notes.reserve(syncChunk.notes()->size());
    for (const auto & note: std::as_const(*syncChunk.notes())) {
        if (Q_UNLIKELY(!note.guid())) {
            QNWARNING(
                "synchronization::utils",
                "Detected note without guid, skipping it: " << note);
            continue;
        }

        if (Q_UNLIKELY(!note.updateSequenceNum())) {
            QNWARNING(
                "synchronization::utils",
                "Detected note without update sequence number, skipping it: "
                    << note);
            continue;
        }

        if (Q_UNLIKELY(!note.notebookGuid())) {
            QNWARNING(
                "synchronization::utils",
                "Detected note without notebook guid, skipping it: " << note);
            continue;
        }

        notes << note;
    }

    return notes;
}

QList<qevercloud::Guid> collectExpungedNoteGuidsFromSyncChunk(
    const qevercloud::SyncChunk & syncChunk)
{
    return syncChunk.expungedNotes().value_or(QList<qevercloud::Guid>{});
}

QList<qevercloud::Resource> collectResourcesFromSyncChunk(
    const qevercloud::SyncChunk & syncChunk)
{
    if (!syncChunk.resources() || syncChunk.resources()->isEmpty()) {
        return {};
    }

    QList<qevercloud::Resource> resources;
    resources.reserve(syncChunk.resources()->size());
    for (const auto & resource: std::as_const(*syncChunk.resources())) {
        if (Q_UNLIKELY(!resource.guid())) {
            QNWARNING(
                "synchronization::utils",
                "Detected resource without guid, skipping it: " << resource);
            continue;
        }

        if (Q_UNLIKELY(!resource.updateSequenceNum())) {
            QNWARNING(
                "synchronization::utils",
                "Detected resource without update sequence number, skipping "
                    << "it: " << resource);
            continue;
        }

        if (Q_UNLIKELY(!resource.noteGuid())) {
            QNWARNING(
                "synchronization::utils",
                "Detected resource without note guid, skipping it: "
                    << resource);
            continue;
        }

        resources << resource;
    }

    return resources;
}

QList<qevercloud::SavedSearch> collectSavedSearchesFromSyncChunk(
    const qevercloud::SyncChunk & syncChunk)
{
    if (!syncChunk.searches() || syncChunk.searches()->isEmpty()) {
        return {};
    }

    QList<qevercloud::SavedSearch> savedSearches;
    savedSearches.reserve(syncChunk.searches()->size());
    for (const auto & savedSearch: std::as_const(*syncChunk.searches())) {
        if (Q_UNLIKELY(!savedSearch.guid())) {
            QNWARNING(
                "synchronization::utils",
                "Detected saved search without guid, skipping it: "
                    << savedSearch);
            continue;
        }

        if (Q_UNLIKELY(!savedSearch.updateSequenceNum())) {
            QNWARNING(
                "synchronization::utils",
                "Detected saved search without update sequence number, "
                    << "skipping it: " << savedSearch);
            continue;
        }

        if (Q_UNLIKELY(!savedSearch.name())) {
            QNWARNING(
                "synchronization::utils",
                "Detected saved search without name, skipping it: "
                    << savedSearch);
            continue;
        }

        savedSearches << savedSearch;
    }

    return savedSearches;
}

QList<qevercloud::Guid> collectExpungedSavedSearchGuidsFromSyncChunk(
    const qevercloud::SyncChunk & syncChunk)
{
    return syncChunk.expungedSearches().value_or(QList<qevercloud::Guid>{});
}

QList<qevercloud::Tag> collectTagsFromSyncChunk(
    const qevercloud::SyncChunk & syncChunk)
{
    if (!syncChunk.tags() || syncChunk.tags()->isEmpty()) {
        return {};
    }

    QList<qevercloud::Tag> tags;
    tags.reserve(syncChunk.tags()->size());
    for (const auto & tag: std::as_const(*syncChunk.tags())) {
        if (Q_UNLIKELY(!tag.guid())) {
            QNWARNING(
                "synchronization::utils",
                "Detected tag without guid, skipping it: " << tag);
            continue;
        }

        if (Q_UNLIKELY(!tag.updateSequenceNum())) {
            QNWARNING(
                "synchronization::utils",
                "Detected tag without update sequence number, skipping it: "
                    << tag);
            continue;
        }

        if (Q_UNLIKELY(!tag.name())) {
            QNWARNING(
                "synchronization::utils",
                "Detected tag without name, skipping it: " << tag);
            continue;
        }

        tags << tag;
    }

    return tags;
}

QList<qevercloud::Guid> collectExpungedTagGuidsFromSyncChunk(
    const qevercloud::SyncChunk & syncChunk)
{
    return syncChunk.expungedTags().value_or(QList<qevercloud::Guid>{});
}

QString briefSyncChunkInfo(const qevercloud::SyncChunk & syncChunk)
{
    QString res;
    QTextStream strm{&res};

    strm << "Current time = "
         << utility::printableDateTimeFromTimestamp(syncChunk.currentTime())
         << " (" << syncChunk.currentTime() << "), chunk high USN = "
         << (syncChunk.chunkHighUSN()
                 ? QString::number(*syncChunk.chunkHighUSN())
                 : QStringLiteral("<none>"))
         << ", update count = " << syncChunk.updateCount() << "\n";

    const auto printContainer = [&strm](const auto & container) {
        for (const auto & item: std::as_const(container)) {
            strm << "    [" << item.guid().value_or(QStringLiteral("<unknown>"))
                 << ": " << item.updateSequenceNum().value_or(-1) << "]\n";
        }
    };

    if (syncChunk.notes() && !syncChunk.notes()->isEmpty()) {
        strm << "Notes (" << syncChunk.notes()->size() << "):\n";
        printContainer(*syncChunk.notes());
    }

    if (syncChunk.notebooks() && !syncChunk.notebooks()->isEmpty()) {
        strm << "Notebooks(" << syncChunk.notebooks()->size() << "):\n";
        printContainer(*syncChunk.notebooks());
    }

    if (syncChunk.tags() && !syncChunk.tags()->isEmpty()) {
        strm << "Tags (" << syncChunk.tags()->size() << "):\n";
        printContainer(*syncChunk.tags());
    }

    if (syncChunk.searches() && !syncChunk.searches()->isEmpty()) {
        strm << "Saved searches (" << syncChunk.searches()->size() << "):\n";
        printContainer(*syncChunk.searches());
    }

    if (syncChunk.resources() && !syncChunk.resources()->isEmpty()) {
        strm << "Resources (" << syncChunk.resources()->size() << "):\n";
        printContainer(*syncChunk.resources());
    }

    if (syncChunk.linkedNotebooks() && !syncChunk.linkedNotebooks()->isEmpty())
    {
        strm << "Linked notebooks (" << syncChunk.linkedNotebooks()->size()
             << "):\n";
        printContainer(*syncChunk.linkedNotebooks());
    }

    const auto printExpungedContainer = [&strm](const auto & container) {
        for (const auto & item: std::as_const(container)) {
            strm << "    [" << item << "]\n";
        }
    };

    if (syncChunk.expungedNotes() && !syncChunk.expungedNotes()->isEmpty()) {
        strm << "Expunged notes (" << syncChunk.expungedNotes()->size()
             << "):\n";
        printExpungedContainer(*syncChunk.expungedNotes());
    }

    if (syncChunk.expungedNotebooks() &&
        !syncChunk.expungedNotebooks()->isEmpty())
    {
        strm << "Expunged notebooks (" << syncChunk.expungedNotebooks()->size()
             << "):\n";
        printExpungedContainer(*syncChunk.expungedNotebooks());
    }

    if (syncChunk.expungedTags() && !syncChunk.expungedTags()->isEmpty()) {
        strm << "Expunged tags (" << syncChunk.expungedTags()->size() << "):\n";
        printExpungedContainer(*syncChunk.expungedTags());
    }

    if (syncChunk.expungedSearches()) {
        strm << "Expunged saved searches ("
             << syncChunk.expungedSearches()->size() << "):\n";
        printExpungedContainer(*syncChunk.expungedSearches());
    }

    if (syncChunk.expungedLinkedNotebooks() &&
        !syncChunk.expungedLinkedNotebooks()->isEmpty())
    {
        strm << "Expunged linked notebooks ("
             << syncChunk.expungedLinkedNotebooks()->size() << "):\n";
        printExpungedContainer(*syncChunk.expungedLinkedNotebooks());
    }

    return res;
}

QString briefSyncChunksInfo(const QList<qevercloud::SyncChunk> & syncChunks)
{
    QString res;
    QTextStream strm{&res};

    strm << "Sync chunks (" << syncChunks.size() << "):\n";
    for (const auto & syncChunk: std::as_const(syncChunks)) {
        strm << briefSyncChunkInfo(syncChunk) << "\n";
    }

    return res;
}

} // namespace quentier::synchronization::utils
