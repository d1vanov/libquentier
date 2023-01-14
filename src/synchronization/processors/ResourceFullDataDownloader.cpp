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

#include "ResourceFullDataDownloader.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/threading/QtFutureContinuations.h>

#include <qevercloud/services/INoteStore.h>

#include <QMutexLocker>

namespace quentier::synchronization {

ResourceFullDataDownloader::ResourceFullDataDownloader(
    qevercloud::INoteStorePtr noteStore, quint32 maxInFlightDownloads) :
    m_noteStore{std::move(noteStore)},
    m_maxInFlightDownloads{maxInFlightDownloads}
{
    if (Q_UNLIKELY(!m_noteStore)) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "ResourceFullDataDownloader ctor: note store is null")}};
    }

    if (Q_UNLIKELY(m_maxInFlightDownloads == 0U)) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "ResourceFullDataDownloader ctor: max in flight downloads must be "
            "positive")}};
    }
}

QFuture<qevercloud::Resource>
    ResourceFullDataDownloader::downloadFullResourceData(
        qevercloud::Guid resourceGuid, qevercloud::IRequestContextPtr ctx)
{
    auto promise = std::make_shared<QPromise<qevercloud::Resource>>();
    auto future = promise->future();

    if (m_inFlightDownloads.load(std::memory_order_acquire) >=
        m_maxInFlightDownloads)
    {
        // Already have too much requests in flight, will enqueue this one
        // and execute it later, when some of the previous requests are finished
        const QMutexLocker lock{&m_queuedRequestsMutex};
        m_queuedRequests.append(QueuedRequest{
            std::move(resourceGuid), std::move(ctx), std::move(promise)});
        return future;
    }

    downloadFullResourceDataImpl(
        std::move(resourceGuid), std::move(ctx), promise);

    return future;
}

void ResourceFullDataDownloader::downloadFullResourceDataImpl(
    qevercloud::Guid resourceGuid, qevercloud::IRequestContextPtr ctx,
    const std::shared_ptr<QPromise<qevercloud::Resource>> & promise)
{
    Q_ASSERT(promise);
    promise->start();

    m_inFlightDownloads.fetch_add(1, std::memory_order_acq_rel);

    auto getResourceFuture = m_noteStore->getResourceAsync(
        std::move(resourceGuid),
        /* withData = */ true,
        /* withRecognition = */ true,
        /* withAttributes = */ true,
        /* withAlternateData = */ true, std::move(ctx));

    const auto selfWeak = weak_from_this();

    auto processedResourceFuture = threading::then(
        std::move(getResourceFuture),
        [promise, selfWeak](const qevercloud::Resource & resource) {
            promise->addResult(resource);
            promise->finish();

            const auto self = selfWeak.lock();
            if (self) {
                self->onResourceFullDataDownloadFinished();
            }
        });

    threading::onFailed(
        std::move(processedResourceFuture),
        [promise, selfWeak](const QException & e) {
            promise->setException(e);
            promise->finish();

            const auto self = selfWeak.lock();
            if (self) {
                self->onResourceFullDataDownloadFinished();
            }
        });
}

void ResourceFullDataDownloader::onResourceFullDataDownloadFinished()
{
    Q_ASSERT(m_inFlightDownloads.load(std::memory_order_acquire) > 0U);
    m_inFlightDownloads.fetch_sub(1, std::memory_order_acq_rel);

    QueuedRequest request;
    {
        const QMutexLocker lock{&m_queuedRequestsMutex};
        if (m_queuedRequests.isEmpty()) {
            return;
        }

        request = m_queuedRequests.dequeue();
    }

    downloadFullResourceDataImpl(
        std::move(request.m_resourceGuid), std::move(request.m_ctx),
        request.m_promise);
}

} // namespace quentier::synchronization
