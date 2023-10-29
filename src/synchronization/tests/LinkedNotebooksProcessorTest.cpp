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

#include "Utils.h"

#include <synchronization/processors/LinkedNotebooksProcessor.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/RuntimeError.h>
#include <quentier/local_storage/tests/mocks/MockILocalStorage.h>
#include <quentier/threading/Factory.h>
#include <quentier/threading/Future.h>
#include <quentier/utility/UidGenerator.h>

#include <qevercloud/types/builders/LinkedNotebookBuilder.h>
#include <qevercloud/types/builders/SyncChunkBuilder.h>

#include <QCoreApplication>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>

// clazy:excludeall=non-pod-global-static
// clazy:excludeall=returning-void-expression

namespace quentier::synchronization::tests {

using testing::StrictMock;

namespace {

class MockICallback : public ILinkedNotebooksProcessor::ICallback
{
public:
    MOCK_METHOD(
        void, onLinkedNotebooksProcessingProgress,
        (qint32 totalLinkedNotebooks, qint32 totalLinkedNotebooksToExpunge,
         qint32 processedLinkedNotebooks, qint32 expungedLinkedNotebooks),
        (override));
};

void compareLinkedNotebookLists(
    const QList<qevercloud::LinkedNotebook> & lhs,
    const QList<qevercloud::LinkedNotebook> & rhs)
{
    ASSERT_EQ(lhs.size(), rhs.size());

    for (const auto & l: qAsConst(lhs)) {
        const auto it = std::find_if(
            rhs.constBegin(), rhs.constEnd(),
            [guid = l.guid().value()](const qevercloud::LinkedNotebook & r) {
                return r.guid() == guid;
            });
        EXPECT_NE(it, rhs.constEnd());
        if (Q_UNLIKELY(it == rhs.constEnd())) {
            continue;
        }

        EXPECT_EQ(*it, l);
    }
}

} // namespace

class LinkedNotebooksProcessorTest : public testing::Test
{
protected:
    const std::shared_ptr<local_storage::tests::mocks::MockILocalStorage>
        m_mockLocalStorage = std::make_shared<
            StrictMock<local_storage::tests::mocks::MockILocalStorage>>();
};

TEST_F(LinkedNotebooksProcessorTest, Ctor)
{
    EXPECT_NO_THROW(
        const auto linkedNotebooksProcessor =
            std::make_shared<LinkedNotebooksProcessor>(m_mockLocalStorage));
}

TEST_F(LinkedNotebooksProcessorTest, CtorNullLocalStorage)
{
    EXPECT_THROW(
        const auto linkedNotebooksProcessor =
            std::make_shared<LinkedNotebooksProcessor>(nullptr),
        InvalidArgument);
}

TEST_F(
    LinkedNotebooksProcessorTest,
    ProcessSyncChunksWithoutLinkedNotebooksToProcess)
{
    const auto syncChunks = QList<qevercloud::SyncChunk>{}
        << qevercloud::SyncChunkBuilder{}.build();

    const auto linkedNotebooksProcessor =
        std::make_shared<LinkedNotebooksProcessor>(m_mockLocalStorage);

    const auto mockCallback = std::make_shared<StrictMock<MockICallback>>();

    auto future = linkedNotebooksProcessor->processLinkedNotebooks(
        syncChunks, mockCallback);

    waitForFuture(future);
    EXPECT_NO_THROW(future.waitForFinished());
}

TEST_F(LinkedNotebooksProcessorTest, ProcessLinkedNotebooks)
{
    const auto linkedNotebooks = QList<qevercloud::LinkedNotebook>{}
        << qevercloud::LinkedNotebookBuilder{}
               .setGuid(UidGenerator::Generate())
               .setUsername(QStringLiteral("username #1"))
               .setUpdateSequenceNum(35)
               .build()
        << qevercloud::LinkedNotebookBuilder{}
               .setGuid(UidGenerator::Generate())
               .setUsername(QStringLiteral("username #2"))
               .setUpdateSequenceNum(36)
               .build()
        << qevercloud::LinkedNotebookBuilder{}
               .setGuid(UidGenerator::Generate())
               .setUsername(QStringLiteral("username #3"))
               .setUpdateSequenceNum(37)
               .build()
        << qevercloud::LinkedNotebookBuilder{}
               .setGuid(UidGenerator::Generate())
               .setUsername(QStringLiteral("username #4"))
               .setUpdateSequenceNum(38)
               .build();

    QMutex mutex;
    QList<qevercloud::LinkedNotebook> linkedNotebooksPutIntoLocalStorage;

    EXPECT_CALL(*m_mockLocalStorage, putLinkedNotebook)
        .WillRepeatedly([&](const qevercloud::LinkedNotebook & linkedNotebook) {
            if (Q_UNLIKELY(!linkedNotebook.guid())) {
                return threading::makeExceptionalFuture<void>(RuntimeError{
                    ErrorString{"Detected linked notebook without guid"}});
            }
            const QMutexLocker locker{&mutex};
            linkedNotebooksPutIntoLocalStorage << linkedNotebook;
            return threading::makeReadyFuture();
        });

    const auto syncChunks = QList<qevercloud::SyncChunk>{}
        << qevercloud::SyncChunkBuilder{}
               .setLinkedNotebooks(linkedNotebooks)
               .build();

    const auto linkedNotebooksProcessor =
        std::make_shared<LinkedNotebooksProcessor>(m_mockLocalStorage);

    const auto mockCallback = std::make_shared<StrictMock<MockICallback>>();

    qint32 totalLinkedNotebooks = 0;
    qint32 totalExpungedLinkedNotebooks = 0;
    qint32 processedLinkedNotebooks = 0;
    qint32 expungedLinkedNotebooks = 0;
    EXPECT_CALL(*mockCallback, onLinkedNotebooksProcessingProgress)
        .WillRepeatedly([&](qint32 ttlLinkedNotebooks,
                            qint32 ttlLinkedNotebooksToExpunge,
                            qint32 processed, qint32 expunged) {
            EXPECT_TRUE(
                totalLinkedNotebooks == 0 ||
                totalLinkedNotebooks == ttlLinkedNotebooks);
            totalLinkedNotebooks = ttlLinkedNotebooks;

            EXPECT_TRUE(
                totalExpungedLinkedNotebooks == 0 ||
                totalExpungedLinkedNotebooks == ttlLinkedNotebooksToExpunge);
            totalExpungedLinkedNotebooks = ttlLinkedNotebooksToExpunge;

            processedLinkedNotebooks = processed;
            expungedLinkedNotebooks = expunged;
        });

    auto future = linkedNotebooksProcessor->processLinkedNotebooks(
        syncChunks, mockCallback);

    waitForFuture(future);
    ASSERT_NO_THROW(future.waitForFinished());

    compareLinkedNotebookLists(
        linkedNotebooksPutIntoLocalStorage, linkedNotebooks);

    EXPECT_EQ(totalLinkedNotebooks, linkedNotebooks.size());
    EXPECT_EQ(totalExpungedLinkedNotebooks, 0);
    EXPECT_EQ(processedLinkedNotebooks, linkedNotebooks.size());
    EXPECT_EQ(expungedLinkedNotebooks, 0);
}

TEST_F(LinkedNotebooksProcessorTest, ProcessExpungedLinkedNotebooks)
{
    const auto expungedLinkedNotebookGuids = QList<qevercloud::Guid>{}
        << UidGenerator::Generate() << UidGenerator::Generate()
        << UidGenerator::Generate();

    const auto syncChunks = QList<qevercloud::SyncChunk>{}
        << qevercloud::SyncChunkBuilder{}
               .setExpungedLinkedNotebooks(expungedLinkedNotebookGuids)
               .build();

    const auto linkedNotebooksProcessor =
        std::make_shared<LinkedNotebooksProcessor>(m_mockLocalStorage);

    QMutex mutex;
    QList<qevercloud::Guid> processedLinkedNotebookGuids;
    EXPECT_CALL(*m_mockLocalStorage, expungeLinkedNotebookByGuid)
        .WillRepeatedly([&](const qevercloud::Guid & linkedNotebookGuid) {
            const QMutexLocker locker{&mutex};
            processedLinkedNotebookGuids << linkedNotebookGuid;
            return threading::makeReadyFuture();
        });

    const auto mockCallback = std::make_shared<StrictMock<MockICallback>>();

    qint32 totalLinkedNotebooks = 0;
    qint32 totalExpungedLinkedNotebooks = 0;
    qint32 processedLinkedNotebooks = 0;
    qint32 expungedLinkedNotebooks = 0;
    EXPECT_CALL(*mockCallback, onLinkedNotebooksProcessingProgress)
        .WillRepeatedly([&](qint32 ttlLinkedNotebooks,
                            qint32 ttlLinkedNotebooksToExpunge,
                            qint32 processed, qint32 expunged) {
            EXPECT_TRUE(
                totalLinkedNotebooks == 0 ||
                totalLinkedNotebooks == ttlLinkedNotebooks);
            totalLinkedNotebooks = ttlLinkedNotebooks;

            EXPECT_TRUE(
                totalExpungedLinkedNotebooks == 0 ||
                totalExpungedLinkedNotebooks == ttlLinkedNotebooksToExpunge);
            totalExpungedLinkedNotebooks = ttlLinkedNotebooksToExpunge;

            processedLinkedNotebooks = processed;
            expungedLinkedNotebooks = expunged;
        });

    auto future = linkedNotebooksProcessor->processLinkedNotebooks(
        syncChunks, mockCallback);

    waitForFuture(future);
    ASSERT_NO_THROW(future.waitForFinished());

    compareGuidLists(processedLinkedNotebookGuids, expungedLinkedNotebookGuids);

    EXPECT_EQ(totalLinkedNotebooks, 0);
    EXPECT_EQ(totalExpungedLinkedNotebooks, expungedLinkedNotebookGuids.size());
    EXPECT_EQ(processedLinkedNotebooks, 0);
    EXPECT_EQ(expungedLinkedNotebooks, expungedLinkedNotebookGuids.size());
}

TEST_F(
    LinkedNotebooksProcessorTest,
    FilterOutExpungedLinkedNotebooksFromSyncChunkNotebooks)
{
    const auto linkedNotebooks = QList<qevercloud::LinkedNotebook>{}
        << qevercloud::LinkedNotebookBuilder{}
               .setGuid(UidGenerator::Generate())
               .setUsername(QStringLiteral("username #1"))
               .setUpdateSequenceNum(35)
               .build()
        << qevercloud::LinkedNotebookBuilder{}
               .setGuid(UidGenerator::Generate())
               .setUsername(QStringLiteral("username #2"))
               .setUpdateSequenceNum(36)
               .build()
        << qevercloud::LinkedNotebookBuilder{}
               .setGuid(UidGenerator::Generate())
               .setUsername(QStringLiteral("username #3"))
               .setUpdateSequenceNum(37)
               .build()
        << qevercloud::LinkedNotebookBuilder{}
               .setGuid(UidGenerator::Generate())
               .setUsername(QStringLiteral("username #4"))
               .setUpdateSequenceNum(38)
               .build();

    const auto expungedLinkedNotebookGuids = [&] {
        QList<qevercloud::Guid> guids;
        guids.reserve(linkedNotebooks.size());
        for (const auto & linkedNotebook: qAsConst(linkedNotebooks)) {
            guids << linkedNotebook.guid().value();
        }
        return guids;
    }();

    const auto syncChunks = QList<qevercloud::SyncChunk>{}
        << qevercloud::SyncChunkBuilder{}
               .setLinkedNotebooks(linkedNotebooks)
               .setExpungedLinkedNotebooks(expungedLinkedNotebookGuids)
               .build();

    const auto linkedNotebooksProcessor =
        std::make_shared<LinkedNotebooksProcessor>(m_mockLocalStorage);

    QMutex mutex;
    QList<qevercloud::Guid> processedLinkedNotebookGuids;
    EXPECT_CALL(*m_mockLocalStorage, expungeLinkedNotebookByGuid)
        .WillRepeatedly([&](const qevercloud::Guid & linkedNotebookGuid) {
            const QMutexLocker locker{&mutex};
            processedLinkedNotebookGuids << linkedNotebookGuid;
            return threading::makeReadyFuture();
        });

    const auto mockCallback = std::make_shared<StrictMock<MockICallback>>();

    qint32 totalLinkedNotebooks = 0;
    qint32 totalExpungedLinkedNotebooks = 0;
    qint32 processedLinkedNotebooks = 0;
    qint32 expungedLinkedNotebooks = 0;
    EXPECT_CALL(*mockCallback, onLinkedNotebooksProcessingProgress)
        .WillRepeatedly([&](qint32 ttlLinkedNotebooks,
                            qint32 ttlLinkedNotebooksToExpunge,
                            qint32 processed, qint32 expunged) {
            EXPECT_TRUE(
                totalLinkedNotebooks == 0 ||
                totalLinkedNotebooks == ttlLinkedNotebooks);
            totalLinkedNotebooks = ttlLinkedNotebooks;

            EXPECT_TRUE(
                totalExpungedLinkedNotebooks == 0 ||
                totalExpungedLinkedNotebooks == ttlLinkedNotebooksToExpunge);
            totalExpungedLinkedNotebooks = ttlLinkedNotebooksToExpunge;

            processedLinkedNotebooks = processed;
            expungedLinkedNotebooks = expunged;
        });

    auto future = linkedNotebooksProcessor->processLinkedNotebooks(
        syncChunks, mockCallback);

    waitForFuture(future);
    ASSERT_NO_THROW(future.waitForFinished());

    compareGuidLists(processedLinkedNotebookGuids, expungedLinkedNotebookGuids);

    EXPECT_EQ(totalLinkedNotebooks, 0);
    EXPECT_EQ(totalExpungedLinkedNotebooks, expungedLinkedNotebookGuids.size());
    EXPECT_EQ(processedLinkedNotebooks, 0);
    EXPECT_EQ(expungedLinkedNotebooks, expungedLinkedNotebookGuids.size());
}

} // namespace quentier::synchronization::tests
