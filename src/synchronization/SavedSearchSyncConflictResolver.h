/*
 * Copyright 2017-2021 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_SYNCHRONIZATION_SAVED_SEARCH_SYNC_CONFLICT_RESOLVER_H
#define LIB_QUENTIER_SYNCHRONIZATION_SAVED_SEARCH_SYNC_CONFLICT_RESOLVER_H

#include <quentier/types/SavedSearch.h>

#include <qevercloud/generated/types/SavedSearch.h>

#include <QObject>

class QDebug;

namespace quentier {

class SavedSearchSyncCache;
class LocalStorageManagerAsync;

class Q_DECL_HIDDEN SavedSearchSyncConflictResolver final : public QObject
{
    Q_OBJECT
public:
    explicit SavedSearchSyncConflictResolver(
        const qevercloud::SavedSearch & remoteSavedSearch,
        const qevercloud::SavedSearch & localConflict,
        SavedSearchSyncCache & cache,
        LocalStorageManagerAsync & localStorageManagerAsync,
        QObject * parent = nullptr);

    void start();

    [[nodiscard]] const qevercloud::SavedSearch & remoteSavedSearch()
        const noexcept
    {
        return m_remoteSavedSearch;
    }

    [[nodiscard]] const qevercloud::SavedSearch & localConflict() const noexcept
    {
        return m_localConflict;
    }

Q_SIGNALS:
    void finished(qevercloud::SavedSearch remoteSavedSearch);

    void failure(
        qevercloud::SavedSearch remoteSavedSearch,
        ErrorString errorDescription);

    // private signals
    void fillSavedSearchesCache();
    void addSavedSearch(qevercloud::SavedSearch search, QUuid requestId);
    void updateSavedSearch(qevercloud::SavedSearch search, QUuid requestId);
    void findSavedSearch(qevercloud::SavedSearch search, QUuid requestId);

private Q_SLOTS:
    void onAddSavedSearchComplete(
        qevercloud::SavedSearch search, QUuid requestId);

    void onAddSavedSearchFailed(
        qevercloud::SavedSearch search, ErrorString errorDescription,
        QUuid requestId);

    void onUpdateSavedSearchComplete(
        qevercloud::SavedSearch search, QUuid requestId);

    void onUpdateSavedSearchFailed(
        qevercloud::SavedSearch search, ErrorString errorDescription,
        QUuid requestId);

    void onFindSavedSearchComplete(
        qevercloud::SavedSearch search, QUuid requestId);

    void onFindSavedSearchFailed(
        qevercloud::SavedSearch search, ErrorString errorDescription,
        QUuid requestId);

    void onCacheFilled();
    void onCacheFailed(ErrorString errorDescription);

private:
    void connectToLocalStorage();
    void processSavedSearchesConflictByGuid();

    void processSavedSearchesConflictByName(
        const qevercloud::SavedSearch & localConflict);

    void overrideLocalChangesWithRemoteChanges();

    void renameConflictingLocalSavedSearch(
        const qevercloud::SavedSearch & localConflict);

    enum class State
    {
        Undefined = 0,
        OverrideLocalChangesWithRemoteChanges,
        PendingConflictingSavedSearchRenaming,
        PendingRemoteSavedSearchAdoptionInLocalStorage
    };

    friend QDebug & operator<<(QDebug & dbg, const State state);

private:
    SavedSearchSyncCache & m_cache;
    LocalStorageManagerAsync & m_localStorageManagerAsync;

    qevercloud::SavedSearch m_remoteSavedSearch;
    qevercloud::SavedSearch m_localConflict;

    qevercloud::SavedSearch m_savedSearchToBeRenamed;

    State m_state = State::Undefined;

    QUuid m_addSavedSearchRequestId;
    QUuid m_updateSavedSearchRequestId;
    QUuid m_findSavedSearchRequestId;

    bool m_started = false;
    bool m_pendingCacheFilling = false;
};

} // namespace quentier

#endif // LIB_QUENTIER_SYNCHRONIZATION_SAVED_SEARCH_SYNC_CONFLICT_RESOLVER_H
