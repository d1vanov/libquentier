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

#include "SavedSearchSyncCache.h"
#include <quentier/logging/QuentierLogger.h>

namespace quentier {

SavedSearchSyncCache::SavedSearchSyncCache(LocalStorageManagerAsync & localStorageManagerAsync) :
    m_localStorageManagerAsync(localStorageManagerAsync),
    m_connectedToLocalStorage(false),
    m_savedSearchNameByLocalUid(),
    m_savedSearchNameByGuid(),
    m_savedSearchGuidByName(),
    m_dirtySavedSearchesByGuid(),
    m_listSavedSearchesRequestId(),
    m_limit(50),
    m_offset(0)
{}

void SavedSearchSyncCache::clear()
{
    QNDEBUG(QStringLiteral("SavedSearchSyncCache::clear"));

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
    QNDEBUG(QStringLiteral("SavedSearchSyncCache::fill"));

    if (m_connectedToLocalStorage) {
        QNDEBUG(QStringLiteral("Already connected to the local storage, no need to do anything"));
        return;
    }

    connectToLocalStorage();
    requestSavedSearchesList();
}

void SavedSearchSyncCache::onListSavedSearchesComplete(LocalStorageManager::ListObjectsOptions flag,
                                                       size_t limit, size_t offset,
                                                       LocalStorageManager::ListSavedSearchesOrder::type order,
                                                       LocalStorageManager::OrderDirection::type orderDirection,
                                                       QList<SavedSearch> foundSearches, QUuid requestId)
{
    if (requestId != m_listSavedSearchesRequestId) {
        return;
    }

    QNDEBUG(QStringLiteral("SavedSearchSyncCache::onListSavedSearchesComplete: flag = ")
            << flag << QStringLiteral(", limit = ") << limit << QStringLiteral(", offset = ")
            << offset << QStringLiteral(", order = ") << order << QStringLiteral(", order direction = ")
            << orderDirection << QStringLiteral(", request id = ") << requestId);

    for(auto it = foundSearches.constBegin(), end = foundSearches.constEnd(); it != end; ++it) {
        processSavedSearch(*it);
    }

    m_listSavedSearchesRequestId = QUuid();

    if (foundSearches.size() == static_cast<int>(limit)) {
        QNTRACE(QStringLiteral("The number of found saved searches matches the limit, requesting more saved searches "
                               "from the local storage"));
        m_offset += limit;
        requestSavedSearchesList();
        return;
    }

    emit filled();
}

void SavedSearchSyncCache::onListSavedSearchesFailed(LocalStorageManager::ListObjectsOptions flag,
                                                     size_t limit, size_t offset,
                                                     LocalStorageManager::ListSavedSearchesOrder::type order,
                                                     LocalStorageManager::OrderDirection::type orderDirection,
                                                     ErrorString errorDescription, QUuid requestId)
{
    if (requestId != m_listSavedSearchesRequestId) {
        return;
    }

    QNDEBUG(QStringLiteral("SavedSearchSyncCache::onListSavedSearchesFailed: flag = ")
            << flag << QStringLiteral(", limit = ") << limit << QStringLiteral(", offset = ")
            << offset << QStringLiteral(", order = ") << order << QStringLiteral(", order direction = ")
            << orderDirection << QStringLiteral(", error description = ") << errorDescription
            << QStringLiteral(", request id = ") << requestId);

    QNWARNING(QStringLiteral("Failed to cache the saved search information required for the sync conflicts resolution: ")
              << errorDescription);

    m_savedSearchNameByLocalUid.clear();
    m_savedSearchNameByGuid.clear();
    m_savedSearchGuidByName.clear();
    m_dirtySavedSearchesByGuid.clear();
    disconnectFromLocalStorage();

    emit failure(errorDescription);
}

void SavedSearchSyncCache::onAddSavedSearchComplete(SavedSearch search, QUuid requestId)
{
    QNDEBUG(QStringLiteral("SavedSearchSyncCache::onAddSavedSearchComplete: request id = ")
            << requestId << QStringLiteral(", saved search: ") << search);

    processSavedSearch(search);
}

void SavedSearchSyncCache::onUpdateSavedSearchComplete(SavedSearch search, QUuid requestId)
{
    QNDEBUG(QStringLiteral("SavedSearchSyncCache::onUpdateSavedSearchComplete: request id = ")
            << requestId << QStringLiteral(", saved search: ") << search);

    removeSavedSearch(search.localUid());
    processSavedSearch(search);
}

void SavedSearchSyncCache::onExpungeSavedSearchComplete(SavedSearch search, QUuid requestId)
{
    QNDEBUG(QStringLiteral("SavedSearchSyncCache::onExpungeSavedSearchComplete: request id = ")
            << requestId << QStringLiteral(", saved search: ") << search);

    removeSavedSearch(search.localUid());
}

void SavedSearchSyncCache::connectToLocalStorage()
{
    QNDEBUG(QStringLiteral("SavedSearchSyncCache::connectToLocalStorage"));

    if (m_connectedToLocalStorage) {
        QNDEBUG(QStringLiteral("Already connected to the local storage"));
        return;
    }

    // Connect local signals to local storage manager async's slots
    QObject::connect(this,
                     QNSIGNAL(SavedSearchSyncCache,listSavedSearches,
                              LocalStorageManager::ListObjectsOptions,size_t,size_t,
                              LocalStorageManager::ListSavedSearchesOrder::type,
                              LocalStorageManager::OrderDirection::type,QUuid),
                     &m_localStorageManagerAsync,
                     QNSLOT(LocalStorageManagerAsync,onListSavedSearchesRequest,
                            LocalStorageManager::ListObjectsOptions,size_t,size_t,
                            LocalStorageManager::ListSavedSearchesOrder::type,
                            LocalStorageManager::OrderDirection::type,QUuid));

    // Connect local storage manager async's signals to local slots
    QObject::connect(&m_localStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,listSavedSearchesComplete,
                              LocalStorageManager::ListObjectsOptions,size_t,size_t,
                              LocalStorageManager::ListSavedSearchesOrder::type,
                              LocalStorageManager::OrderDirection::type,
                              QList<SavedSearch>,QUuid),
                     this,
                     QNSLOT(SavedSearchSyncCache,onListSavedSearchesComplete,
                            LocalStorageManager::ListObjectsOptions,size_t,size_t,
                            LocalStorageManager::ListSavedSearchesOrder::type,
                            LocalStorageManager::OrderDirection::type,
                            QList<SavedSearch>,QUuid));
    QObject::connect(&m_localStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,listSavedSearchesFailed,
                              LocalStorageManager::ListObjectsOptions,size_t,size_t,
                              LocalStorageManager::ListSavedSearchesOrder::type,
                              LocalStorageManager::OrderDirection::type,
                              ErrorString,QUuid),
                     this,
                     QNSLOT(SavedSearchSyncCache,onListSavedSearchesFailed,
                            LocalStorageManager::ListObjectsOptions,size_t,size_t,
                            LocalStorageManager::ListSavedSearchesOrder::type,
                            LocalStorageManager::OrderDirection::type,
                            ErrorString,QUuid));
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addSavedSearchComplete,SavedSearch,QUuid),
                     this, QNSLOT(SavedSearchSyncCache,onAddSavedSearchComplete,SavedSearch,QUuid));
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateSavedSearchComplete,SavedSearch,QUuid),
                     this, QNSLOT(SavedSearchSyncCache,onUpdateSavedSearchComplete,SavedSearch,QUuid));
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeSavedSearchComplete,SavedSearch,QUuid),
                     this, QNSLOT(SavedSearchSyncCache,onExpungeSavedSearchComplete,SavedSearch,QUuid));

    m_connectedToLocalStorage = true;
}

