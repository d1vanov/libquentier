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

#include <synchronization/processors/SavedSearchesProcessor.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/RuntimeError.h>
#include <quentier/local_storage/tests/mocks/MockILocalStorage.h>
#include <quentier/synchronization/tests/mocks/MockISyncConflictResolver.h>
#include <quentier/threading/Future.h>
#include <quentier/utility/UidGenerator.h>

#include <qevercloud/types/builders/SavedSearchBuilder.h>
#include <qevercloud/types/builders/SyncChunkBuilder.h>

#include <QSet>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>

// clazy:excludeall=non-pod-global-static
// clazy:excludeall=returning-void-expression

namespace quentier::synchronization::tests {

using testing::StrictMock;

class SavedSearchesProcessorTest : public testing::Test
{
protected:
    std::shared_ptr<local_storage::tests::mocks::MockILocalStorage>
        m_mockLocalStorage = std::make_shared<
            StrictMock<local_storage::tests::mocks::MockILocalStorage>>();

    std::shared_ptr<mocks::MockISyncConflictResolver>
        m_mockSyncConflictResolver =
            std::make_shared<StrictMock<mocks::MockISyncConflictResolver>>();
};

TEST_F(SavedSearchesProcessorTest, Ctor)
{
    EXPECT_NO_THROW(
        const auto savedSearchesProcessor =
            std::make_shared<SavedSearchesProcessor>(
                m_mockLocalStorage, m_mockSyncConflictResolver));
}

TEST_F(SavedSearchesProcessorTest, CtorNullLocalStorage)
{
    EXPECT_THROW(
        const auto savedSearchesProcessor =
            std::make_shared<SavedSearchesProcessor>(
                nullptr, m_mockSyncConflictResolver),
        InvalidArgument);
}

TEST_F(SavedSearchesProcessorTest, CtorNullSyncConflictResolver)
{
    EXPECT_THROW(
        const auto savedSearchesProcessor =
            std::make_shared<SavedSearchesProcessor>(
                m_mockLocalStorage, nullptr),
        InvalidArgument);
}

TEST_F(
    SavedSearchesProcessorTest, ProcessSyncChunksWithoutSavedSearchesToProcess)
{
    const auto syncChunks = QList<qevercloud::SyncChunk>{}
        << qevercloud::SyncChunkBuilder{}.build();

    const auto savedSearchesProcessor =
        std::make_shared<SavedSearchesProcessor>(
            m_mockLocalStorage, m_mockSyncConflictResolver);

    auto future = savedSearchesProcessor->processSavedSearches(syncChunks);
    ASSERT_TRUE(future.isFinished());
    EXPECT_NO_THROW(future.waitForFinished());
}

TEST_F(SavedSearchesProcessorTest, ProcessSavedSearchesWithoutConflicts)
{
    const auto savedSearches = QList<qevercloud::SavedSearch>{}
        << qevercloud::SavedSearchBuilder{}
               .setGuid(UidGenerator::Generate())
               .setName(QStringLiteral("Saved search #1"))
               .setUpdateSequenceNum(0)
               .build()
        << qevercloud::SavedSearchBuilder{}
               .setGuid(UidGenerator::Generate())
               .setName(QStringLiteral("Saved search #2"))
               .setUpdateSequenceNum(35)
               .build()
        << qevercloud::SavedSearchBuilder{}
               .setGuid(UidGenerator::Generate())
               .setName(QStringLiteral("Saved search #3"))
               .setUpdateSequenceNum(36)
               .build()
        << qevercloud::SavedSearchBuilder{}
               .setGuid(UidGenerator::Generate())
               .setName(QStringLiteral("Saved search #4"))
               .setUpdateSequenceNum(54)
               .build();

    QList<qevercloud::SavedSearch> savedSearchesPutIntoLocalStorage;
    QSet<qevercloud::Guid> triedGuids;
    QSet<QString> triedNames;

    EXPECT_CALL(*m_mockLocalStorage, findSavedSearchByGuid)
        .WillRepeatedly([&](const qevercloud::Guid & guid) {
            EXPECT_FALSE(triedGuids.contains(guid));
            triedGuids.insert(guid);

            const auto it = std::find_if(
                savedSearchesPutIntoLocalStorage.constBegin(),
                savedSearchesPutIntoLocalStorage.constEnd(),
                [&](const qevercloud::SavedSearch & savedSearch) {
                    return savedSearch.guid() && (*savedSearch.guid() == guid);
                });
            if (it != savedSearchesPutIntoLocalStorage.constEnd()) {
                return threading::makeReadyFuture<
                    std::optional<qevercloud::SavedSearch>>(*it);
            }

            return threading::makeReadyFuture<
                std::optional<qevercloud::SavedSearch>>(std::nullopt);
        });

    EXPECT_CALL(*m_mockLocalStorage, findSavedSearchByName)
        .WillRepeatedly([&](const QString & name) {
            EXPECT_FALSE(triedNames.contains(name));
            triedNames.insert(name);

            const auto it = std::find_if(
                savedSearchesPutIntoLocalStorage.constBegin(),
                savedSearchesPutIntoLocalStorage.constEnd(),
                [&](const qevercloud::SavedSearch & savedSearch) {
                    return savedSearch.name() && (*savedSearch.name() == name);
                });
            if (it != savedSearchesPutIntoLocalStorage.constEnd()) {
                return threading::makeReadyFuture<
                    std::optional<qevercloud::SavedSearch>>(*it);
            }

            return threading::makeReadyFuture<
                std::optional<qevercloud::SavedSearch>>(std::nullopt);
        });

    EXPECT_CALL(*m_mockLocalStorage, putSavedSearch)
        .WillRepeatedly([&](const qevercloud::SavedSearch & savedSearch) {
            if (Q_UNLIKELY(!savedSearch.guid())) {
                return threading::makeExceptionalFuture<void>(RuntimeError{
                    ErrorString{"Detected saved search without guid"}});
            }

            EXPECT_TRUE(triedGuids.contains(*savedSearch.guid()));

            if (Q_UNLIKELY(!savedSearch.name())) {
                return threading::makeExceptionalFuture<void>(RuntimeError{
                    ErrorString{"Detected saved search without name"}});
            }

            EXPECT_TRUE(triedNames.contains(*savedSearch.name()));

            savedSearchesPutIntoLocalStorage << savedSearch;
            return threading::makeReadyFuture();
        });

    const auto syncChunks = QList<qevercloud::SyncChunk>{}
        << qevercloud::SyncChunkBuilder{}.setSearches(savedSearches).build();

    const auto savedSearchesProcessor =
        std::make_shared<SavedSearchesProcessor>(
            m_mockLocalStorage, m_mockSyncConflictResolver);

    auto future = savedSearchesProcessor->processSavedSearches(syncChunks);
    ASSERT_TRUE(future.isFinished());
    EXPECT_NO_THROW(future.waitForFinished());

    EXPECT_EQ(savedSearchesPutIntoLocalStorage, savedSearches);
}

TEST_F(SavedSearchesProcessorTest, ProcessExpungedSavedSearches)
{
    const auto expungedSavedSearchGuids = QList<qevercloud::Guid>{}
        << UidGenerator::Generate() << UidGenerator::Generate()
        << UidGenerator::Generate();

    const auto syncChunks = QList<qevercloud::SyncChunk>{}
        << qevercloud::SyncChunkBuilder{}
               .setExpungedSearches(expungedSavedSearchGuids)
               .build();

    const auto savedSearchesProcessor =
        std::make_shared<SavedSearchesProcessor>(
            m_mockLocalStorage, m_mockSyncConflictResolver);

    QList<qevercloud::Guid> processedSavedSearchGuids;
    EXPECT_CALL(*m_mockLocalStorage, expungeSavedSearchByGuid)
        .WillRepeatedly([&](const qevercloud::Guid & savedSearchGuid) {
            processedSavedSearchGuids << savedSearchGuid;
            return threading::makeReadyFuture();
        });

    auto future = savedSearchesProcessor->processSavedSearches(syncChunks);
    ASSERT_TRUE(future.isFinished());
    EXPECT_NO_THROW(future.waitForFinished());

    EXPECT_EQ(processedSavedSearchGuids, expungedSavedSearchGuids);
}

class SavedSearchesProcessorTestWithConflict :
    public SavedSearchesProcessorTest,
    public testing::WithParamInterface<
        ISyncConflictResolver::SavedSearchConflictResolution>
{};

const auto gConflictResolutions = std::array{
    ISyncConflictResolver::SavedSearchConflictResolution{
        ISyncConflictResolver::ConflictResolution::UseTheirs{}},
    ISyncConflictResolver::SavedSearchConflictResolution{
        ISyncConflictResolver::ConflictResolution::UseMine{}},
    ISyncConflictResolver::SavedSearchConflictResolution{
        ISyncConflictResolver::ConflictResolution::IgnoreMine{}},
    ISyncConflictResolver::SavedSearchConflictResolution{
        ISyncConflictResolver::ConflictResolution::MoveMine<
            qevercloud::SavedSearch>{qevercloud::SavedSearch{}}}};

INSTANTIATE_TEST_SUITE_P(
    SavedSearchesProcessorTestWithConflictInstance,
    SavedSearchesProcessorTestWithConflict,
    testing::ValuesIn(gConflictResolutions));

TEST_P(SavedSearchesProcessorTestWithConflict, HandleConflictByGuid)
{
    const auto savedSearch = qevercloud::SavedSearchBuilder{}
                                 .setGuid(UidGenerator::Generate())
                                 .setName(QStringLiteral("Saved search #1"))
                                 .setUpdateSequenceNum(1)
                                 .build();

    const auto localConflict =
        qevercloud::SavedSearchBuilder{}
            .setGuid(savedSearch.guid())
            .setName(savedSearch.name())
            .setUpdateSequenceNum(savedSearch.updateSequenceNum().value() - 1)
            .build();

    QList<qevercloud::SavedSearch> savedSearchesPutIntoLocalStorage;
    QSet<qevercloud::Guid> triedGuids;
    QSet<QString> triedNames;

    EXPECT_CALL(*m_mockLocalStorage, findSavedSearchByGuid)
        .WillRepeatedly([&](const qevercloud::Guid & guid) {
            EXPECT_FALSE(triedGuids.contains(guid));
            triedGuids.insert(guid);

            const auto it = std::find_if(
                savedSearchesPutIntoLocalStorage.constBegin(),
                savedSearchesPutIntoLocalStorage.constEnd(),
                [&](const qevercloud::SavedSearch & savedSearch) {
                    return savedSearch.guid() && (*savedSearch.guid() == guid);
                });
            if (it != savedSearchesPutIntoLocalStorage.constEnd()) {
                return threading::makeReadyFuture<
                    std::optional<qevercloud::SavedSearch>>(*it);
            }

            if (guid == savedSearch.guid()) {
                return threading::makeReadyFuture<
                    std::optional<qevercloud::SavedSearch>>(localConflict);
            }

            return threading::makeReadyFuture<
                std::optional<qevercloud::SavedSearch>>(std::nullopt);
        });

    auto resolution = GetParam();
    std::optional<qevercloud::SavedSearch> movedLocalConflict;
    if (std::holds_alternative<ISyncConflictResolver::ConflictResolution::
                                   MoveMine<qevercloud::SavedSearch>>(
            resolution))
    {
        movedLocalConflict =
            qevercloud::SavedSearchBuilder{}
                .setGuid(UidGenerator::Generate())
                .setName(
                    localConflict.name().value() + QStringLiteral("_moved"))
                .build();

        resolution = ISyncConflictResolver::SavedSearchConflictResolution{
            ISyncConflictResolver::ConflictResolution::MoveMine<
                qevercloud::SavedSearch>{*movedLocalConflict}};
    }

    EXPECT_CALL(*m_mockSyncConflictResolver, resolveSavedSearchConflict)
        .WillOnce([&, resolution](
                      const qevercloud::SavedSearch & theirs,
                      const qevercloud::SavedSearch & mine) mutable {
            EXPECT_EQ(theirs, savedSearch);
            EXPECT_EQ(mine, localConflict);
            return threading::makeReadyFuture<
                ISyncConflictResolver::SavedSearchConflictResolution>(
                std::move(resolution));
        });

    EXPECT_CALL(*m_mockLocalStorage, findSavedSearchByName)
        .WillRepeatedly([&](const QString & name) {
            EXPECT_FALSE(triedNames.contains(name));
            triedNames.insert(name);

            const auto it = std::find_if(
                savedSearchesPutIntoLocalStorage.constBegin(),
                savedSearchesPutIntoLocalStorage.constEnd(),
                [&](const qevercloud::SavedSearch & savedSearch) {
                    return savedSearch.name() && (*savedSearch.name() == name);
                });
            if (it != savedSearchesPutIntoLocalStorage.constEnd()) {
                return threading::makeReadyFuture<
                    std::optional<qevercloud::SavedSearch>>(*it);
            }

            return threading::makeReadyFuture<
                std::optional<qevercloud::SavedSearch>>(std::nullopt);
        });

    EXPECT_CALL(*m_mockLocalStorage, putSavedSearch)
        .WillRepeatedly([&, conflictGuid = savedSearch.guid()](
                            const qevercloud::SavedSearch & savedSearch) {
            if (Q_UNLIKELY(!savedSearch.guid())) {
                return threading::makeExceptionalFuture<void>(RuntimeError{
                    ErrorString{"Detected saved search without guid"}});
            }

            EXPECT_TRUE(
                triedGuids.contains(*savedSearch.guid()) ||
                (movedLocalConflict && movedLocalConflict == savedSearch));

            if (Q_UNLIKELY(!savedSearch.name())) {
                return threading::makeExceptionalFuture<void>(RuntimeError{
                    ErrorString{"Detected saved search without name"}});
            }

            EXPECT_TRUE(
                triedNames.contains(*savedSearch.name()) ||
                savedSearch.guid() == conflictGuid ||
                (movedLocalConflict && movedLocalConflict == savedSearch));

            savedSearchesPutIntoLocalStorage << savedSearch;
            return threading::makeReadyFuture();
        });

    auto savedSearches = QList<qevercloud::SavedSearch>{}
        << savedSearch
        << qevercloud::SavedSearchBuilder{}
               .setGuid(UidGenerator::Generate())
               .setName(QStringLiteral("Saved search #2"))
               .setUpdateSequenceNum(35)
               .build()
        << qevercloud::SavedSearchBuilder{}
               .setGuid(UidGenerator::Generate())
               .setName(QStringLiteral("Saved search #3"))
               .setUpdateSequenceNum(36)
               .build()
        << qevercloud::SavedSearchBuilder{}
               .setGuid(UidGenerator::Generate())
               .setName(QStringLiteral("Saved search #4"))
               .setUpdateSequenceNum(54)
               .build();

    const auto syncChunks = QList<qevercloud::SyncChunk>{}
        << qevercloud::SyncChunkBuilder{}.setSearches(savedSearches).build();

    const auto savedSearchesProcessor =
        std::make_shared<SavedSearchesProcessor>(
            m_mockLocalStorage, m_mockSyncConflictResolver);

    auto future = savedSearchesProcessor->processSavedSearches(syncChunks);
    ASSERT_TRUE(future.isFinished());
    EXPECT_NO_THROW(future.waitForFinished());

    if (std::holds_alternative<
            ISyncConflictResolver::ConflictResolution::UseMine>(resolution))
    {
        savedSearches.removeAt(0);
    }
    else if (std::holds_alternative<ISyncConflictResolver::ConflictResolution::
                                        MoveMine<qevercloud::SavedSearch>>(
                 resolution))
    {
        ASSERT_TRUE(movedLocalConflict);
        savedSearches.push_front(*movedLocalConflict);
    }

    EXPECT_EQ(savedSearchesPutIntoLocalStorage, savedSearches);
}

TEST_P(SavedSearchesProcessorTestWithConflict, HandleConflictByName)
{
    const auto savedSearch = qevercloud::SavedSearchBuilder{}
                                 .setGuid(UidGenerator::Generate())
                                 .setName(QStringLiteral("Saved search #1"))
                                 .setUpdateSequenceNum(1)
                                 .build();

    const auto localConflict =
        qevercloud::SavedSearchBuilder{}.setName(savedSearch.name()).build();

    QList<qevercloud::SavedSearch> savedSearchesPutIntoLocalStorage;
    QSet<qevercloud::Guid> triedGuids;
    QSet<QString> triedNames;

    EXPECT_CALL(*m_mockLocalStorage, findSavedSearchByGuid)
        .WillRepeatedly([&](const qevercloud::Guid & guid) {
            EXPECT_FALSE(triedGuids.contains(guid));
            triedGuids.insert(guid);

            const auto it = std::find_if(
                savedSearchesPutIntoLocalStorage.constBegin(),
                savedSearchesPutIntoLocalStorage.constEnd(),
                [&](const qevercloud::SavedSearch & savedSearch) {
                    return savedSearch.guid() && (*savedSearch.guid() == guid);
                });
            if (it != savedSearchesPutIntoLocalStorage.constEnd()) {
                return threading::makeReadyFuture<
                    std::optional<qevercloud::SavedSearch>>(*it);
            }

            return threading::makeReadyFuture<
                std::optional<qevercloud::SavedSearch>>(std::nullopt);
        });

    EXPECT_CALL(*m_mockLocalStorage, findSavedSearchByName)
        .WillRepeatedly([&, conflictName = savedSearch.name()](
                            const QString & name) {
            EXPECT_FALSE(triedNames.contains(name));
            triedNames.insert(name);

            const auto it = std::find_if(
                savedSearchesPutIntoLocalStorage.constBegin(),
                savedSearchesPutIntoLocalStorage.constEnd(),
                [&](const qevercloud::SavedSearch & savedSearch) {
                    return savedSearch.name() && (*savedSearch.name() == name);
                });
            if (it != savedSearchesPutIntoLocalStorage.constEnd()) {
                return threading::makeReadyFuture<
                    std::optional<qevercloud::SavedSearch>>(*it);
            }

            if (name == conflictName) {
                return threading::makeReadyFuture<
                    std::optional<qevercloud::SavedSearch>>(localConflict);
            }

            return threading::makeReadyFuture<
                std::optional<qevercloud::SavedSearch>>(std::nullopt);
        });

    auto resolution = GetParam();
    std::optional<qevercloud::SavedSearch> movedLocalConflict;
    if (std::holds_alternative<ISyncConflictResolver::ConflictResolution::
                                   MoveMine<qevercloud::SavedSearch>>(
            resolution))
    {
        movedLocalConflict =
            qevercloud::SavedSearchBuilder{}
                .setGuid(UidGenerator::Generate())
                .setName(
                    localConflict.name().value() + QStringLiteral("_moved"))
                .build();

        resolution = ISyncConflictResolver::SavedSearchConflictResolution{
            ISyncConflictResolver::ConflictResolution::MoveMine<
                qevercloud::SavedSearch>{*movedLocalConflict}};
    }

    EXPECT_CALL(*m_mockSyncConflictResolver, resolveSavedSearchConflict)
        .WillOnce([&, resolution](
                      const qevercloud::SavedSearch & theirs,
                      const qevercloud::SavedSearch & mine) mutable {
            EXPECT_EQ(theirs, savedSearch);
            EXPECT_EQ(mine, localConflict);
            return threading::makeReadyFuture<
                ISyncConflictResolver::SavedSearchConflictResolution>(
                std::move(resolution));
        });

    EXPECT_CALL(*m_mockLocalStorage, putSavedSearch)
        .WillRepeatedly([&, conflictGuid = savedSearch.guid()](
                            const qevercloud::SavedSearch & savedSearch) {
            if (Q_UNLIKELY(!savedSearch.guid())) {
                return threading::makeExceptionalFuture<void>(RuntimeError{
                    ErrorString{"Detected saved search without guid"}});
            }

            EXPECT_TRUE(
                triedGuids.contains(*savedSearch.guid()) ||
                (movedLocalConflict && movedLocalConflict == savedSearch));

            if (Q_UNLIKELY(!savedSearch.name())) {
                return threading::makeExceptionalFuture<void>(RuntimeError{
                    ErrorString{"Detected saved search without name"}});
            }

            EXPECT_TRUE(
                triedNames.contains(*savedSearch.name()) ||
                savedSearch.guid() == conflictGuid ||
                (movedLocalConflict && movedLocalConflict == savedSearch));

            savedSearchesPutIntoLocalStorage << savedSearch;
            return threading::makeReadyFuture();
        });

    auto savedSearches = QList<qevercloud::SavedSearch>{}
        << savedSearch
        << qevercloud::SavedSearchBuilder{}
               .setGuid(UidGenerator::Generate())
               .setName(QStringLiteral("Saved search #2"))
               .setUpdateSequenceNum(35)
               .build()
        << qevercloud::SavedSearchBuilder{}
               .setGuid(UidGenerator::Generate())
               .setName(QStringLiteral("Saved search #3"))
               .setUpdateSequenceNum(36)
               .build()
        << qevercloud::SavedSearchBuilder{}
               .setGuid(UidGenerator::Generate())
               .setName(QStringLiteral("Saved search #4"))
               .setUpdateSequenceNum(54)
               .build();

    const auto syncChunks = QList<qevercloud::SyncChunk>{}
        << qevercloud::SyncChunkBuilder{}.setSearches(savedSearches).build();

    const auto savedSearchesProcessor =
        std::make_shared<SavedSearchesProcessor>(
            m_mockLocalStorage, m_mockSyncConflictResolver);

    auto future = savedSearchesProcessor->processSavedSearches(syncChunks);
    ASSERT_TRUE(future.isFinished());
    EXPECT_NO_THROW(future.waitForFinished());

    if (std::holds_alternative<
            ISyncConflictResolver::ConflictResolution::UseMine>(resolution))
    {
        savedSearches.removeAt(0);
    }
    else if (std::holds_alternative<ISyncConflictResolver::ConflictResolution::
                                        MoveMine<qevercloud::SavedSearch>>(
                 resolution))
    {
        ASSERT_TRUE(movedLocalConflict);
        savedSearches.push_front(*movedLocalConflict);
    }

    EXPECT_EQ(savedSearchesPutIntoLocalStorage, savedSearches);
}

} // namespace quentier::synchronization::tests
