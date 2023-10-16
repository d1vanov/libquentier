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

#include "Downloader.h"
#include "Utils.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/OperationCanceled.h>
#include <quentier/local_storage/ILocalStorage.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/synchronization/ISyncStateStorage.h>
#include <quentier/threading/Future.h>
#include <quentier/threading/TrackedTask.h>
#include <quentier/utility/cancelers/ICanceler.h>

#include <synchronization/IAuthenticationInfoProvider.h>
#include <synchronization/IFullSyncStaleDataExpunger.h>
#include <synchronization/INoteStoreProvider.h>
#include <synchronization/IProtocolVersionChecker.h>
#include <synchronization/SyncChunksDataCounters.h>
#include <synchronization/processors/IDurableNotesProcessor.h>
#include <synchronization/processors/IDurableResourcesProcessor.h>
#include <synchronization/processors/ILinkedNotebooksProcessor.h>
#include <synchronization/processors/INotebooksProcessor.h>
#include <synchronization/processors/ISavedSearchesProcessor.h>
#include <synchronization/processors/ITagsProcessor.h>
#include <synchronization/sync_chunks/ISyncChunksProvider.h>
#include <synchronization/types/DownloadNotesStatus.h>
#include <synchronization/types/DownloadResourcesStatus.h>

#include <qevercloud/RequestContextBuilder.h>
#include <qevercloud/services/INoteStore.h>

#include <QMutex>
#include <QMutexLocker>

#include <atomic>
#include <limits>

namespace quentier::synchronization {

namespace {

////////////////////////////////////////////////////////////////////////////////

class SyncChunksProviderCallback final : public ISyncChunksProvider::ICallback
{
public:
    explicit SyncChunksProviderCallback(
        IDownloader::ICallbackWeakPtr callbackWeak) :
        m_callbackWeak{std::move(callbackWeak)}
    {}

    // ISyncChunksProvider::ICallback
    void onUserOwnSyncChunksDownloadProgress(
        qint32 highestDownloadedUsn, qint32 highestServerUsn,
        qint32 lastPreviousUsn) override
    {
        if (const auto callback = m_callbackWeak.lock()) {
            callback->onSyncChunksDownloadProgress(
                highestDownloadedUsn, highestServerUsn, lastPreviousUsn);
        }
    }

