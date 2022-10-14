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
#include <quentier/utility/cancelers/ManualCanceler.h>

#include <qevercloud/RequestContext.h>
#include <qevercloud/types/builders/LinkedNotebookBuilder.h>
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

    utility::cancelers::ManualCancelerPtr m_manualCanceler =
        std::make_shared<utility::cancelers::ManualCanceler>();
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

    EXPECT_CALL(*m_mockSyncChunksDownloader, downloadSyncChunks(82, _, _, _))
        .WillOnce(Return(
            threading::makeReadyFuture<ISyncChunksDownloader::SyncChunksResult>(
                {})));

    auto future = provider.fetchSyncChunks(
        0, qevercloud::newRequestContext(), m_manualCanceler);

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

    EXPECT_CALL(*m_mockSyncChunksDownloader, downloadSyncChunks(54, _, _, _))
        .WillOnce([downloadedSyncChunks](
                      qint32 afterUsn,
                      qevercloud::IRequestContextPtr ctx,        // NOLINT
                      utility::cancelers::ICancelerPtr canceler, // NOLINT
                      ISyncChunksDownloader::ICallbackWeakPtr
                          callbackWeak) mutable // NOLINT
                  {
                      Q_UNUSED(afterUsn)
                      Q_UNUSED(ctx)
                      Q_UNUSED(canceler)
                      Q_UNUSED(callbackWeak)

                      return threading::makeReadyFuture<
                          ISyncChunksDownloader::SyncChunksResult>(
                          ISyncChunksDownloader::SyncChunksResult{
                              std::move(downloadedSyncChunks), nullptr});
                  });

    auto future = provider.fetchSyncChunks(
        0, qevercloud::newRequestContext(), m_manualCanceler);

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

    EXPECT_CALL(*m_mockSyncChunksDownloader, downloadSyncChunks(0, _, _, _))
        .WillOnce(Return(
            threading::makeReadyFuture<ISyncChunksDownloader::SyncChunksResult>(
                ISyncChunksDownloader::SyncChunksResult{syncChunks, nullptr})));

    auto future = provider.fetchSyncChunks(
        0, qevercloud::newRequestContext(), m_manualCanceler);

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

    EXPECT_CALL(*m_mockSyncChunksDownloader, downloadSyncChunks(54, _, _, _))
        .WillOnce([downloadedSyncChunks](
                      qint32 afterUsn,
                      qevercloud::IRequestContextPtr ctx,        // NOLINT
                      utility::cancelers::ICancelerPtr canceler, // NOLINT
                      ISyncChunksDownloader::ICallbackWeakPtr
                          callbackWeak) mutable // NOLINT
                  {
                      Q_UNUSED(afterUsn)
                      Q_UNUSED(ctx)
                      Q_UNUSED(canceler)
                      Q_UNUSED(callbackWeak)

                      return threading::makeReadyFuture<
                          ISyncChunksDownloader::SyncChunksResult>(
                          ISyncChunksDownloader::SyncChunksResult{
                              std::move(downloadedSyncChunks), nullptr});
                  });

    auto future = provider.fetchSyncChunks(
        0, qevercloud::newRequestContext(), m_manualCanceler);

    ASSERT_TRUE(future.isFinished());
    ASSERT_TRUE(future.resultCount());
    EXPECT_EQ(future.result(), fullSyncChunks);
}

TEST_F(
    SyncChunksProviderTest,
    StoreDownloadedSyncChunksIfSyncChunksDownloaderReturnsChunksWithException)
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

    EXPECT_CALL(*m_mockSyncChunksDownloader, downloadSyncChunks(0, _, _, _))
        .WillOnce(Return(
            threading::makeReadyFuture<ISyncChunksDownloader::SyncChunksResult>(
                ISyncChunksDownloader::SyncChunksResult{
                    syncChunks,
                    std::make_shared<qevercloud::EverCloudException>(
                        QStringLiteral("something"))})));

    EXPECT_CALL(*m_mockSyncChunksStorage, putUserOwnSyncChunks(syncChunks))
        .Times(1);

    auto future = provider.fetchSyncChunks(
        0, qevercloud::newRequestContext(), m_manualCanceler);

    EXPECT_THROW(future.waitForFinished(), qevercloud::EverCloudException);
}

