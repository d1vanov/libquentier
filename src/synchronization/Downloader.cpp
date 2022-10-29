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

#include "Downloader.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/OperationCanceled.h>
#include <quentier/local_storage/ILocalStorage.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/synchronization/ISyncStateStorage.h>
#include <quentier/threading/Future.h>
#include <quentier/threading/TrackedTask.h>
#include <quentier/utility/cancelers/ICanceler.h>

#include <synchronization/IAccountLimitsProvider.h>
#include <synchronization/IAuthenticationInfoProvider.h>
#include <synchronization/IFullSyncStaleDataExpunger.h>
#include <synchronization/IProtocolVersionChecker.h>
#include <synchronization/IUserInfoProvider.h>
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
                std::move(linkedNotebook));
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

} // namespace

////////////////////////////////////////////////////////////////////////////////

class Downloader::LinkedNotebooksProcessorCallback final :
    public ILinkedNotebooksProcessor::ICallback
{
public:
    LinkedNotebooksProcessorCallback(
        SyncChunksDataCountersPtr syncChunksDataCounters,
        IDownloader::ICallbackWeakPtr callbackWeak,
        std::shared_ptr<QMutex> mutex,
        const Downloader::ContentSource contentSource) :
        m_syncChunksDataCounters{std::move(syncChunksDataCounters)},
        m_callbackWeak{std::move(callbackWeak)}, m_mutex{std::move(mutex)},
        m_contentSource{contentSource}
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

        switch (m_contentSource) {
        case Downloader::ContentSource::UserAccount:
            callback->onSyncChunksDataProcessingProgress(
                std::make_shared<SyncChunksDataCounters>(
                    *m_syncChunksDataCounters));
            break;
        case Downloader::ContentSource::LinkedNotebook:
            callback->onLinkedNotebookSyncChunksDataProcessingProgress(
                std::make_shared<SyncChunksDataCounters>(
                    *m_syncChunksDataCounters));
            break;
        }
    }

private:
    const SyncChunksDataCountersPtr m_syncChunksDataCounters;
    const IDownloader::ICallbackWeakPtr m_callbackWeak;
    const std::shared_ptr<QMutex> m_mutex;
    const Downloader::ContentSource m_contentSource;
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
        const Downloader::ContentSource contentSource) :
        m_syncChunksDataCounters{std::move(syncChunksDataCounters)},
        m_callbackWeak{std::move(callbackWeak)}, m_mutex{std::move(mutex)},
        m_contentSource{contentSource}
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

        switch (m_contentSource) {
        case Downloader::ContentSource::UserAccount:
            callback->onSyncChunksDataProcessingProgress(
                std::make_shared<SyncChunksDataCounters>(
                    *m_syncChunksDataCounters));
            break;
        case Downloader::ContentSource::LinkedNotebook:
            callback->onLinkedNotebookSyncChunksDataProcessingProgress(
                std::make_shared<SyncChunksDataCounters>(
                    *m_syncChunksDataCounters));
            break;
        }
    }

private:
    const SyncChunksDataCountersPtr m_syncChunksDataCounters;
    const IDownloader::ICallbackWeakPtr m_callbackWeak;
    const std::shared_ptr<QMutex> m_mutex;
    const Downloader::ContentSource m_contentSource;
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
        const Downloader::ContentSource contentSource) :
        m_syncChunksDataCounters{std::move(syncChunksDataCounters)},
        m_callbackWeak{std::move(callbackWeak)}, m_mutex{std::move(mutex)},
        m_totalNotesToDownload{totalNotesToDownload}, m_contentSource{
                                                          contentSource}
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

        switch (m_contentSource) {
        case Downloader::ContentSource::UserAccount:
            callback->onLinkedNotebooksNotesDownloadProgress(
                static_cast<quint32>(downloadedNotes),
                static_cast<quint32>(m_totalNotesToDownload));
            break;
        case Downloader::ContentSource::LinkedNotebook:
            callback->onNotesDownloadProgress(
                static_cast<quint32>(downloadedNotes),
                static_cast<quint32>(m_totalNotesToDownload));
            break;
        }
    }