    void onLinkedNotebookSyncChunksDownloadProgress(
        qint32 highestDownloadedUsn, qint32 highestServerUsn,
        qint32 lastPreviousUsn,
        qevercloud::LinkedNotebook linkedNotebook) override
    {
        if (const auto callback = m_callbackWeak.lock()) {
            callback->onLinkedNotebookSyncChunksDownloadProgress(
                highestDownloadedUsn, highestServerUsn, lastPreviousUsn,
                linkedNotebook);
        }
    }

private:
    const IDownloader::ICallbackWeakPtr m_callbackWeak;
};

////////////////////////////////////////////////////////////////////////////////

[[nodiscard]] IFullSyncStaleDataExpunger::PreservedGuids collectPreservedGuids(
    const QList<qevercloud::SyncChunk> & syncChunks)
{
    IFullSyncStaleDataExpunger::PreservedGuids preservedGuids;

    for (const auto & syncChunk: qAsConst(syncChunks)) {
        const auto & notebooks = syncChunk.notebooks();
        if (notebooks) {
            for (const auto & notebook: qAsConst(*notebooks)) {
                if (notebook.guid()) {
                    preservedGuids.notebookGuids.insert(*notebook.guid());
                }
            }
        }

        const auto & tags = syncChunk.tags();
        if (tags) {
            for (const auto & tag: qAsConst(*tags)) {
                if (tag.guid()) {
                    preservedGuids.tagGuids.insert(*tag.guid());
                }
            }
        }

        const auto & notes = syncChunk.notes();
        if (notes) {
            for (const auto & note: qAsConst(*notes)) {
                if (note.guid()) {
                    preservedGuids.noteGuids.insert(*note.guid());
                }
            }
        }

        const auto & savedSearches = syncChunk.searches();
        if (savedSearches) {
            for (const auto & savedSearch: qAsConst(*savedSearches)) {
                if (savedSearch.guid()) {
                    preservedGuids.savedSearchGuids.insert(*savedSearch.guid());
                }
            }
        }
    }

    return preservedGuids;
}

[[nodiscard]] quint64 countNotesInSyncChunks(
    const QList<qevercloud::SyncChunk> & syncChunks) noexcept
{
    quint64 result = 0UL;
    for (const auto & syncChunk: qAsConst(syncChunks)) {
        if (!syncChunk.notes()) {
            continue;
        }

        result += static_cast<quint64>(std::max(syncChunk.notes()->size(), 0));
    }
    return result;
}

[[nodiscard]] quint64 countResourcesInSyncChunks(
    const QList<qevercloud::SyncChunk> & syncChunks) noexcept
{
    quint64 result = 0UL;
    for (const auto & syncChunk: qAsConst(syncChunks)) {
        if (!syncChunk.resources()) {
            continue;
        }

        result +=
            static_cast<quint64>(std::max(syncChunk.resources()->size(), 0));
    }
    return result;
}

} // namespace

////////////////////////////////////////////////////////////////////////////////

class Downloader::LinkedNotebooksProcessorCallback final :
    public ILinkedNotebooksProcessor::ICallback
{
public:
    LinkedNotebooksProcessorCallback(
        SyncChunksDataCountersPtr syncChunksDataCounters,
        IDownloader::ICallbackWeakPtr callbackWeak,
        std::shared_ptr<QMutex> mutex) :
        m_syncChunksDataCounters{std::move(syncChunksDataCounters)},
        m_callbackWeak{std::move(callbackWeak)}, m_mutex{std::move(mutex)}
    {
        Q_ASSERT(m_syncChunksDataCounters);
        Q_ASSERT(!m_callbackWeak.expired());
        Q_ASSERT(m_mutex);
    }

    // ILinkedNotebooksProcessor::ICallback
    void onLinkedNotebooksProcessingProgress(
        qint32 totalLinkedNotebooks,          // NOLINT
        qint32 totalLinkedNotebooksToExpunge, // NOLINT
        qint32 processedLinkedNotebooks,      // NOLINT
        qint32 expungedLinkedNotebooks) override
    {
        const auto callback = m_callbackWeak.lock();
        if (!callback) {
            return;
        }

        Q_ASSERT(totalLinkedNotebooks >= 0);
        Q_ASSERT(totalLinkedNotebooksToExpunge >= 0);
        Q_ASSERT(processedLinkedNotebooks >= 0);
        Q_ASSERT(expungedLinkedNotebooks >= 0);

        const QMutexLocker lock{m_mutex.get()};

        Q_ASSERT(
            m_syncChunksDataCounters->m_totalLinkedNotebooks == 0 ||
            m_syncChunksDataCounters->m_totalLinkedNotebooks ==
                static_cast<quint64>(totalLinkedNotebooks));

        Q_ASSERT(
            m_syncChunksDataCounters->m_totalExpungedLinkedNotebooks == 0 ||
            m_syncChunksDataCounters->m_totalExpungedLinkedNotebooks ==
                static_cast<quint64>(totalLinkedNotebooksToExpunge));

        m_syncChunksDataCounters->m_totalLinkedNotebooks =
            static_cast<quint64>(totalLinkedNotebooks);

        m_syncChunksDataCounters->m_totalExpungedLinkedNotebooks =
            static_cast<quint64>(totalLinkedNotebooksToExpunge);

        m_syncChunksDataCounters->m_addedLinkedNotebooks =
            static_cast<quint64>(processedLinkedNotebooks);

        m_syncChunksDataCounters->m_expungedLinkedNotebooks =
            static_cast<quint64>(expungedLinkedNotebooks);

        callback->onSyncChunksDataProcessingProgress(
            std::make_shared<SyncChunksDataCounters>(
                *m_syncChunksDataCounters));
    }

private:
    const SyncChunksDataCountersPtr m_syncChunksDataCounters;
    const IDownloader::ICallbackWeakPtr m_callbackWeak;
    const std::shared_ptr<QMutex> m_mutex;
};

////////////////////////////////////////////////////////////////////////////////

class Downloader::NotebooksProcessorCallback final :
    public INotebooksProcessor::ICallback
{
public:
    NotebooksProcessorCallback(
        SyncChunksDataCountersPtr syncChunksDataCounters,
        IDownloader::ICallbackWeakPtr callbackWeak,
        std::shared_ptr<QMutex> mutex,
        std::optional<qevercloud::LinkedNotebook> linkedNotebook) :
        m_syncChunksDataCounters{std::move(syncChunksDataCounters)},
        m_callbackWeak{std::move(callbackWeak)}, m_mutex{std::move(mutex)},
        m_linkedNotebook{std::move(linkedNotebook)}
    {
        Q_ASSERT(m_syncChunksDataCounters);
        Q_ASSERT(!m_callbackWeak.expired());
        Q_ASSERT(m_mutex);
    }

    // INotebooksProcessor::ICallback
    void onNotebooksProcessingProgress(
        qint32 totalNotebooks, qint32 totalNotebooksToExpunge, // NOLINT
        qint32 addedNotebooks, qint32 updatedNotebooks,        // NOLINT
        qint32 expungedNotebooks) override
    {
        const auto callback = m_callbackWeak.lock();
        if (!callback) {
            return;
        }

        Q_ASSERT(totalNotebooks >= 0);
        Q_ASSERT(totalNotebooksToExpunge >= 0);
        Q_ASSERT(addedNotebooks >= 0);
        Q_ASSERT(updatedNotebooks >= 0);
        Q_ASSERT(expungedNotebooks >= 0);

        const QMutexLocker lock{m_mutex.get()};

        Q_ASSERT(
            m_syncChunksDataCounters->m_totalNotebooks == 0 ||
            m_syncChunksDataCounters->m_totalNotebooks ==
                static_cast<quint64>(totalNotebooks));

        Q_ASSERT(
            m_syncChunksDataCounters->m_totalExpungedNotebooks == 0 ||
            m_syncChunksDataCounters->m_totalExpungedNotebooks ==
                static_cast<quint64>(totalNotebooksToExpunge));

        m_syncChunksDataCounters->m_totalNotebooks =
            static_cast<quint64>(totalNotebooks);

        m_syncChunksDataCounters->m_totalExpungedNotebooks =
            static_cast<quint64>(totalNotebooksToExpunge);

        m_syncChunksDataCounters->m_addedNotebooks =
            static_cast<quint64>(addedNotebooks);

        m_syncChunksDataCounters->m_updatedNotebooks =
            static_cast<quint64>(updatedNotebooks);

        m_syncChunksDataCounters->m_expungedNotebooks =
            static_cast<quint64>(expungedNotebooks);

        if (m_linkedNotebook) {
            callback->onLinkedNotebookSyncChunksDataProcessingProgress(
                std::make_shared<SyncChunksDataCounters>(
                    *m_syncChunksDataCounters),
                *m_linkedNotebook);
        }
        else {
            callback->onSyncChunksDataProcessingProgress(
                std::make_shared<SyncChunksDataCounters>(
                    *m_syncChunksDataCounters));
        }
    }

private:
    const SyncChunksDataCountersPtr m_syncChunksDataCounters;
    const IDownloader::ICallbackWeakPtr m_callbackWeak;
    const std::shared_ptr<QMutex> m_mutex;
    const std::optional<qevercloud::LinkedNotebook> m_linkedNotebook;
};

////////////////////////////////////////////////////////////////////////////////

class Downloader::NotesProcessorCallback final :
    public IDurableNotesProcessor::ICallback
{
public:
    NotesProcessorCallback(
        SyncChunksDataCountersPtr syncChunksDataCounters,
        IDownloader::ICallbackWeakPtr callbackWeak,
        std::shared_ptr<QMutex> mutex, const quint64 totalNotesToDownload,
        std::optional<qevercloud::LinkedNotebook> linkedNotebook) :
        m_syncChunksDataCounters{std::move(syncChunksDataCounters)},
        m_callbackWeak{std::move(callbackWeak)}, m_mutex{std::move(mutex)},
        m_totalNotesToDownload{totalNotesToDownload},
        m_linkedNotebook{std::move(linkedNotebook)}
    {
        Q_ASSERT(m_syncChunksDataCounters);
        Q_ASSERT(!m_callbackWeak.expired());
        Q_ASSERT(m_mutex);
        Q_ASSERT(m_totalNotesToDownload <= std::numeric_limits<quint32>::max());
    }

    // IDurableNotesProcessor::ICallback
    void onProcessedNote(
        [[maybe_unused]] const qevercloud::Guid & noteGuid,
        [[maybe_unused]] qint32 noteUpdateSequenceNum) noexcept override
    {
        incrementDownloadedNotes();
    }

    void onExpungedNote(
        [[maybe_unused]] const qevercloud::Guid & noteGuid) noexcept override
    {}

    void onFailedToExpungeNote(
        [[maybe_unused]] const qevercloud::Guid & noteGuid,
        [[maybe_unused]] const QException & e) noexcept override
    {}

    void onNoteFailedToDownload(
        [[maybe_unused]] const qevercloud::Note & note,
        [[maybe_unused]] const QException & e) noexcept override
    {
        incrementDownloadedNotes();
    }

    void onNoteFailedToProcess(
        [[maybe_unused]] const qevercloud::Note & note,
        [[maybe_unused]] const QException & e) noexcept override
    {
        incrementDownloadedNotes();
    }

    void onNoteProcessingCancelled(
        [[maybe_unused]] const qevercloud::Note & note) noexcept override
    {}

private:
    void incrementDownloadedNotes()
    {
        const auto callback = m_callbackWeak.lock();
        if (!callback) {
            return;
        }

        const quint64 downloadedNotes =
            m_downloadedNotes.fetch_add(1UL, std::memory_order_acq_rel) + 1;
        Q_ASSERT(downloadedNotes <= std::numeric_limits<quint32>::max());

        if (m_linkedNotebook) {
            callback->onLinkedNotebookNotesDownloadProgress(
                static_cast<quint32>(downloadedNotes),
                static_cast<quint32>(m_totalNotesToDownload),
                *m_linkedNotebook);
        }
        else {
            callback->onNotesDownloadProgress(
                static_cast<quint32>(downloadedNotes),
                static_cast<quint32>(m_totalNotesToDownload));
        }
    }

private:
    const SyncChunksDataCountersPtr m_syncChunksDataCounters;
    const IDownloader::ICallbackWeakPtr m_callbackWeak;
    const std::shared_ptr<QMutex> m_mutex;
    const quint64 m_totalNotesToDownload;
    const std::optional<qevercloud::LinkedNotebook> m_linkedNotebook;

    std::atomic<quint64> m_downloadedNotes{0UL};
};

////////////////////////////////////////////////////////////////////////////////

class Downloader::ResourcesProcessorCallback final :
    public IDurableResourcesProcessor::ICallback
{
public:
    ResourcesProcessorCallback(
        SyncChunksDataCountersPtr syncChunksDataCounters,
        IDownloader::ICallbackWeakPtr callbackWeak,
        std::shared_ptr<QMutex> mutex, const quint64 totalResourcesToDownload,
        std::optional<qevercloud::LinkedNotebook> linkedNotebook) :
        m_syncChunksDataCounters{std::move(syncChunksDataCounters)},
        m_callbackWeak{std::move(callbackWeak)}, m_mutex{std::move(mutex)},
        m_totalResourcesToDownload{totalResourcesToDownload},
        m_linkedNotebook{std::move(linkedNotebook)}
    {
        Q_ASSERT(m_syncChunksDataCounters);
        Q_ASSERT(!m_callbackWeak.expired());
        Q_ASSERT(m_mutex);
        Q_ASSERT(
            m_totalResourcesToDownload <= std::numeric_limits<quint32>::max());
    }

    // IDurableResourcesProcessor::ICallback
    void onProcessedResource(
        [[maybe_unused]] const qevercloud::Guid & resourceGuid,
        [[maybe_unused]] qint32 resourceUpdateSequenceNum) noexcept override
    {
        incrementDownloadedResources();
    }

    void onResourceFailedToDownload(
        [[maybe_unused]] const qevercloud::Resource & resource,
        [[maybe_unused]] const QException & e) noexcept override
    {
        incrementDownloadedResources();
    }

    void onResourceFailedToProcess(
        [[maybe_unused]] const qevercloud::Resource & resource,
        [[maybe_unused]] const QException & e) noexcept override
    {
        incrementDownloadedResources();
    }

    void onResourceProcessingCancelled(
        [[maybe_unused]] const qevercloud::Resource & resource) noexcept
        override
    {}

private:
    void incrementDownloadedResources()
    {
        const auto callback = m_callbackWeak.lock();
        if (!callback) {
            return;
        }

        const quint64 downloadedResources =
            m_downloadedResources.fetch_add(1UL, std::memory_order_acq_rel) + 1;
        Q_ASSERT(downloadedResources <= std::numeric_limits<quint32>::max());

        if (m_linkedNotebook) {
            callback->onLinkedNotebookResourcesDownloadProgress(
                static_cast<quint32>(downloadedResources),
                static_cast<quint32>(m_totalResourcesToDownload),
                *m_linkedNotebook);
        }
        else {
            callback->onResourcesDownloadProgress(
                static_cast<quint32>(downloadedResources),
                static_cast<quint32>(m_totalResourcesToDownload));
        }
    }

private:
    const SyncChunksDataCountersPtr m_syncChunksDataCounters;
    const IDownloader::ICallbackWeakPtr m_callbackWeak;
    const std::shared_ptr<QMutex> m_mutex;
    const quint64 m_totalResourcesToDownload;
    const std::optional<qevercloud::LinkedNotebook> m_linkedNotebook;

    std::atomic<quint64> m_downloadedResources{0UL};
};

////////////////////////////////////////////////////////////////////////////////

class Downloader::SavedSearchesProcessorCallback final :
    public ISavedSearchesProcessor::ICallback
{
public:
    SavedSearchesProcessorCallback(
        SyncChunksDataCountersPtr syncChunksDataCounters,
        IDownloader::ICallbackWeakPtr callbackWeak,
        std::shared_ptr<QMutex> mutex) :
        m_syncChunksDataCounters{std::move(syncChunksDataCounters)},
        m_callbackWeak{std::move(callbackWeak)}, m_mutex{std::move(mutex)}
    {
        Q_ASSERT(m_syncChunksDataCounters);
        Q_ASSERT(!m_callbackWeak.expired());
        Q_ASSERT(m_mutex);
    }

    // ISavedSearchesProcessor::ICallback
    void onSavedSearchesProcessingProgress(
        qint32 totalSavedSearches, qint32 totalSavedSearchesToExpunge, // NOLINT
        qint32 addedSavedSearches, qint32 updatedSavedSearches,        // NOLINT
        qint32 expungedSavedSearches) override
    {
        const auto callback = m_callbackWeak.lock();
        if (!callback) {
            return;
        }

        Q_ASSERT(totalSavedSearches >= 0);
        Q_ASSERT(totalSavedSearchesToExpunge >= 0);
        Q_ASSERT(addedSavedSearches >= 0);
        Q_ASSERT(updatedSavedSearches >= 0);
        Q_ASSERT(expungedSavedSearches >= 0);

        const QMutexLocker lock{m_mutex.get()};

        Q_ASSERT(
            m_syncChunksDataCounters->m_totalSavedSearches == 0 ||
            m_syncChunksDataCounters->m_totalSavedSearches ==
                static_cast<quint64>(totalSavedSearches));

        Q_ASSERT(
            m_syncChunksDataCounters->m_totalExpungedSavedSearches == 0 ||
            m_syncChunksDataCounters->m_totalExpungedSavedSearches ==
                static_cast<quint64>(totalSavedSearchesToExpunge));

        m_syncChunksDataCounters->m_totalSavedSearches =
            static_cast<quint64>(totalSavedSearches);

        m_syncChunksDataCounters->m_totalExpungedSavedSearches =
            static_cast<quint64>(totalSavedSearchesToExpunge);

        m_syncChunksDataCounters->m_addedSavedSearches =
            static_cast<quint64>(addedSavedSearches);

        m_syncChunksDataCounters->m_updatedSavedSearches =
            static_cast<quint64>(updatedSavedSearches);

        m_syncChunksDataCounters->m_expungedSavedSearches =
            static_cast<quint64>(expungedSavedSearches);

        callback->onSyncChunksDataProcessingProgress(
            std::make_shared<SyncChunksDataCounters>(
                *m_syncChunksDataCounters));
    }

private:
    const SyncChunksDataCountersPtr m_syncChunksDataCounters;
    const IDownloader::ICallbackWeakPtr m_callbackWeak;
    const std::shared_ptr<QMutex> m_mutex;
};

////////////////////////////////////////////////////////////////////////////////

class Downloader::TagsProcessorCallback final : public ITagsProcessor::ICallback
{
public:
    TagsProcessorCallback(
        SyncChunksDataCountersPtr syncChunksDataCounters,
        IDownloader::ICallbackWeakPtr callbackWeak,
        std::shared_ptr<QMutex> mutex,
        std::optional<qevercloud::LinkedNotebook> linkedNotebook) :
        m_syncChunksDataCounters{std::move(syncChunksDataCounters)},
        m_callbackWeak{std::move(callbackWeak)}, m_mutex{std::move(mutex)},
        m_linkedNotebook{std::move(linkedNotebook)}
    {
        Q_ASSERT(m_syncChunksDataCounters);
        Q_ASSERT(!m_callbackWeak.expired());
        Q_ASSERT(m_mutex);
    }

