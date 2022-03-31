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
#include <QPointer>

#include <algorithm>
#include <cmath>
#include <type_traits>

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

// Maps progress values from two futures into the progress for the separate
// progress
template <class T, class U, class V>
void mapProgress(
    QFuture<T> firstFuture, QFuture<U> secondFuture,
    std::shared_ptr<QPromise<V>> promise)
{
    Q_ASSERT(firstFuture.progressMaximum() >= 0);
    Q_ASSERT(firstFuture.progressMinimum() >= 0);
    Q_ASSERT(firstFuture.progressMaximum() >= firstFuture.progressMinimum());

    Q_ASSERT(secondFuture.progressMaximum() >= 0);
    Q_ASSERT(secondFuture.progressMinimum() >= 0);
    Q_ASSERT(secondFuture.progressMaximum() >= secondFuture.progressMinimum());

    const int firstFutureProgressRange =
        firstFuture.progressMaximum() - firstFuture.progressMinimum();

    const int secondFutureProgressRange =
        secondFuture.progressMaximum() - secondFuture.progressMinimum();

    promise->setProgressRange(0, 100);
    promise->setProgressValue(0);

    auto promiseProgressMutex = std::make_shared<QMutex>();

    const auto computePromiseProgress = [promiseProgressMutex](
        const int currentFutureProgressRange,
        const int currentFutureProgress, // NOLINT
        const int otherFutureProgressRange,
        const std::shared_ptr<double> & currentFutureProgressPercentage,
        const std::shared_ptr<double> & otherFutureProgressPercentage)
    {
        Q_ASSERT(currentFutureProgressRange);
        Q_ASSERT(otherFutureProgressRange);

        // Convert current future progress into a percentage
        *currentFutureProgressPercentage = [&] {
            if (currentFutureProgressRange == 0) {
                return 0.0;
            }

            return std::clamp(
                static_cast<double>(currentFutureProgress) /
                    currentFutureProgressRange,
                0.0, 1.0);
        }();

        const QMutexLocker lock{promiseProgressMutex.get()};

        const double newProgress = [&] {
            if (currentFutureProgressRange == 0 &&
                otherFutureProgressRange == 0) {
                return 0.0;
            }

            return (*currentFutureProgressPercentage *
                        currentFutureProgressRange +
                    *otherFutureProgressPercentage * otherFutureProgressRange) /
                (currentFutureProgressRange + otherFutureProgressRange);
        }();

        return std::clamp(
            static_cast<int>(std::round(newProgress * 100.0)), 0, 100);
    };

    auto firstFutureProgressPercentage = std::make_shared<double>(0.0);
    auto secondFutureProgressPercentage = std::make_shared<double>(0.0);

    auto firstFutureWatcher = std::make_unique<QFutureWatcher<T>>();
    firstFutureWatcher->setFuture(firstFuture);

    QObject::connect(
        firstFutureWatcher.get(), &QFutureWatcher<T>::progressValueChanged,
        firstFutureWatcher.get(),
        [firstFutureProgressPercentage, secondFutureProgressPercentage,
         firstFutureProgressRange, secondFutureProgressRange, promise,
         promiseProgressMutex, computePromiseProgress](int progressValue) {
            const int newProgress = computePromiseProgress(
                firstFutureProgressRange, progressValue,
                secondFutureProgressRange, firstFutureProgressPercentage,
                secondFutureProgressPercentage);

            promise->setProgressValue(
                std::max(promise->future().progressValue(), newProgress));
        });

    QObject::connect(
        firstFutureWatcher.get(), &QFutureWatcher<T>::finished,
        firstFutureWatcher.get(),
        [firstFutureWatcher =
             QPointer<QFutureWatcher<T>>(firstFutureWatcher.get())] {
            if (!firstFutureWatcher.isNull()) {
                firstFutureWatcher->deleteLater();
            }
        });

    QObject::connect(
        firstFutureWatcher.get(), &QFutureWatcher<T>::canceled,
        firstFutureWatcher.get(),
        [firstFutureWatcher =
             QPointer<QFutureWatcher<T>>(firstFutureWatcher.get())] {
            if (!firstFutureWatcher.isNull()) {
                firstFutureWatcher->deleteLater();
            }
        });

    Q_UNUSED(firstFutureWatcher.release())

    auto secondFutureWatcher = std::make_unique<QFutureWatcher<U>>();
    secondFutureWatcher->setFuture(secondFuture);

    QObject::connect(
        secondFutureWatcher.get(), &QFutureWatcher<U>::progressValueChanged,
        secondFutureWatcher.get(),
        [firstFutureProgressPercentage, secondFutureProgressPercentage,
         firstFutureProgressRange, secondFutureProgressRange, promise,
         promiseProgressMutex, computePromiseProgress](int progressValue) {
            const int newProgress = computePromiseProgress(
                secondFutureProgressRange, progressValue,
                firstFutureProgressRange, secondFutureProgressPercentage,
                firstFutureProgressPercentage);

            promise->setProgressValue(
                std::max(promise->future().progressValue(), newProgress));
         });

    QObject::connect(
        secondFutureWatcher.get(), &QFutureWatcher<U>::finished,
        secondFutureWatcher.get(),
        [secondFutureWatcher =
             QPointer<QFutureWatcher<U>>(secondFutureWatcher.get())] {
            if (!secondFutureWatcher.isNull()) {
                secondFutureWatcher->deleteLater();
            }
        });

    QObject::connect(
        secondFutureWatcher.get(), &QFutureWatcher<U>::canceled,
        secondFutureWatcher.get(),
        [secondFutureWatcher =
             QPointer<QFutureWatcher<U>>(secondFutureWatcher.get())] {
            if (!secondFutureWatcher.isNull()) {
                secondFutureWatcher->deleteLater();
            }
        });

    Q_UNUSED(secondFutureWatcher.release());
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

    mapProgress(processNotesFuture, expungeNotesFuture, promise);

    // TODO: set up handling of finalization and errors of both futures

    return future;
}

} // namespace quentier::synchronization