private:
    const SyncChunksDataCountersPtr m_syncChunksDataCounters;
    const IDownloader::ICallbackWeakPtr m_callbackWeak;
    const std::shared_ptr<QMutex> m_mutex;
    const quint64 m_totalNotesToDownload;
    const Downloader::ContentSource m_contentSource;

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
        const Downloader::ContentSource contentSource) :
        m_syncChunksDataCounters{std::move(syncChunksDataCounters)},
        m_callbackWeak{std::move(callbackWeak)}, m_mutex{std::move(mutex)},
        m_totalResourcesToDownload{totalResourcesToDownload}, m_contentSource{
                                                                  contentSource}
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

        switch (m_contentSource) {
        case Downloader::ContentSource::UserAccount:
            callback->onLinkedNotebooksResourcesDownloadProgress(
                static_cast<quint32>(downloadedResources),
                static_cast<quint32>(m_totalResourcesToDownload));
            break;
        case Downloader::ContentSource::LinkedNotebook:
            callback->onResourcesDownloadProgress(
                static_cast<quint32>(downloadedResources),
                static_cast<quint32>(m_totalResourcesToDownload));
            break;
        }
    }

private:
    const SyncChunksDataCountersPtr m_syncChunksDataCounters;
    const IDownloader::ICallbackWeakPtr m_callbackWeak;
    const std::shared_ptr<QMutex> m_mutex;
    const quint64 m_totalResourcesToDownload;
    const Downloader::ContentSource m_contentSource;

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
        std::shared_ptr<QMutex> mutex,
        const Downloader::ContentSource contentSource) :
        m_syncChunksDataCounters{std::move(syncChunksDataCounters)},
        m_callbackWeak{std::move(callbackWeak)}, m_mutex{std::move(mutex)},
        m_contentSource{contentSource}
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

        switch (m_contentSource) {
        case Downloader::ContentSource::UserAccount:
            callback->onSyncChunksDataProcessingProgress(
                std::make_shared<SyncChunksDataCounters>(
                    *m_syncChunksDataCounters));
            break;
        case Downloader::ContentSource::LinkedNotebook:
            callback->onLinkedNotebookSyncChunksDataProcessingProgress(
                std::make_shared<SyncChunksDataCounters>(
                    *m_syncChunksDataCounters));
            break;
        }
    }

private:
    const SyncChunksDataCountersPtr m_syncChunksDataCounters;
    const IDownloader::ICallbackWeakPtr m_callbackWeak;
    const std::shared_ptr<QMutex> m_mutex;
    const Downloader::ContentSource m_contentSource;
};

////////////////////////////////////////////////////////////////////////////////

class Downloader::TagsProcessorCallback final : public ITagsProcessor::ICallback
{
public:
    TagsProcessorCallback(
        SyncChunksDataCountersPtr syncChunksDataCounters,
        IDownloader::ICallbackWeakPtr callbackWeak,
        std::shared_ptr<QMutex> mutex,
        const Downloader::ContentSource contentSource) :
        m_syncChunksDataCounters{std::move(syncChunksDataCounters)},
        m_callbackWeak{std::move(callbackWeak)}, m_mutex{std::move(mutex)},
        m_contentSource{contentSource}
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

        switch (m_contentSource) {
        case Downloader::ContentSource::UserAccount:
            callback->onSyncChunksDataProcessingProgress(
                std::make_shared<SyncChunksDataCounters>(
                    *m_syncChunksDataCounters));
            break;
        case Downloader::ContentSource::LinkedNotebook:
            callback->onLinkedNotebookSyncChunksDataProcessingProgress(
                std::make_shared<SyncChunksDataCounters>(
                    *m_syncChunksDataCounters));
            break;
        }
    }

private:
    const SyncChunksDataCountersPtr m_syncChunksDataCounters;
    const IDownloader::ICallbackWeakPtr m_callbackWeak;
    const std::shared_ptr<QMutex> m_mutex;
    const Downloader::ContentSource m_contentSource;
};

////////////////////////////////////////////////////////////////////////////////

