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

#include "AccountSynchronizerFactory.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/RuntimeError.h>
#include <quentier/synchronization/types/ISyncOptions.h>

#include <synchronization/AccountSynchronizer.h>
#include <synchronization/Downloader.h>
#include <synchronization/FullSyncStaleDataExpunger.h>
#include <synchronization/IAccountSyncPersistenceDirProvider.h>
#include <synchronization/InkNoteImageDownloaderFactory.h>
#include <synchronization/LinkedNotebookFinder.h>
#include <synchronization/LinkedNotebookTagsCleaner.h>
#include <synchronization/NoteStoreFactory.h>
#include <synchronization/NoteStoreProvider.h>
#include <synchronization/NoteThumbnailDownloaderFactory.h>
#include <synchronization/NotebookFinder.h>
#include <synchronization/Sender.h>
#include <synchronization/processors/DurableNotesProcessor.h>
#include <synchronization/processors/DurableResourcesProcessor.h>
#include <synchronization/processors/LinkedNotebooksProcessor.h>
#include <synchronization/processors/NoteFullDataDownloader.h>
#include <synchronization/processors/NotebooksProcessor.h>
#include <synchronization/processors/NotesProcessor.h>
#include <synchronization/processors/ResourceFullDataDownloader.h>
#include <synchronization/processors/ResourcesProcessor.h>
#include <synchronization/processors/SavedSearchesProcessor.h>
#include <synchronization/processors/TagsProcessor.h>
#include <synchronization/sync_chunks/SyncChunksDownloader.h>
#include <synchronization/sync_chunks/SyncChunksProvider.h>
#include <synchronization/sync_chunks/SyncChunksStorage.h>

#include <qevercloud/DurableService.h>

#include <QFileInfo>

namespace quentier::synchronization {

AccountSynchronizerFactory::AccountSynchronizerFactory(
    ISyncStateStoragePtr syncStateStorage,
    IAuthenticationInfoProviderPtr authenticationInfoProvider,
    IAccountSyncPersistenceDirProviderPtr accountSyncPersistenceDirProvider) :
    m_syncStateStorage{std::move(syncStateStorage)},
    m_authenticationInfoProvider{std::move(authenticationInfoProvider)},
    m_accountSyncPersistenceDirProvider{
        std::move(accountSyncPersistenceDirProvider)}
{
    if (Q_UNLIKELY(!m_syncStateStorage)) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "AccountSynchronizerFactory ctor: sync state storage is null")}};
    }

    if (Q_UNLIKELY(!m_authenticationInfoProvider)) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "AccountSynchronizerFactory ctor: authentication info provider "
            "is null")}};
    }

    if (Q_UNLIKELY(!m_accountSyncPersistenceDirProvider)) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "AccountSynchronizerFactory ctor: account sync persistence dir "
            "provider is null")}};
    }
}

