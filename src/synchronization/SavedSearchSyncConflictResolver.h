/*
 * Copyright 2017 Dmitry Ivanov
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
#include <QObject>

#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
#include <qt5qevercloud/QEverCloudOAuth.h>
#else
#include <qt4qevercloud/QEverCloudOAuth.h>
#endif

namespace quentier {

QT_FORWARD_DECLARE_CLASS(SavedSearchSyncConflictResolutionCache)
QT_FORWARD_DECLARE_CLASS(LocalStorageManagerAsync)

class SavedSearchSyncConflictResolver: public QObject
{
    Q_OBJECT
public:
    explicit SavedSearchSyncConflictResolver(const qevercloud::SavedSearch & remoteSavedSearch,
                                             const SavedSearch & localConflict,
                                             SavedSearchSyncConflictResolutionCache & cache,
                                             LocalStorageManagerAsync & localStorageManagerAsync,
                                             QObject * parent = Q_NULLPTR);

    void start();

    const qevercloud::SavedSearch & remoteSavedSearch() const { return m_remoteSavedSearch; }
    const SavedSearch & localConflict() const { return m_localConflict; }

Q_SIGNALS:
    void finished(qevercloud::SavedSearch remoteSavedSearch);
    void failure(qevercloud::SavedSearch remoteSavedSearch, ErrorString errorDescription);

// private signals
    void fillSavedSearchesCache();
    void addSavedSearch(SavedSearch search, QUuid requestId);
    void updateSavedSearch(SavedSearch search, QUuid requestId);
    void findSavedSearch(SavedSearch search, QUuid requestId);

private Q_SLOTS:
    void onAddSavedSearchComplete(SavedSearch search, QUuid requestId);
    void onAddSavedSearchFailed(SavedSearch search, ErrorString errorDescription, QUuid requestId);
    void onUpdateSavedSearchComplete(SavedSearch search, QUuid requestId);
    void onUpdateSavedSearchFailed(SavedSearch search, ErrorString errorDescription, QUuid requestId);
    void onFindSavedSearchComplete(SavedSearch search, QUuid requestId);
    void onFindSavedSearchFailed(SavedSearch search, ErrorString errorDescription, QUuid requestId);

    void onCacheFilled();
    void onCacheFailed(ErrorString errorDescription);

private:
    void connectToLocalStorage();
    void processSavedSearchesConflictByGuid();
    void processSavedSearchesConflictByName(const SavedSearch & localConflict);
    void overrideLocalChangesWithRemoteChanges();
    void renameConflictingLocalSavedSearch(const SavedSearch & localConflict);

    struct State
    {
        enum type
        {
            Undefined = 0,
            OverrideLocalChangesWithRemoteChanges,
            PendingConflictingSavedSearchRenaming,
            PendingRemoteSavedSearchAdoptionInLocalStorage
        };
    };

private:
    SavedSearchSyncConflictResolutionCache &    m_cache;
    LocalStorageManagerAsync &                  m_localStorageManagerAsync;

    qevercloud::SavedSearch         m_remoteSavedSearch;
    SavedSearch                     m_localConflict;

    SavedSearch                     m_savedSearchToBeRenamed;

    State::type                     m_state;

    QUuid                           m_addSavedSearchRequestId;
    QUuid                           m_updateSavedSearchRequestId;
    QUuid                           m_findSavedSearchRequestId;

    bool                            m_started;
    bool                            m_pendingCacheFilling;
};

} // namespace quentier

#endif // LIB_QUENTIER_SYNCHRONIZATION_SAVED_SEARCH_SYNC_CONFLICT_RESOLVER_H