    // ITagsProcessor::ICallback
    void onTagsProcessingProgress(
        qint32 totalTags, qint32 totalTagsToExpunge, qint32 addedTags, // NOLINT
        qint32 updatedTags, qint32 expungedTags) override              // NOLINT
    {
        const auto callback = m_callbackWeak.lock();
        if (!callback) {
            return;
        }

        Q_ASSERT(totalTags >= 0);
        Q_ASSERT(totalTagsToExpunge >= 0);
        Q_ASSERT(addedTags >= 0);
        Q_ASSERT(updatedTags >= 0);
        Q_ASSERT(expungedTags >= 0);

        const QMutexLocker lock{m_mutex.get()};

        Q_ASSERT(
            m_syncChunksDataCounters->m_totalTags == 0 ||
            m_syncChunksDataCounters->m_totalTags ==
                static_cast<quint64>(totalTags));

        Q_ASSERT(
            m_syncChunksDataCounters->m_totalExpungedTags == 0 ||
            m_syncChunksDataCounters->m_totalExpungedTags ==
                static_cast<quint64>(totalTagsToExpunge));

        m_syncChunksDataCounters->m_totalTags = static_cast<quint64>(totalTags);

        m_syncChunksDataCounters->m_totalExpungedTags =
            static_cast<quint64>(totalTagsToExpunge);

        m_syncChunksDataCounters->m_addedTags = static_cast<quint64>(addedTags);

        m_syncChunksDataCounters->m_updatedTags =
            static_cast<quint64>(updatedTags);

        m_syncChunksDataCounters->m_expungedTags =
            static_cast<quint64>(expungedTags);

        if (m_linkedNotebook) {
            callback->onLinkedNotebookSyncChunksDataProcessingProgress(
                std::make_shared<SyncChunksDataCounters>(
                    *m_syncChunksDataCounters),
                *m_linkedNotebook);
        }
        else {
            callback->onSyncChunksDataProcessingProgress(
                std::make_shared<SyncChunksDataCounters>(
                    *m_syncChunksDataCounters));
        }
    }

private:
    const SyncChunksDataCountersPtr m_syncChunksDataCounters;
    const IDownloader::ICallbackWeakPtr m_callbackWeak;
    const std::shared_ptr<QMutex> m_mutex;
    const std::optional<qevercloud::LinkedNotebook> m_linkedNotebook;
};

////////////////////////////////////////////////////////////////////////////////

Downloader::Downloader(
    Account account, IAuthenticationInfoProviderPtr authenticationInfoProvider,
    ISyncStateStoragePtr syncStateStorage,
    ISyncChunksProviderPtr syncChunksProvider,
    ILinkedNotebooksProcessorPtr linkedNotebooksProcessor,
    INotebooksProcessorPtr notebooksProcessor,
    IDurableNotesProcessorPtr notesProcessor,
    IDurableResourcesProcessorPtr resourcesProcessor,
    ISavedSearchesProcessorPtr savedSearchesProcessor,
    ITagsProcessorPtr tagsProcessor,
    IFullSyncStaleDataExpungerPtr fullSyncStaleDataExpunger,
    INoteStoreProviderPtr noteStoreProvider,
    local_storage::ILocalStoragePtr localStorage,
    qevercloud::IRequestContextPtr ctx,
    qevercloud::IRetryPolicyPtr retryPolicy) :
    m_account{std::move(account)},
    m_authenticationInfoProvider{std::move(authenticationInfoProvider)},
    m_syncStateStorage{std::move(syncStateStorage)},
    m_syncChunksProvider{std::move(syncChunksProvider)},
    m_linkedNotebooksProcessor{std::move(linkedNotebooksProcessor)},
    m_notebooksProcessor{std::move(notebooksProcessor)},
    // clang-format does some weird crap here, working around
    // clang-format off
    m_notesProcessor{std::move(notesProcessor)},
    m_resourcesProcessor{std::move(resourcesProcessor)},
    m_savedSearchesProcessor{std::move(savedSearchesProcessor)},
    m_tagsProcessor{std::move(tagsProcessor)},
    m_fullSyncStaleDataExpunger{std::move(fullSyncStaleDataExpunger)},
    m_noteStoreProvider{std::move(noteStoreProvider)},
    m_localStorage{std::move(localStorage)},
    m_ctx{std::move(ctx)},
    m_retryPolicy{std::move(retryPolicy)},
    m_mutex{std::make_shared<QMutex>()}
// clang-format on
{
    if (Q_UNLIKELY(m_account.isEmpty())) {
        throw InvalidArgument{
            ErrorString{QStringLiteral("Downloader ctor: account is empty")}};
    }

    if (Q_UNLIKELY(m_account.type() != Account::Type::Evernote)) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "Downloader ctor: account is not of Evernote type")}};
    }

    if (Q_UNLIKELY(!m_authenticationInfoProvider)) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "Downloader ctor: authentication info provider is null")}};
    }

    if (Q_UNLIKELY(!m_syncStateStorage)) {
        throw InvalidArgument{ErrorString{
            QStringLiteral("Downloader ctor: sync state storage is null")}};
    }

    if (Q_UNLIKELY(!m_syncChunksProvider)) {
        throw InvalidArgument{ErrorString{
            QStringLiteral("Downloader ctor: sync chunks provider is null")}};
    }

    if (Q_UNLIKELY(!m_linkedNotebooksProcessor)) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "Downloader ctor: linked notebooks processor is null")}};
    }

    if (Q_UNLIKELY(!m_notebooksProcessor)) {
        throw InvalidArgument{ErrorString{
            QStringLiteral("Downloader ctor: notebooks processor is null")}};
    }

    if (Q_UNLIKELY(!m_notesProcessor)) {
        throw InvalidArgument{ErrorString{
            QStringLiteral("Downloader ctor: notes processor is null")}};
    }

    if (Q_UNLIKELY(!m_resourcesProcessor)) {
        throw InvalidArgument{ErrorString{
            QStringLiteral("Downloader ctor: resources processor is null")}};
    }

    if (Q_UNLIKELY(!m_savedSearchesProcessor)) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "Downloader ctor: saved searches processor is null")}};
    }

    if (Q_UNLIKELY(!m_tagsProcessor)) {
        throw InvalidArgument{ErrorString{
            QStringLiteral("Downloader ctor: tags processor is null")}};
    }

    if (Q_UNLIKELY(!m_fullSyncStaleDataExpunger)) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "Downloader ctor: full sync stale data expunger is null")}};
    }

    if (Q_UNLIKELY(!m_noteStoreProvider)) {
        throw InvalidArgument{ErrorString{
            QStringLiteral("Downloader ctor: note store provider is null")}};
    }

    if (Q_UNLIKELY(!m_localStorage)) {
        throw InvalidArgument{ErrorString{
            QStringLiteral("Downloader ctor: local storage is null")}};
    }
}

