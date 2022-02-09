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

#include <synchronization/sync_chunks/SyncChunksProvider.h>
#include <synchronization/tests/mocks/MockISyncChunksDownloader.h>
#include <synchronization/tests/mocks/MockISyncChunksStorage.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/threading/Future.h>
#include <quentier/utility/UidGenerator.h>

#include <qevercloud/RequestContext.h>
#include <qevercloud/types/builders/NotebookBuilder.h>
#include <qevercloud/types/builders/SyncChunkBuilder.h>

#include <gtest/gtest.h>

#include <memory>

// clazy:excludeall=returning-void-expression

namespace quentier::synchronization::tests {

using testing::_;
using testing::InSequence;
using testing::Return;
using testing::StrictMock;

class SyncChunksProviderTest : public testing::Test
{
protected:
    std::shared_ptr<mocks::MockISyncChunksDownloader>
        m_mockSyncChunksDownloader =
            std::make_shared<StrictMock<mocks::MockISyncChunksDownloader>>();

    std::shared_ptr<mocks::MockISyncChunksStorage> m_mockSyncChunksStorage =
        std::make_shared<StrictMock<mocks::MockISyncChunksStorage>>();
};

TEST_F(SyncChunksProviderTest, Ctor)
{
    EXPECT_NO_THROW((SyncChunksProvider{
        m_mockSyncChunksDownloader, m_mockSyncChunksStorage}));
}

TEST_F(SyncChunksProviderTest, CtorNullSyncChunksDownloader)
{
    EXPECT_THROW(
        (SyncChunksProvider{nullptr, m_mockSyncChunksStorage}),
        InvalidArgument);
}

TEST_F(SyncChunksProviderTest, CtorNullSyncChunksStorage)
{
    EXPECT_THROW(
        (SyncChunksProvider{m_mockSyncChunksDownloader, nullptr}),
        InvalidArgument);
}

TEST_F(SyncChunksProviderTest, FetchUserOwnSyncChunksFromStorage)
{
    SyncChunksProvider provider{
        m_mockSyncChunksDownloader, m_mockSyncChunksStorage};

    const QList<std::pair<qint32, qint32>> usnsRange =
        QList<std::pair<qint32, qint32>>{} << std::make_pair<qint32>(0, 35)
                                           << std::make_pair<qint32>(36, 54)
                                           << std::make_pair(55, 82);

    InSequence s;

    EXPECT_CALL(*m_mockSyncChunksStorage, fetchUserOwnSyncChunksLowAndHighUsns)
        .WillOnce(Return(usnsRange));

    const QList<qevercloud::SyncChunk> syncChunks =
        QList<qevercloud::SyncChunk>{}
        << qevercloud::SyncChunkBuilder{}
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
               .build()
        << qevercloud::SyncChunkBuilder{}
               .setNotebooks(
                   QList<qevercloud::Notebook>{}
                   << qevercloud::NotebookBuilder{}
                          .setGuid(UidGenerator::Generate())
                          .setName(QStringLiteral("Notebook #3"))
                          .setUpdateSequenceNum(36)
                          .build()
                   << qevercloud::NotebookBuilder{}
                          .setGuid(UidGenerator::Generate())
                          .setName(QStringLiteral("Notebook #4"))
                          .setUpdateSequenceNum(54)
                          .build())
               .setChunkHighUSN(54)
               .build()
        << qevercloud::SyncChunkBuilder{}
               .setNotebooks(
                   QList<qevercloud::Notebook>{}
                   << qevercloud::NotebookBuilder{}
                          .setGuid(UidGenerator::Generate())
                          .setName(QStringLiteral("Notebook #5"))
                          .setUpdateSequenceNum(55)
                          .build()
                   << qevercloud::NotebookBuilder{}
                          .setGuid(UidGenerator::Generate())
                          .setName(QStringLiteral("Notebook #6"))
                          .setUpdateSequenceNum(82)
                          .build())
               .setChunkHighUSN(82)
               .build();

    EXPECT_CALL(*m_mockSyncChunksStorage, fetchRelevantUserOwnSyncChunks(0))
        .WillOnce(Return(syncChunks));

    EXPECT_CALL(*m_mockSyncChunksDownloader, downloadSyncChunks(82, _))
        .WillOnce(Return(
            threading::makeReadyFuture<QList<qevercloud::SyncChunk>>({})));

    auto future = provider.fetchSyncChunks(0, qevercloud::newRequestContext());
    ASSERT_TRUE(future.isFinished());
    ASSERT_TRUE(future.resultCount());
    EXPECT_EQ(future.result(), syncChunks);
}

TEST_F(SyncChunksProviderTest, FetchPartOfUserOwnSyncChunksFromStorage)
{
    SyncChunksProvider provider{
        m_mockSyncChunksDownloader, m_mockSyncChunksStorage};

    const QList<std::pair<qint32, qint32>> usnsRange =
        QList<std::pair<qint32, qint32>>{} << std::make_pair<qint32>(0, 35)
                                           << std::make_pair<qint32>(36, 54);

    InSequence s;

    EXPECT_CALL(*m_mockSyncChunksStorage, fetchUserOwnSyncChunksLowAndHighUsns)
        .WillOnce(Return(usnsRange));

    const QList<qevercloud::SyncChunk> syncChunks =
        QList<qevercloud::SyncChunk>{}
        << qevercloud::SyncChunkBuilder{}
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
               .build()
        << qevercloud::SyncChunkBuilder{}
               .setNotebooks(
                   QList<qevercloud::Notebook>{}
                   << qevercloud::NotebookBuilder{}
                          .setGuid(UidGenerator::Generate())
                          .setName(QStringLiteral("Notebook #3"))
                          .setUpdateSequenceNum(36)
                          .build()
                   << qevercloud::NotebookBuilder{}
                          .setGuid(UidGenerator::Generate())
                          .setName(QStringLiteral("Notebook #4"))
                          .setUpdateSequenceNum(54)
                          .build())
               .setChunkHighUSN(54)
               .build();

    EXPECT_CALL(*m_mockSyncChunksStorage, fetchRelevantUserOwnSyncChunks(0))
        .WillOnce(Return(syncChunks));

    auto downloadedSyncChunks = QList<qevercloud::SyncChunk>{}
        << qevercloud::SyncChunkBuilder{}
               .setNotebooks(
                   QList<qevercloud::Notebook>{}
                   << qevercloud::NotebookBuilder{}
                          .setGuid(UidGenerator::Generate())
                          .setName(QStringLiteral("Notebook #5"))
                          .setUpdateSequenceNum(55)
                          .build()
                   << qevercloud::NotebookBuilder{}
                          .setGuid(UidGenerator::Generate())
                          .setName(QStringLiteral("Notebook #6"))
                          .setUpdateSequenceNum(82)
                          .build())
               .setChunkHighUSN(82)
               .build();

    auto fullSyncChunks = syncChunks;
    fullSyncChunks << downloadedSyncChunks;

    EXPECT_CALL(*m_mockSyncChunksDownloader, downloadSyncChunks(54, _))
        .WillOnce(
            [downloadedSyncChunks](
                qint32 afterUsn,
                qevercloud::IRequestContextPtr ctx) mutable // NOLINT
            {
                Q_UNUSED(afterUsn)
                Q_UNUSED(ctx)

                return threading::makeReadyFuture<QList<qevercloud::SyncChunk>>(
                    std::move(downloadedSyncChunks));
            });

    auto future = provider.fetchSyncChunks(0, qevercloud::newRequestContext());
    ASSERT_TRUE(future.isFinished());
    ASSERT_TRUE(future.resultCount());
    EXPECT_EQ(future.result(), fullSyncChunks);
}

TEST_F(
    SyncChunksProviderTest, DownloadUserOwnSyncChunksWhenThereAreNoneInStorage)
{
    SyncChunksProvider provider{
        m_mockSyncChunksDownloader, m_mockSyncChunksStorage};

    InSequence s;

    EXPECT_CALL(*m_mockSyncChunksStorage, fetchUserOwnSyncChunksLowAndHighUsns)
        .WillOnce(Return(QList<std::pair<qint32, qint32>>{}));

    const QList<qevercloud::SyncChunk> syncChunks =
        QList<qevercloud::SyncChunk>{}
        << qevercloud::SyncChunkBuilder{}
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
               .build()
        << qevercloud::SyncChunkBuilder{}
               .setNotebooks(
                   QList<qevercloud::Notebook>{}
                   << qevercloud::NotebookBuilder{}
                          .setGuid(UidGenerator::Generate())
                          .setName(QStringLiteral("Notebook #3"))
                          .setUpdateSequenceNum(36)
                          .build()
                   << qevercloud::NotebookBuilder{}
                          .setGuid(UidGenerator::Generate())
                          .setName(QStringLiteral("Notebook #4"))
                          .setUpdateSequenceNum(54)
                          .build())
               .setChunkHighUSN(54)
               .build()
        << qevercloud::SyncChunkBuilder{}
               .setNotebooks(
                   QList<qevercloud::Notebook>{}
                   << qevercloud::NotebookBuilder{}
                          .setGuid(UidGenerator::Generate())
                          .setName(QStringLiteral("Notebook #5"))
                          .setUpdateSequenceNum(55)
                          .build()
                   << qevercloud::NotebookBuilder{}
                          .setGuid(UidGenerator::Generate())
                          .setName(QStringLiteral("Notebook #6"))
                          .setUpdateSequenceNum(82)
                          .build())
               .setChunkHighUSN(82)
               .build();

    EXPECT_CALL(*m_mockSyncChunksDownloader, downloadSyncChunks(0, _))
        .WillOnce(
            Return(threading::makeReadyFuture<QList<qevercloud::SyncChunk>>(
                QList{syncChunks})));

    auto future = provider.fetchSyncChunks(0, qevercloud::newRequestContext());
    ASSERT_TRUE(future.isFinished());
    ASSERT_TRUE(future.resultCount());
    EXPECT_EQ(future.result(), syncChunks);
}

TEST_F(
    SyncChunksProviderTest,
    DownloadUserOwnSyncChunksWhenStorageGivesIncompleteSyncChunks)
{
    SyncChunksProvider provider{
        m_mockSyncChunksDownloader, m_mockSyncChunksStorage};

    const QList<std::pair<qint32, qint32>> usnsRange =
        QList<std::pair<qint32, qint32>>{} << std::make_pair<qint32>(0, 35)
                                           << std::make_pair<qint32>(36, 54)
                                           << std::make_pair(55, 82);

    InSequence s;

    EXPECT_CALL(*m_mockSyncChunksStorage, fetchUserOwnSyncChunksLowAndHighUsns)
        .WillOnce(Return(usnsRange));

    const QList<qevercloud::SyncChunk> syncChunks =
        QList<qevercloud::SyncChunk>{}
        << qevercloud::SyncChunkBuilder{}
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
               .build()
        << qevercloud::SyncChunkBuilder{}
               .setNotebooks(
                   QList<qevercloud::Notebook>{}
                   << qevercloud::NotebookBuilder{}
                          .setGuid(UidGenerator::Generate())
                          .setName(QStringLiteral("Notebook #3"))
                          .setUpdateSequenceNum(36)
                          .build()
                   << qevercloud::NotebookBuilder{}
                          .setGuid(UidGenerator::Generate())
                          .setName(QStringLiteral("Notebook #4"))
                          .setUpdateSequenceNum(54)
                          .build())
               .setChunkHighUSN(54)
               .build();

    EXPECT_CALL(*m_mockSyncChunksStorage, fetchRelevantUserOwnSyncChunks(0))
        .WillOnce(Return(syncChunks));

    auto downloadedSyncChunks = QList<qevercloud::SyncChunk>{}
        << qevercloud::SyncChunkBuilder{}
               .setNotebooks(
                   QList<qevercloud::Notebook>{}
                   << qevercloud::NotebookBuilder{}
                          .setGuid(UidGenerator::Generate())
                          .setName(QStringLiteral("Notebook #5"))
                          .setUpdateSequenceNum(55)
                          .build()
                   << qevercloud::NotebookBuilder{}
                          .setGuid(UidGenerator::Generate())
                          .setName(QStringLiteral("Notebook #6"))
                          .setUpdateSequenceNum(82)
                          .build())
               .setChunkHighUSN(82)
               .build();

    auto fullSyncChunks = syncChunks;
    fullSyncChunks << downloadedSyncChunks;

    EXPECT_CALL(*m_mockSyncChunksDownloader, downloadSyncChunks(54, _))
        .WillOnce(
            [downloadedSyncChunks](
                qint32 afterUsn,
                qevercloud::IRequestContextPtr ctx) mutable // NOLINT
            {
                Q_UNUSED(afterUsn)
                Q_UNUSED(ctx)

                return threading::makeReadyFuture<QList<qevercloud::SyncChunk>>(
                    std::move(downloadedSyncChunks));
            });

    auto future = provider.fetchSyncChunks(0, qevercloud::newRequestContext());
    ASSERT_TRUE(future.isFinished());
    ASSERT_TRUE(future.resultCount());
    EXPECT_EQ(future.result(), fullSyncChunks);
}

} // namespace quentier::synchronization::tests
