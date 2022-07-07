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

#include "IDurableResourcesProcessor.h"
#include "IResourcesProcessor.h"

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
    public IResourcesProcessor::ICallback,
    public std::enable_shared_from_this<DurableResourcesProcessor>
{
public:
    DurableResourcesProcessor(
        IResourcesProcessorPtr resourcesProcessor,
        const QDir & syncPersistentStorageDir);

    // IDurableResourcesProcessor
    [[nodiscard]] QFuture<DownloadResourcesStatusPtr> processResources(
        const QList<qevercloud::SyncChunk> & syncChunks,
        utility::cancelers::ICancelerPtr canceler) override;

private:
    // IResourcesProcessor::ICallback
    void onProcessedResource(
        const qevercloud::Guid & resourceGuid,
        qint32 resourceUpdateSequenceNum) noexcept override;

    void onResourceFailedToDownload(
        const qevercloud::Resource & resource,
        const QException & e) noexcept override;

    void onResourceFailedToProcess(
        const qevercloud::Resource & resource,
        const QException & e) noexcept override;

    void onResourceProcessingCancelled(
        const qevercloud::Resource & resource) noexcept override;

private:
    [[nodiscard]] QList<qevercloud::Resource> resourcesFromPreviousSync() const;

    [[nodiscard]] QFuture<DownloadResourcesStatusPtr> processResourcesImpl(
        const QList<qevercloud::SyncChunk> & syncChunks,
        utility::cancelers::ICancelerPtr canceler,
        QList<qevercloud::Resource> previousResources);

private:
    const IResourcesProcessorPtr m_resourcesProcessor;
    const QDir m_syncResourcesDir;
};

} // namespace quentier::synchronization
