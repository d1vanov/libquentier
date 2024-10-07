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

#include "Utils.h"

#include <synchronization/processors/DurableResourcesProcessor.h>
#include <synchronization/processors/Utils.h>
#include <synchronization/sync_chunks/Utils.h>
#include <synchronization/tests/mocks/MockIResourcesProcessor.h>
#include <synchronization/types/DownloadResourcesStatus.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/RuntimeError.h>
#include <quentier/threading/Future.h>
#include <quentier/utility/FileSystem.h>
#include <quentier/utility/UidGenerator.h>
#include <quentier/utility/cancelers/ManualCanceler.h>

#include <qevercloud/IRequestContext.h>
#include <qevercloud/types/SyncChunk.h>
#include <qevercloud/types/builders/ResourceBuilder.h>
#include <qevercloud/types/builders/SyncChunkBuilder.h>
#include <qevercloud/utility/ToRange.h>

#include <QSettings>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <optional>
#include <utility>

// clazy:excludeall=non-pod-global-static
// clazy:excludeall=returning-void-expression

namespace quentier::synchronization::tests {

using testing::_;
using testing::Return;
using testing::StrictMock;

namespace {

[[nodiscard]] QList<qevercloud::Guid> generateTestGuids(const qint32 count)
{
    QList<qevercloud::Guid> result;
    result.reserve(count);
    for (qint32 i = 0; i < count; ++i) {
        result << UidGenerator::Generate();
    }

    return result;
}

const QList gTestGuidsSet1 = generateTestGuids(5);
const QList gTestGuidsSet2 = generateTestGuids(3);
const QList gTestGuidsSet3 = generateTestGuids(3);
const QList gTestGuidsSet4 = generateTestGuids(3);

[[nodiscard]] QList<qevercloud::Resource> generateTestResources(
    const QList<qevercloud::Guid> & resourceGuids, const qint32 startUsn)
{
    if (resourceGuids.isEmpty()) {
        return {};
    }

    const auto noteGuid = UidGenerator::Generate();

    QList<qevercloud::Resource> result;
    result.reserve(resourceGuids.size());
    qint32 usn = startUsn;
    for (const auto & resourceGuid: std::as_const(resourceGuids)) {
        result << qevercloud::ResourceBuilder{}
                      .setGuid(resourceGuid)
                      .setNoteGuid(noteGuid)
                      .setUpdateSequenceNum(usn)
                      .build();
        ++usn;
    }

    return result;
}

[[nodiscard]] QHash<qevercloud::Guid, qint32>
    generateTestProcessedResourcesInfo(
        const QList<qevercloud::Guid> & resourceGuids, const qint32 startUsn)
{
    QHash<qevercloud::Guid, qint32> result;
    result.reserve(resourceGuids.size());
    qint32 usn = startUsn;
    for (const auto & resourceGuid: std::as_const(resourceGuids)) {
        result[resourceGuid] = usn;
        ++usn;
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

        for (const auto & entry: std::as_const(entries)) {
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

    const utility::cancelers::ManualCancelerPtr m_manualCanceler =
        std::make_shared<utility::cancelers::ManualCanceler>();

    const qevercloud::IRequestContextPtr m_ctx =
        qevercloud::newRequestContext();

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
    const auto resources = generateTestResources(gTestGuidsSet1, 1);

    const auto syncChunks = QList<qevercloud::SyncChunk>{}
        << qevercloud::SyncChunkBuilder{}.setResources(resources).build();

    const auto durableResourcesProcessor =
        std::make_shared<DurableResourcesProcessor>(
            m_mockResourcesProcessor, QDir{m_temporaryDir.path()});

    EXPECT_CALL(*m_mockResourcesProcessor, processResources)
        .WillOnce([&](const QList<qevercloud::SyncChunk> & syncChunks,
                      const utility::cancelers::ICancelerPtr & canceler,
                      const qevercloud::IRequestContextPtr & ctx,
                      const IResourcesProcessor::ICallbackWeakPtr &
                          callbackWeak) {
            EXPECT_TRUE(canceler);
            EXPECT_EQ(canceler.get(), m_manualCanceler.get());
            EXPECT_EQ(ctx.get(), m_ctx.get());

            const auto callback = callbackWeak.lock();
            EXPECT_TRUE(callback);

            const QList<qevercloud::Resource> syncChunkResources = [&] {
                QList<qevercloud::Resource> result;
                for (const auto & syncChunk: std::as_const(syncChunks)) {
                    result << utils::collectResourcesFromSyncChunk(syncChunk);
                }
                return result;
            }();

            EXPECT_EQ(syncChunkResources, resources);

            auto status = std::make_shared<DownloadResourcesStatus>();
            status->m_totalNewResources =
                static_cast<quint64>(syncChunkResources.size());

            for (const auto & resource: std::as_const(resources)) {
                status
                    ->m_processedResourceGuidsAndUsns[resource.guid().value()] =
                    resource.updateSequenceNum().value();

                if (callback) {
                    callback->onProcessedResource(
                        resource.guid().value(),
                        resource.updateSequenceNum().value());
                }
            }

            return threading::makeReadyFuture<DownloadResourcesStatusPtr>(
                std::move(status));
        });

    auto future = durableResourcesProcessor->processResources(
        syncChunks, m_manualCanceler, m_ctx);

    waitForFuture(future);
    ASSERT_EQ(future.resultCount(), 1);
    const auto status = future.result();

    EXPECT_EQ(status->m_totalNewResources, resources.size());
    ASSERT_EQ(status->m_processedResourceGuidsAndUsns.size(), resources.size());
    for (const auto & resource: std::as_const(resources)) {
        const auto it = status->m_processedResourceGuidsAndUsns.find(
            resource.guid().value());

        EXPECT_NE(it, status->m_processedResourceGuidsAndUsns.end());
        if (it != status->m_processedResourceGuidsAndUsns.end()) {
            EXPECT_EQ(it.value(), resource.updateSequenceNum().value());
        }
    }

    QDir lastSyncResourcesDir = [&] {
        QDir syncPersistentStorageDir{m_temporaryDir.path()};
        QDir lastSyncDataDir{syncPersistentStorageDir.absoluteFilePath(
            QStringLiteral("last_sync_data"))};
        return QDir{
            lastSyncDataDir.absoluteFilePath(QStringLiteral("resources"))};
    }();

    const auto processedResourcesInfo =
        utils::processedResourcesInfoFromLastSync(lastSyncResourcesDir);
    ASSERT_EQ(processedResourcesInfo.size(), resources.size());

    for (const auto it:
         qevercloud::toRange(std::as_const(processedResourcesInfo)))
    {
        const auto sit = status->m_processedResourceGuidsAndUsns.find(it.key());

        EXPECT_NE(sit, status->m_processedResourceGuidsAndUsns.end());
        if (sit != status->m_processedResourceGuidsAndUsns.end()) {
            EXPECT_EQ(sit.value(), it.value());
        }
    }

    durableResourcesProcessor->cleanup();
    const auto emptyProcessedResourcesInfo =
        utils::processedResourcesInfoFromLastSync(lastSyncResourcesDir);
    EXPECT_TRUE(emptyProcessedResourcesInfo.isEmpty());
}

TEST_F(
    DurableResourcesProcessorTest,
    HandleDifferentCallbacksDuringSyncChunksProcessing)
{
    const auto resources = generateTestResources(gTestGuidsSet1, 1);

    const auto syncChunks = QList<qevercloud::SyncChunk>{}
        << qevercloud::SyncChunkBuilder{}.setResources(resources).build();

    const auto durableResourcesProcessor =
        std::make_shared<DurableResourcesProcessor>(
            m_mockResourcesProcessor, QDir{m_temporaryDir.path()});

    EXPECT_CALL(*m_mockResourcesProcessor, processResources)
        .WillOnce([&](const QList<qevercloud::SyncChunk> & syncChunks,
                      const utility::cancelers::ICancelerPtr & canceler,
                      const qevercloud::IRequestContextPtr & ctx,
                      const IResourcesProcessor::ICallbackWeakPtr &
                          callbackWeak) {
            EXPECT_TRUE(canceler);
            EXPECT_EQ(canceler.get(), m_manualCanceler.get());
            EXPECT_EQ(ctx.get(), m_ctx.get());

            const auto callback = callbackWeak.lock();
            EXPECT_TRUE(callback);

            const QList<qevercloud::Resource> syncChunkResources = [&] {
                QList<qevercloud::Resource> result;
                for (const auto & syncChunk: std::as_const(syncChunks)) {
                    result << utils::collectResourcesFromSyncChunk(syncChunk);
                }
                return result;
            }();

            EXPECT_EQ(syncChunkResources, resources);

            EXPECT_EQ(syncChunkResources.size(), 5);
            if (syncChunkResources.size() != 5) {
                return threading::makeExceptionalFuture<
                    DownloadResourcesStatusPtr>(
                    RuntimeError{ErrorString{"Invalid resource count"}});
            }

            auto status = std::make_shared<DownloadResourcesStatus>();
            status->m_totalNewResources =
                static_cast<quint64>(syncChunkResources.size());

            // First resource gets marked as a successfully processed one
            status->m_processedResourceGuidsAndUsns
                [syncChunkResources[0].guid().value()] =
                syncChunkResources[0].updateSequenceNum().value();

            if (callback) {
                callback->onProcessedResource(
                    *syncChunkResources[0].guid(),
                    *syncChunkResources[0].updateSequenceNum());
            }

            // Second resource is marked as failed to process one
            status->m_resourcesWhichFailedToProcess
                << DownloadResourcesStatus::ResourceWithException{
                       syncChunkResources[1],
                       std::make_shared<RuntimeError>(
                           ErrorString{"Failed to process resource"})};

            if (callback) {
                callback->onResourceFailedToProcess(
                    status->m_resourcesWhichFailedToProcess.last().first,
                    *status->m_resourcesWhichFailedToProcess.last().second);
            }

            // Third resource is marked as failed to download one
            status->m_resourcesWhichFailedToDownload
                << DownloadResourcesStatus::ResourceWithException{
                       syncChunkResources[2],
                       std::make_shared<RuntimeError>(
                           ErrorString{"Failed to download resource"})};

            if (callback) {
                callback->onResourceFailedToDownload(
                    status->m_resourcesWhichFailedToDownload.last().first,
                    *status->m_resourcesWhichFailedToDownload.last().second);
            }

            // Fourth and fifth resources are marked as cancelled because,
            // for example, the download error was API rate limit exceeding.
            for (int i = 3; i < 5; ++i) {
                status->m_cancelledResourceGuidsAndUsns
                    [syncChunkResources[i].guid().value()] =
                    syncChunkResources[i].updateSequenceNum().value();

                if (callback) {
                    callback->onResourceProcessingCancelled(
                        syncChunkResources[i]);
                }
            }

            return threading::makeReadyFuture<DownloadResourcesStatusPtr>(
                std::move(status));
        });

    auto future = durableResourcesProcessor->processResources(
        syncChunks, m_manualCanceler, m_ctx);

    waitForFuture(future);

    ASSERT_EQ(future.resultCount(), 1);
    const auto status = future.result();

    EXPECT_EQ(status->m_totalNewResources, resources.size());

    ASSERT_EQ(status->m_processedResourceGuidsAndUsns.size(), 1);
    EXPECT_EQ(
        status->m_processedResourceGuidsAndUsns.constBegin().key(),
        resources[0].guid().value());
    EXPECT_EQ(
        status->m_processedResourceGuidsAndUsns.constBegin().value(),
        resources[0].updateSequenceNum().value());

    ASSERT_EQ(status->m_resourcesWhichFailedToProcess.size(), 1);
    EXPECT_EQ(
        status->m_resourcesWhichFailedToProcess.constBegin()->first,
        resources[1]);

    ASSERT_EQ(status->m_resourcesWhichFailedToDownload.size(), 1);
    EXPECT_EQ(
        status->m_resourcesWhichFailedToDownload.constBegin()->first,
        resources[2]);

    ASSERT_EQ(status->m_cancelledResourceGuidsAndUsns.size(), 2);
    for (int i = 3; i < 5; ++i) {
        const auto it = status->m_cancelledResourceGuidsAndUsns.constFind(
            resources[i].guid().value());
        EXPECT_NE(it, status->m_cancelledResourceGuidsAndUsns.constEnd());

        if (it != status->m_cancelledResourceGuidsAndUsns.constEnd()) {
            EXPECT_EQ(it.value(), resources[i].updateSequenceNum().value());
        }
    }

    QDir lastSyncResourcesDir = [&] {
        QDir syncPersistentStorageDir{m_temporaryDir.path()};
        QDir lastSyncDataDir{syncPersistentStorageDir.absoluteFilePath(
            QStringLiteral("last_sync_data"))};
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

struct PreviousResourceSyncTestData
{
    QList<qevercloud::Resource> m_resourcesToProcess;

    QHash<qevercloud::Guid, qint32> m_processedResourcesInfo = {};

    QList<qevercloud::Resource>
        m_resourcesWhichFailedToDownloadDuringPreviousSync = {};

    QList<qevercloud::Resource>
        m_resourcesWhichFailedToProcessDuringPreviousSync = {};

    QList<qevercloud::Resource> m_resourcesCancelledDuringPreviousSync = {};
};

class DurableResourcesProcessorTestWithPreviousSyncData :
    public DurableResourcesProcessorTest,
    public testing::WithParamInterface<PreviousResourceSyncTestData>
{};

const std::array gTestData{
    PreviousResourceSyncTestData{
        generateTestResources(gTestGuidsSet1, 14), // m_resourcesToProcess
    },
    PreviousResourceSyncTestData{
        generateTestResources(gTestGuidsSet1, 14), // m_resourcesToProcess
        generateTestProcessedResourcesInfo(
            gTestGuidsSet1, 1), // m_processedResourcesInfo
    },
    PreviousResourceSyncTestData{
        generateTestResources(gTestGuidsSet1, 14), // m_resourcesToProcess
        generateTestProcessedResourcesInfo(
            gTestGuidsSet1, 1), // m_processedResourcesInfo
        generateTestResources(
            gTestGuidsSet2,
            5), // m_resourcesWhichFailedToDownloadDuringPreviousSync
    },
    PreviousResourceSyncTestData{
        generateTestResources(gTestGuidsSet1, 14), // m_resourcesToProcess
        generateTestProcessedResourcesInfo(
            gTestGuidsSet1, 1), // m_processedResourcesInfo
        generateTestResources(
            gTestGuidsSet2,
            5), // m_resourcesWhichFailedToDownloadDuringPreviousSync
        generateTestResources(
            gTestGuidsSet3,
            8), // m_resourcesWhichFailedToProcessDuringPreviousSync
    },
    PreviousResourceSyncTestData{
        generateTestResources(gTestGuidsSet1, 14), // m_resourcesToProcess
        generateTestProcessedResourcesInfo(
            gTestGuidsSet1, 1), // m_processedResourcesInfo
        generateTestResources(
            gTestGuidsSet2,
            5), // m_resourcesWhichFailedToDownloadDuringPreviousSync
        generateTestResources(
            gTestGuidsSet3,
            8), // m_resourcesWhichFailedToProcessDuringPreviousSync
        generateTestResources(
            gTestGuidsSet4, 11), // m_resourcesCancelledDuringPreviousSync
    },
    PreviousResourceSyncTestData{
        {}, // m_resourcesToProcess
        generateTestProcessedResourcesInfo(
            gTestGuidsSet1, 1), // m_processedResourcesInfo
        generateTestResources(
            gTestGuidsSet2,
            5), // m_resourcesWhichFailedToDownloadDuringPreviousSync
        generateTestResources(
            gTestGuidsSet3,
            8), // m_resourcesWhichFailedToProcessDuringPreviousSync
        generateTestResources(
            gTestGuidsSet4, 11), // m_resourcesCancelledDuringPreviousSync
    },
    PreviousResourceSyncTestData{
        {}, // m_resourcesToProcess
        {}, // m_processedResourcesInfo
        generateTestResources(
            gTestGuidsSet2,
            5), // m_resourcesWhichFailedToDownloadDuringPreviousSync
        generateTestResources(
            gTestGuidsSet3,
            8), // m_resourcesWhichFailedToProcessDuringPreviousSync
        generateTestResources(
            gTestGuidsSet4, 11), // m_resourcesCancelledDuringPreviousSync
    },
    PreviousResourceSyncTestData{
        {}, // m_resourcesToProcess
        {}, // m_processedResourcesInfo
        {}, // m_resourcesWhichFailedToDownloadDuringPreviousSync
        generateTestResources(
            gTestGuidsSet3,
            8), // m_resourcesWhichFailedToProcessDuringPreviousSync
        generateTestResources(
            gTestGuidsSet4, 11), // m_resourcesCancelledDuringPreviousSync
    },
    PreviousResourceSyncTestData{
        {}, // m_resourcesToProcess
        {}, // m_processedResourcesInfo
        {}, // m_resourcesWhichFailedToDownloadDuringPreviousSync
        {}, // m_resourcesWhichFailedToProcessDuringPreviousSync
        generateTestResources(
            gTestGuidsSet4, 11), // m_resourcesCancelledDuringPreviousSync
    },
};

INSTANTIATE_TEST_SUITE_P(
    DurableResourcesProcessorTestWithPreviousSyncDataInstance,
    DurableResourcesProcessorTestWithPreviousSyncData,
    testing::ValuesIn(gTestData));

TEST_P(
    DurableResourcesProcessorTestWithPreviousSyncData,
    ProcessSyncChunksWithPreviousSyncInfo)
{
    const auto & testData = GetParam();
    const auto & resources = testData.m_resourcesToProcess;

    const auto syncChunks = QList<qevercloud::SyncChunk>{}
        << qevercloud::SyncChunkBuilder{}.setResources(resources).build();

    QDir syncPersistentStorageDir{m_temporaryDir.path()};

    QDir syncResourcesDir = [&] {
        QDir lastSyncDataDir{syncPersistentStorageDir.absoluteFilePath(
            QStringLiteral("last_sync_data"))};

        return QDir{
            lastSyncDataDir.absoluteFilePath(QStringLiteral("resources"))};
    }();

    // Prepare test data
    if (!testData.m_processedResourcesInfo.isEmpty()) {
        for (const auto it:
             qevercloud::toRange(testData.m_processedResourcesInfo))
        {
            utils::writeProcessedResourceInfo(
                it.key(), it.value(), syncResourcesDir);
        }
    }

    if (!testData.m_resourcesWhichFailedToDownloadDuringPreviousSync.isEmpty())
    {
        for (const auto & resource: std::as_const(
                 testData.m_resourcesWhichFailedToDownloadDuringPreviousSync))
        {
            utils::writeFailedToDownloadResource(resource, syncResourcesDir);
        }
    }

    if (!testData.m_resourcesWhichFailedToProcessDuringPreviousSync.isEmpty()) {
        for (const auto & resource: std::as_const(
                 testData.m_resourcesWhichFailedToProcessDuringPreviousSync))
        {
            utils::writeFailedToProcessResource(resource, syncResourcesDir);
        }
    }

    if (!testData.m_resourcesCancelledDuringPreviousSync.isEmpty()) {
        for (const auto & resource:
             std::as_const(testData.m_resourcesCancelledDuringPreviousSync))
        {
            utils::writeCancelledResource(resource, syncResourcesDir);
        }
    }

    const QList<qevercloud::Resource> resourcesFromPreviousSync = [&] {
        QList<qevercloud::Resource> result;
        result << testData.m_resourcesWhichFailedToDownloadDuringPreviousSync;
        result << testData.m_resourcesWhichFailedToProcessDuringPreviousSync;
        result << testData.m_resourcesCancelledDuringPreviousSync;

        for (auto it = result.begin(); it != result.end();) {
            EXPECT_TRUE(it->guid().has_value());
            if (it->guid().has_value()) {
                const auto pit =
                    testData.m_processedResourcesInfo.find(*it->guid());

                if (pit != testData.m_processedResourcesInfo.end() &&
                    it->updateSequenceNum() == pit.value())
                {
                    it = result.erase(it);
                    continue;
                }
            }

            ++it;
        }

        return result;
    }();

    DownloadResourcesStatus currentResourcesStatus;
    currentResourcesStatus.m_totalNewResources =
        static_cast<quint64>(std::max<qsizetype>(resources.size(), 0));
    for (const auto & resource: std::as_const(resources)) {
        EXPECT_TRUE(resource.guid());
        if (!resource.guid()) {
            continue;
        }

        EXPECT_TRUE(resource.updateSequenceNum());
        if (!resource.updateSequenceNum()) {
            continue;
        }

        currentResourcesStatus
            .m_processedResourceGuidsAndUsns[*resource.guid()] =
            *resource.updateSequenceNum();
    }

    EXPECT_CALL(
        *m_mockResourcesProcessor, processResources(syncChunks, _, _, _))
        .WillOnce(Return(threading::makeReadyFuture<DownloadResourcesStatusPtr>(
            std::make_shared<DownloadResourcesStatus>(
                currentResourcesStatus))));

    std::optional<DownloadResourcesStatus> previousResourcesStatus;
    if (!resourcesFromPreviousSync.isEmpty()) {
        const auto expectedSyncChunks = QList<qevercloud::SyncChunk>{}
            << qevercloud::SyncChunkBuilder{}
                   .setResources(resourcesFromPreviousSync)
                   .build();

        previousResourcesStatus.emplace();
        previousResourcesStatus->m_totalUpdatedResources = static_cast<quint64>(
            std::max<qsizetype>(resourcesFromPreviousSync.size(), 0));

        for (const auto & resource: std::as_const(resourcesFromPreviousSync)) {
            EXPECT_TRUE(resource.guid());
            if (!resource.guid()) {
                continue;
            }

            EXPECT_TRUE(resource.updateSequenceNum());
            if (!resource.updateSequenceNum()) {
                continue;
            }

            previousResourcesStatus
                ->m_processedResourceGuidsAndUsns[*resource.guid()] =
                *resource.updateSequenceNum();
        }

        EXPECT_CALL(
            *m_mockResourcesProcessor,
            processResources(
                testing::MatcherCast<const QList<qevercloud::SyncChunk> &>(
                    EqSyncChunksWithSortedResources(expectedSyncChunks)),
                _, _, _))
            .WillOnce([&](const QList<qevercloud::SyncChunk> & syncChunks,
                          const utility::cancelers::ICancelerPtr & canceler,
                          const qevercloud::IRequestContextPtr & ctx,
                          const IResourcesProcessor::ICallbackWeakPtr &
                              callbackWeak) {
                EXPECT_TRUE(canceler);
                EXPECT_EQ(canceler.get(), m_manualCanceler.get());
                EXPECT_EQ(ctx.get(), m_ctx.get());

                const auto callback = callbackWeak.lock();
                EXPECT_TRUE(callback);
                if (callback) {
                    for (const auto & syncChunk: std::as_const(syncChunks)) {
                        if (!syncChunk.resources()) {
                            continue;
                        }

                        for (const auto & resource: *syncChunk.resources()) {
                            EXPECT_TRUE(resource.guid());
                            if (!resource.guid()) {
                                continue;
                            }

                            EXPECT_TRUE(resource.updateSequenceNum());
                            if (!resource.updateSequenceNum()) {
                                continue;
                            }

                            callback->onProcessedResource(
                                *resource.guid(),
                                *resource.updateSequenceNum());
                        }
                    }
                }

                return threading::makeReadyFuture<DownloadResourcesStatusPtr>(
                    std::make_shared<DownloadResourcesStatus>(
                        *previousResourcesStatus));
            });
    }

    const auto durableResourcesProcessor =
        std::make_shared<DurableResourcesProcessor>(
            m_mockResourcesProcessor, QDir{m_temporaryDir.path()});

    auto future = durableResourcesProcessor->processResources(
        syncChunks, m_manualCanceler, m_ctx);

    waitForFuture(future);
    ASSERT_EQ(future.resultCount(), 1);
    const auto status = future.result();
    ASSERT_TRUE(status);

    const DownloadResourcesStatus expectedStatus = [&] {
        DownloadResourcesStatus expectedStatus;
        if (previousResourcesStatus) {
            expectedStatus = utils::mergeDownloadResourcesStatuses(
                std::move(expectedStatus), *previousResourcesStatus);
        }

        return utils::mergeDownloadResourcesStatuses(
            std::move(expectedStatus), currentResourcesStatus);
    }();

    EXPECT_EQ(*status, expectedStatus);

    const auto processedResourcesInfo =
        utils::processedResourcesInfoFromLastSync(syncResourcesDir);

    const auto expectedProcessedResourcesInfo = [&] {
        QHash<qevercloud::Guid, qint32> result;

        if (!testData.m_processedResourcesInfo.isEmpty()) {
            for (const auto it:
                 qevercloud::toRange(testData.m_processedResourcesInfo))
            {
                result.insert(it.key(), it.value());
            }
        }

        const auto appendResources =
            [&result](const QList<qevercloud::Resource> & resources) {
                if (resources.isEmpty()) {
                    return;
                }

                for (const auto & resource: std::as_const(resources)) {
                    EXPECT_TRUE(resource.guid());
                    if (Q_UNLIKELY(!resource.guid())) {
                        continue;
                    }

                    EXPECT_TRUE(resource.updateSequenceNum());
                    if (Q_UNLIKELY(!resource.updateSequenceNum())) {
                        continue;
                    }

                    result[*resource.guid()] = *resource.updateSequenceNum();
                }
            };

        appendResources(
            testData.m_resourcesWhichFailedToDownloadDuringPreviousSync);

        appendResources(
            testData.m_resourcesWhichFailedToProcessDuringPreviousSync);

        appendResources(testData.m_resourcesCancelledDuringPreviousSync);
        return result;
    }();

    EXPECT_EQ(processedResourcesInfo, expectedProcessedResourcesInfo);
}

} // namespace quentier::synchronization::tests
