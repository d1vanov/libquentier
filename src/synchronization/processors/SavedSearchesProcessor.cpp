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

#include "SavedSearchesProcessor.h"
#include "Utils.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/local_storage/ILocalStorage.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/synchronization/ISyncConflictResolver.h>
#include <quentier/threading/Future.h>

#include <qevercloud/types/SyncChunk.h>

#include <QMutex>
#include <QMutexLocker>

namespace quentier::synchronization {

SavedSearchesProcessor::SavedSearchesProcessor(
    local_storage::ILocalStoragePtr localStorage,
    ISyncConflictResolverPtr syncConflictResolver) :
    m_localStorage{std::move(localStorage)},
    m_syncConflictResolver{std::move(syncConflictResolver)}
{
    if (Q_UNLIKELY(!m_localStorage)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::SavedSearchesProcessor",
            "SavedSearchesProcessor ctor: local storage is null")}};
    }

    if (Q_UNLIKELY(!m_syncConflictResolver)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::SavedSearchesProcessor",
            "SavedSearchesProcessor ctor: sync conflict resolver is null")}};
    }
}

QFuture<void> SavedSearchesProcessor::processSavedSearches(
    const QList<qevercloud::SyncChunk> & syncChunks)
{
    QNDEBUG(
        "synchronization::SavedSearchesProcessor",
        "SavedSearchesProcessor::processSavedSearches");

    QList<qevercloud::SavedSearch> savedSearches;
    QList<qevercloud::Guid> expungedSavedSearches;
    for (const auto & syncChunk: qAsConst(syncChunks)) {
        savedSearches << collectSavedSearches(syncChunk);
        expungedSavedSearches << collectExpungedSavedSearchGuids(syncChunk);
    }

    utils::filterOutExpungedItems(expungedSavedSearches, savedSearches);

    if (savedSearches.isEmpty() && expungedSavedSearches.isEmpty()) {
        QNDEBUG(
            "synchronization::SavedSearchesProcessor",
            "No new/updated/expunged saved searches in the sync chunks");

        return threading::makeReadyFuture();
    }

    const int totalItemCount =
        savedSearches.size() + expungedSavedSearches.size();

    const auto selfWeak = weak_from_this();
    QList<QFuture<void>> savedSearchFutures;
    savedSearchFutures.reserve(totalItemCount);

    for (const auto & savedSearch: qAsConst(savedSearches)) {
        auto savedSearchPromise = std::make_shared<QPromise<void>>();
        savedSearchFutures << savedSearchPromise->future();
        savedSearchPromise->start();

        auto findSavedSearchByGuidFuture =
            m_localStorage->findSavedSearchByGuid(*savedSearch.guid());

        threading::thenOrFailed(
            std::move(findSavedSearchByGuidFuture), savedSearchPromise,
            [this, selfWeak, updatedSavedSearch = savedSearch,
             savedSearchPromise](const std::optional<qevercloud::SavedSearch> &
                                     savedSearch) mutable {
                const auto self = selfWeak.lock();
                if (!self) {
                    return;
                }

                if (savedSearch) {
                    onFoundDuplicate(
                        savedSearchPromise, std::move(updatedSavedSearch),
                        *savedSearch);
                    return;
                }

                tryToFindDuplicateByName(
                    savedSearchPromise, std::move(updatedSavedSearch));
            });
    }

    for (const auto & guid: qAsConst(expungedSavedSearches)) {
        auto savedSearchPromise = std::make_shared<QPromise<void>>();
        savedSearchFutures << savedSearchPromise->future();
        savedSearchPromise->start();

        auto expungeSavedSearchFuture =
            m_localStorage->expungeSavedSearchByGuid(guid);

        threading::thenOrFailed(
            std::move(expungeSavedSearchFuture), savedSearchPromise);
    }

    return threading::whenAll(std::move(savedSearchFutures));
}

QList<qevercloud::SavedSearch> SavedSearchesProcessor::collectSavedSearches(
    const qevercloud::SyncChunk & syncChunk) const
{
    if (!syncChunk.searches() || syncChunk.searches()->isEmpty()) {
        return {};
    }

    QList<qevercloud::SavedSearch> savedSearches;
    savedSearches.reserve(syncChunk.searches()->size());
    for (const auto & savedSearch: qAsConst(*syncChunk.searches())) {
        if (Q_UNLIKELY(!savedSearch.guid())) {
            QNWARNING(
                "synchronization::SavedSearchesProcessor",
                "Detected saved search without guid, skipping it: "
                    << savedSearch);
            continue;
        }

        if (Q_UNLIKELY(!savedSearch.updateSequenceNum())) {
            QNWARNING(
                "synchronization::SavedSearchesProcessor",
                "Detected saved search without update sequence number, "
                    << "skipping it: " << savedSearch);
            continue;
        }

        if (Q_UNLIKELY(!savedSearch.name())) {
            QNWARNING(
                "synchronization::SavedSearchesProcessor",
                "Detected saved search without name, skipping it: "
                    << savedSearch);
            continue;
        }

        savedSearches << savedSearch;
    }

    return savedSearches;
}

