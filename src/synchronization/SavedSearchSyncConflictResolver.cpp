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

#include "SavedSearchSyncConflictResolver.h"
#include "SavedSearchSyncConflictResolutionCache.h"
#include <quentier/local_storage/LocalStorageManagerAsync.h>
#include <quentier/logging/QuentierLogger.h>

namespace quentier {

SavedSearchSyncConflictResolver::SavedSearchSyncConflictResolver(const qevercloud::SavedSearch & remoteSavedSearch,
                                                                 const SavedSearch & localConflict,
                                                                 SavedSearchSyncConflictResolutionCache & cache,
                                                                 LocalStorageManagerAsync & localStorageManagerAsync,
                                                                 QObject * parent) :
    QObject(parent),
    m_cache(cache),
    m_localStorageManagerAsync(localStorageManagerAsync),
    m_remoteSavedSearch(remoteSavedSearch),
    m_localConflict(localConflict),
    m_savedSearchToBeRenamed(),
    m_state(State::Undefined),
    m_addSavedSearchRequestId(),
    m_updateSavedSearchRequestId(),
    m_findSavedSearchRequestId(),
    m_started(false),
    m_pendingCacheFilling(false)
{}

void SavedSearchSyncConflictResolver::start()
{
    QNDEBUG(QStringLiteral("SavedSearchSyncConflictResolver::start"));

    if (m_started) {
        QNDEBUG(QStringLiteral("Already started"));
        return;
    }

    m_started = true;

    if (Q_UNLIKELY(!m_remoteSavedSearch.guid.isSet())) {
        ErrorString error(QT_TR_NOOP("Can't resolve the conflict between remote and local saved searches: "
                                     "the remote saved search has no guid set"));
        QNWARNING(error << QStringLiteral(": ") << m_remoteSavedSearch);
        emit failure(m_remoteSavedSearch, error);
        return;
    }

    if (Q_UNLIKELY(!m_remoteSavedSearch.name.isSet())) {
        ErrorString error(QT_TR_NOOP("Can't resolve the conflict between remote and local saved searches: "
                                     "the remote saved search has no name set"));
        QNWARNING(error << QStringLiteral(": ") << m_remoteSavedSearch);
        emit failure(m_remoteSavedSearch, error);
        return;
    }

    if (Q_UNLIKELY(!m_localConflict.hasGuid() && !m_localConflict.hasName())) {
        ErrorString error(QT_TR_NOOP("Can't resolve the conflict between remote and local saved searches: "
                                     "the local conflicting saved search has neither guid not name set"));
        QNWARNING(error << QStringLiteral(": ") << m_localConflict);
        emit failure(m_remoteSavedSearch, error);
        return;
    }

    connectToLocalStorage();

    if (m_localConflict.hasName() && (m_localConflict.name() == m_remoteSavedSearch.name.ref())) {
        processSavedSearchesConflictByName(m_localConflict);
    }
    else {
        processSavedSearchesConflictByGuid();
    }
}

void SavedSearchSyncConflictResolver::onAddSavedSearchComplete(SavedSearch search, QUuid requestId)
{
    if (requestId != m_addSavedSearchRequestId) {
        return;
    }

    QNDEBUG(QStringLiteral("SavedSearchSyncConflictResolver::onAddSavedSearchComplete: requets id = ")
            << requestId << QStringLiteral(", saved search: ") << search);

    if (m_state == State::PendingRemoteSavedSearchAdoptionInLocalStorage)
    {
        QNDEBUG(QStringLiteral("Successfully added the remote saved search to the local storage"));
        emit finished(m_remoteSavedSearch);
    }
    else
    {
        ErrorString error(QT_TR_NOOP("Internal error: wrong state on receiving the confirmation about the saved search addition "
                                     "from the local storage"));
        QNWARNING(error << QStringLiteral(", saved search: ") << search);
        emit failure(m_remoteSavedSearch, error);
    }
}

void SavedSearchSyncConflictResolver::onAddSavedSearchFailed(SavedSearch search, ErrorString errorDescription, QUuid requestId)
{
    if (requestId != m_addSavedSearchRequestId) {
        return;
    }

    QNDEBUG(QStringLiteral("SavedSearchSyncConflictResolver::onAddSavedSearchFailed: request id = ")
            << requestId << QStringLiteral(", error description = ") << errorDescription
            << QStringLiteral("; saved search: ") << search);

    emit failure(m_remoteSavedSearch, errorDescription);
}

void SavedSearchSyncConflictResolver::onUpdateSavedSearchComplete(SavedSearch search, QUuid requestId)
{
    if (requestId != m_updateSavedSearchRequestId) {
        return;
    }

    QNDEBUG(QStringLiteral("SavedSearchSyncConflictResolver::onUpdateSavedSearchComplete: request id = ")
            << requestId << QStringLiteral(", saved search: ") << search);

    if (m_state == State::OverrideLocalChangesWithRemoteChanges)
    {
        QNDEBUG(QStringLiteral("Successfully overridden the local changes with remote changes"));
        emit finished(m_remoteSavedSearch);
        return;
    }
    else if (m_state == State::PendingConflictingSavedSearchRenaming)
    {
        QNDEBUG(QStringLiteral("Successfully renamed the local saved search conflicting by name with the remote search"));

        // Now need o find the duplicate of the remote saved search by guid:
        // 1) if one exists, update it from the remote changes - notwithstanding its "dirty" state
        // 2) if one doesn't exist, add it to the local storage

        // The cache should have been filled by that moment, otherwise how could the local saved search conflicting by
        // name be renamed properly?
        if (Q_UNLIKELY(!m_cache.isFilled())) {
            ErrorString error(QT_TR_NOOP("Internal error: the cache of saved search info is not filled while it should have been"));
            QNWARNING(error);
            emit failure(m_remoteSavedSearch, error);
            return;
        }

        m_state = State::PendingRemoteSavedSearchAdoptionInLocalStorage;

        const QHash<QString,QString> & nameByGuidHash = m_cache.nameByGuidHash();
        auto it = nameByGuidHash.find(m_remoteSavedSearch.guid.ref());
        if (it == nameByGuidHash.end())
        {
            QNDEBUG(QStringLiteral("Found no duplicate of the remote saved search by guid, adding new saved search to the local storage"));

            SavedSearch search(m_remoteSavedSearch);
            search.setDirty(false);
            search.setLocal(false);

            m_addSavedSearchRequestId = QUuid::createUuid();
            QNTRACE(QStringLiteral("Emitting the request to add saved search: request id = ") << m_addSavedSearchRequestId
                    << QStringLiteral(", saved search: ") << search);
            emit addSavedSearch(search, m_addSavedSearchRequestId);
        }
        else
        {
            QNDEBUG(QStringLiteral("The duplicate by guid exists in the local storage, updating it with the state "
                                   "of the remote saved search"));

            SavedSearch search(m_remoteSavedSearch);
            search.setDirty(false);
            search.setLocal(false);

            m_updateSavedSearchRequestId = QUuid::createUuid();
            QNTRACE(QStringLiteral("Emitting the request to update saved search: request id = ") << m_updateSavedSearchRequestId
                    << QStringLiteral(", saved search: ") << search);
            emit updateSavedSearch(search, m_updateSavedSearchRequestId);
        }
    }
    else if (m_state == State::PendingRemoteSavedSearchAdoptionInLocalStorage)
    {
        QNDEBUG(QStringLiteral("Successfully finalized the sequence of actions required for resolving the conflict of saved searches"));
        emit finished(m_remoteSavedSearch);
    }
    else
    {
        ErrorString error(QT_TR_NOOP("Internal eerror: wrong state on receiving the confirmation about the saved search update "
                                     "from the local storage"));
        QNWARNING(error << QStringLiteral(", saved search: ") << search);
        emit failure(m_remoteSavedSearch, error);
    }
}

void SavedSearchSyncConflictResolver::onUpdateSavedSearchFailed(SavedSearch search, ErrorString errorDescription, QUuid requestId)
{
    if (requestId != m_updateSavedSearchRequestId) {
        return;
    }

    QNDEBUG(QStringLiteral("SavedSearchSyncConflictResolver::onUpdateSavedSearchFailed: request id = ")
            << requestId << QStringLiteral(", error description = ") << errorDescription
            << QStringLiteral(" saved search: ") << search);

    emit failure(m_remoteSavedSearch, errorDescription);
}

void SavedSearchSyncConflictResolver::onFindSavedSearchComplete(SavedSearch search, QUuid requestId)
{
    if (requestId != m_findSavedSearchRequestId) {
        return;
    }

    QNDEBUG(QStringLiteral("SavedSearchSyncConflictResolver::onFindSavedSearchComplete: request id = ")
            << requestId << QStringLiteral(", saved search: ") << search);

    m_findSavedSearchRequestId = QUuid();

    // Found the saved search duplicate by name
    processSavedSearchesConflictByName(search);
}

void SavedSearchSyncConflictResolver::onFindSavedSearchFailed(SavedSearch search, ErrorString errorDescription, QUuid requestId)
{
    if (requestId != m_findSavedSearchRequestId) {
        return;
    }

    QNDEBUG(QStringLiteral("SavedSearchSyncConflictResolver::onFindSavedSearchFailed: request id = ")
            << requestId << QStringLiteral(", error description = ") << errorDescription
            << QStringLiteral(", saved search: ") << search);

    m_findSavedSearchRequestId = QUuid();

    // Found no duplicate saved search by name, can override the local changes with the remote changes
    overrideLocalChangesWithRemoteChanges();
}

void SavedSearchSyncConflictResolver::onCacheFilled()
{
    QNDEBUG(QStringLiteral("SavedSearchSyncConflictResolver::onCacheFilled"));

    if (!m_pendingCacheFilling) {
        QNDEBUG(QStringLiteral("Not pending the cache filling"));
        return;
    }

    m_pendingCacheFilling = false;

    if (m_state == State::PendingConflictingSavedSearchRenaming)
    {
        renameConflictingLocalSavedSearch(m_savedSearchToBeRenamed);
    }
    else
    {
        ErrorString error(QT_TR_NOOP("Internal error: wrong state on receiving the saved search info cache filling notification"));
        QNWARNING(error << QStringLiteral(", state = ") << m_state);
        emit failure(m_remoteSavedSearch, error);
    }
}

void SavedSearchSyncConflictResolver::onCacheFailed(ErrorString errorDescription)
{
    QNDEBUG(QStringLiteral("SavedSearchSyncConflictResolver::onCacheFailed: ") << errorDescription);

    if (!m_pendingCacheFilling) {
        QNDEBUG(QStringLiteral("Not pending the cache filling"));
        return;
    }

    m_pendingCacheFilling = false;
    emit failure(m_remoteSavedSearch, errorDescription);
}

void SavedSearchSyncConflictResolver::connectToLocalStorage()
{
    QNDEBUG(QStringLiteral("SavedSearchSyncConflictResolver::connectToLocalStorage"));

    // Connect local signals to local storage manager async's slots
    QObject::connect(this, QNSIGNAL(SavedSearchSyncConflictResolver,addSavedSearch,SavedSearch,QUuid),
                     &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onAddSavedSearchRequest,SavedSearch,QUuid));
    QObject::connect(this, QNSIGNAL(SavedSearchSyncConflictResolver,updateSavedSearch,SavedSearch,QUuid),
                     &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onUpdateSavedSearchRequest,SavedSearch,QUuid));
    QObject::connect(this, QNSIGNAL(SavedSearchSyncConflictResolver,findSavedSearch,SavedSearch,QUuid),
                     &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onFindSavedSearchRequest,SavedSearch,QUuid));

    // Connect local storage manager async's signals to local slots
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addSavedSearchComplete,SavedSearch,QUuid),
                     this, QNSLOT(SavedSearchSyncConflictResolver,onAddSavedSearchComplete,SavedSearch,QUuid));
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addSavedSearchFailed,SavedSearch,ErrorString,QUuid),
                     this, QNSLOT(SavedSearchSyncConflictResolver,onAddSavedSearchFailed,SavedSearch,ErrorString,QUuid));
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateSavedSearchComplete,SavedSearch,QUuid),
                     this, QNSLOT(SavedSearchSyncConflictResolver,onUpdateSavedSearchComplete,SavedSearch,QUuid));
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateSavedSearchFailed,SavedSearch,ErrorString,QUuid),
                     this, QNSLOT(SavedSearchSyncConflictResolver,onUpdateSavedSearchFailed,SavedSearch,ErrorString,QUuid));
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findSavedSearchComplete,SavedSearch,QUuid),
                     this, QNSLOT(SavedSearchSyncConflictResolver,onFindSavedSearchComplete,SavedSearch,QUuid));
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findSavedSearchFailed,SavedSearch,ErrorString,QUuid),
                     this, QNSLOT(SavedSearchSyncConflictResolver,onFindSavedSearchFailed,SavedSearch,ErrorString,QUuid));
}