Downloader::~Downloader() = default;

QFuture<IDownloader::Result> Downloader::download(
    utility::cancelers::ICancelerPtr canceler, ICallbackWeakPtr callbackWeak)
{
    QNDEBUG("synchronization::Downloader", "Downloader::download");

    auto lastSyncState = readLastSyncState(m_syncStateStorage, m_account);
    Q_ASSERT(lastSyncState);

    QNDEBUG(
        "synchronization::Downloader", "Last sync state: " << *lastSyncState);

    auto promise = std::make_shared<QPromise<Result>>();
    auto future = promise->future();
    promise->start();

    if (Q_UNLIKELY(canceler->isCanceled())) {
        cancel(*promise);
        return promise->future();
    }

    auto authenticationInfoFuture =
        m_authenticationInfoProvider->authenticateAccount(
            m_account, IAuthenticationInfoProvider::Mode::Cache);

    threading::bindCancellation(future, authenticationInfoFuture);

    const auto selfWeak = weak_from_this();

    threading::thenOrFailed(
        std::move(authenticationInfoFuture), promise,
        threading::TrackedTask{
            selfWeak,
            [this, selfWeak, callbackWeak = std::move(callbackWeak),
             canceler = std::move(canceler),
             lastSyncState = std::move(lastSyncState), promise](
                const IAuthenticationInfoPtr & authenticationInfo) mutable {
                Q_ASSERT(authenticationInfo);

                if (canceler->isCanceled()) {
                    cancel(*promise);
                    return;
                }

                auto downloadFuture = launchDownload(
                    *authenticationInfo, std::move(lastSyncState),
                    std::move(canceler), std::move(callbackWeak));

                threading::bindCancellation(promise->future(), downloadFuture);

                threading::mapFutureProgress(downloadFuture, promise);

                threading::thenOrFailed(
                    std::move(downloadFuture), promise,
                    [promise](Result result) {
                        promise->addResult(std::move(result));
                        promise->finish();
                    });
            }});

    return future;
}

