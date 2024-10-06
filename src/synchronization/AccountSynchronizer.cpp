/*
 * Copyright 2023-2024 Dmitry Ivanov
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

#include "AccountSynchronizer.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/RuntimeError.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/synchronization/ISyncStateStorage.h>
#include <quentier/synchronization/types/IDownloadNotesStatus.h>
#include <quentier/synchronization/types/IDownloadResourcesStatus.h>
#include <quentier/synchronization/types/ISendStatus.h>
#include <quentier/threading/Factory.h>
#include <quentier/threading/Future.h>
#include <quentier/threading/TrackedTask.h>

#include <synchronization/IAuthenticationInfoProvider.h>
#include <synchronization/IDownloader.h>
#include <synchronization/ISender.h>
#include <synchronization/Utils.h>
#include <synchronization/sync_chunks/ISyncChunksStorage.h>
#include <synchronization/sync_chunks/Utils.h>
#include <synchronization/types/DownloadNotesStatus.h>
#include <synchronization/types/DownloadResourcesStatus.h>
#include <synchronization/types/SendStatus.h>
#include <synchronization/types/SyncChunksDataCounters.h>
#include <synchronization/types/SyncResult.h>
#include <synchronization/types/SyncState.h>

#include <qevercloud/exceptions/EDAMSystemExceptionAuthExpired.h>
#include <qevercloud/exceptions/EDAMSystemExceptionRateLimitReached.h>
#include <qevercloud/utility/ToRange.h>

#include <QMutex>
#include <QMutexLocker>
#include <QTextStream>
#include <QThread>

#include <algorithm>
#include <exception>
#include <utility>

namespace quentier::synchronization {

namespace {

template <class T>
struct GuidExtractor
{
    [[nodiscard]] static std::optional<qevercloud::Guid> guid(const T & item)
    {
        return item.guid();
    }
};

template <>
struct GuidExtractor<qevercloud::Guid>
{
    [[nodiscard]] static std::optional<qevercloud::Guid> guid(
        const qevercloud::Guid & guid)
    {
        return guid;
    }
};

template <class T1, class T2>
void mergeItemsWithExceptions(
    const QList<std::pair<T1, std::shared_ptr<QException>>> & from,
    QList<std::pair<T2, std::shared_ptr<QException>>> & to)
{
    for (const auto & itemWithException: from) {
        const auto it = std::find_if(to.begin(), to.end(), [&](const auto & n) {
            return GuidExtractor<T2>::guid(n.first) ==
                GuidExtractor<T1>::guid(itemWithException.first);
        });
        if (it == to.end()) {
            to << itemWithException;
        }
        else {
            *it = itemWithException;
        }
    }
}

void merge(const IDownloadNotesStatus & from, DownloadNotesStatus & to)
{
    to.m_totalNewNotes += from.totalNewNotes();
    to.m_totalUpdatedNotes += from.totalUpdatedNotes();
    to.m_totalExpungedNotes += from.totalExpungedNotes();

    mergeItemsWithExceptions(
        from.notesWhichFailedToDownload(), to.m_notesWhichFailedToDownload);

    mergeItemsWithExceptions(
        from.notesWhichFailedToProcess(), to.m_notesWhichFailedToProcess);

    mergeItemsWithExceptions(
        from.noteGuidsWhichFailedToExpunge(),
        to.m_noteGuidsWhichFailedToExpunge);

    const auto processedNoteGuidsAndUsns = from.processedNoteGuidsAndUsns();
    for (const auto it: qevercloud::toRange(processedNoteGuidsAndUsns)) {
        to.m_processedNoteGuidsAndUsns[it.key()] = it.value();
    }

    const auto cancelledNoteGuidsAndUsns = from.cancelledNoteGuidsAndUsns();
    for (const auto it: qevercloud::toRange(cancelledNoteGuidsAndUsns)) {
        to.m_cancelledNoteGuidsAndUsns[it.key()] = it.value();
    }

    to.m_expungedNoteGuids << from.expungedNoteGuids();
    to.m_expungedNoteGuids.removeDuplicates();

    to.m_stopSynchronizationError = from.stopSynchronizationError();
}

void merge(const IDownloadResourcesStatus & from, DownloadResourcesStatus & to)
{
    to.m_totalNewResources += from.totalNewResources();
    to.m_totalUpdatedResources += from.totalUpdatedResources();

    mergeItemsWithExceptions(
        from.resourcesWhichFailedToDownload(),
        to.m_resourcesWhichFailedToDownload);

    mergeItemsWithExceptions(
        from.resourcesWhichFailedToProcess(),
        to.m_resourcesWhichFailedToProcess);

    const auto processedResourceGuidsAndUsns =
        from.processedResourceGuidsAndUsns();
    for (const auto it: qevercloud::toRange(processedResourceGuidsAndUsns)) {
        to.m_processedResourceGuidsAndUsns[it.key()] = it.value();
    }

    const auto cancelledResourceGuidsAndUsns =
        from.cancelledResourceGuidsAndUsns();
    for (const auto it: qevercloud::toRange(cancelledResourceGuidsAndUsns)) {
        to.m_cancelledResourceGuidsAndUsns[it.key()] = it.value();
    }

    to.m_stopSynchronizationError = from.stopSynchronizationError();
}

void merge(const ISendStatus & from, SendStatus & to)
{
    // NOTE: when computing merged total attempted to send item count we will
    // only add the number of successfully sent items because those which were
    // not successfully sent previously would be attempted to be sent again.
    to.m_totalAttemptedToSendNotes += from.totalSuccessfullySentNotes();
    to.m_totalAttemptedToSendNotebooks += from.totalSuccessfullySentNotebooks();
    to.m_totalAttemptedToSendTags += from.totalSuccessfullySentTags();
    to.m_totalAttemptedToSendSavedSearches +=
        from.totalSuccessfullySentSavedSearches();

    to.m_totalSuccessfullySentNotes += from.totalSuccessfullySentNotes();
    to.m_totalSuccessfullySentNotebooks +=
        from.totalSuccessfullySentNotebooks();

    to.m_totalSuccessfullySentTags += from.totalSuccessfullySentTags();
    to.m_totalSuccessfullySentSavedSearches +=
        from.totalSuccessfullySentSavedSearches();

    mergeItemsWithExceptions(from.failedToSendNotes(), to.m_failedToSendNotes);
    mergeItemsWithExceptions(
        from.failedToSendNotebooks(), to.m_failedToSendNotebooks);

    mergeItemsWithExceptions(from.failedToSendTags(), to.m_failedToSendTags);
    mergeItemsWithExceptions(
        from.failedToSendSavedSearches(), to.m_failedToSendSavedSearches);

    to.m_stopSynchronizationError = from.stopSynchronizationError();
    to.m_needToRepeatIncrementalSync = from.needToRepeatIncrementalSync();
}

void merge(const ISyncState & from, SyncState & to)
{
    to.m_userDataUpdateCount = from.userDataUpdateCount();
    to.m_userDataLastSyncTime = from.userDataLastSyncTime();

    const auto linkedNotebookUpdateCounts = from.linkedNotebookUpdateCounts();
    for (const auto it: qevercloud::toRange(linkedNotebookUpdateCounts)) {
        to.m_linkedNotebookUpdateCounts[it.key()] = it.value();
    }

    const auto linkedNotebookLastSyncTimes = from.linkedNotebookLastSyncTimes();
    for (const auto it: qevercloud::toRange(linkedNotebookLastSyncTimes)) {
        to.m_linkedNotebookLastSyncTimes[it.key()] = it.value();
    }
}

void merge(const ISyncChunksDataCounters & from, SyncChunksDataCounters & to)
{
    to.m_totalSavedSearches += from.totalSavedSearches();
    to.m_totalExpungedSavedSearches += from.totalExpungedSavedSearches();
    to.m_addedSavedSearches += from.addedSavedSearches();
    to.m_updatedSavedSearches += from.updatedSavedSearches();
    to.m_expungedSavedSearches += from.expungedSavedSearches();

    to.m_totalTags += from.totalTags();
    to.m_totalExpungedTags += from.totalExpungedTags();
    to.m_addedTags += from.addedTags();
    to.m_updatedTags += from.updatedTags();
    to.m_expungedTags += from.expungedTags();

    to.m_totalLinkedNotebooks += from.totalLinkedNotebooks();
    to.m_totalExpungedLinkedNotebooks += from.totalExpungedLinkedNotebooks();
    to.m_addedLinkedNotebooks += from.addedLinkedNotebooks();
    to.m_updatedLinkedNotebooks += from.updatedLinkedNotebooks();
    to.m_expungedLinkedNotebooks += from.expungedLinkedNotebooks();

    to.m_totalNotebooks += from.totalNotebooks();
    to.m_totalExpungedNotebooks += from.totalExpungedNotebooks();
    to.m_addedNotebooks += from.addedNotebooks();
    to.m_updatedNotebooks += from.updatedNotebooks();
    to.m_expungedNotebooks += from.expungedNotebooks();
}

[[nodiscard]] bool somethingDownloaded(
    const ISyncChunksDataCounters & counters) noexcept
{
    return counters.totalSavedSearches() != 0 ||
        counters.totalExpungedSavedSearches() != 0 ||
        counters.totalTags() != 0 || counters.totalExpungedTags() != 0 ||
        counters.totalLinkedNotebooks() != 0 ||
        counters.totalExpungedLinkedNotebooks() != 0 ||
        counters.totalNotebooks() != 0 ||
        counters.totalExpungedNotebooks() != 0;
}

[[nodiscard]] bool somethingDownloaded(
    const IDownloadNotesStatus & status) noexcept
{
    return status.totalNewNotes() != 0 || status.totalUpdatedNotes() != 0 ||
        status.totalExpungedNotes() != 0;
}

[[nodiscard]] bool somethingDownloaded(
    const IDownloadResourcesStatus & status) noexcept
{
    return status.totalNewResources() != 0 ||
        status.totalUpdatedResources() != 0;
}

[[nodiscard]] bool somethingDownloaded(
    const IDownloader::LocalResult & localResult) noexcept
{
    return (localResult.syncChunksDataCounters &&
            somethingDownloaded(*localResult.syncChunksDataCounters)) ||
        (localResult.downloadNotesStatus &&
         somethingDownloaded(*localResult.downloadNotesStatus)) ||
        (localResult.downloadResourcesStatus &&
         somethingDownloaded(*localResult.downloadResourcesStatus));
}

[[nodiscard]] bool somethingDownloaded(
    const IDownloader::Result & result) noexcept
{
    if (somethingDownloaded(result.userOwnResult)) {
        return true;
    }

    return std::any_of(
        result.linkedNotebookResults.constBegin(),
        result.linkedNotebookResults.constEnd(),
        [](const IDownloader::LocalResult & result) {
            return somethingDownloaded(result);
        });
}

} // namespace

class AccountSynchronizer::CallbackWrapper :
    public IAccountSynchronizer::ICallback
{
public:
    explicit CallbackWrapper(
        IAccountSynchronizer::ICallbackWeakPtr callbackWeak,
        AccountSynchronizer::ContextWeakPtr contextWeak) :
        m_callbackWeak{std::move(callbackWeak)},
        m_contextWeak{std::move(contextWeak)}
    {}

    [[nodiscard]] SyncChunksDataCountersPtr userOwnSyncChunksDataCounters()
        const
    {
        const QMutexLocker locker{&m_mutex};
        return m_userOwnSyncChunksDataCounters;
    }

    [[nodiscard]] QHash<qevercloud::Guid, SyncChunksDataCountersPtr>
        linkedNotebookSyncChunksDataCounters() const
    {
        const QMutexLocker locker{&m_mutex};
        return m_linkedNotebookSyncChunksDataCounters;
    }

public: // IDownloader::ICallback
    void onSyncChunksDownloadProgress(
        const qint32 highestDownloadedUsn, const qint32 highestServerUsn,
        const qint32 lastPreviousUsn) override
    {
        QNDEBUG(
            "synchronization::AccountSynchronizer::CallbackWrapper",
            "AccountSynchronizer::CallbackWrapper::onSyncChunksDownloadProgress"
                << ": highest downloaded usn = " << highestDownloadedUsn
                << ", highest server usn = " << highestServerUsn
                << ", last previous usn = " << lastPreviousUsn);

        if (const auto callback = m_callbackWeak.lock()) {
            callback->onSyncChunksDownloadProgress(
                highestDownloadedUsn, highestServerUsn, lastPreviousUsn);
        }
    }

    void onSyncChunksDownloaded(
        QList<qevercloud::SyncChunk> syncChunks) override
    {
        QNDEBUG(
            "synchronization::AccountSynchronizer::CallbackWrapper",
            "AccountSynchronizer::CallbackWrapper::onSyncChunksDownloaded: "
                << syncChunksUsnInfo(syncChunks));

        if (const auto callback = m_callbackWeak.lock()) {
            callback->onSyncChunksDownloaded(syncChunks);
        }

        if (const auto context = m_contextWeak.lock()) {
            Q_ASSERT(context->syncChunksMutex);
            const QMutexLocker locker{context->syncChunksMutex.get()};
            context->downloadedUserOwnSyncChunks = std::move(syncChunks);
        }
    }

    void onSyncChunksDataProcessingProgress(
        SyncChunksDataCountersPtr counters) override
    {
        Q_ASSERT(counters);

        QNDEBUG(
            "synchronization::AccountSynchronizer::CallbackWrapper",
            "AccountSynchronizer::CallbackWrapper::"
                << "onSyncChunksDataProcessingProgress: " << *counters);

        if (const auto callback = m_callbackWeak.lock()) {
            callback->onSyncChunksDataProcessingProgress(counters);
        }

        const QMutexLocker locker{&m_mutex};
        m_userOwnSyncChunksDataCounters = std::move(counters);
    }

    void onStartLinkedNotebooksDataDownloading(
        const QList<qevercloud::LinkedNotebook> & linkedNotebooks) override
    {
        QNDEBUG(
            "synchronization::AccountSynchronizer::CallbackWrapper",
            "AccountSynchronizer::CallbackWrapper::"
                << "onStartLinkedNotebooksDataDownloading: "
                << linkedNotebooksInfo(linkedNotebooks));

        if (const auto callback = m_callbackWeak.lock()) {
            callback->onStartLinkedNotebooksDataDownloading(linkedNotebooks);
        }
    }

    void onLinkedNotebookSyncChunksDownloadProgress(
        const qint32 highestDownloadedUsn, const qint32 highestServerUsn,
        const qint32 lastPreviousUsn,
        const qevercloud::LinkedNotebook & linkedNotebook) override
    {
        QNDEBUG(
            "synchronization::AccountSynchronizer::CallbackWrapper",
            "AccountSynchronizer::CallbackWrapper::"
                << "onLinkedNotebookSyncChunksDownloadProgress: "
                << "highest downloaded usn = " << highestDownloadedUsn
                << ", highest server usn = " << highestServerUsn
                << ", last previous usn = " << lastPreviousUsn
                << ", linked notebook: " << linkedNotebookInfo(linkedNotebook));

        if (const auto callback = m_callbackWeak.lock()) {
            callback->onLinkedNotebookSyncChunksDownloadProgress(
                highestDownloadedUsn, highestServerUsn, lastPreviousUsn,
                linkedNotebook);
        }
    }

    void onLinkedNotebookSyncChunksDownloaded(
        const qevercloud::LinkedNotebook & linkedNotebook,
        QList<qevercloud::SyncChunk> syncChunks) override
    {
        QNDEBUG(
            "synchronization::AccountSynchronizer::CallbackWrapper",
            "AccountSynchronizer::CallbackWrapper::"
                << "onLinkedNotebookSyncChunksDownloaded: linked notebook: "
                << linkedNotebookInfo(linkedNotebook)
                << ", sync chunks: " << syncChunksUsnInfo(syncChunks));

        if (const auto callback = m_callbackWeak.lock()) {
            callback->onLinkedNotebookSyncChunksDownloaded(
                linkedNotebook, syncChunks);
        }

        if (const auto context = m_contextWeak.lock()) {
            Q_ASSERT(context->syncChunksMutex);
            Q_ASSERT(linkedNotebook.guid());
            const QMutexLocker locker{context->syncChunksMutex.get()};
            context
                ->downloadedLinkedNotebookSyncChunks[*linkedNotebook.guid()] =
                std::move(syncChunks);
        }
    }

    void onLinkedNotebookSyncChunksDataProcessingProgress(
        SyncChunksDataCountersPtr counters,
        const qevercloud::LinkedNotebook & linkedNotebook) override
    {
        Q_ASSERT(linkedNotebook.guid());

        QNDEBUG(
            "synchronization::AccountSynchronizer::CallbackWrapper",
            "AccountSynchronizer::CallbackWrapper::"
                << "onLinkedNotebookSyncChunksDataProcessingProgress: "
                << "linked notebook: " << linkedNotebookInfo(linkedNotebook)
                << ", sync chunk data counters: " << *counters);

        if (const auto callback = m_callbackWeak.lock()) {
            callback->onLinkedNotebookSyncChunksDataProcessingProgress(
                counters, linkedNotebook);
        }

        const QMutexLocker locker{&m_mutex};
        m_linkedNotebookSyncChunksDataCounters[*linkedNotebook.guid()] =
            std::move(counters);
    }

    void onNotesDownloadProgress(
        const quint32 notesDownloaded,
        const quint32 totalNotesToDownload) override
    {
        QNDEBUG(
            "synchronization::AccountSynchronizer::CallbackWrapper",
            "AccountSynchronizer::CallbackWrapper::onNotesDownloadProgress: "
                << "notes downloaded: " << notesDownloaded
                << ", total notes to download: " << totalNotesToDownload);

        if (const auto callback = m_callbackWeak.lock()) {
            callback->onNotesDownloadProgress(
                notesDownloaded, totalNotesToDownload);
        }
    }

    void onLinkedNotebookNotesDownloadProgress(
        const quint32 notesDownloaded, const quint32 totalNotesToDownload,
        const qevercloud::LinkedNotebook & linkedNotebook) override
    {
        QNDEBUG(
            "synchronization::AccountSynchronizer::CallbackWrapper",
            "AccountSynchronizer::CallbackWrapper::"
                << "onLinkedNotebookNotesDownloadProgress: linked notebook: "
                << linkedNotebookInfo(linkedNotebook)
                << ", notes downloaded: " << notesDownloaded
                << ", total notes to download: " << totalNotesToDownload);

        if (const auto callback = m_callbackWeak.lock()) {
            callback->onLinkedNotebookNotesDownloadProgress(
                notesDownloaded, totalNotesToDownload, linkedNotebook);
        }
    }

    void onResourcesDownloadProgress(
        const quint32 resourcesDownloaded,
        const quint32 totalResourcesToDownload) override
    {
        QNDEBUG(
            "synchronization::AccountSynchronizer::CallbackWrapper",
            "AccountSynchronizer::CallbackWrapper::"
                << "onResourcesDownloadProgress: resources downloaded: "
                << resourcesDownloaded << ", total resources to download: "
                << totalResourcesToDownload);

        if (const auto callback = m_callbackWeak.lock()) {
            callback->onResourcesDownloadProgress(
                resourcesDownloaded, totalResourcesToDownload);
        }
    }

    void onLinkedNotebookResourcesDownloadProgress(
        const quint32 resourcesDownloaded,
        const quint32 totalResourcesToDownload,
        const qevercloud::LinkedNotebook & linkedNotebook) override
    {
        QNDEBUG(
            "synchronization::AccountSynchronizer::CallbackWrapper",
            "AccountSynchronizer::CallbackWrapper::"
                << "onLinkedNotebookResourcesDownloadProgress: "
                << "linked notebook: " << linkedNotebookInfo(linkedNotebook)
                << ", resources downloaded: " << resourcesDownloaded
                << ", total resources to download: "
                << totalResourcesToDownload);

        if (const auto callback = m_callbackWeak.lock()) {
            callback->onLinkedNotebookResourcesDownloadProgress(
                resourcesDownloaded, totalResourcesToDownload, linkedNotebook);
        }
    }

public: // ISender::ICallback
    void onUserOwnSendStatusUpdate(SendStatusPtr sendStatus) override
    {
        QNDEBUG(
            "synchronization::AccountSynchronizer::CallbackWrapper",
            "AccountSynchronizer::CallbackWrapper::onUserOwnSendStatusUpdate: "
                << (sendStatus ? sendStatus->toString()
                               : QStringLiteral("<null>")));

        if (const auto callback = m_callbackWeak.lock()) {
            callback->onUserOwnSendStatusUpdate(std::move(sendStatus));
        }
    }

    void onLinkedNotebookSendStatusUpdate(
        const qevercloud::Guid & linkedNotebookGuid,
        SendStatusPtr sendStatus) override
    {
        QNDEBUG(
            "synchronization::AccountSynchronizer::CallbackWrapper",
            "AccountSynchronizer::CallbackWrapper::"
                << "onLinkedNotebookSendStatusUpdate: linked notebook guid = "
                << linkedNotebookGuid << ", send status: "
                << (sendStatus ? sendStatus->toString()
                               : QStringLiteral("<null>")));

        if (const auto callback = m_callbackWeak.lock()) {
            callback->onLinkedNotebookSendStatusUpdate(
                linkedNotebookGuid, std::move(sendStatus));
        }
    }

public: // ICallback
    void onDownloadFinished(const bool dataDownloaded) override
    {
        QNDEBUG(
            "synchronization::AccountSynchronizer::CallbackWrapper",
            "AccountSynchronizer::CallbackWrapper::onDownloadFinished: "
                << "data downloaded = " << (dataDownloaded ? "true" : "false"));

        if (const auto callback = m_callbackWeak.lock()) {
            callback->onDownloadFinished(dataDownloaded);
        }
    }

private:
    const IAccountSynchronizer::ICallbackWeakPtr m_callbackWeak;
    const AccountSynchronizer::ContextWeakPtr m_contextWeak;

    mutable QMutex m_mutex;
    SyncChunksDataCountersPtr m_userOwnSyncChunksDataCounters;
    QHash<qevercloud::Guid, SyncChunksDataCountersPtr>
        m_linkedNotebookSyncChunksDataCounters;
};

AccountSynchronizer::AccountSynchronizer(
    Account account, IDownloaderPtr downloader, ISenderPtr sender,
    IAuthenticationInfoProviderPtr authenticationInfoProvider,
    ISyncStateStoragePtr syncStateStorage,
    ISyncChunksStoragePtr syncChunksStorage) :
    m_account{std::move(account)}, m_downloader{std::move(downloader)},
    // clang-format off
    m_sender{std::move(sender)},
    m_authenticationInfoProvider{std::move(authenticationInfoProvider)},
    m_syncStateStorage{std::move(syncStateStorage)},
    m_syncChunksStorage{std::move(syncChunksStorage)}
// clang-format on
{
    if (Q_UNLIKELY(m_account.isEmpty())) {
        throw InvalidArgument{ErrorString{
            QStringLiteral("AccountSynchronizer ctor: account is empty")}};
    }

    if (Q_UNLIKELY(!m_downloader)) {
        throw InvalidArgument{ErrorString{
            QStringLiteral("AccountSynchronizer ctor: downloader is null")}};
    }

    if (Q_UNLIKELY(!m_sender)) {
        throw InvalidArgument{ErrorString{
            QStringLiteral("AccountSynchronizer ctor: sender is null")}};
    }

    if (Q_UNLIKELY(!m_authenticationInfoProvider)) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "AccountSynchronizer ctor: authentication info provider is null")}};
    }

    if (Q_UNLIKELY(!m_syncStateStorage)) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "AccountSynchronizer ctor: sync state storage is null")}};
    }

    if (Q_UNLIKELY(!m_syncChunksStorage)) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "AccountSynchronizer ctor: sync chunks storage is null")}};
    }
}

QFuture<ISyncResultPtr> AccountSynchronizer::synchronize(
    ICallbackWeakPtr callbackWeak, utility::cancelers::ICancelerPtr canceler)
{
    QNINFO(
        "synchronization::AccountSynchronizer",
        "Starting synchronization for account " << m_account.name() << " ("
                                                << m_account.id() << ")");

    Q_ASSERT(canceler);

    auto promise = std::make_shared<QPromise<ISyncResultPtr>>();
    auto future = promise->future();
    promise->start();

    auto context = std::make_shared<Context>();
    context->promise = std::move(promise);
    context->canceler = std::move(canceler);
    context->syncChunksMutex = std::make_shared<QMutex>();

    context->callbackWrapper = std::make_shared<CallbackWrapper>(
        std::move(callbackWeak), std::weak_ptr{context});

    synchronizeImpl(std::move(context));
    return future;
}

void AccountSynchronizer::synchronizeImpl(
    ContextPtr context, const SendAfterDownload sendAfterDownload)
{
    Q_ASSERT(context);

    QNDEBUG(
        "synchronization::AccountSynchronizer",
        "AccountSynchronizer::synchronizeImpl: send after download = "
            << sendAfterDownload);

    const auto selfWeak = weak_from_this();
    auto * currentThread = QThread::currentThread();

    auto downloadFuture =
        m_downloader->download(context->canceler, context->callbackWrapper);

    auto downloadThenFuture = threading::then(
        std::move(downloadFuture), currentThread,
        threading::TrackedTask{
            selfWeak,
            [this, sendAfterDownload, context = context](
                const IDownloader::Result & downloadResult) mutable {
                onDownloadFinished(
                    std::move(context), downloadResult, sendAfterDownload);
            }});

    threading::onFailed(
        std::move(downloadThenFuture), currentThread,
        [this, selfWeak,
         context = std::move(context)](const QException & e) mutable {
            // This exception should only be possible to come from sync
            // chunks downloading as notes and resources downloading
            // reports separate errors per each note/resource and does
            // so via the sync result, not via the exception inside
            // QFuture.
            // It is possible to recover the information about what part of sync
            // chunks was downloaded before the error through sync chunks data
            // counters stored in context->callbackWrapper.
            const auto self = selfWeak.lock();
            if (!self) {
                return;
            }

            onDownloadFailed(std::move(context), e);
        });
}

void AccountSynchronizer::onDownloadFinished(
    ContextPtr context, const IDownloader::Result & downloadResult,
    const SendAfterDownload sendAfterDownload)
{
    QNINFO(
        "synchronization::AccountSynchronizer",
        "Downloading finished for account " << m_account.name() << " ("
                                            << m_account.id() << ")");

    Q_ASSERT(context);

    if (processDownloadStopSynchronizationError(context, downloadResult)) {
        return;
    }

    appendToPreviousSyncResult(*context, downloadResult);
    updateStoredSyncState(downloadResult);

    if (sendAfterDownload == SendAfterDownload::Yes) {
        context->callbackWrapper->onDownloadFinished(
            somethingDownloaded(downloadResult));

        send(std::move(context));
        return;
    }

    finalize(*context);
}

void AccountSynchronizer::onDownloadFailed(
    ContextPtr context, const QException & e)
{
    Q_ASSERT(context);

    QNDEBUG(
        "synchronization::AccountSynchronizer",
        "AccountSynchronizer::onDownloadFailed: " << e.what());

    storeDownloadedSyncChunks(*context);

    try {
        e.raise();
    }
    catch (const qevercloud::EDAMSystemExceptionAuthExpired & ea) {
        QNINFO(
            "synchronization::AccountSynchronizer",
            "Detected authentication expiration during sync, "
            "trying to re-authenticate and restart sync");
        clearAuthenticationCachesAndRestartSync(std::move(context));
    }
    catch (const qevercloud::EDAMSystemExceptionRateLimitReached & er) {
        QNINFO(
            "synchronization::AccountSynchronizer",
            "Detected API rate limit exceeding, rate limit duration = "
                << (er.rateLimitDuration()
                        ? QString::number(*er.rateLimitDuration())
                        : QStringLiteral("<none>")));
        // Rate limit reaching means it's pointless to try to
        // continue the sync right now
        auto syncResult = context->previousSyncResult
            ? context->previousSyncResult
            : std::make_shared<SyncResult>();

        // Sync chunks counters for downloaded sync chunks should be available
        // in counters cached within callbackWrapper
        if (auto counters =
                context->callbackWrapper->userOwnSyncChunksDataCounters())
        {
            syncResult->m_userAccountSyncChunksDataCounters =
                std::move(counters);
        }

        const auto linkedNotebookSyncChunksDataCounters =
            context->callbackWrapper->linkedNotebookSyncChunksDataCounters();
        for (const auto it:
             qevercloud::toRange(linkedNotebookSyncChunksDataCounters))
        {
            const auto & linkedNotebookGuid = it.key();
            auto counters = it.value();
            Q_ASSERT(counters);

            syncResult
                ->m_linkedNotebookSyncChunksDataCounters[linkedNotebookGuid] =
                std::move(counters);
        }

        syncResult->m_stopSynchronizationError = StopSynchronizationError{
            RateLimitReachedError{er.rateLimitDuration()}};
        context->promise->addResult(std::move(syncResult));
        context->promise->finish();
    }
    catch (const QException & eq) {
        QNWARNING(
            "synchronization::AccountSynchronizer",
            "Caught exception on download attempt: " << e.what());
        context->promise->setException(e);
        context->promise->finish();
    }
    catch (const std::exception & es) {
        QNWARNING(
            "synchronization::AccountSynchronizer",
            "Caught unknown std::exception on download attempt: " << e.what());
        context->promise->setException(
            RuntimeError{ErrorString{QString::fromStdString(e.what())}});
        context->promise->finish();
    }
    catch (...) {
        QNWARNING(
            "synchronization::AccountSynchronizer",
            "Caught unknown on download attempt");
        context->promise->setException(
            RuntimeError{ErrorString{QStringLiteral("Unknown error")}});
        context->promise->finish();
    }
}

void AccountSynchronizer::updateStoredSyncState(
    const IDownloader::Result & downloadResult)
{
    if (!downloadResult.syncState) {
        QNDEBUG(
            "synchronization::AccountSynchronizer",
            "AccountSynchronizer::updateStoredSyncState (after download): "
                << "no sync state to store");
        return;
    }

    QNDEBUG(
        "synchronization::AccountSynchronizer",
        "AccountSynchronizer::updateStoredSyncState (after download): "
            << *downloadResult.syncState);

    m_syncStateStorage->setSyncState(m_account, downloadResult.syncState);
}

bool AccountSynchronizer::processDownloadStopSynchronizationError(
    const ContextPtr & context, const IDownloader::Result & downloadResult)
{
    Q_ASSERT(context);

    if ((downloadResult.userOwnResult.downloadNotesStatus &&
         std::holds_alternative<AuthenticationExpiredError>(
             downloadResult.userOwnResult.downloadNotesStatus
                 ->stopSynchronizationError())) ||
        (downloadResult.userOwnResult.downloadResourcesStatus &&
         std::holds_alternative<AuthenticationExpiredError>(
             downloadResult.userOwnResult.downloadResourcesStatus
                 ->stopSynchronizationError())))
    {
        QNINFO(
            "synchronization::AccountSynchronizer",
            "Detected authentication expiration when trying to download user "
                << "own data, trying to re-authenticate and restart; account "
                << m_account.name() << " (" << m_account.id() << ")");

        m_authenticationInfoProvider->clearCaches(
            IAuthenticationInfoProvider::ClearCacheOptions{
                IAuthenticationInfoProvider::ClearCacheOption::User{
                    m_account.id()}});

        appendToPreviousSyncResult(*context, downloadResult);
        storeDownloadedSyncChunks(*context);
        synchronizeImpl(context);
        return true;
    }

    for (const auto it: qevercloud::toRange(
             std::as_const(downloadResult.linkedNotebookResults)))
    {
        const auto & linkedNotebookGuid = it.key();
        const auto & result = it.value();

        if ((result.downloadNotesStatus &&
             std::holds_alternative<AuthenticationExpiredError>(
                 result.downloadNotesStatus->stopSynchronizationError())) ||
            (result.downloadResourcesStatus &&
             std::holds_alternative<AuthenticationExpiredError>(
                 result.downloadResourcesStatus->stopSynchronizationError())))
        {
            QNINFO(
                "synchronization::AccountSynchronizer",
                "Detected authentication expiration when trying to download "
                    << "linked notebook data, trying to re-authenticate and "
                    << "restart sync; account " << m_account.name() << " ("
                    << m_account.id() << ")");

            m_authenticationInfoProvider->clearCaches(
                IAuthenticationInfoProvider::ClearCacheOptions{
                    IAuthenticationInfoProvider::ClearCacheOption::
                        LinkedNotebook{linkedNotebookGuid}});

            appendToPreviousSyncResult(*context, downloadResult);
            storeDownloadedSyncChunks(*context);
            synchronizeImpl(context);
            return true;
        }
    }

    auto rateLimitReachedError =
        [&downloadResult]() -> std::optional<RateLimitReachedError> {
        if (downloadResult.userOwnResult.downloadNotesStatus &&
            std::holds_alternative<RateLimitReachedError>(
                downloadResult.userOwnResult.downloadNotesStatus
                    ->stopSynchronizationError()))
        {
            if (QuentierIsLogLevelActive(LogLevel::Info)) {
                const auto rateLimitReachedError =
                    std::get<RateLimitReachedError>(
                        downloadResult.userOwnResult.downloadNotesStatus
                            ->stopSynchronizationError());
                QNINFO(
                    "synchronization::AccountSynchronizer",
                    "Detected rate limit exceeding when trying to download "
                        << "user own notes; rate limit duration = "
                        << (rateLimitReachedError.rateLimitDurationSec
                                ? QString::number(*rateLimitReachedError
                                                       .rateLimitDurationSec)
                                : QStringLiteral("<none>"))
                        << " seconds");
            }

            return std::get<RateLimitReachedError>(
                downloadResult.userOwnResult.downloadNotesStatus
                    ->stopSynchronizationError());
        }

        if (downloadResult.userOwnResult.downloadResourcesStatus &&
            std::holds_alternative<RateLimitReachedError>(
                downloadResult.userOwnResult.downloadResourcesStatus
                    ->stopSynchronizationError()))
        {
            if (QuentierIsLogLevelActive(LogLevel::Info)) {
                const auto rateLimitReachedError =
                    std::get<RateLimitReachedError>(
                        downloadResult.userOwnResult.downloadResourcesStatus
                            ->stopSynchronizationError());
                QNINFO(
                    "synchronization::AccountSynchronizer",
                    "Detected rate limit exceeding when trying to download "
                        << "user own resources; rate limit duration = "
                        << (rateLimitReachedError.rateLimitDurationSec
                                ? QString::number(*rateLimitReachedError
                                                       .rateLimitDurationSec)
                                : QStringLiteral("<none>"))
                        << " seconds");
            }

            return std::get<RateLimitReachedError>(
                downloadResult.userOwnResult.downloadResourcesStatus
                    ->stopSynchronizationError());
        }

        for (const auto it:
             qevercloud::toRange(downloadResult.linkedNotebookResults))
        {
            const auto & result = it.value();

            if (result.downloadNotesStatus &&
                std::holds_alternative<RateLimitReachedError>(
                    result.downloadNotesStatus->stopSynchronizationError()))
            {
                if (QuentierIsLogLevelActive(LogLevel::Info)) {
                    const auto rateLimitReachedError =
                        std::get<RateLimitReachedError>(
                            result.downloadNotesStatus
                                ->stopSynchronizationError());
                    QNINFO(
                        "synchronization::AccountSynchronizer",
                        "Detected rate limit exceeding when trying to download "
                            << "linked notebook notes; rate limit duration = "
                            << (rateLimitReachedError.rateLimitDurationSec
                                    ? QString::number(
                                          *rateLimitReachedError
                                               .rateLimitDurationSec)
                                    : QStringLiteral("<none>"))
                            << " seconds; linked notebook guid = " << it.key());
                }

                return std::get<RateLimitReachedError>(
                    result.downloadNotesStatus->stopSynchronizationError());
            }

            if (result.downloadResourcesStatus &&
                std::holds_alternative<RateLimitReachedError>(
                    result.downloadResourcesStatus->stopSynchronizationError()))
            {
                if (QuentierIsLogLevelActive(LogLevel::Info)) {
                    const auto rateLimitReachedError =
                        std::get<RateLimitReachedError>(
                            result.downloadResourcesStatus
                                ->stopSynchronizationError());
                    QNINFO(
                        "synchronization::AccountSynchronizer",
                        "Detected rate limit exceeding when trying to download "
                            << "linked notebook resources; rate limit "
                            << "duration = "
                            << (rateLimitReachedError.rateLimitDurationSec
                                    ? QString::number(
                                          *rateLimitReachedError
                                               .rateLimitDurationSec)
                                    : QStringLiteral("<none>"))
                            << " seconds; linked notebook guid = " << it.key());
                }

                return std::get<RateLimitReachedError>(
                    result.downloadResourcesStatus->stopSynchronizationError());
            }
        }

        return std::nullopt;
    }();

    if (rateLimitReachedError) {
        appendToPreviousSyncResult(*context, downloadResult);
        storeDownloadedSyncChunks(*context);
        auto & syncResult = context->previousSyncResult;
        Q_ASSERT(syncResult);
        syncResult->m_stopSynchronizationError = *rateLimitReachedError;
        context->promise->addResult(std::move(syncResult));
        context->promise->finish();
        return true;
    }

    return false;
}

void AccountSynchronizer::appendToPreviousSyncResult(
    Context & context, const IDownloader::Result & downloadResult) const
{
    if (!context.previousSyncResult) {
        context.previousSyncResult = std::make_shared<SyncResult>();
    }

    if (downloadResult.userOwnResult.syncChunksDataCounters) {
        if (context.previousSyncResult->m_userAccountSyncChunksDataCounters) {
            merge(
                *downloadResult.userOwnResult.syncChunksDataCounters,
                *context.previousSyncResult
                     ->m_userAccountSyncChunksDataCounters);
        }
        else {
            context.previousSyncResult->m_userAccountSyncChunksDataCounters =
                downloadResult.userOwnResult.syncChunksDataCounters;
        }
    }

    context.previousSyncResult->m_userAccountSyncChunksDownloaded |=
        downloadResult.userOwnResult.syncChunksDownloaded;

    if (!context.previousSyncResult->m_userAccountDownloadNotesStatus) {
        context.previousSyncResult->m_userAccountDownloadNotesStatus =
            downloadResult.userOwnResult.downloadNotesStatus;
    }
    else if (downloadResult.userOwnResult.downloadNotesStatus) {
        merge(
            *downloadResult.userOwnResult.downloadNotesStatus,
            *context.previousSyncResult->m_userAccountDownloadNotesStatus);
    }

    if (!context.previousSyncResult->m_userAccountDownloadResourcesStatus) {
        context.previousSyncResult->m_userAccountDownloadResourcesStatus =
            downloadResult.userOwnResult.downloadResourcesStatus;
    }
    else if (downloadResult.userOwnResult.downloadResourcesStatus) {
        merge(
            *downloadResult.userOwnResult.downloadResourcesStatus,
            *context.previousSyncResult->m_userAccountDownloadResourcesStatus);
    }

    for (const auto it: qevercloud::toRange(
             std::as_const(downloadResult.linkedNotebookResults)))
    {
        const auto & linkedNotebookGuid = it.key();
        const auto & result = it.value();

        if (result.syncChunksDataCounters) {
            auto & counters = context.previousSyncResult
                                  ->m_linkedNotebookSyncChunksDataCounters
                                      [linkedNotebookGuid];
            if (counters) {
                merge(*result.syncChunksDataCounters, *counters);
            }
            else {
                counters = result.syncChunksDataCounters;
            }
        }

        if (result.syncChunksDownloaded) {
            context.previousSyncResult
                ->m_linkedNotebookGuidsWithSyncChunksDownloaded.insert(
                    linkedNotebookGuid);
        }

        if (result.downloadNotesStatus) {
            const auto nit = context.previousSyncResult
                                 ->m_linkedNotebookDownloadNotesStatuses.find(
                                     linkedNotebookGuid);
            if (nit !=
                context.previousSyncResult
                    ->m_linkedNotebookDownloadNotesStatuses.end())
            {
                Q_ASSERT(nit.value());
                merge(*result.downloadNotesStatus, *nit.value());
            }
            else {
                context.previousSyncResult
                    ->m_linkedNotebookDownloadNotesStatuses
                        [linkedNotebookGuid] = result.downloadNotesStatus;
            }
        }

        if (result.downloadResourcesStatus) {
            const auto rit =
                context.previousSyncResult
                    ->m_linkedNotebookDownloadResourcesStatuses.find(
                        linkedNotebookGuid);
            if (rit !=
                context.previousSyncResult
                    ->m_linkedNotebookDownloadResourcesStatuses.end())
            {
                Q_ASSERT(rit.value());
                merge(*result.downloadResourcesStatus, *rit.value());
            }
            else {
                context.previousSyncResult
                    ->m_linkedNotebookDownloadResourcesStatuses
                        [linkedNotebookGuid] = result.downloadResourcesStatus;
            }
        }
    }

    if (!context.previousSyncResult->m_syncState) {
        context.previousSyncResult->m_syncState = downloadResult.syncState;
    }
    else if (downloadResult.syncState) {
        merge(
            *downloadResult.syncState,
            *context.previousSyncResult->m_syncState);
    }
}

void AccountSynchronizer::appendToPreviousSyncResult(
    Context & context, const ISender::Result & sendResult) const
{
    if (!context.previousSyncResult) {
        context.previousSyncResult = std::make_shared<SyncResult>();
    }

    if (sendResult.syncState) {
        context.previousSyncResult->m_syncState = sendResult.syncState;
    }

    if (!context.previousSyncResult->m_syncState) {
        context.previousSyncResult->m_syncState = sendResult.syncState;
    }
    else if (sendResult.syncState) {
        merge(*sendResult.syncState, *context.previousSyncResult->m_syncState);
    }

    if (sendResult.userOwnResult) {
        if (!context.previousSyncResult->m_userAccountSendStatus) {
            context.previousSyncResult->m_userAccountSendStatus =
                sendResult.userOwnResult;
        }
        else {
            merge(
                *sendResult.userOwnResult,
                *context.previousSyncResult->m_userAccountSendStatus);
        }
    }

    for (const auto it:
         qevercloud::toRange(std::as_const(sendResult.linkedNotebookResults)))
    {
        const auto & linkedNotebookGuid = it.key();
        const auto & result = it.value();

        Q_ASSERT(result);

        const auto nit =
            context.previousSyncResult->m_linkedNotebookSendStatuses.find(
                linkedNotebookGuid);
        if (nit !=
            context.previousSyncResult->m_linkedNotebookSendStatuses.end())
        {
            Q_ASSERT(nit.value());
            merge(*result, *nit.value());
        }
        else {
            context.previousSyncResult
                ->m_linkedNotebookSendStatuses[linkedNotebookGuid] = result;
        }
    }
}

void AccountSynchronizer::send(ContextPtr context)
{
    QNINFO(
        "synchronization::AccountSynchronizer",
        "Sending data to Evernote for account " << m_account.name() << " ("
                                                << m_account.id() << ")");

    Q_ASSERT(context);

    const auto selfWeak = weak_from_this();
    auto * currentThread = QThread::currentThread();

    auto sendFuture =
        m_sender->send(context->canceler, context->callbackWrapper);

    auto sendThenFuture = threading::then(
        std::move(sendFuture), currentThread,
        threading::TrackedTask{
            selfWeak,
            [this,
             context = context](const ISender::Result & sendResult) mutable {
                onSendFinished(std::move(context), sendResult);
            }});

    threading::onFailed(
        std::move(sendThenFuture), currentThread,
        [context = std::move(context)](const QException & e) mutable {
            // If sending fails, it fails for some really good reason so there's
            // not much that can be done, will just forward this error to the
            // overall result
            context->promise->setException(e);
            context->promise->finish();
        });
}

void AccountSynchronizer::onSendFinished(
    ContextPtr context, const ISender::Result & sendResult) // NOLINT
{
    Q_ASSERT(context);

    QNDEBUG(
        "synchronization::AccountSynchronizer",
        "AccountSynchronizer::onSendFinished: " << sendResult);

    if (processSendStopSynchronizationError(context, sendResult)) {
        return;
    }

    appendToPreviousSyncResult(*context, sendResult);
    updateStoredSyncState(sendResult);

    Q_ASSERT(context->previousSyncResult);

    const bool needToRepeatIncrementalSync = [&context, this]() -> bool {
        if (context->previousSyncResult->m_userAccountSendStatus &&
            context->previousSyncResult->m_userAccountSendStatus
                ->m_needToRepeatIncrementalSync)
        {
            QNINFO(
                "synchronization::AccountSynchronizer",
                "Detected the need to repeat incremental sync after sending "
                    << "user own data for account " << m_account.name() << " ("
                    << m_account.id() << ")");
            return true;
        }

        for (const auto it: // NOLINT
             qevercloud::toRange(std::as_const(
                 context->previousSyncResult->m_linkedNotebookSendStatuses)))
        {
            const auto & result = it.value();
            Q_ASSERT(result);

            if (result->m_needToRepeatIncrementalSync) {
                QNINFO(
                    "synchronization::AccountSynchronizer",
                    "Detected the need to repeat incremental sync after "
                        << "sending linked notebook data for account "
                        << m_account.name() << " (" << m_account.id() << ")"
                        << ", linked notebook guid = " << it.key());
                return true;
            }
        }

        return false;
    }();

    if (needToRepeatIncrementalSync) {
        synchronizeImpl(std::move(context), SendAfterDownload::No);
        return;
    }

    finalize(*context);
}

void AccountSynchronizer::updateStoredSyncState(
    const ISender::Result & sendResult)
{
    if (!sendResult.syncState) {
        QNDEBUG(
            "synchronization::AccountSynchronizer",
            "AccountSynchronizer::updateStoredSyncState (after send): "
                << "no sync state to store");
        return;
    }

    QNDEBUG(
        "synchronization::AccountSynchronizer",
        "AccountSynchronizer::updateStoredSyncState (after send): "
            << *sendResult.syncState);

    m_syncStateStorage->setSyncState(m_account, sendResult.syncState);
}

bool AccountSynchronizer::processSendStopSynchronizationError(
    const ContextPtr & context, const ISender::Result & sendResult)
{
    Q_ASSERT(context);

    if (sendResult.userOwnResult &&
        std::holds_alternative<AuthenticationExpiredError>(
            sendResult.userOwnResult->stopSynchronizationError()))
    {
        m_authenticationInfoProvider->clearCaches(
            IAuthenticationInfoProvider::ClearCacheOptions{
                IAuthenticationInfoProvider::ClearCacheOption::User{
                    m_account.id()}});

        appendToPreviousSyncResult(*context, sendResult);
        synchronizeImpl(context);
        return true;
    }

    for (const auto it: // NOLINT
         qevercloud::toRange(std::as_const(sendResult.linkedNotebookResults)))
    {
        const auto & linkedNotebookGuid = it.key();
        const auto & result = it.value();

        Q_ASSERT(result);
        if (std::holds_alternative<AuthenticationExpiredError>(
                result->stopSynchronizationError()))
        {
            m_authenticationInfoProvider->clearCaches(
                IAuthenticationInfoProvider::ClearCacheOptions{
                    IAuthenticationInfoProvider::ClearCacheOption::
                        LinkedNotebook{linkedNotebookGuid}});

            appendToPreviousSyncResult(*context, sendResult);
            synchronizeImpl(context);
            return true;
        }
    }

    auto rateLimitReachedError =
        [&sendResult]() -> std::optional<RateLimitReachedError> {
        if (sendResult.userOwnResult &&
            std::holds_alternative<RateLimitReachedError>(
                sendResult.userOwnResult->stopSynchronizationError()))
        {
            return std::get<RateLimitReachedError>(
                sendResult.userOwnResult->stopSynchronizationError());
        }

        for (const auto it:
             qevercloud::toRange(sendResult.linkedNotebookResults))
        {
            const auto & result = it.value();

            Q_ASSERT(result);
            if (std::holds_alternative<RateLimitReachedError>(
                    result->stopSynchronizationError()))
            {
                return std::get<RateLimitReachedError>(
                    result->stopSynchronizationError());
            }
        }

        return std::nullopt;
    }();

    if (rateLimitReachedError) {
        appendToPreviousSyncResult(*context, sendResult);
        updateStoredSyncState(sendResult);
        auto & syncResult = context->previousSyncResult;
        Q_ASSERT(syncResult);
        syncResult->m_stopSynchronizationError = *rateLimitReachedError;
        context->promise->addResult(std::move(syncResult));
        context->promise->finish();
        return true;
    }

    return false;
}

void AccountSynchronizer::storeDownloadedSyncChunks(Context & context)
{
    QNDEBUG(
        "synchronization::AccountSynchronizer",
        "AccountSynchronizer::storeDownloadedSyncChunks");

    QList<qevercloud::SyncChunk> userOwnSyncChunks;
    QHash<qevercloud::Guid, QList<qevercloud::SyncChunk>>
        linkedNotebookSyncChunks;

    {
        Q_ASSERT(context.syncChunksMutex);
        const QMutexLocker locker{context.syncChunksMutex.get()};

        userOwnSyncChunks = std::move(context.downloadedUserOwnSyncChunks);
        context.downloadedUserOwnSyncChunks = {};

        linkedNotebookSyncChunks = context.downloadedLinkedNotebookSyncChunks;
        context.downloadedLinkedNotebookSyncChunks = {};
    }

    bool storedSomething = false;
    if (!userOwnSyncChunks.isEmpty()) {
        m_syncChunksStorage->putUserOwnSyncChunks(std::move(userOwnSyncChunks));
        storedSomething = true;
    }

    for (auto it: qevercloud::toRange(linkedNotebookSyncChunks)) {
        m_syncChunksStorage->putLinkedNotebookSyncChunks(
            it.key(), std::move(it.value()));
        storedSomething = true;
    }

    if (storedSomething) {
        m_syncChunksStorage->flush();
    }
}

void AccountSynchronizer::clearAuthenticationCachesAndRestartSync(
    ContextPtr context)
{
    Q_ASSERT(context);

    m_authenticationInfoProvider->clearCaches(
        IAuthenticationInfoProvider::ClearCacheOptions{
            IAuthenticationInfoProvider::ClearCacheOption::All{}});

    synchronizeImpl(std::move(context));
}

void AccountSynchronizer::finalize(Context & context)
{
    Q_ASSERT(context.previousSyncResult);

    QNINFO(
        "synchronization::AccountSynchronizer",
        "Synchronization finished for account " << m_account.name() << " ("
                                                << m_account.id() << ")");

    context.promise->addResult(std::move(context.previousSyncResult));
    context.promise->finish();
}

QDebug & operator<<(
    QDebug & dbg,
    const AccountSynchronizer::SendAfterDownload sendAfterDownload)
{
    switch (sendAfterDownload) {
    case AccountSynchronizer::SendAfterDownload::Yes:
        dbg << "Yes";
        break;
    case AccountSynchronizer::SendAfterDownload::No:
        dbg << "No";
        break;
    }

    return dbg;
}

} // namespace quentier::synchronization
