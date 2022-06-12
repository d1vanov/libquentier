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

#include <synchronization/processors/DurableResourcesProcessor.h>
#include <synchronization/processors/Utils.h>
#include <synchronization/sync_chunks/Utils.h>
#include <synchronization/tests/mocks/MockIResourcesProcessor.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/RuntimeError.h>
#include <quentier/threading/Future.h>
#include <quentier/utility/FileSystem.h>
#include <quentier/utility/UidGenerator.h>

#include <qevercloud/types/SyncChunk.h>
#include <qevercloud/types/builders/ResourceBuilder.h>
#include <qevercloud/types/builders/SyncChunkBuilder.h>
#include <qevercloud/utility/ToRange.h>

#include <QSettings>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <algorithm>
#include <optional>

// clazy:excludeall=non-pod-global-static
// clazy:excludeall=returning-void-expression

namespace quentier::synchronization::tests {

using testing::StrictMock;

namespace {

[[nodiscard]] QList<qevercloud::Resource> generateTestResources(
    const qint32 startUsn, const qint32 endUsn)
{
    EXPECT_GE(endUsn, startUsn);
    if (endUsn < startUsn) {
        return {};
    }

    const auto noteGuid = UidGenerator::Generate();

    QList<qevercloud::Resource> result;
    result.reserve(endUsn - startUsn + 1);
    for (qint32 i = startUsn; i <= endUsn; ++i) {
        result << qevercloud::ResourceBuilder{}
                      .setGuid(UidGenerator::Generate())
                      .setNoteGuid(noteGuid)
                      .setUpdateSequenceNum(i)
                      .build();
    }

    return result;
}

[[nodiscard]] QHash<qevercloud::Guid, qint32>
    generateTestProcessedResourcesInfo(
        const qint32 startUsn, const qint32 endUsn)
{
    EXPECT_GT(endUsn, startUsn);
    if (endUsn < startUsn) {
        return {};
    }

    QHash<qevercloud::Guid, qint32> result;
    result.reserve(endUsn - startUsn + 1);
    for (qint32 i = startUsn; i <= endUsn; ++i) {
        result[UidGenerator::Generate()] = i;
    }

    return result;
}

MATCHER_P(
    EqSyncChunksWithSortedResources, syncChunks,
    "Check sync chunks with sorted resources equality")
{
    const auto sortSyncChunkResources = [](qevercloud::SyncChunk & syncChunk) {
        if (!syncChunk.resources()) {
            return;
        }

        std::sort(
            syncChunk.mutableResources()->begin(),
            syncChunk.mutableResources()->end(),
            [](const qevercloud::Resource & lhs,
               const qevercloud::Resource & rhs) {
                return lhs.updateSequenceNum() < rhs.updateSequenceNum();
            });
    };

    QList<qevercloud::SyncChunk> argSyncChunks = arg;
    for (auto & syncChunk: argSyncChunks) {
        sortSyncChunkResources(syncChunk);
    }

    QList<qevercloud::SyncChunk> expectedSyncChunks = syncChunks;
    for (auto & syncChunk: expectedSyncChunks) {
        sortSyncChunkResources(syncChunk);
    }

    testing::Matcher<QList<qevercloud::SyncChunk>> matcher =
        testing::Eq(expectedSyncChunks);
    return matcher.MatchAndExplain(argSyncChunks, result_listener);
}

} // namespace

class DurableResourcesProcessorTest : public testing::Test
{
protected:
    void TearDown() override
    {
        QDir dir{m_temporaryDir.path()};
        const auto entries =
            dir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);

        for (const auto & entry: qAsConst(entries)) {
            if (entry.isDir()) {
                ASSERT_TRUE(removeDir(entry.absoluteFilePath()));
            }
            else {
                ASSERT_TRUE(removeFile(entry.absoluteFilePath()));
            }
        }
    }

protected:
    const std::shared_ptr<mocks::MockIResourcesProcessor>
        m_mockResourcesProcessor =
            std::make_shared<StrictMock<mocks::MockIResourcesProcessor>>();

