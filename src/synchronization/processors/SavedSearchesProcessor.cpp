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
#include <quentier/threading/TrackedTask.h>

#include <qevercloud/types/SyncChunk.h>

namespace quentier::synchronization {

namespace {

[[nodiscard]] QList<qevercloud::SavedSearch> collectSavedSearches(
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

[[nodiscard]] QList<qevercloud::Guid> collectExpungedSavedSearchGuids(
    const qevercloud::SyncChunk & syncChunk)
{
    return syncChunk.expungedSearches().value_or(QList<qevercloud::Guid>{});
}

} // namespace

class SavedSearchesProcessor::SavedSearchCounters
{
public:
    SavedSearchCounters(
        const qint32 totalSavedSearches,
        const qint32 totalSavedSearchesToExpunge,
        ISavedSearchesProcessor::ICallbackWeakPtr callbackWeak) :
        m_totalSavedSearches{totalSavedSearches},
        m_totalExpungedSavedSearches{totalSavedSearchesToExpunge},
        m_callbackWeak{std::move(callbackWeak)}
    {}

    void onAddedSavedSearch()
    {
        ++m_addedSavedSearches;
        notifyUpdate();
    }

    void onUpdatedSavedSearch()
    {
        ++m_updatedSavedSearches;
        notifyUpdate();
    }

    void onExpungedSavedSearch()
    {
        ++m_expungedSavedSearches;
        notifyUpdate();
    }

private:
    void notifyUpdate()
    {
        if (const auto callback = m_callbackWeak.lock()) {
            callback->onSavedSearchesProcessingProgress(
                m_totalSavedSearches, m_totalExpungedSavedSearches,
                m_addedSavedSearches, m_updatedSavedSearches,
                m_expungedSavedSearches);
        }
    }

private:
    const qint32 m_totalSavedSearches;
    const qint32 m_totalExpungedSavedSearches;
    const ISavedSearchesProcessor::ICallbackWeakPtr m_callbackWeak;

    qint32 m_addedSavedSearches{0};
    qint32 m_updatedSavedSearches{0};
    qint32 m_expungedSavedSearches{0};
};

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
    const QList<qevercloud::SyncChunk> & syncChunks,
    ICallbackWeakPtr callbackWeak)
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

    const qint32 totalSavedSearches = savedSearches.size();
    const qint32 totalSavedSearchesToExpunge = expungedSavedSearches.size();
    const qint32 totalItemCount =
        totalSavedSearches + totalSavedSearchesToExpunge;

    const auto selfWeak = weak_from_this();
    QList<QFuture<void>> savedSearchFutures;
    savedSearchFutures.reserve(totalItemCount);

    const auto savedSearchCounters = std::make_shared<SavedSearchCounters>(
        totalSavedSearches, totalSavedSearchesToExpunge,
        std::move(callbackWeak));

    for (const auto & savedSearch: qAsConst(savedSearches)) {
        auto savedSearchPromise = std::make_shared<QPromise<void>>();
        savedSearchFutures << savedSearchPromise->future();
        savedSearchPromise->start();

        Q_ASSERT(savedSearch.guid());

        auto findSavedSearchByGuidFuture =
            m_localStorage->findSavedSearchByGuid(*savedSearch.guid());

        threading::thenOrFailed(
            std::move(findSavedSearchByGuidFuture), savedSearchPromise,
            threading::TrackedTask{
                selfWeak,
                [this, updatedSavedSearch = savedSearch, savedSearchPromise,
                 savedSearchCounters](
                    const std::optional<qevercloud::SavedSearch> &
                        savedSearch) mutable {
                    if (savedSearch) {
                        onFoundDuplicate(
                            savedSearchPromise, savedSearchCounters,
                            std::move(updatedSavedSearch), *savedSearch);
                        return;
                    }

                    tryToFindDuplicateByName(
                        savedSearchPromise, savedSearchCounters,
                        std::move(updatedSavedSearch));
                },
            });
    }

    for (const auto & guid: qAsConst(expungedSavedSearches)) {
        auto savedSearchPromise = std::make_shared<QPromise<void>>();
        savedSearchFutures << savedSearchPromise->future();
        savedSearchPromise->start();

        auto expungeSavedSearchFuture =
            m_localStorage->expungeSavedSearchByGuid(guid);

        auto thenFuture = threading::then(
            std::move(expungeSavedSearchFuture), [savedSearchCounters] {
                savedSearchCounters->onExpungedSavedSearch();
            });

        threading::thenOrFailed(
            std::move(thenFuture), std::move(savedSearchPromise));
    }

    return threading::whenAll(std::move(savedSearchFutures));
}

