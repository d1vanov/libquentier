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

#include "NoteFullDataDownloader.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/threading/Future.h>
#include <quentier/threading/QtFutureContinuations.h>

#include <qevercloud/services/INoteStore.h>
#include <qevercloud/types/builders/NoteResultSpecBuilder.h>

#include <QMutexLocker>
#include <QThread>

namespace quentier::synchronization {

NoteFullDataDownloader::NoteFullDataDownloader(quint32 maxInFlightDownloads) :
    m_maxInFlightDownloads{maxInFlightDownloads}
{
    if (Q_UNLIKELY(m_maxInFlightDownloads == 0U)) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "NoteFullDataDownloader ctor: max in flight downloads must be "
            "positive")}};
    }
}

QFuture<qevercloud::Note> NoteFullDataDownloader::downloadFullNoteData(
    qevercloud::Guid noteGuid, qevercloud::INoteStorePtr noteStore,
    qevercloud::IRequestContextPtr ctx)
{
    if (Q_UNLIKELY(!noteStore)) {
        return threading::makeExceptionalFuture<qevercloud::Note>(
            InvalidArgument{ErrorString{
                QStringLiteral("NoteFullDataDownloader: note store is null")}});
    }

    auto promise = std::make_shared<QPromise<qevercloud::Note>>();
    auto future = promise->future();

    if (m_inFlightDownloads.load(std::memory_order_acquire) >=
        m_maxInFlightDownloads)
    {
        // Already have too much requests in flight, will enqueue this one
        // and execute it later, when some of the previous requests are finished
        const QMutexLocker lock{&m_queuedRequestsMutex};
        m_queuedRequests.append(QueuedRequest{
            std::move(noteGuid), std::move(ctx), std::move(noteStore),
            std::move(promise)});
        return future;
    }

    downloadFullNoteDataImpl(
        std::move(noteGuid), noteStore, std::move(ctx), promise);

    return future;
}

void NoteFullDataDownloader::downloadFullNoteDataImpl(
    qevercloud::Guid noteGuid, const qevercloud::INoteStorePtr & noteStore,
    qevercloud::IRequestContextPtr ctx,
    const std::shared_ptr<QPromise<qevercloud::Note>> & promise)
{
    Q_ASSERT(noteStore);

    Q_ASSERT(promise);
    promise->start();

    m_inFlightDownloads.fetch_add(1, std::memory_order_acq_rel);

    auto getNoteFuture = noteStore->getNoteWithResultSpecAsync(
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
                noteStore->linkedNotebookGuid().has_value())
            .build(),
        std::move(ctx));

    const auto selfWeak = weak_from_this();
    auto * currentThread = QThread::currentThread();

    auto processedNoteFuture = threading::then(
        std::move(getNoteFuture), currentThread,
        [promise, selfWeak](const qevercloud::Note & note) {
            promise->addResult(note);
            promise->finish();

            const auto self = selfWeak.lock();
            if (self) {
                self->onNoteFullDataDownloadFinished();
            }
        });

    threading::onFailed(
        std::move(processedNoteFuture), currentThread,
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

    downloadFullNoteDataImpl(
        std::move(request.m_noteGuid), request.m_noteStore,
        std::move(request.m_ctx), request.m_promise);
}

} // namespace quentier::synchronization