QFuture<IDownloader::Result> Downloader::launchDownload(
    const IAuthenticationInfo & authenticationInfo, SyncStatePtr lastSyncState,
    utility::cancelers::ICancelerPtr canceler, ICallbackWeakPtr callbackWeak)
{
    QNDEBUG("synchronization::Downloader", "Downloader::launchDownload");

    Q_ASSERT(lastSyncState);

    auto promise = std::make_shared<QPromise<Result>>();
    auto future = promise->future();

    promise->start();

    auto ctxBuilder = qevercloud::RequestContextBuilder{};
    ctxBuilder.setAuthenticationToken(authenticationInfo.authToken())
        .setCookies(authenticationInfo.userStoreCookies());

    if (m_ctx) {
        ctxBuilder.setConnectionTimeout(m_ctx->connectionTimeout())
            .setIncreaseConnectionTimeoutExponentially(
                m_ctx->increaseConnectionTimeoutExponentially())
            .setMaxConnectionTimeout(m_ctx->maxConnectionTimeout())
            .setMaxRetryCount(m_ctx->maxRequestRetryCount());
    }

    auto ctx = ctxBuilder.build();

    auto downloadContext = std::make_shared<DownloadContext>();
    downloadContext->lastSyncState = std::move(lastSyncState);
    downloadContext->lastSyncStateMutex = std::make_shared<QMutex>();
    downloadContext->promise = promise;
    downloadContext->ctx = std::move(ctx);
    downloadContext->canceler = std::move(canceler);
    downloadContext->callbackWeak = std::move(callbackWeak);

    auto noteStoreFuture = m_noteStoreProvider->userOwnNoteStore(
        downloadContext->ctx, m_retryPolicy);

    threading::thenOrFailed(
        std::move(noteStoreFuture), promise,
        [selfWeak = weak_from_this(), promise,
         downloadContext = std::move(downloadContext)](
            const qevercloud::INoteStorePtr & noteStore) mutable {
            Q_ASSERT(noteStore);

            auto syncStateFuture =
                noteStore->getSyncStateAsync(downloadContext->ctx);

            threading::thenOrFailed(
                std::move(syncStateFuture), promise,
                [selfWeak, downloadContext = std::move(downloadContext)](
                    qevercloud::SyncState && syncState) mutable {
                    if (const auto self = selfWeak.lock()) {
                        QNDEBUG(
                            "synchronization::Downloader",
                            "Sync state from Evernote: " << syncState);

                        if (syncState.fullSyncBefore() >
                            downloadContext->lastSyncState
                                ->m_userDataLastSyncTime)
                        {
                            QNDEBUG(
                                "synchronization::Downloader",
                                "Performing full synchronization instead of "
                                "incremental one");

                            self->launchUserOwnDataDownload(
                                std::move(downloadContext),
                                SynchronizationMode::Full);
                        }
                        else if (
                            syncState.updateCount() ==
                            downloadContext->lastSyncState
                                ->m_userDataUpdateCount)
                        {
                            QNDEBUG(
                                "synchronization::Downloader",
                                "Evernote has no updates for user own data");

                            // NOTE: will nevertheless try to download both
                            // notes and resources with empty sync chunks list
                            // so that any notes or resources which haven't been
                            // downloaded/processed during the last previous
                            // sync would be attempted to be processed now
                            if (!downloadContext->syncChunksDataCounters) {
                                downloadContext->syncChunksDataCounters =
                                    std::make_shared<SyncChunksDataCounters>();
                            }

                            self->downloadNotes(
                                std::move(downloadContext),
                                SynchronizationMode::Incremental);
                        }
                        else {
                            QNDEBUG(
                                "synchronization::Downloader",
                                "Launching incremental sync of user own data");

                            self->launchUserOwnDataDownload(
                                std::move(downloadContext),
                                SynchronizationMode::Incremental);
                        }

                        return;
                    }

                    downloadContext->promise->setException(OperationCanceled{});
                    downloadContext->promise->finish();
                });
        });

    return future;
}

void Downloader::launchUserOwnDataDownload(
    DownloadContextPtr downloadContext, const SynchronizationMode syncMode)
{
    QNDEBUG(
        "synchronization::Downloader",
        "Downloader::launchUserOwnDataDownload: sync mode = " << syncMode);

    Q_ASSERT(downloadContext);

    const qint32 afterUsn =
        (syncMode == SynchronizationMode::Full
             ? 0
             : downloadContext->lastSyncState->m_userDataUpdateCount);

    auto syncChunksProviderCallback =
        std::make_shared<SyncChunksProviderCallback>(
            downloadContext->callbackWeak);

    auto syncChunksFuture = m_syncChunksProvider->fetchSyncChunks(
        afterUsn, syncMode, downloadContext->ctx, downloadContext->canceler,
        syncChunksProviderCallback);

    auto promise = downloadContext->promise;
    threading::thenOrFailed(
        std::move(syncChunksFuture), std::move(promise),
        threading::TrackedTask{
            weak_from_this(),
            [this, downloadContext = std::move(downloadContext), syncMode,
             syncChunksProviderCallback =
                 std::move(syncChunksProviderCallback)](
                QList<qevercloud::SyncChunk> syncChunks) mutable {
                if (const auto callback = downloadContext->callbackWeak.lock())
                {
                    callback->onSyncChunksDownloaded();
                }

                downloadContext->syncChunks = std::move(syncChunks);
                processSyncChunks(std::move(downloadContext), syncMode);
            }});
}

void Downloader::listLinkedNotebooksAndLaunchDataDownload(
    DownloadContextPtr downloadContext, const SynchronizationMode syncMode)
{
    auto listLinkedNotebooksFuture = m_localStorage->listLinkedNotebooks();

    const auto selfWeak = weak_from_this();
    auto promise = downloadContext->promise;
    threading::thenOrFailed(
        std::move(listLinkedNotebooksFuture), promise,
        threading::TrackedTask{
            selfWeak,
            [this, downloadContext = std::move(downloadContext), syncMode](
                QList<qevercloud::LinkedNotebook> linkedNotebooks) mutable {
                if (downloadContext->canceler->isCanceled()) {
                    cancel(*downloadContext->promise);
                    return;
                }

                launchLinkedNotebooksDataDownload(
                    std::move(downloadContext), syncMode,
                    std::move(linkedNotebooks));
            }});
}

