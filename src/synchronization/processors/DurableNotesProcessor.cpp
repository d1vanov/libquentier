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

#include "DurableNotesProcessor.h"
#include "INotesProcessor.h"

#include <synchronization/processors/INotesProcessor.h>
#include <synchronization/processors/Utils.h>
#include <synchronization/sync_chunks/Utils.h>
#include <synchronization/types/DownloadNotesStatus.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/threading/QtFutureContinuations.h>
#include <quentier/threading/TrackedTask.h>

#include <qevercloud/types/builders/SyncChunkBuilder.h>

#include <QThread>

#include <algorithm>

namespace quentier::synchronization {

namespace {

class Callback final : public INotesProcessor::ICallback
{
public:
    explicit Callback(
        IDurableNotesProcessor::ICallbackWeakPtr callbackWeak,
        std::weak_ptr<IDurableNotesProcessor> durableProcessorWeak,
        const QDir & syncNotesDir) :
        m_callbackWeak{std::move(callbackWeak)},
        m_durableProcessorWeak{std::move(durableProcessorWeak)},
        m_syncNotesDir{syncNotesDir}
    {}

    // INotesProcessor::ICallback
    void onProcessedNote(
        const qevercloud::Guid & noteGuid,
        const qint32 noteUpdateSequenceNum) noexcept override
    {
        QNDEBUG(
            "synchronization::DurableNotesProcessor",
            "Callback::onProcessedNote: note guid = " << noteGuid << ", usn = "
                                                      << noteUpdateSequenceNum);

        const auto durableProcessor = m_durableProcessorWeak.lock();
        if (!durableProcessor) {
            QNDEBUG(
                "synchronization::DurableNotesProcessor",
                "Durable processor has expired");
            return;
        }

        try {
            utils::writeProcessedNoteInfo(
                noteGuid, noteUpdateSequenceNum, m_syncNotesDir);
        }
        catch (const std::exception & e) {
            QNWARNING(
                "synchronization::DurableNotesProcessor",
                "Failed to write processed note info: "
                    << e.what() << ", note guid = " << noteGuid
                    << ", note usn = " << noteUpdateSequenceNum);
        }
        catch (...) {
            QNWARNING(
                "synchronization::DurableNotesProcessor",
                "Failed to write processed note info: unknown exception, "
                    << "note guid = " << noteGuid
                    << ", note usn = " << noteUpdateSequenceNum);
        }

        if (const auto callback = m_callbackWeak.lock()) {
            callback->onProcessedNote(noteGuid, noteUpdateSequenceNum);
        }
    }

    void onExpungedNote(const qevercloud::Guid & noteGuid) noexcept override
    {
        QNDEBUG(
            "synchronization::DurableNotesProcessor",
            "Callback::onExpungedNote: note guid = " << noteGuid);

        const auto durableProcessor = m_durableProcessorWeak.lock();
        if (!durableProcessor) {
            QNDEBUG(
                "synchronization::DurableNotesProcessor",
                "Durable processor has expired");
            return;
        }

        try {
            utils::writeExpungedNote(noteGuid, m_syncNotesDir);
        }
        catch (const std::exception & e) {
            QNWARNING(
                "synchronization::DurableNotesProcessor",
                "Failed to write expunged note guid: "
                    << e.what() << ", note guid = " << noteGuid);
        }
        catch (...) {
            QNWARNING(
                "synchronization::DurableNotesProcessor",
                "Failed to write expunged note guid: unknown exception, "
                    << "note guid = " << noteGuid);
        }

        if (const auto callback = m_callbackWeak.lock()) {
            callback->onExpungedNote(noteGuid);
        }
    }

    void onFailedToExpungeNote(
        const qevercloud::Guid & noteGuid,
        const QException & e) noexcept override
    {
        QNDEBUG(
            "synchronization::DurableNotesProcessor",
            "Callback::onFailedToExpungeNote: note guid = " << noteGuid
                << ", error: " << e.what());

        const auto durableProcessor = m_durableProcessorWeak.lock();
        if (!durableProcessor) {
            QNDEBUG(
                "synchronization::DurableNotesProcessor",
                "Durable processor has expired");
            return;
        }

        try {
            utils::writeFailedToExpungeNote(noteGuid, m_syncNotesDir);
        }
        catch (const std::exception & e) {
            QNWARNING(
                "synchronization::DurableNotesProcessor",
                "Failed to write failed to expunge note guid: "
                    << e.what() << ", note guid = " << noteGuid);
        }
        catch (...) {
            QNWARNING(
                "synchronization::DurableNotesProcessor",
                "Failed to write failed to expunge note guid: unknown "
                    << "exception, note guid = " << noteGuid);
        }

        if (const auto callback = m_callbackWeak.lock()) {
            callback->onFailedToExpungeNote(noteGuid, e);
        }
    }

