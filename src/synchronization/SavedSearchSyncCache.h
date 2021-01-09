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

#ifndef LIB_QUENTIER_SYNCHRONIZATION_SAVED_SEARCH_SYNC_CACHE_H
#define LIB_QUENTIER_SYNCHRONIZATION_SAVED_SEARCH_SYNC_CACHE_H

#include <quentier/local_storage/LocalStorageManagerAsync.h>

#include <QHash>
#include <QObject>
#include <QUuid>

namespace quentier {

class Q_DECL_HIDDEN SavedSearchSyncCache final : public QObject
{
    Q_OBJECT
public:
    SavedSearchSyncCache(
        LocalStorageManagerAsync & localStorageManagerAsync,
        QObject * parent = nullptr);

    void clear();

    /**
     * @return      True if the cache is already filled with up-to-moment data,
     *              false otherwise
     */
    [[nodiscard]] bool isFilled() const noexcept;

    [[nodiscard]] const QHash<QString, QString> & nameByLocalIdHash()
        const noexcept
    {
        return m_savedSearchNameByLocalId;
    }

    [[nodiscard]] const QHash<QString, QString> & nameByGuidHash()
        const noexcept
    {
        return m_savedSearchNameByGuid;
    }

    [[nodiscard]] const QHash<QString, QString> & guidByNameHash()
        const noexcept
    {
        return m_savedSearchGuidByName;
    }

    [[nodiscard]] const QHash<QString, qevercloud::SavedSearch> &
    dirtySavedSearchesByGuid() const noexcept
    {
        return m_dirtySavedSearchesByGuid;
    }

Q_SIGNALS:
    void filled();
    void failure(ErrorString errorDescription);

    // private signals
    void listSavedSearches(
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListSavedSearchesOrder order,
        LocalStorageManager::OrderDirection orderDirection, QUuid requestId);

public Q_SLOTS:
    /**
     * Start collecting the information about saved searches; does nothing if
     * the information is already collected or is being collected at the moment,
     * otherwise initiates the sequence of actions required to collect
     * the saved search information
     */
    void fill();

private Q_SLOTS:
    void onListSavedSearchesComplete(
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListSavedSearchesOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QList<qevercloud::SavedSearch> foundSearches, QUuid requestId);

    void onListSavedSearchesFailed(
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListSavedSearchesOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        ErrorString errorDescription, QUuid requestId);

    void onAddSavedSearchComplete(
        qevercloud::SavedSearch search, QUuid requestId);

    void onUpdateSavedSearchComplete(
        qevercloud::SavedSearch search, QUuid requestId);

    void onExpungeSavedSearchComplete(
        qevercloud::SavedSearch search, QUuid requestId);

private:
    void connectToLocalStorage();
    void disconnectFromLocalStorage();

    void requestSavedSearchesList();

    void removeSavedSearch(const QString & savedSearchLocalId);
    void processSavedSearch(const qevercloud::SavedSearch & search);

private:
    LocalStorageManagerAsync & m_localStorageManagerAsync;
    bool m_connectedToLocalStorage = false;

    QHash<QString, QString> m_savedSearchNameByLocalId;
    QHash<QString, QString> m_savedSearchNameByGuid;
    QHash<QString, QString> m_savedSearchGuidByName;

    QHash<QString, qevercloud::SavedSearch> m_dirtySavedSearchesByGuid;

    QUuid m_listSavedSearchesRequestId;
    size_t m_limit = 50;
    size_t m_offset = 0;
};

} // namespace quentier

#endif // LIB_QUENTIER_SYNCHRONIZATION_SAVED_SEARCH_SYNC_CACHE_H
