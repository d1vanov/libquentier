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

#include <synchronization/sync_chunks/SyncChunksDownloader.h>
#include <synchronization/tests/mocks/qevercloud/services/MockINoteStore.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/threading/Future.h>
#include <quentier/utility/UidGenerator.h>

#include <qevercloud/types/builders/NotebookBuilder.h>
#include <qevercloud/types/builders/SyncChunkBuilder.h>
#include <qevercloud/types/builders/SyncChunkFilterBuilder.h>

#include <gtest/gtest.h>

#include <memory>

// clazy:excludeall=returning-void-expression

namespace quentier::synchronization::tests {

using ::testing::Return;

class SyncChunksDownloaderTest : public ::testing::Test
{
protected:
    std::shared_ptr<mocks::qevercloud::MockINoteStore> m_mockNoteStore =
        std::make_shared<mocks::qevercloud::MockINoteStore>();
};

TEST_F(SyncChunksDownloaderTest, Ctor)
{
    EXPECT_NO_THROW(SyncChunksDownloader downloader(
        SynchronizationMode::Full, m_mockNoteStore));
}

TEST_F(SyncChunksDownloaderTest, CtorNullNoteStore)
{
    EXPECT_THROW(
        SyncChunksDownloader downloader(SynchronizationMode::Full, nullptr),
        InvalidArgument);
}

TEST_F(SyncChunksDownloaderTest, DownloadSingleUserOwnSyncChunkWithFullSync)
{
    SyncChunksDownloader downloader{SynchronizationMode::Full, m_mockNoteStore};

    const QString authToken = QStringLiteral("token");
    auto ctx = qevercloud::newRequestContext(authToken);

    const auto syncChunk =
        qevercloud::SyncChunkBuilder{}
            .setNotebooks(
                QList<qevercloud::Notebook>{}
                << qevercloud::NotebookBuilder{}
                       .setGuid(UidGenerator::Generate())
                       .setName(QStringLiteral("Notebook #1"))
                       .setUpdateSequenceNum(0)
                       .build()
                << qevercloud::NotebookBuilder{}
                       .setGuid(UidGenerator::Generate())
                       .setName(QStringLiteral("Notebook #2"))
                       .setUpdateSequenceNum(35)
                       .build())
            .setChunkHighUSN(35)
            .setUpdateCount(35)
            .build();

    const auto syncChunkFilter =
        qevercloud::SyncChunkFilterBuilder{}
            .setIncludeNotebooks(true)
            .setIncludeNotes(true)
            .setIncludeTags(true)
            .setIncludeSearches(true)
            .setIncludeNoteResources(true)
            .setIncludeNoteAttributes(true)
            .setIncludeNoteApplicationDataFullMap(true)
            .setIncludeNoteResourceApplicationDataFullMap(true)
            .setIncludeLinkedNotebooks(true)
            .build();

    constexpr qint32 afterUsn = 0;
    constexpr qint32 maxEntries = 50;
    EXPECT_CALL(
        *m_mockNoteStore,
        getFilteredSyncChunkAsync(afterUsn, maxEntries, syncChunkFilter, ctx))
        .WillOnce(Return(threading::makeReadyFuture(syncChunk)));

    const auto syncChunksFuture = downloader.downloadSyncChunks(afterUsn, ctx);
    ASSERT_TRUE(syncChunksFuture.isFinished());
    ASSERT_EQ(syncChunksFuture.resultCount(), 1);
    const auto syncChunksResult = syncChunksFuture.result();
    ASSERT_EQ(syncChunksResult.m_exception, nullptr);
    ASSERT_EQ(syncChunksResult.m_syncChunks.size(), 1);
    EXPECT_EQ(syncChunksResult.m_syncChunks[0], syncChunk);
}

} // namespace quentier::synchronization::tests
