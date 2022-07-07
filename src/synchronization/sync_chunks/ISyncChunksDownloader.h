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

#include <quentier/utility/cancelers/Fwd.h>

#include <qevercloud/Fwd.h>
#include <qevercloud/types/LinkedNotebook.h>
#include <qevercloud/types/SyncChunk.h>

#include <QFuture>
#include <QList>

#include <memory>

class QException;

namespace quentier::synchronization {

class ISyncChunksDownloader
{
public:
    virtual ~ISyncChunksDownloader() = default;

    struct SyncChunksResult
    {
        QList<qevercloud::SyncChunk> m_syncChunks;
        std::shared_ptr<QException> m_exception;
    };

    [[nodiscard]] virtual QFuture<SyncChunksResult> downloadSyncChunks(
        qint32 afterUsn, qevercloud::IRequestContextPtr ctx,
        utility::cancelers::ICancelerPtr canceler) = 0;

    [[nodiscard]] virtual QFuture<SyncChunksResult>
        downloadLinkedNotebookSyncChunks(
            qevercloud::LinkedNotebook linkedNotebook, qint32 afterUsn,
            qevercloud::IRequestContextPtr ctx,
            utility::cancelers::ICancelerPtr canceler) = 0;
};

} // namespace quentier::synchronization