Downloader::Downloader(
    Account account, IAuthenticationInfoProviderPtr authenticationInfoProvider,
    IProtocolVersionCheckerPtr protocolVersionChecker,
    IUserInfoProviderPtr userInfoProvider,
    IAccountLimitsProviderPtr accountLimitsProvider,
    ISyncStateStoragePtr syncStateStorage,
    ISyncChunksProviderPtr syncChunksProvider,
    ISyncChunksStoragePtr syncChunksStorage,
    ILinkedNotebooksProcessorPtr linkedNotebooksProcessor,
    INotebooksProcessorPtr notebooksProcessor,
    IDurableNotesProcessorPtr notesProcessor,
    IDurableResourcesProcessorPtr resourcesProcessor,
    ISavedSearchesProcessorPtr savedSearchesProcessor,
    ITagsProcessorPtr tagsProcessor,
    IFullSyncStaleDataExpungerPtr fullSyncStaleDataExpunger,
    qevercloud::IRequestContextPtr ctx, qevercloud::INoteStorePtr noteStore,
    local_storage::ILocalStoragePtr localStorage,
    const QDir & syncPersistentStorageDir) :
    m_account{std::move(account)},
    m_authenticationInfoProvider{std::move(authenticationInfoProvider)},
    m_protocolVersionChecker{std::move(protocolVersionChecker)},
    m_userInfoProvider{std::move(userInfoProvider)},
    m_accountLimitsProvider{std::move(accountLimitsProvider)},
    m_syncStateStorage{std::move(syncStateStorage)},
    m_syncChunksProvider{std::move(syncChunksProvider)},
    m_syncChunksStorage{std::move(syncChunksStorage)},
    m_linkedNotebooksProcessor{std::move(linkedNotebooksProcessor)},
    m_notebooksProcessor{std::move(notebooksProcessor)},
    // clang-format does some weird crap here, working around
    // clang-format off
    m_notesProcessor{std::move(notesProcessor)},
    m_resourcesProcessor{std::move(resourcesProcessor)},
    m_savedSearchesProcessor{std::move(savedSearchesProcessor)},
    m_tagsProcessor{std::move(tagsProcessor)},
    m_fullSyncStaleDataExpunger{std::move(fullSyncStaleDataExpunger)},
    m_ctx{std::move(ctx)}, m_noteStore{std::move(noteStore)},
    m_localStorage{std::move(localStorage)},
    m_syncPersistentStorageDir{syncPersistentStorageDir},
    m_mutex{std::shared_ptr<QMutex>()}
// clang-format on
{
    if (Q_UNLIKELY(m_account.isEmpty())) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::Downloader",
            "Downloader ctor: account is empty")}};
    }

    if (Q_UNLIKELY(m_account.type() != Account::Type::Evernote)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::Downloader",
            "Downloader ctor: account is not of Evernote type")}};
    }

    if (Q_UNLIKELY(!m_authenticationInfoProvider)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::Downloader",
            "Downloader ctor: authentication info provider is null")}};
    }

    if (Q_UNLIKELY(!m_protocolVersionChecker)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::Downloader",
            "Downloader ctor: protocol version checker is null")}};
    }

    if (Q_UNLIKELY(!m_userInfoProvider)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::Downloader",
            "Downloader ctor: user info provider is null")}};
    }

    if (Q_UNLIKELY(!m_accountLimitsProvider)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::Downloader",
            "Downloader ctor: account limits provider is null")}};
    }

    if (Q_UNLIKELY(!m_syncStateStorage)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::Downloader",
            "Downloader ctor: sync state storage is null")}};
    }

    if (Q_UNLIKELY(!m_syncChunksProvider)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::Downloader",
            "Downloader ctor: sync chunks provider is null")}};
    }

    if (Q_UNLIKELY(!m_syncChunksStorage)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::Downloader",
            "Downloader ctor: sync chunks storage is null")}};
    }

    if (Q_UNLIKELY(!m_linkedNotebooksProcessor)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::Downloader",
            "Downloader ctor: linked notebooks processor is null")}};
    }

    if (Q_UNLIKELY(!m_notebooksProcessor)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::Downloader",
            "Downloader ctor: notebooks processor is null")}};
    }

    if (Q_UNLIKELY(!m_notesProcessor)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::Downloader",
            "Downloader ctor: notes processor is null")}};
    }

    if (Q_UNLIKELY(!m_resourcesProcessor)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::Downloader",
            "Downloader ctor: resources processor is null")}};
    }

    if (Q_UNLIKELY(!m_savedSearchesProcessor)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::Downloader",
            "Downloader ctor: saved searches processor is null")}};
    }

    if (Q_UNLIKELY(!m_tagsProcessor)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::Downloader",
            "Downloader ctor: tags processor is null")}};
    }

    if (Q_UNLIKELY(!m_fullSyncStaleDataExpunger)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::Downloader",
            "Downloader ctor: full sync stale data expunger is null")}};
    }

    if (Q_UNLIKELY(!m_ctx)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::Downloader",
            "Downloader ctor: request context is null")}};
    }

    if (Q_UNLIKELY(!m_noteStore)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::Downloader",
            "Downloader ctor: note store is null")}};
    }

    if (Q_UNLIKELY(!m_localStorage)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::Downloader",
            "Downloader ctor: local storage is null")}};
    }
}

