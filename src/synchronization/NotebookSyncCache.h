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

#ifndef LIB_QUENTIER_SYNCHRONIZATION_NOTEBOOK_SYNC_CACHE_H
#define LIB_QUENTIER_SYNCHRONIZATION_NOTEBOOK_SYNC_CACHE_H

#include <quentier/local_storage/LocalStorageManagerAsync.h>

#include <QHash>
#include <QObject>
#include <QUuid>

namespace quentier {

/**
 * The NotebookSyncCache class is a lazy cache of notebook info required for
 * the sync conflicts resolution and possibly for expunging stale notebooks
 * after the out of order full sync. The cache is lazy because initially it
 * doesn't contain any information, it only starts to collect it after
 * the first request to do so hence saving the CPU and memory in case it won't
 * be needed (i.e. there won't be any conflicts detected during sync + there
 * won't be the need to expunge stale notebooks after the full sync).
 */
class Q_DECL_HIDDEN NotebookSyncCache final : public QObject
{
    Q_OBJECT
public:
    NotebookSyncCache(
        LocalStorageManagerAsync & localStorageManagerAsync,
        const QString & linkedNotebookGuid, QObject * parent = nullptr);

    void clear();

    /**
     * @return  True if the cache is already filled with up-to-moment data,
     *          false otherwise
     */
    bool isFilled() const;

    const QHash<QString, QString> & nameByLocalUidHash() const
    {
        return m_notebookNameByLocalUid;
    }

    const QHash<QString, QString> & nameByGuidHash() const
    {
        return m_notebookNameByGuid;
    }

    const QHash<QString, QString> & guidByNameHash() const
    {
        return m_notebookGuidByName;
    }

    const QHash<QString, Notebook> & dirtyNotebooksByGuidHash() const
    {
        return m_dirtyNotebooksByGuid;
    }

    const QString & linkedNotebookGuid() const
    {
        return m_linkedNotebookGuid;
    }

Q_SIGNALS:
    void filled();
    void failure(ErrorString errorDescription);

    // private signals
    void listNotebooks(
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListNotebooksOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QString linkedNotebookGuid, QUuid requestId);

public Q_SLOTS:
    /**
     * Start collecting the information about notebooks; does nothing if
     * the information is already collected or is being collected at the moment,
     * otherwise initiates the sequence of actions required to collect
     * the notebook information
     */
    void fill();

private Q_SLOTS:
    void onListNotebooksComplete(
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListNotebooksOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QString linkedNotebookGuid, QList<Notebook> foundNotebooks,
        QUuid requestId);

    void onListNotebooksFailed(
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListNotebooksOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QString linkedNotebookGuid, ErrorString errorDescription,
        QUuid requestId);

    void onAddNotebookComplete(Notebook notebook, QUuid requestId);
    void onUpdateNotebookComplete(Notebook notebook, QUuid requestId);
    void onExpungeNotebookComplete(Notebook notebook, QUuid requestId);

private:
    void connectToLocalStorage();
    void disconnectFromLocalStorage();

    void requestNotebooksList();

    void removeNotebook(const QString & notebookLocalUid);
    void processNotebook(const Notebook & notebook);

private:
    LocalStorageManagerAsync & m_localStorageManagerAsync;
    bool m_connectedToLocalStorage = false;

    QString m_linkedNotebookGuid;

    QHash<QString, QString> m_notebookNameByLocalUid;
    QHash<QString, QString> m_notebookNameByGuid;
    QHash<QString, QString> m_notebookGuidByName;

    QHash<QString, Notebook> m_dirtyNotebooksByGuid;

    QUuid m_listNotebooksRequestId;
    size_t m_limit = 20;
    size_t m_offset = 0;
};

} // namespace quentier

#endif // LIB_QUENTIER_SYNCHRONIZATION_NOTEBOOK_SYNC_CACHE_H
