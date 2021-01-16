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

#ifndef LIB_QUENTIER_SYNCHRONIZATION_TAG_SYNC_CACHE_H
#define LIB_QUENTIER_SYNCHRONIZATION_TAG_SYNC_CACHE_H

#include <quentier/local_storage/LocalStorageManagerAsync.h>

#include <QHash>
#include <QObject>
#include <QUuid>

namespace quentier {

class Q_DECL_HIDDEN TagSyncCache final : public QObject
{
    Q_OBJECT
public:
    TagSyncCache(
        LocalStorageManagerAsync & localStorageManagerAsync,
        QString linkedNotebookGuid, QObject * parent = nullptr);

    void clear();

    /**
     * @return True if the cache is already filled with up-to-moment data,
     * false otherwise
     */
    [[nodiscard]] bool isFilled() const noexcept;

    [[nodiscard]] const QHash<QString, QString> & nameByLocalIdHash()
        const noexcept
    {
        return m_tagNameByLocalId;
    }

    [[nodiscard]] const QHash<QString, QString> & nameByGuidHash()
        const noexcept
    {
        return m_tagNameByGuid;
    }

    [[nodiscard]] const QHash<QString, QString> & guidByNameHash()
        const noexcept
    {
        return m_tagGuidByName;
    }

    [[nodiscard]] const QHash<QString, qevercloud::Tag> & dirtyTagsByGuidHash()
        const noexcept
    {
        return m_dirtyTagsByGuid;
    }

    [[nodiscard]] const QString & linkedNotebookGuid() const noexcept
    {
        return m_linkedNotebookGuid;
    }

Q_SIGNALS:
    void filled();
    void failure(ErrorString errorDescription);

    // private signals
    void listTags(
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListTagsOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QString linkedNotebookGuid, QUuid requestId);

public Q_SLOTS:
    /**
     * Start collecting the information about tags; does nothing if the
     * information is already collected or is being collected at the moment,
     * otherwise initiates the sequence of actions required to collect the tag
     * information
     */
    void fill();

private Q_SLOTS:
    void onListTagsComplete(
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListTagsOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QString linkedNotebookGuid, QList<qevercloud::Tag> foundTags,
        QUuid requestId);

    void onListTagsFailed(
        LocalStorageManager::ListObjectsOptions flag, size_t limit,
        size_t offset, LocalStorageManager::ListTagsOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QString linkedNotebookGuid, ErrorString errorDescription,
        QUuid requestId);

    void onAddTagComplete(qevercloud::Tag tag, QUuid requestId);
    void onUpdateTagComplete(qevercloud::Tag tag, QUuid requestId);

    void onExpungeTagComplete(
        qevercloud::Tag tag, QStringList expungedChildTagLocalIds,
        QUuid requestId);

private:
    void connectToLocalStorage();
    void disconnectFromLocalStorage();

    void requestTagsList();

    void removeTag(const QString & tagLocalId);
    void processTag(const qevercloud::Tag & tag);

private:
    LocalStorageManagerAsync & m_localStorageManagerAsync;
    bool m_connectedToLocalStorage = false;

    QString m_linkedNotebookGuid;

    QHash<QString, QString> m_tagNameByLocalId;
    QHash<QString, QString> m_tagNameByGuid;
    QHash<QString, QString> m_tagGuidByName;

    QHash<QString, qevercloud::Tag> m_dirtyTagsByGuid;

    QUuid m_listTagsRequestId;
    size_t m_limit = 50;
    size_t m_offset = 0;
};

} // namespace quentier

#endif // LIB_QUENTIER_SYNCHRONIZATION_TAG_SYNC_CACHE_H