    QTemporaryDir m_temporaryDir;
};

TEST_F(DurableResourcesProcessorTest, Ctor)
{
    EXPECT_NO_THROW(
        const auto durableResourcesProcessor =
            std::make_shared<DurableResourcesProcessor>(
                m_mockResourcesProcessor, QDir{m_temporaryDir.path()}));
}

TEST_F(DurableResourcesProcessorTest, CtorNullResourcesProcessor)
{
    EXPECT_THROW(
        const auto durableResourcesProcessor =
            std::make_shared<DurableResourcesProcessor>(
                nullptr, QDir{m_temporaryDir.path()}),
        InvalidArgument);
}

TEST_F(DurableResourcesProcessorTest, ProcessSyncChunksWithoutPreviousSyncInfo)
{
    const auto resources = generateTestResources(1, 4);

    const auto syncChunks = QList<qevercloud::SyncChunk>{}
        << qevercloud::SyncChunkBuilder{}.setResources(resources).build();

    const auto durableResourcesProcessor =
        std::make_shared<DurableResourcesProcessor>(
            m_mockResourcesProcessor, QDir{m_temporaryDir.path()});

    EXPECT_CALL(*m_mockResourcesProcessor, processResources)
        .WillOnce([&](const QList<qevercloud::SyncChunk> & syncChunks,
                      const IResourcesProcessor::ICallbackWeakPtr &
                          callbackWeak) {
            const auto callback = callbackWeak.lock();
            EXPECT_TRUE(callback);

            const QList<qevercloud::Resource> syncChunkResources = [&] {
                QList<qevercloud::Resource> result;
                for (const auto & syncChunk: qAsConst(syncChunks)) {
                    result << utils::collectResourcesFromSyncChunk(syncChunk);
                }
                return result;
            }();

            EXPECT_EQ(syncChunkResources, resources);

            IDurableResourcesProcessor::DownloadResourcesStatus status;
            status.totalNewResources =
                static_cast<quint64>(syncChunkResources.size());

            for (const auto & resource: qAsConst(resources)) {
                status.processedResourceGuidsAndUsns[resource.guid().value()] =
                    resource.updateSequenceNum().value();

                if (callback) {
                    callback->onProcessedResource(
                        resource.guid().value(),
                        resource.updateSequenceNum().value());
                }
            }

            return threading::makeReadyFuture<
                IDurableResourcesProcessor::DownloadResourcesStatus>(
                std::move(status));
        });

    auto future = durableResourcesProcessor->processResources(syncChunks);
    ASSERT_TRUE(future.isFinished());

    ASSERT_EQ(future.resultCount(), 1);
    const auto status = future.result();

    EXPECT_EQ(status.totalNewResources, resources.size());
    ASSERT_EQ(status.processedResourceGuidsAndUsns.size(), resources.size());
    for (const auto & resource: qAsConst(resources)) {
        const auto it =
            status.processedResourceGuidsAndUsns.find(resource.guid().value());

        EXPECT_NE(it, status.processedResourceGuidsAndUsns.end());
        if (it != status.processedResourceGuidsAndUsns.end()) {
            EXPECT_EQ(it.value(), resource.updateSequenceNum().value());
        }
    }

    QDir lastSyncResourcesDir = [&] {
        QDir syncPersistentStorageDir{m_temporaryDir.path()};
        QDir lastSyncDataDir{syncPersistentStorageDir.absoluteFilePath(
            QStringLiteral("lastSyncData"))};
        return QDir{
            lastSyncDataDir.absoluteFilePath(QStringLiteral("resources"))};
    }();

    const auto processedResourcesInfo =
        utils::processedResourcesInfoFromLastSync(lastSyncResourcesDir);
    ASSERT_EQ(processedResourcesInfo.size(), resources.size());

    for (const auto it: qevercloud::toRange(qAsConst(processedResourcesInfo))) {
        const auto sit = status.processedResourceGuidsAndUsns.find(it.key());

        EXPECT_NE(sit, status.processedResourceGuidsAndUsns.end());
        if (sit != status.processedResourceGuidsAndUsns.end()) {
            EXPECT_EQ(sit.value(), it.value());
        }
    }
}

