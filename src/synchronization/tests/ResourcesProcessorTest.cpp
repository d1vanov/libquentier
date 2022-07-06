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

#include <synchronization/processors/ResourcesProcessor.h>
#include <synchronization/tests/mocks/MockIResourceFullDataDownloader.h>
#include <synchronization/types/DownloadResourcesStatus.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/local_storage/tests/mocks/MockILocalStorage.h>
#include <quentier/threading/Future.h>
#include <quentier/utility/UidGenerator.h>

#include <qevercloud/exceptions/builders/EDAMSystemExceptionBuilder.h>
#include <qevercloud/types/builders/DataBuilder.h>
#include <qevercloud/types/builders/NoteBuilder.h>
#include <qevercloud/types/builders/ResourceBuilder.h>
#include <qevercloud/types/builders/SyncChunkBuilder.h>

#include <QCoreApplication>
#include <QCryptographicHash>

#include <gtest/gtest.h>

// clazy:excludeall=non-pod-global-static
// clazy:excludeall=returning-void-expression

namespace quentier::synchronization::tests {

using testing::Return;
using testing::StrictMock;

class ResourcesProcessorTest : public testing::Test
{
protected:
    const std::shared_ptr<local_storage::tests::mocks::MockILocalStorage>
        m_mockLocalStorage = std::make_shared<
            StrictMock<local_storage::tests::mocks::MockILocalStorage>>();

    const std::shared_ptr<mocks::MockIResourceFullDataDownloader>
        m_mockResourceFullDataDownloader = std::make_shared<
            StrictMock<mocks::MockIResourceFullDataDownloader>>();
};

struct ResourcesProcessorCallback final : public IResourcesProcessor::ICallback
{
    void onProcessedResource(
        const qevercloud::Guid & resourceGuid,
        qint32 resourceUpdateSequenceNum) noexcept override
    {
        m_processedResourceGuidsAndUsns[resourceGuid] =
            resourceUpdateSequenceNum;
    }

    void onResourceFailedToDownload(
        const qevercloud::Resource & resource,
        const QException & e) noexcept override
    {
        m_resourcesWhichFailedToDownload
            << std::make_pair(resource, std::shared_ptr<QException>(e.clone()));
    }

    void onResourceFailedToProcess(
        const qevercloud::Resource & resource,
        const QException & e) noexcept override
    {
        m_resourcesWhichFailedToProcess
            << std::make_pair(resource, std::shared_ptr<QException>(e.clone()));
    }

    void onResourceProcessingCancelled(
        const qevercloud::Resource & resource) noexcept override
    {
        m_cancelledResources << resource;
    }

    using ResourceWithException =
        std::pair<qevercloud::Resource, std::shared_ptr<QException>>;

