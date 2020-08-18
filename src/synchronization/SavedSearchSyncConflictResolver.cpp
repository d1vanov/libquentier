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

#include "SavedSearchSyncConflictResolver.h"

#include "SavedSearchSyncCache.h"

#include <quentier/local_storage/LocalStorageManagerAsync.h>
#include <quentier/logging/QuentierLogger.h>

namespace quentier {

SavedSearchSyncConflictResolver::SavedSearchSyncConflictResolver(
    const qevercloud::SavedSearch & remoteSavedSearch,
    const SavedSearch & localConflict, SavedSearchSyncCache & cache,
    LocalStorageManagerAsync & localStorageManagerAsync, QObject * parent) :
    QObject(parent),
    m_cache(cache), m_localStorageManagerAsync(localStorageManagerAsync),
    m_remoteSavedSearch(remoteSavedSearch), m_localConflict(localConflict)
{}

void SavedSearchSyncConflictResolver::start()
{
    QNDEBUG(
        "synchronization:saved_search_conflict",
        "SavedSearchSyncConflictResolver::start");

    if (m_started) {
        QNDEBUG("synchronization:saved_search_conflict", "Already started");
        return;
    }

    m_started = true;

    if (Q_UNLIKELY(!m_remoteSavedSearch.guid.isSet())) {
        ErrorString error(
            QT_TR_NOOP("Can't resolve the conflict between remote "
                       "and local saved searches: the remote saved "
                       "search has no guid set"));
        QNWARNING(
            "synchronization:saved_search_conflict",
            error << ": " << m_remoteSavedSearch);
        Q_EMIT failure(m_remoteSavedSearch, error);
        return;
    }

    if (Q_UNLIKELY(!m_remoteSavedSearch.name.isSet())) {
        ErrorString error(
            QT_TR_NOOP("Can't resolve the conflict between remote "
                       "and local saved searches: the remote saved "
                       "search has no name set"));
        QNWARNING(
            "synchronization:saved_search_conflict",
            error << ": " << m_remoteSavedSearch);
        Q_EMIT failure(m_remoteSavedSearch, error);
        return;
    }

    if (Q_UNLIKELY(!m_localConflict.hasGuid() && !m_localConflict.hasName())) {
        ErrorString error(
            QT_TR_NOOP("Can't resolve the conflict between remote "
                       "and local saved searches: the local "
                       "conflicting saved search has neither guid "
                       "not name set"));
        QNWARNING(
            "synchronization:saved_search_conflict",
            error << ": " << m_localConflict);
        Q_EMIT failure(m_remoteSavedSearch, error);
        return;
    }

    connectToLocalStorage();

    if (m_localConflict.hasName() &&
        (m_localConflict.name() == m_remoteSavedSearch.name.ref()))
    {
        processSavedSearchesConflictByName(m_localConflict);
    }
    else {
        processSavedSearchesConflictByGuid();
    }
}

void SavedSearchSyncConflictResolver::onAddSavedSearchComplete(
    SavedSearch search, QUuid requestId)
{
    if (requestId != m_addSavedSearchRequestId) {
        return;
    }

    QNDEBUG(
        "synchronization:saved_search_conflict",
        "SavedSearchSyncConflictResolver::onAddSavedSearchComplete: "
            << "request id = " << requestId << ", saved search: " << search);

    if (m_state == State::PendingRemoteSavedSearchAdoptionInLocalStorage) {
        QNDEBUG(
            "synchronization:saved_search_conflict",
            "Successfully added "
                << "the remote saved search to the local storage");
        Q_EMIT finished(m_remoteSavedSearch);
    }
    else {
        ErrorString error(
            QT_TR_NOOP("Internal error: wrong state on receiving "
                       "the confirmation about the saved search "
                       "addition from the local storage"));
        QNWARNING(
            "synchronization:saved_search_conflict",
            error << ", saved search: " << search);
        Q_EMIT failure(m_remoteSavedSearch, error);
    }
}

void SavedSearchSyncConflictResolver::onAddSavedSearchFailed(
    SavedSearch search, ErrorString errorDescription, QUuid requestId)
{
    if (requestId != m_addSavedSearchRequestId) {
        return;
    }

    QNDEBUG(
        "synchronization:saved_search_conflict",
        "SavedSearchSyncConflictResolver::onAddSavedSearchFailed: request id = "
            << requestId << ", error description = " << errorDescription
            << "; saved search: " << search);

    Q_EMIT failure(m_remoteSavedSearch, errorDescription);
}

void SavedSearchSyncConflictResolver::onUpdateSavedSearchComplete(
    SavedSearch search, QUuid requestId)
{
    if (requestId != m_updateSavedSearchRequestId) {
        return;
    }

    QNDEBUG(
        "synchronization:saved_search_conflict",
        "SavedSearchSyncConflictResolver::onUpdateSavedSearchComplete: "
            << "request id = " << requestId << ", saved search: " << search);

    if (m_state == State::OverrideLocalChangesWithRemoteChanges) {
        QNDEBUG(
            "synchronization:saved_search_conflict",
            "Successfully "
                << "overridden the local changes with remote changes");
        Q_EMIT finished(m_remoteSavedSearch);
        return;
    }
    else if (m_state == State::PendingConflictingSavedSearchRenaming) {
        QNDEBUG(
            "synchronization:saved_search_conflict",
            "Successfully renamed "
                << "the local saved search conflicting by name with the remote "
                << "search");

        /**
         * Now need to find the duplicate of the remote saved search by guid:
         * 1) if one exists, update it from the remote changes - notwithstanding
         *    its "dirty" state
         * 2) if one doesn't exist, add it to the local storage

         * The cache should have been filled by that moment, otherwise how could
         * the local saved search conflicting by name be renamed properly?
         */
        if (Q_UNLIKELY(!m_cache.isFilled())) {
            ErrorString error(
                QT_TR_NOOP("Internal error: the cache of saved search info is "
                           "not filled while it should have been"));
            QNWARNING("synchronization:saved_search_conflict", error);
            Q_EMIT failure(m_remoteSavedSearch, error);
            return;
        }

        m_state = State::PendingRemoteSavedSearchAdoptionInLocalStorage;

        const auto & nameByGuidHash = m_cache.nameByGuidHash();
        auto it = nameByGuidHash.find(m_remoteSavedSearch.guid.ref());
        if (it == nameByGuidHash.end()) {
            QNDEBUG(
                "synchronization:saved_search_conflict",
                "Found no "
                    << "duplicate of the remote saved search by guid, "
                    << "adding new saved search to the local storage");

            SavedSearch search(m_remoteSavedSearch);
            search.setDirty(false);
            search.setLocal(false);

            m_addSavedSearchRequestId = QUuid::createUuid();
            QNTRACE(
                "synchronization:saved_search_conflict",
                "Emitting "
                    << "the request to add saved search: request id = "
                    << m_addSavedSearchRequestId
                    << ", saved search: " << search);
            Q_EMIT addSavedSearch(search, m_addSavedSearchRequestId);
        }
        else {
            QNDEBUG(
                "synchronization:saved_search_conflict",
                "The duplicate by "
                    << "guid exists in the local storage, updating it with "
                    << "the state of the remote saved search");

            SavedSearch search(m_localConflict);
            search.qevercloudSavedSearch() = m_remoteSavedSearch;
            search.setDirty(false);
            search.setLocal(false);

            m_updateSavedSearchRequestId = QUuid::createUuid();

            QNTRACE(
                "synchronization:saved_search_conflict",
                "Emitting "
                    << "the request to update saved search: request id = "
                    << m_updateSavedSearchRequestId
                    << ", saved search: " << search);
            Q_EMIT updateSavedSearch(search, m_updateSavedSearchRequestId);
        }
    }
    else if (m_state == State::PendingRemoteSavedSearchAdoptionInLocalStorage) {
        QNDEBUG(
            "synchronization:saved_search_conflict",
            "Successfully "
                << "finalized the sequence of actions required for resolving "
                << "the conflict of saved searches");
        Q_EMIT finished(m_remoteSavedSearch);
    }
    else {
        ErrorString error(
            QT_TR_NOOP("Internal eerror: wrong state on receiving "
                       "the confirmation about the saved search "
                       "update from the local storage"));
        QNWARNING(
            "synchronization:saved_search_conflict",
            error << ", saved search: " << search);
        Q_EMIT failure(m_remoteSavedSearch, error);
    }
}

void SavedSearchSyncConflictResolver::onUpdateSavedSearchFailed(
    SavedSearch search, ErrorString errorDescription, QUuid requestId)
{
    if (requestId != m_updateSavedSearchRequestId) {
        return;
    }

    QNDEBUG(
        "synchronization:saved_search_conflict",
        "SavedSearchSyncConflictResolver::onUpdateSavedSearchFailed: "
            << "request id = " << requestId << ", error description = "
            << errorDescription << " saved search: " << search);

    Q_EMIT failure(m_remoteSavedSearch, errorDescription);
}

void SavedSearchSyncConflictResolver::onFindSavedSearchComplete(
    SavedSearch search, QUuid requestId)
{
    if (requestId != m_findSavedSearchRequestId) {
        return;
    }

    QNDEBUG(
        "synchronization:saved_search_conflict",
        "SavedSearchSyncConflictResolver::onFindSavedSearchComplete: "
            << "request id = " << requestId << ", saved search: " << search);

    m_findSavedSearchRequestId = QUuid();

    // Found the saved search duplicate by name
    processSavedSearchesConflictByName(search);
}

void SavedSearchSyncConflictResolver::onFindSavedSearchFailed(
    SavedSearch search, ErrorString errorDescription, QUuid requestId)
{
    if (requestId != m_findSavedSearchRequestId) {
        return;
    }

    QNDEBUG(
        "synchronization:saved_search_conflict",
        "SavedSearchSyncConflictResolver::onFindSavedSearchFailed: "
            << "request id = " << requestId << ", error description = "
            << errorDescription << ", saved search: " << search);

    m_findSavedSearchRequestId = QUuid();

    // Found no duplicate saved search by name, can override the local changes
    // with the remote changes
    overrideLocalChangesWithRemoteChanges();
}

void SavedSearchSyncConflictResolver::onCacheFilled()
{
    QNDEBUG(
        "synchronization:saved_search_conflict",
        "SavedSearchSyncConflictResolver::onCacheFilled");

    if (!m_pendingCacheFilling) {
        QNDEBUG(
            "synchronization:saved_search_conflict",
            "Not pending "
                << "the cache filling");
        return;
    }

    m_pendingCacheFilling = false;

    if (m_state == State::PendingConflictingSavedSearchRenaming) {
        renameConflictingLocalSavedSearch(m_savedSearchToBeRenamed);
    }
    else {
        ErrorString error(
            QT_TR_NOOP("Internal error: wrong state on receiving the saved "
                       "search info cache filling notification"));
        QNWARNING(
            "synchronization:saved_search_conflict",
            error << ", state = " << m_state);
        Q_EMIT failure(m_remoteSavedSearch, error);
    }
}

void SavedSearchSyncConflictResolver::onCacheFailed(
    ErrorString errorDescription)
{
    QNDEBUG(
        "synchronization:saved_search_conflict",
        "SavedSearchSyncConflictResolver::onCacheFailed: " << errorDescription);

    if (!m_pendingCacheFilling) {
        QNDEBUG(
            "synchronization:saved_search_conflict",
            "Not pending "
                << "the cache filling");
        return;
    }

    m_pendingCacheFilling = false;
    Q_EMIT failure(m_remoteSavedSearch, errorDescription);
}

void SavedSearchSyncConflictResolver::connectToLocalStorage()
{
    QNDEBUG(
        "synchronization:saved_search_conflict",
        "SavedSearchSyncConflictResolver::connectToLocalStorage");

    // Connect local signals to local storage manager async's slots
    QObject::connect(
        this, &SavedSearchSyncConflictResolver::addSavedSearch,
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::onAddSavedSearchRequest,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        this, &SavedSearchSyncConflictResolver::updateSavedSearch,
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::onUpdateSavedSearchRequest,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        this, &SavedSearchSyncConflictResolver::findSavedSearch,
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::onFindSavedSearchRequest,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    // Connect local storage manager async's signals to local slots
    QObject::connect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::addSavedSearchComplete, this,
        &SavedSearchSyncConflictResolver::onAddSavedSearchComplete,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::addSavedSearchFailed, this,
        &SavedSearchSyncConflictResolver::onAddSavedSearchFailed,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::updateSavedSearchComplete, this,
        &SavedSearchSyncConflictResolver::onUpdateSavedSearchComplete,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::updateSavedSearchFailed, this,
        &SavedSearchSyncConflictResolver::onUpdateSavedSearchFailed,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::findSavedSearchComplete, this,
        &SavedSearchSyncConflictResolver::onFindSavedSearchComplete,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::findSavedSearchFailed, this,
        &SavedSearchSyncConflictResolver::onFindSavedSearchFailed,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));
}

