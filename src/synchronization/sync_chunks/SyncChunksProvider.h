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

#include <synchronization/Fwd.h>
#include <synchronization/sync_chunks/ISyncChunksProvider.h>

namespace quentier::synchronization {

class SyncChunksProvider final : public ISyncChunksProvider
{
public:
    explicit SyncChunksProvider(
        ISyncChunksDownloaderPtr syncChunksDownloader,
        ISyncChunksStoragePtr syncChunksStorage);

    [[nodiscard]] QFuture<QList<qevercloud::SyncChunk>> fetchSyncChunks(
        qint32 afterUsn, qevercloud::IRequestContextPtr ctx) override;

    [[nodiscard]] QFuture<QList<qevercloud::SyncChunk>>
        fetchLinkedNotebookSyncChunks(
            qevercloud::LinkedNotebook linkedNotebook, qint32 afterUsn,
            qevercloud::IRequestContextPtr ctx) override;

private:
    ISyncChunksDownloaderPtr m_syncChunksDownloader;
    ISyncChunksStoragePtr m_syncChunksStorage;
};

} // namespace quentier::synchronization