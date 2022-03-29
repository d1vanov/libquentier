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

#include "NotesProcessor.h"
#include "Utils.h"

#include <synchronization/SyncChunksDataCounters.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/local_storage/ILocalStorage.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/threading/Future.h>
#include <quentier/threading/TrackedTask.h>

#include <qevercloud/services/INoteStore.h>
#include <qevercloud/types/SyncChunk.h>

#include <algorithm>

namespace quentier::synchronization {

namespace {

[[nodiscard]] QList<qevercloud::Note> collectNotes(
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
                "synchronization::NotesProcessor",
                "Detected note without guid, skippint it: " << note);
            continue;
        }

        if (Q_UNLIKELY(!note.updateSequenceNum())) {
            QNWARNING(
                "synchronization::NotesProcessor",
                "Detected note without update sequence number, skipping it: "
                    << note);
            continue;
        }

        if (Q_UNLIKELY(!note.notebookGuid())) {
            QNWARNING(
                "synchronization::NotesProcessor",
                "Detected note without notebook guid, skipping it: " << note);
            continue;
        }

        notes << note;
    }

    return notes;
}

[[nodiscard]] QList<qevercloud::Guid> collectExpungedNoteGuids(
    const qevercloud::SyncChunk & syncChunk)
{
    return syncChunk.expungedNotes().value_or(QList<qevercloud::Guid>{});
}

enum class ProcessNoteStatus
{
    AddedNote,
    UpdatedNote
};

} // namespace

NotesProcessor::NotesProcessor(
    local_storage::ILocalStoragePtr localStorage,
    ISyncConflictResolverPtr syncConflictResolver,
    qevercloud::INoteStorePtr noteStore) :
    m_localStorage{std::move(localStorage)},
    m_syncConflictResolver{std::move(syncConflictResolver)},
    m_noteStore{std::move(noteStore)}
{
    if (Q_UNLIKELY(!m_localStorage)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::NotesProcessor",
            "NotesProcessor ctor: local storage is null")}};
    }

    if (Q_UNLIKELY(!m_syncConflictResolver)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::NotesProcessor",
            "NotesProcessor ctor: sync conflict resolver is null")}};
    }

    if (Q_UNLIKELY(!m_noteStore)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::NotesProcessor",
            "NotesProcessor ctor: note store is null")}};
    }
}

QFuture<INotesProcessor::ProcessNotesStatus> NotesProcessor::processNotes(
    const QList<qevercloud::SyncChunk> & syncChunks)
{
    QNDEBUG("synchronization::NotesProcessor", "NotesProcessor::processNotes");

    QList<qevercloud::Note> notes;
    QList<qevercloud::Guid> expungedNotes;
    for (const auto & syncChunk: qAsConst(syncChunks)) {
        notes << collectNotes(syncChunk);
        expungedNotes << collectExpungedNoteGuids(syncChunk);
    }

    utils::filterOutExpungedItems(expungedNotes, notes);

    if (notes.isEmpty() && expungedNotes.isEmpty()) {
        QNDEBUG(
            "synchronization::NotesProcessor",
            "No new/updated/expunged notes in the sync chunks");

        return threading::makeReadyFuture<ProcessNotesStatus>({});
    }

    const int noteCount = notes.size();
    const int expungedNoteCount = expungedNotes.size();
    const int totalItemCount = noteCount + expungedNoteCount;

    const auto selfWeak = weak_from_this();
    
    QList<QFuture<ProcessNoteStatus>> noteFutures;
    noteFutures.reserve(noteCount);

    using FetchNoteOptions = local_storage::ILocalStorage::FetchNoteOptions;
    using FetchNoteOption = local_storage::ILocalStorage::FetchNoteOption;

    for (const auto & note: qAsConst(notes)) {
        auto notePromise = std::make_shared<QPromise<ProcessNoteStatus>>();
        noteFutures << notePromise->future();
        notePromise->start();

        Q_ASSERT(note.guid());

        auto findNoteByGuidFuture = m_localStorage->findNoteByGuid(
            *note.guid(),
            FetchNoteOptions{} | FetchNoteOption::WithResourceMetadata);

        threading::thenOrFailed(
            std::move(findNoteByGuidFuture), notePromise,
            threading::TrackedTask{
                selfWeak,
                [this, updatedNote = note, notePromise](
                    const std::optional<qevercloud::Note> & note) {
                    // TODO: continue from here
                }});
    }


}

} // namespace quentier::synchronization
