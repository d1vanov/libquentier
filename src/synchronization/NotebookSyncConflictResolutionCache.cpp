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

#include "NotebookSyncConflictResolutionCache.h"
#include <quentier/logging/QuentierLogger.h>

namespace quentier {

NotebookSyncConflictResolutionCache::NotebookSyncConflictResolutionCache(LocalStorageManagerAsync & localStorageManagerAsync) :
    m_localStorageManagerAsync(localStorageManagerAsync),
    m_connectedToLocalStorage(false),
    m_notebookNameByLocalUid(),
    m_notebookNameByGuid(),
    m_notebookGuidByName(),
    m_listNotebooksRequestId(),
    m_limit(20),
    m_offset(0)
{}

void NotebookSyncConflictResolutionCache::clear()
{
    QNDEBUG(QStringLiteral("NotebookSyncConflictResolutionCache::clear"));

    disconnectFromLocalStorage();

    m_notebookNameByLocalUid.clear();
    m_notebookNameByGuid.clear();
    m_notebookGuidByName.clear();

    m_listNotebooksRequestId = QUuid();
    m_offset = 0;
}

bool NotebookSyncConflictResolutionCache::isFilled() const
{
    if (!m_connectedToLocalStorage) {
        return false;
    }

    if (m_listNotebooksRequestId.isNull()) {
        return true;
    }

    return false;
}

void NotebookSyncConflictResolutionCache::fill()
{
    QNDEBUG(QStringLiteral("NotebookSyncConflictResolutionCache::fill"));

    if (m_connectedToLocalStorage) {
        QNDEBUG(QStringLiteral("Already connected to the local storage, no need to do anything"));
        return;
    }

    connectToLocalStorage();
    requestNotebooksList();
}

void NotebookSyncConflictResolutionCache::onListNotebooksComplete(LocalStorageManager::ListObjectsOptions flag,
                                                                  size_t limit, size_t offset,
                                                                  LocalStorageManager::ListNotebooksOrder::type order,
                                                                  LocalStorageManager::OrderDirection::type orderDirection,
                                                                  QString linkedNotebookGuid, QList<Notebook> foundNotebooks,
                                                                  QUuid requestId)
{
    if (requestId != m_listNotebooksRequestId) {
        return;
    }

    QNDEBUG(QStringLiteral("NotebookSyncConflictResolutionCache::onListNotebooksComplete: flag = ")
            << flag << QStringLiteral(", limit = ") << limit << QStringLiteral(", offset = ")
            << offset << QStringLiteral(", order = ") << order << QStringLiteral(", order direction = ")
            << orderDirection << QStringLiteral(", linked notebook guid = ") << linkedNotebookGuid
            << QStringLiteral(", request id = ") << requestId);

    for(auto it = foundNotebooks.constBegin(), end = foundNotebooks.constEnd(); it != end; ++it) {
        processNotebook(*it);
    }

    m_listNotebooksRequestId = QUuid();

    if (foundNotebooks.size() == static_cast<int>(limit)) {
        QNTRACE(QStringLiteral("The number of found notebooks matches the limit, requesting more notebooks from the local storage"));
        m_offset += limit;
        requestNotebooksList();
        return;
    }

    emit filled();
}

void NotebookSyncConflictResolutionCache::onListNotebooksFailed(LocalStorageManager::ListObjectsOptions flag,
                                                                size_t limit, size_t offset,
                                                                LocalStorageManager::ListNotebooksOrder::type order,
                                                                LocalStorageManager::OrderDirection::type orderDirection,
                                                                QString linkedNotebookGuid, ErrorString errorDescription,
                                                                QUuid requestId)
{
    if (requestId != m_listNotebooksRequestId) {
        return;
    }

    QNDEBUG(QStringLiteral("NotebookSyncConflictResolutionCache::onListNotebooksFailed: flag = ")
            << flag << QStringLiteral(", limit = ") << limit << QStringLiteral(", offset = ")
            << offset << QStringLiteral(", order = ") << order << QStringLiteral(", order direction = ")
            << orderDirection << QStringLiteral(", linked notebook guid = ") << linkedNotebookGuid
            << QStringLiteral(", error description = ") << errorDescription
            << QStringLiteral(", request id = ") << requestId);

    QNWARNING(QStringLiteral("Failed to cache the notebook information required for the sync conflicts resolution: ")
              << errorDescription);

    m_notebookNameByLocalUid.clear();
    m_notebookNameByGuid.clear();
    m_notebookGuidByName.clear();
    disconnectFromLocalStorage();

    emit failure(errorDescription);
}

void NotebookSyncConflictResolutionCache::onAddNotebookComplete(Notebook notebook, QUuid requestId)
{
    QNDEBUG(QStringLiteral("NotebookSyncConflictResolutionCache::onAddNotebookComplete: request id = ")
            << requestId << QStringLiteral(", notebook: ") << notebook);

    processNotebook(notebook);
}

void NotebookSyncConflictResolutionCache::onUpdateNotebookComplete(Notebook notebook, QUuid requestId)
{
    QNDEBUG(QStringLiteral("NotebookSyncConflictResolutionCache::onUpdateNotebookComplete: request id = ")
            << requestId << QStringLiteral(", notebook: ") << notebook);

    removeNotebook(notebook.localUid());
    processNotebook(notebook);
}

void NotebookSyncConflictResolutionCache::onExpungeNotebookComplete(Notebook notebook, QUuid requestId)
{
    QNDEBUG(QStringLiteral("NotebookSyncConflictResolutionCache::onExpungeNotebookComplete: request id = ")
            << requestId << QStringLiteral(", notebook: ") << notebook);

    removeNotebook(notebook.localUid());
}

void NotebookSyncConflictResolutionCache::connectToLocalStorage()
{
    QNDEBUG(QStringLiteral("NotebookSyncConflictResolutionCache::connectToLocalStorage"));

    if (m_connectedToLocalStorage) {
        QNDEBUG(QStringLiteral("Already connected to the local storage"));
        return;
    }

    // Connect local signals to local storage manager async's slots
    QObject::connect(this,
                     QNSIGNAL(NotebookSyncConflictResolutionCache,listNotebooks,LocalStorageManager::ListObjectsOptions,
                              size_t,size_t,LocalStorageManager::ListNotebooksOrder::type,
                              LocalStorageManager::OrderDirection::type orderDirection,QString,QUuid),
                     &m_localStorageManagerAsync,
                     QNSLOT(LocalStorageManagerAsync,onListNotebooksRequest,LocalStorageManager::ListObjectsOptions,
                            size_t,size_t,LocalStorageManager::ListNotebooksOrder::type,
                            LocalStorageManager::OrderDirection::type orderDirection,QString,QUuid));

    // Connect local storage manager async's signals to local slots
    QObject::connect(&m_localStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,listNotebooksComplete,
                              LocalStorageManager::ListObjectsOptions,size_t,size_t,
                              LocalStorageManager::ListNotebooksOrder::type,
                              LocalStorageManager::OrderDirection::type,
                              QString,QList<Notebook>,QUuid),
                     this,
                     QNSLOT(NotebookSyncConflictResolutionCache,onListNotebooksComplete,
                            LocalStorageManager::ListObjectsOptions,size_t,size_t,
                            LocalStorageManager::ListNotebooksOrder::type,
                            LocalStorageManager::OrderDirection::type,
                            QString,QList<Notebook>,QUuid));
    QObject::connect(&m_localStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,listNotebooksFailed,
                              LocalStorageManager::ListObjectsOptions,
                              size_t,size_t,LocalStorageManager::ListNotebooksOrder::type,
                              LocalStorageManager::OrderDirection::type,
                              QString,ErrorString,QUuid),
                     this,
                     QNSLOT(NotebookSyncConflictResolutionCache,onListNotebooksFailed,
                            LocalStorageManager::ListObjectsOptions,
                            size_t,size_t,LocalStorageManager::ListNotebooksOrder::type,
                            LocalStorageManager::OrderDirection::type,
                            QString,ErrorString,QUuid));
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addNotebookComplete,Notebook,QUuid),
                     this, QNSLOT(NotebookSyncConflictResolutionCache,onAddNotebookComplete,Notebook,QUuid));
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateNotebookComplete,Notebook,QUuid),
                     this, QNSLOT(NotebookSyncConflictResolutionCache,onUpdateNotebookComplete,Notebook,QUuid));
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeNotebookComplete,Notebook,QUuid),
                     this, QNSLOT(NotebookSyncConflictResolutionCache,onExpungeNotebookComplete,Notebook,QUuid));

    m_connectedToLocalStorage = true;
}

