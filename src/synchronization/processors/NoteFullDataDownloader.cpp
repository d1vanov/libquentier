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

#include "NoteFullDataDownloader.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/threading/QtFutureContinuations.h>

#include <qevercloud/services/INoteStore.h>
#include <qevercloud/types/builders/NoteResultSpecBuilder.h>

#include <QMutexLocker>

namespace quentier::synchronization {

NoteFullDataDownloader::NoteFullDataDownloader(
    qevercloud::INoteStorePtr noteStore, quint32 maxInFlightDownloads) :
    m_noteStore{std::move(noteStore)},
    m_maxInFlightDownloads{maxInFlightDownloads}
{
    if (Q_UNLIKELY(!m_noteStore)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::NoteFullDataDownloader",
            "NoteFullDataDownloader ctor: note store is null")}};
    }

    if (Q_UNLIKELY(m_maxInFlightDownloads == 0U)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::NoteFullDataDownloader",
            "NoteFullDataDownloader ctor: max in flight downloads must be "
            "positive")}};
    }
}

QFuture<qevercloud::Note> NoteFullDataDownloader::downloadFullNoteData(
    qevercloud::Guid noteGuid, qevercloud::IRequestContextPtr ctx,
    const IncludeNoteLimits includeNoteLimitsOption)
{
    auto promise = std::make_shared<QPromise<qevercloud::Note>>();
    auto future = promise->future();

    if (m_inFlightDownloads.loadAcquire() >= m_maxInFlightDownloads) {
        // Already have too much requests in flight, will enqueue this one
        // and execute it later, when some of the previous requests are finished
        const QMutexLocker lock{&m_queuedRequestsMutex};
        m_queuedRequests.append(QueuedRequest{
            std::move(noteGuid), std::move(ctx), includeNoteLimitsOption,
            std::move(promise)});
        return future;
    }

    downloadFullNoteDataImpl(
        std::move(noteGuid), std::move(ctx), includeNoteLimitsOption, promise);

    return future;
}

void NoteFullDataDownloader::downloadFullNoteDataImpl(
    qevercloud::Guid noteGuid, qevercloud::IRequestContextPtr ctx,
    const IncludeNoteLimits includeNoteLimitsOption,
    const std::shared_ptr<QPromise<qevercloud::Note>> & promise)
{
    Q_ASSERT(promise);
    promise->start();

    ++m_inFlightDownloads;

    auto getNoteFuture = m_noteStore->getNoteWithResultSpecAsync(
        std::move(noteGuid),
        qevercloud::NoteResultSpecBuilder{}
            .setIncludeContent(true)
            .setIncludeResourcesData(true)
            .setIncludeResourcesRecognition(true)
            .setIncludeResourcesAlternateData(true)
            .setIncludeSharedNotes(true)
            .setIncludeNoteAppDataValues(true)
            .setIncludeResourceAppDataValues(true)
            .setIncludeAccountLimits(
                includeNoteLimitsOption == IncludeNoteLimits::Yes)
            .build(),
        std::move(ctx));

    const auto selfWeak = weak_from_this();

    auto processedNoteFuture = threading::then(
        std::move(getNoteFuture),
        [promise, selfWeak](const qevercloud::Note & note) {
            promise->addResult(note);
            promise->finish();

            const auto self = selfWeak.lock();
            if (self) {
                self->onNoteFullDataDownloadFinished();
            }
        });

    threading::onFailed(
        std::move(processedNoteFuture),
        [promise, selfWeak](const QException & e) {
            promise->setException(e);
            promise->finish();

            const auto self = selfWeak.lock();
            if (self) {
                self->onNoteFullDataDownloadFinished();
            }
        });
}

void NoteFullDataDownloader::onNoteFullDataDownloadFinished()
{
    Q_ASSERT(m_inFlightDownloads > 0U);
    --m_inFlightDownloads;

    QueuedRequest request;
    {
        const QMutexLocker lock{&m_queuedRequestsMutex};
        if (m_queuedRequests.isEmpty()) {
            return;
        }

        request = m_queuedRequests.dequeue();
    }

    downloadFullNoteDataImpl(
        std::move(request.m_noteGuid), std::move(request.m_ctx),
        request.m_includeNoteLimits, request.m_promise);
}

} // namespace quentier::synchronization
