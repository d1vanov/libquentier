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

#include "NotebookSyncCache.h"
#include <quentier/logging/QuentierLogger.h>

#define __NCLOG_BASE(message, level) \
    if (m_linkedNotebookGuid.isEmpty()) { \
        __QNLOG_BASE(message, level); \
    } \
    else { \
        __QNLOG_BASE(QStringLiteral("[linked notebook ") << m_linkedNotebookGuid << QStringLiteral("]: ") << message, level); \
    }

#define NCTRACE(message) \
    __NCLOG_BASE(message, Trace)

#define NCDEBUG(message) \
    __NCLOG_BASE(message, Debug)

#define NCWARNING(message) \
    __NCLOG_BASE(message, Warn)

namespace quentier {

NotebookSyncCache::NotebookSyncCache(LocalStorageManagerAsync & localStorageManagerAsync,
                                     const QString & linkedNotebookGuid, QObject * parent) :
    QObject(parent),
    m_localStorageManagerAsync(localStorageManagerAsync),
    m_connectedToLocalStorage(false),
    m_linkedNotebookGuid(linkedNotebookGuid),
    m_notebookNameByLocalUid(),
    m_notebookNameByGuid(),
    m_notebookGuidByName(),
    m_dirtyNotebooksByGuid(),
    m_listNotebooksRequestId(),
    m_limit(20),
    m_offset(0)
{}

void NotebookSyncCache::clear()
{
    NCDEBUG(QStringLiteral("NotebookSyncCache::clear"));

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
    NCDEBUG(QStringLiteral("NotebookSyncCache::fill"));

    if (m_connectedToLocalStorage) {
        NCDEBUG(QStringLiteral("Already connected to the local storage, no need to do anything"));
        return;
    }

    connectToLocalStorage();
    requestNotebooksList();
}

void NotebookSyncCache::onListNotebooksComplete(LocalStorageManager::ListObjectsOptions flag,
                                                size_t limit, size_t offset,
                                                LocalStorageManager::ListNotebooksOrder::type order,
                                                LocalStorageManager::OrderDirection::type orderDirection,
                                                QString linkedNotebookGuid, QList<Notebook> foundNotebooks,
                                                QUuid requestId)
{
    if (requestId != m_listNotebooksRequestId) {
        return;
    }

    NCDEBUG(QStringLiteral("NotebookSyncCache::onListNotebooksComplete: flag = ")
            << flag << QStringLiteral(", limit = ") << limit << QStringLiteral(", offset = ")
            << offset << QStringLiteral(", order = ") << order << QStringLiteral(", order direction = ")
            << orderDirection << QStringLiteral(", linked notebook guid = ") << linkedNotebookGuid
            << QStringLiteral(", request id = ") << requestId);

    for(auto it = foundNotebooks.constBegin(), end = foundNotebooks.constEnd(); it != end; ++it) {
        processNotebook(*it);
    }

    m_listNotebooksRequestId = QUuid();

    if (foundNotebooks.size() == static_cast<int>(limit)) {
        NCTRACE(QStringLiteral("The number of found notebooks matches the limit, requesting more notebooks from the local storage"));
        m_offset += limit;
        requestNotebooksList();
        return;
    }

    Q_EMIT filled();
}

void NotebookSyncCache::onListNotebooksFailed(LocalStorageManager::ListObjectsOptions flag,
                                              size_t limit, size_t offset,
                                              LocalStorageManager::ListNotebooksOrder::type order,
                                              LocalStorageManager::OrderDirection::type orderDirection,
                                              QString linkedNotebookGuid, ErrorString errorDescription,
                                              QUuid requestId)
{
    if (requestId != m_listNotebooksRequestId) {
        return;
    }

    NCDEBUG(QStringLiteral("NotebookSyncCache::onListNotebooksFailed: flag = ")
            << flag << QStringLiteral(", limit = ") << limit << QStringLiteral(", offset = ")
            << offset << QStringLiteral(", order = ") << order << QStringLiteral(", order direction = ")
            << orderDirection << QStringLiteral(", linked notebook guid = ") << linkedNotebookGuid
            << QStringLiteral(", error description = ") << errorDescription
            << QStringLiteral(", request id = ") << requestId);

    NCWARNING(QStringLiteral("Failed to cache the notebook information required for the sync: ") << errorDescription);

    m_notebookNameByLocalUid.clear();
    m_notebookNameByGuid.clear();
    m_notebookGuidByName.clear();
    m_dirtyNotebooksByGuid.clear();
    disconnectFromLocalStorage();

    Q_EMIT failure(errorDescription);
}

void NotebookSyncCache::onAddNotebookComplete(Notebook notebook, QUuid requestId)
{
    NCDEBUG(QStringLiteral("NotebookSyncCache::onAddNotebookComplete: request id = ")
            << requestId << QStringLiteral(", notebook: ") << notebook);

    processNotebook(notebook);
}

void NotebookSyncCache::onUpdateNotebookComplete(Notebook notebook, QUuid requestId)
{
    NCDEBUG(QStringLiteral("NotebookSyncCache::onUpdateNotebookComplete: request id = ")
            << requestId << QStringLiteral(", notebook: ") << notebook);

    removeNotebook(notebook.localUid());
    processNotebook(notebook);
}

void NotebookSyncCache::onExpungeNotebookComplete(Notebook notebook, QUuid requestId)
{
    NCDEBUG(QStringLiteral("NotebookSyncCache::onExpungeNotebookComplete: request id = ")
            << requestId << QStringLiteral(", notebook: ") << notebook);

    removeNotebook(notebook.localUid());
}

void NotebookSyncCache::connectToLocalStorage()
{
    NCDEBUG(QStringLiteral("NotebookSyncCache::connectToLocalStorage"));

    if (m_connectedToLocalStorage) {
        NCDEBUG(QStringLiteral("Already connected to the local storage"));
        return;
    }

    // Connect local signals to local storage manager async's slots
    QObject::connect(this,
                     QNSIGNAL(NotebookSyncCache,listNotebooks,LocalStorageManager::ListObjectsOptions,
                              size_t,size_t,LocalStorageManager::ListNotebooksOrder::type,
                              LocalStorageManager::OrderDirection::type,QString,QUuid),
                     &m_localStorageManagerAsync,
                     QNSLOT(LocalStorageManagerAsync,onListNotebooksRequest,LocalStorageManager::ListObjectsOptions,
                            size_t,size_t,LocalStorageManager::ListNotebooksOrder::type,
                            LocalStorageManager::OrderDirection::type,QString,QUuid));

    // Connect local storage manager async's signals to local slots
    QObject::connect(&m_localStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,listNotebooksComplete,
                              LocalStorageManager::ListObjectsOptions,size_t,size_t,
                              LocalStorageManager::ListNotebooksOrder::type,
                              LocalStorageManager::OrderDirection::type,
                              QString,QList<Notebook>,QUuid),
                     this,
                     QNSLOT(NotebookSyncCache,onListNotebooksComplete,
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
                     QNSLOT(NotebookSyncCache,onListNotebooksFailed,
                            LocalStorageManager::ListObjectsOptions,
                            size_t,size_t,LocalStorageManager::ListNotebooksOrder::type,
                            LocalStorageManager::OrderDirection::type,
                            QString,ErrorString,QUuid));
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addNotebookComplete,Notebook,QUuid),
                     this, QNSLOT(NotebookSyncCache,onAddNotebookComplete,Notebook,QUuid));
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateNotebookComplete,Notebook,QUuid),
                     this, QNSLOT(NotebookSyncCache,onUpdateNotebookComplete,Notebook,QUuid));
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeNotebookComplete,Notebook,QUuid),
                     this, QNSLOT(NotebookSyncCache,onExpungeNotebookComplete,Notebook,QUuid));

    m_connectedToLocalStorage = true;
}

