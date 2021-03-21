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
    QString linkedNotebookGuid, QObject * parent) :
    QObject(parent),
    m_localStorageManagerAsync(localStorageManagerAsync),
    m_linkedNotebookGuid(std::move(linkedNotebookGuid))
{}

void NotebookSyncCache::clear()
{
    NCDEBUG("NotebookSyncCache::clear");

    disconnectFromLocalStorage();

    m_notebookNameByLocalId.clear();
    m_notebookNameByGuid.clear();
    m_notebookGuidByName.clear();
    m_dirtyNotebooksByGuid.clear();

    m_listNotebooksRequestId = QUuid();
    m_offset = 0;
}

bool NotebookSyncCache::isFilled() const noexcept
{
    if (!m_connectedToLocalStorage) {
        return false;
    }

    return m_listNotebooksRequestId.isNull();
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
    QString linkedNotebookGuid, // NOLINT
    QList<qevercloud::Notebook> foundNotebooks, QUuid requestId)
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
    QString linkedNotebookGuid, ErrorString errorDescription, // NOLINT
    QUuid requestId)
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

    m_notebookNameByLocalId.clear();
    m_notebookNameByGuid.clear();
    m_notebookGuidByName.clear();
    m_dirtyNotebooksByGuid.clear();
    disconnectFromLocalStorage();

    Q_EMIT failure(errorDescription);
}

void NotebookSyncCache::onAddNotebookComplete(
    qevercloud::Notebook notebook, QUuid requestId) // NOLINT
{
    NCDEBUG(
        "NotebookSyncCache::onAddNotebookComplete: request id = "
        << requestId << ", notebook: " << notebook);

    processNotebook(notebook);
}

void NotebookSyncCache::onUpdateNotebookComplete(
    qevercloud::Notebook notebook, QUuid requestId) // NOLINT
{
    NCDEBUG(
        "NotebookSyncCache::onUpdateNotebookComplete: request id = "
        << requestId << ", notebook: " << notebook);

    removeNotebook(notebook.localId());
    processNotebook(notebook);
}

void NotebookSyncCache::onExpungeNotebookComplete(
    qevercloud::Notebook notebook, QUuid requestId) // NOLINT
{
    NCDEBUG(
        "NotebookSyncCache::onExpungeNotebookComplete: request id = "
        << requestId << ", notebook: " << notebook);

    removeNotebook(notebook.localId());
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

void NotebookSyncCache::removeNotebook(const QString & notebookLocalId)
{
    NCDEBUG(
        "NotebookSyncCache::removeNotebook: local id = " << notebookLocalId);

    const auto localIdIt = m_notebookNameByLocalId.find(notebookLocalId);
    if (Q_UNLIKELY(localIdIt == m_notebookNameByLocalId.end())) {
        NCDEBUG("The notebook name was not found in the cache by local id");
        return;
    }

    const QString name = localIdIt.value();
    Q_UNUSED(m_notebookNameByLocalId.erase(localIdIt))

    const auto guidIt = m_notebookGuidByName.find(name);
    if (Q_UNLIKELY(guidIt == m_notebookGuidByName.end())) {
        NCDEBUG("The notebook guid was not found in the cache by name");
        return;
    }

    const QString guid = guidIt.value();
    Q_UNUSED(m_notebookGuidByName.erase(guidIt))

    const auto dirtyNotebookIt = m_dirtyNotebooksByGuid.find(guid);
    if (dirtyNotebookIt != m_dirtyNotebooksByGuid.end()) {
        Q_UNUSED(m_dirtyNotebooksByGuid.erase(dirtyNotebookIt))
    }

    const auto nameIt = m_notebookNameByGuid.find(guid);
    if (Q_UNLIKELY(nameIt == m_notebookNameByGuid.end())) {
        NCDEBUG("The notebook name was not found in the cache by guid");
        return;
    }

    Q_UNUSED(m_notebookNameByGuid.erase(nameIt))
}

void NotebookSyncCache::processNotebook(const qevercloud::Notebook & notebook)
{
    NCDEBUG("NotebookSyncCache::processNotebook: " << notebook);

    if (notebook.guid()) {
        if (notebook.isLocallyModified()) {
            m_dirtyNotebooksByGuid[*notebook.guid()] = notebook;
        }
        else {
            const auto it = m_dirtyNotebooksByGuid.find(*notebook.guid());
            if (it != m_dirtyNotebooksByGuid.end()) {
                Q_UNUSED(m_dirtyNotebooksByGuid.erase(it))
            }
        }
    }

    if (!notebook.name()) {
        NCDEBUG("Skipping the notebook without a name");
        return;
    }

    const QString name = notebook.name()->toLower();
    m_notebookNameByLocalId[notebook.localId()] = name;

    if (!notebook.guid()) {
        return;
    }

    const QString & guid = *notebook.guid();
    m_notebookNameByGuid[guid] = name;
    m_notebookGuidByName[name] = guid;
}

} // namespace quentier