void SavedSearchSyncConflictResolver::processSavedSearchesConflictByGuid()
{
    QNDEBUG(QStringLiteral("SavedSearchSyncConflictResolver::processSavedSearchesConflictByGuid"));

    // Need to understand whether there's a duplicate by name in the local storage for the new state
    // of the remote saved search

    if (m_cache.isFilled())
    {
        const QHash<QString,QString> & guidByNameHash = m_cache.guidByNameHash();
        auto it = guidByNameHash.find(m_remoteSavedSearch.name.ref().toLower());
        if (it == guidByNameHash.end()) {
            QNDEBUG(QStringLiteral("As deduced by the existing tag info cache, there is no local tag with the same name "
                                   "as the name from the new state of the remote tag, can safely override the local changes "
                                   "with the remote changes: ") << m_remoteSavedSearch);
            overrideLocalChangesWithRemoteChanges();
            return;
        }
        // NOTE: no else block because even if we know the duplicate saved search by name exists, we still need to have its full
        // state in order to rename it
    }

    SavedSearch dummySearch;
    dummySearch.unsetLocalUid();
    dummySearch.setName(m_remoteSavedSearch.name.ref());
    m_findSavedSearchRequestId = QUuid::createUuid();
    QNTRACE(QStringLiteral("Emitting the request to find saved search: request id = ") << m_findSavedSearchRequestId
            << QStringLiteral(", saved search: ") << dummySearch);
    emit findSavedSearch(dummySearch, m_findSavedSearchRequestId);
}

