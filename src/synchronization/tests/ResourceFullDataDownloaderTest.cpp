/*
 * Copyright 2022-2023 Dmitry Ivanov
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

#include <synchronization/processors/ResourceFullDataDownloader.h>
#include <synchronization/tests/mocks/qevercloud/services/MockINoteStore.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/threading/Future.h>
#include <quentier/utility/UidGenerator.h>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include <quentier/threading/Qt5Promise.h>
#endif

#include <qevercloud/RequestContext.h>
#include <qevercloud/types/builders/DataBuilder.h>
#include <qevercloud/types/builders/ResourceBuilder.h>

#include <QCoreApplication>
#include <QCryptographicHash>

#include <gtest/gtest.h>

#include <memory>

// clazy:excludeall=non-pod-global-static
// clazy:excludeall=returning-void-expression

namespace quentier::synchronization::tests {

using testing::Return;
using testing::StrictMock;

class ResourceFullDataDownloaderTest : public testing::Test
{
protected:
    std::shared_ptr<mocks::qevercloud::MockINoteStore> m_mockNoteStore =
        std::make_shared<mocks::qevercloud::MockINoteStore>();

    const quint32 m_maxInFlightDownloads = 100U;
};

TEST_F(ResourceFullDataDownloaderTest, Ctor)
{
    EXPECT_NO_THROW(
        const auto resourceFullDataDownloader =
            std::make_shared<ResourceFullDataDownloader>(
                m_maxInFlightDownloads));
}

TEST_F(ResourceFullDataDownloaderTest, CtorZeroMaxInFlightDownloads)
{
    EXPECT_THROW(
        const auto resourceFullDataDownloader =
            std::make_shared<ResourceFullDataDownloader>(0U),
        InvalidArgument);
}

TEST_F(ResourceFullDataDownloaderTest, DownloadSingleResource)
{
    const auto resourceFullDataDownloader =
        std::make_shared<ResourceFullDataDownloader>(m_maxInFlightDownloads);

    const QString authToken = QStringLiteral("token");
    const auto ctx = qevercloud::newRequestContext(authToken);

    const auto dataBody = QString::fromUtf8("Data").toUtf8();

    const auto resource =
        qevercloud::ResourceBuilder{}
            .setGuid(UidGenerator::Generate())
            .setUpdateSequenceNum(1U)
            .setNoteGuid(UidGenerator::Generate())
            .setData(qevercloud::DataBuilder{}
                         .setBody(dataBody)
                         .setSize(dataBody.size())
                         .setBodyHash(QCryptographicHash::hash(
                             dataBody, QCryptographicHash::Md5))
                         .build())
            .build();

    EXPECT_CALL(
        *m_mockNoteStore,
        getResourceAsync(resource.guid().value(), true, true, true, true, ctx))
        .WillOnce(Return(threading::makeReadyFuture(resource)));

    auto future = resourceFullDataDownloader->downloadFullResourceData(
        resource.guid().value(), m_mockNoteStore, ctx);

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);
    EXPECT_EQ(future.result(), resource);
}

TEST_F(
    ResourceFullDataDownloaderTest,
    DownloadSeveralResourcesInParallelWithinLimit)
{
    const quint32 resourceCount = 5;

    const auto resourceFullDataDownloader =
        std::make_shared<ResourceFullDataDownloader>(resourceCount);

    const QString authToken = QStringLiteral("token");
    const auto ctx = qevercloud::newRequestContext(authToken);

    const QList<qevercloud::Resource> resources = [&] {
        QList<qevercloud::Resource> result;
        result.reserve(resourceCount);
        for (quint32 i = 0; i < resourceCount; ++i) {
            const auto dataBody =
                QString::fromUtf8("Data #%1").arg(i + 1).toUtf8();
            result << qevercloud::ResourceBuilder{}
                          .setGuid(UidGenerator::Generate())
                          .setUpdateSequenceNum(1U)
                          .setNoteGuid(UidGenerator::Generate())
                          .setData(qevercloud::DataBuilder{}
                                       .setBody(dataBody)
                                       .setSize(dataBody.size())
                                       .setBodyHash(QCryptographicHash::hash(
                                           dataBody, QCryptographicHash::Md5))
                                       .build())
                          .build();
        }
        return result;
    }();

    QList<std::shared_ptr<QPromise<qevercloud::Resource>>> promises;
    promises.reserve(static_cast<int>(resourceCount));

    EXPECT_CALL(*m_mockNoteStore, getResourceAsync)
        .WillRepeatedly([&](const qevercloud::Guid &, // NOLINT
                            const bool withData, const bool withRecognition,
                            const bool withAttributes,
                            const bool withAlternateData,
                            const qevercloud::IRequestContextPtr &) { // NOLINT
            EXPECT_TRUE(withData);
            EXPECT_TRUE(withRecognition);
            EXPECT_TRUE(withAttributes);
            EXPECT_TRUE(withAlternateData);
            auto promise = std::make_shared<QPromise<qevercloud::Resource>>();
            promise->start();
            auto future = promise->future();
            promises << promise;
            return future;
        });

    QList<QFuture<qevercloud::Resource>> futures;
    futures.reserve(resourceCount);

    for (const auto & resource: qAsConst(resources)) {
        futures << resourceFullDataDownloader->downloadFullResourceData(
            resource.guid().value(), m_mockNoteStore, ctx);
        EXPECT_FALSE(futures.back().isFinished());
    }

    ASSERT_EQ(promises.size(), resourceCount);
    for (int i = 0; i < static_cast<int>(resourceCount); ++i) {
        auto & promise = promises[i];
        promise->addResult(resources[i]);
        promise->finish();
    }

    QCoreApplication::processEvents();

    for (int i = 0; i < static_cast<int>(resourceCount); ++i) {
        const auto & future = futures[i];
        ASSERT_TRUE(future.isFinished()) << i;
        ASSERT_EQ(future.resultCount(), 1);
        EXPECT_EQ(future.result(), resources[i]);
    }
}

TEST_F(
    ResourceFullDataDownloaderTest,
    DownloadSeveralResourcesInParallelBeyondLimit)
{
    const quint32 resourceCount = 10;

    const auto resourceFullDataDownloader =
        std::make_shared<ResourceFullDataDownloader>(resourceCount / 2);

    const QString authToken = QStringLiteral("token");
    const auto ctx = qevercloud::newRequestContext(authToken);

    const QList<qevercloud::Resource> resources = [&] {
        QList<qevercloud::Resource> result;
        result.reserve(resourceCount);
        for (quint32 i = 0; i < resourceCount; ++i) {
            const auto dataBody =
                QString::fromUtf8("Data #%1").arg(i + 1).toUtf8();
            result << qevercloud::ResourceBuilder{}
                          .setGuid(UidGenerator::Generate())
                          .setUpdateSequenceNum(1U)
                          .setNoteGuid(UidGenerator::Generate())
                          .setData(qevercloud::DataBuilder{}
                                       .setBody(dataBody)
                                       .setSize(dataBody.size())
                                       .setBodyHash(QCryptographicHash::hash(
                                           dataBody, QCryptographicHash::Md5))
                                       .build())
                          .build();
        }
        return result;
    }();

    QList<std::shared_ptr<QPromise<qevercloud::Resource>>> promises;
    promises.reserve(static_cast<int>(resourceCount));

    EXPECT_CALL(*m_mockNoteStore, getResourceAsync)
        .WillRepeatedly([&](const qevercloud::Guid &, // NOLINT
                            const bool withData, const bool withRecognition,
                            const bool withAttributes,
                            const bool withAlternateData,
                            const qevercloud::IRequestContextPtr &) { // NOLINT
            EXPECT_TRUE(withData);
            EXPECT_TRUE(withRecognition);
            EXPECT_TRUE(withAttributes);
            EXPECT_TRUE(withAlternateData);
            auto promise = std::make_shared<QPromise<qevercloud::Resource>>();
            promise->start();
            auto future = promise->future();
            promises << promise;
            return future;
        });

    QList<QFuture<qevercloud::Resource>> futures;
    futures.reserve(resourceCount);

    for (const auto & resource: qAsConst(resources)) {
        futures << resourceFullDataDownloader->downloadFullResourceData(
            resource.guid().value(), m_mockNoteStore, ctx);
        EXPECT_FALSE(futures.back().isFinished());
    }

    ASSERT_EQ(promises.size(), resourceCount / 2);
    for (int i = 0; i < static_cast<int>(resourceCount / 2); ++i) {
        auto & promise = promises[i];
        promise->addResult(resources[i]);
        promise->finish();
    }

    QCoreApplication::processEvents();

    for (int i = 0; i < static_cast<int>(resourceCount / 2); ++i) {
        const auto & future = futures[i];
        ASSERT_TRUE(future.isFinished()) << i;
        ASSERT_EQ(future.resultCount(), 1);
        EXPECT_EQ(future.result(), resources[i]);
    }

    for (int i = static_cast<int>(resourceCount / 2) + 1;
         i < static_cast<int>(resourceCount); ++i)
    {
        const auto & future = futures[i];
        EXPECT_FALSE(future.isFinished());
    }

    ASSERT_EQ(promises.size(), resourceCount);

    for (int i = static_cast<int>(resourceCount / 2);
         i < static_cast<int>(resourceCount); ++i)
    {
        auto & promise = promises[i];
        promise->addResult(resources[i]);
        promise->finish();
    }

    QCoreApplication::processEvents();

    for (int i = static_cast<int>(resourceCount / 2) + 1;
         i < static_cast<int>(resourceCount); ++i)
    {
        const auto & future = futures[i];
        ASSERT_TRUE(future.isFinished()) << i;
        ASSERT_EQ(future.resultCount(), 1);
        EXPECT_EQ(future.result(), resources[i]);
    }
}

} // namespace quentier::synchronization::tests