void Downloader::launchLinkedNotebooksDataDownload(
    DownloadContextPtr downloadContext, const SynchronizationMode syncMode,
    QList<qevercloud::LinkedNotebook> linkedNotebooks)
{
    Q_ASSERT(downloadContext);
    Q_ASSERT(downloadContext->ctx);

    if (linkedNotebooks.isEmpty()) {
        finalize(downloadContext);
        return;
    }

    if (const auto callback = downloadContext->callbackWeak.lock()) {
        callback->onStartLinkedNotebooksDataDownloading(linkedNotebooks);
    }

    auto noteStoreFuture = m_noteStoreProvider->userOwnNoteStore(
        downloadContext->ctx, m_retryPolicy);

    auto promise = downloadContext->promise;
    threading::thenOrFailed(
        std::move(noteStoreFuture), promise,
        [selfWeak = weak_from_this(), this, promise, syncMode,
         downloadContext = std::move(downloadContext),
         linkedNotebooks = std::move(linkedNotebooks)](
            const qevercloud::INoteStorePtr & noteStore) mutable {
            Q_ASSERT(noteStore);

            const auto self = selfWeak.lock();
            if (!self) {
                downloadContext->promise->setException(OperationCanceled{});
                downloadContext->promise->finish();
                return;
            }

            QList<qevercloud::Guid> linkedNotebookGuids;
            linkedNotebookGuids.reserve(std::max(linkedNotebooks.size(), 0));

            QList<QFuture<Result>> linkedNotebookFutures;
            linkedNotebookFutures.reserve(std::max(linkedNotebooks.size(), 0));

            for (const auto & linkedNotebook: qAsConst(linkedNotebooks)) {
                if (Q_UNLIKELY(!linkedNotebook.guid())) {
                    QNWARNING(
                        "synchronization::Downloader",
                        "Skipping linked notebook without guid: "
                            << linkedNotebook);
                    continue;
                }

                linkedNotebookGuids << *linkedNotebook.guid();

                auto syncStateFuture =
                    noteStore->getLinkedNotebookSyncStateAsync(
                        linkedNotebook, downloadContext->ctx);

                auto linkedNotebookResultPromise =
                    std::make_shared<QPromise<Result>>();

                linkedNotebookFutures << linkedNotebookResultPromise->future();
                linkedNotebookResultPromise->start();

                threading::thenOrFailed(
                    std::move(syncStateFuture), linkedNotebookResultPromise,
                    [selfWeak, this, linkedNotebookResultPromise, syncMode,
                     downloadContext, linkedNotebook = linkedNotebook](
                        qevercloud::SyncState linkedNotebookSyncState) mutable {
                        const auto self = selfWeak.lock();
                        if (!self) {
                            downloadContext->promise->setException(
                                OperationCanceled{});
                            downloadContext->promise->finish();
                            return;
                        }

                        auto f =
                            fetchAuthInfoAndStartLinkedNotebookDataDownload(
                                downloadContext,
                                std::move(linkedNotebookSyncState), syncMode,
                                std::move(linkedNotebook));

                        threading::thenOrFailed(
                            std::move(f), linkedNotebookResultPromise,
                            [linkedNotebookResultPromise](Result result) {
                                linkedNotebookResultPromise->addResult(
                                    std::move(result));
                                linkedNotebookResultPromise->finish();
                            });
                    });
            }

            if (linkedNotebookFutures.isEmpty()) {
                finalize(downloadContext);
                return;
            }

            auto allLinkedNotebooksFuture =
                threading::whenAll<Result>(std::move(linkedNotebookFutures));

            threading::thenOrFailed(
                std::move(allLinkedNotebooksFuture), promise,
                [downloadContext = std::move(downloadContext),
                 linkedNotebookGuids = std::move(linkedNotebookGuids)](
                    QList<Result> linkedNotebookResults) {
                    const int size = linkedNotebookResults.size();
                    Q_ASSERT(linkedNotebookGuids.size() == size);

                    QHash<qevercloud::Guid, LocalResult> results;
                    results.reserve(std::max(size, 0));

                    for (int i = 0; i < size; ++i) {
                        // Somewhat confusing piece: for the sake of
                        // code unification when processing stuff from
                        // linked notebooks we still put things into
                        // userOwnResult of local DownloadContext
                        // results and only here we properly unite all
                        // the local results into a single global one.
                        auto & currentResult =
                            linkedNotebookResults[i].userOwnResult;

                        results[linkedNotebookGuids[i]] = LocalResult{
                            std::move(currentResult.syncChunksDataCounters),
                            std::move(currentResult.downloadNotesStatus),
                            std::move(currentResult.downloadResourcesStatus)};
                    }

                    Downloader::updateSyncState(*downloadContext);
                    downloadContext->promise->addResult(Result{
                        LocalResult{
                            std::move(downloadContext->syncChunksDataCounters),
                            std::move(downloadContext->downloadNotesStatus),
                            std::move(
                                downloadContext->downloadResourcesStatus)},
                        std::move(results),
                        std::move(downloadContext->lastSyncState)});

                    downloadContext->promise->finish();
                });
        });
}

QFuture<IDownloader::Result>
    Downloader::fetchAuthInfoAndStartLinkedNotebookDataDownload(
        const DownloadContextPtr & downloadContext,
        qevercloud::SyncState linkedNotebookSyncState,
        const SynchronizationMode syncMode,
        qevercloud::LinkedNotebook linkedNotebook)
{
    Q_ASSERT(downloadContext);

    auto authFuture =
        m_authenticationInfoProvider->authenticateToLinkedNotebook(
            m_account, linkedNotebook,
            IAuthenticationInfoProvider::Mode::Cache);

    auto promise = std::make_shared<QPromise<Result>>();
    promise->start();

    auto future = promise->future();

    threading::thenOrFailed(
        std::move(authFuture), promise,
        threading::TrackedTask{
            weak_from_this(),
            [this, downloadContext, syncMode, promise = promise,
             linkedNotebook = std::move(linkedNotebook),
             linkedNotebookSyncState = std::move(linkedNotebookSyncState)](
                const IAuthenticationInfoPtr & authInfo) mutable {
                Q_ASSERT(authInfo);

                auto linkedNotebookDownloadContext =
                    std::make_shared<DownloadContext>();

                linkedNotebookDownloadContext->promise = std::move(promise);
                linkedNotebookDownloadContext->lastSyncState =
                    downloadContext->lastSyncState;

                linkedNotebookDownloadContext->lastSyncStateMutex =
                    downloadContext->lastSyncStateMutex;

                linkedNotebookDownloadContext->canceler =
                    downloadContext->canceler;

                linkedNotebookDownloadContext->callbackWeak =
                    downloadContext->callbackWeak;

                linkedNotebookDownloadContext->ctx = downloadContext->ctx;
                linkedNotebookDownloadContext->linkedNotebook =
                    std::move(linkedNotebook);

                linkedNotebookDownloadContext->linkedNotebookAuthToken =
                    authInfo->authToken();

                startLinkedNotebookDataDownload(
                    std::move(linkedNotebookDownloadContext),
                    linkedNotebookSyncState, syncMode);
            }});

    return future;
}

void Downloader::startLinkedNotebookDataDownload(
    DownloadContextPtr downloadContext,
    const qevercloud::SyncState & linkedNotebookSyncState,
    const SynchronizationMode syncMode)
{
    Q_ASSERT(downloadContext);
    Q_ASSERT(downloadContext->linkedNotebook);
    Q_ASSERT(downloadContext->linkedNotebook->guid());
    Q_ASSERT(downloadContext->linkedNotebookAuthToken);

    const qint32 afterUsn = [&] {
        if (syncMode == SynchronizationMode::Full) {
            return 0;
        }

        const QMutexLocker locked{downloadContext->lastSyncStateMutex.get()};

        const auto & linkedNotebookUpdateCounts =
            downloadContext->lastSyncState->m_linkedNotebookUpdateCounts;

        const auto it = linkedNotebookUpdateCounts.constFind(
            *downloadContext->linkedNotebook->guid());
        if (it != linkedNotebookUpdateCounts.constEnd()) {
            return it.value();
        }

        return 0;
    }();

    if (afterUsn == linkedNotebookSyncState.updateCount()) {
        QNDEBUG(
            "synchronization::Downloader",
            "Evernote has no updates for linked notebook "
                << linkedNotebookInfo(*downloadContext->linkedNotebook));
        finalize(downloadContext);
        return;
    }

    auto syncChunksProviderCallback =
        std::make_shared<SyncChunksProviderCallback>(
            downloadContext->callbackWeak);

    auto syncChunksFuture = m_syncChunksProvider->fetchLinkedNotebookSyncChunks(
        *downloadContext->linkedNotebook, afterUsn, syncMode,
        downloadContext->ctx, downloadContext->canceler,
        syncChunksProviderCallback);

    auto promise = downloadContext->promise;
    threading::thenOrFailed(
        std::move(syncChunksFuture), std::move(promise),
        threading::TrackedTask{
            weak_from_this(),
            [this, syncMode, downloadContext = std::move(downloadContext),
             syncChunksProviderCallback =
                 std::move(syncChunksProviderCallback)](
                QList<qevercloud::SyncChunk> syncChunks) mutable {
                if (const auto callback = downloadContext->callbackWeak.lock())
                {
                    callback->onLinkedNotebookSyncChunksDownloaded(
                        *downloadContext->linkedNotebook);
                }

                downloadContext->syncChunks = std::move(syncChunks);

                // Below calls to NoteStore should use authentication token
                // corresponding to the linked notebook instead of the
                // token corresponding to user own account
                auto ctxBuilder = qevercloud::RequestContextBuilder{};
                ctxBuilder.setAuthenticationToken(
                    *downloadContext->linkedNotebookAuthToken);

                if (downloadContext->ctx) {
                    ctxBuilder
                        .setCookies(downloadContext->ctx->cookies())
                        .setConnectionTimeout(
                            downloadContext->ctx->connectionTimeout())
                        .setMaxConnectionTimeout(
                            downloadContext->ctx->maxConnectionTimeout())
                        .setIncreaseConnectionTimeoutExponentially(
                            downloadContext->ctx
                            ->increaseConnectionTimeoutExponentially())
                        .setMaxRetryCount(
                            downloadContext->ctx->maxRequestRetryCount());
                }

                downloadContext->ctx = ctxBuilder.build();

                processSyncChunks(std::move(downloadContext), syncMode);
            }});
}