Downloader::~Downloader() = default;

QFuture<IDownloader::Result> Downloader::download(
    utility::cancelers::ICancelerPtr canceler, ICallbackWeakPtr callbackWeak)
{
    QNDEBUG("synchronization::Downloader", "Downloader::download");

    std::shared_ptr<QPromise<Result>> promise;
    {
        const QMutexLocker locker{m_mutex.get()};
        if (m_future) {
            QNDEBUG(
                "synchronization::Downloader",
                "Download is already in progress");
            return *m_future;
        }

        if (!m_lastSyncState) {
            readLastSyncState();
            Q_ASSERT(m_lastSyncState);
        }

        QNDEBUG(
            "synchronization::Downloader",
            "Last sync state: " << *m_lastSyncState);

        promise = std::make_shared<QPromise<Result>>();
        m_future = promise->future();
    }

    promise->start();

    if (Q_UNLIKELY(canceler->isCanceled())) {
        cancel(*promise);
        // NOTE: m_future is already reset inside cancel()
        return promise->future();
    }

    auto authenticationInfoFuture =
        m_authenticationInfoProvider->authenticateAccount(
            m_account, IAuthenticationInfoProvider::Mode::Cache);

    threading::bindCancellation(*m_future, authenticationInfoFuture);

    const auto selfWeak = weak_from_this();

    threading::thenOrFailed(
        std::move(authenticationInfoFuture), promise,
        threading::TrackedTask{
            selfWeak,
            [this, selfWeak, callbackWeak = std::move(callbackWeak),
             canceler = std::move(canceler),
             promise](IAuthenticationInfoPtr authenticationInfo) mutable {
                Q_ASSERT(authenticationInfo);

                if (canceler->isCanceled()) {
                    cancel(*promise);
                    return;
                }

                auto protocolVersionFuture =
                    m_protocolVersionChecker->checkProtocolVersion(
                        *authenticationInfo);

                threading::thenOrFailed(
                    std::move(protocolVersionFuture), promise,
                    threading::TrackedTask{
                        selfWeak,
                        [this, selfWeak, promise,
                         canceler = std::move(canceler),
                         callbackWeak = std::move(callbackWeak),
                         authenticationInfo =
                             std::move(authenticationInfo)]() mutable {
                            if (canceler->isCanceled()) {
                                cancel(*promise);
                                return;
                            }

                            auto downloadFuture = launchDownload(
                                *authenticationInfo, std::move(canceler),
                                std::move(callbackWeak));

                            threading::bindCancellation(
                                promise->future(), downloadFuture);

                            threading::mapFutureProgress(
                                downloadFuture, promise);

                            threading::thenOrFailed(
                                std::move(downloadFuture), promise,
                                [promise](Result result) {
                                    promise->addResult(std::move(result));
                                    promise->finish();
                                });
                        }});
            }});

    return *m_future;
}

void Downloader::readLastSyncState()
{
    const auto syncState = m_syncStateStorage->getSyncState(m_account);
    m_lastSyncState = SyncState{
        syncState->userDataUpdateCount(), syncState->userDataLastSyncTime(),
        syncState->linkedNotebookUpdateCounts(),
        syncState->linkedNotebookLastSyncTimes()};
}