void NotebookSyncCache::disconnectFromLocalStorage()
{
    NCDEBUG(QStringLiteral("NotebookSyncCache::disconnectFromLocalStorage"));

    if (!m_connectedToLocalStorage) {
        NCDEBUG(QStringLiteral("Not connected to local storage at the moment"));
        return;
    }

    // Disconnect local signals from local storage manager async's slots
    QObject::disconnect(this,
                        QNSIGNAL(NotebookSyncCache,listNotebooks,LocalStorageManager::ListObjectsOptions,
                                 size_t,size_t,LocalStorageManager::ListNotebooksOrder::type,
                                 LocalStorageManager::OrderDirection::type,QString,QUuid),
                        &m_localStorageManagerAsync,
                        QNSLOT(LocalStorageManagerAsync,onListNotebooksRequest,LocalStorageManager::ListObjectsOptions,
                               size_t,size_t,LocalStorageManager::ListNotebooksOrder::type,
                               LocalStorageManager::OrderDirection::type,QString,QUuid));

    // Connect local storage manager async's signals to local slots
    QObject::disconnect(&m_localStorageManagerAsync,
                        QNSIGNAL(LocalStorageManagerAsync,listNotebooksComplete,
                                 LocalStorageManager::ListObjectsOptions,size_t,size_t,
                                 LocalStorageManager::ListNotebooksOrder::type,
                                 LocalStorageManager::OrderDirection::type,
                                 QString,QList<Notebook>,QUuid),
                        this,
                        QNSLOT(NotebookSyncCache,onListNotebooksComplete,
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
                        QNSLOT(NotebookSyncCache,onListNotebooksFailed,
                               LocalStorageManager::ListObjectsOptions,
                               size_t,size_t,LocalStorageManager::ListNotebooksOrder::type,
                               LocalStorageManager::OrderDirection::type,
                               QString,ErrorString,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addNotebookComplete,Notebook,QUuid),
                        this, QNSLOT(NotebookSyncCache,onAddNotebookComplete,Notebook,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateNotebookComplete,Notebook,QUuid),
                        this, QNSLOT(NotebookSyncCache,onUpdateNotebookComplete,Notebook,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeNotebookComplete,Notebook,QUuid),
                        this, QNSLOT(NotebookSyncCache,onExpungeNotebookComplete,Notebook,QUuid));

    m_connectedToLocalStorage = false;
}

void NotebookSyncCache::requestNotebooksList()
{
    NCDEBUG(QStringLiteral("NotebookSyncCache::requestNotebooksList"));

    m_listNotebooksRequestId = QUuid::createUuid();

    NCTRACE(QStringLiteral("Emitting the request to list notebooks: request id = ")
            << m_listNotebooksRequestId << QStringLiteral(", offset = ") << m_offset);
    Q_EMIT listNotebooks(LocalStorageManager::ListAll,
                       m_limit, m_offset, LocalStorageManager::ListNotebooksOrder::NoOrder,
                       LocalStorageManager::OrderDirection::Ascending,
                       m_linkedNotebookGuid, m_listNotebooksRequestId);

}

void NotebookSyncCache::removeNotebook(const QString & notebookLocalUid)
{
    NCDEBUG(QStringLiteral("NotebookSyncCache::removeNotebook: local uid = ") << notebookLocalUid);

    auto localUidIt = m_notebookNameByLocalUid.find(notebookLocalUid);
    if (Q_UNLIKELY(localUidIt == m_notebookNameByLocalUid.end())) {
        NCDEBUG(QStringLiteral("The notebook name was not found in the cache by local uid"));
        return;
    }

    QString name = localUidIt.value();
    Q_UNUSED(m_notebookNameByLocalUid.erase(localUidIt))

    auto guidIt = m_notebookGuidByName.find(name);
    if (Q_UNLIKELY(guidIt == m_notebookGuidByName.end())) {
        NCDEBUG(QStringLiteral("The notebook guid was not found in the cache by name"));
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
        NCDEBUG(QStringLiteral("The notebook name was not found in the cache by guid"));
        return;
    }

    Q_UNUSED(m_notebookNameByGuid.erase(nameIt))
}

void NotebookSyncCache::processNotebook(const Notebook & notebook)
{
    NCDEBUG(QStringLiteral("NotebookSyncCache::processNotebook: ") << notebook);

    if (notebook.hasGuid())
    {
        if (notebook.isDirty())
        {
            m_dirtyNotebooksByGuid[notebook.guid()] = notebook;
        }
        else
        {
            auto it = m_dirtyNotebooksByGuid.find(notebook.guid());
            if (it != m_dirtyNotebooksByGuid.end()) {
                Q_UNUSED(m_dirtyNotebooksByGuid.erase(it))
            }
        }
    }

    if (!notebook.hasName()) {
        NCDEBUG(QStringLiteral("Skipping the notebook without a name"));
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
