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

#include <synchronization/processors/NotebooksProcessor.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/RuntimeError.h>
#include <quentier/local_storage/tests/mocks/MockILocalStorage.h>
#include <quentier/synchronization/tests/mocks/MockISyncConflictResolver.h>
#include <quentier/threading/Future.h>
#include <quentier/utility/UidGenerator.h>

#include <qevercloud/types/builders/NotebookBuilder.h>
#include <qevercloud/types/builders/SyncChunkBuilder.h>

#include <QSet>

#include <gtest/gtest.h>

#include <algorithm>

// clazy:excludeall=returning-void-expression

namespace quentier::synchronization::tests {

using testing::StrictMock;

class NotebooksProcessorTest : public testing::Test
{
protected:
    std::shared_ptr<local_storage::tests::mocks::MockILocalStorage>
        m_mockLocalStorage = std::make_shared<
            StrictMock<local_storage::tests::mocks::MockILocalStorage>>();

    std::shared_ptr<mocks::MockISyncConflictResolver>
        m_mockSyncConflictResolver = std::make_shared<
            StrictMock<mocks::MockISyncConflictResolver>>();
};

TEST_F(NotebooksProcessorTest, Ctor)
{
    EXPECT_NO_THROW(
        const auto notebooksProcessor = std::make_shared<NotebooksProcessor>(
            m_mockLocalStorage, m_mockSyncConflictResolver));
}

TEST_F(NotebooksProcessorTest, CtorNullLocalStorage)
{
    EXPECT_THROW(
        const auto notebooksProcessor = std::make_shared<NotebooksProcessor>(
            nullptr, m_mockSyncConflictResolver),
        InvalidArgument);
}

TEST_F(NotebooksProcessorTest, CtorNullSyncConflictResolver)
{
    EXPECT_THROW(
        const auto notebooksProcessor = std::make_shared<NotebooksProcessor>(
            m_mockLocalStorage, nullptr),
        InvalidArgument);
}

TEST_F(NotebooksProcessorTest, ProcessNotebooksWithoutConflicts)
{
    const auto notebooks = QList<qevercloud::Notebook>{}
        << qevercloud::NotebookBuilder{}
               .setGuid(UidGenerator::Generate())
               .setName(QStringLiteral("Notebook #1"))
               .setUpdateSequenceNum(0)
               .build()
        << qevercloud::NotebookBuilder{}
               .setGuid(UidGenerator::Generate())
               .setName(QStringLiteral("Notebook #2"))
               .setUpdateSequenceNum(35)
               .build()
        << qevercloud::NotebookBuilder{}
               .setGuid(UidGenerator::Generate())
               .setName(QStringLiteral("Notebook #3"))
               .setUpdateSequenceNum(36)
               .build()
        << qevercloud::NotebookBuilder{}
               .setGuid(UidGenerator::Generate())
               .setName(QStringLiteral("Notebook #4"))
               .setUpdateSequenceNum(54)
               .build();

    QList<qevercloud::Notebook> notebooksPutIntoLocalStorage;
    QSet<qevercloud::Guid> triedGuids;
    QSet<QString> triedNames;

    EXPECT_CALL(*m_mockLocalStorage, findNotebookByGuid)
        .WillRepeatedly([&](const qevercloud::Guid & guid) {
            EXPECT_FALSE(triedGuids.contains(guid));
            triedGuids.insert(guid);

            const auto it = std::find_if(
                notebooksPutIntoLocalStorage.constBegin(),
                notebooksPutIntoLocalStorage.constEnd(),
                [&](const qevercloud::Notebook & notebook)
                {
                    return notebook.guid() && (*notebook.guid() == guid);
                });
            if (it != notebooksPutIntoLocalStorage.constEnd()) {
                return threading::makeReadyFuture<
                    std::optional<qevercloud::Notebook>>(*it);
            }

            return threading::makeReadyFuture<
                std::optional<qevercloud::Notebook>>(std::nullopt);
        });

    EXPECT_CALL(*m_mockLocalStorage, findNotebookByName)
        .WillRepeatedly([&](const QString & name,
                            const std::optional<QString> & linkedNotebookGuid) {
            EXPECT_FALSE(triedNames.contains(name));
            triedNames.insert(name);

            EXPECT_FALSE(linkedNotebookGuid);

            const auto it = std::find_if(
                notebooksPutIntoLocalStorage.constBegin(),
                notebooksPutIntoLocalStorage.constEnd(),
                [&](const qevercloud::Notebook & notebook) {
                    return notebook.name() && (*notebook.name() == name);
                });
            if (it != notebooksPutIntoLocalStorage.constEnd()) {
                return threading::makeReadyFuture<
                    std::optional<qevercloud::Notebook>>(*it);
            }

            return threading::makeReadyFuture<
                std::optional<qevercloud::Notebook>>(std::nullopt);
        });

    EXPECT_CALL(*m_mockLocalStorage, putNotebook)
        .WillRepeatedly(
            [&](const qevercloud::Notebook & notebook)
            {
                if (Q_UNLIKELY(!notebook.guid())) {
                    return threading::makeExceptionalFuture<void>(RuntimeError{
                        ErrorString{"Detected notebook without guid"}});
                }

                EXPECT_TRUE(triedGuids.contains(*notebook.guid()));

                if (Q_UNLIKELY(!notebook.name())) {
                    return threading::makeExceptionalFuture<void>(RuntimeError{
                        ErrorString{"Detected notebook without name"}});
                }

                EXPECT_TRUE(triedNames.contains(*notebook.name()));

                notebooksPutIntoLocalStorage << notebook;
                return threading::makeReadyFuture();
            });

    const auto syncChunks = QList<qevercloud::SyncChunk>{}
        << qevercloud::SyncChunkBuilder{}.setNotebooks(notebooks).build();

    const auto notebooksProcessor = std::make_shared<NotebooksProcessor>(
        m_mockLocalStorage, m_mockSyncConflictResolver);

    auto future = notebooksProcessor->processNotebooks(syncChunks);
    ASSERT_TRUE(future.isFinished());
    EXPECT_NO_THROW(future.waitForFinished());

    EXPECT_EQ(notebooksPutIntoLocalStorage, notebooks);
}

} // namespace quentier::synchronization::tests