QFuture<IDownloader::Result> Downloader::launchDownload(
    const IAuthenticationInfo & authenticationInfo,
    utility::cancelers::ICancelerPtr canceler,
    ICallbackWeakPtr callbackWeak) // NOLINT
{
    auto promise = std::make_shared<QPromise<Result>>();
    auto future = promise->future();

    promise->start();

    auto ctx = qevercloud::RequestContextBuilder{}
                   .setAuthenticationToken(authenticationInfo.authToken())
                   .setCookies(authenticationInfo.userStoreCookies())
                   .setRequestTimeout(m_ctx->requestTimeout())
                   .setIncreaseRequestTimeoutExponentially(
                       m_ctx->increaseRequestTimeoutExponentially())
                   .setMaxRequestTimeout(m_ctx->maxRequestTimeout())
                   .setMaxRetryCount(m_ctx->maxRequestRetryCount())
                   .build();

    auto userFuture = fetchUser(ctx);

    const auto selfWeak = weak_from_this();

    auto accountLimitsFuture = threading::then(
        std::move(userFuture),
        [selfWeak, ctx = ctx,
         canceler = canceler](qevercloud::User && user) mutable {
            if (const auto self = selfWeak.lock()) {
                if (canceler->isCanceled()) {
                    return threading::makeExceptionalFuture<
                        qevercloud::AccountLimits>(OperationCanceled{});
                }

                const qevercloud::ServiceLevel serviceLevel = [&] {
                    auto level = user.serviceLevel();
                    if (level) {
                        return *level;
                    }

                    QNWARNING(
                        "synchronization::Downloader",
                        "No service level set for user: " << user);
                    return qevercloud::ServiceLevel::BASIC;
                }();

                return self->fetchAccountLimits(serviceLevel, std::move(ctx));
            }

            return threading::makeExceptionalFuture<qevercloud::AccountLimits>(
                OperationCanceled{});
        });

    auto syncStateFuture = threading::then(
        std::move(accountLimitsFuture),
        [selfWeak,
         ctx = ctx](qevercloud::AccountLimits && accountLimits) mutable {
            Q_UNUSED(accountLimits)
            if (const auto self = selfWeak.lock()) {
                return self->m_noteStore->getSyncStateAsync(std::move(ctx));
            }

            return threading::makeExceptionalFuture<qevercloud::SyncState>(
                OperationCanceled{});
        });

    auto downloadContext = std::make_shared<DownloadContext>();
    downloadContext->promise = promise;
    downloadContext->ctx = std::move(ctx);
    downloadContext->canceler = std::move(canceler);
    downloadContext->callbackWeak = std::move(callbackWeak);

    threading::thenOrFailed(
        std::move(syncStateFuture), promise,
        [selfWeak, downloadContext = std::move(downloadContext)](
            qevercloud::SyncState && syncState) mutable {
            if (const auto self = selfWeak.lock()) {
                QNDEBUG(
                    "synchronization::Downloader",
                    "Sync state from Evernote: " << syncState);

                Q_ASSERT(self->m_lastSyncState);

                if (syncState.fullSyncBefore() >
                    self->m_lastSyncState->m_userDataLastSyncTime) {
                    QNDEBUG(
                        "synchronization::Downloader",
                        "Performing full synchronization instead of "
                        "incremental one");

                    self->launchUserOwnDataDownload(
                        std::move(downloadContext), SyncMode::Full);
                }
                else if (
                    syncState.updateCount() ==
                    self->m_lastSyncState->m_userDataUpdateCount)
                {
                    QNDEBUG(
                        "synchronization::Downloader",
                        "Evernote has no updates for user own data");

                    self->launchLinkedNotebooksDataDownload(
                        std::move(downloadContext));
                }
                else {
                    QNDEBUG(
                        "synchronization::Downloader",
                        "Launching incremental sync of user own data");

                    self->launchUserOwnDataDownload(
                        std::move(downloadContext), SyncMode::Incremental);
                }

                return;
            }

            downloadContext->promise->setException(OperationCanceled{});
            downloadContext->promise->finish();
        });

    return future;
}

