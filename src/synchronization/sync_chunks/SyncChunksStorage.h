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
#include <QFuture>
#include <QList>

#include <optional>
#include <utility>

class QThreadPool;

namespace quentier::synchronization {

/**
 * @brief The SyncChunksStorage class is the implementation of
 * ISyncChunksStorage interface. It is not thread-safe!
 */
class SyncChunksStorage final : public ISyncChunksStorage
{
public:
    explicit SyncChunksStorage(const QDir & rootDir, QThreadPool * threadPool);

    [[nodiscard]] QList<std::pair<qint32, qint32>>
        fetchUserOwnSyncChunksLowAndHighUsns() const override;

    [[nodiscard]] QList<std::pair<qint32, qint32>>
        fetchLinkedNotebookSyncChunksLowAndHighUsns(
            const qevercloud::Guid & linkedNotebookGuid) const override;

    [[nodiscard]] QList<qevercloud::SyncChunk> fetchRelevantUserOwnSyncChunks(
        qint32 afterUsn) const override;

    [[nodiscard]] QList<qevercloud::SyncChunk>
        fetchRelevantLinkedNotebookSyncChunks(
            const qevercloud::Guid & linkedNotebookGuid,
            qint32 afterUsn) const override;

    void putUserOwnSyncChunks(
        QList<qevercloud::SyncChunk> syncChunks) override;

    void putLinkedNotebookSyncChunks(
        const qevercloud::Guid & linkedNotebookGuid,
        QList<qevercloud::SyncChunk> syncChunks) override;

    void clearUserOwnSyncChunks() override;

    void clearLinkedNotebookSyncChunks(
        const qevercloud::Guid & linkedNotebookGuid) override;

    void clearAllSyncChunks() override;

    void flush() override;

private:
    class LowAndHighUsnsDataAccessor
    {
    public:
        struct LowAndHighUsnsData
        {
            using LowAndHighUsnsList = QList<std::pair<qint32, qint32>>;

            LowAndHighUsnsList m_userOwnSyncChunkLowAndHighUsns;

            QHash<qevercloud::Guid, LowAndHighUsnsList>
                m_linkedNotebookSyncChunkLowAndHighUsns;
        };

    public:
        explicit LowAndHighUsnsDataAccessor(
            const QDir & rootDir, const QDir & userOwnSyncChunksDir,
            QThreadPool * threadPool);

        [[nodiscard]] LowAndHighUsnsData & data();

        void reset();

    private:
        void waitForLowAndHighUsnsDataInit();

    private:
        std::optional<QFuture<LowAndHighUsnsData>> m_lowAndHighUsnsDataFuture;

        LowAndHighUsnsData m_lowAndHighUsnsData;
    };

    struct SyncChunkInfo
    {
        qevercloud::SyncChunk m_syncChunk;
        qint32 m_lowUsn = 0;
        qint32 m_highUsn = 0;
    };

private:
    [[nodiscard]] QList<SyncChunkInfo> toSyncChunksInfo(
        QList<qevercloud::SyncChunk> syncChunks) const;

    [[nodiscard]] QList<std::pair<qint32, qint32>> toUsns(
        const QList<SyncChunkInfo> & syncChunksInfo) const;

    void appendPendingSyncChunks(
        const QList<SyncChunkInfo> & syncChunksInfo, qint32 afterUsn,
        QList<qevercloud::SyncChunk> & result) const;

private:
    QDir m_rootDir;
    QDir m_userOwnSyncChunksDir;

    QList<SyncChunkInfo> m_userOwnSyncChunksPendingPersistence;

    QHash<qevercloud::Guid, QList<SyncChunkInfo>>
        m_linkedNotebookSyncChunksPendingPersistence;

    mutable LowAndHighUsnsDataAccessor m_lowAndHighUsnsDataAccessor;
};

} // namespace quentier::synchronization
