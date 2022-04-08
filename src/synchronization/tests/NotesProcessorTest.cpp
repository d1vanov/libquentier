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

#include <synchronization/processors/NotesProcessor.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/local_storage/tests/mocks/MockILocalStorage.h>
#include <quentier/synchronization/tests/mocks/MockISyncConflictResolver.h>

// NOTE: strange but with these headers moved higher, above "quentier/" ones,
// build fails. Probably something related to #pragma once not being perfectly
// implemented in the compiler.
#include <synchronization/tests/mocks/MockINoteFullDataDownloader.h>
#include <synchronization/tests/mocks/qevercloud/services/MockINoteStore.h>

#include <qevercloud/types/builders/SyncChunkBuilder.h>

#include <gtest/gtest.h>

// clazy:excludeall=non-pod-global-static
// clazy:excludeall=returning-void-expression

namespace quentier::synchronization::tests {

using testing::StrictMock;

class NotesProcessorTest : public testing::Test
{
protected:
    const std::shared_ptr<local_storage::tests::mocks::MockILocalStorage>
        m_mockLocalStorage = std::make_shared<
            StrictMock<local_storage::tests::mocks::MockILocalStorage>>();

    const std::shared_ptr<mocks::MockISyncConflictResolver>
        m_mockSyncConflictResolver =
            std::make_shared<StrictMock<mocks::MockISyncConflictResolver>>();

    const std::shared_ptr<mocks::MockINoteFullDataDownloader>
        m_mockNoteFullDataDownloader =
            std::make_shared<StrictMock<mocks::MockINoteFullDataDownloader>>();

    const std::shared_ptr<mocks::qevercloud::MockINoteStore> m_mockNoteStore =
        std::make_shared<StrictMock<mocks::qevercloud::MockINoteStore>>();
};

TEST_F(NotesProcessorTest, Ctor)
{
    EXPECT_NO_THROW(
        const auto notesProcessor = std::make_shared<NotesProcessor>(
            m_mockLocalStorage, m_mockSyncConflictResolver,
            m_mockNoteFullDataDownloader, m_mockNoteStore));
}

TEST_F(NotesProcessorTest, CtorNullLocalStorage)
{
    EXPECT_THROW(
        const auto notesProcessor = std::make_shared<NotesProcessor>(
            nullptr, m_mockSyncConflictResolver,
            m_mockNoteFullDataDownloader, m_mockNoteStore),
        InvalidArgument);
}

TEST_F(NotesProcessorTest, CtorNullSyncConflictResolver)
{
    EXPECT_THROW(
        const auto notesProcessor = std::make_shared<NotesProcessor>(
            m_mockLocalStorage, nullptr, m_mockNoteFullDataDownloader,
            m_mockNoteStore),
        InvalidArgument);
}

TEST_F(NotesProcessorTest, CtorNullNoteFullDataDownloader)
{
    EXPECT_THROW(
        const auto notesProcessor = std::make_shared<NotesProcessor>(
            m_mockLocalStorage, m_mockSyncConflictResolver, nullptr,
            m_mockNoteStore),
        InvalidArgument);
}

TEST_F(NotesProcessorTest, CtorNullNoteStore)
{
    EXPECT_THROW(
        const auto notesProcessor = std::make_shared<NotesProcessor>(
            m_mockLocalStorage, m_mockSyncConflictResolver,
            m_mockNoteFullDataDownloader, nullptr),
        InvalidArgument);
}

TEST_F(NotesProcessorTest, ProcessSyncChunksWithoutNotesToProcess)
{
    const auto syncChunks = QList<qevercloud::SyncChunk>{}
        << qevercloud::SyncChunkBuilder{}.build();

    const auto notesProcessor = std::make_shared<NotesProcessor>(
        m_mockLocalStorage, m_mockSyncConflictResolver,
        m_mockNoteFullDataDownloader, m_mockNoteStore);

    auto future = notesProcessor->processNotes(syncChunks);
    ASSERT_TRUE(future.isFinished());
    EXPECT_NO_THROW(future.waitForFinished());
}

} // namespace quentier::synchronization::tests
