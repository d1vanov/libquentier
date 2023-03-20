/*
 * Copyright 2021-2023 Dmitry Ivanov
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

#pragma once

#include <memory>

namespace quentier::synchronization {

////////////////////////////////////////////////////////////////////////////////

class IAccountLimitsProvider;
using IAccountLimitsProviderPtr = std::shared_ptr<IAccountLimitsProvider>;

class IAccountSynchronizer;
using IAccountSynchronizerPtr = std::shared_ptr<IAccountSynchronizer>;

class IAuthenticationInfoProvider;
using IAuthenticationInfoProviderPtr =
    std::shared_ptr<IAuthenticationInfoProvider>;

class IDurableNotesProcessor;
using IDurableNotesProcessorPtr = std::shared_ptr<IDurableNotesProcessor>;

class IDurableResourcesProcessor;
using IDurableResourcesProcessorPtr =
    std::shared_ptr<IDurableResourcesProcessor>;

class IFullSyncStaleDataExpunger;
using IFullSyncStaleDataExpungerPtr =
    std::shared_ptr<IFullSyncStaleDataExpunger>;

class IInkNoteImageDownloaderFactory;
using IInkNoteImageDownloaderFactoryPtr =
    std::shared_ptr<IInkNoteImageDownloaderFactory>;

class ILinkedNotebookFinder;
using ILinkedNotebookFinderPtr = std::shared_ptr<ILinkedNotebookFinder>;

class INoteFullDataDownloader;
using INoteFullDataDownloaderPtr = std::shared_ptr<INoteFullDataDownloader>;

class INoteStoreFactory;
using INoteStoreFactoryPtr = std::shared_ptr<INoteStoreFactory>;

class INoteStoreProvider;
using INoteStoreProviderPtr = std::shared_ptr<INoteStoreProvider>;

class INoteThumbnailDownloaderFactory;
using INoteThumbnailDownloaderFactoryPtr =
    std::shared_ptr<INoteThumbnailDownloaderFactory>;

class IProtocolVersionChecker;
using IProtocolVersionCheckerPtr = std::shared_ptr<IProtocolVersionChecker>;

class IResourceFullDataDownloader;
using IResourceFullDataDownloaderPtr = std::shared_ptr<IResourceFullDataDownloader>;

class IUserInfoProvider;
using IUserInfoProviderPtr = std::shared_ptr<IUserInfoProvider>;

////////////////////////////////////////////////////////////////////////////////

class ISimpleNotebookSyncConflictResolver;

using ISimpleNotebookSyncConflictResolverPtr =
    std::shared_ptr<ISimpleNotebookSyncConflictResolver>;

class ISimpleNoteSyncConflictResolver;

using ISimpleNoteSyncConflictResolverPtr =
    std::shared_ptr<ISimpleNoteSyncConflictResolver>;

class ISimpleSavedSearchSyncConflictResolver;

using ISimpleSavedSearchSyncConflictResolverPtr =
    std::shared_ptr<ISimpleSavedSearchSyncConflictResolver>;

class ISimpleTagSyncConflictResolver;

using ISimpleTagSyncConflictResolverPtr =
    std::shared_ptr<ISimpleTagSyncConflictResolver>;

////////////////////////////////////////////////////////////////////////////////

class ISyncChunksDownloader;
using ISyncChunksDownloaderPtr = std::shared_ptr<ISyncChunksDownloader>;

class ISyncChunksProvider;
using ISyncChunksProviderPtr = std::shared_ptr<ISyncChunksProvider>;

class ISyncChunksStorage;
using ISyncChunksStoragePtr = std::shared_ptr<ISyncChunksStorage>;

////////////////////////////////////////////////////////////////////////////////

class ILinkedNotebooksProcessor;
using ILinkedNotebooksProcessorPtr = std::shared_ptr<ILinkedNotebooksProcessor>;

class INotebooksProcessor;
using INotebooksProcessorPtr = std::shared_ptr<INotebooksProcessor>;

class INotesProcessor;
using INotesProcessorPtr = std::shared_ptr<INotesProcessor>;

class IResourcesProcessor;
using IResourcesProcessorPtr = std::shared_ptr<IResourcesProcessor>;

class ISavedSearchesProcessor;
using ISavedSearchesProcessorPtr = std::shared_ptr<ISavedSearchesProcessor>;

class ITagsProcessor;
using ITagsProcessorPtr = std::shared_ptr<ITagsProcessor>;

////////////////////////////////////////////////////////////////////////////////

struct SendStatus;
using SendStatusPtr = std::shared_ptr<SendStatus>;

class SyncEventsNotifier;

} // namespace quentier::synchronization

namespace quentier {

struct SyncChunksDataCounters;
using SyncChunksDataCountersPtr = std::shared_ptr<SyncChunksDataCounters>;

} // namespace quentier
