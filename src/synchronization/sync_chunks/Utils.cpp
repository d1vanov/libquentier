/*
 * Copyright 2022 Dmitry Ivanov
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

#include <qevercloud/types/SyncChunk.h>

namespace quentier::synchronization::utils {

[[nodiscard]] std::optional<qint32> syncChunkLowUsn(
    const qevercloud::SyncChunk & syncChunk)
{
    std::optional<qint32> lowUsn;

    const auto checkLowUsn = [&](const auto & items) {
        for (const auto & item: qAsConst(items)) {
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
    for (const auto & notebook: qAsConst(*syncChunk.notebooks())) {
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
    for (const auto & linkedNotebook: qAsConst(*syncChunk.linkedNotebooks())) {
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
    for (const auto & note: qAsConst(*syncChunk.notes())) {
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
    for (const auto & resource: qAsConst(*syncChunk.resources())) {
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
    for (const auto & savedSearch: qAsConst(*syncChunk.searches())) {
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
    for (const auto & tag: qAsConst(*syncChunk.tags())) {
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

} // namespace quentier::synchronization::utils