void Downloader::launchUserOwnDataDownload(
    DownloadContextPtr downloadContext, const SyncMode syncMode)
{
    Q_ASSERT(downloadContext);
    Q_ASSERT(m_lastSyncState);

    const qint32 afterUsn =
        (syncMode == SyncMode::Full ? 0
                                    : m_lastSyncState->m_userDataUpdateCount);

    auto syncChunksProviderCallback =
        std::make_shared<SyncChunksProviderCallback>(
            downloadContext->callbackWeak);

    auto syncChunksFuture = m_syncChunksProvider->fetchSyncChunks(
        afterUsn, downloadContext->ctx, downloadContext->canceler,
        syncChunksProviderCallback);

    auto selfWeak = weak_from_this();
    auto promise = downloadContext->promise;

    threading::thenOrFailed(
        std::move(syncChunksFuture), promise,
        threading::TrackedTask{
            selfWeak,
            [this, downloadContext = std::move(downloadContext), syncMode,
             syncChunksProviderCallback = std::move(syncChunksProviderCallback)](
                QList<qevercloud::SyncChunk> syncChunks) mutable { // NOLINT
                if (const auto callback = downloadContext->callbackWeak.lock())
                {
                    callback->onSyncChunksDownloaded();
                }

                downloadContext->syncChunks = std::move(syncChunks);
                processSyncChunks(
                    std::move(downloadContext), syncMode,
                    ContentSource::UserAccount);
            }});
}

void Downloader::launchLinkedNotebooksDataDownload(
    DownloadContextPtr downloadContext) // NOLINT
{
    // TODO: implement
    Q_UNUSED(downloadContext)
}

QFuture<qevercloud::User> Downloader::fetchUser(
    qevercloud::IRequestContextPtr ctx)
{
    std::shared_ptr<QPromise<qevercloud::User>> promise;
    {
        const QMutexLocker locker{m_mutex.get()};
        if (m_userFuture) {
            return *m_userFuture;
        }

        promise = std::make_shared<QPromise<qevercloud::User>>();
        m_userFuture = promise->future();
    }

    promise->start();

    auto userFuture = m_userInfoProvider->userInfo(std::move(ctx));
    threading::thenOrFailed(
        std::move(userFuture), promise, [promise](qevercloud::User user) {
            promise->addResult(std::move(user));
            promise->finish();
        });

    return promise->future();
}

QFuture<qevercloud::AccountLimits> Downloader::fetchAccountLimits(
    const qevercloud::ServiceLevel serviceLevel,
    qevercloud::IRequestContextPtr ctx)
{
    std::shared_ptr<QPromise<qevercloud::AccountLimits>> promise;
    {
        const QMutexLocker locker{m_mutex.get()};
        if (m_accountLimitsFuture) {
            return *m_accountLimitsFuture;
        }

        promise = std::make_shared<QPromise<qevercloud::AccountLimits>>();
        m_accountLimitsFuture = promise->future();
    }

    promise->start();

    auto accountLimitsFuture =
        m_accountLimitsProvider->accountLimits(serviceLevel, std::move(ctx));

    threading::thenOrFailed(
        std::move(accountLimitsFuture), promise,
        [promise](qevercloud::AccountLimits accountLimits) {
            promise->addResult(std::move(accountLimits));
            promise->finish();
        });

    return promise->future();
}