    QHash<qevercloud::Guid, qint32> m_processedResourceGuidsAndUsns;
    QList<ResourceWithException> m_resourcesWhichFailedToDownload;
    QList<ResourceWithException> m_resourcesWhichFailedToProcess;
    QList<qevercloud::Resource> m_cancelledResources;
};

TEST_F(ResourcesProcessorTest, Ctor)
{
    EXPECT_NO_THROW(
        const auto resourcesProcessor = std::make_shared<ResourcesProcessor>(
            m_mockLocalStorage, m_mockResourceFullDataDownloader));
}

TEST_F(ResourcesProcessorTest, CtorNullLocalStorage)
{
    EXPECT_THROW(
        const auto resourcesProcessor = std::make_shared<ResourcesProcessor>(
            nullptr, m_mockResourceFullDataDownloader),
        InvalidArgument);
}

TEST_F(ResourcesProcessorTest, CtorNullResourceFullDataDownloader)
{
    EXPECT_THROW(
        const auto resourcesProcessor =
            std::make_shared<ResourcesProcessor>(m_mockLocalStorage, nullptr),
        InvalidArgument);
}

TEST_F(ResourcesProcessorTest, ProcessSyncChunksWithoutResourcesToProcess)
{
    const auto syncChunks = QList<qevercloud::SyncChunk>{}
        << qevercloud::SyncChunkBuilder{}.build();

    const auto resourcesProcessor = std::make_shared<ResourcesProcessor>(
        m_mockLocalStorage, m_mockResourceFullDataDownloader);

    const auto callback = std::make_shared<ResourcesProcessorCallback>();

    auto future = resourcesProcessor->processResources(syncChunks, callback);
    ASSERT_TRUE(future.isFinished());
    EXPECT_NO_THROW(future.waitForFinished());

    ASSERT_EQ(future.resultCount(), 1);
    const auto status = future.result();
    EXPECT_EQ(status->m_totalNewResources, 0UL);
    EXPECT_EQ(status->m_totalUpdatedResources, 0UL);
    EXPECT_TRUE(status->m_resourcesWhichFailedToDownload.isEmpty());
    EXPECT_TRUE(status->m_resourcesWhichFailedToProcess.isEmpty());
    EXPECT_TRUE(status->m_processedResourceGuidsAndUsns.isEmpty());
    EXPECT_TRUE(status->m_cancelledResourceGuidsAndUsns.isEmpty());

    EXPECT_TRUE(callback->m_resourcesWhichFailedToDownload.isEmpty());
    EXPECT_TRUE(callback->m_resourcesWhichFailedToProcess.isEmpty());
    EXPECT_TRUE(callback->m_processedResourceGuidsAndUsns.isEmpty());
    EXPECT_TRUE(callback->m_cancelledResources.isEmpty());
}

TEST_F(ResourcesProcessorTest, ProcessResourcesWithoutConflicts)
{
    const auto noteGuid = UidGenerator::Generate();

    const auto resources = QList<qevercloud::Resource>{}
        << qevercloud::ResourceBuilder{}
               .setGuid(UidGenerator::Generate())
               .setNoteGuid(noteGuid)
               .setUpdateSequenceNum(1)
               .build()
        << qevercloud::ResourceBuilder{}
               .setGuid(UidGenerator::Generate())
               .setNoteGuid(noteGuid)
               .setUpdateSequenceNum(2)
               .build()
        << qevercloud::ResourceBuilder{}
               .setGuid(UidGenerator::Generate())
               .setNoteGuid(noteGuid)
               .setUpdateSequenceNum(3)
               .build()
        << qevercloud::ResourceBuilder{}
               .setGuid(UidGenerator::Generate())
               .setNoteGuid(noteGuid)
               .setUpdateSequenceNum(4)
               .build();

    QList<qevercloud::Resource> resourcesPutIntoLocalStorage;
    QSet<qevercloud::Guid> triedGuids;

    const auto addDataToResource = [&](qevercloud::Resource resource,
                                       const int index) {
        auto dataBody = QString::fromUtf8("Resource #%1").arg(index).toUtf8();
        const auto size = dataBody.size();
        auto hash = QCryptographicHash::hash(dataBody, QCryptographicHash::Md5);

        resource.setData(qevercloud::DataBuilder{}
                             .setBody(std::move(dataBody))
                             .setSize(size)
                             .setBodyHash(std::move(hash))
                             .build());
        return resource;
    };

    EXPECT_CALL(*m_mockLocalStorage, findResourceByGuid)
        .WillRepeatedly(
            [&](const qevercloud::Guid & guid,
                const local_storage::ILocalStorage::FetchResourceOptions
                    fetchResourceOptions) {
                using FetchResourceOptions =
                    local_storage::ILocalStorage::FetchResourceOptions;

                EXPECT_EQ(fetchResourceOptions, FetchResourceOptions{});

                EXPECT_FALSE(triedGuids.contains(guid));
                triedGuids.insert(guid);

                const auto it = std::find_if(
                    resourcesPutIntoLocalStorage.constBegin(),
                    resourcesPutIntoLocalStorage.constEnd(),
                    [&](const qevercloud::Resource & resource) {
                        return resource.guid() && (*resource.guid() == guid);
                    });
                if (it != resourcesPutIntoLocalStorage.constEnd()) {
                    return threading::makeReadyFuture<
                        std::optional<qevercloud::Resource>>(*it);
                }

                return threading::makeReadyFuture<
                    std::optional<qevercloud::Resource>>(std::nullopt);
            });

    EXPECT_CALL(*m_mockResourceFullDataDownloader, downloadFullResourceData)
        .WillRepeatedly([&](qevercloud::Guid resourceGuid,
                            qevercloud::IRequestContextPtr ctx) { // NOLINT
            Q_UNUSED(ctx)

            const auto it = std::find_if(
                resources.begin(), resources.end(),
                [&](const qevercloud::Resource & resource) {
                    return resource.guid() &&
                        (*resource.guid() == resourceGuid);
                });
            if (Q_UNLIKELY(it == resources.end())) {
                return threading::makeExceptionalFuture<qevercloud::Resource>(
                    RuntimeError{ErrorString{
                        "Detected attempt to download unrecognized resource"}});
            }

            const int index =
                static_cast<int>(std::distance(resources.begin(), it));

            return threading::makeReadyFuture<qevercloud::Resource>(
                addDataToResource(*it, index));
        });

    EXPECT_CALL(*m_mockLocalStorage, putResource)
        .WillRepeatedly([&](const qevercloud::Resource & resource) {
            if (Q_UNLIKELY(!resource.guid())) {
                return threading::makeExceptionalFuture<void>(RuntimeError{
                    ErrorString{"Detected resource without guid"}});
            }

            EXPECT_TRUE(triedGuids.contains(*resource.guid()));

            resourcesPutIntoLocalStorage << resource;
            return threading::makeReadyFuture();
        });

    const auto syncChunks = QList<qevercloud::SyncChunk>{}
        << qevercloud::SyncChunkBuilder{}.setResources(resources).build();

    const auto resourcesProcessor = std::make_shared<ResourcesProcessor>(
        m_mockLocalStorage, m_mockResourceFullDataDownloader);

    const auto callback = std::make_shared<ResourcesProcessorCallback>();

    auto future = resourcesProcessor->processResources(syncChunks, callback);
    ASSERT_TRUE(future.isFinished());
    EXPECT_NO_THROW(future.waitForFinished());

    ASSERT_EQ(resourcesPutIntoLocalStorage.size(), resources.size());
    for (int i = 0, size = resources.size(); i < size; ++i) {
        const auto resourceWithData = addDataToResource(resources[i], i);
        EXPECT_EQ(resourcesPutIntoLocalStorage[i], resourceWithData);
    }

    ASSERT_EQ(future.resultCount(), 1);
    const auto status = future.result();
    EXPECT_EQ(
        status->m_totalNewResources, static_cast<quint64>(resources.size()));
    EXPECT_EQ(status->m_totalUpdatedResources, 0UL);
    EXPECT_TRUE(status->m_resourcesWhichFailedToDownload.isEmpty());
    EXPECT_TRUE(status->m_resourcesWhichFailedToProcess.isEmpty());
    EXPECT_TRUE(status->m_cancelledResourceGuidsAndUsns.isEmpty());

    ASSERT_EQ(status->m_processedResourceGuidsAndUsns.size(), resources.size());

    for (const auto & resource: qAsConst(resources)) {
        const auto it = status->m_processedResourceGuidsAndUsns.find(
            resource.guid().value());

        ASSERT_NE(it, status->m_processedResourceGuidsAndUsns.end());
        EXPECT_EQ(it.value(), resource.updateSequenceNum().value());
    }

    EXPECT_TRUE(callback->m_resourcesWhichFailedToDownload.isEmpty());
    EXPECT_TRUE(callback->m_resourcesWhichFailedToProcess.isEmpty());
    EXPECT_TRUE(callback->m_cancelledResources.isEmpty());

    ASSERT_EQ(
        callback->m_processedResourceGuidsAndUsns.size(), resources.size());

    for (const auto & resource: qAsConst(resources)) {
        const auto it = callback->m_processedResourceGuidsAndUsns.find(
            resource.guid().value());

        ASSERT_NE(it, callback->m_processedResourceGuidsAndUsns.end());
        EXPECT_EQ(it.value(), resource.updateSequenceNum().value());
    }
}

TEST_F(ResourcesProcessorTest, TolerateFailuresToDownloadFullResourceData)
{
    const auto noteGuid = UidGenerator::Generate();

    const auto resources = QList<qevercloud::Resource>{}
        << qevercloud::ResourceBuilder{}
               .setGuid(UidGenerator::Generate())
               .setNoteGuid(noteGuid)
               .setUpdateSequenceNum(1)
               .build()
        << qevercloud::ResourceBuilder{}
               .setGuid(UidGenerator::Generate())
               .setNoteGuid(noteGuid)
               .setUpdateSequenceNum(2)
               .build()
        << qevercloud::ResourceBuilder{}
               .setGuid(UidGenerator::Generate())
               .setNoteGuid(noteGuid)
               .setUpdateSequenceNum(3)
               .build()
        << qevercloud::ResourceBuilder{}
               .setGuid(UidGenerator::Generate())
               .setNoteGuid(noteGuid)
               .setUpdateSequenceNum(4)
               .build();

    QList<qevercloud::Resource> resourcesPutIntoLocalStorage;
    QSet<qevercloud::Guid> triedGuids;

    const auto addDataToResource = [&](qevercloud::Resource resource,
                                       const int index) {
        auto dataBody = QString::fromUtf8("Resource #%1").arg(index).toUtf8();
        const auto size = dataBody.size();
        auto hash = QCryptographicHash::hash(dataBody, QCryptographicHash::Md5);

        resource.setData(qevercloud::DataBuilder{}
                             .setBody(std::move(dataBody))
                             .setSize(size)
                             .setBodyHash(std::move(hash))
                             .build());
        return resource;
    };

    EXPECT_CALL(*m_mockLocalStorage, findResourceByGuid)
        .WillRepeatedly(
            [&](const qevercloud::Guid & guid,
                const local_storage::ILocalStorage::FetchResourceOptions
                    fetchResourceOptions) {
                using FetchResourceOptions =
                    local_storage::ILocalStorage::FetchResourceOptions;

                EXPECT_EQ(fetchResourceOptions, FetchResourceOptions{});

                EXPECT_FALSE(triedGuids.contains(guid));
                triedGuids.insert(guid);

                const auto it = std::find_if(
                    resourcesPutIntoLocalStorage.constBegin(),
                    resourcesPutIntoLocalStorage.constEnd(),
                    [&](const qevercloud::Resource & resource) {
                        return resource.guid() && (*resource.guid() == guid);
                    });
                if (it != resourcesPutIntoLocalStorage.constEnd()) {
                    return threading::makeReadyFuture<
                        std::optional<qevercloud::Resource>>(*it);
                }

                return threading::makeReadyFuture<
                    std::optional<qevercloud::Resource>>(std::nullopt);
            });

    EXPECT_CALL(*m_mockResourceFullDataDownloader, downloadFullResourceData)
        .WillRepeatedly([&](qevercloud::Guid resourceGuid,
                            qevercloud::IRequestContextPtr ctx) { // NOLINT
            Q_UNUSED(ctx)

            const auto it = std::find_if(
                resources.begin(), resources.end(),
                [&](const qevercloud::Resource & resource) {
                    return resource.guid() &&
                        (*resource.guid() == resourceGuid);
                });
            if (Q_UNLIKELY(it == resources.end())) {
                return threading::makeExceptionalFuture<qevercloud::Resource>(
                    RuntimeError{ErrorString{
                        "Detected attempt to download unrecognized resource"}});
            }

            if (it->updateSequenceNum().value() == 2) {
                return threading::makeExceptionalFuture<qevercloud::Resource>(
                    RuntimeError{
                        ErrorString{"Failed to download full resource data"}});
            }

            const int index =
                static_cast<int>(std::distance(resources.begin(), it));

            return threading::makeReadyFuture<qevercloud::Resource>(
                addDataToResource(*it, index));
        });

    EXPECT_CALL(*m_mockLocalStorage, putResource)
        .WillRepeatedly([&](const qevercloud::Resource & resource) {
            if (Q_UNLIKELY(!resource.guid())) {
                return threading::makeExceptionalFuture<void>(RuntimeError{
                    ErrorString{"Detected resource without guid"}});
            }

            EXPECT_TRUE(triedGuids.contains(*resource.guid()));

            resourcesPutIntoLocalStorage << resource;
            return threading::makeReadyFuture();
        });

    const auto syncChunks = QList<qevercloud::SyncChunk>{}
        << qevercloud::SyncChunkBuilder{}.setResources(resources).build();

    const auto resourcesProcessor = std::make_shared<ResourcesProcessor>(
        m_mockLocalStorage, m_mockResourceFullDataDownloader);

    const auto callback = std::make_shared<ResourcesProcessorCallback>();

    auto future = resourcesProcessor->processResources(syncChunks, callback);
    ASSERT_TRUE(future.isFinished());
    EXPECT_NO_THROW(future.waitForFinished());

    const QList<qevercloud::Resource> expectedProcessedResources = [&] {
        auto r = resources;
        int i = 0;
        for (auto & resource: r) {
            resource = addDataToResource(resource, i);
            ++i;
        }

        r.removeAt(1);
        return r;
    }();

    EXPECT_EQ(resourcesPutIntoLocalStorage, expectedProcessedResources);

    ASSERT_EQ(future.resultCount(), 1);
    const auto status = future.result();
    EXPECT_EQ(
        status->m_totalNewResources, static_cast<quint64>(resources.size()));
    EXPECT_EQ(status->m_totalUpdatedResources, 0UL);
    EXPECT_TRUE(status->m_resourcesWhichFailedToProcess.isEmpty());
    EXPECT_TRUE(status->m_cancelledResourceGuidsAndUsns.isEmpty());

    ASSERT_EQ(status->m_resourcesWhichFailedToDownload.size(), 1);
    EXPECT_EQ(status->m_resourcesWhichFailedToDownload[0].first, resources[1]);

    ASSERT_EQ(
        status->m_processedResourceGuidsAndUsns.size(), resources.size() - 1);

    for (const auto & resource: qAsConst(resources)) {
        if (resource.updateSequenceNum().value() == 2) {
            continue;
        }

        const auto it = status->m_processedResourceGuidsAndUsns.find(
            resource.guid().value());

        ASSERT_NE(it, status->m_processedResourceGuidsAndUsns.end());
        EXPECT_EQ(it.value(), resource.updateSequenceNum().value());
    }

    EXPECT_TRUE(callback->m_resourcesWhichFailedToProcess.isEmpty());
    EXPECT_TRUE(callback->m_cancelledResources.isEmpty());

    ASSERT_EQ(callback->m_resourcesWhichFailedToDownload.size(), 1);
    EXPECT_EQ(
        callback->m_resourcesWhichFailedToDownload.constBegin()->first,
        resources[1]);

    ASSERT_EQ(
        callback->m_processedResourceGuidsAndUsns.size(), resources.size() - 1);

    for (const auto & resource: qAsConst(resources)) {
        if (resource.updateSequenceNum().value() == 2) {
            continue;
        }

        const auto it = callback->m_processedResourceGuidsAndUsns.find(
            resource.guid().value());

        ASSERT_NE(it, callback->m_processedResourceGuidsAndUsns.end());
        EXPECT_EQ(it.value(), resource.updateSequenceNum().value());
    }
}

TEST_F(
    ResourcesProcessorTest, TolerateFailuresToFindResourceByGuidInLocalStorage)
{
    const auto noteGuid = UidGenerator::Generate();

    const auto resources = QList<qevercloud::Resource>{}
        << qevercloud::ResourceBuilder{}
               .setGuid(UidGenerator::Generate())
               .setNoteGuid(noteGuid)
               .setUpdateSequenceNum(1)
               .build()
        << qevercloud::ResourceBuilder{}
               .setGuid(UidGenerator::Generate())
               .setNoteGuid(noteGuid)
               .setUpdateSequenceNum(2)
               .build()
        << qevercloud::ResourceBuilder{}
               .setGuid(UidGenerator::Generate())
               .setNoteGuid(noteGuid)
               .setUpdateSequenceNum(3)
               .build()
        << qevercloud::ResourceBuilder{}
               .setGuid(UidGenerator::Generate())
               .setNoteGuid(noteGuid)
               .setUpdateSequenceNum(4)
               .build();

    QList<qevercloud::Resource> resourcesPutIntoLocalStorage;
    QSet<qevercloud::Guid> triedGuids;

    const auto addDataToResource = [&](qevercloud::Resource resource,
                                       const int index) {
        auto dataBody = QString::fromUtf8("Resource #%1").arg(index).toUtf8();
        const auto size = dataBody.size();
        auto hash = QCryptographicHash::hash(dataBody, QCryptographicHash::Md5);

        resource.setData(qevercloud::DataBuilder{}
                             .setBody(std::move(dataBody))
                             .setSize(size)
                             .setBodyHash(std::move(hash))
                             .build());
        return resource;
    };

    EXPECT_CALL(*m_mockLocalStorage, findResourceByGuid)
        .WillRepeatedly(
            [&](const qevercloud::Guid & guid,
                const local_storage::ILocalStorage::FetchResourceOptions
                    fetchResourceOptions) {
                using FetchResourceOptions =
                    local_storage::ILocalStorage::FetchResourceOptions;

                EXPECT_EQ(fetchResourceOptions, FetchResourceOptions{});

                EXPECT_FALSE(triedGuids.contains(guid));
                triedGuids.insert(guid);

                const auto it = std::find_if(
                    resourcesPutIntoLocalStorage.constBegin(),
                    resourcesPutIntoLocalStorage.constEnd(),
                    [&](const qevercloud::Resource & resource) {
                        return resource.guid() && (*resource.guid() == guid);
                    });
                if (it != resourcesPutIntoLocalStorage.constEnd()) {
                    return threading::makeReadyFuture<
                        std::optional<qevercloud::Resource>>(*it);
                }

                if (guid == resources[1].guid().value()) {
                    return threading::makeExceptionalFuture<
                        std::optional<qevercloud::Resource>>(RuntimeError{
                        ErrorString{"Failed to find note by guid in the local "
                                    "storage"}});
                }

                return threading::makeReadyFuture<
                    std::optional<qevercloud::Resource>>(std::nullopt);
            });

    EXPECT_CALL(*m_mockResourceFullDataDownloader, downloadFullResourceData)
        .WillRepeatedly([&](qevercloud::Guid resourceGuid,
                            qevercloud::IRequestContextPtr ctx) { // NOLINT
            Q_UNUSED(ctx)

            const auto it = std::find_if(
                resources.begin(), resources.end(),
                [&](const qevercloud::Resource & resource) {
                    return resource.guid() &&
                        (*resource.guid() == resourceGuid);
                });
            if (Q_UNLIKELY(it == resources.end())) {
                return threading::makeExceptionalFuture<qevercloud::Resource>(
                    RuntimeError{ErrorString{
                        "Detected attempt to download unrecognized resource"}});
            }

            const int index =
                static_cast<int>(std::distance(resources.begin(), it));

            return threading::makeReadyFuture<qevercloud::Resource>(
                addDataToResource(*it, index));
        });

    EXPECT_CALL(*m_mockLocalStorage, putResource)
        .WillRepeatedly([&](const qevercloud::Resource & resource) {
            if (Q_UNLIKELY(!resource.guid())) {
                return threading::makeExceptionalFuture<void>(RuntimeError{
                    ErrorString{"Detected resource without guid"}});
            }

            EXPECT_TRUE(triedGuids.contains(*resource.guid()));

            resourcesPutIntoLocalStorage << resource;
            return threading::makeReadyFuture();
        });

    const auto syncChunks = QList<qevercloud::SyncChunk>{}
        << qevercloud::SyncChunkBuilder{}.setResources(resources).build();

    const auto resourcesProcessor = std::make_shared<ResourcesProcessor>(
        m_mockLocalStorage, m_mockResourceFullDataDownloader);

    const auto callback = std::make_shared<ResourcesProcessorCallback>();

    auto future = resourcesProcessor->processResources(syncChunks, callback);
    ASSERT_TRUE(future.isFinished());
    EXPECT_NO_THROW(future.waitForFinished());

    const QList<qevercloud::Resource> expectedProcessedResources = [&] {
        auto r = resources;
        int i = 0;
        for (auto & resource: r) {
            resource = addDataToResource(resource, i);
            ++i;
        }

        r.removeAt(1);
        return r;
    }();

    EXPECT_EQ(resourcesPutIntoLocalStorage, expectedProcessedResources);

    ASSERT_EQ(future.resultCount(), 1);
    const auto status = future.result();
    EXPECT_EQ(
        status->m_totalNewResources,
        static_cast<quint64>(resources.size()) - 1);
    EXPECT_EQ(status->m_totalUpdatedResources, 0UL);
    EXPECT_TRUE(status->m_resourcesWhichFailedToDownload.isEmpty());
    EXPECT_TRUE(status->m_cancelledResourceGuidsAndUsns.isEmpty());

    ASSERT_EQ(status->m_resourcesWhichFailedToProcess.size(), 1);
    EXPECT_EQ(status->m_resourcesWhichFailedToProcess[0].first, resources[1]);

    ASSERT_EQ(
        status->m_processedResourceGuidsAndUsns.size(), resources.size() - 1);

    for (const auto & resource: qAsConst(resources)) {
        if (resource.guid().value() == resources[1].guid().value()) {
            continue;
        }

        const auto it = status->m_processedResourceGuidsAndUsns.find(
            resource.guid().value());

        ASSERT_NE(it, status->m_processedResourceGuidsAndUsns.end());
        EXPECT_EQ(it.value(), resource.updateSequenceNum().value());
    }

    EXPECT_TRUE(callback->m_resourcesWhichFailedToDownload.isEmpty());
    EXPECT_TRUE(callback->m_cancelledResources.isEmpty());

    ASSERT_EQ(callback->m_resourcesWhichFailedToProcess.size(), 1);
    EXPECT_EQ(
        callback->m_resourcesWhichFailedToProcess.constBegin()->first,
        resources[1]);

    ASSERT_EQ(
        callback->m_processedResourceGuidsAndUsns.size(), resources.size() - 1);

    for (const auto & resource: qAsConst(resources)) {
        if (resource.updateSequenceNum().value() == 2) {
            continue;
        }

        const auto it = callback->m_processedResourceGuidsAndUsns.find(
            resource.guid().value());

        ASSERT_NE(it, callback->m_processedResourceGuidsAndUsns.end());
        EXPECT_EQ(it.value(), resource.updateSequenceNum().value());
    }
}

TEST_F(ResourcesProcessorTest, TolerateFailuresToPutResourceIntoLocalStorage)
{
    const auto noteGuid = UidGenerator::Generate();

    const auto resources = QList<qevercloud::Resource>{}
        << qevercloud::ResourceBuilder{}
               .setGuid(UidGenerator::Generate())
               .setNoteGuid(noteGuid)
               .setUpdateSequenceNum(1)
               .build()
        << qevercloud::ResourceBuilder{}
               .setGuid(UidGenerator::Generate())
               .setNoteGuid(noteGuid)
               .setUpdateSequenceNum(2)
               .build()
        << qevercloud::ResourceBuilder{}
               .setGuid(UidGenerator::Generate())
               .setNoteGuid(noteGuid)
               .setUpdateSequenceNum(3)
               .build()
        << qevercloud::ResourceBuilder{}
               .setGuid(UidGenerator::Generate())
               .setNoteGuid(noteGuid)
               .setUpdateSequenceNum(4)
               .build();

    QList<qevercloud::Resource> resourcesPutIntoLocalStorage;
    QSet<qevercloud::Guid> triedGuids;

    const auto addDataToResource = [&](qevercloud::Resource resource,
                                       const int index) {
        auto dataBody = QString::fromUtf8("Resource #%1").arg(index).toUtf8();
        const auto size = dataBody.size();
        auto hash = QCryptographicHash::hash(dataBody, QCryptographicHash::Md5);

        resource.setData(qevercloud::DataBuilder{}
                             .setBody(std::move(dataBody))
                             .setSize(size)
                             .setBodyHash(std::move(hash))
                             .build());
        return resource;
    };

    EXPECT_CALL(*m_mockLocalStorage, findResourceByGuid)
        .WillRepeatedly(
            [&](const qevercloud::Guid & guid,
                const local_storage::ILocalStorage::FetchResourceOptions
                    fetchResourceOptions) {
                using FetchResourceOptions =
                    local_storage::ILocalStorage::FetchResourceOptions;

                EXPECT_EQ(fetchResourceOptions, FetchResourceOptions{});

                EXPECT_FALSE(triedGuids.contains(guid));
                triedGuids.insert(guid);

                const auto it = std::find_if(
                    resourcesPutIntoLocalStorage.constBegin(),
                    resourcesPutIntoLocalStorage.constEnd(),
                    [&](const qevercloud::Resource & resource) {
                        return resource.guid() && (*resource.guid() == guid);
                    });
                if (it != resourcesPutIntoLocalStorage.constEnd()) {
                    return threading::makeReadyFuture<
                        std::optional<qevercloud::Resource>>(*it);
                }

                return threading::makeReadyFuture<
                    std::optional<qevercloud::Resource>>(std::nullopt);
            });

    EXPECT_CALL(*m_mockResourceFullDataDownloader, downloadFullResourceData)
        .WillRepeatedly([&](qevercloud::Guid resourceGuid,
                            qevercloud::IRequestContextPtr ctx) { // NOLINT
            Q_UNUSED(ctx)

            const auto it = std::find_if(
                resources.begin(), resources.end(),
                [&](const qevercloud::Resource & resource) {
                    return resource.guid() &&
                        (*resource.guid() == resourceGuid);
                });
            if (Q_UNLIKELY(it == resources.end())) {
                return threading::makeExceptionalFuture<qevercloud::Resource>(
                    RuntimeError{ErrorString{
                        "Detected attempt to download unrecognized resource"}});
            }

            const int index =
                static_cast<int>(std::distance(resources.begin(), it));

            return threading::makeReadyFuture<qevercloud::Resource>(
                addDataToResource(*it, index));
        });

    EXPECT_CALL(*m_mockLocalStorage, putResource)
        .WillRepeatedly([&](const qevercloud::Resource & resource) {
            if (Q_UNLIKELY(!resource.guid())) {
                return threading::makeExceptionalFuture<void>(RuntimeError{
                    ErrorString{"Detected resource without guid"}});
            }

            EXPECT_TRUE(triedGuids.contains(*resource.guid()));

            if (resource.guid() == resources[1].guid()) {
                return threading::makeExceptionalFuture<void>(RuntimeError{
                    ErrorString{"Failed to put resource into local storage"}});
            }

            resourcesPutIntoLocalStorage << resource;
            return threading::makeReadyFuture();
        });

    const auto syncChunks = QList<qevercloud::SyncChunk>{}
        << qevercloud::SyncChunkBuilder{}.setResources(resources).build();

    const auto resourcesProcessor = std::make_shared<ResourcesProcessor>(
        m_mockLocalStorage, m_mockResourceFullDataDownloader);

    const auto callback = std::make_shared<ResourcesProcessorCallback>();

    auto future = resourcesProcessor->processResources(syncChunks, callback);
    ASSERT_TRUE(future.isFinished());
    EXPECT_NO_THROW(future.waitForFinished());

    const QList<qevercloud::Resource> expectedProcessedResources = [&] {
        auto r = resources;
        int i = 0;
        for (auto & resource: r) {
            resource = addDataToResource(resource, i);
            ++i;
        }

        r.removeAt(1);
        return r;
    }();

    EXPECT_EQ(resourcesPutIntoLocalStorage, expectedProcessedResources);

    ASSERT_EQ(future.resultCount(), 1);
    const auto status = future.result();
    EXPECT_EQ(
        status->m_totalNewResources, static_cast<quint64>(resources.size()));
    EXPECT_EQ(status->m_totalUpdatedResources, 0UL);
    EXPECT_TRUE(status->m_resourcesWhichFailedToDownload.isEmpty());
    EXPECT_TRUE(status->m_cancelledResourceGuidsAndUsns.isEmpty());

    ASSERT_EQ(status->m_resourcesWhichFailedToProcess.size(), 1);
    EXPECT_EQ(
        status->m_resourcesWhichFailedToProcess[0].first,
        addDataToResource(resources[1], 1));

    ASSERT_EQ(
        status->m_processedResourceGuidsAndUsns.size(), resources.size() - 1);

    for (const auto & resource: qAsConst(resources)) {
        if (resource.guid().value() == resources[1].guid().value()) {
            continue;
        }

        const auto it = status->m_processedResourceGuidsAndUsns.find(
            resource.guid().value());

        ASSERT_NE(it, status->m_processedResourceGuidsAndUsns.end());
        EXPECT_EQ(it.value(), resource.updateSequenceNum().value());
    }

    EXPECT_TRUE(callback->m_resourcesWhichFailedToDownload.isEmpty());
    EXPECT_TRUE(callback->m_cancelledResources.isEmpty());

    ASSERT_EQ(callback->m_resourcesWhichFailedToProcess.size(), 1);
    EXPECT_EQ(
        callback->m_resourcesWhichFailedToProcess.constBegin()->first,
        addDataToResource(resources[1], 1));

    ASSERT_EQ(
        callback->m_processedResourceGuidsAndUsns.size(), resources.size() - 1);

    for (const auto & resource: qAsConst(resources)) {
        if (resource.updateSequenceNum().value() == 2) {
            continue;
        }

        const auto it = callback->m_processedResourceGuidsAndUsns.find(
            resource.guid().value());

        ASSERT_NE(it, callback->m_processedResourceGuidsAndUsns.end());
        EXPECT_EQ(it.value(), resource.updateSequenceNum().value());
    }
}

TEST_F(
    ResourcesProcessorTest,
    HandleExistingResourceWhichShouldBeOverriddenByDownloadedVersion)
{
    const auto noteGuid = UidGenerator::Generate();

    const auto resources = QList<qevercloud::Resource>{}
        << qevercloud::ResourceBuilder{}
               .setGuid(UidGenerator::Generate())
               .setNoteGuid(noteGuid)
               .setUpdateSequenceNum(1)
               .build()
        << qevercloud::ResourceBuilder{}
               .setGuid(UidGenerator::Generate())
               .setNoteGuid(noteGuid)
               .setUpdateSequenceNum(2)
               .build()
        << qevercloud::ResourceBuilder{}
               .setGuid(UidGenerator::Generate())
               .setNoteGuid(noteGuid)
               .setUpdateSequenceNum(3)
               .build()
        << qevercloud::ResourceBuilder{}
               .setGuid(UidGenerator::Generate())
               .setNoteGuid(noteGuid)
               .setUpdateSequenceNum(4)
               .build();

    QList<qevercloud::Resource> resourcesPutIntoLocalStorage;
    QSet<qevercloud::Guid> triedGuids;

    const auto addDataToResource = [&](qevercloud::Resource resource,
                                       const int index) {
        auto dataBody = QString::fromUtf8("Resource #%1").arg(index).toUtf8();
        const auto size = dataBody.size();
        auto hash = QCryptographicHash::hash(dataBody, QCryptographicHash::Md5);

        resource.setData(qevercloud::DataBuilder{}
                             .setBody(std::move(dataBody))
                             .setSize(size)
                             .setBodyHash(std::move(hash))
                             .build());
        return resource;
    };

    EXPECT_CALL(*m_mockLocalStorage, findResourceByGuid)
        .WillRepeatedly(
            [&](const qevercloud::Guid & guid,
                const local_storage::ILocalStorage::FetchResourceOptions
                    fetchResourceOptions) {
                using FetchResourceOptions =
                    local_storage::ILocalStorage::FetchResourceOptions;

                EXPECT_EQ(fetchResourceOptions, FetchResourceOptions{});

                EXPECT_FALSE(triedGuids.contains(guid));
                triedGuids.insert(guid);

                const auto it = std::find_if(
                    resourcesPutIntoLocalStorage.constBegin(),
                    resourcesPutIntoLocalStorage.constEnd(),
                    [&](const qevercloud::Resource & resource) {
                        return resource.guid() && (*resource.guid() == guid);
                    });
                if (it != resourcesPutIntoLocalStorage.constEnd()) {
                    return threading::makeReadyFuture<
                        std::optional<qevercloud::Resource>>(*it);
                }

                if (guid == resources[1].guid().value()) {
                    return threading::makeReadyFuture<
                        std::optional<qevercloud::Resource>>(
                        qevercloud::ResourceBuilder{}
                            .setGuid(guid)
                            .setNoteGuid(noteGuid)
                            .setUpdateSequenceNum(
                                resources[1].updateSequenceNum())
                            .build());
                }

                return threading::makeReadyFuture<
                    std::optional<qevercloud::Resource>>(std::nullopt);
            });

    EXPECT_CALL(*m_mockResourceFullDataDownloader, downloadFullResourceData)
        .WillRepeatedly([&](qevercloud::Guid resourceGuid,
                            qevercloud::IRequestContextPtr ctx) { // NOLINT
            Q_UNUSED(ctx)

            const auto it = std::find_if(
                resources.begin(), resources.end(),
                [&](const qevercloud::Resource & resource) {
                    return resource.guid() &&
                        (*resource.guid() == resourceGuid);
                });
            if (Q_UNLIKELY(it == resources.end())) {
                return threading::makeExceptionalFuture<qevercloud::Resource>(
                    RuntimeError{ErrorString{
                        "Detected attempt to download unrecognized resource"}});
            }

            const int index =
                static_cast<int>(std::distance(resources.begin(), it));

            return threading::makeReadyFuture<qevercloud::Resource>(
                addDataToResource(*it, index));
        });

    EXPECT_CALL(*m_mockLocalStorage, putResource)
        .WillRepeatedly([&](const qevercloud::Resource & resource) {
            if (Q_UNLIKELY(!resource.guid())) {
                return threading::makeExceptionalFuture<void>(RuntimeError{
                    ErrorString{"Detected resource without guid"}});
            }

            EXPECT_TRUE(triedGuids.contains(*resource.guid()));

            resourcesPutIntoLocalStorage << resource;
            return threading::makeReadyFuture();
        });

    const auto syncChunks = QList<qevercloud::SyncChunk>{}
        << qevercloud::SyncChunkBuilder{}.setResources(resources).build();

    const auto resourcesProcessor = std::make_shared<ResourcesProcessor>(
        m_mockLocalStorage, m_mockResourceFullDataDownloader);

    const auto callback = std::make_shared<ResourcesProcessorCallback>();

    auto future = resourcesProcessor->processResources(syncChunks, callback);
    ASSERT_TRUE(future.isFinished());
    EXPECT_NO_THROW(future.waitForFinished());

    ASSERT_EQ(resourcesPutIntoLocalStorage.size(), resources.size());
    for (int i = 0, size = resources.size(); i < size; ++i) {
        const auto resourceWithData = addDataToResource(resources[i], i);
        EXPECT_EQ(resourcesPutIntoLocalStorage[i], resourceWithData);
    }

    ASSERT_EQ(future.resultCount(), 1);
    const auto status = future.result();
    EXPECT_EQ(
        status->m_totalNewResources,
        static_cast<quint64>(resources.size() - 1));
    EXPECT_EQ(status->m_totalUpdatedResources, 1UL);
    EXPECT_TRUE(status->m_resourcesWhichFailedToDownload.isEmpty());
    EXPECT_TRUE(status->m_resourcesWhichFailedToProcess.isEmpty());
    EXPECT_TRUE(status->m_cancelledResourceGuidsAndUsns.isEmpty());

    ASSERT_EQ(status->m_processedResourceGuidsAndUsns.size(), resources.size());

    for (const auto & resource: qAsConst(resources)) {
        const auto it = status->m_processedResourceGuidsAndUsns.find(
            resource.guid().value());

        ASSERT_NE(it, status->m_processedResourceGuidsAndUsns.end());
        EXPECT_EQ(it.value(), resource.updateSequenceNum().value());
    }

    EXPECT_TRUE(callback->m_resourcesWhichFailedToDownload.isEmpty());
    EXPECT_TRUE(callback->m_resourcesWhichFailedToProcess.isEmpty());
    EXPECT_TRUE(callback->m_cancelledResources.isEmpty());

    ASSERT_EQ(
        callback->m_processedResourceGuidsAndUsns.size(), resources.size());

    for (const auto & resource: qAsConst(resources)) {
        const auto it = callback->m_processedResourceGuidsAndUsns.find(
            resource.guid().value());

        ASSERT_NE(it, callback->m_processedResourceGuidsAndUsns.end());
        EXPECT_EQ(it.value(), resource.updateSequenceNum().value());
    }
}

TEST_F(
    ResourcesProcessorTest,
    HandleExistingResourceWhichShouldBeMovedToLocalConflictingNote)
{
    const auto noteGuid = UidGenerator::Generate();

    const auto resources = QList<qevercloud::Resource>{}
        << qevercloud::ResourceBuilder{}
               .setGuid(UidGenerator::Generate())
               .setNoteGuid(noteGuid)
               .setUpdateSequenceNum(1)
               .build()
        << qevercloud::ResourceBuilder{}
               .setGuid(UidGenerator::Generate())
               .setNoteGuid(noteGuid)
               .setUpdateSequenceNum(2)
               .build()
        << qevercloud::ResourceBuilder{}
               .setGuid(UidGenerator::Generate())
               .setNoteGuid(noteGuid)
               .setUpdateSequenceNum(3)
               .build()
        << qevercloud::ResourceBuilder{}
               .setGuid(UidGenerator::Generate())
               .setNoteGuid(noteGuid)
               .setUpdateSequenceNum(4)
               .build();

    QList<qevercloud::Resource> resourcesPutIntoLocalStorage;
    QSet<qevercloud::Guid> triedGuids;

    const auto addDataToResource = [&](qevercloud::Resource resource,
                                       const int index) {
        auto dataBody = QString::fromUtf8("Resource #%1").arg(index).toUtf8();
        const auto size = dataBody.size();
        auto hash = QCryptographicHash::hash(dataBody, QCryptographicHash::Md5);

        resource.setData(qevercloud::DataBuilder{}
                             .setBody(std::move(dataBody))
                             .setSize(size)
                             .setBodyHash(std::move(hash))
                             .build());
        return resource;
    };

    EXPECT_CALL(*m_mockLocalStorage, findResourceByGuid)
        .WillRepeatedly(
            [&](const qevercloud::Guid & guid,
                const local_storage::ILocalStorage::FetchResourceOptions
                    fetchResourceOptions) {
                using FetchResourceOptions =
                    local_storage::ILocalStorage::FetchResourceOptions;

                EXPECT_EQ(fetchResourceOptions, FetchResourceOptions{});

                EXPECT_FALSE(triedGuids.contains(guid));
                triedGuids.insert(guid);

                const auto it = std::find_if(
                    resourcesPutIntoLocalStorage.constBegin(),
                    resourcesPutIntoLocalStorage.constEnd(),
                    [&](const qevercloud::Resource & resource) {
                        return resource.guid() && (*resource.guid() == guid);
                    });
                if (it != resourcesPutIntoLocalStorage.constEnd()) {
                    return threading::makeReadyFuture<
                        std::optional<qevercloud::Resource>>(*it);
                }

                if (guid == resources[1].guid().value()) {
                    return threading::makeReadyFuture<
                        std::optional<qevercloud::Resource>>(
                        qevercloud::ResourceBuilder{}
                            .setGuid(guid)
                            .setNoteGuid(noteGuid)
                            .setUpdateSequenceNum(
                                resources[1].updateSequenceNum())
                            .setLocallyModified(true)
                            .build());
                }

                return threading::makeReadyFuture<
                    std::optional<qevercloud::Resource>>(std::nullopt);
            });

    EXPECT_CALL(*m_mockResourceFullDataDownloader, downloadFullResourceData)
        .WillRepeatedly([&](qevercloud::Guid resourceGuid,
                            qevercloud::IRequestContextPtr ctx) { // NOLINT
            Q_UNUSED(ctx)

            const auto it = std::find_if(
                resources.begin(), resources.end(),
                [&](const qevercloud::Resource & resource) {
                    return resource.guid() &&
                        (*resource.guid() == resourceGuid);
                });
            if (Q_UNLIKELY(it == resources.end())) {
                return threading::makeExceptionalFuture<qevercloud::Resource>(
                    RuntimeError{ErrorString{
                        "Detected attempt to download unrecognized resource"}});
            }

            const int index =
                static_cast<int>(std::distance(resources.begin(), it));

            return threading::makeReadyFuture<qevercloud::Resource>(
                addDataToResource(*it, index));
        });

    EXPECT_CALL(*m_mockLocalStorage, putResource)
        .WillRepeatedly([&](const qevercloud::Resource & resource) {
            if (Q_UNLIKELY(!resource.guid())) {
                return threading::makeExceptionalFuture<void>(RuntimeError{
                    ErrorString{"Detected resource without guid"}});
            }

            EXPECT_TRUE(triedGuids.contains(*resource.guid()));

            resourcesPutIntoLocalStorage << resource;
            return threading::makeReadyFuture();
        });

    // Note owning the conflicting resource
    qevercloud::Note note =
        qevercloud::NoteBuilder{}
            .setGuid(noteGuid)
            .setUpdateSequenceNum(5)
            .setResources(
                QList<qevercloud::Resource>{}
                << resources[0]
                << qevercloud::ResourceBuilder{}
                       .setGuid(resources[1].guid())
                       .setNoteGuid(noteGuid)
                       .setUpdateSequenceNum(resources[1].updateSequenceNum())
                       .setLocallyModified(true)
                       .build()
                << resources[2] << resources[3])
            .build();

    EXPECT_CALL(
        *m_mockLocalStorage,
        findNoteByGuid(
            noteGuid,
            local_storage::ILocalStorage::FetchNoteOptions{} |
                local_storage::ILocalStorage::FetchNoteOption::
                    WithResourceMetadata))
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::Note>>(note)));

    EXPECT_CALL(*m_mockLocalStorage, putNote)
        .WillOnce([&](const qevercloud::Note & putNote) {
            EXPECT_FALSE(putNote.guid());
            EXPECT_FALSE(putNote.updateSequenceNum());

            EXPECT_TRUE(putNote.resources());
            if (putNote.resources()) {
                for (const auto & resource: qAsConst(*putNote.resources())) {
                    EXPECT_FALSE(resource.guid());
                    EXPECT_FALSE(resource.updateSequenceNum());
                    EXPECT_FALSE(resource.noteGuid());
                }
            }

            return threading::makeReadyFuture();
        });

    const auto syncChunks = QList<qevercloud::SyncChunk>{}
        << qevercloud::SyncChunkBuilder{}.setResources(resources).build();

    const auto resourcesProcessor = std::make_shared<ResourcesProcessor>(
        m_mockLocalStorage, m_mockResourceFullDataDownloader);

    const auto callback = std::make_shared<ResourcesProcessorCallback>();

    auto future = resourcesProcessor->processResources(syncChunks, callback);
    ASSERT_TRUE(future.isFinished());
    EXPECT_NO_THROW(future.waitForFinished());

    ASSERT_EQ(resourcesPutIntoLocalStorage.size(), resources.size());
    for (int i = 0, size = resources.size(); i < size; ++i) {
        const auto resourceWithData = addDataToResource(resources[i], i);
        EXPECT_EQ(resourcesPutIntoLocalStorage[i], resourceWithData);
    }

    ASSERT_EQ(future.resultCount(), 1);
    const auto status = future.result();
    EXPECT_EQ(
        status->m_totalNewResources,
        static_cast<quint64>(resources.size() - 1));
    EXPECT_EQ(status->m_totalUpdatedResources, 1UL);
    EXPECT_TRUE(status->m_resourcesWhichFailedToDownload.isEmpty());
    EXPECT_TRUE(status->m_resourcesWhichFailedToProcess.isEmpty());
    EXPECT_TRUE(status->m_cancelledResourceGuidsAndUsns.isEmpty());

    ASSERT_EQ(status->m_processedResourceGuidsAndUsns.size(), resources.size());

    for (const auto & resource: qAsConst(resources)) {
        const auto it = status->m_processedResourceGuidsAndUsns.find(
            resource.guid().value());

        ASSERT_NE(it, status->m_processedResourceGuidsAndUsns.end());
        EXPECT_EQ(it.value(), resource.updateSequenceNum().value());
    }

    EXPECT_TRUE(callback->m_resourcesWhichFailedToDownload.isEmpty());
    EXPECT_TRUE(callback->m_resourcesWhichFailedToProcess.isEmpty());
    EXPECT_TRUE(callback->m_cancelledResources.isEmpty());

    ASSERT_EQ(
        callback->m_processedResourceGuidsAndUsns.size(), resources.size());

    for (const auto & resource: qAsConst(resources)) {
        const auto it = callback->m_processedResourceGuidsAndUsns.find(
            resource.guid().value());

        ASSERT_NE(it, callback->m_processedResourceGuidsAndUsns.end());
        EXPECT_EQ(it.value(), resource.updateSequenceNum().value());
    }
}