void SavedSearchSyncConflictResolver::processSavedSearchesConflictByName(const SavedSearch & localConflict)
{
    QNDEBUG(QStringLiteral("SavedSearchSyncConflictResolver::processSavedSearchesConflictByName: local conflict = ") << localConflict);

    if (localConflict.hasGuid() && (localConflict.guid() == m_remoteSavedSearch.guid.ref())) {
        QNDEBUG(QStringLiteral("The conflicting saved searches match by name and guid => the changes from the remote "
                               "saved search should override the local changes"));
        overrideLocalChangesWithRemoteChanges();
        return;
    }

    QNDEBUG(QStringLiteral("The conflicting saved searches match by name but not by guid => should rename "
                           "the local conflicting saved search to \"free\" the name it occupies"));

    m_state = State::PendingConflictingSavedSearchRenaming;

    if (!m_cache.isFilled())
    {
        QNDEBUG(QStringLiteral("The cache of saved search info has not been filled yet"));

        QObject::connect(&m_cache, QNSIGNAL(SavedSearchSyncConflictResolutionCache,filled),
                         this, QNSLOT(SavedSearchSyncConflictResolver,onCacheFilled));
        QObject::connect(&m_cache, QNSIGNAL(SavedSearchSyncConflictResolutionCache,failure,ErrorString),
                         this, QNSLOT(SavedSearchSyncConflictResolver,onCacheFailed,ErrorString));
        QObject::connect(this, QNSIGNAL(SavedSearchSyncConflictResolver,fillSavedSearchesCache),
                         &m_cache, QNSLOT(SavedSearchSyncConflictResolutionCache,fill));

        m_pendingCacheFilling = true;
        m_savedSearchToBeRenamed = localConflict;
        QNTRACE(QStringLiteral("Emitting the request to fill the saved searches cache"));
        emit fillSavedSearchesCache();
        return;
    }

    QNDEBUG(QStringLiteral("The cache of saved search info has already been filled"));
    renameConflictingLocalSavedSearch(localConflict);
}

