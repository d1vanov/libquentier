/*
 * Copyright 2017-2020 Dmitry Ivanov
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

#include "SavedSearchSyncCache.h"

#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/Compat.h>

namespace quentier {

SavedSearchSyncCache::SavedSearchSyncCache(
    LocalStorageManagerAsync & localStorageManagerAsync, QObject * parent) :
    QObject(parent),
    m_localStorageManagerAsync(localStorageManagerAsync)
{}

void SavedSearchSyncCache::clear()
{
    QNDEBUG(
        "synchronization:saved_search_cache", "SavedSearchSyncCache::clear");

    disconnectFromLocalStorage();

    m_savedSearchNameByLocalUid.clear();
    m_savedSearchNameByGuid.clear();
    m_savedSearchGuidByName.clear();
    m_dirtySavedSearchesByGuid.clear();

    m_listSavedSearchesRequestId = QUuid();
    m_offset = 0;
}

bool SavedSearchSyncCache::isFilled() const
{
    if (!m_connectedToLocalStorage) {
        return false;
    }

    if (m_listSavedSearchesRequestId.isNull()) {
        return true;
    }

    return false;
}

void SavedSearchSyncCache::fill()
{
    QNDEBUG("synchronization:saved_search_cache", "SavedSearchSyncCache::fill");

    if (m_connectedToLocalStorage) {
        QNDEBUG(
            "synchronization:saved_search_cache",
            "Already connected to "
                << "the local storage, no need to do anything");
        return;
    }

    connectToLocalStorage();
    requestSavedSearchesList();
}

void SavedSearchSyncCache::onListSavedSearchesComplete(
    LocalStorageManager::ListObjectsOptions flag, size_t limit, size_t offset,
    LocalStorageManager::ListSavedSearchesOrder order,
    LocalStorageManager::OrderDirection orderDirection,
    QList<SavedSearch> foundSearches, QUuid requestId)
{
    if (requestId != m_listSavedSearchesRequestId) {
        return;
    }

    QNDEBUG(
        "synchronization:saved_search_cache",
        "SavedSearchSyncCache::onListSavedSearchesComplete: flag = "
            << flag << ", limit = " << limit << ", offset = " << offset
            << ", order = " << order << ", order direction = " << orderDirection
            << ", request id = " << requestId);

    for (const auto & search: qAsConst(foundSearches)) {
        processSavedSearch(search);
    }

    m_listSavedSearchesRequestId = QUuid();

    if (foundSearches.size() == static_cast<int>(limit)) {
        QNTRACE(
            "synchronization:saved_search_cache",
            "The number of found "
                << "saved searches matches the limit, requesting more saved "
                << "searches from the local storage");
        m_offset += limit;
        requestSavedSearchesList();
        return;
    }

    Q_EMIT filled();
}

void SavedSearchSyncCache::onListSavedSearchesFailed(
    LocalStorageManager::ListObjectsOptions flag, size_t limit, size_t offset,
    LocalStorageManager::ListSavedSearchesOrder order,
    LocalStorageManager::OrderDirection orderDirection,
    ErrorString errorDescription, QUuid requestId)
{
    if (requestId != m_listSavedSearchesRequestId) {
        return;
    }

    QNDEBUG(
        "synchronization:saved_search_cache",
        "SavedSearchSyncCache::onListSavedSearchesFailed: flag = "
            << flag << ", limit = " << limit << ", offset = " << offset
            << ", order = " << order << ", order direction = " << orderDirection
            << ", error description = " << errorDescription
            << ", request id = " << requestId);

    QNWARNING(
        "synchronization:saved_search_cache",
        "Failed to cache the saved "
            << "search information required for the sync: "
            << errorDescription);

    m_savedSearchNameByLocalUid.clear();
    m_savedSearchNameByGuid.clear();
    m_savedSearchGuidByName.clear();
    m_dirtySavedSearchesByGuid.clear();
    disconnectFromLocalStorage();

    Q_EMIT failure(errorDescription);
}

void SavedSearchSyncCache::onAddSavedSearchComplete(
    SavedSearch search, QUuid requestId)
{
    QNDEBUG(
        "synchronization:saved_search_cache",
        "SavedSearchSyncCache::onAddSavedSearchComplete: request id = "
            << requestId << ", saved search: " << search);

    processSavedSearch(search);
}

void SavedSearchSyncCache::onUpdateSavedSearchComplete(
    SavedSearch search, QUuid requestId)
{
    QNDEBUG(
        "synchronization:saved_search_cache",
        "SavedSearchSyncCache::onUpdateSavedSearchComplete: request id = "
            << requestId << ", saved search: " << search);

    removeSavedSearch(search.localUid());
    processSavedSearch(search);
}

void SavedSearchSyncCache::onExpungeSavedSearchComplete(
    SavedSearch search, QUuid requestId)
{
    QNDEBUG(
        "synchronization:saved_search_cache",
        "SavedSearchSyncCache::onExpungeSavedSearchComplete: request id = "
            << requestId << ", saved search: " << search);

    removeSavedSearch(search.localUid());
}

void SavedSearchSyncCache::connectToLocalStorage()
{
    QNDEBUG(
        "synchronization:saved_search_cache",
        "SavedSearchSyncCache::connectToLocalStorage");

    if (m_connectedToLocalStorage) {
        QNDEBUG(
            "synchronization:saved_search_cache",
            "Already connected to "
                << "the local storage");
        return;
    }

    // Connect local signals to local storage manager async's slots
    QObject::connect(
        this, &SavedSearchSyncCache::listSavedSearches,
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::onListSavedSearchesRequest,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    // Connect local storage manager async's signals to local slots
    QObject::connect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::listSavedSearchesComplete, this,
        &SavedSearchSyncCache::onListSavedSearchesComplete,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::listSavedSearchesFailed, this,
        &SavedSearchSyncCache::onListSavedSearchesFailed,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::addSavedSearchComplete, this,
        &SavedSearchSyncCache::onAddSavedSearchComplete,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::updateSavedSearchComplete, this,
        &SavedSearchSyncCache::onUpdateSavedSearchComplete,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::expungeSavedSearchComplete, this,
        &SavedSearchSyncCache::onExpungeSavedSearchComplete,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    m_connectedToLocalStorage = true;
}

void SavedSearchSyncCache::disconnectFromLocalStorage()
{
    QNDEBUG(
        "synchronization:saved_search_cache",
        "SavedSearchSyncCache::disconnectFromLocalStorage");

    if (!m_connectedToLocalStorage) {
        QNDEBUG(
            "synchronization:saved_search_cache",
            "Not connected to "
                << "the local storage at the moment");
        return;
    }

    // Disconnect local signals from local storage manager async's slots
    QObject::disconnect(
        this, &SavedSearchSyncCache::listSavedSearches,
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::onListSavedSearchesRequest);

    // Disconnect local storage manager async's signals from local slots
    QObject::disconnect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::listSavedSearchesComplete, this,
        &SavedSearchSyncCache::onListSavedSearchesComplete);

    QObject::disconnect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::listSavedSearchesFailed, this,
        &SavedSearchSyncCache::onListSavedSearchesFailed);

    QObject::disconnect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::addSavedSearchComplete, this,
        &SavedSearchSyncCache::onAddSavedSearchComplete);

    QObject::disconnect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::updateSavedSearchComplete, this,
        &SavedSearchSyncCache::onUpdateSavedSearchComplete);

    QObject::disconnect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::expungeSavedSearchComplete, this,
        &SavedSearchSyncCache::onExpungeSavedSearchComplete);

    m_connectedToLocalStorage = false;
}

void SavedSearchSyncCache::requestSavedSearchesList()
{
    QNDEBUG(
        "synchronization:saved_search_cache",
        "SavedSearchSyncCache::requestSavedSearchesList");

    m_listSavedSearchesRequestId = QUuid::createUuid();

    QNTRACE(
        "synchronization:saved_search_cache",
        "Emitting the request to "
            << "list saved searches: request id = "
            << m_listSavedSearchesRequestId << ", offset = " << m_offset);

    Q_EMIT listSavedSearches(
        LocalStorageManager::ListObjectsOption::ListAll, m_limit, m_offset,
        LocalStorageManager::ListSavedSearchesOrder::NoOrder,
        LocalStorageManager::OrderDirection::Ascending,
        m_listSavedSearchesRequestId);
}

void SavedSearchSyncCache::removeSavedSearch(
    const QString & savedSearchLocalUid)
{
    QNDEBUG(
        "synchronization:saved_search_cache",
        "SavedSearchSyncCache::removeSavedSearch: local uid = "
            << savedSearchLocalUid);

    auto localUidIt = m_savedSearchNameByLocalUid.find(savedSearchLocalUid);
    if (Q_UNLIKELY(localUidIt == m_savedSearchNameByLocalUid.end())) {
        QNDEBUG(
            "synchronization:saved_search_cache",
            "The saved search name "
                << "was not found in the cache by local uid");
        return;
    }

    QString name = localUidIt.value();
    Q_UNUSED(m_savedSearchNameByLocalUid.erase(localUidIt))

    auto guidIt = m_savedSearchNameByGuid.find(name);
    if (Q_UNLIKELY(guidIt == m_savedSearchNameByGuid.end())) {
        QNDEBUG(
            "synchronization:saved_search_cache",
            "The saved search guid "
                << "was not found in the cache by name");
        return;
    }

    QString guid = guidIt.value();
    Q_UNUSED(m_savedSearchNameByGuid.erase(guidIt))

    auto dirtySavedSearchIt = m_dirtySavedSearchesByGuid.find(guid);
    if (dirtySavedSearchIt != m_dirtySavedSearchesByGuid.end()) {
        Q_UNUSED(m_dirtySavedSearchesByGuid.erase(dirtySavedSearchIt))
    }

    auto nameIt = m_savedSearchNameByGuid.find(guid);
    if (Q_UNLIKELY(nameIt == m_savedSearchNameByGuid.end())) {
        QNDEBUG(
            "synchronization:saved_search_cache",
            "The saved search name "
                << "was not found in the cache by guid");
        return;
    }

    Q_UNUSED(m_savedSearchNameByGuid.erase(nameIt))
}

void SavedSearchSyncCache::processSavedSearch(const SavedSearch & search)
{
    QNDEBUG(
        "synchronization:saved_search_cache",
        "SavedSearchSyncCache::processSavedSearch: " << search);

    if (search.hasGuid()) {
        if (search.isDirty()) {
            m_dirtySavedSearchesByGuid[search.guid()] = search;
        }
        else {
            auto it = m_dirtySavedSearchesByGuid.find(search.guid());
            if (it != m_dirtySavedSearchesByGuid.end()) {
                Q_UNUSED(m_dirtySavedSearchesByGuid.erase(it))
            }
        }
    }

    if (!search.hasName()) {
        QNDEBUG(
            "synchronization:saved_search_cache",
            "Skipping the saved "
                << "search without a name");
        return;
    }

    QString name = search.name().toLower();
    m_savedSearchNameByLocalUid[search.localUid()] = name;

    if (!search.hasGuid()) {
        return;
    }

    const QString & guid = search.guid();
    m_savedSearchNameByGuid[guid] = name;
    m_savedSearchGuidByName[name] = guid;
}

} // namespace quentier
