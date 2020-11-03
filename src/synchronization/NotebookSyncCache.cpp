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

#include "NotebookSyncCache.h"

#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/Compat.h>

#define __NCLOG_BASE(message, level)                                           \
    if (m_linkedNotebookGuid.isEmpty()) {                                      \
        __QNLOG_BASE("synchronization:notebook_cache", message, level);        \
    }                                                                          \
    else {                                                                     \
        __QNLOG_BASE(                                                          \
            "synchronization:notebook_cache",                                  \
            "[linked notebook " << m_linkedNotebookGuid << "]: " << message,   \
            level);                                                            \
    }

#define NCTRACE(message) __NCLOG_BASE(message, Trace)

#define NCDEBUG(message) __NCLOG_BASE(message, Debug)

#define NCWARNING(message) __NCLOG_BASE(message, Warning)

namespace quentier {

NotebookSyncCache::NotebookSyncCache(
    LocalStorageManagerAsync & localStorageManagerAsync,
    const QString & linkedNotebookGuid, QObject * parent) :
    QObject(parent),
    m_localStorageManagerAsync(localStorageManagerAsync),
    m_linkedNotebookGuid(linkedNotebookGuid)
{}

void NotebookSyncCache::clear()
{
    NCDEBUG("NotebookSyncCache::clear");

    disconnectFromLocalStorage();

    m_notebookNameByLocalUid.clear();
    m_notebookNameByGuid.clear();
    m_notebookGuidByName.clear();
    m_dirtyNotebooksByGuid.clear();

    m_listNotebooksRequestId = QUuid();
    m_offset = 0;
}

bool NotebookSyncCache::isFilled() const
{
    if (!m_connectedToLocalStorage) {
        return false;
    }

    if (m_listNotebooksRequestId.isNull()) {
        return true;
    }

    return false;
}

void NotebookSyncCache::fill()
{
    NCDEBUG("NotebookSyncCache::fill");

    if (m_connectedToLocalStorage) {
        NCDEBUG(
            "Already connected to the local storage, "
            << "no need to do anything");
        return;
    }

    connectToLocalStorage();
    requestNotebooksList();
}

void NotebookSyncCache::onListNotebooksComplete(
    LocalStorageManager::ListObjectsOptions flag, size_t limit, size_t offset,
    LocalStorageManager::ListNotebooksOrder order,
    LocalStorageManager::OrderDirection orderDirection,
    QString linkedNotebookGuid, QList<Notebook> foundNotebooks, QUuid requestId)
{
    if (requestId != m_listNotebooksRequestId) {
        return;
    }

    NCDEBUG(
        "NotebookSyncCache::onListNotebooksComplete: flag = "
        << flag << ", limit = " << limit << ", offset = " << offset
        << ", order = " << order << ", order direction = " << orderDirection
        << ", linked notebook guid = " << linkedNotebookGuid
        << ", request id = " << requestId);

    for (const auto & notebook: qAsConst(foundNotebooks)) {
        processNotebook(notebook);
    }

    m_listNotebooksRequestId = QUuid();

    if (foundNotebooks.size() == static_cast<int>(limit)) {
        NCTRACE(
            "The number of found notebooks matches the limit, "
            << "requesting more notebooks from the local storage");
        m_offset += limit;
        requestNotebooksList();
        return;
    }

    Q_EMIT filled();
}

void NotebookSyncCache::onListNotebooksFailed(
    LocalStorageManager::ListObjectsOptions flag, size_t limit, size_t offset,
    LocalStorageManager::ListNotebooksOrder order,
    LocalStorageManager::OrderDirection orderDirection,
    QString linkedNotebookGuid, ErrorString errorDescription, QUuid requestId)
{
    if (requestId != m_listNotebooksRequestId) {
        return;
    }

    NCDEBUG(
        "NotebookSyncCache::onListNotebooksFailed: flag = "
        << flag << ", limit = " << limit << ", offset = " << offset
        << ", order = " << order << ", order direction = " << orderDirection
        << ", linked notebook guid = " << linkedNotebookGuid
        << ", error description = " << errorDescription
        << ", request id = " << requestId);

    NCWARNING(
        "Failed to cache the notebook information required "
        << "for the sync: " << errorDescription);

    m_notebookNameByLocalUid.clear();
    m_notebookNameByGuid.clear();
    m_notebookGuidByName.clear();
    m_dirtyNotebooksByGuid.clear();
    disconnectFromLocalStorage();

    Q_EMIT failure(errorDescription);
}

void NotebookSyncCache::onAddNotebookComplete(
    Notebook notebook, QUuid requestId)
{
    NCDEBUG(
        "NotebookSyncCache::onAddNotebookComplete: request id = "
        << requestId << ", notebook: " << notebook);

    processNotebook(notebook);
}

void NotebookSyncCache::onUpdateNotebookComplete(
    Notebook notebook, QUuid requestId)
{
    NCDEBUG(
        "NotebookSyncCache::onUpdateNotebookComplete: request id = "
        << requestId << ", notebook: " << notebook);

    removeNotebook(notebook.localUid());
    processNotebook(notebook);
}

void NotebookSyncCache::onExpungeNotebookComplete(
    Notebook notebook, QUuid requestId)
{
    NCDEBUG(
        "NotebookSyncCache::onExpungeNotebookComplete: request id = "
        << requestId << ", notebook: " << notebook);

    removeNotebook(notebook.localUid());
}

void NotebookSyncCache::connectToLocalStorage()
{
    NCDEBUG("NotebookSyncCache::connectToLocalStorage");

    if (m_connectedToLocalStorage) {
        NCDEBUG("Already connected to the local storage");
        return;
    }

    // Connect local signals to local storage manager async's slots
    QObject::connect(
        this, &NotebookSyncCache::listNotebooks, &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::onListNotebooksRequest,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    // Connect local storage manager async's signals to local slots
    QObject::connect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::listNotebooksComplete, this,
        &NotebookSyncCache::onListNotebooksComplete,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::listNotebooksFailed, this,
        &NotebookSyncCache::onListNotebooksFailed,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::addNotebookComplete, this,
        &NotebookSyncCache::onAddNotebookComplete,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::updateNotebookComplete, this,
        &NotebookSyncCache::onUpdateNotebookComplete,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::expungeNotebookComplete, this,
        &NotebookSyncCache::onExpungeNotebookComplete,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    m_connectedToLocalStorage = true;
}

void NotebookSyncCache::disconnectFromLocalStorage()
{
    NCDEBUG("NotebookSyncCache::disconnectFromLocalStorage");

    if (!m_connectedToLocalStorage) {
        NCDEBUG("Not connected to local storage at the moment");
        return;
    }

    // Disconnect local signals from local storage manager async's slots
    QObject::disconnect(
        this, &NotebookSyncCache::listNotebooks, &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::onListNotebooksRequest);

    // Disconnect local storage manager async's signals from local slots
    QObject::disconnect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::listNotebooksComplete, this,
        &NotebookSyncCache::onListNotebooksComplete);

    QObject::disconnect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::listNotebooksFailed, this,
        &NotebookSyncCache::onListNotebooksFailed);

