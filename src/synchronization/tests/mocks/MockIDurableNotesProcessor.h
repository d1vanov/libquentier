/*
 * Copyright 2022-2024 Dmitry Ivanov
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

#include <synchronization/processors/IDurableNotesProcessor.h>

#include <gmock/gmock.h>

// clazy:excludeall=returning-void-expression

namespace quentier::synchronization::tests::mocks {

class MockIDurableNotesProcessor : public IDurableNotesProcessor
{
public:
    MOCK_METHOD(
        QFuture<DownloadNotesStatusPtr>, processNotes,
        (const QList<qevercloud::SyncChunk> & syncChunks,
         utility::cancelers::ICancelerPtr canceler,
         qevercloud::IRequestContextPtr ctx,
         const std::optional<qevercloud::Guid> & linkedNotebookGuid,
         ICallbackWeakPtr callbackWeak),
        (override));

    MOCK_METHOD(void, cleanup, (), (override));
};

} // namespace quentier::synchronization::tests::mocks