TEST_F(
    SyncChunksProviderTest,
    StoreOnlyDownloadedSyncChunksIfSyncChunksDownloaderReturnsChunksWithException)
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

    EXPECT_CALL(*m_mockSyncChunksDownloader, downloadSyncChunks(54, _, _, _))
        .WillOnce([downloadedSyncChunks](
                      qint32 afterUsn,
                      qevercloud::IRequestContextPtr ctx,        // NOLINT
                      utility::cancelers::ICancelerPtr canceler, // NOLINT
                      ISyncChunksDownloader::ICallbackWeakPtr
                          callbackWeak) mutable // NOLINT
                  {
                      Q_UNUSED(afterUsn)
                      Q_UNUSED(ctx)
                      Q_UNUSED(canceler)
                      Q_UNUSED(callbackWeak)

                      return threading::makeReadyFuture<
                          ISyncChunksDownloader::SyncChunksResult>(
                          ISyncChunksDownloader::SyncChunksResult{
                              std::move(downloadedSyncChunks),
                              std::make_shared<qevercloud::EverCloudException>(
                                  QStringLiteral("something"))});
                  });

    EXPECT_CALL(
        *m_mockSyncChunksStorage, putUserOwnSyncChunks(downloadedSyncChunks))
        .Times(1);

    auto future = provider.fetchSyncChunks(
        0, qevercloud::newRequestContext(), m_manualCanceler);

    EXPECT_THROW(future.waitForFinished(), qevercloud::EverCloudException);
}

TEST_F(SyncChunksProviderTest, FetchLinkedNotebookSyncChunksFromStorage)
{
    SyncChunksProvider provider{
        m_mockSyncChunksDownloader, m_mockSyncChunksStorage};

    const qevercloud::Guid linkedNotebookGuid = UidGenerator::Generate();

    const QList<std::pair<qint32, qint32>> usnsRange =
        QList<std::pair<qint32, qint32>>{} << std::make_pair<qint32>(0, 35)
                                           << std::make_pair<qint32>(36, 54)
                                           << std::make_pair(55, 82);

    InSequence s;

    EXPECT_CALL(
        *m_mockSyncChunksStorage,
        fetchLinkedNotebookSyncChunksLowAndHighUsns(linkedNotebookGuid))
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

    EXPECT_CALL(
        *m_mockSyncChunksStorage,
        fetchRelevantLinkedNotebookSyncChunks(linkedNotebookGuid, 0))
        .WillOnce(Return(syncChunks));

    const auto linkedNotebook =
        qevercloud::LinkedNotebookBuilder{}.setGuid(linkedNotebookGuid).build();

    EXPECT_CALL(
        *m_mockSyncChunksDownloader,
        downloadLinkedNotebookSyncChunks(linkedNotebook, 82, _, _, _))
        .WillOnce(Return(
            threading::makeReadyFuture<ISyncChunksDownloader::SyncChunksResult>(
                {})));

    auto future = provider.fetchLinkedNotebookSyncChunks(
        linkedNotebook, 0, qevercloud::newRequestContext(), m_manualCanceler);

    ASSERT_TRUE(future.isFinished());
    ASSERT_TRUE(future.resultCount());
    EXPECT_EQ(future.result(), syncChunks);
}

TEST_F(SyncChunksProviderTest, FetchPartOfLinkedNotebookSyncChunksFromStorage)
{
    SyncChunksProvider provider{
        m_mockSyncChunksDownloader, m_mockSyncChunksStorage};

    const qevercloud::Guid linkedNotebookGuid = UidGenerator::Generate();

    const QList<std::pair<qint32, qint32>> usnsRange =
        QList<std::pair<qint32, qint32>>{} << std::make_pair<qint32>(0, 35)
                                           << std::make_pair<qint32>(36, 54);

    InSequence s;

    EXPECT_CALL(
        *m_mockSyncChunksStorage,
        fetchLinkedNotebookSyncChunksLowAndHighUsns(linkedNotebookGuid))
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

    EXPECT_CALL(
        *m_mockSyncChunksStorage,
        fetchRelevantLinkedNotebookSyncChunks(linkedNotebookGuid, 0))
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

    const auto linkedNotebook =
        qevercloud::LinkedNotebookBuilder{}.setGuid(linkedNotebookGuid).build();

    EXPECT_CALL(
        *m_mockSyncChunksDownloader,
        downloadLinkedNotebookSyncChunks(linkedNotebook, 54, _, _, _))
        .WillOnce([downloadedSyncChunks, &linkedNotebook](
                      qevercloud::LinkedNotebook ln, // NOLINT
                      qint32 afterUsn,
                      qevercloud::IRequestContextPtr ctx,        // NOLINT
                      utility::cancelers::ICancelerPtr canceler, // NOLINT
                      ISyncChunksDownloader::ICallbackWeakPtr
                          callbackWeak) mutable // NOLINT
                  {
                      EXPECT_EQ(ln, linkedNotebook);
                      Q_UNUSED(afterUsn)
                      Q_UNUSED(ctx)
                      Q_UNUSED(canceler)
                      Q_UNUSED(callbackWeak)

                      return threading::makeReadyFuture<
                          ISyncChunksDownloader::SyncChunksResult>(
                          ISyncChunksDownloader::SyncChunksResult{
                              std::move(downloadedSyncChunks), nullptr});
                  });

    auto future = provider.fetchLinkedNotebookSyncChunks(
        linkedNotebook, 0, qevercloud::newRequestContext(), m_manualCanceler);

    ASSERT_TRUE(future.isFinished());
    ASSERT_TRUE(future.resultCount());
    EXPECT_EQ(future.result(), fullSyncChunks);
}

