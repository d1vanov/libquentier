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

#include "MockIAccountSyncPersistenceDirProvider.h"
#include "MockIAccountSynchronizer.h"
#include "MockIAccountSynchronizerFactory.h"
#include "MockIAuthenticationInfoProvider.h"
#include "MockIDownloader.h"
#include "MockIDurableNotesProcessor.h"
#include "MockIDurableResourcesProcessor.h"
#include "MockIFullSyncStaleDataExpunger.h"
#include "MockIInkNoteImageDownloaderFactory.h"
#include "MockILinkedNotebookFinder.h"
#include "MockILinkedNotebookTagsCleaner.h"
#include "MockILinkedNotebooksProcessor.h"
#include "MockINoteFullDataDownloader.h"
#include "MockINoteStoreProvider.h"
#include "MockINoteThumbnailDownloaderFactory.h"
#include "MockINotebookFinder.h"
#include "MockINotebooksProcessor.h"
#include "MockINotesProcessor.h"
#include "MockIProtocolVersionChecker.h"
#include "MockISavedSearchesProcessor.h"
#include "MockISender.h"
#include "MockISimpleNoteSyncConflictResolver.h"
#include "MockISimpleNotebookSyncConflictResolver.h"
#include "MockISimpleSavedSearchSyncConflictResolver.h"
#include "MockISimpleTagSyncConflictResolver.h"
#include "MockISyncChunksDownloader.h"
#include "MockISyncChunksProvider.h"
#include "MockISyncChunksStorage.h"
#include "MockITagsProcessor.h"
#include "MockIUserInfoProvider.h"
#include "qevercloud/MockIInkNoteImageDownloader.h"
#include "qevercloud/MockINoteThumbnailDownloader.h"
#include "qevercloud/services/MockINoteStore.h"
#include "qevercloud/services/MockIUserStore.h"
#include <quentier/synchronization/tests/mocks/MockIAuthenticator.h>
#include <quentier/synchronization/tests/mocks/MockINoteStoreFactory.h>
#include <quentier/synchronization/tests/mocks/MockISyncConflictResolver.h>
#include <quentier/synchronization/tests/mocks/MockISyncStateStorage.h>
