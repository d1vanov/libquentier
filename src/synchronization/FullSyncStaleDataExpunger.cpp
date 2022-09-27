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

#include "FullSyncStaleDataExpunger.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/local_storage/ILocalStorage.h>
#include <quentier/threading/Future.h>
#include <quentier/threading/TrackedTask.h>
#include <quentier/utility/cancelers/ICanceler.h>

#include <QHash>

namespace quentier::synchronization {

FullSyncStaleDataExpunger::FullSyncStaleDataExpunger(
    local_storage::ILocalStoragePtr localStorage,
    utility::cancelers::ICancelerPtr canceler) :
    m_localStorage{std::move(localStorage)},
    m_canceler{std::move(canceler)}
{
    if (Q_UNLIKELY(!m_localStorage)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::FullSyncStaleDataExpunger",
            "FullSyncStaleDataExpunger ctor: local storage is null")}};
    }

    if (Q_UNLIKELY(!m_canceler)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::FullSyncStaleDataExpunger",
            "FullSyncStaleDataExpunger ctor: canceler is null")}};
    }
}

QFuture<void> FullSyncStaleDataExpunger::expungeStaleData(
    PreservedGuids preservedGuids, // NOLINT
    std::optional<qevercloud::Guid> linkedNotebookGuid) // NOLINT
{
    const local_storage::ILocalStorage::ListGuidsFilters modifiedFilters{
        local_storage::ILocalStorage::ListObjectsFilter::Include, // modified
        std::nullopt, // favorited
    };

    const local_storage::ILocalStorage::ListGuidsFilters unmodifiedFilters{
        local_storage::ILocalStorage::ListObjectsFilter::Exclude, // modified
        std::nullopt, // favorited
    };

    auto listModifiedNotebookGuidsFuture = m_localStorage->listNotebookGuids(
        modifiedFilters, linkedNotebookGuid);

    auto listUnmodifiedNotebookGuidsFuture = m_localStorage->listNotebookGuids(
        unmodifiedFilters, linkedNotebookGuid);

    auto listModifiedTagGuidsFuture = m_localStorage->listTagGuids(
        modifiedFilters, linkedNotebookGuid);

    auto listUnmodifiedTagGuidsFuture = m_localStorage->listTagGuids(
        unmodifiedFilters, linkedNotebookGuid);

    auto listModifiedNoteGuidsFuture = m_localStorage->listNoteGuids(
        modifiedFilters, linkedNotebookGuid);

    auto listUnmodifiedNoteGuidsFuture = m_localStorage->listNoteGuids(
        unmodifiedFilters, linkedNotebookGuid);

    auto listModifiedSavedSearchGuidsFuture =
        (linkedNotebookGuid == std::nullopt
         ? m_localStorage->listSavedSearchGuids(modifiedFilters)
         : threading::makeReadyFuture<QSet<qevercloud::Guid>>({}));

    auto listUnmodifiedSavedSearchGuidsFuture =
        (linkedNotebookGuid == std::nullopt
         ? m_localStorage->listSavedSearchGuids(unmodifiedFilters)
         : threading::makeReadyFuture<QSet<qevercloud::Guid>>({}));

    auto listAllFuture = threading::whenAll<QSet<qevercloud::Guid>>(
        QList<QFuture<QSet<qevercloud::Guid>>>{}
            << listModifiedNotebookGuidsFuture
            << listUnmodifiedNotebookGuidsFuture
            << listModifiedTagGuidsFuture
            << listUnmodifiedTagGuidsFuture
            << listModifiedNoteGuidsFuture
            << listUnmodifiedNoteGuidsFuture
            << listModifiedSavedSearchGuidsFuture
            << listUnmodifiedSavedSearchGuidsFuture);

    auto promise = std::make_shared<QPromise<void>>();
    auto future = promise->future();
    promise->start();

    auto selfWeak = weak_from_this();

    threading::thenOrFailed(
        std::move(listAllFuture), promise,
        threading::TrackedTask{
            selfWeak,
            [this, promise, linkedNotebookGuid = std::move(linkedNotebookGuid),
            preservedGuids = std::move(preservedGuids)](
                QList<QSet<qevercloud::Guid>> result) mutable {
                Q_ASSERT(result.size() == 8);

                Guids guids;
                guids.locallyModifiedNotebookGuids = std::move(result[0]);
                guids.unmodifiedNotebookGuids = std::move(result[1]);
                guids.locallyModifiedTagGuids = std::move(result[2]);
                guids.unmodifiedTagGuids = std::move(result[3]);
                guids.locallyModifiedNoteGuids = std::move(result[4]);
                guids.unmodifiedNoteGuids = std::move(result[5]);
                guids.locallyModifiedSavedSearchGuids = std::move(result[6]);
                guids.unmodifiedSavedSearchGuids = std::move(result[7]);

                onGuidsListed(
                    std::move(guids), std::move(preservedGuids),
                    std::move(linkedNotebookGuid), std::move(promise));
            }});

    return future;
}