void NotebookSyncConflictResolutionCache::disconnectFromLocalStorage()
{
    QNDEBUG(QStringLiteral("NotebookSyncConflictResolutionCache::disconnectFromLocalStorage"));

    if (!m_connectedToLocalStorage) {
        QNDEBUG(QStringLiteral("Not connected to local storage at the moment"));
        return;
    }

    // Disconnect local signals from local storage manager async's slots
    QObject::disconnect(this,
                        QNSIGNAL(NotebookSyncConflictResolutionCache,listNotebooks,LocalStorageManager::ListObjectsOptions,
                                 size_t,size_t,LocalStorageManager::ListNotebooksOrder::type,
                                 LocalStorageManager::OrderDirection::type orderDirection,QString,QUuid),
                        &m_localStorageManagerAsync,
                        QNSLOT(LocalStorageManagerAsync,onListNotebooksRequest,LocalStorageManager::ListObjectsOptions,
                               size_t,size_t,LocalStorageManager::ListNotebooksOrder::type,
                               LocalStorageManager::OrderDirection::type orderDirection,QString,QUuid));

    // Connect local storage manager async's signals to local slots
    QObject::disconnect(&m_localStorageManagerAsync,
                        QNSIGNAL(LocalStorageManagerAsync,listNotebooksComplete,
                                 LocalStorageManager::ListObjectsOptions,size_t,size_t,
                                 LocalStorageManager::ListNotebooksOrder::type,
                                 LocalStorageManager::OrderDirection::type,
                                 QString,QList<Notebook>,QUuid),
                        this,
                        QNSLOT(NotebookSyncConflictResolutionCache,onListNotebooksComplete,
                               LocalStorageManager::ListObjectsOptions,size_t,size_t,
                               LocalStorageManager::ListNotebooksOrder::type,
                               LocalStorageManager::OrderDirection::type,
                               QString,QList<Notebook>,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync,
                        QNSIGNAL(LocalStorageManagerAsync,listNotebooksFailed,
                                 LocalStorageManager::ListObjectsOptions,
                                 size_t,size_t,LocalStorageManager::ListNotebooksOrder::type,
                                 LocalStorageManager::OrderDirection::type,
                                 QString,ErrorString,QUuid),
                        this,
                        QNSLOT(NotebookSyncConflictResolutionCache,onListNotebooksFailed,
                               LocalStorageManager::ListObjectsOptions,
                               size_t,size_t,LocalStorageManager::ListNotebooksOrder::type,
                               LocalStorageManager::OrderDirection::type,
                               QString,ErrorString,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addNotebookComplete,Notebook,QUuid),
                        this, QNSLOT(NotebookSyncConflictResolutionCache,onAddNotebookComplete,Notebook,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateNotebookComplete,Notebook,QUuid),
                        this, QNSLOT(NotebookSyncConflictResolutionCache,onUpdateNotebookComplete,Notebook,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeNotebookComplete,Notebook,QUuid),
                        this, QNSLOT(NotebookSyncConflictResolutionCache,onExpungeNotebookComplete,Notebook,QUuid));

    m_connectedToLocalStorage = false;
}

void NotebookSyncConflictResolutionCache::requestNotebooksList()
{
    QNDEBUG(QStringLiteral("NotebookSyncConflictResolutionCache::requestNotebooksList"));

    m_listNotebooksRequestId = QUuid::createUuid();

    QNTRACE(QStringLiteral("Emitting the request to list notebooks: request id = ")
            << m_listNotebooksRequestId << QStringLiteral(", offset = ") << m_offset);
    emit listNotebooks(LocalStorageManager::ListAll,
                       m_limit, m_offset, LocalStorageManager::ListNotebooksOrder::NoOrder,
                       LocalStorageManager::OrderDirection::Ascending,
                       QString(), m_listNotebooksRequestId);

}

void NotebookSyncConflictResolutionCache::removeNotebook(const QString & notebookLocalUid)
{
    QNDEBUG(QStringLiteral("NotebookSyncConflictResolutionCache::removeNotebook: local uid = ") << notebookLocalUid);

    auto localUidIt = m_notebookNameByLocalUid.find(notebookLocalUid);
    if (Q_UNLIKELY(localUidIt == m_notebookNameByLocalUid.end())) {
        QNDEBUG(QStringLiteral("The notebook name was not found in the cache by local uid"));
        return;
    }

    QString name = localUidIt.value();
    Q_UNUSED(m_notebookNameByLocalUid.erase(localUidIt))

    auto guidIt = m_notebookGuidByName.find(name);
    if (Q_UNLIKELY(guidIt == m_notebookGuidByName.end())) {
        QNDEBUG(QStringLiteral("The notebook guid was not found in the cache by name"));
        return;
    }

    QString guid = guidIt.value();
    Q_UNUSED(m_notebookGuidByName.erase(guidIt))

    auto nameIt = m_notebookNameByGuid.find(guid);
    if (Q_UNLIKELY(nameIt == m_notebookNameByGuid.end())) {
        QNDEBUG(QStringLiteral("The notebook name was not found in the cache by guid"));
        return;
    }

    Q_UNUSED(m_notebookNameByGuid.erase(nameIt))
}

void NotebookSyncConflictResolutionCache::processNotebook(const Notebook & notebook)
{
    QNDEBUG(QStringLiteral("NotebookSyncConflictResolutionCache::processNotebook: ") << notebook);

    if (!notebook.hasName()) {
        QNDEBUG(QStringLiteral("Skipping the notebook without a name"));
        return;
    }

    QString name = notebook.name().toLower();
    m_notebookNameByLocalUid[notebook.localUid()] = name;

    if (!notebook.hasGuid()) {
        return;
    }

    const QString & guid = notebook.guid();
    m_notebookNameByGuid[guid] = name;
    m_notebookGuidByName[name] = guid;
}

} // namespace quentier