void Downloader::processSyncChunks(
    DownloadContextPtr downloadContext, SynchronizationMode syncMode,
    CheckForFirstSync checkForFirstSync)
{
    QNDEBUG("synchronization::Downloader", "Downloader::processSyncChunks");

    Q_ASSERT(downloadContext);

    if (downloadContext->canceler->isCanceled()) {
        cancel(*downloadContext->promise);
        return;
    }

    Q_ASSERT(downloadContext->lastSyncState);

    if (downloadContext->syncChunks.isEmpty()) {
        if (downloadContext->linkedNotebook) {
            QNINFO(
                "synchronization::Downloader",
                "No new data found in Evernote for linked notebook of "
                    << downloadContext->linkedNotebook->username().value_or(
                           QStringLiteral("<unknown>")));
        }
        else {
            QNINFO(
                "synchronization::Downloader",
                "No new data found in Evernote for user's own account");
        }

        // NOTE: will nevertheless try to download both notes and
        // resources with empty sync chunks list so that any notes
        // or resources which haven't been downloaded/processed
        // during the last previous sync would be attempted to be
        // processed now
        if (!downloadContext->syncChunksDataCounters) {
            downloadContext->syncChunksDataCounters =
                std::make_shared<SyncChunksDataCounters>();
        }

        downloadNotes(std::move(downloadContext), syncMode);
        return;
    }

    const auto selfWeak = weak_from_this();

    if (checkForFirstSync == CheckForFirstSync::Yes) {
        const bool isFirstSync = [&] {
            const QMutexLocker locker{
                downloadContext->lastSyncStateMutex.get()};

            if (downloadContext->linkedNotebook) {
                Q_ASSERT(downloadContext->linkedNotebook->guid());
                const auto & linkedNotebookUpdateCounts =
                    downloadContext->lastSyncState
                        ->m_linkedNotebookUpdateCounts;
                const auto it = linkedNotebookUpdateCounts.constFind(
                    *downloadContext->linkedNotebook->guid());
                if (it == linkedNotebookUpdateCounts.constEnd()) {
                    return true;
                }

                return it.value() == 0;
            }

            return downloadContext->lastSyncState->userDataUpdateCount() == 0;
        }();

        if (!isFirstSync && (syncMode == SynchronizationMode::Full)) {
            auto preservedGuids =
                collectPreservedGuids(downloadContext->syncChunks);

            auto future = m_fullSyncStaleDataExpunger->expungeStaleData(
                std::move(preservedGuids), downloadContext->canceler,
                downloadContext->linkedNotebook
                    ? downloadContext->linkedNotebook->guid()
                    : std::nullopt);

            auto promise = downloadContext->promise;
            threading::thenOrFailed(
                std::move(future), promise,
                threading::TrackedTask{
                    selfWeak,
                    [this, downloadContext = std::move(downloadContext),
                     syncMode]() mutable {
                        processSyncChunks(
                            std::move(downloadContext), syncMode,
                            CheckForFirstSync::No);
                    }});
            return;
        }
    }

    struct Callbacks
    {
        std::shared_ptr<LinkedNotebooksProcessorCallback>
            linkedNotebooksProcessorCallback;

        std::shared_ptr<NotebooksProcessorCallback> notebooksProcessorCallback;
        std::shared_ptr<TagsProcessorCallback> tagsProcessorCallback;

        std::shared_ptr<SavedSearchesProcessorCallback>
            savedSearchesProcessorCallback;
    };

    if (!downloadContext->syncChunksDataCounters) {
        downloadContext->syncChunksDataCounters =
            std::make_shared<SyncChunksDataCounters>();
        initializeTotalsInSyncChunksDataCounters(
            downloadContext->syncChunks,
            *downloadContext->syncChunksDataCounters);
    }

    Callbacks callbacks;
    callbacks.notebooksProcessorCallback =
        std::make_shared<NotebooksProcessorCallback>(
            downloadContext->syncChunksDataCounters,
            downloadContext->callbackWeak, m_mutex,
            downloadContext->linkedNotebook);

    callbacks.tagsProcessorCallback = std::make_shared<TagsProcessorCallback>(
        downloadContext->syncChunksDataCounters, downloadContext->callbackWeak,
        m_mutex, downloadContext->linkedNotebook);

    if (!downloadContext->linkedNotebook) {
        callbacks.linkedNotebooksProcessorCallback =
            std::make_shared<LinkedNotebooksProcessorCallback>(
                downloadContext->syncChunksDataCounters,
                downloadContext->callbackWeak, m_mutex);

        callbacks.savedSearchesProcessorCallback =
            std::make_shared<SavedSearchesProcessorCallback>(
                downloadContext->syncChunksDataCounters,
                downloadContext->callbackWeak, m_mutex);
    }

    auto notebooksFuture = m_notebooksProcessor->processNotebooks(
        downloadContext->syncChunks, callbacks.notebooksProcessorCallback);

    auto tagsFuture = m_tagsProcessor->processTags(
        downloadContext->syncChunks, callbacks.tagsProcessorCallback);

    auto savedSearchesFuture =
        (downloadContext->linkedNotebook
             ? threading::makeReadyFuture()
             : m_savedSearchesProcessor->processSavedSearches(
                   downloadContext->syncChunks,
                   callbacks.savedSearchesProcessorCallback));

    auto linkedNotebooksFuture =
        (downloadContext->linkedNotebook
             ? threading::makeReadyFuture()
             : m_linkedNotebooksProcessor->processLinkedNotebooks(
                   downloadContext->syncChunks,
                   callbacks.linkedNotebooksProcessorCallback));

    auto allFirstStageFuture = threading::whenAll(
        QList<QFuture<void>>{} << notebooksFuture << tagsFuture
                               << savedSearchesFuture << linkedNotebooksFuture);

    auto promise = downloadContext->promise;
    threading::thenOrFailed(
        std::move(allFirstStageFuture), promise,
        threading::TrackedTask{
            selfWeak,
            [this, downloadContext = std::move(downloadContext),
             callbacks = std::move(callbacks), syncMode]() mutable {
                downloadNotes(std::move(downloadContext), syncMode);
            }});
}