QList<qevercloud::Guid> SavedSearchesProcessor::collectExpungedSavedSearchGuids(
    const qevercloud::SyncChunk & syncChunk) const
{
    if (!syncChunk.expungedSearches() ||
        syncChunk.expungedSearches()->isEmpty()) {
        return {};
    }

    return *syncChunk.expungedSearches();
}

void SavedSearchesProcessor::tryToFindDuplicateByName(
    const std::shared_ptr<QPromise<void>> & savedSearchPromise,
    qevercloud::SavedSearch updatedSavedSearch)
{
    Q_ASSERT(updatedSavedSearch.name());

    const auto selfWeak = weak_from_this();

    auto findSavedSearchByNameFuture =
        m_localStorage->findSavedSearchByName(*updatedSavedSearch.name());

    threading::thenOrFailed(
        std::move(findSavedSearchByNameFuture), savedSearchPromise,
        [this, selfWeak, updatedSavedSearch = std::move(updatedSavedSearch),
         savedSearchPromise](const std::optional<qevercloud::SavedSearch> &
                                 savedSearch) mutable {
            const auto self = selfWeak.lock();
            if (!self) {
                return;
            }

            if (savedSearch) {
                onFoundDuplicate(
                    savedSearchPromise, std::move(updatedSavedSearch),
                    *savedSearch);
                return;
            }

            // No duplicate by either guid or name was found, just put
            // the updated saved search to the local storage
            auto putSavedSearchFuture =
                m_localStorage->putSavedSearch(std::move(updatedSavedSearch));

            threading::thenOrFailed(
                std::move(putSavedSearchFuture), savedSearchPromise);
        });
}

void SavedSearchesProcessor::onFoundDuplicate(
    const std::shared_ptr<QPromise<void>> & savedSearchPromise,
    qevercloud::SavedSearch updatedSavedSearch,
    qevercloud::SavedSearch localSavedSearch)
{
    using ConflictResolution = ISyncConflictResolver::ConflictResolution;
    using SavedSearchConflictResolution =
        ISyncConflictResolver::SavedSearchConflictResolution;

    auto statusFuture = m_syncConflictResolver->resolveSavedSearchConflict(
        updatedSavedSearch, std::move(localSavedSearch));

    const auto selfWeak = weak_from_this();

    threading::thenOrFailed(
        std::move(statusFuture), savedSearchPromise,
        [this, selfWeak, savedSearchPromise,
         updatedSavedSearch = std::move(updatedSavedSearch)](
            const SavedSearchConflictResolution & resolution) mutable {
            const auto self = selfWeak.lock();
            if (!self) {
                return;
            }

            if (std::holds_alternative<ConflictResolution::UseTheirs>(
                    resolution) ||
                std::holds_alternative<ConflictResolution::IgnoreMine>(
                    resolution))
            {
                auto putSavedSearchFuture = m_localStorage->putSavedSearch(
                    std::move(updatedSavedSearch));

                threading::thenOrFailed(
                    std::move(putSavedSearchFuture), savedSearchPromise);

                return;
            }

            if (std::holds_alternative<ConflictResolution::UseMine>(resolution))
            {
                savedSearchPromise->finish();
                return;
            }

            if (std::holds_alternative<
                    ConflictResolution::MoveMine<qevercloud::SavedSearch>>(
                    resolution))
            {
                const auto & mineResolution = std::get<
                    ConflictResolution::MoveMine<qevercloud::SavedSearch>>(
                    resolution);

                auto updateLocalSavedSearchFuture =
                    m_localStorage->putSavedSearch(mineResolution.mine);

                threading::thenOrFailed(
                    std::move(updateLocalSavedSearchFuture), savedSearchPromise,
                    [this, selfWeak, savedSearchPromise,
                     updatedSavedSearch =
                         std::move(updatedSavedSearch)]() mutable {
                        const auto self = selfWeak.lock();
                        if (!self) {
                            return;
                        }

                        auto putSavedSearchFuture =
                            m_localStorage->putSavedSearch(
                                std::move(updatedSavedSearch));

                        auto thenFuture = threading::then(
                            std::move(putSavedSearchFuture),
                            [savedSearchPromise]() mutable {
                                savedSearchPromise->finish();
                            });

                        threading::onFailed(
                            std::move(thenFuture),
                            [savedSearchPromise](const QException & e) {
                                savedSearchPromise->setException(e);
                                savedSearchPromise->finish();
                            });
                    });
            }
        });
}

} // namespace quentier::synchronization
