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

#pragma once

#include <quentier/utility/cancelers/Fwd.h>

#include <qevercloud/types/TypeAliases.h>

#include <QFuture>
#include <QSet>

#include <optional>

class QDebug;
class QTextStream;

namespace quentier::synchronization {

/**
 * @brief The IFullSyncStaleDataExpunger interface is meant to ensure there
 * would be no stale data left within the local storage after the full sync if
 * the full sync is being performed for an account which has already performed
 * full sync in the past i.e. which local storage is not empty but filled with
 * something.
 *
 * From time to time the Evernote synchronization protocol (EDAM) might require
 * the client to perform a full sync instead of incremental sync. It might
 * happen because the client hasn't synced with the service for too long so that
 * the guids of expunged data items are no longer stored within the service. It
 * also might happen in case of some unforeseen service's malfunction so that
 * the status quo needs to be restored for all clients. In any event, sometimes
 * Evernote might require the client to perform the full sync.
 *
 * When the client performs full sync for the first time, there is no need for
 * the client to expunge anything: it starts with empty local storage and only
 * puts the data received from the service into it. However, when full sync
 * is done after the local storage had already been filled with something, the
 * client needs to understand which data items are now stale within its local
 * storage (i.e. were expunged from the service at some point) and thus need to
 * be expunged from the client's local storage. These are all data items which
 * have guids which were not referenced during the last full sync.
 *
 * However, for the sake of preserving the modified but not yet synchronized
 * data, the matching data items which are marked as locally modified need not
 * be expunged from the local storage: instead these items are recreated in the
 * local storage as local ones which have not yet been synchronized with
 * Evernote.
 */
class IFullSyncStaleDataExpunger
{
public:
    virtual ~IFullSyncStaleDataExpunger() = default;

    /**
     * Collection of guids of data items which need to be preserved i.e. not
     * expunged from the local storage. These guids are meant to be taken from
     * sync chunks downloaded during the full sync.
     */
    struct PreservedGuids
    {
        QSet<qevercloud::Guid> notebookGuids;
        QSet<qevercloud::Guid> tagGuids;
        QSet<qevercloud::Guid> noteGuids;
        QSet<qevercloud::Guid> savedSearchGuids;
    };

    friend QTextStream & operator<<(
        QTextStream & strm, const PreservedGuids & preservedGuids);

    friend QDebug & operator<<(
        QDebug & dbg, const PreservedGuids & preservedGuids);

    /**
     * Expunge relevant data items not matching the guids meant to be preserved.
     * @param preservedGuids        Guids of data items which should not be
     *                              expunged.
     * @param canceler              Canceler for asynchronous process of stale
     *                              data expunging.
     * @param linkedNotebookGuid    If not equal to std::nullopt, this guid
     *                              represents the fact that the only stale data
     *                              which should be expunged is the data
     *                              belonging to the particular linked notebook
     *                              corresponding to this guid. Otherwise stale
     *                              data for user's own account is expunged.
     * @return Future with no value or with exception in case of error.
     */
    [[nodiscard]] virtual QFuture<void> expungeStaleData(
        PreservedGuids preservedGuids,
        utility::cancelers::ICancelerPtr canceler,
        std::optional<qevercloud::Guid> linkedNotebookGuid = {}) = 0;
};

[[nodiscard]] bool operator==(
    const IFullSyncStaleDataExpunger::PreservedGuids & lhs,
    const IFullSyncStaleDataExpunger::PreservedGuids & rhs) noexcept;

[[nodiscard]] bool operator!=(
    const IFullSyncStaleDataExpunger::PreservedGuids & lhs,
    const IFullSyncStaleDataExpunger::PreservedGuids & rhs) noexcept;

} // namespace quentier::synchronization
