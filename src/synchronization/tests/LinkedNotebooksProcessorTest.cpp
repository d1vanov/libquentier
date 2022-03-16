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
    std::shared_ptr<local_storage::tests::mocks::MockILocalStorage>
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

    auto future = linkedNotebooksProcessor->processLinkedNotebooks(
        syncChunks);

    ASSERT_TRUE(future.isFinished());
    EXPECT_NO_THROW(future.waitForFinished());
}

TEST_F(
    LinkedNotebooksProcessorTest, ProcessLinkedNotebooks)
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
        std::make_shared<LinkedNotebooksProcessor>(m_mockLocalStorage);

    auto future = linkedNotebooksProcessor->processLinkedNotebooks(
        syncChunks);

    ASSERT_TRUE(future.isFinished());
    EXPECT_NO_THROW(future.waitForFinished());

    EXPECT_EQ(linkedNotebooksPutIntoLocalStorage, linkedNotebooks);
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

    QList<qevercloud::Guid> processedLinkedNotebookGuids;
    EXPECT_CALL(*m_mockLocalStorage, expungeLinkedNotebookByGuid)
        .WillRepeatedly([&](const qevercloud::Guid & linkedNotebookGuid) {
            processedLinkedNotebookGuids << linkedNotebookGuid;
            return threading::makeReadyFuture();
        });

    auto future = linkedNotebooksProcessor->processLinkedNotebooks(
        syncChunks);

    ASSERT_TRUE(future.isFinished());
    EXPECT_NO_THROW(future.waitForFinished());

    EXPECT_EQ(processedLinkedNotebookGuids, expungedLinkedNotebookGuids);
}

TEST_F(LinkedNotebooksProcessorTest, FilterOutExpungedLinkedNotebooksFromSyncChunkNotebooks)
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

    const auto expungedLinkedNotebookGuids = [&]
    {
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

    QList<qevercloud::Guid> processedLinkedNotebookGuids;
    EXPECT_CALL(*m_mockLocalStorage, expungeLinkedNotebookByGuid)
        .WillRepeatedly([&](const qevercloud::Guid & linkedNotebookGuid) {
            processedLinkedNotebookGuids << linkedNotebookGuid;
            return threading::makeReadyFuture();
        });

    auto future = linkedNotebooksProcessor->processLinkedNotebooks(
        syncChunks);

    ASSERT_TRUE(future.isFinished());
    EXPECT_NO_THROW(future.waitForFinished());

    EXPECT_EQ(processedLinkedNotebookGuids, expungedLinkedNotebookGuids);
}

} // namespace quentier::synchronization::tests
