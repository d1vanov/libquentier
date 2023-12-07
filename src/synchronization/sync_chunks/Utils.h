/*
 * Copyright 2022-2023 Dmitry Ivanov
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

#include <qevercloud/types/Fwd.h>
#include <qevercloud/types/LinkedNotebook.h>
#include <qevercloud/types/Note.h>
#include <qevercloud/types/Notebook.h>
#include <qevercloud/types/TypeAliases.h>

#include <QtGlobal>

#include <optional>

namespace quentier::synchronization::utils {

[[nodiscard]] std::optional<qint32> syncChunkLowUsn(
    const qevercloud::SyncChunk & syncChunk);

void setLinkedNotebookGuidToSyncChunkEntries(
    const qevercloud::Guid & linkedNotebookGuid,
    qevercloud::SyncChunk & syncChunk);

[[nodiscard]] QList<qevercloud::Notebook> collectNotebooksFromSyncChunk(
    const qevercloud::SyncChunk & syncChunk);

[[nodiscard]] QList<qevercloud::Guid> collectExpungedNotebookGuidsFromSyncChunk(
    const qevercloud::SyncChunk & syncChunk);

[[nodiscard]] QList<qevercloud::LinkedNotebook>
    collectLinkedNotebooksFromSyncChunk(
        const qevercloud::SyncChunk & syncChunk);

[[nodiscard]] QList<qevercloud::Guid>
    collectExpungedLinkedNotebookGuidsFromSyncChunk(
        const qevercloud::SyncChunk & syncChunk);

[[nodiscard]] QList<qevercloud::Note> collectNotesFromSyncChunk(
    const qevercloud::SyncChunk & syncChunk);

[[nodiscard]] QList<qevercloud::Guid> collectExpungedNoteGuidsFromSyncChunk(
    const qevercloud::SyncChunk & syncChunk);

[[nodiscard]] QList<qevercloud::Resource> collectResourcesFromSyncChunk(
    const qevercloud::SyncChunk & syncChunk);

[[nodiscard]] QList<qevercloud::SavedSearch> collectSavedSearchesFromSyncChunk(
    const qevercloud::SyncChunk & syncChunk);

[[nodiscard]] QList<qevercloud::Guid>
    collectExpungedSavedSearchGuidsFromSyncChunk(
        const qevercloud::SyncChunk & syncChunk);

[[nodiscard]] QList<qevercloud::Tag> collectTagsFromSyncChunk(
    const qevercloud::SyncChunk & syncChunk);

[[nodiscard]] QList<qevercloud::Guid> collectExpungedTagGuidsFromSyncChunk(
    const qevercloud::SyncChunk & syncChunk);

[[nodiscard]] QString briefSyncChunkInfo(
    const qevercloud::SyncChunk & syncChunk);

} // namespace quentier::synchronization::utils