void SavedSearchSyncConflictResolver::processSavedSearchesConflictByGuid()
{
    QNDEBUG(
        "synchronization:saved_search_conflict",
        "SavedSearchSyncConflictResolver::processSavedSearchesConflictByGuid");

    // Need to understand whether there's a duplicate by name in the local
    // storage for the new state of the remote saved search

    if (m_cache.isFilled()) {
        const auto & guidByNameHash = m_cache.guidByNameHash();
        auto it = guidByNameHash.find(m_remoteSavedSearch.name.ref().toLower());
        if (it == guidByNameHash.end()) {
            QNDEBUG(
                "synchronization:saved_search_conflict",
                "As deduced by "
                    << "the existing tag info cache, there is no local tag "
                       "with "
                    << "the same name as the name from the new state of the "
                       "remote "
                    << "tag, can safely override the local changes with "
                    << "remote changes: " << m_remoteSavedSearch);
            overrideLocalChangesWithRemoteChanges();
            return;
        }
        /**
         * NOTE: no else block because even if we know the duplicate saved
         * search by name exists, we still need to have its full state in order
         * to rename it
         */
    }

    SavedSearch dummySearch;
    dummySearch.unsetLocalUid();
    dummySearch.setName(m_remoteSavedSearch.name.ref());

    m_findSavedSearchRequestId = QUuid::createUuid();

    QNTRACE(
        "synchronization:saved_search_conflict",
        "Emitting the request to "
            << "find saved search: request id = " << m_findSavedSearchRequestId
            << ", saved search: " << dummySearch);
    Q_EMIT findSavedSearch(dummySearch, m_findSavedSearchRequestId);
}