TEST_F(
    SyncChunksProviderTest,
    DownloadLinkedNotebookSyncChunksWhenThereAreNoneInStorage)
{
    SyncChunksProvider provider{
        m_mockSyncChunksDownloader, m_mockSyncChunksStorage};

    const qevercloud::Guid linkedNotebookGuid = UidGenerator::Generate();

    InSequence s;

    EXPECT_CALL(
        *m_mockSyncChunksStorage,
        fetchLinkedNotebookSyncChunksLowAndHighUsns(linkedNotebookGuid))
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

    const auto linkedNotebook =
        qevercloud::LinkedNotebookBuilder{}.setGuid(linkedNotebookGuid).build();

    EXPECT_CALL(
        *m_mockSyncChunksDownloader,
        downloadLinkedNotebookSyncChunks(linkedNotebook, 0, _, _, _))
        .WillOnce(Return(
            threading::makeReadyFuture<ISyncChunksDownloader::SyncChunksResult>(
                ISyncChunksDownloader::SyncChunksResult{syncChunks, nullptr})));

    auto future = provider.fetchLinkedNotebookSyncChunks(
        linkedNotebook, 0, qevercloud::newRequestContext(), m_manualCanceler);

    ASSERT_TRUE(future.isFinished());
    ASSERT_TRUE(future.resultCount());
    EXPECT_EQ(future.result(), syncChunks);
}

TEST_F(
    SyncChunksProviderTest,
    DownloadLinkedNotebookSyncChunksWhenStorageGivesIncompleteSyncChunks)
{
    SyncChunksProvider provider{
        m_mockSyncChunksDownloader, m_mockSyncChunksStorage};

    const qevercloud::Guid linkedNotebookGuid = UidGenerator::Generate();

    const QList<std::pair<qint32, qint32>> usnsRange =
        QList<std::pair<qint32, qint32>>{} << std::make_pair<qint32>(0, 35)
                                           << std::make_pair<qint32>(36, 54)
                                           << std::make_pair(55, 82);

    InSequence s;

    EXPECT_CALL(
        *m_mockSyncChunksStorage,
        fetchLinkedNotebookSyncChunksLowAndHighUsns(linkedNotebookGuid))
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

    EXPECT_CALL(
        *m_mockSyncChunksStorage,
        fetchRelevantLinkedNotebookSyncChunks(linkedNotebookGuid, 0))
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

    const auto linkedNotebook =
        qevercloud::LinkedNotebookBuilder{}.setGuid(linkedNotebookGuid).build();

    EXPECT_CALL(
        *m_mockSyncChunksDownloader,
        downloadLinkedNotebookSyncChunks(linkedNotebook, 54, _, _, _))
        .WillOnce([downloadedSyncChunks, &linkedNotebook](
                      qevercloud::LinkedNotebook ln, // NOLINT
                      qint32 afterUsn,
                      qevercloud::IRequestContextPtr ctx,        // NOLINT
                      utility::cancelers::ICancelerPtr canceler, // NOLINT
                      ISyncChunksDownloader::ICallbackWeakPtr
                          callbackWeak) mutable // NOLINT
                  {
                      EXPECT_EQ(ln, linkedNotebook);
                      Q_UNUSED(afterUsn)
                      Q_UNUSED(ctx)
                      Q_UNUSED(canceler)
                      Q_UNUSED(callbackWeak)

                      return threading::makeReadyFuture<
                          ISyncChunksDownloader::SyncChunksResult>(
                          ISyncChunksDownloader::SyncChunksResult{
                              std::move(downloadedSyncChunks), nullptr});
                  });

    auto future = provider.fetchLinkedNotebookSyncChunks(
        linkedNotebook, 0, qevercloud::newRequestContext(), m_manualCanceler);

    ASSERT_TRUE(future.isFinished());
    ASSERT_TRUE(future.resultCount());
    EXPECT_EQ(future.result(), fullSyncChunks);
}