void SavedSearchesProcessor::tryToFindDuplicateByName(
    const std::shared_ptr<QPromise<void>> & savedSearchPromise,
    const std::shared_ptr<SavedSearchCounters> & savedSearchCounters,
    qevercloud::SavedSearch updatedSavedSearch)
{
    Q_ASSERT(updatedSavedSearch.name());

    const auto selfWeak = weak_from_this();

    auto findSavedSearchByNameFuture =
        m_localStorage->findSavedSearchByName(*updatedSavedSearch.name());

    threading::thenOrFailed(
        std::move(findSavedSearchByNameFuture), savedSearchPromise,
        threading::TrackedTask{
            selfWeak,
            [this, selfWeak, updatedSavedSearch = std::move(updatedSavedSearch),
             savedSearchPromise = savedSearchPromise,
             savedSearchCounters](const std::optional<qevercloud::SavedSearch> &
                                      savedSearch) mutable {
                if (savedSearch) {
                    onFoundDuplicate(
                        savedSearchPromise, savedSearchCounters,
                        std::move(updatedSavedSearch), *savedSearch);
                    return;
                }

                // No duplicate by either guid or name was found, just put
                // the updated saved search into the local storage
                auto putSavedSearchFuture = m_localStorage->putSavedSearch(
                    std::move(updatedSavedSearch));

                auto thenFuture = threading::then(
                    std::move(putSavedSearchFuture), [savedSearchCounters] {
                        savedSearchCounters->onAddedSavedSearch();
                    });

                threading::thenOrFailed(
                    std::move(thenFuture), std::move(savedSearchPromise));
            }});
}

void SavedSearchesProcessor::onFoundDuplicate(
    const std::shared_ptr<QPromise<void>> & savedSearchPromise,
    const std::shared_ptr<SavedSearchCounters> & savedSearchCounters,
    qevercloud::SavedSearch updatedSavedSearch,
    qevercloud::SavedSearch localSavedSearch)
{
    using ConflictResolution = ISyncConflictResolver::ConflictResolution;
    using SavedSearchConflictResolution =
        ISyncConflictResolver::SavedSearchConflictResolution;

    auto localSavedSearchLocalId = localSavedSearch.localId();

    const auto localSavedSearchLocallyFavorited =
        localSavedSearch.isLocallyFavorited();

    auto statusFuture = m_syncConflictResolver->resolveSavedSearchConflict(
        updatedSavedSearch, std::move(localSavedSearch));

    const auto selfWeak = weak_from_this();

    threading::thenOrFailed(
        std::move(statusFuture), savedSearchPromise,
        threading::TrackedTask{
            selfWeak,
            [this, selfWeak, savedSearchPromise, savedSearchCounters,
             updatedSavedSearch = std::move(updatedSavedSearch),
             localSavedSearchLocalId = std::move(localSavedSearchLocalId),
             localSavedSearchLocallyFavorited](
                const SavedSearchConflictResolution & resolution) mutable {
                if (std::holds_alternative<ConflictResolution::UseTheirs>(
                        resolution) ||
                    std::holds_alternative<ConflictResolution::IgnoreMine>(
                        resolution))
                {
                    if (std::holds_alternative<ConflictResolution::UseTheirs>(
                            resolution)) {
                        updatedSavedSearch.setLocalId(localSavedSearchLocalId);

                        updatedSavedSearch.setLocallyFavorited(
                            localSavedSearchLocallyFavorited);
                    }

                    auto putSavedSearchFuture = m_localStorage->putSavedSearch(
                        std::move(updatedSavedSearch));

                    auto thenFuture = threading::then(
                        std::move(putSavedSearchFuture), [savedSearchCounters] {
                            savedSearchCounters->onUpdatedSavedSearch();
                        });

                    threading::thenOrFailed(
                        std::move(thenFuture), savedSearchPromise);

                    return;
                }

                if (std::holds_alternative<ConflictResolution::UseMine>(
                        resolution)) {
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
                        std::move(updateLocalSavedSearchFuture),
                        savedSearchPromise,
                        threading::TrackedTask{
                            selfWeak,
                            [this, selfWeak, savedSearchPromise,
                             savedSearchCounters,
                             updatedSavedSearch =
                                 std::move(updatedSavedSearch)]() mutable {
                                auto putSavedSearchFuture =
                                    m_localStorage->putSavedSearch(
                                        std::move(updatedSavedSearch));

                                auto thenFuture = threading::then(
                                    std::move(putSavedSearchFuture),
                                    [savedSearchPromise, savedSearchCounters] {
                                        savedSearchCounters
                                            ->onAddedSavedSearch();
                                        savedSearchPromise->finish();
                                    });

                                threading::onFailed(
                                    std::move(thenFuture),
                                    [savedSearchPromise](const QException & e) {
                                        savedSearchPromise->setException(e);
                                        savedSearchPromise->finish();
                                    });
                            }});
                }
            }});
}

} // namespace quentier::synchronization
