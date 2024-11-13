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

#include "NoteFullDataDownloader.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/threading/Future.h>
#include <quentier/threading/QtFutureContinuations.h>

#include <qevercloud/services/INoteStore.h>
#include <qevercloud/types/builders/NoteResultSpecBuilder.h>

#include <QMutexLocker>
#include <QThread>

namespace quentier::synchronization {

NoteFullDataDownloader::NoteFullDataDownloader(
    const quint32 maxInFlightDownloads) :
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
    QNDEBUG(
        "synchronization::NoteFullDataDownloader",
        "NoteFullDataDownloader::downloadFullNoteData: note guid = "
            << noteGuid);

    if (Q_UNLIKELY(!noteStore)) {
        return threading::makeExceptionalFuture<qevercloud::Note>(
            InvalidArgument{ErrorString{
                QStringLiteral("NoteFullDataDownloader: note store is null")}});
    }

    auto promise = std::make_shared<QPromise<qevercloud::Note>>();
    auto future = promise->future();

    const auto inFlightDownloads =
        m_inFlightDownloads.load(std::memory_order_acquire);
    if (inFlightDownloads >= m_maxInFlightDownloads) {
        QNDEBUG(
            "synchronization::NoteFullDataDownloader",
            "Already have " << inFlightDownloads << " current downloads, "
                            << "delaying this note download request");

        // Already have too many requests in flight, will enqueue this one
        // and execute it later, when some of the previous requests are finished
        const QMutexLocker lock{&m_queuedRequestsMutex};
        m_queuedRequests.append(QueuedRequest{
            std::move(noteGuid), std::move(ctx), std::move(noteStore),
            std::move(promise)});

        QNDEBUG(
            "synchronization::NoteFullDataDownloader",
            "Got " << m_queuedRequests.size() << " delayed note download "
                   << "requests now");

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

    QNDEBUG(
        "synchronization::NoteFullDataDownloader",
        "NoteFullDataDownloader::downloadFullNoteDataImpl: note guid = "
            << noteGuid);

    promise->start();

    m_inFlightDownloads.fetch_add(1, std::memory_order_acq_rel);

    if (ctx) {
        ctx = qevercloud::IRequestContextPtr{ctx->clone()};
    }

    auto getNoteFuture = noteStore->getNoteWithResultSpecAsync(
        noteGuid,
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
        [promise, noteGuid, selfWeak](const qevercloud::Note & note) {
            QNDEBUG(
                "synchronization::NoteFullDataDownloader",
                "Successfully downloaded full note data for note guid "
                    << noteGuid);

            promise->addResult(note);
            promise->finish();

            const auto self = selfWeak.lock();
            if (self) {
                self->onNoteFullDataDownloadFinished();
            }
        });

    threading::onFailed(
        std::move(processedNoteFuture), currentThread,
        [promise, selfWeak,
         noteGuid = std::move(noteGuid)](const QException & e) {
            QNWARNING(
                "synchronization::NoteFullDataDownloader",
                "Failed to download full note data for note guid " << noteGuid);

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

        QNDEBUG(
            "synchronization::NoteFullDataDownloader",
            "Processing pending request from note download requests queue, "
                << "got " << m_queuedRequests.size()
                << " delayed requests left");
    }

    downloadFullNoteDataImpl(
        std::move(request.m_noteGuid), request.m_noteStore,
        std::move(request.m_ctx), request.m_promise);
}

} // namespace quentier::synchronization
