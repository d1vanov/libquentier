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

#include <QFutureWatcher>
#include <QMutex>
#include <QMutexLocker>

#include <algorithm>
#include <cmath>

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
    Q_ASSERT(totalItemCount > 0);

    const auto selfWeak = weak_from_this();

    QList<QFuture<ProcessNoteStatus>> noteFutures;
    noteFutures.reserve(noteCount);

    using FetchNoteOptions = local_storage::ILocalStorage::FetchNoteOptions;
    using FetchNoteOption = local_storage::ILocalStorage::FetchNoteOption;

    auto status = std::make_shared<ProcessNotesStatus>();
    status->m_totalExpungedNotes = expungedNotes.size();

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
                [this, updatedNote = note, notePromise, status](
                    const std::optional<qevercloud::Note> & note) mutable {
                    if (note) {
                        ++status->m_totalUpdatedNotes;
                        onFoundDuplicate(
                            notePromise, std::move(updatedNote), *note);
                        return;
                    }

                    ++status->m_totalNewNotes;
                    // No duplicate by guid was found, will download full note
                    // data and then put it into the local storage
                    // TODO: continue from here
                }});
    }

    auto processNotesFuture =
        threading::whenAll<ProcessNoteStatus>(std::move(noteFutures));

    QList<QFuture<void>> expungedNoteFutures;
    expungedNoteFutures.reserve(noteCount);
    for (const auto & guid: qAsConst(expungedNotes)) {
        expungedNoteFutures << m_localStorage->expungeNoteByGuid(guid);
    }

    auto expungeNotesFuture =
        threading::whenAll(std::move(expungedNoteFutures));

    const auto promise = std::make_shared<QPromise<ProcessNotesStatus>>();
    auto future = promise->future();
    promise->start();

    promise->setProgressRange(0, 100);
    promise->setProgressValue(0);

    auto promiseProgressMutex = std::make_shared<QMutex>();

    // Need to map progress values from two different futures of different types
    // into the single promise

    // TODO: refactor this whole stuff - move to a separate method or something

    auto processNotesFutureProgress = std::make_shared<double>(0.0);
    auto expungeNotesFutureProgress = std::make_shared<double>(0.0);

    // Start with the future representing the processing of new/updated notes
    const int maxProcessNotesFutureProgress =
        processNotesFuture.progressMaximum();

    const int minProcessNotesFutureProgress =
        processNotesFuture.progressMinimum();

    Q_ASSERT(maxProcessNotesFutureProgress >= 0);
    Q_ASSERT(minProcessNotesFutureProgress >= 0);
    Q_ASSERT(maxProcessNotesFutureProgress >= minProcessNotesFutureProgress);

    const int processNotesFutureProgressRange =
        maxProcessNotesFutureProgress - minProcessNotesFutureProgress;

    auto processNotesFutureWatcher =
        threading::makeFutureWatcher<QList<ProcessNoteStatus>>();

    processNotesFutureWatcher->setFuture(processNotesFuture);
    auto * rawProcessNotesFutureWatcher = processNotesFutureWatcher.get();

    QObject::connect(
        rawProcessNotesFutureWatcher,
        &QFutureWatcher<QList<ProcessNoteStatus>>::progressValueChanged,
        rawProcessNotesFutureWatcher,
        [totalItemCount, noteCount, expungedNoteCount,
         processNotesFutureProgressRange, promise, promiseProgressMutex,
         processNotesFutureProgress, expungeNotesFutureProgress](
             int progressValue) {
             // Convert progressValue into percentage
             *processNotesFutureProgress = [&] {
                 if (processNotesFutureProgressRange == 0) {
                     return 0.0;
                 }

                 return std::clamp(
                     static_cast<double>(progressValue) /
                         processNotesFutureProgressRange,
                     0.0, 1.0);
             }();

             const QMutexLocker lock{promiseProgressMutex.get()};

             const auto newProgress = *processNotesFutureProgress * noteCount /
                     static_cast<double>(totalItemCount) +
                 *expungeNotesFutureProgress * expungedNoteCount /
                     static_cast<double>(totalItemCount);

             promise->setProgressValue(std::max(
                 promise->future().progressValue(),
                 std::clamp(
                     static_cast<int>(std::round(newProgress * 100.0)), 0,
                     100)));
         });

    // Now handle the future representing the expunging of expunged notes
    const int maxExpungeNotesFutureProgress =
        expungeNotesFuture.progressMaximum();

    const int minExpungeNotesFutureProgress =
        expungeNotesFuture.progressMinimum();

    Q_ASSERT(maxExpungeNotesFutureProgress >= 0);
    Q_ASSERT(minExpungeNotesFutureProgress >= 0);
    Q_ASSERT(maxExpungeNotesFutureProgress >= minExpungeNotesFutureProgress);

    const int expungeNotesFutureProgressRange =
        maxExpungeNotesFutureProgress - minExpungeNotesFutureProgress;

    auto expungeNotesFutureWatcher = threading::makeFutureWatcher<void>();
    expungeNotesFutureWatcher->setFuture(expungeNotesFuture);
    auto * rawExpungeNotesFutureWatcher = expungeNotesFutureWatcher.get();

    QObject::connect(
        rawExpungeNotesFutureWatcher,
        &QFutureWatcher<void>::progressValueChanged,
        rawExpungeNotesFutureWatcher,
        [totalItemCount, noteCount, expungedNoteCount,
         expungeNotesFutureProgressRange, promise, promiseProgressMutex,
         processNotesFutureProgress, expungeNotesFutureProgress](int progressValue) {
             // Convert progressValue into percentage
             *expungeNotesFutureProgress = [&] {
                 if (expungeNotesFutureProgressRange == 0) {
                     return 0.0;
                 }

                 return std::clamp(
                     static_cast<double>(progressValue) /
                         expungeNotesFutureProgressRange,
                     0.0, 1.0);
             }();

             const QMutexLocker lock{promiseProgressMutex.get()};

             const auto newProgress = *processNotesFutureProgress * noteCount /
                     static_cast<double>(totalItemCount) +
                 *expungeNotesFutureProgress * expungedNoteCount /
                     static_cast<double>(totalItemCount);

             promise->setProgressValue(std::max(
                 promise->future().progressValue(),
                 std::clamp(
                     static_cast<int>(std::round(newProgress * 100.0)), 0,
                     100)));
         });

    // TODO: watch the progress of expungedNotes similarly
    // TODO: set up handling of finalization and errors of both futures

    return future;
}

} // namespace quentier::synchronization