void SavedSearchSyncConflictResolver::processSavedSearchesConflictByName(
    const SavedSearch & localConflict)
{
    QNDEBUG(
        "synchronization:saved_search_conflict",
        "SavedSearchSyncConflictResolver::processSavedSearchesConflictByName: "
            << "local conflict = " << localConflict);

    if (localConflict.hasGuid() &&
        (localConflict.guid() == m_remoteSavedSearch.guid.ref()))
    {
        QNDEBUG(
            "synchronization:saved_search_conflict",
            "The conflicting "
                << "saved searches match by name and guid => the changes from "
                << "the remote saved search should override the local changes");
        overrideLocalChangesWithRemoteChanges();
        return;
    }

    QNDEBUG(
        "synchronization:saved_search_conflict",
        "The conflicting saved "
            << "searches match by name but not by guid => should rename the "
               "local "
            << "conflicting saved search to \"free\" the name it occupies");

    m_state = State::PendingConflictingSavedSearchRenaming;

    if (!m_cache.isFilled()) {
        QNDEBUG(
            "synchronization:saved_search_conflict",
            "The cache of saved "
                << "search info has not been filled yet");

        QObject::connect(
            &m_cache, &SavedSearchSyncCache::filled, this,
            &SavedSearchSyncConflictResolver::onCacheFilled);

        QObject::connect(
            &m_cache, &SavedSearchSyncCache::failure, this,
            &SavedSearchSyncConflictResolver::onCacheFailed);

        QObject::connect(
            this, &SavedSearchSyncConflictResolver::fillSavedSearchesCache,
            &m_cache, &SavedSearchSyncCache::fill);

        m_pendingCacheFilling = true;
        m_savedSearchToBeRenamed = localConflict;

        QNTRACE(
            "synchronization:saved_search_conflict",
            "Emitting the request "
                << "to fill the saved searches cache");
        Q_EMIT fillSavedSearchesCache();
        return;
    }

    QNDEBUG(
        "synchronization:saved_search_conflict",
        "The cache of saved "
            << "search info has already been filled");
    renameConflictingLocalSavedSearch(localConflict);
}

