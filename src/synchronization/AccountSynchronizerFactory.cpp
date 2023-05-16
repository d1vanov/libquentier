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

#include "AccountSynchronizerFactory.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/RuntimeError.h>

#include <synchronization/InkNoteImageDownloaderFactory.h>
#include <synchronization/LinkedNotebookFinder.h>
#include <synchronization/NoteStoreFactory.h>
#include <synchronization/NoteStoreProvider.h>
#include <synchronization/NoteThumbnailDownloaderFactory.h>
#include <synchronization/processors/DurableNotesProcessor.h>
#include <synchronization/processors/LinkedNotebooksProcessor.h>
#include <synchronization/processors/NoteFullDataDownloader.h>
#include <synchronization/processors/NotebooksProcessor.h>
#include <synchronization/processors/NotesProcessor.h>
#include <synchronization/sync_chunks/SyncChunksDownloader.h>
#include <synchronization/sync_chunks/SyncChunksProvider.h>
#include <synchronization/sync_chunks/SyncChunksStorage.h>

#include <qevercloud/DurableService.h>

#include <QFileInfo>

namespace quentier::synchronization {

AccountSynchronizerFactory::AccountSynchronizerFactory(
    ISyncStateStoragePtr syncStateStorage,
    IAuthenticationInfoProviderPtr authenticationInfoProvider,
    const QDir & synchronizationPersistenceDir) :
    m_syncStateStorage{std::move(syncStateStorage)},
    m_authenticationInfoProvider{std::move(authenticationInfoProvider)},
    m_synchronizationPersistenceDir{synchronizationPersistenceDir}
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

    if (!m_synchronizationPersistenceDir.exists()) {
        if (Q_UNLIKELY(!m_synchronizationPersistenceDir.mkpath(
                m_synchronizationPersistenceDir.absolutePath())))
        {
            throw RuntimeError{ErrorString{QStringLiteral(
                "Cannot create root dir for synchronization persistene")}};
        }
    }
    else {
        const QFileInfo rootDirInfo{m_synchronizationPersistenceDir.absolutePath()};

        if (Q_UNLIKELY(!rootDirInfo.isReadable())) {
            throw InvalidArgument{ErrorString{QStringLiteral(
                "AccountSynchronizerFactory requires readable synchronization "
                "persistence dir")}};
        }

        if (Q_UNLIKELY(!rootDirInfo.isWritable())) {
            throw InvalidArgument{ErrorString{QStringLiteral(
                "AccountSynchronizerFactory requires writable synchronization "
                "persistence dir")}};
        }
    }
}

IAccountSynchronizerPtr AccountSynchronizerFactory::createAccountSynchronizer(
    Account account,
    ISyncConflictResolverPtr syncConflictResolver,
    local_storage::ILocalStoragePtr localStorage,
    ISyncOptionsPtr options)
{
    if (Q_UNLIKELY(account.isEmpty())) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "AccountSynchronizerFactory: account is empty")}};
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

    auto noteStoreProvider = std::make_shared<NoteStoreProvider>(
        linkedNotebookFinder, m_authenticationInfoProvider,
        std::move(noteStoreFactory), account);

    auto syncChunksDownloader = std::make_shared<SyncChunksDownloader>(
        noteStoreProvider, qevercloud::newRetryPolicy());

    const QDir syncChunksDir{m_synchronizationPersistenceDir.absoluteFilePath(
        QStringLiteral("sync_chunks"))};

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
        std::move(syncChunksDownloader), std::move(syncChunksStorage));

    auto linkedNotebooksProcessor = std::make_shared<LinkedNotebooksProcessor>(
        localStorage);

    auto notebooksProcessor = std::make_shared<NotebooksProcessor>(
        localStorage, syncConflictResolver);

    auto inkNoteImageDownloaderFactory =
        std::make_shared<InkNoteImageDownloaderFactory>(
            account, m_authenticationInfoProvider, linkedNotebookFinder);

    auto noteThumbnailDownloaderFactory =
        std::make_shared<NoteThumbnailDownloaderFactory>(
            account, m_authenticationInfoProvider, linkedNotebookFinder);

    auto ctx = accountRequestContext(account);
    auto retryPolicy = accountRetryPolicy(account);

    auto noteFullDataDownloader = std::make_shared<NoteFullDataDownloader>(
        accountMaxInFlightNoteDownloads(account));

    auto notesProcessor = std::make_shared<NotesProcessor>(
        localStorage, syncConflictResolver, noteFullDataDownloader,
        noteStoreProvider, inkNoteImageDownloaderFactory,
        noteThumbnailDownloaderFactory, options, ctx, retryPolicy);

    auto durableNotesProcessor = std::make_shared<DurableNotesProcessor>(
        std::move(notesProcessor), m_synchronizationPersistenceDir);

    // TODO: implement further
    return nullptr;
}

qevercloud::IRequestContextPtr AccountSynchronizerFactory::accountRequestContext(
    const Account & account) const
{
    // TODO: implement
    Q_UNUSED(account)
    return qevercloud::newRequestContext();
}

qevercloud::IRetryPolicyPtr AccountSynchronizerFactory::accountRetryPolicy(
    const Account & account) const
{
    // TODO: implement
    Q_UNUSED(account)
    return qevercloud::newRetryPolicy();
}

qint32 AccountSynchronizerFactory::accountMaxInFlightNoteDownloads(
    const Account & account) const
{
    // TODO: implement
    Q_UNUSED(account)
    return 100;
}

} // namespace quentier::synchronization