TEST_F(
    SyncChunksProviderTest,
    StoreDownloadedLinkedNotebookSyncChunksIfSyncChunksDownloaderReturnsChunksWithException)
{
    SyncChunksProvider provider{
        m_mockSyncChunksDownloader, m_mockSyncChunksStorage};

    const qevercloud::Guid linkedNotebookGuid = UidGenerator::Generate();

    InSequence s;

    EXPECT_CALL(
        *m_mockSyncChunksStorage,
        fetchLinkedNotebookSyncChunksLowAndHighUsns(linkedNotebookGuid))
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

    const auto linkedNotebook =
        qevercloud::LinkedNotebookBuilder{}.setGuid(linkedNotebookGuid).build();

    EXPECT_CALL(
        *m_mockSyncChunksDownloader,
        downloadLinkedNotebookSyncChunks(linkedNotebook, 0, _, _, _))
        .WillOnce(Return(
            threading::makeReadyFuture<ISyncChunksDownloader::SyncChunksResult>(
                ISyncChunksDownloader::SyncChunksResult{
                    syncChunks,
                    std::make_shared<qevercloud::EverCloudException>(
                        QStringLiteral("something"))})));

    EXPECT_CALL(
        *m_mockSyncChunksStorage,
        putLinkedNotebookSyncChunks(linkedNotebookGuid, syncChunks))
        .Times(1);

    auto future = provider.fetchLinkedNotebookSyncChunks(
        linkedNotebook, 0, qevercloud::newRequestContext(), m_manualCanceler);

    EXPECT_THROW(future.waitForFinished(), qevercloud::EverCloudException);
}

TEST_F(
    SyncChunksProviderTest,
    StoreOnlyDownloadedLinkedNotebookSyncChunksIfSyncChunksDownloaderReturnsChunksWithException)
{
    SyncChunksProvider provider{
        m_mockSyncChunksDownloader, m_mockSyncChunksStorage};

    const qevercloud::Guid linkedNotebookGuid = UidGenerator::Generate();

    const QList<std::pair<qint32, qint32>> usnsRange =
        QList<std::pair<qint32, qint32>>{} << std::make_pair<qint32>(0, 35)
                                           << std::make_pair<qint32>(36, 54);

    InSequence s;

    EXPECT_CALL(
        *m_mockSyncChunksStorage,
        fetchLinkedNotebookSyncChunksLowAndHighUsns(linkedNotebookGuid))
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

    EXPECT_CALL(
        *m_mockSyncChunksStorage,
        fetchRelevantLinkedNotebookSyncChunks(linkedNotebookGuid, 0))
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

    const auto linkedNotebook =
        qevercloud::LinkedNotebookBuilder{}.setGuid(linkedNotebookGuid).build();

    EXPECT_CALL(
        *m_mockSyncChunksDownloader,
        downloadLinkedNotebookSyncChunks(linkedNotebook, 54, _, _, _))
        .WillOnce([downloadedSyncChunks, &linkedNotebook](
                      qevercloud::LinkedNotebook ln, // NOLINT
                      qint32 afterUsn,
                      qevercloud::IRequestContextPtr ctx,        // NOLINT
                      utility::cancelers::ICancelerPtr canceler, // NOLINT
                      ISyncChunksDownloader::ICallbackWeakPtr
                          callbackWeak) mutable // NOLINT
                  {
                      EXPECT_EQ(ln, linkedNotebook);
                      Q_UNUSED(afterUsn)
                      Q_UNUSED(ctx)
                      Q_UNUSED(canceler)
                      Q_UNUSED(callbackWeak)

                      return threading::makeReadyFuture<
                          ISyncChunksDownloader::SyncChunksResult>(
                          ISyncChunksDownloader::SyncChunksResult{
                              std::move(downloadedSyncChunks),
                              std::make_shared<qevercloud::EverCloudException>(
                                  QStringLiteral("something"))});
                  });

    EXPECT_CALL(
        *m_mockSyncChunksStorage,
        putLinkedNotebookSyncChunks(linkedNotebookGuid, downloadedSyncChunks))
        .Times(1);

    auto future = provider.fetchLinkedNotebookSyncChunks(
        linkedNotebook, 0, qevercloud::newRequestContext(), m_manualCanceler);

    EXPECT_THROW(future.waitForFinished(), qevercloud::EverCloudException);
}

} // namespace quentier::synchronization::tests
