/*
 * Copyright 2022-2024 Dmitry Ivanov
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
#include <quentier/logging/QuentierLogger.h>
#include <quentier/threading/Future.h>
#include <quentier/threading/QtFutureContinuations.h>

#include <qevercloud/services/INoteStore.h>

#include <QMutexLocker>

namespace quentier::synchronization {

ResourceFullDataDownloader::ResourceFullDataDownloader(
    const quint32 maxInFlightDownloads) : m_maxInFlightDownloads{maxInFlightDownloads}
{
    if (Q_UNLIKELY(m_maxInFlightDownloads == 0U)) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "ResourceFullDataDownloader ctor: max in flight downloads must be "
            "positive")}};
    }
}

QFuture<qevercloud::Resource>
    ResourceFullDataDownloader::downloadFullResourceData(
        qevercloud::Guid resourceGuid, qevercloud::INoteStorePtr noteStore,
        qevercloud::IRequestContextPtr ctx)
{
    QNDEBUG(
        "synchronization::ResourceFullDataDownloader",
        "ResourceFullDataDownloader::downloadFullResourceData: resource guid = "
            << resourceGuid);

    if (Q_UNLIKELY(!noteStore)) {
        return threading::makeExceptionalFuture<qevercloud::Resource>(
            InvalidArgument{ErrorString{QStringLiteral(
                "ResourceFullDataDownloader: note store is null")}});
    }

    auto promise = std::make_shared<QPromise<qevercloud::Resource>>();
    auto future = promise->future();

    const auto inFlightDownloads =
        m_inFlightDownloads.load(std::memory_order_acquire);
    if (inFlightDownloads >= m_maxInFlightDownloads) {
        QNDEBUG(
            "synchronization::ResourceFullDataDownloader",
            "Already have " << inFlightDownloads << " current downloads, "
                            << "delaying this resource download request");

        // Already have too many requests in flight, will enqueue this one
        // and execute it later, when some of the previous requests are finished
        const QMutexLocker lock{&m_queuedRequestsMutex};
        m_queuedRequests.append(QueuedRequest{
            std::move(resourceGuid), std::move(ctx), std::move(noteStore),
            std::move(promise)});

        QNDEBUG(
            "synchronization::ResourceFullDataDownloader",
            "Got " << m_queuedRequests.size() << " delayed resource download "
                   << "requests now");

        return future;
    }

    downloadFullResourceDataImpl(
        std::move(resourceGuid), noteStore, std::move(ctx), promise);

    return future;
}

void ResourceFullDataDownloader::downloadFullResourceDataImpl(
    qevercloud::Guid resourceGuid, const qevercloud::INoteStorePtr & noteStore,
    qevercloud::IRequestContextPtr ctx,
    const std::shared_ptr<QPromise<qevercloud::Resource>> & promise)
{
    Q_ASSERT(noteStore);
    Q_ASSERT(promise);

    QNDEBUG(
        "synchronization::ResourceFullDataDownloader",
        "ResourceFullDataDownloader::downloadFullResourceDataImpl: resource "
            << "guid = " << resourceGuid);

    promise->start();

    m_inFlightDownloads.fetch_add(1, std::memory_order_acq_rel);

    auto getResourceFuture = noteStore->getResourceAsync(
        resourceGuid,
        /* withData = */ true,
        /* withRecognition = */ true,
        /* withAttributes = */ true,
        /* withAlternateData = */ true, std::move(ctx));

    const auto selfWeak = weak_from_this();

    auto processedResourceFuture = threading::then(
        std::move(getResourceFuture),
        [promise, resourceGuid,
         selfWeak](const qevercloud::Resource & resource) {
            QNDEBUG(
                "synchronization::ResourceFullDataDownloader",
                "Successfully downloaded full resource data for resource guid "
                    << resourceGuid);

            promise->addResult(resource);
            promise->finish();

            const auto self = selfWeak.lock();
            if (self) {
                self->onResourceFullDataDownloadFinished();
            }
        });

    threading::onFailed(
        std::move(processedResourceFuture),
        [promise, selfWeak,
         resourceGuid = std::move(resourceGuid)](const QException & e) {
            QNWARNING(
                "synchronization::ResourceFullDataDownloader",
                "Failed to download full resource data for resource guid "
                    << resourceGuid);

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

        QNDEBUG(
            "synchronization::ResourceFullDataDownloader",
            "Processing pending request from resource download requests queue, "
                << "got " << m_queuedRequests.size()
                << " delayed requests left");
    }

    downloadFullResourceDataImpl(
        std::move(request.m_resourceGuid), request.m_noteStore,
        std::move(request.m_ctx), request.m_promise);
}

} // namespace quentier::synchronization
