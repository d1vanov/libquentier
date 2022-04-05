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

#include "INoteFullDataDownloader.h"

#include <qevercloud/services/Fwd.h>

#include <QAtomicInteger>
#include <QMutex>
#include <QQueue>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include <quentier/threading/Qt5Promise.h>
#endif

#include <memory>

namespace quentier::synchronization {

class NoteFullDataDownloader final :
    public INoteFullDataDownloader,
    public std::enable_shared_from_this<NoteFullDataDownloader>
{
public:
    explicit NoteFullDataDownloader(
        qevercloud::INoteStorePtr noteStore, quint32 maxInFlightDownloads);

    [[nodiscard]] QFuture<qevercloud::Note> downloadFullNoteData(
        qevercloud::Guid noteGuid, qevercloud::IRequestContextPtr ctx,
        IncludeNoteLimits includeNoteLimitsOption) override;

private:
    void downloadFullNoteDataImpl(
        qevercloud::Guid noteGuid, qevercloud::IRequestContextPtr ctx,
        IncludeNoteLimits includeNoteLimitsOption,
        const std::shared_ptr<QPromise<qevercloud::Note>> & promise);

    void onNoteFullDataDownloadFinished();

    struct QueuedRequest
    {
        qevercloud::Guid m_noteGuid;
        qevercloud::IRequestContextPtr m_ctx;
        IncludeNoteLimits m_includeNoteLimits = IncludeNoteLimits::No;
        std::shared_ptr<QPromise<qevercloud::Note>> m_promise;
    };

private:
    const qevercloud::INoteStorePtr m_noteStore;
    const quint32 m_maxInFlightDownloads;

    QAtomicInteger<quint32> m_inFlightDownloads{0U};

    QQueue<QueuedRequest> m_queuedRequests;
    QMutex m_queuedRequestsMutex;
};

} // namespace quentier::synchronization
