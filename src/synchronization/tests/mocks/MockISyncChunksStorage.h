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

#pragma once

#include <synchronization/sync_chunks/ISyncChunksStorage.h>

#include <gmock/gmock.h>

// clazy:excludeall=returning-void-expression

namespace quentier::synchronization::tests::mocks {

class MockISyncChunksStorage : public ISyncChunksStorage
{
public:
    MOCK_METHOD(
        (QList<std::pair<qint32, qint32>>),
        fetchUserOwnSyncChunksLowAndHighUsns, (), (const, override));

    MOCK_METHOD(
        (QList<std::pair<qint32, qint32>>),
        fetchLinkedNotebookSyncChunksLowAndHighUsns,
        (const qevercloud::Guid & linkedNotebookGuid), (const, override));

    MOCK_METHOD(
        QList<qevercloud::SyncChunk>, fetchRelevantUserOwnSyncChunks,
        (qint32 afterUsn), (const, override));

    MOCK_METHOD(
        QList<qevercloud::SyncChunk>, fetchRelevantLinkedNotebookSyncChunks,
        (const qevercloud::Guid & linkedNotebookGuid, qint32 afterUsn),
        (const, override));

    MOCK_METHOD(
        void, putUserOwnSyncChunks,
        (const QList<qevercloud::SyncChunk> & syncChunks), (override));

    MOCK_METHOD(
        void, putLinkedNotebookSyncChunks,
        (const qevercloud::Guid & linkedNotebookGuid,
         const QList<qevercloud::SyncChunk> & syncChunks), (override));

    MOCK_METHOD(void, clearUserOwnSyncChunks, (), (override));

    MOCK_METHOD(
        void, clearLinkedNotebookSyncChunks,
        (const qevercloud::Guid & linkedNotebookGuid), (override));

    MOCK_METHOD(void, clearAllSyncChunks, (), (override));
};

} // namespace quentier::synchronization::tests::mocks
