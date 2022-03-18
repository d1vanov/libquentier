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

#include <synchronization/SyncChunksDataCounters.h>
#include <synchronization/processors/LinkedNotebooksProcessor.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/RuntimeError.h>
#include <quentier/local_storage/tests/mocks/MockILocalStorage.h>
#include <quentier/threading/Future.h>
#include <quentier/utility/UidGenerator.h>

#include <qevercloud/types/builders/LinkedNotebookBuilder.h>
#include <qevercloud/types/builders/SyncChunkBuilder.h>

#include <QSet>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>

// clazy:excludeall=non-pod-global-static
// clazy:excludeall=returning-void-expression

namespace quentier::synchronization::tests {

using testing::StrictMock;

class LinkedNotebooksProcessorTest : public testing::Test
{
protected:
    const std::shared_ptr<local_storage::tests::mocks::MockILocalStorage>
        m_mockLocalStorage = std::make_shared<
            StrictMock<local_storage::tests::mocks::MockILocalStorage>>();

    const SyncChunksDataCountersPtr m_syncChunksDataCounters =
        std::make_shared<SyncChunksDataCounters>();
};

TEST_F(LinkedNotebooksProcessorTest, Ctor)
{
    EXPECT_NO_THROW(
        const auto linkedNotebooksProcessor =
            std::make_shared<LinkedNotebooksProcessor>(
                m_mockLocalStorage, m_syncChunksDataCounters));
}

TEST_F(LinkedNotebooksProcessorTest, CtorNullLocalStorage)
{
    EXPECT_THROW(
        const auto linkedNotebooksProcessor =
            std::make_shared<LinkedNotebooksProcessor>(
                nullptr, m_syncChunksDataCounters),
        InvalidArgument);
}

TEST_F(LinkedNotebooksProcessorTest, CtorNullSyncChunksDataCounters)
{
    EXPECT_THROW(
        const auto linkedNotebooksProcessor =
            std::make_shared<LinkedNotebooksProcessor>(
                m_mockLocalStorage, nullptr),
        InvalidArgument);
}

TEST_F(
    LinkedNotebooksProcessorTest,
    ProcessSyncChunksWithoutLinkedNotebooksToProcess)
{
    const auto syncChunks = QList<qevercloud::SyncChunk>{}
        << qevercloud::SyncChunkBuilder{}.build();

    const auto linkedNotebooksProcessor =
        std::make_shared<LinkedNotebooksProcessor>(
            m_mockLocalStorage, m_syncChunksDataCounters);

    auto future = linkedNotebooksProcessor->processLinkedNotebooks(syncChunks);

    ASSERT_TRUE(future.isFinished());
    EXPECT_NO_THROW(future.waitForFinished());

    EXPECT_EQ(m_syncChunksDataCounters->totalLinkedNotebooks(), 0UL);
    EXPECT_EQ(m_syncChunksDataCounters->totalExpungedLinkedNotebooks(), 0UL);
    EXPECT_EQ(m_syncChunksDataCounters->addedLinkedNotebooks(), 0UL);
    EXPECT_EQ(m_syncChunksDataCounters->updatedLinkedNotebooks(), 0UL);
    EXPECT_EQ(m_syncChunksDataCounters->expungedLinkedNotebooks(), 0UL);
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

    QList<qevercloud::LinkedNotebook> linkedNotebooksPutIntoLocalStorage;

    EXPECT_CALL(*m_mockLocalStorage, putLinkedNotebook)
        .WillRepeatedly([&](const qevercloud::LinkedNotebook & linkedNotebook) {
            if (Q_UNLIKELY(!linkedNotebook.guid())) {
                return threading::makeExceptionalFuture<void>(RuntimeError{
                    ErrorString{"Detected linked notebook without guid"}});
            }
            linkedNotebooksPutIntoLocalStorage << linkedNotebook;
            return threading::makeReadyFuture();
        });

    const auto syncChunks = QList<qevercloud::SyncChunk>{}
        << qevercloud::SyncChunkBuilder{}
               .setLinkedNotebooks(linkedNotebooks)
               .build();

    const auto linkedNotebooksProcessor =
        std::make_shared<LinkedNotebooksProcessor>(
            m_mockLocalStorage, m_syncChunksDataCounters);

    auto future = linkedNotebooksProcessor->processLinkedNotebooks(syncChunks);

    ASSERT_TRUE(future.isFinished());
    EXPECT_NO_THROW(future.waitForFinished());

    EXPECT_EQ(linkedNotebooksPutIntoLocalStorage, linkedNotebooks);

    EXPECT_EQ(
        m_syncChunksDataCounters->totalLinkedNotebooks(),
        static_cast<quint64>(linkedNotebooks.size()));

    EXPECT_EQ(m_syncChunksDataCounters->totalExpungedLinkedNotebooks(), 0UL);

    // Since linked notebooks are put into local storage without checking for
    // duplicated, all linked notebooks put into the local storage go into
    // updatedLinkedNotebooks counter
    EXPECT_EQ(m_syncChunksDataCounters->addedLinkedNotebooks(), 0UL);

    EXPECT_EQ(
        m_syncChunksDataCounters->updatedLinkedNotebooks(),
        static_cast<quint64>(linkedNotebooks.size()));

    EXPECT_EQ(m_syncChunksDataCounters->expungedLinkedNotebooks(), 0UL);
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
        std::make_shared<LinkedNotebooksProcessor>(
            m_mockLocalStorage, m_syncChunksDataCounters);

    QList<qevercloud::Guid> processedLinkedNotebookGuids;
    EXPECT_CALL(*m_mockLocalStorage, expungeLinkedNotebookByGuid)
        .WillRepeatedly([&](const qevercloud::Guid & linkedNotebookGuid) {
            processedLinkedNotebookGuids << linkedNotebookGuid;
            return threading::makeReadyFuture();
        });

    auto future = linkedNotebooksProcessor->processLinkedNotebooks(syncChunks);

    ASSERT_TRUE(future.isFinished());
    EXPECT_NO_THROW(future.waitForFinished());

    EXPECT_EQ(processedLinkedNotebookGuids, expungedLinkedNotebookGuids);

    EXPECT_EQ(m_syncChunksDataCounters->totalLinkedNotebooks(), 0UL);

    EXPECT_EQ(
        m_syncChunksDataCounters->totalExpungedLinkedNotebooks(),
        static_cast<quint64>(expungedLinkedNotebookGuids.size()));

    EXPECT_EQ(m_syncChunksDataCounters->addedLinkedNotebooks(), 0UL);
    EXPECT_EQ(m_syncChunksDataCounters->updatedLinkedNotebooks(), 0UL);

    EXPECT_EQ(
        m_syncChunksDataCounters->expungedLinkedNotebooks(),
        static_cast<quint64>(expungedLinkedNotebookGuids.size()));
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
        std::make_shared<LinkedNotebooksProcessor>(
            m_mockLocalStorage, m_syncChunksDataCounters);

    QList<qevercloud::Guid> processedLinkedNotebookGuids;
    EXPECT_CALL(*m_mockLocalStorage, expungeLinkedNotebookByGuid)
        .WillRepeatedly([&](const qevercloud::Guid & linkedNotebookGuid) {
            processedLinkedNotebookGuids << linkedNotebookGuid;
            return threading::makeReadyFuture();
        });

    auto future = linkedNotebooksProcessor->processLinkedNotebooks(syncChunks);

    ASSERT_TRUE(future.isFinished());
    EXPECT_NO_THROW(future.waitForFinished());

    EXPECT_EQ(processedLinkedNotebookGuids, expungedLinkedNotebookGuids);

    EXPECT_EQ(m_syncChunksDataCounters->totalLinkedNotebooks(), 0UL);

    EXPECT_EQ(
        m_syncChunksDataCounters->totalExpungedLinkedNotebooks(),
        static_cast<quint64>(expungedLinkedNotebookGuids.size()));

    EXPECT_EQ(m_syncChunksDataCounters->addedLinkedNotebooks(), 0UL);
    EXPECT_EQ(m_syncChunksDataCounters->updatedLinkedNotebooks(), 0UL);

    EXPECT_EQ(
        m_syncChunksDataCounters->expungedLinkedNotebooks(),
        static_cast<quint64>(expungedLinkedNotebookGuids.size()));
}

} // namespace quentier::synchronization::tests