    QObject::disconnect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::addNotebookComplete, this,
        &NotebookSyncCache::onAddNotebookComplete);

    QObject::disconnect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::updateNotebookComplete, this,
        &NotebookSyncCache::onUpdateNotebookComplete);

    QObject::disconnect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::expungeNotebookComplete, this,
        &NotebookSyncCache::onExpungeNotebookComplete);

    m_connectedToLocalStorage = false;
}

void NotebookSyncCache::requestNotebooksList()
{
    NCDEBUG("NotebookSyncCache::requestNotebooksList");

    m_listNotebooksRequestId = QUuid::createUuid();

    NCTRACE(
        "Emitting the request to list notebooks: request id = "
        << m_listNotebooksRequestId << ", offset = " << m_offset);

    Q_EMIT listNotebooks(
        LocalStorageManager::ListObjectsOption::ListAll, m_limit, m_offset,
        LocalStorageManager::ListNotebooksOrder::NoOrder,
        LocalStorageManager::OrderDirection::Ascending, m_linkedNotebookGuid,
        m_listNotebooksRequestId);
}

void NotebookSyncCache::removeNotebook(const QString & notebookLocalUid)
{
    NCDEBUG(
        "NotebookSyncCache::removeNotebook: local uid = " << notebookLocalUid);

    auto localUidIt = m_notebookNameByLocalUid.find(notebookLocalUid);
    if (Q_UNLIKELY(localUidIt == m_notebookNameByLocalUid.end())) {
        NCDEBUG("The notebook name was not found in the cache by local uid");
        return;
    }

    QString name = localUidIt.value();
    Q_UNUSED(m_notebookNameByLocalUid.erase(localUidIt))

    auto guidIt = m_notebookGuidByName.find(name);
    if (Q_UNLIKELY(guidIt == m_notebookGuidByName.end())) {
        NCDEBUG("The notebook guid was not found in the cache by name");
        return;
    }

    QString guid = guidIt.value();
    Q_UNUSED(m_notebookGuidByName.erase(guidIt))

    auto dirtyNotebookIt = m_dirtyNotebooksByGuid.find(guid);
    if (dirtyNotebookIt != m_dirtyNotebooksByGuid.end()) {
        Q_UNUSED(m_dirtyNotebooksByGuid.erase(dirtyNotebookIt))
    }

    auto nameIt = m_notebookNameByGuid.find(guid);
    if (Q_UNLIKELY(nameIt == m_notebookNameByGuid.end())) {
        NCDEBUG("The notebook name was not found in the cache by guid");
        return;
    }

    Q_UNUSED(m_notebookNameByGuid.erase(nameIt))
}

void NotebookSyncCache::processNotebook(const Notebook & notebook)
{
    NCDEBUG("NotebookSyncCache::processNotebook: " << notebook);

    if (notebook.hasGuid()) {
        if (notebook.isDirty()) {
            m_dirtyNotebooksByGuid[notebook.guid()] = notebook;
        }
        else {
            auto it = m_dirtyNotebooksByGuid.find(notebook.guid());
            if (it != m_dirtyNotebooksByGuid.end()) {
                Q_UNUSED(m_dirtyNotebooksByGuid.erase(it))
            }
        }
    }

    if (!notebook.hasName()) {
        NCDEBUG("Skipping the notebook without a name");
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