void Downloader::processSyncChunks(
    DownloadContextPtr downloadContext, SyncMode syncMode,
    ContentSource contentSource, CheckForFirstSync checkForFirstSync)
{
    Q_ASSERT(downloadContext);

    if (downloadContext->canceler->isCanceled()) {
        cancel(*downloadContext->promise);
        return;
    }

    Q_ASSERT(m_lastSyncState);

    if (downloadContext->syncChunks.isEmpty()) {
        switch (contentSource) {
        case ContentSource::UserAccount:
        {
            QNINFO(
                "synchronization::Downloader",
                "No new data found in Evernote for user's own account");
            launchLinkedNotebooksDataDownload(std::move(downloadContext));
        } break;
        case ContentSource::LinkedNotebook:
            // TODO: implement
            break;
        }

        return;
    }

    const auto selfWeak = weak_from_this();

    if (checkForFirstSync == CheckForFirstSync::Yes) {
        const bool isFirstSync = (m_lastSyncState->userDataUpdateCount() == 0);
        if (!isFirstSync && (syncMode == SyncMode::Full)) {
            auto preservedGuids =
                collectPreservedGuids(downloadContext->syncChunks);

            auto future = m_fullSyncStaleDataExpunger->expungeStaleData(
                std::move(preservedGuids));

            auto promise = downloadContext->promise;
            threading::thenOrFailed(
                std::move(future), promise,
                threading::TrackedTask{
                    selfWeak,
                    [this, downloadContext = std::move(downloadContext),
                     syncMode, contentSource]() mutable {
                        processSyncChunks(
                            std::move(downloadContext), syncMode, contentSource,
                            CheckForFirstSync::No);
                    }});
            return;
        }
    }

    auto notebooksFuture =
        m_notebooksProcessor->processNotebooks(downloadContext->syncChunks, {});

    auto tagsFuture =
        m_tagsProcessor->processTags(downloadContext->syncChunks, {});

    auto savedSearchesFuture =
        (contentSource == ContentSource::UserAccount
             ? m_savedSearchesProcessor->processSavedSearches(
                   downloadContext->syncChunks, {})
             : threading::makeReadyFuture());

    auto linkedNotebooksFuture =
        (contentSource == ContentSource::UserAccount
             ? m_linkedNotebooksProcessor->processLinkedNotebooks(
                   downloadContext->syncChunks, {})
             : threading::makeReadyFuture());

    auto allFirstStageFuture = threading::whenAll(
        QList<QFuture<void>>{} << notebooksFuture << tagsFuture
                               << savedSearchesFuture << linkedNotebooksFuture);

    auto promise = downloadContext->promise;
    threading::thenOrFailed(
        std::move(allFirstStageFuture), promise,
        threading::TrackedTask{
            selfWeak,
            [this, downloadContext = std::move(downloadContext),
             contentSource]() mutable {
                downloadNotes(std::move(downloadContext), contentSource);
            }});
}

void Downloader::downloadNotes(
    DownloadContextPtr downloadContext, const ContentSource contentSource)
{
    Q_ASSERT(downloadContext);

    if (downloadContext->canceler->isCanceled()) {
        cancel(*downloadContext->promise);
        return;
    }

    auto notesFuture = m_notesProcessor->processNotes(
        downloadContext->syncChunks, downloadContext->canceler, {});

    auto promise = downloadContext->promise;
    const auto selfWeak = weak_from_this();
    threading::thenOrFailed(
        std::move(notesFuture), promise,
        threading::TrackedTask{
            selfWeak,
            [this, downloadContext = std::move(downloadContext),
             contentSource](DownloadNotesStatusPtr notesStatus) mutable {
                downloadContext->downloadNotesStatus = std::move(notesStatus);
                downloadResources(std::move(downloadContext), contentSource);
            }});
}

void Downloader::downloadResources(
    DownloadContextPtr downloadContext, ContentSource contentSource)
{
    Q_ASSERT(downloadContext);

    if (downloadContext->canceler->isCanceled()) {
        cancel(*downloadContext->promise);
        return;
    }

    auto resourcesFuture = m_resourcesProcessor->processResources(
        downloadContext->syncChunks, downloadContext->canceler, {});

    const auto selfWeak = weak_from_this();
    auto promise = downloadContext->promise;
    threading::thenOrFailed(
        std::move(resourcesFuture), promise,
        threading::TrackedTask{
            selfWeak,
            [this, downloadContext = std::move(downloadContext),
             contentSource]([[maybe_unused]] DownloadResourcesStatusPtr
                                resourcesStatus) { // NOLINT
                // TODO: either report sync finalization or start linked
                // notebooks sync
            }});
}

void Downloader::cancel(QPromise<IDownloader::Result> & promise)
{
    promise.setException(OperationCanceled{});
    promise.finish();

    const QMutexLocker locker{m_mutex.get()};
    m_future.reset();

    if (m_userFuture) {
        m_userFuture->cancel();
        m_userFuture.reset();
    }

    if (m_accountLimitsFuture) {
        m_accountLimitsFuture->cancel();
        m_accountLimitsFuture.reset();
    }
}

} // namespace quentier::synchronization