TEST_F(
    DurableResourcesProcessorTest,
    HandleDifferentCallbacksDuringSyncChunksProcessing)
{
    const auto resources = generateTestResources(1, 5);

    const auto syncChunks = QList<qevercloud::SyncChunk>{}
        << qevercloud::SyncChunkBuilder{}.setResources(resources).build();

    const auto durableResourcesProcessor =
        std::make_shared<DurableResourcesProcessor>(
            m_mockResourcesProcessor, QDir{m_temporaryDir.path()});

    EXPECT_CALL(*m_mockResourcesProcessor, processResources)
        .WillOnce([&](const QList<qevercloud::SyncChunk> & syncChunks,
                      const IResourcesProcessor::ICallbackWeakPtr &
                          callbackWeak) {
            const auto callback = callbackWeak.lock();
            EXPECT_TRUE(callback);

            const QList<qevercloud::Resource> syncChunkResources = [&] {
                QList<qevercloud::Resource> result;
                for (const auto & syncChunk: qAsConst(syncChunks)) {
                    result << utils::collectResourcesFromSyncChunk(syncChunk);
                }
                return result;
            }();

            EXPECT_EQ(syncChunkResources, resources);

            EXPECT_EQ(syncChunkResources.size(), 5);
            if (syncChunkResources.size() != 5) {
                return threading::makeExceptionalFuture<
                    IDurableResourcesProcessor::DownloadResourcesStatus>(
                    RuntimeError{ErrorString{"Invalid resource count"}});
            }

            IDurableResourcesProcessor::DownloadResourcesStatus status;
            status.totalNewResources =
                static_cast<quint64>(syncChunkResources.size());

            // First resource gets marked as a successfully processed one
            status.processedResourceGuidsAndUsns
                [syncChunkResources[0].guid().value()] =
                syncChunkResources[0].updateSequenceNum().value();

            if (callback) {
                callback->onProcessedResource(
                    *syncChunkResources[0].guid(),
                    *syncChunkResources[0].updateSequenceNum());
            }

            // Second resource is marked as failed to process one
            status.resourcesWhichFailedToProcess << IDurableResourcesProcessor::
                    DownloadResourcesStatus::ResourceWithException{
                        syncChunkResources[1],
                        std::make_shared<RuntimeError>(
                            ErrorString{"Failed to process resource"})};

            if (callback) {
                callback->onResourceFailedToProcess(
                    status.resourcesWhichFailedToProcess.last().resource,
                    *status.resourcesWhichFailedToProcess.last().exception);
            }

            // Third resource is marked as failed to download one
            status.resourcesWhichFailedToDownload
                << IDurableResourcesProcessor::DownloadResourcesStatus::
                       ResourceWithException{
                           syncChunkResources[2],
                           std::make_shared<RuntimeError>(
                               ErrorString{"Failed to download resource"})};

            if (callback) {
                callback->onResourceFailedToDownload(
                    status.resourcesWhichFailedToDownload.last().resource,
                    *status.resourcesWhichFailedToDownload.last().exception);
            }

            // Fourth and fifth resources are marked as cancelled because, for
            // example, the download error was API rate limit exceeding.
            for (int i = 3; i < 5; ++i) {
                status.cancelledResourceGuidsAndUsns
                    [syncChunkResources[i].guid().value()] =
                    syncChunkResources[i].updateSequenceNum().value();

                if (callback) {
                    callback->onResourceProcessingCancelled(
                        syncChunkResources[i]);
                }
            }

            return threading::makeReadyFuture<
                IDurableResourcesProcessor::DownloadResourcesStatus>(
                std::move(status));
        });

    auto future = durableResourcesProcessor->processResources(syncChunks);
    ASSERT_TRUE(future.isFinished());

    ASSERT_EQ(future.resultCount(), 1);
    const auto status = future.result();

    EXPECT_EQ(status.totalNewResources, resources.size());

    ASSERT_EQ(status.processedResourceGuidsAndUsns.size(), 1);
    EXPECT_EQ(
        status.processedResourceGuidsAndUsns.constBegin().key(),
        resources[0].guid().value());
    EXPECT_EQ(
        status.processedResourceGuidsAndUsns.constBegin().value(),
        resources[0].updateSequenceNum().value());

    ASSERT_EQ(status.resourcesWhichFailedToProcess.size(), 1);
    EXPECT_EQ(
        status.resourcesWhichFailedToProcess.constBegin()->resource,
        resources[1]);

    ASSERT_EQ(status.resourcesWhichFailedToDownload.size(), 1);
    EXPECT_EQ(
        status.resourcesWhichFailedToDownload.constBegin()->resource,
        resources[2]);

    ASSERT_EQ(status.cancelledResourceGuidsAndUsns.size(), 2);
    for (int i = 3; i < 5; ++i) {
        const auto it = status.cancelledResourceGuidsAndUsns.constFind(
            resources[i].guid().value());
        EXPECT_NE(it, status.cancelledResourceGuidsAndUsns.constEnd());

        if (it != status.cancelledResourceGuidsAndUsns.constEnd()) {
            EXPECT_EQ(it.value(), resources[i].updateSequenceNum().value());
        }
    }

    QDir lastSyncResourcesDir = [&] {
        QDir syncPersistentStorageDir{m_temporaryDir.path()};
        QDir lastSyncDataDir{syncPersistentStorageDir.absoluteFilePath(
            QStringLiteral("lastSyncData"))};
        return QDir{
            lastSyncDataDir.absoluteFilePath(QStringLiteral("resources"))};
    }();

    const auto processedResourcesInfo =
        utils::processedResourcesInfoFromLastSync(lastSyncResourcesDir);
    ASSERT_EQ(processedResourcesInfo.size(), 1);
    EXPECT_EQ(
        processedResourcesInfo.constBegin().key(), resources[0].guid().value());
    EXPECT_EQ(
        processedResourcesInfo.constBegin().value(),
        resources[0].updateSequenceNum().value());

    const auto failedToProcessResources =
        utils::resourcesWhichFailedToProcessDuringLastSync(
            lastSyncResourcesDir);
    ASSERT_EQ(failedToProcessResources.size(), 1);
    EXPECT_EQ(*failedToProcessResources.constBegin(), resources[1]);

    const auto failedToDownloadResources =
        utils::resourcesWhichFailedToDownloadDuringLastSync(
            lastSyncResourcesDir);
    ASSERT_EQ(failedToDownloadResources.size(), 1);
    EXPECT_EQ(*failedToDownloadResources.constBegin(), resources[2]);

    const auto cancelledResources = [&] {
        auto cancelledResources =
            utils::resourcesCancelledDuringLastSync(lastSyncResourcesDir);

        std::sort(
            cancelledResources.begin(), cancelledResources.end(),
            [](const qevercloud::Resource & lhs,
               const qevercloud::Resource & rhs) {
                return lhs.updateSequenceNum() < rhs.updateSequenceNum();
            });

        return cancelledResources;
    }();

    ASSERT_EQ(cancelledResources.size(), 2);
    for (int i = 3, j = 0; i < 5 && j < cancelledResources.size(); ++i, ++j) {
        EXPECT_EQ(cancelledResources[j], resources[i]);
    }
}

} // namespace quentier::synchronization::tests
