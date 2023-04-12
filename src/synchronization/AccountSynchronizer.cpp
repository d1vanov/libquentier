/*
 * Copyright 2023 Dmitry Ivanov
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
#include <synchronization/SyncChunksDataCounters.h>
#include <synchronization/types/DownloadNotesStatus.h>
#include <synchronization/types/DownloadResourcesStatus.h>
#include <synchronization/types/SendStatus.h>
#include <synchronization/types/SyncResult.h>

#include <qevercloud/exceptions/EDAMSystemExceptionAuthExpired.h>
#include <qevercloud/exceptions/EDAMSystemExceptionRateLimitReached.h>
#include <qevercloud/utility/ToRange.h>

#include <QMutex>
#include <QMutexLocker>

#include <algorithm>
#include <exception>

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

    for (const auto it: qevercloud::toRange(from.processedNoteGuidsAndUsns())) {
        to.m_processedNoteGuidsAndUsns[it.key()] = it.value();
    }

    for (const auto it: qevercloud::toRange(from.cancelledNoteGuidsAndUsns())) {
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

    for (const auto it:
         qevercloud::toRange(from.processedResourceGuidsAndUsns())) {
        to.m_processedResourceGuidsAndUsns[it.key()] = it.value();
    }

    for (const auto it:
         qevercloud::toRange(from.cancelledResourceGuidsAndUsns())) {
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

} // namespace

class AccountSynchronizer::CallbackWrapper :
    public IAccountSynchronizer::ICallback
{
public:
    explicit CallbackWrapper(
        IAccountSynchronizer::ICallbackWeakPtr callbackWeak) :
        m_callbackWeak{std::move(callbackWeak)}
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
        qint32 highestDownloadedUsn, qint32 highestServerUsn,
        qint32 lastPreviousUsn) override
    {
        if (const auto callback = m_callbackWeak.lock()) {
            callback->onSyncChunksDownloadProgress(
                highestDownloadedUsn, highestServerUsn, lastPreviousUsn);
        }
    }

    void onSyncChunksDownloaded() override
    {
        if (const auto callback = m_callbackWeak.lock()) {
            callback->onSyncChunksDownloaded();
        }
    }

    void onSyncChunksDataProcessingProgress(
        SyncChunksDataCountersPtr counters) override
    {
        Q_ASSERT(counters);

        if (const auto callback = m_callbackWeak.lock()) {
            callback->onSyncChunksDataProcessingProgress(counters);
        }

        const QMutexLocker locker{&m_mutex};
        m_userOwnSyncChunksDataCounters = std::move(counters);
    }

    void onStartLinkedNotebooksDataDownloading(
        const QList<qevercloud::LinkedNotebook> & linkedNotebooks) override
    {
        if (const auto callback = m_callbackWeak.lock()) {
            callback->onStartLinkedNotebooksDataDownloading(linkedNotebooks);
        }
    }

    void onLinkedNotebookSyncChunksDownloadProgress(
        qint32 highestDownloadedUsn, qint32 highestServerUsn,
        qint32 lastPreviousUsn,
        const qevercloud::LinkedNotebook & linkedNotebook) override
    {
        if (const auto callback = m_callbackWeak.lock()) {
            callback->onLinkedNotebookSyncChunksDownloadProgress(
                highestDownloadedUsn, highestServerUsn, lastPreviousUsn,
                linkedNotebook);
        }
    }

    void onLinkedNotebookSyncChunksDownloaded(
        const qevercloud::LinkedNotebook & linkedNotebook) override
    {
        if (const auto callback = m_callbackWeak.lock()) {
            callback->onLinkedNotebookSyncChunksDownloaded(linkedNotebook);
        }
    }

    void onLinkedNotebookSyncChunksDataProcessingProgress(
        SyncChunksDataCountersPtr counters,
        const qevercloud::LinkedNotebook & linkedNotebook) override
    {
        if (const auto callback = m_callbackWeak.lock()) {
            callback->onLinkedNotebookSyncChunksDataProcessingProgress(
                counters, linkedNotebook);
        }

        Q_ASSERT(linkedNotebook.guid());

        const QMutexLocker locker{&m_mutex};
        m_linkedNotebookSyncChunksDataCounters[*linkedNotebook.guid()] =
            std::move(counters);
    }

    void onNotesDownloadProgress(
        quint32 notesDownloaded, quint32 totalNotesToDownload) override
    {
        if (const auto callback = m_callbackWeak.lock()) {
            callback->onNotesDownloadProgress(
                notesDownloaded, totalNotesToDownload);
        }
    }

    void onLinkedNotebookNotesDownloadProgress(
        quint32 notesDownloaded, quint32 totalNotesToDownload,
        const qevercloud::LinkedNotebook & linkedNotebook) override
    {
        if (const auto callback = m_callbackWeak.lock()) {
            callback->onLinkedNotebookNotesDownloadProgress(
                notesDownloaded, totalNotesToDownload, linkedNotebook);
        }
    }

    void onResourcesDownloadProgress(
        quint32 resourcesDownloaded, quint32 totalResourcesToDownload) override
    {
        if (const auto callback = m_callbackWeak.lock()) {
            callback->onResourcesDownloadProgress(
                resourcesDownloaded, totalResourcesToDownload);
        }
    }

    void onLinkedNotebookResourcesDownloadProgress(
        quint32 resourcesDownloaded, quint32 totalResourcesToDownload,
        const qevercloud::LinkedNotebook & linkedNotebook) override
    {
        if (const auto callback = m_callbackWeak.lock()) {
            callback->onLinkedNotebookResourcesDownloadProgress(
                resourcesDownloaded, totalResourcesToDownload, linkedNotebook);
        }
    }

public: // ISender::ICallback
    void onUserOwnSendStatusUpdate(SendStatusPtr sendStatus) override
    {
        if (const auto callback = m_callbackWeak.lock()) {
            callback->onUserOwnSendStatusUpdate(std::move(sendStatus));
        }
    }

    void onLinkedNotebookSendStatusUpdate(
        const qevercloud::Guid & linkedNotebookGuid,
        SendStatusPtr sendStatus) override
    {
        if (const auto callback = m_callbackWeak.lock()) {
            callback->onLinkedNotebookSendStatusUpdate(
                linkedNotebookGuid, std::move(sendStatus));
        }
    }

private:
    const IAccountSynchronizer::ICallbackWeakPtr m_callbackWeak;

    mutable QMutex m_mutex;
    SyncChunksDataCountersPtr m_userOwnSyncChunksDataCounters;
    QHash<qevercloud::Guid, SyncChunksDataCountersPtr>
        m_linkedNotebookSyncChunksDataCounters;
};

AccountSynchronizer::AccountSynchronizer(
    Account account, IDownloaderPtr downloader, ISenderPtr sender,
    IAuthenticationInfoProviderPtr authenticationInfoProvider,
    ISyncStateStoragePtr syncStateStorage,
    threading::QThreadPoolPtr threadPool) :
    m_account{std::move(account)},
    m_downloader{std::move(downloader)},
    // clang-format off
    m_sender{std::move(sender)},
    m_authenticationInfoProvider{std::move(authenticationInfoProvider)},
    m_syncStateStorage{std::move(syncStateStorage)},
    // clang-format on
    m_threadPool{
        threadPool ? std::move(threadPool) : threading::globalThreadPool()}
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

    Q_ASSERT(m_threadPool);
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
    context->callbackWrapper =
        std::make_shared<CallbackWrapper>(std::move(callbackWeak));
    context->canceler = std::move(canceler);

    synchronizeImpl(std::move(context));
    return future;
}

void AccountSynchronizer::synchronizeImpl(ContextPtr context)
{
    Q_ASSERT(context);

    const auto selfWeak = weak_from_this();

    auto downloadFuture =
        m_downloader->download(context->canceler, context->callbackWrapper);

    auto downloadThenFuture = threading::then(
        std::move(downloadFuture), m_threadPool.get(),
        threading::TrackedTask{
            selfWeak,
            [this, context = context](
                const IDownloader::Result & downloadResult) mutable {
                onDownloadFinished(std::move(context), downloadResult);
            }});

    threading::onFailed(
        std::move(downloadThenFuture),
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
    ContextPtr context, const IDownloader::Result & downloadResult)
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
    updateStoredSyncState(*context, downloadResult);

    send(std::move(context));
}

void AccountSynchronizer::onDownloadFailed(
    ContextPtr context, const QException & e)
{
    Q_ASSERT(context);

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

        for (const auto it:
             qevercloud::toRange(context->callbackWrapper
                                     ->linkedNotebookSyncChunksDataCounters()))
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
    const Context & context, const IDownloader::Result & downloadResult)
{
    // TODO: implement
    Q_UNUSED(context)
    Q_UNUSED(downloadResult)
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
        synchronizeImpl(context);
        return true;
    }

    for (const auto it:
         qevercloud::toRange(qAsConst(downloadResult.linkedNotebookResults)))
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
                downloadResult.userOwnResult.downloadNotesStatus
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
             qevercloud::toRange(downloadResult.linkedNotebookResults)) {
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
        auto syncResult = context->previousSyncResult
            ? context->previousSyncResult
            : std::make_shared<SyncResult>();
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
        context.previousSyncResult->m_userAccountSyncChunksDataCounters =
            downloadResult.userOwnResult.syncChunksDataCounters;
    }

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

    for (const auto it:
         qevercloud::toRange(qAsConst(downloadResult.linkedNotebookResults)))
    {
        const auto & linkedNotebookGuid = it.key();
        const auto & result = it.value();

        if (result.syncChunksDataCounters) {
            context.previousSyncResult
                ->m_linkedNotebookSyncChunksDataCounters[linkedNotebookGuid] =
                result.syncChunksDataCounters;
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
}

void AccountSynchronizer::appendToPreviousSyncResult(
    Context & context, const ISender::Result & sendResult) const
{
    if (!context.previousSyncResult) {
        context.previousSyncResult = std::make_shared<SyncResult>();
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
         qevercloud::toRange(qAsConst(sendResult.linkedNotebookResults)))
    {
        const auto & linkedNotebookGuid = it.key();
        const auto & result = it.value();

        Q_ASSERT(result);

        const auto nit =
            context.previousSyncResult->m_linkedNotebookSendStatuses.find(
                linkedNotebookGuid);
        if (nit !=
            context.previousSyncResult->m_linkedNotebookSendStatuses.end()) {
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

    auto sendFuture =
        m_sender->send(context->canceler, context->callbackWrapper);

    auto sendThenFuture = threading::then(
        std::move(sendFuture), m_threadPool.get(),
        threading::TrackedTask{
            selfWeak,
            [this,
             context = context](const ISender::Result & sendResult) mutable {
                onSendFinished(std::move(context), sendResult);
            }});

    threading::onFailed(
        std::move(sendThenFuture),
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

    if (processSendStopSynchronizationError(context, sendResult)) {
        return;
    }

    appendToPreviousSyncResult(*context, sendResult);

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
             qevercloud::toRange(qAsConst(
                 context->previousSyncResult->m_linkedNotebookSendStatuses)))
        {
            const auto & result = it.value();
            Q_ASSERT(result);

            if (result->m_needToRepeatIncrementalSync) {
                QNINFO(
                    "synchronization::AccountSynchronizer",
                    "Detected the need to repeat incremental sync after "
                    "sending "
                        << "linked notebook data for account "
                        << m_account.name() << " (" << m_account.id() << ")"
                        << ", linked notebook guid = " << it.key());
                return true;
            }
        }

        return false;
    }();

    if (needToRepeatIncrementalSync) {
        synchronizeImpl(std::move(context));
        return;
    }

    QNINFO(
        "synchronization::AccountSynchronizer",
        "Synchronization finished for account " << m_account.name() << " ("
                                                << m_account.id() << ")");

    context->promise->addResult(std::move(context->previousSyncResult));
    context->promise->finish();
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
         qevercloud::toRange(qAsConst(sendResult.linkedNotebookResults)))
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
             qevercloud::toRange(sendResult.linkedNotebookResults)) {
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
        auto syncResult = context->previousSyncResult
            ? context->previousSyncResult
            : std::make_shared<SyncResult>();
        syncResult->m_stopSynchronizationError = *rateLimitReachedError;
        context->promise->addResult(std::move(syncResult));
        context->promise->finish();
        return true;
    }

    return false;
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

} // namespace quentier::synchronization