void SavedSearchSyncConflictResolver::overrideLocalChangesWithRemoteChanges()
{
    QNDEBUG(
        "synchronization:saved_search_conflict",
        "SavedSearchSyncConflictResolver::"
        "overrideLocalChangesWithRemoteChanges");

    m_state = State::OverrideLocalChangesWithRemoteChanges;

    SavedSearch search(m_localConflict);
    search.qevercloudSavedSearch() = m_remoteSavedSearch;
    search.setDirty(false);
    search.setLocal(false);

    m_updateSavedSearchRequestId = QUuid::createUuid();

    QNTRACE(
        "synchronization:saved_search_conflict",
        "Emitting the request to "
            << "update saved search: request id = "
            << m_updateSavedSearchRequestId << " saved search: " << search);
    Q_EMIT updateSavedSearch(search, m_updateSavedSearchRequestId);
}

void SavedSearchSyncConflictResolver::renameConflictingLocalSavedSearch(
    const SavedSearch & localConflict)
{
    QNDEBUG(
        "synchronization:saved_search_conflict",
        "SavedSearchSyncConflictResolver::renameConflictingLocalSavedSearch: "
            << "local conflict = " << localConflict);

    QString name =
        (localConflict.hasName() ? localConflict.name()
                                 : m_remoteSavedSearch.name.ref());

    const auto & guidByNameHash = m_cache.guidByNameHash();

    QString conflictingName = name + QStringLiteral(" - ") + tr("conflicting");

    int suffix = 1;
    QString currentName = conflictingName;
    auto it = guidByNameHash.find(currentName.toLower());
    while (it != guidByNameHash.end()) {
        currentName = conflictingName + QStringLiteral(" (") +
            QString::number(suffix) + QStringLiteral(")");
        ++suffix;
        it = guidByNameHash.find(currentName.toLower());
    }

    conflictingName = currentName;

    SavedSearch search(localConflict);
    search.setName(conflictingName);
    search.setDirty(true);

    m_updateSavedSearchRequestId = QUuid::createUuid();

    QNTRACE(
        "synchronization:saved_search_conflict",
        "Emitting the request to "
            << "update saved search: request id = "
            << m_updateSavedSearchRequestId << ", saved search: " << search);
    Q_EMIT updateSavedSearch(search, m_updateSavedSearchRequestId);
}

QDebug & operator<<(
    QDebug & dbg, const SavedSearchSyncConflictResolver::State state)
{
    using State = SavedSearchSyncConflictResolver::State;

    switch (state) {
    case State::Undefined:
        dbg << "Undefined";
        break;
    case State::OverrideLocalChangesWithRemoteChanges:
        dbg << "Override local changes with remote changes";
        break;
    case State::PendingConflictingSavedSearchRenaming:
        dbg << "Pending conflicting saved search renaming";
        break;
    case State::PendingRemoteSavedSearchAdoptionInLocalStorage:
        dbg << "Pending remote saved search adoption in local storage";
        break;
    default:
        dbg << "Unknown (" << static_cast<qint64>(state) << ")";
        break;
    }

    return dbg;
}

} // namespace quentier