void Downloader::initializeTotalsInSyncChunksDataCounters(
    const QList<qevercloud::SyncChunk> & syncChunks,
    SyncChunksDataCounters & syncChunksDataCounters) const
{
    int totalSavedSearches = 0;
    int totalExpungedSavedSearches = 0;

    int totalTags = 0;
    int totalExpungedTags = 0;

    int totalLinkedNotebooks = 0;
    int totalExpungedLinkedNotebooks = 0;

    int totalNotebooks = 0;
    int totalExpungedNotebooks = 0;

    for (const auto & syncChunk: qAsConst(syncChunks)) {
        if (syncChunk.searches()) {
            totalSavedSearches += syncChunk.searches()->size();
        }

        if (syncChunk.expungedSearches()) {
            totalExpungedSavedSearches += syncChunk.expungedSearches()->size();
        }

        if (syncChunk.tags()) {
            totalTags += syncChunk.tags()->size();
        }

        if (syncChunk.expungedTags()) {
            totalExpungedTags += syncChunk.expungedTags()->size();
        }

        if (syncChunk.linkedNotebooks()) {
            totalLinkedNotebooks += syncChunk.linkedNotebooks()->size();
        }

        if (syncChunk.expungedLinkedNotebooks()) {
            totalExpungedLinkedNotebooks +=
                syncChunk.expungedLinkedNotebooks()->size();
        }

        if (syncChunk.notebooks()) {
            totalNotebooks += syncChunk.notebooks()->size();
        }

        if (syncChunk.expungedNotebooks()) {
            totalExpungedNotebooks += syncChunk.expungedNotebooks()->size();
        }
    }

    const auto toCounter = [](const int value) {
        return static_cast<quint64>(std::max(value, 0));
    };

    syncChunksDataCounters.m_totalSavedSearches = toCounter(totalSavedSearches);
    syncChunksDataCounters.m_totalExpungedSavedSearches =
        toCounter(totalExpungedSavedSearches);

    syncChunksDataCounters.m_totalTags = toCounter(totalTags);
    syncChunksDataCounters.m_totalExpungedTags = toCounter(totalExpungedTags);

    syncChunksDataCounters.m_totalLinkedNotebooks =
        toCounter(totalLinkedNotebooks);

    syncChunksDataCounters.m_totalExpungedLinkedNotebooks =
        toCounter(totalExpungedLinkedNotebooks);

    syncChunksDataCounters.m_totalNotebooks = toCounter(totalNotebooks);
    syncChunksDataCounters.m_totalExpungedNotebooks =
        toCounter(totalExpungedNotebooks);
}

void Downloader::updateSyncState(const DownloadContext & downloadContext)
{
    // NOTE: this method is called before the finalization of downloading
    // process at which point it is known that
    // 1. All sync chunks were downloaded
    // 2. All notebooks, tags, saved searches and linked notebooks from them
    //    were processed successfully
    // 3. All notes were *attempted* to be downloaded and processed
    // 4. All resources (if any) were *attempted* to be downloaded and processed
    //
    // Processing of any note or resource might have failed. Furthermore,
    // processing of any note or resource might have been cancelled due to an
    // error. Nevertheless, the sync state is updated because during the next
    // sync all notes and resource which were not downloaded or processed
    // successfully (or which processing has been cancelled) would be attempted
    // to be processed again without regard to the sync state.

    const QMutexLocker locker{downloadContext.lastSyncStateMutex.get()};
    for (const auto & syncChunk: qAsConst(downloadContext.syncChunks)) {
        const auto chunkHighUsn = syncChunk.chunkHighUSN();
        if (Q_UNLIKELY(!chunkHighUsn)) {
            QNWARNING(
                "synchronization::Downloader",
                "Detected sync chunk without chunk high usn: "
                    << syncChunk
                    << "\nSomething is wrong, will not update the sync state");
            return;
        }

        if (downloadContext.linkedNotebook) {
            Q_ASSERT(downloadContext.linkedNotebook->guid());
            const auto & linkedNotebookGuid =
                *downloadContext.linkedNotebook->guid();

            auto & updateCounts =
                downloadContext.lastSyncState->m_linkedNotebookUpdateCounts;

            auto & lastSyncTimes =
                downloadContext.lastSyncState->m_linkedNotebookLastSyncTimes;

            const auto it =
                updateCounts.find(*downloadContext.linkedNotebook->guid());

            qint32 lastUpdateCount =
                (it == updateCounts.constEnd() ? 0 : it.value());
            if (lastUpdateCount < *chunkHighUsn) {
                if (it != updateCounts.constEnd()) {
                    it.value() = *chunkHighUsn;
                }
                else {
                    updateCounts[linkedNotebookGuid] = *chunkHighUsn;
                }

                lastSyncTimes[linkedNotebookGuid] = syncChunk.currentTime();
            }

            continue;
        }

        if (downloadContext.lastSyncState->m_userDataUpdateCount <
            *chunkHighUsn) {
            downloadContext.lastSyncState->m_userDataUpdateCount =
                *chunkHighUsn;

            downloadContext.lastSyncState->m_userDataLastSyncTime =
                syncChunk.currentTime();
        }
    }
}

void Downloader::downloadNotes(
    DownloadContextPtr downloadContext, const SynchronizationMode syncMode)
{
    Q_ASSERT(downloadContext);

    if (downloadContext->canceler->isCanceled()) {
        cancel(*downloadContext->promise);
        return;
    }

    Q_ASSERT(downloadContext->syncChunksDataCounters);

    const auto noteCount = countNotesInSyncChunks(downloadContext->syncChunks);
    auto notesProcessorCallback = std::make_shared<NotesProcessorCallback>(
        downloadContext->syncChunksDataCounters, downloadContext->callbackWeak,
        m_mutex, noteCount, downloadContext->linkedNotebook);

    auto notesFuture = m_notesProcessor->processNotes(
        downloadContext->syncChunks, downloadContext->canceler,
        notesProcessorCallback);

    auto promise = downloadContext->promise;
    threading::thenOrFailed(
        std::move(notesFuture), promise,
        threading::TrackedTask{
            weak_from_this(),
            [this, downloadContext = std::move(downloadContext), syncMode,
             notesProcessorCallback = std::move(notesProcessorCallback)](
                DownloadNotesStatusPtr notesStatus) mutable {
                downloadContext->downloadNotesStatus = std::move(notesStatus);
                downloadResources(std::move(downloadContext), syncMode);
            }});
}

void Downloader::downloadResources(
    DownloadContextPtr downloadContext, const SynchronizationMode syncMode)
{
    Q_ASSERT(downloadContext);

    if (downloadContext->canceler->isCanceled()) {
        cancel(*downloadContext->promise);
        return;
    }

    const auto resourceCount =
        countResourcesInSyncChunks(downloadContext->syncChunks);

    auto resourcesProcessorCallback =
        std::make_shared<ResourcesProcessorCallback>(
            downloadContext->syncChunksDataCounters,
            downloadContext->callbackWeak, m_mutex, resourceCount,
            downloadContext->linkedNotebook);

    auto resourcesFuture = m_resourcesProcessor->processResources(
        downloadContext->syncChunks, downloadContext->canceler,
        resourcesProcessorCallback);

    auto promise = downloadContext->promise;
    threading::thenOrFailed(
        std::move(resourcesFuture), promise,
        threading::TrackedTask{
            weak_from_this(),
            [this, downloadContext = std::move(downloadContext), syncMode,
             resourcesProcessorCallback =
                 std::move(resourcesProcessorCallback)](
                DownloadResourcesStatusPtr resourcesStatus) mutable {
                downloadContext->downloadResourcesStatus =
                    std::move(resourcesStatus);
                if (downloadContext->linkedNotebook) {
                    finalize(downloadContext);
                    return;
                }

                listLinkedNotebooksAndLaunchDataDownload(
                    std::move(downloadContext), syncMode);
            }});
}

void Downloader::finalize(DownloadContextPtr & downloadContext)
{
    Q_ASSERT(downloadContext);

    Downloader::updateSyncState(*downloadContext);
    downloadContext->promise->addResult(Result{
        LocalResult{
            std::move(downloadContext->syncChunksDataCounters),
            std::move(downloadContext->downloadNotesStatus),
            std::move(
                downloadContext->downloadResourcesStatus)}, // userOwnResult
        {},                                        // linkedNotebookResults
        std::move(downloadContext->lastSyncState), // syncState
    });
    downloadContext->promise->finish();
}

void Downloader::cancel(QPromise<IDownloader::Result> & promise)
{
    promise.setException(OperationCanceled{});
    promise.finish();
}

} // namespace quentier::synchronization
