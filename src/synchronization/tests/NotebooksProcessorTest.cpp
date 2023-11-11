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

#include <synchronization/processors/NotebooksProcessor.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/RuntimeError.h>
#include <quentier/local_storage/tests/mocks/MockILocalStorage.h>
#include <quentier/synchronization/tests/mocks/MockISyncConflictResolver.h>
#include <quentier/threading/Factory.h>
#include <quentier/threading/Future.h>
#include <quentier/utility/UidGenerator.h>

#include <qevercloud/types/builders/NotebookBuilder.h>
#include <qevercloud/types/builders/SyncChunkBuilder.h>

#include <QCoreApplication>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <utility>

// clazy:excludeall=non-pod-global-static
// clazy:excludeall=returning-void-expression

namespace quentier::synchronization::tests {

using testing::StrictMock;

namespace {

class MockICallback : public INotebooksProcessor::ICallback
{
public:
    MOCK_METHOD(
        void, onNotebooksProcessingProgress,
        (qint32 totalNotebooks, qint32 totalNotebooksToExpunge,
         qint32 addedNotebooks, qint32 updatedNotebooks,
         qint32 expungedNotebooks),
        (override));
};

void compareNotebookLists(
    const QList<qevercloud::Notebook> & lhs,
    const QList<qevercloud::Notebook> & rhs)
{
    ASSERT_EQ(lhs.size(), rhs.size());

    for (const auto & l: std::as_const(lhs)) {
        const auto it = std::find_if(
            rhs.constBegin(), rhs.constEnd(),
            [localId = l.localId()](const qevercloud::Notebook & r) {
                return r.localId() == localId;
            });
        EXPECT_NE(it, rhs.constEnd());
        if (Q_UNLIKELY(it == rhs.constEnd())) {
            continue;
        }

        EXPECT_EQ(*it, l);
    }
}

} // namespace

class NotebooksProcessorTest : public testing::Test
{
protected:
    const std::shared_ptr<local_storage::tests::mocks::MockILocalStorage>
        m_mockLocalStorage = std::make_shared<
            StrictMock<local_storage::tests::mocks::MockILocalStorage>>();

