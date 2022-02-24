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
#include <quentier/exception/RuntimeError.h>
#include <quentier/threading/Future.h>
#include <quentier/utility/UidGenerator.h>

#include <qevercloud/exceptions/EDAMSystemExceptionRateLimitReached.h>
#include <qevercloud/types/builders/LinkedNotebookBuilder.h>
#include <qevercloud/types/builders/NotebookBuilder.h>
#include <qevercloud/types/builders/SyncChunkBuilder.h>
#include <qevercloud/types/builders/SyncChunkFilterBuilder.h>

#include <gtest/gtest.h>

#include <array>
#include <memory>

// clazy:excludeall=non-pod-global-static
// clazy:excludeall=returning-void-expression

namespace quentier::synchronization::tests {

namespace {

const auto sampleSyncChunk1 =
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

const auto sampleSyncChunk2 =
    qevercloud::SyncChunkBuilder{}
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
        .setUpdateCount(54)
        .build();

const auto sampleSyncChunk3 =
    qevercloud::SyncChunkBuilder{}
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
        .setUpdateCount(82)
        .build();

const auto sampleFullSyncSyncChunkFilter =
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

const auto sampleIncrementalSyncSyncChunkFilter =
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
        .setIncludeExpunged(true)
        .setIncludeResources(true)
        .build();

[[nodiscard]] QList<qevercloud::SyncChunk> adjustSyncChunksUpdateCounts(
    QList<qevercloud::SyncChunk> syncChunks)
{
    if (Q_UNLIKELY(syncChunks.isEmpty())) {
        return {};
    }

    const auto & lastSyncChunk = syncChunks.constLast();
    const qint32 updateCount = lastSyncChunk.updateCount();
    for (auto & syncChunk: syncChunks) {
        syncChunk.setUpdateCount(updateCount);
    }

    return syncChunks;
}

} // namespace

using testing::InSequence;
using testing::Return;

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

struct UserOwnSyncChunksTestData
{
    QString m_testName;
    SynchronizationMode m_syncMode = SynchronizationMode::Full;
    QList<qevercloud::SyncChunk> m_syncChunks;
    qevercloud::SyncChunkFilter m_syncChunkFilter;
};

class SyncChunksDownloaderUserOwnSyncChunksTest :
    public SyncChunksDownloaderTest,
    public testing::WithParamInterface<UserOwnSyncChunksTestData>
{};

std::array gUserOwnSyncChunksTestData{
    UserOwnSyncChunksTestData{
        QStringLiteral("Single user own sync chunk with full sync"),
        SynchronizationMode::Full,
        QList<qevercloud::SyncChunk>{} << sampleSyncChunk1,
        sampleFullSyncSyncChunkFilter,
    },
    UserOwnSyncChunksTestData{
        QStringLiteral("Single user own sync chunk with incremental sync"),
        SynchronizationMode::Incremental,
        QList<qevercloud::SyncChunk>{} << sampleSyncChunk1,
        sampleIncrementalSyncSyncChunkFilter,
    },
    UserOwnSyncChunksTestData{
        QStringLiteral("Multiple user own sync chunks with full sync"),
        SynchronizationMode::Full,
        adjustSyncChunksUpdateCounts(
            QList<qevercloud::SyncChunk>{}
            << sampleSyncChunk1 << sampleSyncChunk2 << sampleSyncChunk3),
        sampleFullSyncSyncChunkFilter,
    },
    UserOwnSyncChunksTestData{
        QStringLiteral("Multiple user own sync chunks with incremental sync"),
        SynchronizationMode::Incremental,
        adjustSyncChunksUpdateCounts(
            QList<qevercloud::SyncChunk>{}
            << sampleSyncChunk1 << sampleSyncChunk2 << sampleSyncChunk3),
        sampleIncrementalSyncSyncChunkFilter,
    },
};

INSTANTIATE_TEST_SUITE_P(
    SyncChunksDownloaderUserOwnSyncChunksTestInstance,
    SyncChunksDownloaderUserOwnSyncChunksTest,
    testing::ValuesIn(gUserOwnSyncChunksTestData));

TEST_P(SyncChunksDownloaderUserOwnSyncChunksTest, DownloadUserOwnSyncChunks)
{
    const auto & testData = GetParam();

    SyncChunksDownloader downloader{testData.m_syncMode, m_mockNoteStore};

    const QString authToken = QStringLiteral("token");
    const auto ctx = qevercloud::newRequestContext(authToken);

    constexpr qint32 afterUsnInitial = 0;
    constexpr qint32 maxEntries = 50;
    qint32 afterUsn = afterUsnInitial;

    InSequence s;

    std::optional<qint32> previousChunkHighUsn;
    for (const auto & syncChunk: qAsConst(testData.m_syncChunks)) {
        if (previousChunkHighUsn) {
            afterUsn = *previousChunkHighUsn;
        }

        EXPECT_CALL(*m_mockNoteStore, getFilteredSyncChunkAsync)
            .WillOnce([&, afterUsnCurrent = afterUsn](
                          const qint32 afterUsnParam,
                          const qint32 maxEntriesParam,
                          const qevercloud::SyncChunkFilter & syncChunkFilter,
                          const qevercloud::IRequestContextPtr & ctxParam) {
                EXPECT_EQ(afterUsnParam, afterUsnCurrent);
                EXPECT_EQ(maxEntriesParam, maxEntries);
                EXPECT_EQ(syncChunkFilter, testData.m_syncChunkFilter);
                EXPECT_EQ(ctxParam, ctx);
                return threading::makeReadyFuture(syncChunk);
            });

        ASSERT_TRUE(syncChunk.chunkHighUSN());
        previousChunkHighUsn = *syncChunk.chunkHighUSN();
    }

    const auto syncChunksFuture =
        downloader.downloadSyncChunks(afterUsnInitial, ctx);

    ASSERT_TRUE(syncChunksFuture.isFinished())
        << testData.m_testName.toStdString();

    ASSERT_EQ(syncChunksFuture.resultCount(), 1)
        << testData.m_testName.toStdString();

    const auto syncChunksResult = syncChunksFuture.result();
    EXPECT_EQ(syncChunksResult.m_exception, nullptr)
        << testData.m_testName.toStdString();

    EXPECT_EQ(syncChunksResult.m_syncChunks, testData.m_syncChunks)
        << testData.m_testName.toStdString();
}

struct LinkedNotebookSyncChunksTestData
{
    QString m_testName;
    SynchronizationMode m_syncMode = SynchronizationMode::Full;
    QList<qevercloud::SyncChunk> m_syncChunks;
};

class SyncChunksDownloaderLinkedNotebookSyncChunksTest :
    public SyncChunksDownloaderTest,
    public testing::WithParamInterface<LinkedNotebookSyncChunksTestData>
{};

std::array gLinkedNotebookSyncChunksTestData{
    LinkedNotebookSyncChunksTestData{
        QStringLiteral("Single linked notebook sync chunk with full sync"),
        SynchronizationMode::Full,
        QList<qevercloud::SyncChunk>{} << sampleSyncChunk1,
    },
    LinkedNotebookSyncChunksTestData{
        QStringLiteral(
            "Single linked notebook sync chunk with incremental sync"),
        SynchronizationMode::Incremental,
        QList<qevercloud::SyncChunk>{} << sampleSyncChunk1,
    },
    LinkedNotebookSyncChunksTestData{
        QStringLiteral("Multiple linked notebook sync chunks with full sync"),
        SynchronizationMode::Full,
        adjustSyncChunksUpdateCounts(
            QList<qevercloud::SyncChunk>{}
            << sampleSyncChunk1 << sampleSyncChunk2 << sampleSyncChunk3),
    },
    LinkedNotebookSyncChunksTestData{
        QStringLiteral(
            "Multiple linked notebook sync chunks with incremental sync"),
        SynchronizationMode::Incremental,
        adjustSyncChunksUpdateCounts(
            QList<qevercloud::SyncChunk>{}
            << sampleSyncChunk1 << sampleSyncChunk2 << sampleSyncChunk3),
    },
};

INSTANTIATE_TEST_SUITE_P(
    SyncChunksDownloaderLinkedNotebookSyncChunksTestInstance,
    SyncChunksDownloaderLinkedNotebookSyncChunksTest,
    testing::ValuesIn(gLinkedNotebookSyncChunksTestData));

TEST_P(
    SyncChunksDownloaderLinkedNotebookSyncChunksTest,
    DownloadLinkedNotebookSyncChunks)
{
    const auto & testData = GetParam();

    SyncChunksDownloader downloader{testData.m_syncMode, m_mockNoteStore};

    const QString authToken = QStringLiteral("token");
    const auto ctx = qevercloud::newRequestContext(authToken);

    const auto linkedNotebook = qevercloud::LinkedNotebookBuilder{}
                                    .setGuid(UidGenerator::Generate())
                                    .build();

    constexpr qint32 afterUsnInitial = 0;
    constexpr qint32 maxEntries = 50;
    qint32 afterUsn = afterUsnInitial;

    InSequence s;

    std::optional<qint32> previousChunkHighUsn;
    for (const auto & syncChunk: qAsConst(testData.m_syncChunks)) {
        if (previousChunkHighUsn) {
            afterUsn = *previousChunkHighUsn;
        }

        EXPECT_CALL(*m_mockNoteStore, getLinkedNotebookSyncChunkAsync)
            .WillOnce(
                [&, afterUsnCurrent = afterUsn](
                    const qevercloud::LinkedNotebook & linkedNotebookParam,
                    const qint32 afterUsnParam, const qint32 maxEntriesParam,
                    const bool fullSyncOnly,
                    const qevercloud::IRequestContextPtr & ctxParam) {
                    EXPECT_EQ(linkedNotebookParam, linkedNotebook);
                    EXPECT_EQ(afterUsnParam, afterUsnCurrent);
                    EXPECT_EQ(maxEntriesParam, maxEntries);
                    EXPECT_EQ(
                        fullSyncOnly,
                        (testData.m_syncMode == SynchronizationMode::Full));
                    EXPECT_EQ(ctxParam, ctx);
                    return threading::makeReadyFuture(syncChunk);
                });

        ASSERT_TRUE(syncChunk.chunkHighUSN());
        previousChunkHighUsn = *syncChunk.chunkHighUSN();
    }

    const auto syncChunksFuture = downloader.downloadLinkedNotebookSyncChunks(
        linkedNotebook, afterUsnInitial, ctx);

    ASSERT_TRUE(syncChunksFuture.isFinished())
        << testData.m_testName.toStdString();

    ASSERT_EQ(syncChunksFuture.resultCount(), 1)
        << testData.m_testName.toStdString();

    const auto syncChunksResult = syncChunksFuture.result();
    EXPECT_EQ(syncChunksResult.m_exception, nullptr)
        << testData.m_testName.toStdString();

    EXPECT_EQ(syncChunksResult.m_syncChunks, testData.m_syncChunks)
        << testData.m_testName.toStdString();
}

TEST_F(
    SyncChunksDownloaderTest,
    ReturnPartialUserOwnSyncChunksIfEverCloudExceptionOccursInTheProcess)
{
    SyncChunksDownloader downloader{SynchronizationMode::Full, m_mockNoteStore};

    const QString authToken = QStringLiteral("token");
    const auto ctx = qevercloud::newRequestContext(authToken);

    constexpr qint32 afterUsnInitial = 0;
    constexpr qint32 maxEntries = 50;
    qint32 afterUsn = afterUsnInitial;

    qevercloud::EDAMSystemExceptionRateLimitReached e;
    e.setRateLimitDuration(30000);

    InSequence s;

    const auto syncChunks = adjustSyncChunksUpdateCounts(
        QList<qevercloud::SyncChunk>{} << sampleSyncChunk1 << sampleSyncChunk2
                                       << sampleSyncChunk3);

    std::optional<qint32> previousChunkHighUsn;
    for (const auto & syncChunk: qAsConst(syncChunks)) {
        if (previousChunkHighUsn) {
            afterUsn = *previousChunkHighUsn;
        }

        EXPECT_CALL(*m_mockNoteStore, getFilteredSyncChunkAsync)
            .WillOnce([&, afterUsnCurrent = afterUsn](
                          const qint32 afterUsnParam,
                          const qint32 maxEntriesParam,
                          const qevercloud::SyncChunkFilter & syncChunkFilter,
                          const qevercloud::IRequestContextPtr & ctxParam) {
                EXPECT_EQ(afterUsnParam, afterUsnCurrent);
                EXPECT_EQ(maxEntriesParam, maxEntries);
                EXPECT_EQ(syncChunkFilter, sampleFullSyncSyncChunkFilter);
                EXPECT_EQ(ctxParam, ctx);

                if (afterUsnParam == sampleSyncChunk2.updateCount()) {
                    return threading::makeExceptionalFuture<
                        qevercloud::SyncChunk>(e);
                }

                return threading::makeReadyFuture(syncChunk);
            });

        ASSERT_TRUE(syncChunk.chunkHighUSN());
        previousChunkHighUsn = *syncChunk.chunkHighUSN();
    }

    const auto syncChunksFuture =
        downloader.downloadSyncChunks(afterUsnInitial, ctx);

    ASSERT_TRUE(syncChunksFuture.isFinished());
    ASSERT_EQ(syncChunksFuture.resultCount(), 1);

    const auto syncChunksResult = syncChunksFuture.result();

    const auto * exc =
        dynamic_cast<const qevercloud::EDAMSystemExceptionRateLimitReached *>(
            syncChunksResult.m_exception.get());

    ASSERT_TRUE(exc);
    EXPECT_EQ(exc->rateLimitDuration(), e.rateLimitDuration());

    const auto partialSyncChunks = [&]
    {
        auto chunks = syncChunks;
        Q_UNUSED(chunks.takeLast());
        return chunks;
    }();

    EXPECT_EQ(syncChunksResult.m_syncChunks, partialSyncChunks);
}

TEST_F(
    SyncChunksDownloaderTest,
    ReturnPartialUserOwnSyncChunksIfNonEverCloudExceptionOccursInTheProcess)
{
    SyncChunksDownloader downloader{SynchronizationMode::Full, m_mockNoteStore};

    const QString authToken = QStringLiteral("token");
    const auto ctx = qevercloud::newRequestContext(authToken);

    constexpr qint32 afterUsnInitial = 0;
    constexpr qint32 maxEntries = 50;
    qint32 afterUsn = afterUsnInitial;

    const RuntimeError e{ErrorString{QStringLiteral("Error")}};

    InSequence s;

    const auto syncChunks = adjustSyncChunksUpdateCounts(
        QList<qevercloud::SyncChunk>{} << sampleSyncChunk1 << sampleSyncChunk2
                                       << sampleSyncChunk3);

    std::optional<qint32> previousChunkHighUsn;
    for (const auto & syncChunk: qAsConst(syncChunks)) {
        if (previousChunkHighUsn) {
            afterUsn = *previousChunkHighUsn;
        }

        EXPECT_CALL(*m_mockNoteStore, getFilteredSyncChunkAsync)
            .WillOnce([&, afterUsnCurrent = afterUsn](
                          const qint32 afterUsnParam,
                          const qint32 maxEntriesParam,
                          const qevercloud::SyncChunkFilter & syncChunkFilter,
                          const qevercloud::IRequestContextPtr & ctxParam) {
                EXPECT_EQ(afterUsnParam, afterUsnCurrent);
                EXPECT_EQ(maxEntriesParam, maxEntries);
                EXPECT_EQ(syncChunkFilter, sampleFullSyncSyncChunkFilter);
                EXPECT_EQ(ctxParam, ctx);

                if (afterUsnParam == sampleSyncChunk2.updateCount()) {
                    return threading::makeExceptionalFuture<
                        qevercloud::SyncChunk>(e);
                }

                return threading::makeReadyFuture(syncChunk);
            });

        ASSERT_TRUE(syncChunk.chunkHighUSN());
        previousChunkHighUsn = *syncChunk.chunkHighUSN();
    }

    const auto syncChunksFuture =
        downloader.downloadSyncChunks(afterUsnInitial, ctx);

    ASSERT_TRUE(syncChunksFuture.isFinished());
    ASSERT_EQ(syncChunksFuture.resultCount(), 1);

    const auto syncChunksResult = syncChunksFuture.result();

    const auto * exc =
        dynamic_cast<const RuntimeError *>(
            syncChunksResult.m_exception.get());

    ASSERT_TRUE(exc);
    EXPECT_EQ(exc->nonLocalizedErrorMessage(), e.nonLocalizedErrorMessage());

    const auto partialSyncChunks = [&]
    {
        auto chunks = syncChunks;
        Q_UNUSED(chunks.takeLast());
        return chunks;
    }();

    EXPECT_EQ(syncChunksResult.m_syncChunks, partialSyncChunks);
}

TEST_F(
    SyncChunksDownloaderTest,
    ReturnPartialLinkedNotebookSyncChunksIfEverCloudExceptionOccursInTheProcess)
{
    SyncChunksDownloader downloader{SynchronizationMode::Full, m_mockNoteStore};

    const QString authToken = QStringLiteral("token");
    const auto ctx = qevercloud::newRequestContext(authToken);

    const auto linkedNotebook = qevercloud::LinkedNotebookBuilder{}
                                    .setGuid(UidGenerator::Generate())
                                    .build();

    constexpr qint32 afterUsnInitial = 0;
    constexpr qint32 maxEntries = 50;
    qint32 afterUsn = afterUsnInitial;

    qevercloud::EDAMSystemExceptionRateLimitReached e;
    e.setRateLimitDuration(30000);

    InSequence s;

    const auto syncChunks = adjustSyncChunksUpdateCounts(
        QList<qevercloud::SyncChunk>{} << sampleSyncChunk1 << sampleSyncChunk2
                                       << sampleSyncChunk3);

    std::optional<qint32> previousChunkHighUsn;
    for (const auto & syncChunk: qAsConst(syncChunks)) {
        if (previousChunkHighUsn) {
            afterUsn = *previousChunkHighUsn;
        }

        EXPECT_CALL(*m_mockNoteStore, getLinkedNotebookSyncChunkAsync)
            .WillOnce(
                [&, afterUsnCurrent = afterUsn](
                    const qevercloud::LinkedNotebook & linkedNotebookParam,
                    const qint32 afterUsnParam, const qint32 maxEntriesParam,
                    const bool fullSyncOnly,
                    const qevercloud::IRequestContextPtr & ctxParam) {
                    EXPECT_EQ(linkedNotebookParam, linkedNotebook);
                    EXPECT_EQ(afterUsnParam, afterUsnCurrent);
                    EXPECT_EQ(maxEntriesParam, maxEntries);
                    EXPECT_TRUE(fullSyncOnly);
                    EXPECT_EQ(ctxParam, ctx);

                    if (afterUsnParam == sampleSyncChunk2.updateCount()) {
                        return threading::makeExceptionalFuture<
                            qevercloud::SyncChunk>(e);
                    }

                    return threading::makeReadyFuture(syncChunk);
                });

        ASSERT_TRUE(syncChunk.chunkHighUSN());
        previousChunkHighUsn = *syncChunk.chunkHighUSN();
    }

    const auto syncChunksFuture = downloader.downloadLinkedNotebookSyncChunks(
        linkedNotebook, afterUsnInitial, ctx);

    ASSERT_TRUE(syncChunksFuture.isFinished());
    ASSERT_EQ(syncChunksFuture.resultCount(), 1);

    const auto syncChunksResult = syncChunksFuture.result();

    const auto * exc =
        dynamic_cast<const qevercloud::EDAMSystemExceptionRateLimitReached *>(
            syncChunksResult.m_exception.get());

    ASSERT_TRUE(exc);
    EXPECT_EQ(exc->rateLimitDuration(), e.rateLimitDuration());

    const auto partialSyncChunks = [&]
    {
        auto chunks = syncChunks;
        Q_UNUSED(chunks.takeLast());
        return chunks;
    }();

    EXPECT_EQ(syncChunksResult.m_syncChunks, partialSyncChunks);
}

TEST_F(
    SyncChunksDownloaderTest,
    ReturnPartialLinkedNotebookSyncChunksIfNonEverCloudExceptionOccursInTheProcess)
{
    SyncChunksDownloader downloader{SynchronizationMode::Full, m_mockNoteStore};

    const QString authToken = QStringLiteral("token");
    const auto ctx = qevercloud::newRequestContext(authToken);

    const auto linkedNotebook = qevercloud::LinkedNotebookBuilder{}
                                    .setGuid(UidGenerator::Generate())
                                    .build();

    constexpr qint32 afterUsnInitial = 0;
    constexpr qint32 maxEntries = 50;
    qint32 afterUsn = afterUsnInitial;

    const RuntimeError e{ErrorString{QStringLiteral("Error")}};

    InSequence s;

    const auto syncChunks = adjustSyncChunksUpdateCounts(
        QList<qevercloud::SyncChunk>{} << sampleSyncChunk1 << sampleSyncChunk2
                                       << sampleSyncChunk3);

    std::optional<qint32> previousChunkHighUsn;
    for (const auto & syncChunk: qAsConst(syncChunks)) {
        if (previousChunkHighUsn) {
            afterUsn = *previousChunkHighUsn;
        }

        EXPECT_CALL(*m_mockNoteStore, getLinkedNotebookSyncChunkAsync)
            .WillOnce(
                [&, afterUsnCurrent = afterUsn](
                    const qevercloud::LinkedNotebook & linkedNotebookParam,
                    const qint32 afterUsnParam, const qint32 maxEntriesParam,
                    const bool fullSyncOnly,
                    const qevercloud::IRequestContextPtr & ctxParam) {
                    EXPECT_EQ(linkedNotebookParam, linkedNotebook);
                    EXPECT_EQ(afterUsnParam, afterUsnCurrent);
                    EXPECT_EQ(maxEntriesParam, maxEntries);
                    EXPECT_TRUE(fullSyncOnly);
                    EXPECT_EQ(ctxParam, ctx);

                    if (afterUsnParam == sampleSyncChunk2.updateCount()) {
                        return threading::makeExceptionalFuture<
                            qevercloud::SyncChunk>(e);
                    }

                    return threading::makeReadyFuture(syncChunk);
                });

        ASSERT_TRUE(syncChunk.chunkHighUSN());
        previousChunkHighUsn = *syncChunk.chunkHighUSN();
    }

    const auto syncChunksFuture = downloader.downloadLinkedNotebookSyncChunks(
        linkedNotebook, afterUsnInitial, ctx);

    ASSERT_TRUE(syncChunksFuture.isFinished());
    ASSERT_EQ(syncChunksFuture.resultCount(), 1);

    const auto syncChunksResult = syncChunksFuture.result();

    const auto * exc =
        dynamic_cast<const RuntimeError *>(
            syncChunksResult.m_exception.get());

    ASSERT_TRUE(exc);
    EXPECT_EQ(exc->nonLocalizedErrorMessage(), e.nonLocalizedErrorMessage());

    const auto partialSyncChunks = [&]
    {
        auto chunks = syncChunks;
        Q_UNUSED(chunks.takeLast());
        return chunks;
    }();

    EXPECT_EQ(syncChunksResult.m_syncChunks, partialSyncChunks);
}

} // namespace quentier::synchronization::tests