IAccountSynchronizerPtr AccountSynchronizerFactory::createAccountSynchronizer(
    Account account, ISyncConflictResolverPtr syncConflictResolver,
    local_storage::ILocalStoragePtr localStorage, ISyncOptionsPtr options)
{
    if (Q_UNLIKELY(account.isEmpty())) {
        throw InvalidArgument{ErrorString{
            QStringLiteral("AccountSynchronizerFactory: account is empty")}};
    }

    if (Q_UNLIKELY(account.type() != Account::Type::Evernote)) {
        throw InvalidArgument{ErrorString{
            QStringLiteral("AccountSynchronizerFactory: account is not of "
                           "Evernote type")}};
    }

    if (Q_UNLIKELY(!syncConflictResolver)) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "AccountSynchronizerFactory: sync conflict resolver is null")}};
    }

    if (Q_UNLIKELY(!localStorage)) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "AccountSynchronizerFactory: local storage is null")}};
    }

    if (Q_UNLIKELY(!options)) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "AccountSynchronizerFactory: sync options are null")}};
    }

    auto noteStoreFactory = std::make_shared<NoteStoreFactory>();
    auto linkedNotebookFinder =
        std::make_shared<LinkedNotebookFinder>(localStorage);

    linkedNotebookFinder->init();

    auto notebookFinder = std::make_shared<NotebookFinder>(localStorage);
    notebookFinder->init();

    auto noteStoreProvider = std::make_shared<NoteStoreProvider>(
        linkedNotebookFinder, notebookFinder, m_authenticationInfoProvider,
        std::move(noteStoreFactory), account);

    auto syncChunksDownloader = std::make_shared<SyncChunksDownloader>(
        noteStoreProvider, qevercloud::newRetryPolicy());

    const QDir syncPersistenceDataDir =
        m_accountSyncPersistenceDirProvider->syncPersistenceDir(account);

    const QDir syncChunksDir{
        syncPersistenceDataDir.absoluteFilePath(QStringLiteral("sync_chunks"))};

    if (!syncChunksDir.exists()) {
        if (Q_UNLIKELY(!syncChunksDir.mkpath(syncChunksDir.absolutePath()))) {
            throw RuntimeError{ErrorString{QStringLiteral(
                "AccountSynchronizerFactory: cannot create dir for "
                "temporary sync chunks storage")}};
        }
    }
    else {
        const QFileInfo syncChunksDirInfo{syncChunksDir.absolutePath()};
        if (Q_UNLIKELY(!syncChunksDirInfo.isReadable())) {
            throw RuntimeError{ErrorString{QStringLiteral(
                "AccountSynchronizerFactory: dir for temporary sync chunks "
                "storage is not readable")}};
        }

        if (Q_UNLIKELY(!syncChunksDirInfo.isWritable())) {
            throw RuntimeError{ErrorString{QStringLiteral(
                "AccountSynchronizerFactory: dir for temporary sync chunks "
                "storage is not writable")}};
        }
    }

    auto syncChunksStorage = std::make_shared<SyncChunksStorage>(syncChunksDir);

    auto syncChunksProvider = std::make_shared<SyncChunksProvider>(
        std::move(syncChunksDownloader), syncChunksStorage);

    auto linkedNotebooksProcessor =
        std::make_shared<LinkedNotebooksProcessor>(localStorage);

    auto notebooksProcessor = std::make_shared<NotebooksProcessor>(
        localStorage, syncConflictResolver);

    auto inkNoteImageDownloaderFactory =
        std::make_shared<InkNoteImageDownloaderFactory>(
            account, m_authenticationInfoProvider, linkedNotebookFinder);

    auto noteThumbnailDownloaderFactory =
        std::make_shared<NoteThumbnailDownloaderFactory>(
            account, m_authenticationInfoProvider, linkedNotebookFinder);

    auto ctx = options->requestContext();
    auto retryPolicy = options->retryPolicy();

    constexpr quint32 defaultMaxConcurrentNoteDownloads = 100;
    auto noteFullDataDownloader = std::make_shared<NoteFullDataDownloader>(
        options->maxConcurrentNoteDownloads().value_or(
            defaultMaxConcurrentNoteDownloads));

    auto notesProcessor = std::make_shared<NotesProcessor>(
        localStorage, syncConflictResolver, noteFullDataDownloader,
        noteStoreProvider, inkNoteImageDownloaderFactory,
        noteThumbnailDownloaderFactory, options, retryPolicy);

    auto durableNotesProcessor = std::make_shared<DurableNotesProcessor>(
        std::move(notesProcessor), syncPersistenceDataDir);

    constexpr quint32 defaultMaxConcurrentResourceDownloads = 100;
    auto resourceFullDataDownloader =
        std::make_shared<ResourceFullDataDownloader>(
            options->maxConcurrentResourceDownloads().value_or(
                defaultMaxConcurrentResourceDownloads));

    auto resourcesProcessor = std::make_shared<ResourcesProcessor>(
        localStorage, std::move(resourceFullDataDownloader), noteStoreProvider,
        retryPolicy);

    auto durableResourcesProcessor =
        std::make_shared<DurableResourcesProcessor>(
            std::move(resourcesProcessor), syncPersistenceDataDir);

    auto savedSearchesProcessor = std::make_shared<SavedSearchesProcessor>(
        localStorage, syncConflictResolver);

    auto tagsProcessor =
        std::make_shared<TagsProcessor>(localStorage, syncConflictResolver);

    auto fullSyncStaleDataExpunger =
        std::make_shared<FullSyncStaleDataExpunger>(localStorage);

    auto linkedNotebookTagsCleaner =
        std::make_shared<LinkedNotebookTagsCleaner>(localStorage);

    auto downloader = std::make_shared<Downloader>(
        account, m_authenticationInfoProvider, m_syncStateStorage,
        std::move(syncChunksProvider), syncChunksStorage,
        std::move(linkedNotebooksProcessor), std::move(notebooksProcessor),
        std::move(durableNotesProcessor), std::move(durableResourcesProcessor),
        std::move(savedSearchesProcessor), std::move(tagsProcessor),
        std::move(fullSyncStaleDataExpunger), noteStoreProvider,
        std::move(linkedNotebookTagsCleaner), localStorage, ctx, retryPolicy);

    auto sender = std::make_shared<Sender>(
        account, std::move(localStorage), m_syncStateStorage,
        std::move(noteStoreProvider), ctx, retryPolicy);

    return std::make_shared<AccountSynchronizer>(
        std::move(account), std::move(downloader), std::move(sender),
        m_authenticationInfoProvider, m_syncStateStorage,
        std::move(syncChunksStorage));
}

} // namespace quentier::synchronization
