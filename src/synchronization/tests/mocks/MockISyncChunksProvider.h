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

#include <synchronization/sync_chunks/ISyncChunksProvider.h>

#include <gmock/gmock.h>

namespace quentier::synchronization::tests::mocks {

class MockISyncChunksProvider : public ISyncChunksProvider
{
public:
    MOCK_METHOD(
        QFuture<QList<qevercloud::SyncChunk>>, fetchSyncChunks,
        (qint32 afterUsn, qevercloud::IRequestContextPtr ctx), (override));

    MOCK_METHOD(
        QFuture<QList<qevercloud::SyncChunk>>, fetchLinkedNotebookSyncChunks,
        (qevercloud::LinkedNotebook linkedNotebook, qint32 afterUsn,
         qevercloud::IRequestContextPtr ctx), (override));
};

} // namespace quentier::synchronization::tests::mocks