    const std::shared_ptr<mocks::MockISyncConflictResolver>
        m_mockSyncConflictResolver =
            std::make_shared<StrictMock<mocks::MockISyncConflictResolver>>();
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

TEST_F(NotebooksProcessorTest, ProcessSyncChunksWithoutNotebooksToProcess)
{
    const auto syncChunks = QList<qevercloud::SyncChunk>{}
        << qevercloud::SyncChunkBuilder{}.build();

    const auto mockCallback = std::make_shared<StrictMock<MockICallback>>();

    const auto notebooksProcessor = std::make_shared<NotebooksProcessor>(
        m_mockLocalStorage, m_mockSyncConflictResolver);

    auto future =
        notebooksProcessor->processNotebooks(syncChunks, mockCallback);

    waitForFuture(future);
    EXPECT_NO_THROW(future.waitForFinished());
}

TEST_F(NotebooksProcessorTest, ProcessNotebooksWithoutConflicts)
{
    const auto linkedNotebookGuid = UidGenerator::Generate();

    const auto notebooks = QList<qevercloud::Notebook>{}
        << qevercloud::NotebookBuilder{}
               .setLocalId(UidGenerator::Generate())
               .setGuid(UidGenerator::Generate())
               .setName(QStringLiteral("Notebook #1"))
               .setUpdateSequenceNum(0)
               .build()
        << qevercloud::NotebookBuilder{}
               .setLocalId(UidGenerator::Generate())
               .setGuid(UidGenerator::Generate())
               .setName(QStringLiteral("Notebook #2"))
               .setUpdateSequenceNum(35)
               .build()
        << qevercloud::NotebookBuilder{}
               .setLocalId(UidGenerator::Generate())
               .setGuid(UidGenerator::Generate())
               .setName(QStringLiteral("Notebook #3"))
               .setUpdateSequenceNum(36)
               .setLinkedNotebookGuid(linkedNotebookGuid)
               .build()
        << qevercloud::NotebookBuilder{}
               .setLocalId(UidGenerator::Generate())
               .setGuid(UidGenerator::Generate())
               .setName(QStringLiteral("Notebook #4"))
               .setUpdateSequenceNum(54)
               .setLinkedNotebookGuid(linkedNotebookGuid)
               .build();

    QMutex mutex;
    QList<qevercloud::Notebook> notebooksPutIntoLocalStorage;
    QSet<qevercloud::Guid> triedGuids;
    QSet<QString> triedNames;

    EXPECT_CALL(*m_mockLocalStorage, findNotebookByGuid)
        .WillRepeatedly([&](const qevercloud::Guid & guid) {
            const QMutexLocker locker{&mutex};
            EXPECT_FALSE(triedGuids.contains(guid));
            triedGuids.insert(guid);

            const auto it = std::find_if(
                notebooksPutIntoLocalStorage.constBegin(),
                notebooksPutIntoLocalStorage.constEnd(),
                [&](const qevercloud::Notebook & notebook) {
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
            const QMutexLocker locker{&mutex};
            EXPECT_FALSE(triedNames.contains(name));
            triedNames.insert(name);

            const bool isLinkedNotebookTag =
                (name == QStringLiteral("Notebook #3") ||
                 name == QStringLiteral("Notebook #4"));

            if (isLinkedNotebookTag) {
                EXPECT_TRUE(linkedNotebookGuid);
            }
            else {
                EXPECT_FALSE(linkedNotebookGuid);
            }

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
        .WillRepeatedly([&](const qevercloud::Notebook & notebook) {
            if (Q_UNLIKELY(!notebook.guid())) {
                return threading::makeExceptionalFuture<void>(RuntimeError{
                    ErrorString{"Detected notebook without guid"}});
            }

            const QMutexLocker locker{&mutex};
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

    qint32 totalNotebooks = 0;
    qint32 totalExpungedNotebooks = 0;
    qint32 addedNotebooks = 0;
    qint32 updatedNotebooks = 0;
    qint32 expungedNotebooks = 0;
    auto mockCallback = std::make_shared<StrictMock<MockICallback>>();
    EXPECT_CALL(*mockCallback, onNotebooksProcessingProgress)
        .WillRepeatedly([&](qint32 total, qint32 totalExpunged, qint32 added,
                            qint32 updated, qint32 expunged) {
            EXPECT_TRUE(totalNotebooks == 0 || totalNotebooks == total);
            totalNotebooks = total;

            EXPECT_TRUE(
                totalExpungedNotebooks == 0 ||
                totalExpungedNotebooks == totalExpunged);
            totalExpungedNotebooks = totalExpunged;

            addedNotebooks = added;
            updatedNotebooks = updated;
            expungedNotebooks = expunged;
        });

    auto future =
        notebooksProcessor->processNotebooks(syncChunks, mockCallback);

    waitForFuture(future);
    ASSERT_NO_THROW(future.waitForFinished());

    compareNotebookLists(notebooksPutIntoLocalStorage, notebooks);

    EXPECT_EQ(totalNotebooks, notebooks.size());
    EXPECT_EQ(totalExpungedNotebooks, 0);

    EXPECT_EQ(addedNotebooks, notebooks.size());
    EXPECT_EQ(updatedNotebooks, 0);
    EXPECT_EQ(expungedNotebooks, 0);
}

TEST_F(NotebooksProcessorTest, ProcessExpungedNotebooks)
{
    const auto expungedNotebookGuids = QList<qevercloud::Guid>{}
        << UidGenerator::Generate() << UidGenerator::Generate()
        << UidGenerator::Generate();

    const auto syncChunks = QList<qevercloud::SyncChunk>{}
        << qevercloud::SyncChunkBuilder{}
               .setExpungedNotebooks(expungedNotebookGuids)
               .build();

    const auto notebooksProcessor = std::make_shared<NotebooksProcessor>(
        m_mockLocalStorage, m_mockSyncConflictResolver);

    QMutex mutex;
    QList<qevercloud::Guid> processedNotebookGuids;
    EXPECT_CALL(*m_mockLocalStorage, expungeNotebookByGuid)
        .WillRepeatedly([&](const qevercloud::Guid & notebookGuid) {
            const QMutexLocker locker{&mutex};
            processedNotebookGuids << notebookGuid;
            return threading::makeReadyFuture();
        });

    qint32 totalNotebooks = 0;
    qint32 totalExpungedNotebooks = 0;
    qint32 addedNotebooks = 0;
    qint32 updatedNotebooks = 0;
    qint32 expungedNotebooks = 0;
    auto mockCallback = std::make_shared<StrictMock<MockICallback>>();
    EXPECT_CALL(*mockCallback, onNotebooksProcessingProgress)
        .WillRepeatedly([&](qint32 total, qint32 totalExpunged, qint32 added,
                            qint32 updated, qint32 expunged) {
            EXPECT_TRUE(totalNotebooks == 0 || totalNotebooks == total);
            totalNotebooks = total;

            EXPECT_TRUE(
                totalExpungedNotebooks == 0 ||
                totalExpungedNotebooks == totalExpunged);
            totalExpungedNotebooks = totalExpunged;

            addedNotebooks = added;
            updatedNotebooks = updated;
            expungedNotebooks = expunged;
        });

    auto future =
        notebooksProcessor->processNotebooks(syncChunks, mockCallback);

    waitForFuture(future);
    ASSERT_NO_THROW(future.waitForFinished());

    compareGuidLists(processedNotebookGuids, expungedNotebookGuids);

    EXPECT_EQ(totalNotebooks, 0);
    EXPECT_EQ(totalExpungedNotebooks, expungedNotebookGuids.size());
    EXPECT_EQ(addedNotebooks, 0);
    EXPECT_EQ(updatedNotebooks, 0);
    EXPECT_EQ(expungedNotebooks, expungedNotebookGuids.size());
}

TEST_F(NotebooksProcessorTest, FilterOutExpungedNotebooksFromSyncChunkNotebooks)
{
    const auto notebooks = QList<qevercloud::Notebook>{}
        << qevercloud::NotebookBuilder{}
               .setLocalId(UidGenerator::Generate())
               .setGuid(UidGenerator::Generate())
               .setName(QStringLiteral("Notebook #1"))
               .setUpdateSequenceNum(0)
               .build()
        << qevercloud::NotebookBuilder{}
               .setLocalId(UidGenerator::Generate())
               .setGuid(UidGenerator::Generate())
               .setName(QStringLiteral("Notebook #2"))
               .setUpdateSequenceNum(35)
               .build()
        << qevercloud::NotebookBuilder{}
               .setLocalId(UidGenerator::Generate())
               .setGuid(UidGenerator::Generate())
               .setName(QStringLiteral("Notebook #3"))
               .setUpdateSequenceNum(36)
               .build()
        << qevercloud::NotebookBuilder{}
               .setLocalId(UidGenerator::Generate())
               .setGuid(UidGenerator::Generate())
               .setName(QStringLiteral("Notebook #4"))
               .setUpdateSequenceNum(54)
               .build();

    const auto expungedNotebookGuids = [&] {
        QList<qevercloud::Guid> guids;
        guids.reserve(notebooks.size());
        for (const auto & notebook: std::as_const(notebooks)) {
            guids << notebook.guid().value();
        }
        return guids;
    }();

    const auto syncChunks = QList<qevercloud::SyncChunk>{}
        << qevercloud::SyncChunkBuilder{}
               .setNotebooks(notebooks)
               .setExpungedNotebooks(expungedNotebookGuids)
               .build();

    const auto notebooksProcessor = std::make_shared<NotebooksProcessor>(
        m_mockLocalStorage, m_mockSyncConflictResolver);

    QMutex mutex;
    QList<qevercloud::Guid> processedNotebookGuids;
    EXPECT_CALL(*m_mockLocalStorage, expungeNotebookByGuid)
        .WillRepeatedly([&](const qevercloud::Guid & notebookGuid) {
            const QMutexLocker locker{&mutex};
            processedNotebookGuids << notebookGuid;
            return threading::makeReadyFuture();
        });

    qint32 totalNotebooks = 0;
    qint32 totalExpungedNotebooks = 0;
    qint32 addedNotebooks = 0;
    qint32 updatedNotebooks = 0;
    qint32 expungedNotebooks = 0;
    auto mockCallback = std::make_shared<StrictMock<MockICallback>>();
    EXPECT_CALL(*mockCallback, onNotebooksProcessingProgress)
        .WillRepeatedly([&](qint32 total, qint32 totalExpunged, qint32 added,
                            qint32 updated, qint32 expunged) {
            EXPECT_TRUE(totalNotebooks == 0 || totalNotebooks == total);
            totalNotebooks = total;

            EXPECT_TRUE(
                totalExpungedNotebooks == 0 ||
                totalExpungedNotebooks == totalExpunged);
            totalExpungedNotebooks = totalExpunged;

            addedNotebooks = added;
            updatedNotebooks = updated;
            expungedNotebooks = expunged;
        });

    auto future =
        notebooksProcessor->processNotebooks(syncChunks, mockCallback);

    waitForFuture(future);
    ASSERT_NO_THROW(future.waitForFinished());

    compareGuidLists(processedNotebookGuids, expungedNotebookGuids);

    EXPECT_EQ(totalNotebooks, 0);
    EXPECT_EQ(totalExpungedNotebooks, expungedNotebookGuids.size());
    EXPECT_EQ(addedNotebooks, 0);
    EXPECT_EQ(updatedNotebooks, 0);
    EXPECT_EQ(expungedNotebooks, expungedNotebookGuids.size());
}

class NotebooksProcessorTestWithConflict :
    public NotebooksProcessorTest,
    public testing::WithParamInterface<
        ISyncConflictResolver::NotebookConflictResolution>
{};

const std::array gConflictResolutions{
    ISyncConflictResolver::NotebookConflictResolution{
        ISyncConflictResolver::ConflictResolution::UseTheirs{}},
    ISyncConflictResolver::NotebookConflictResolution{
        ISyncConflictResolver::ConflictResolution::UseMine{}},
    ISyncConflictResolver::NotebookConflictResolution{
        ISyncConflictResolver::ConflictResolution::IgnoreMine{}},
    ISyncConflictResolver::NotebookConflictResolution{
        ISyncConflictResolver::ConflictResolution::MoveMine<
            qevercloud::Notebook>{qevercloud::Notebook{}}}};

INSTANTIATE_TEST_SUITE_P(
    NotebooksProcessorTestWithConflictInstance,
    NotebooksProcessorTestWithConflict,
    testing::ValuesIn(gConflictResolutions));

TEST_P(NotebooksProcessorTestWithConflict, HandleConflictByGuid)
{
    auto notebook = qevercloud::NotebookBuilder{}
                        .setLocalId(UidGenerator::Generate())
                        .setGuid(UidGenerator::Generate())
                        .setName(QStringLiteral("Notebook #1"))
                        .setUpdateSequenceNum(1)
                        .build();

    const auto localConflict =
        qevercloud::NotebookBuilder{}
            .setLocalId(UidGenerator::Generate())
            .setGuid(notebook.guid())
            .setName(notebook.name())
            .setUpdateSequenceNum(notebook.updateSequenceNum().value() - 1)
            .setLocallyFavorited(true)
            .build();

    QMutex mutex;
    QList<qevercloud::Notebook> notebooksPutIntoLocalStorage;
    QSet<qevercloud::Guid> triedGuids;
    QSet<QString> triedNames;

    EXPECT_CALL(*m_mockLocalStorage, findNotebookByGuid)
        .WillRepeatedly([&](const qevercloud::Guid & guid) {
            const QMutexLocker locker{&mutex};
            EXPECT_FALSE(triedGuids.contains(guid));
            triedGuids.insert(guid);

            const auto it = std::find_if(
                notebooksPutIntoLocalStorage.constBegin(),
                notebooksPutIntoLocalStorage.constEnd(),
                [&](const qevercloud::Notebook & notebook) {
                    return notebook.guid() && (*notebook.guid() == guid);
                });
            if (it != notebooksPutIntoLocalStorage.constEnd()) {
                return threading::makeReadyFuture<
                    std::optional<qevercloud::Notebook>>(*it);
            }

            if (guid == notebook.guid()) {
                return threading::makeReadyFuture<
                    std::optional<qevercloud::Notebook>>(localConflict);
            }

            return threading::makeReadyFuture<
                std::optional<qevercloud::Notebook>>(std::nullopt);
        });

    auto resolution = GetParam();
    std::optional<qevercloud::Notebook> movedLocalConflict;
    if (std::holds_alternative<ISyncConflictResolver::ConflictResolution::
                                   MoveMine<qevercloud::Notebook>>(resolution))
    {
        movedLocalConflict =
            qevercloud::NotebookBuilder{}
                .setLocalId(UidGenerator::Generate())
                .setName(
                    localConflict.name().value() + QStringLiteral("_moved"))
                .build();

        resolution = ISyncConflictResolver::NotebookConflictResolution{
            ISyncConflictResolver::ConflictResolution::MoveMine<
                qevercloud::Notebook>{*movedLocalConflict}};
    }

    EXPECT_CALL(*m_mockSyncConflictResolver, resolveNotebookConflict)
        .WillOnce([&, resolution](
                      const qevercloud::Notebook & theirs,
                      const qevercloud::Notebook & mine) mutable {
            EXPECT_EQ(theirs, notebook);
            EXPECT_EQ(mine, localConflict);
            return threading::makeReadyFuture<
                ISyncConflictResolver::NotebookConflictResolution>(
                std::move(resolution));
        });

    EXPECT_CALL(*m_mockLocalStorage, findNotebookByName)
        .WillRepeatedly([&](const QString & name,
                            const std::optional<QString> & linkedNotebookGuid) {
            const QMutexLocker locker{&mutex};
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
        .WillRepeatedly([&, conflictGuid = notebook.guid()](
                            const qevercloud::Notebook & notebook) {
            if (Q_UNLIKELY(!notebook.guid())) {
                if (std::holds_alternative<
                        ISyncConflictResolver::ConflictResolution::MoveMine<
                            qevercloud::Notebook>>(resolution))
                {
                    const QMutexLocker locker{&mutex};
                    notebooksPutIntoLocalStorage << notebook;
                    return threading::makeReadyFuture();
                }

                return threading::makeExceptionalFuture<void>(RuntimeError{
                    ErrorString{"Detected notebook without guid"}});
            }

            const QMutexLocker locker{&mutex};
            EXPECT_TRUE(
                triedGuids.contains(*notebook.guid()) ||
                (movedLocalConflict && movedLocalConflict == notebook));

            if (Q_UNLIKELY(!notebook.name())) {
                return threading::makeExceptionalFuture<void>(RuntimeError{
                    ErrorString{"Detected notebook without name"}});
            }

            EXPECT_TRUE(
                triedNames.contains(*notebook.name()) ||
                notebook.guid() == conflictGuid ||
                (movedLocalConflict && movedLocalConflict == notebook));

            notebooksPutIntoLocalStorage << notebook;
            return threading::makeReadyFuture();
        });

    if (std::holds_alternative<
            ISyncConflictResolver::ConflictResolution::UseTheirs>(resolution))
    {
        notebook.setLocalId(localConflict.localId());
        notebook.setLocallyFavorited(localConflict.isLocallyFavorited());
    }

    auto notebooks = QList<qevercloud::Notebook>{}
        << notebook
        << qevercloud::NotebookBuilder{}
               .setLocalId(UidGenerator::Generate())
               .setGuid(UidGenerator::Generate())
               .setName(QStringLiteral("Notebook #2"))
               .setUpdateSequenceNum(35)
               .build()
        << qevercloud::NotebookBuilder{}
               .setLocalId(UidGenerator::Generate())
               .setGuid(UidGenerator::Generate())
               .setName(QStringLiteral("Notebook #3"))
               .setUpdateSequenceNum(36)
               .build()
        << qevercloud::NotebookBuilder{}
               .setLocalId(UidGenerator::Generate())
               .setGuid(UidGenerator::Generate())
               .setName(QStringLiteral("Notebook #4"))
               .setUpdateSequenceNum(54)
               .build();

    const auto originalNotebooksSize = notebooks.size();

    const auto syncChunks = QList<qevercloud::SyncChunk>{}
        << qevercloud::SyncChunkBuilder{}.setNotebooks(notebooks).build();

    const auto notebooksProcessor = std::make_shared<NotebooksProcessor>(
        m_mockLocalStorage, m_mockSyncConflictResolver);

    qint32 totalNotebooks = 0;
    qint32 totalExpungedNotebooks = 0;
    qint32 addedNotebooks = 0;
    qint32 updatedNotebooks = 0;
    qint32 expungedNotebooks = 0;
    auto mockCallback = std::make_shared<StrictMock<MockICallback>>();
    EXPECT_CALL(*mockCallback, onNotebooksProcessingProgress)
        .WillRepeatedly([&](qint32 total, qint32 totalExpunged, qint32 added,
                            qint32 updated, qint32 expunged) {
            EXPECT_TRUE(totalNotebooks == 0 || totalNotebooks == total);
            totalNotebooks = total;

            EXPECT_TRUE(
                totalExpungedNotebooks == 0 ||
                totalExpungedNotebooks == totalExpunged);
            totalExpungedNotebooks = totalExpunged;

            addedNotebooks = added;
            updatedNotebooks = updated;
            expungedNotebooks = expunged;
        });

    auto future =
        notebooksProcessor->processNotebooks(syncChunks, mockCallback);

    waitForFuture(future);
    ASSERT_NO_THROW(future.waitForFinished());

    if (std::holds_alternative<
            ISyncConflictResolver::ConflictResolution::UseMine>(resolution))
    {
        notebooks.removeAt(0);
    }
    else if (std::holds_alternative<ISyncConflictResolver::ConflictResolution::
                                        MoveMine<qevercloud::Notebook>>(
                 resolution))
    {
        ASSERT_TRUE(movedLocalConflict);
        notebooks.push_front(*movedLocalConflict);
    }

    compareNotebookLists(notebooksPutIntoLocalStorage, notebooks);

    EXPECT_EQ(totalNotebooks, originalNotebooksSize);
    EXPECT_EQ(totalExpungedNotebooks, 0);

    if (std::holds_alternative<
            ISyncConflictResolver::ConflictResolution::UseTheirs>(resolution) ||
        std::holds_alternative<
            ISyncConflictResolver::ConflictResolution::IgnoreMine>(
            resolution) ||
        std::holds_alternative<
            ISyncConflictResolver::ConflictResolution::UseMine>(resolution))
    {
        EXPECT_EQ(addedNotebooks, originalNotebooksSize - 1);

        if (std::holds_alternative<
                ISyncConflictResolver::ConflictResolution::UseMine>(resolution))
        {
            EXPECT_EQ(updatedNotebooks, 0);
        }
        else {
            EXPECT_EQ(updatedNotebooks, 1);
        }
    }
    else {
        EXPECT_EQ(addedNotebooks, originalNotebooksSize);
        EXPECT_EQ(updatedNotebooks, 0);
    }
}

TEST_P(NotebooksProcessorTestWithConflict, HandleConflictByName)
{
    const auto notebook = qevercloud::NotebookBuilder{}
                              .setLocalId(UidGenerator::Generate())
                              .setGuid(UidGenerator::Generate())
                              .setName(QStringLiteral("Notebook #1"))
                              .setUpdateSequenceNum(1)
                              .build();

    const auto localConflict = qevercloud::NotebookBuilder{}
        .setLocalId(UidGenerator::Generate())
        .setName(notebook.name())
        .build();

    QMutex mutex;
    QList<qevercloud::Notebook> notebooksPutIntoLocalStorage;
    QSet<qevercloud::Guid> triedGuids;
    QSet<QString> triedNames;

    EXPECT_CALL(*m_mockLocalStorage, findNotebookByGuid)
        .WillRepeatedly([&](const qevercloud::Guid & guid) {
            const QMutexLocker locker{&mutex};
            EXPECT_FALSE(triedGuids.contains(guid));
            triedGuids.insert(guid);

            const auto it = std::find_if(
                notebooksPutIntoLocalStorage.constBegin(),
                notebooksPutIntoLocalStorage.constEnd(),
                [&](const qevercloud::Notebook & notebook) {
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
        .WillRepeatedly([&, conflictName = notebook.name()](
                            const QString & name,
                            const std::optional<QString> & linkedNotebookGuid) {
            const QMutexLocker locker{&mutex};
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

            if (name == conflictName) {
                return threading::makeReadyFuture<
                    std::optional<qevercloud::Notebook>>(localConflict);
            }

            return threading::makeReadyFuture<
                std::optional<qevercloud::Notebook>>(std::nullopt);
        });

    auto resolution = GetParam();
    std::optional<qevercloud::Notebook> movedLocalConflict;
    if (std::holds_alternative<ISyncConflictResolver::ConflictResolution::
                                   MoveMine<qevercloud::Notebook>>(resolution))
    {
        movedLocalConflict =
            qevercloud::NotebookBuilder{}
                .setLocalId(UidGenerator::Generate())
                .setName(
                    localConflict.name().value() + QStringLiteral("_moved"))
                .build();

        resolution = ISyncConflictResolver::NotebookConflictResolution{
            ISyncConflictResolver::ConflictResolution::MoveMine<
                qevercloud::Notebook>{*movedLocalConflict}};
    }

    EXPECT_CALL(*m_mockSyncConflictResolver, resolveNotebookConflict)
        .WillOnce([&, resolution](
                      const qevercloud::Notebook & theirs,
                      const qevercloud::Notebook & mine) mutable {
            EXPECT_EQ(theirs, notebook);
            EXPECT_EQ(mine, localConflict);
            return threading::makeReadyFuture<
                ISyncConflictResolver::NotebookConflictResolution>(
                std::move(resolution));
        });

    EXPECT_CALL(*m_mockLocalStorage, putNotebook)
        .WillRepeatedly([&, conflictGuid = notebook.guid()](
                            const qevercloud::Notebook & notebook) {
            if (Q_UNLIKELY(!notebook.guid())) {
                if (std::holds_alternative<
                        ISyncConflictResolver::ConflictResolution::MoveMine<
                            qevercloud::Notebook>>(resolution))
                {
                    const QMutexLocker locker{&mutex};
                    notebooksPutIntoLocalStorage << notebook;
                    return threading::makeReadyFuture();
                }

                return threading::makeExceptionalFuture<void>(RuntimeError{
                    ErrorString{"Detected notebook without guid"}});
            }

            const QMutexLocker locker{&mutex};
            EXPECT_TRUE(
                triedGuids.contains(*notebook.guid()) ||
                (movedLocalConflict && movedLocalConflict == notebook));

            if (Q_UNLIKELY(!notebook.name())) {
                return threading::makeExceptionalFuture<void>(RuntimeError{
                    ErrorString{"Detected notebook without name"}});
            }

            EXPECT_TRUE(
                triedNames.contains(*notebook.name()) ||
                notebook.guid() == conflictGuid ||
                (movedLocalConflict && movedLocalConflict == notebook));

            notebooksPutIntoLocalStorage << notebook;
            return threading::makeReadyFuture();
        });

    auto notebooks = QList<qevercloud::Notebook>{}
        << notebook
        << qevercloud::NotebookBuilder{}
               .setLocalId(UidGenerator::Generate())
               .setGuid(UidGenerator::Generate())
               .setName(QStringLiteral("Notebook #2"))
               .setUpdateSequenceNum(35)
               .build()
        << qevercloud::NotebookBuilder{}
               .setLocalId(UidGenerator::Generate())
               .setGuid(UidGenerator::Generate())
               .setName(QStringLiteral("Notebook #3"))
               .setUpdateSequenceNum(36)
               .build()
        << qevercloud::NotebookBuilder{}
               .setLocalId(UidGenerator::Generate())
               .setGuid(UidGenerator::Generate())
               .setName(QStringLiteral("Notebook #4"))
               .setUpdateSequenceNum(54)
               .build();

    const auto originalNotebooksSize = notebooks.size();

    const auto syncChunks = QList<qevercloud::SyncChunk>{}
        << qevercloud::SyncChunkBuilder{}.setNotebooks(notebooks).build();

    const auto notebooksProcessor = std::make_shared<NotebooksProcessor>(
        m_mockLocalStorage, m_mockSyncConflictResolver);

    qint32 totalNotebooks = 0;
    qint32 totalExpungedNotebooks = 0;
    qint32 addedNotebooks = 0;
    qint32 updatedNotebooks = 0;
    qint32 expungedNotebooks = 0;
    auto mockCallback = std::make_shared<StrictMock<MockICallback>>();
    EXPECT_CALL(*mockCallback, onNotebooksProcessingProgress)
        .WillRepeatedly([&](qint32 total, qint32 totalExpunged, qint32 added,
                            qint32 updated, qint32 expunged) {
            EXPECT_TRUE(totalNotebooks == 0 || totalNotebooks == total);
            totalNotebooks = total;

            EXPECT_TRUE(
                totalExpungedNotebooks == 0 ||
                totalExpungedNotebooks == totalExpunged);
            totalExpungedNotebooks = totalExpunged;

            addedNotebooks = added;
            updatedNotebooks = updated;
            expungedNotebooks = expunged;
        });

    auto future =
        notebooksProcessor->processNotebooks(syncChunks, mockCallback);

    waitForFuture(future);
    ASSERT_NO_THROW(future.waitForFinished());

    if (std::holds_alternative<
            ISyncConflictResolver::ConflictResolution::UseMine>(resolution))
    {
        notebooks.removeAt(0);
    }
    else if (std::holds_alternative<ISyncConflictResolver::ConflictResolution::
                                        MoveMine<qevercloud::Notebook>>(
                 resolution))
    {
        ASSERT_TRUE(movedLocalConflict);
        notebooks.push_front(*movedLocalConflict);
    }

    if (std::holds_alternative<
            ISyncConflictResolver::ConflictResolution::UseTheirs>(resolution))
    {
        notebooks[0].setLocalId(localConflict.localId());
    }
    compareNotebookLists(notebooksPutIntoLocalStorage, notebooks);

    EXPECT_EQ(totalNotebooks, originalNotebooksSize);
    EXPECT_EQ(totalExpungedNotebooks, 0);

    if (std::holds_alternative<
            ISyncConflictResolver::ConflictResolution::UseTheirs>(resolution) ||
        std::holds_alternative<
            ISyncConflictResolver::ConflictResolution::IgnoreMine>(
            resolution) ||
        std::holds_alternative<
            ISyncConflictResolver::ConflictResolution::UseMine>(resolution))
    {
        EXPECT_EQ(addedNotebooks, originalNotebooksSize - 1);

        if (std::holds_alternative<
                ISyncConflictResolver::ConflictResolution::UseMine>(resolution))
        {
            EXPECT_EQ(updatedNotebooks, 0);
        }
        else {
            EXPECT_EQ(updatedNotebooks, 1);
        }
    }
    else {
        EXPECT_EQ(addedNotebooks, originalNotebooksSize);
        EXPECT_EQ(updatedNotebooks, 0);
    }
}

} // namespace quentier::synchronization::tests