void SavedSearchSyncConflictResolver::overrideLocalChangesWithRemoteChanges()
{
    QNDEBUG(QStringLiteral("SavedSearchSyncConflictResolver::overrideLocalChangesWithRemoteChanges"));

    m_state = State::OverrideLocalChangesWithRemoteChanges;

    SavedSearch search(m_localConflict);
    search.qevercloudSavedSearch() = m_remoteSavedSearch;
    search.setDirty(false);
    search.setLocal(false);

    m_updateSavedSearchRequestId = QUuid::createUuid();
    QNTRACE(QStringLiteral("Emitting the request to update saved search: request id = ")
            << m_updateSavedSearchRequestId << QStringLiteral(" saved search: ") << search);
    emit updateSavedSearch(search, m_updateSavedSearchRequestId);
}

void SavedSearchSyncConflictResolver::renameConflictingLocalSavedSearch(const SavedSearch & localConflict)
{
    QNDEBUG(QStringLiteral("SavedSearchSyncConflictResolver::renameConflictingLocalSavedSearch: local conflict = ") << localConflict);

    QString name = (localConflict.hasName() ? localConflict.name() : m_remoteSavedSearch.name.ref());

    const QHash<QString,QString> & guidByNameHash = m_cache.guidByNameHash();

    QString conflictingName = name + QStringLiteral(" - ") + tr("conflicting");

    int suffix = 1;
    QString currentName = conflictingName;
    auto it = guidByNameHash.find(currentName.toLower());
    while(it != guidByNameHash.end()) {
        currentName = conflictingName + QStringLiteral(" (") + QString::number(suffix) + QStringLiteral(")");
        ++suffix;
        it = guidByNameHash.find(currentName.toLower());
    }

    conflictingName = currentName;

    SavedSearch search(localConflict);
    search.setName(conflictingName);
    search.setDirty(true);

    m_updateSavedSearchRequestId = QUuid::createUuid();
    QNTRACE(QStringLiteral("Emitting the request to update saved search: request id = ")
            << m_updateSavedSearchRequestId << QStringLiteral(", saved search: ") << search);
    emit updateSavedSearch(search, m_updateSavedSearchRequestId);
}

} // namespace quentier