void SavedSearchSyncCache::disconnectFromLocalStorage()
{
    QNDEBUG(QStringLiteral("SavedSearchSyncCache::disconnectFromLocalStorage"));

    if (!m_connectedToLocalStorage) {
        QNDEBUG(QStringLiteral("Not connected to local storage at the moment"));
        return;
    }

    // Disconnect local signals from local storage manager async's slots
    QObject::disconnect(this,
                        QNSIGNAL(SavedSearchSyncCache,listSavedSearches,
                                 LocalStorageManager::ListObjectsOptions,size_t,size_t,
                                 LocalStorageManager::ListSavedSearchesOrder::type,
                                 LocalStorageManager::OrderDirection::type,QUuid),
                        &m_localStorageManagerAsync,
                        QNSLOT(LocalStorageManagerAsync,onListSavedSearchesRequest,
                               LocalStorageManager::ListObjectsOptions,size_t,size_t,
                               LocalStorageManager::ListSavedSearchesOrder::type,
                               LocalStorageManager::OrderDirection::type,QUuid));

    // Disconnect local storage manager async's signals from local slots
    QObject::disconnect(&m_localStorageManagerAsync,
                        QNSIGNAL(LocalStorageManagerAsync,listSavedSearchesComplete,
                                 LocalStorageManager::ListObjectsOptions,size_t,size_t,
                                 LocalStorageManager::ListSavedSearchesOrder::type,
                                 LocalStorageManager::OrderDirection::type,
                                 QList<SavedSearch>,QUuid),
                        this,
                        QNSLOT(SavedSearchSyncCache,onListSavedSearchesComplete,
                               LocalStorageManager::ListObjectsOptions,size_t,size_t,
                               LocalStorageManager::ListSavedSearchesOrder::type,
                               LocalStorageManager::OrderDirection::type,
                               QList<SavedSearch>,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync,
                        QNSIGNAL(LocalStorageManagerAsync,listSavedSearchesFailed,
                                 LocalStorageManager::ListObjectsOptions,size_t,size_t,
                                 LocalStorageManager::ListSavedSearchesOrder::type,
                                 LocalStorageManager::OrderDirection::type,
                                 ErrorString,QUuid),
                        this,
                        QNSLOT(SavedSearchSyncCache,onListSavedSearchesFailed,
                               LocalStorageManager::ListObjectsOptions,size_t,size_t,
                               LocalStorageManager::ListSavedSearchesOrder::type,
                               LocalStorageManager::OrderDirection::type,
                               ErrorString,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addSavedSearchComplete,SavedSearch,QUuid),
                        this, QNSLOT(SavedSearchSyncCache,onAddSavedSearchComplete,SavedSearch,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateSavedSearchComplete,SavedSearch,QUuid),
                        this, QNSLOT(SavedSearchSyncCache,onUpdateSavedSearchComplete,SavedSearch,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeSavedSearchComplete,SavedSearch,QUuid),
                        this, QNSLOT(SavedSearchSyncCache,onExpungeSavedSearchComplete,SavedSearch,QUuid));

    m_connectedToLocalStorage = false;
}

void SavedSearchSyncCache::requestSavedSearchesList()
{
    QNDEBUG(QStringLiteral("SavedSearchSyncCache::requestSavedSearchesList"));

    m_listSavedSearchesRequestId = QUuid::createUuid();

    QNTRACE(QStringLiteral("Emitting the request to list saved searches: request id = ")
            << m_listSavedSearchesRequestId << QStringLiteral(", offset = ") << m_offset);
    emit listSavedSearches(LocalStorageManager::ListAll,
                           m_limit, m_offset, LocalStorageManager::ListSavedSearchesOrder::NoOrder,
                           LocalStorageManager::OrderDirection::Ascending,
                           m_listSavedSearchesRequestId);
}

void SavedSearchSyncCache::removeSavedSearch(const QString & savedSearchLocalUid)
{
    QNDEBUG(QStringLiteral("SavedSearchSyncCache::removeSavedSearch: local uid = ")
            << savedSearchLocalUid);

    auto localUidIt = m_savedSearchNameByLocalUid.find(savedSearchLocalUid);
    if (Q_UNLIKELY(localUidIt == m_savedSearchNameByLocalUid.end())) {
        QNDEBUG(QStringLiteral("The saved search name was not found in the cache by local uid"));
        return;
    }

    QString name = localUidIt.value();
    Q_UNUSED(m_savedSearchNameByLocalUid.erase(localUidIt))

    auto guidIt = m_savedSearchNameByGuid.find(name);
    if (Q_UNLIKELY(guidIt == m_savedSearchNameByGuid.end())) {
        QNDEBUG(QStringLiteral("The saved search guid was not found in the cache by name"));
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
        QNDEBUG(QStringLiteral("The saved search name was not found in the cache by guid"));
        return;
    }

    Q_UNUSED(m_savedSearchNameByGuid.erase(nameIt))
}

void SavedSearchSyncCache::processSavedSearch(const SavedSearch & search)
{
    QNDEBUG(QStringLiteral("SavedSearchSyncCache::processSavedSearch: ") << search);

    if (search.hasGuid())
    {
        if (search.isDirty())
        {
            m_dirtySavedSearchesByGuid[search.guid()] = search;
        }
        else
        {
            auto it = m_dirtySavedSearchesByGuid.find(search.guid());
            if (it != m_dirtySavedSearchesByGuid.end()) {
                Q_UNUSED(m_dirtySavedSearchesByGuid.erase(it))
            }
        }
    }

    if (!search.hasName()) {
        QNDEBUG(QStringLiteral("Skipping the saved search without a name"));
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