    void onNoteFailedToDownload(
        const qevercloud::Note & note, const QException & e) noexcept override
    {
        QNDEBUG(
            "synchronization::DurableNotesProcessor",
            "Callback::onNoteFailedToDownload: note guid = "
                << note.guid().value_or(QStringLiteral("<none>"))
                << ", error: " << e.what());

        const auto durableProcessor = m_durableProcessorWeak.lock();
        if (!durableProcessor) {
            QNDEBUG(
                "synchronization::DurableNotesProcessor",
                "Durable processor has expired");
            return;
        }

        try {
            utils::writeFailedToDownloadNote(note, m_syncNotesDir);
        }
        catch (const std::exception & e) {
            QNWARNING(
                "synchronization::DurableNotesProcessor",
                "Failed to write failed to download note: "
                    << e.what() << ", note: " << note);
        }
        catch (...) {
            QNWARNING(
                "synchronization::DurableNotesProcessor",
                "Failed to write failed to download note: unknown exception, "
                    << "note: " << note);
        }

        if (const auto callback = m_callbackWeak.lock()) {
            callback->onNoteFailedToDownload(note, e);
        }
    }

    void onNoteFailedToProcess(
        const qevercloud::Note & note,
        const QException & e) noexcept override
    {
        QNDEBUG(
            "synchronization::DurableNotesProcessor",
            "Callback::onNoteFailedToProcess: note guid = "
                << note.guid().value_or(QStringLiteral("<none>"))
                << ", error: " << e.what());

        const auto durableProcessor = m_durableProcessorWeak.lock();
        if (!durableProcessor) {
            QNDEBUG(
                "synchronization::DurableNotesProcessor",
                "Durable processor has expired");
            return;
        }

        try {
            utils::writeFailedToProcessNote(note, m_syncNotesDir);
        }
        catch (const std::exception & e) {
            QNWARNING(
                "synchronization::DurableNotesProcessor",
                "Failed to write failed to process note: "
                    << e.what() << ", note: " << note);
        }
        catch (...) {
            QNWARNING(
                "synchronization::DurableNotesProcessor",
                "Failed to write failed to process note: unknown exception, "
                    << "note: " << note);
        }

        if (const auto callback = m_callbackWeak.lock()) {
            callback->onNoteFailedToProcess(note, e);
        }
    }

    void onNoteProcessingCancelled(
        const qevercloud::Note & note) noexcept override
    {
        QNDEBUG(
            "synchronization::DurableNotesProcessor",
            "Callback::onNoteProcessingCancelled: note guid = "
                << note.guid().value_or(QStringLiteral("<none>")));

        const auto durableProcessor = m_durableProcessorWeak.lock();
        if (!durableProcessor) {
            QNDEBUG(
                "synchronization::DurableNotesProcessor",
                "Durable processor has expired");
            return;
        }

        try {
            utils::writeCancelledNote(note, m_syncNotesDir);
        }
        catch (const std::exception & e) {
            QNWARNING(
                "synchronization::DurableNotesProcessor",
                "Failed to write cancelled note: " << e.what()
                                                   << ", note: " << note);
        }
        catch (...) {
            QNWARNING(
                "synchronization::DurableNotesProcessor",
                "Failed to write cancelled note: unknown exception, note: "
                    << note);
        }

        if (const auto callback = m_callbackWeak.lock()) {
            callback->onNoteProcessingCancelled(note);
        }
    }

private:
    const IDurableNotesProcessor::ICallbackWeakPtr m_callbackWeak;
    const std::weak_ptr<IDurableNotesProcessor> m_durableProcessorWeak;
    const QDir m_syncNotesDir;
};

} // namespace

DurableNotesProcessor::DurableNotesProcessor(
    INotesProcessorPtr notesProcessor, const QDir & syncPersistentStorageDir) :
    m_notesProcessor{std::move(notesProcessor)},
    m_syncNotesDir{[&] {
        QDir lastSyncDataDir{syncPersistentStorageDir.absoluteFilePath(
            QStringLiteral("last_sync_data"))};
        return QDir{lastSyncDataDir.absoluteFilePath(QStringLiteral("notes"))};
    }()}
{
    if (Q_UNLIKELY(!m_notesProcessor)) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "DurableNotesProcessor ctor: notes processor is null")}};
    }
}