TEST_F(
    ResourcesProcessorTest,
    TolerateFailuresToFindNoteOwningConflictResourceByGuidInLocalStorage)
{
    const auto noteGuid = UidGenerator::Generate();

    const auto resources = QList<qevercloud::Resource>{}
        << qevercloud::ResourceBuilder{}
               .setGuid(UidGenerator::Generate())
               .setNoteGuid(noteGuid)
               .setUpdateSequenceNum(1)
               .build()
        << qevercloud::ResourceBuilder{}
               .setGuid(UidGenerator::Generate())
               .setNoteGuid(noteGuid)
               .setUpdateSequenceNum(2)
               .build()
        << qevercloud::ResourceBuilder{}
               .setGuid(UidGenerator::Generate())
               .setNoteGuid(noteGuid)
               .setUpdateSequenceNum(3)
               .build()
        << qevercloud::ResourceBuilder{}
               .setGuid(UidGenerator::Generate())
               .setNoteGuid(noteGuid)
               .setUpdateSequenceNum(4)
               .build();

    QList<qevercloud::Resource> resourcesPutIntoLocalStorage;
    QSet<qevercloud::Guid> triedGuids;

    const auto addDataToResource = [&](qevercloud::Resource resource,
                                       const int index) {
        auto dataBody = QString::fromUtf8("Resource #%1").arg(index).toUtf8();
        const auto size = dataBody.size();
        auto hash = QCryptographicHash::hash(dataBody, QCryptographicHash::Md5);

        resource.setData(qevercloud::DataBuilder{}
                             .setBody(std::move(dataBody))
                             .setSize(size)
                             .setBodyHash(std::move(hash))
                             .build());
        return resource;
    };

    EXPECT_CALL(*m_mockLocalStorage, findResourceByGuid)
        .WillRepeatedly(
            [&](const qevercloud::Guid & guid,
                const local_storage::ILocalStorage::FetchResourceOptions
                    fetchResourceOptions) {
                using FetchResourceOptions =
                    local_storage::ILocalStorage::FetchResourceOptions;

                EXPECT_EQ(fetchResourceOptions, FetchResourceOptions{});

                EXPECT_FALSE(triedGuids.contains(guid));
                triedGuids.insert(guid);

                const auto it = std::find_if(
                    resourcesPutIntoLocalStorage.constBegin(),
                    resourcesPutIntoLocalStorage.constEnd(),
                    [&](const qevercloud::Resource & resource) {
                        return resource.guid() && (*resource.guid() == guid);
                    });
                if (it != resourcesPutIntoLocalStorage.constEnd()) {
                    return threading::makeReadyFuture<
                        std::optional<qevercloud::Resource>>(*it);
                }

                if (guid == resources[1].guid().value()) {
                    return threading::makeReadyFuture<
                        std::optional<qevercloud::Resource>>(
                        qevercloud::ResourceBuilder{}
                            .setGuid(guid)
                            .setNoteGuid(noteGuid)
                            .setUpdateSequenceNum(
                                resources[1].updateSequenceNum())
                            .setLocallyModified(true)
                            .build());
                }

                return threading::makeReadyFuture<
                    std::optional<qevercloud::Resource>>(std::nullopt);
            });

    EXPECT_CALL(*m_mockResourceFullDataDownloader, downloadFullResourceData)
        .WillRepeatedly([&](qevercloud::Guid resourceGuid,
                            qevercloud::IRequestContextPtr ctx) { // NOLINT
            Q_UNUSED(ctx)

            const auto it = std::find_if(
                resources.begin(), resources.end(),
                [&](const qevercloud::Resource & resource) {
                    return resource.guid() &&
                        (*resource.guid() == resourceGuid);
                });
            if (Q_UNLIKELY(it == resources.end())) {
                return threading::makeExceptionalFuture<qevercloud::Resource>(
                    RuntimeError{ErrorString{
                        "Detected attempt to download unrecognized resource"}});
            }

            const int index =
                static_cast<int>(std::distance(resources.begin(), it));

            return threading::makeReadyFuture<qevercloud::Resource>(
                addDataToResource(*it, index));
        });

    EXPECT_CALL(*m_mockLocalStorage, putResource)
        .WillRepeatedly([&](const qevercloud::Resource & resource) {
            if (Q_UNLIKELY(!resource.guid())) {
                return threading::makeExceptionalFuture<void>(RuntimeError{
                    ErrorString{"Detected resource without guid"}});
            }

            EXPECT_TRUE(triedGuids.contains(*resource.guid()));

            resourcesPutIntoLocalStorage << resource;
            return threading::makeReadyFuture();
        });

    EXPECT_CALL(
        *m_mockLocalStorage,
        findNoteByGuid(
            noteGuid,
            local_storage::ILocalStorage::FetchNoteOptions{} |
                local_storage::ILocalStorage::FetchNoteOption::
                    WithResourceMetadata))
        .WillOnce(Return(
            threading::makeExceptionalFuture<std::optional<qevercloud::Note>>(
                RuntimeError{ErrorString{"Failed to find note by guid"}})));

    const auto syncChunks = QList<qevercloud::SyncChunk>{}
        << qevercloud::SyncChunkBuilder{}.setResources(resources).build();

    const auto resourcesProcessor = std::make_shared<ResourcesProcessor>(
        m_mockLocalStorage, m_mockResourceFullDataDownloader);

    const auto callback = std::make_shared<ResourcesProcessorCallback>();

    auto future = resourcesProcessor->processResources(syncChunks, callback);
    ASSERT_TRUE(future.isFinished());
    EXPECT_NO_THROW(future.waitForFinished());

    const QList<qevercloud::Resource> expectedProcessedResources = [&] {
        QList<qevercloud::Resource> res{resources};
        res.removeAt(1);
        return res;
    }();

    ASSERT_EQ(
        resourcesPutIntoLocalStorage.size(), expectedProcessedResources.size());

    for (int i = 0, size = expectedProcessedResources.size(); i < size; ++i) {
        const auto resourceWithData =
            addDataToResource(expectedProcessedResources[i], i < 1 ? i : i + 1);

        EXPECT_EQ(resourcesPutIntoLocalStorage[i], resourceWithData);
    }

    ASSERT_EQ(future.resultCount(), 1);
    const auto status = future.result();
    EXPECT_EQ(
        status->m_totalNewResources,
        static_cast<quint64>(resources.size() - 1));
    EXPECT_EQ(status->m_totalUpdatedResources, 1UL);
    EXPECT_TRUE(status->m_resourcesWhichFailedToDownload.isEmpty());
    EXPECT_TRUE(status->m_cancelledResourceGuidsAndUsns.isEmpty());

    ASSERT_EQ(status->m_resourcesWhichFailedToProcess.size(), 1);
    EXPECT_EQ(
        status->m_resourcesWhichFailedToProcess.begin()->first, resources[1]);

    ASSERT_EQ(
        status->m_processedResourceGuidsAndUsns.size(), resources.size() - 1);

    for (const auto & resource: qAsConst(resources)) {
        if (resource.guid() == resources[1].guid()) {
            continue;
        }

        const auto it = status->m_processedResourceGuidsAndUsns.find(
            resource.guid().value());

        ASSERT_NE(it, status->m_processedResourceGuidsAndUsns.end());
        EXPECT_EQ(it.value(), resource.updateSequenceNum().value());
    }

    EXPECT_TRUE(callback->m_resourcesWhichFailedToDownload.isEmpty());
    EXPECT_TRUE(callback->m_cancelledResources.isEmpty());

    ASSERT_EQ(callback->m_resourcesWhichFailedToProcess.size(), 1);
    EXPECT_EQ(
        callback->m_resourcesWhichFailedToProcess.constBegin()->first,
        resources[1]);

    ASSERT_EQ(
        callback->m_processedResourceGuidsAndUsns.size(), resources.size() - 1);

    for (const auto & resource: qAsConst(resources)) {
        if (resource.guid() == resources[1].guid()) {
            continue;
        }

        const auto it = callback->m_processedResourceGuidsAndUsns.find(
            resource.guid().value());

        ASSERT_NE(it, callback->m_processedResourceGuidsAndUsns.end());
        EXPECT_EQ(it.value(), resource.updateSequenceNum().value());
    }
}

