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

#include "ISyncChunksDownloader.h"

#include <synchronization/SynchronizationMode.h>

#include <qevercloud/services/Fwd.h>

namespace quentier::synchronization {

class SyncChunksDownloader final : public ISyncChunksDownloader
{
public:
    explicit SyncChunksDownloader(
        SynchronizationMode synchronizationMode,
        qevercloud::INoteStorePtr noteStore);

    [[nodiscard]] QFuture<QList<qevercloud::SyncChunk>> downloadSyncChunks(
        qint32 afterUsn, qevercloud::IRequestContextPtr ctx) override;

    [[nodiscard]] QFuture<QList<qevercloud::SyncChunk>>
        downloadLinkedNotebookSyncChunks(
            qevercloud::LinkedNotebook linkedNotebook, qint32 afterUsn,
            qevercloud::IRequestContextPtr ctx) override;

private:
    const SynchronizationMode m_synchronizationMode;
    const qevercloud::INoteStorePtr m_noteStore;
};

} // namespace quentier::synchronization
