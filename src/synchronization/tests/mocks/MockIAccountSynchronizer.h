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

#pragma once

#include <synchronization/IAccountSynchronizer.h>

#include <gmock/gmock.h>

// clazy:excludeall=returning-void-expression

namespace quentier::synchronization::tests::mocks {

class MockIAccountSynchronizer : public IAccountSynchronizer
{
public:
    MOCK_METHOD(
        QFuture<ISyncResultPtr>, synchronize,
        (ICallbackWeakPtr callbackWeak,
         utility::cancelers::ICancelerPtr canceler),
        (override));
};

class MockIAccountSynchronizerCallback : public IAccountSynchronizer::ICallback
{
public: // IDownloader::ICallback
    MOCK_METHOD(
        void, onSyncChunksDownloadProgress,
        (qint32 highestDownloadedUsn, qint32 highestServerUsn,
         qint32 lastPreviousUsn), (override));

    MOCK_METHOD(void, onSyncChunksDownloaded, (), (override));

    MOCK_METHOD(
        void, onSyncChunksDataProcessingProgress,
        (SyncChunksDataCountersPtr counters), (override));

    MOCK_METHOD(
        void, onStartLinkedNotebooksDataDownloading,
        (const QList<qevercloud::LinkedNotebook> & linkedNotebooks),
        (override));

    MOCK_METHOD(
        void, onLinkedNotebookSyncChunksDownloadProgress,
        (qint32 highestDownloadedUsn, qint32 highestServerUsn,
         qint32 lastPreviousUsn,
         const qevercloud::LinkedNotebook & linkedNotebook), (override));

    MOCK_METHOD(
        void, onLinkedNotebookSyncChunksDownloaded,
        (const qevercloud::LinkedNotebook & linkedNotebook), (override));

    MOCK_METHOD(
        void, onLinkedNotebookSyncChunksDataProcessingProgress,
        (SyncChunksDataCountersPtr counters,
         const qevercloud::LinkedNotebook & linkedNotebook), (override));

    MOCK_METHOD(
        void, onNotesDownloadProgress,
        (quint32 notesDownloaded, quint32 totalNotesToDownload), (override));

    MOCK_METHOD(
        void, onLinkedNotebookNotesDownloadProgress,
        (quint32 notesDownloaded, quint32 totalNotesToDownload,
         const qevercloud::LinkedNotebook & linkedNotebook), (override));

    MOCK_METHOD(
        void, onResourcesDownloadProgress,
        (quint32 resourcesDownloaded, quint32 totalResourcesToDownload),
        (override));

    MOCK_METHOD(
        void, onLinkedNotebookResourcesDownloadProgress,
        (quint32 resourcesDownloaded, quint32 totalResourcesToDownload,
         const qevercloud::LinkedNotebook & linkedNotebook),
        (override));

public: // ISender::ICallback
    MOCK_METHOD(
        void, onUserOwnSendStatusUpdate, (SendStatusPtr sendStatus),
        (override));

    MOCK_METHOD(
        void, onLinkedNotebookSendStatusUpdate,
        (const qevercloud::Guid & linkedNotebookGuid,
         SendStatusPtr sendStatus),
        (override));
};

} // namespace quentier::synchronization::tests::mocks
