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

#include "IResourceFullDataDownloader.h"

#include <QMutex>
#include <QQueue>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include <quentier/threading/Qt5Promise.h>
#endif

#include <atomic>
#include <memory>

namespace quentier::synchronization {

class ResourceFullDataDownloader final :
    public IResourceFullDataDownloader,
    public std::enable_shared_from_this<ResourceFullDataDownloader>
{
public:
    explicit ResourceFullDataDownloader(
        qevercloud::INoteStorePtr noteStore, quint32 maxInFlightDownloads);

    [[nodiscard]] QFuture<qevercloud::Resource> downloadFullResourceData(
        qevercloud::Guid resourceGuid,
        qevercloud::IRequestContextPtr ctx = {}) override;

private:
    void downloadFullResourceDataImpl(
        qevercloud::Guid resourceGuid, qevercloud::IRequestContextPtr ctx,
        const std::shared_ptr<QPromise<qevercloud::Resource>> & promise);

    void onResourceFullDataDownloadFinished();

    struct QueuedRequest
    {
        qevercloud::Guid m_resourceGuid;
        qevercloud::IRequestContextPtr m_ctx;
        std::shared_ptr<QPromise<qevercloud::Resource>> m_promise;
    };

private:
    const qevercloud::INoteStorePtr m_noteStore;
    const quint32 m_maxInFlightDownloads;

    std::atomic<quint32> m_inFlightDownloads{0U};

    QQueue<QueuedRequest> m_queuedRequests;
    QMutex m_queuedRequestsMutex;
};

} // namespace quentier::synchronization
