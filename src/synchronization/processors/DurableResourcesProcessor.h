/*
 * Copyright 2022-2023 Dmitry Ivanov
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

#include "IDurableResourcesProcessor.h"

#include <qevercloud/types/Resource.h>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include <quentier/threading/Qt5Promise.h>
#endif

#include <QDir>

#include <memory>

namespace quentier::synchronization {

class DurableResourcesProcessor final :
    public IDurableResourcesProcessor,
    public std::enable_shared_from_this<DurableResourcesProcessor>
{
public:
    DurableResourcesProcessor(
        IResourcesProcessorPtr resourcesProcessor,
        const QDir & syncPersistentStorageDir);

    // IDurableResourcesProcessor
    [[nodiscard]] QFuture<DownloadResourcesStatusPtr> processResources(
        const QList<qevercloud::SyncChunk> & syncChunks,
        utility::cancelers::ICancelerPtr canceler,
        const std::optional<qevercloud::Guid> & linkedNotebookGuid =
            std::nullopt,
        ICallbackWeakPtr callbackWeak = {}) override;

private:
    [[nodiscard]] QList<qevercloud::Resource> resourcesFromPreviousSync(
        const QDir & dir) const;

    [[nodiscard]] QFuture<DownloadResourcesStatusPtr> processResourcesImpl(
        const QList<qevercloud::SyncChunk> & syncChunks,
        utility::cancelers::ICancelerPtr canceler,
        QList<qevercloud::Resource> previousResources,
        const std::optional<qevercloud::Guid> & linkedNotebookGuid,
        ICallbackWeakPtr callbackWeak);

    [[nodiscard]] QDir syncResourcesDir(
        const std::optional<qevercloud::Guid> & linkedNotebookGuid) const;

private:
    const IResourcesProcessorPtr m_resourcesProcessor;
    const QDir m_syncResourcesDir;
};

} // namespace quentier::synchronization