void FullSyncStaleDataExpunger::onGuidsListed(
    FullSyncStaleDataExpunger::Guids guids, PreservedGuids preservedGuids, // NOLINT
    std::optional<qevercloud::Guid> linkedNotebookGuid, // NOLINT
    std::shared_ptr<QPromise<void>> promise) // NOLINT
{
    QSet<qevercloud::Guid> notebookGuidsToExpunge;
    QSet<qevercloud::Guid> tagGuidsToExpunge;
    QSet<qevercloud::Guid> noteGuidsToExpunge;
    QSet<qevercloud::Guid> savedSearchGuidsToExpunge;
    QHash<qevercloud::Guid, qevercloud::Guid> noteGuidsToExpungeByNotebookGuids;

    QSet<qevercloud::Guid> notebookGuidsToUpdate;
    QSet<qevercloud::Guid> tagGuidsToUpdate;
    QSet<qevercloud::Guid> noteGuidsToUpdate;
    QSet<qevercloud::Guid> savedSearchGuidsToUpdate;

    const auto processGuids =
        [](const QSet<qevercloud::Guid> & modifiedGuids, // NOLINT
           const QSet<qevercloud::Guid> & unmodifiedGuids, // NOLINT
           const QSet<qevercloud::Guid> & preservedGuids, // NOLINT
           QSet<qevercloud::Guid> & guidsToExpunge, // NOLINT
           QSet<qevercloud::Guid> & guidsToUpdate) {
            for (const auto & guid: qAsConst(modifiedGuids)) {
                if (preservedGuids.contains(guid)) {
                    continue;
                }

                guidsToUpdate.insert(guid);
            }

            for (const auto & guid: qAsConst(unmodifiedGuids)) {
                if (preservedGuids.contains(guid)) {
                    continue;
                }

                guidsToExpunge.insert(guid);
            }
        };

    processGuids(
        guids.locallyModifiedNotebookGuids,
        guids.unmodifiedNotebookGuids,
        preservedGuids.notebookGuids,
        notebookGuidsToExpunge,
        notebookGuidsToUpdate);

    processGuids(
        guids.locallyModifiedTagGuids,
        guids.unmodifiedTagGuids,
        preservedGuids.tagGuids,
        tagGuidsToExpunge,
        tagGuidsToUpdate);

    processGuids(
        guids.locallyModifiedNoteGuids,
        guids.unmodifiedNoteGuids,
        preservedGuids.noteGuids,
        noteGuidsToExpunge,
        noteGuidsToUpdate);

    if (!linkedNotebookGuid) {
        processGuids(
            guids.locallyModifiedSavedSearchGuids,
            guids.unmodifiedSavedSearchGuids,
            preservedGuids.savedSearchGuids,
            savedSearchGuidsToExpunge,
            savedSearchGuidsToUpdate);
    }

    // TODO: continue from here
}

} // namespace quentier::synchronization
