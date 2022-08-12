/*
 * Copyright 2022 Dmitry Ivanov
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

#pragma once

#include <qevercloud/types/SyncChunk.h>
#include <qevercloud/types/TypeAliases.h>

#include <QList>

#include <utility>

namespace quentier::synchronization {

class ISyncChunksStorage
{
public:
    virtual ~ISyncChunksStorage() = default;

    [[nodiscard]] virtual QList<std::pair<qint32, qint32>>
        fetchUserOwnSyncChunksLowAndHighUsns() const = 0;

    [[nodiscard]] virtual QList<std::pair<qint32, qint32>>
        fetchLinkedNotebookSyncChunksLowAndHighUsns(
            const qevercloud::Guid & linkedNotebookGuid) const = 0;

    [[nodiscard]] virtual QList<qevercloud::SyncChunk>
        fetchRelevantUserOwnSyncChunks(qint32 afterUsn) const = 0;

    [[nodiscard]] virtual QList<qevercloud::SyncChunk>
        fetchRelevantLinkedNotebookSyncChunks(
            const qevercloud::Guid & linkedNotebookGuid,
            qint32 afterUsn) const = 0;

    virtual void putUserOwnSyncChunks(
        QList<qevercloud::SyncChunk> syncChunks) = 0;

    virtual void putLinkedNotebookSyncChunks(
        const qevercloud::Guid & linkedNotebookGuid,
        QList<qevercloud::SyncChunk> syncChunks) = 0;

    virtual void clearUserOwnSyncChunks() = 0;

    virtual void clearLinkedNotebookSyncChunks(
        const qevercloud::Guid & linkedNotebookGuid) = 0;

    virtual void clearAllSyncChunks() = 0;

    virtual void flush() = 0;
};

} // namespace quentier::synchronization