TEST_F(
    ResourcesProcessorTest,
    TolerateMissingNoteOwningConflictResourceInLocalStorage)
{
    const auto noteGuid = UidGenerator::Generate();

    const auto resources = QList<qevercloud::Resource>{}
        << qevercloud::ResourceBuilder{}
               .setGuid(UidGenerator::Generate())
               .setNoteGuid(noteGuid)
               .setUpdateSequenceNum(1)
               .build()
        << qevercloud::ResourceBuilder{}
               .setGuid(UidGenerator::Generate())
               .setNoteGuid(noteGuid)
               .setUpdateSequenceNum(2)
               .build()
        << qevercloud::ResourceBuilder{}
               .setGuid(UidGenerator::Generate())
               .setNoteGuid(noteGuid)
               .setUpdateSequenceNum(3)
               .build()
        << qevercloud::ResourceBuilder{}
               .setGuid(UidGenerator::Generate())
               .setNoteGuid(noteGuid)
               .setUpdateSequenceNum(4)
               .build();

    QList<qevercloud::Resource> resourcesPutIntoLocalStorage;
    QSet<qevercloud::Guid> triedGuids;

    const auto addDataToResource = [&](qevercloud::Resource resource,
                                       const int index) {
        auto dataBody = QString::fromUtf8("Resource #%1").arg(index).toUtf8();
        const auto size = dataBody.size();
        auto hash = QCryptographicHash::hash(dataBody, QCryptographicHash::Md5);

        resource.setData(qevercloud::DataBuilder{}
                             .setBody(std::move(dataBody))
                             .setSize(size)
                             .setBodyHash(std::move(hash))
                             .build());
        return resource;
    };

    EXPECT_CALL(*m_mockLocalStorage, findResourceByGuid)
        .WillRepeatedly(
            [&](const qevercloud::Guid & guid,
                const local_storage::ILocalStorage::FetchResourceOptions
                    fetchResourceOptions) {
                using FetchResourceOptions =
                    local_storage::ILocalStorage::FetchResourceOptions;

                EXPECT_EQ(fetchResourceOptions, FetchResourceOptions{});

                EXPECT_FALSE(triedGuids.contains(guid));
                triedGuids.insert(guid);

                const auto it = std::find_if(
                    resourcesPutIntoLocalStorage.constBegin(),
                    resourcesPutIntoLocalStorage.constEnd(),
                    [&](const qevercloud::Resource & resource) {
                        return resource.guid() && (*resource.guid() == guid);
                    });
                if (it != resourcesPutIntoLocalStorage.constEnd()) {
                    return threading::makeReadyFuture<
                        std::optional<qevercloud::Resource>>(*it);
                }

                if (guid == resources[1].guid().value()) {
                    return threading::makeReadyFuture<
                        std::optional<qevercloud::Resource>>(
                        qevercloud::ResourceBuilder{}
                            .setGuid(guid)
                            .setNoteGuid(noteGuid)
                            .setUpdateSequenceNum(
                                resources[1].updateSequenceNum())
                            .setLocallyModified(true)
                            .build());
                }

                return threading::makeReadyFuture<
                    std::optional<qevercloud::Resource>>(std::nullopt);
            });

    EXPECT_CALL(*m_mockResourceFullDataDownloader, downloadFullResourceData)
        .WillRepeatedly([&](qevercloud::Guid resourceGuid,
                            qevercloud::IRequestContextPtr ctx) { // NOLINT
            Q_UNUSED(ctx)

            const auto it = std::find_if(
                resources.begin(), resources.end(),
                [&](const qevercloud::Resource & resource) {
                    return resource.guid() &&
                        (*resource.guid() == resourceGuid);
                });
            if (Q_UNLIKELY(it == resources.end())) {
                return threading::makeExceptionalFuture<qevercloud::Resource>(
                    RuntimeError{ErrorString{
                        "Detected attempt to download unrecognized resource"}});
            }

            const int index =
                static_cast<int>(std::distance(resources.begin(), it));

            return threading::makeReadyFuture<qevercloud::Resource>(
                addDataToResource(*it, index));
        });

    EXPECT_CALL(*m_mockLocalStorage, putResource)
        .WillRepeatedly([&](const qevercloud::Resource & resource) {
            if (Q_UNLIKELY(!resource.guid())) {
                return threading::makeExceptionalFuture<void>(RuntimeError{
                    ErrorString{"Detected resource without guid"}});
            }

            EXPECT_TRUE(triedGuids.contains(*resource.guid()));

            resourcesPutIntoLocalStorage << resource;
            return threading::makeReadyFuture();
        });

    EXPECT_CALL(
        *m_mockLocalStorage,
        findNoteByGuid(
            noteGuid,
            local_storage::ILocalStorage::FetchNoteOptions{} |
                local_storage::ILocalStorage::FetchNoteOption::
                    WithResourceMetadata))
        .WillOnce(
            Return(threading::makeReadyFuture<std::optional<qevercloud::Note>>(
                std::nullopt)));

    const auto syncChunks = QList<qevercloud::SyncChunk>{}
        << qevercloud::SyncChunkBuilder{}.setResources(resources).build();

    const auto resourcesProcessor = std::make_shared<ResourcesProcessor>(
        m_mockLocalStorage, m_mockResourceFullDataDownloader);

    const auto callback = std::make_shared<ResourcesProcessorCallback>();

    auto future = resourcesProcessor->processResources(syncChunks, callback);
    ASSERT_TRUE(future.isFinished());
    EXPECT_NO_THROW(future.waitForFinished());

    const QList<qevercloud::Resource> expectedProcessedResources = [&] {
        QList<qevercloud::Resource> res{resources};
        res.removeAt(1);
        return res;
    }();

    ASSERT_EQ(
        resourcesPutIntoLocalStorage.size(), expectedProcessedResources.size());

    for (int i = 0, size = expectedProcessedResources.size(); i < size; ++i) {
        const auto resourceWithData =
            addDataToResource(expectedProcessedResources[i], i < 1 ? i : i + 1);

        EXPECT_EQ(resourcesPutIntoLocalStorage[i], resourceWithData);
    }

    ASSERT_EQ(future.resultCount(), 1);
    const auto status = future.result();
    EXPECT_EQ(
        status->m_totalNewResources,
        static_cast<quint64>(resources.size() - 1));
    EXPECT_EQ(status->m_totalUpdatedResources, 1UL);
    EXPECT_TRUE(status->m_resourcesWhichFailedToDownload.isEmpty());
    EXPECT_TRUE(status->m_cancelledResourceGuidsAndUsns.isEmpty());

    ASSERT_EQ(status->m_resourcesWhichFailedToProcess.size(), 1);
    EXPECT_EQ(
        status->m_resourcesWhichFailedToProcess.begin()->first, resources[1]);

    ASSERT_EQ(
        status->m_processedResourceGuidsAndUsns.size(), resources.size() - 1);

    for (const auto & resource: qAsConst(resources)) {
        if (resource.guid() == resources[1].guid()) {
            continue;
        }

        const auto it = status->m_processedResourceGuidsAndUsns.find(
            resource.guid().value());

        ASSERT_NE(it, status->m_processedResourceGuidsAndUsns.end());
        EXPECT_EQ(it.value(), resource.updateSequenceNum().value());
    }

    EXPECT_TRUE(callback->m_resourcesWhichFailedToDownload.isEmpty());
    EXPECT_TRUE(callback->m_cancelledResources.isEmpty());

    ASSERT_EQ(callback->m_resourcesWhichFailedToProcess.size(), 1);
    EXPECT_EQ(
        callback->m_resourcesWhichFailedToProcess.constBegin()->first,
        resources[1]);

    ASSERT_EQ(
        callback->m_processedResourceGuidsAndUsns.size(), resources.size() - 1);

    for (const auto & resource: qAsConst(resources)) {
        if (resource.guid() == resources[1].guid()) {
            continue;
        }

        const auto it = callback->m_processedResourceGuidsAndUsns.find(
            resource.guid().value());

        ASSERT_NE(it, callback->m_processedResourceGuidsAndUsns.end());
        EXPECT_EQ(it.value(), resource.updateSequenceNum().value());
    }
}

} // namespace quentier::synchronization::tests