QFuture<DownloadNotesStatusPtr> DurableNotesProcessor::processNotes(
    const QList<qevercloud::SyncChunk> & syncChunks,
    utility::cancelers::ICancelerPtr canceler,
    qevercloud::IRequestContextPtr ctx,
    const std::optional<qevercloud::Guid> & linkedNotebookGuid,
    ICallbackWeakPtr callbackWeak)
{
    Q_ASSERT(canceler);

    const QDir dir = syncNotesDir(linkedNotebookGuid);

    // First need to check whether there are notes which failed to be processed
    // or which processing was cancelled. If such notes exist, they need to be
    // processed first.
    auto previousNotes = notesFromPreviousSync(dir);
    auto previousExpungedNotes = failedToExpungeNotesFromPreviousSync(dir);

    // Also need to check whether there are notes which were fully processed
    // during the last sync within the sync chunks. If so, such notes should
    // not be processed again.
    const auto alreadyProcessedNotesInfo =
        utils::processedNotesInfoFromLastSync(dir);

    const auto alreadyExpungedNoteGuids =
        utils::noteGuidsExpungedDuringLastSync(dir);

    if (alreadyProcessedNotesInfo.isEmpty() &&
        alreadyExpungedNoteGuids.isEmpty())
    {
        return processNotesImpl(
            syncChunks, std::move(canceler), std::move(ctx),
            std::move(previousNotes), std::move(previousExpungedNotes),
            linkedNotebookGuid, std::move(callbackWeak));
    }

    auto filteredSyncChunks = syncChunks;
    for (auto & syncChunk: filteredSyncChunks) {
        if (syncChunk.notes()) {
            auto & notes = *syncChunk.mutableNotes();
            for (auto it = notes.begin(); it != notes.end();) {
                if (Q_UNLIKELY(!it->guid())) {
                    QNWARNING(
                        "synchronization::DurableNotesProcessor",
                        "Detected note within sync chunks without guid: "
                            << *it);
                    it = notes.erase(it);
                    continue;
                }

                if (Q_UNLIKELY(!it->updateSequenceNum())) {
                    QNWARNING(
                        "synchronization::DurableNotesProcessor",
                        "Detected note within sync chunks without usn: "
                            << *it);
                    it = notes.erase(it);
                    continue;
                }

                const auto processedNoteIt =
                    alreadyProcessedNotesInfo.find(*it->guid());

                if (processedNoteIt != alreadyProcessedNotesInfo.end() &&
                    processedNoteIt.value() >= *it->updateSequenceNum())
                {
                    QNDEBUG(
                        "synchronization::DurableNotesProcessor",
                        "Already processed note with guid "
                            << *it->guid() << " and usn "
                            << processedNoteIt.value()
                            << " while note from sync chunk has usn "
                            << *it->updateSequenceNum()
                            << ", erasing this note from the sync chunk");
                    it = notes.erase(it);
                    continue;
                }

                ++it;
            }
        }

        if (syncChunk.expungedNotes()) {
            auto & expungedNotes = *syncChunk.mutableExpungedNotes();
            for (auto it = expungedNotes.begin(); it != expungedNotes.end();) {
                const auto expungedNoteIt = std::find_if(
                    alreadyExpungedNoteGuids.begin(),
                    alreadyExpungedNoteGuids.end(),
                    [it](const auto & guid) { return guid == *it; });
                if (expungedNoteIt != alreadyExpungedNoteGuids.end()) {
                    QNDEBUG(
                        "synchronization::DurableNotesProcessor",
                        "Already expunged note guid "
                            << *expungedNoteIt
                            << ", erasing it from the sync chunk");
                    it = expungedNotes.erase(it);
                    continue;
                }

                ++it;
            }
        }
    }

    return processNotesImpl(
        filteredSyncChunks, std::move(canceler), std::move(ctx),
        std::move(previousNotes), std::move(previousExpungedNotes),
        linkedNotebookGuid, std::move(callbackWeak));
}

