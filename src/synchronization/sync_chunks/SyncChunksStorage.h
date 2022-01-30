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

#include <synchronization/sync_chunks/ISyncChunksStorage.h>

#include <QDir>

#include <utility>

namespace quentier::synchronization {

class SyncChunksStorage final: public ISyncChunksStorage
{
public:
    explicit SyncChunksStorage(const QDir & rootDir);

    [[nodiscard]] QList<qevercloud::SyncChunk> fetchRelevantUserOwnSyncChunks(
        qint32 afterUsn) const override;

    [[nodiscard]] QList<qevercloud::SyncChunk>
        fetchRelevantLinkedNotebookSyncChunks(
            const qevercloud::Guid & linkedNotebookGuid,
            qint32 afterUsn) const override;

    void putUserOwnSyncChunks(
        const QList<qevercloud::SyncChunk> & syncChunks) override;

    void putLinkedNotebookSyncChunks(
        const qevercloud::Guid & linkedNotebookGuid,
        const QList<qevercloud::SyncChunk> & syncChunks) override;

    void clearUserOwnSyncChunks() override;

    void clearLinkedNotebookSyncChunks(
        const qevercloud::Guid & linkedNotebookGuid) override;

    void clearAllSyncChunks() override;

private:
    QDir m_rootDir;
    QDir m_userOwnSyncChunksDir;
};

} // namespace quentier::synchronization