QList<qevercloud::Note> DurableNotesProcessor::notesFromPreviousSync(
    const QDir & dir) const
{
    if (!dir.exists()) {
        return {};
    }

    QList<qevercloud::Note> result;
    result << utils::notesWhichFailedToDownloadDuringLastSync(dir);
    result << utils::notesWhichFailedToProcessDuringLastSync(dir);
    result << utils::notesCancelledDuringLastSync(dir);
    return result;
}

QList<qevercloud::Guid>
    DurableNotesProcessor::failedToExpungeNotesFromPreviousSync(
        const QDir & dir) const
{
    if (!dir.exists()) {
        return {};
    }

    return utils::noteGuidsWhichFailedToExpungeDuringLastSync(dir);
}

QFuture<DownloadNotesStatusPtr> DurableNotesProcessor::processNotesImpl(
    const QList<qevercloud::SyncChunk> & syncChunks,
    utility::cancelers::ICancelerPtr canceler,
    qevercloud::IRequestContextPtr ctx,
    QList<qevercloud::Note> previousNotes,
    QList<qevercloud::Guid> previousExpungedNotes,
    const std::optional<qevercloud::Guid> & linkedNotebookGuid,
    ICallbackWeakPtr callbackWeak)
{
    const auto selfWeak = weak_from_this();
    auto * currentThread = QThread::currentThread();

    auto promise = std::make_shared<QPromise<DownloadNotesStatusPtr>>();
    auto future = promise->future();
    promise->start();

    const QDir dir = syncNotesDir(linkedNotebookGuid);

    if (previousNotes.isEmpty() && previousExpungedNotes.isEmpty()) {
        QNDEBUG(
            "synchronization::DurableNotesProcessor",
            "DurableNotesProcessor::processNotesImpl: trying to process "
                << previousNotes.size() << " previous notes");

        auto callback =
            std::make_shared<Callback>(std::move(callbackWeak), selfWeak, dir);

        auto processSyncChunksFuture = m_notesProcessor->processNotes(
            syncChunks, std::move(canceler), std::move(ctx), callback);

        threading::thenOrFailed(
            std::move(processSyncChunksFuture), currentThread, promise,
            [promise,
             callback = std::move(callback)](DownloadNotesStatusPtr status) {
                QNDEBUG(
                    "synchronization::DurableNotesProcessor",
                    "Processed previous notes, status: "
                        << (status ? status->toString()
                                   : QStringLiteral("<null>")));
                promise->addResult(std::move(status));
                promise->finish();
            });

        return future;
    }

    if (!previousExpungedNotes.isEmpty()) {
        QNDEBUG(
            "synchronization::DurableNotesProcessor",
            "DurableNotesProcessor::processNotesImpl: trying to process "
                << previousExpungedNotes.size() << " previous expunged "
                << "notes");

        const auto pseudoSyncChunks = QList<qevercloud::SyncChunk>{}
            << qevercloud::SyncChunkBuilder{}
                   .setExpungedNotes(std::move(previousExpungedNotes))
                   .build();

        auto callback = std::make_shared<Callback>(callbackWeak, selfWeak, dir);

        auto expungeNotesFuture = m_notesProcessor->processNotes(
            pseudoSyncChunks, canceler, ctx, callback);

        threading::thenOrFailed(
            std::move(expungeNotesFuture), currentThread, promise,
            threading::TrackedTask{
                selfWeak,
                [this, selfWeak, promise, currentThread, linkedNotebookGuid,
                 syncChunks = syncChunks,
                 previousNotes = std::move(previousNotes),
                 canceler = std::move(canceler), ctx = std::move(ctx),
                 callbackWeak = std::move(callbackWeak),
                 callback = std::move(callback)](
                    DownloadNotesStatusPtr expungeNotesStatus) mutable {
                    QNDEBUG(
                        "synchronization::DurableNotesProcessor",
                        "Processed previous expunged note, status: "
                            << (expungeNotesStatus
                                    ? expungeNotesStatus->toString()
                                    : QStringLiteral("<null>")));

                    QNDEBUG(
                        "synchronization::DurableNotesProcessor",
                        "DurableNotesProcessor::processNotesImpl: trying to "
                            << "process " << previousNotes.size()
                            << " previous notes");

                    auto processNotesFuture = processNotesImpl(
                        syncChunks, std::move(canceler), std::move(ctx),
                        std::move(previousNotes), {}, linkedNotebookGuid,
                        std::move(callbackWeak));

                    threading::thenOrFailed(
                        std::move(processNotesFuture), currentThread, promise,
                        threading::TrackedTask{
                            selfWeak,
                            [selfWeak, promise,
                             expungeNotesStatus = std::move(expungeNotesStatus),
                             syncChunks = std::move(syncChunks)](
                                DownloadNotesStatusPtr status) mutable {
                                QNDEBUG(
                                    "synchronization::DurableNotesProcessor",
                                    "Processed previous notes, status: "
                                        << (status ? status->toString()
                                                   : QStringLiteral("<null>")));

                                *status = utils::mergeDownloadNotesStatuses(
                                    std::move(*status), *expungeNotesStatus);

                                promise->addResult(std::move(status));
                                promise->finish();
                            }});
                }});

        return future;
    }

    if (!previousNotes.isEmpty()) {
        const auto pseudoSyncChunks = QList<qevercloud::SyncChunk>{}
            << qevercloud::SyncChunkBuilder{}
                   .setNotes(std::move(previousNotes))
                   .build();

        QNDEBUG(
            "synchronization::DurableNotesProcessor",
            "DurableNotesProcessor::processNotesImpl: trying to process "
                << previousNotes.size() << " previous notes");

        auto callback = std::make_shared<Callback>(callbackWeak, selfWeak, dir);

        auto notesFuture = m_notesProcessor->processNotes(
            pseudoSyncChunks, canceler, ctx, callback);

        threading::thenOrFailed(
            std::move(notesFuture), currentThread, promise,
            threading::TrackedTask{
                selfWeak,
                [this, selfWeak, promise, currentThread, linkedNotebookGuid,
                 canceler = std::move(canceler), ctx = std::move(ctx),
                 syncChunks, callbackWeak = std::move(callbackWeak),
                 callback = std::move(callback)](
                    DownloadNotesStatusPtr status) mutable {
                    QNDEBUG(
                        "synchronization::DurableNotesProcessor",
                        "Processed previous notes, status: "
                            << (status ? status->toString()
                                       : QStringLiteral("<null>")));

                    auto processNotesFuture = processNotesImpl(
                        syncChunks, canceler, std::move(ctx), {}, {},
                        linkedNotebookGuid, std::move(callbackWeak));

                    threading::thenOrFailed(
                        std::move(processNotesFuture), currentThread, promise,
                        threading::TrackedTask{
                            selfWeak,
                            [selfWeak, promise,
                             processNotesStatus = std::move(status)](
                                DownloadNotesStatusPtr status) mutable {
                                *status = utils::mergeDownloadNotesStatuses(
                                    std::move(*status), *processNotesStatus);

                                promise->addResult(std::move(status));
                                promise->finish();
                            }});
                }});

        return future;
    }

    QNDEBUG(
        "synchronization::DurableNotesProcessor",
        "No previous notes or expunged note guids");

    auto callback =
        std::make_shared<Callback>(std::move(callbackWeak), selfWeak, dir);

    auto processSyncChunksFuture =
        m_notesProcessor->processNotes(syncChunks, canceler, ctx, callback);

    threading::thenOrFailed(
        std::move(processSyncChunksFuture), currentThread, promise,
        [promise,
         callback = std::move(callback)](DownloadNotesStatusPtr status) {
            promise->addResult(std::move(status));
            promise->finish();
        });

    return future;
}

QDir DurableNotesProcessor::syncNotesDir(
    const std::optional<qevercloud::Guid> & linkedNotebookGuid) const
{
    return linkedNotebookGuid
        ? QDir{m_syncNotesDir.absoluteFilePath(
              QStringLiteral("linkedNotebooks/") + *linkedNotebookGuid)}
        : m_syncNotesDir;
}

} // namespace quentier::synchronization
