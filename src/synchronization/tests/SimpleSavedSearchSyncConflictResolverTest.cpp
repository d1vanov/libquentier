/*
 * Copyright 2021-2023 Dmitry Ivanov
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

#include <synchronization/conflict_resolvers/SimpleSavedSearchSyncConflictResolver.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/local_storage/tests/mocks/MockILocalStorage.h>
#include <quentier/threading/Future.h>
#include <quentier/threading/QtFutureContinuations.h>
#include <quentier/utility/UidGenerator.h>

#include <QCoreApplication>

#include <gtest/gtest.h>

// clazy:excludeall=returning-void-expression

namespace quentier::synchronization::tests {

using testing::Return;
using testing::StrictMock;

class SimpleSavedSearchSyncConflictResolverTest : public testing::Test
{
protected:
    std::shared_ptr<local_storage::tests::mocks::MockILocalStorage>
        m_mockLocalStorage = std::make_shared<
            StrictMock<local_storage::tests::mocks::MockILocalStorage>>();
};

TEST_F(SimpleSavedSearchSyncConflictResolverTest, Ctor)
{
    EXPECT_NO_THROW(SimpleSavedSearchSyncConflictResolver{m_mockLocalStorage});
}

TEST_F(SimpleSavedSearchSyncConflictResolverTest, CtorNullLocalStorage)
{
    EXPECT_THROW(SimpleSavedSearchSyncConflictResolver{nullptr}, InvalidArgument);
}

TEST_F(SimpleSavedSearchSyncConflictResolverTest, ConflictWhenTheirsHasNoGuid)
{
    SimpleSavedSearchSyncConflictResolver resolver{m_mockLocalStorage};

    qevercloud::SavedSearch theirs;
    theirs.setName(QStringLiteral("theirs"));

    qevercloud::SavedSearch mine;
    mine.setName(QStringLiteral("mine"));
    mine.setGuid(UidGenerator::Generate());

    auto future =
        resolver.resolveSavedSearchConflict(std::move(theirs), std::move(mine));

    waitForFuture(future);
    EXPECT_THROW(future.result(), InvalidArgument);
}

TEST_F(SimpleSavedSearchSyncConflictResolverTest, ConflictWhenTheirsHasNoName)
{
    SimpleSavedSearchSyncConflictResolver resolver{m_mockLocalStorage};

    qevercloud::SavedSearch theirs;
    theirs.setGuid(UidGenerator::Generate());

    qevercloud::SavedSearch mine;
    mine.setName(QStringLiteral("mine"));
    mine.setGuid(UidGenerator::Generate());

    auto future =
        resolver.resolveSavedSearchConflict(std::move(theirs), std::move(mine));

    waitForFuture(future);
    EXPECT_THROW(future.result(), InvalidArgument);
}

TEST_F(
    SimpleSavedSearchSyncConflictResolverTest, ConflictWhenMineHasNoNameOrGuid)
{
    SimpleSavedSearchSyncConflictResolver resolver{m_mockLocalStorage};

    qevercloud::SavedSearch theirs;
    theirs.setGuid(UidGenerator::Generate());
    theirs.setName(QStringLiteral("theirs"));

    qevercloud::SavedSearch mine;

    auto future =
        resolver.resolveSavedSearchConflict(std::move(theirs), std::move(mine));

    waitForFuture(future);
    EXPECT_THROW(future.result(), InvalidArgument);
}

TEST_F(SimpleSavedSearchSyncConflictResolverTest, ConflictWithSameNameAndGuid)
{
    SimpleSavedSearchSyncConflictResolver resolver{m_mockLocalStorage};

    qevercloud::SavedSearch theirs;
    theirs.setGuid(UidGenerator::Generate());
    theirs.setName(QStringLiteral("name"));

    qevercloud::SavedSearch mine;
    mine.setGuid(theirs.guid());
    mine.setName(QStringLiteral("name"));

    auto future =
        resolver.resolveSavedSearchConflict(std::move(theirs), std::move(mine));

    waitForFuture(future);
    ASSERT_EQ(future.resultCount(), 1);

    EXPECT_TRUE(std::holds_alternative<
                ISyncConflictResolver::ConflictResolution::UseTheirs>(
        future.result()));
}

TEST_F(
    SimpleSavedSearchSyncConflictResolverTest,
    ConflictWithSameNameButDifferentGuid)
{
    SimpleSavedSearchSyncConflictResolver resolver{m_mockLocalStorage};

    qevercloud::SavedSearch theirs;
    theirs.setGuid(UidGenerator::Generate());
    theirs.setName(QStringLiteral("name"));

    qevercloud::SavedSearch mine;
    mine.setGuid(UidGenerator::Generate());
    mine.setName(QStringLiteral("name"));

    const QString newName =
        theirs.name().value() + QStringLiteral(" - conflicting");

    EXPECT_CALL(
        *m_mockLocalStorage,
        findSavedSearchByName(newName))
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::SavedSearch>>(
                std::nullopt)));

    const auto mineGuid = mine.guid();

    auto future =
        resolver.resolveSavedSearchConflict(std::move(theirs), std::move(mine));

    waitForFuture(future);
    ASSERT_EQ(future.resultCount(), 1);

    using MoveMine = ISyncConflictResolver::ConflictResolution::MoveMine<
        qevercloud::SavedSearch>;

    ASSERT_TRUE(std::holds_alternative<MoveMine>(future.result()));

    const auto resolution = std::get<MoveMine>(future.result());
    ASSERT_TRUE(resolution.mine.name());
    EXPECT_EQ(*resolution.mine.name(), newName);
    EXPECT_EQ(resolution.mine.guid(), mineGuid);
}

TEST_F(
    SimpleSavedSearchSyncConflictResolverTest,
    ConflictWithSameNameButDifferentGuidWithTwoStagesOfRenaming)
{
    SimpleSavedSearchSyncConflictResolver resolver{m_mockLocalStorage};

    qevercloud::SavedSearch theirs;
    theirs.setGuid(UidGenerator::Generate());
    theirs.setName(QStringLiteral("name"));

    qevercloud::SavedSearch mine;
    mine.setGuid(UidGenerator::Generate());
    mine.setName(QStringLiteral("name"));

    const QString newName1 =
        theirs.name().value() + QStringLiteral(" - conflicting");

    qevercloud::SavedSearch savedSearch;
    savedSearch.setName(newName1);

    EXPECT_CALL(
        *m_mockLocalStorage,
        findSavedSearchByName(newName1))
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::SavedSearch>>(
                savedSearch)));

    const QString newName2 = newName1 + QStringLiteral(" (2)");

    EXPECT_CALL(
        *m_mockLocalStorage,
        findSavedSearchByName(newName2))
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::SavedSearch>>(
                std::nullopt)));

    const auto mineGuid = mine.guid();

    auto future =
        resolver.resolveSavedSearchConflict(std::move(theirs), std::move(mine));

    waitForFuture(future);
    ASSERT_EQ(future.resultCount(), 1);

    using MoveMine = ISyncConflictResolver::ConflictResolution::MoveMine<
        qevercloud::SavedSearch>;

    ASSERT_TRUE(std::holds_alternative<MoveMine>(future.result()));

    const auto resolution = std::get<MoveMine>(future.result());
    ASSERT_TRUE(resolution.mine.name());
    EXPECT_EQ(*resolution.mine.name(), newName2);
    EXPECT_EQ(resolution.mine.guid(), mineGuid);
}

TEST_F(
    SimpleSavedSearchSyncConflictResolverTest,
    ConflictWithSameNameButDifferentGuidWithThreeStagesOfRenaming)
{
    SimpleSavedSearchSyncConflictResolver resolver{m_mockLocalStorage};

    qevercloud::SavedSearch theirs;
    theirs.setGuid(UidGenerator::Generate());
    theirs.setName(QStringLiteral("name"));

    qevercloud::SavedSearch mine;
    mine.setGuid(UidGenerator::Generate());
    mine.setName(QStringLiteral("name"));

    const QString newName1 =
        theirs.name().value() + QStringLiteral(" - conflicting");

    qevercloud::SavedSearch savedSearch;
    savedSearch.setName(newName1);

    EXPECT_CALL(
        *m_mockLocalStorage,
        findSavedSearchByName(newName1))
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::SavedSearch>>(
                savedSearch)));

    const QString newName2 = newName1 + QStringLiteral(" (2)");
    savedSearch.setName(newName2);

    EXPECT_CALL(
        *m_mockLocalStorage,
        findSavedSearchByName(newName2))
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::SavedSearch>>(
                savedSearch)));

    const QString newName3 = newName1 + QStringLiteral(" (3)");

    EXPECT_CALL(
        *m_mockLocalStorage,
        findSavedSearchByName(newName3))
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::SavedSearch>>(
                std::nullopt)));

    const auto mineGuid = mine.guid();

    auto future =
        resolver.resolveSavedSearchConflict(std::move(theirs), std::move(mine));

    waitForFuture(future);
    ASSERT_EQ(future.resultCount(), 1);

    using MoveMine = ISyncConflictResolver::ConflictResolution::MoveMine<
        qevercloud::SavedSearch>;

    ASSERT_TRUE(std::holds_alternative<MoveMine>(future.result()));

    const auto resolution = std::get<MoveMine>(future.result());
    ASSERT_TRUE(resolution.mine.name());
    EXPECT_EQ(*resolution.mine.name(), newName3);
    EXPECT_EQ(resolution.mine.guid(), mineGuid);
}

TEST_F(
    SimpleSavedSearchSyncConflictResolverTest,
    ConflictWithSameGuidButDifferentName)
{
    SimpleSavedSearchSyncConflictResolver resolver{m_mockLocalStorage};

    const auto guid = UidGenerator::Generate();

    qevercloud::SavedSearch theirs;
    theirs.setGuid(guid);
    theirs.setName(QStringLiteral("name1"));

    qevercloud::SavedSearch mine;
    mine.setGuid(guid);
    mine.setName(QStringLiteral("name2"));

    EXPECT_CALL(
        *m_mockLocalStorage,
        findSavedSearchByName(theirs.name().value()))
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::SavedSearch>>(
                std::nullopt)));

    auto future =
        resolver.resolveSavedSearchConflict(std::move(theirs), std::move(mine));

    waitForFuture(future);
    ASSERT_EQ(future.resultCount(), 1);

    ASSERT_TRUE(std::holds_alternative<
                ISyncConflictResolver::ConflictResolution::UseTheirs>(
        future.result()));
}

TEST_F(
    SimpleSavedSearchSyncConflictResolverTest,
    ConflictWithSameGuidButDifferentNameWithLocalConflictByName)
{
    SimpleSavedSearchSyncConflictResolver resolver{m_mockLocalStorage};

    const auto guid = UidGenerator::Generate();

    qevercloud::SavedSearch theirs;
    theirs.setGuid(guid);
    theirs.setName(QStringLiteral("name1"));

    qevercloud::SavedSearch mine;
    mine.setGuid(guid);
    mine.setName(QStringLiteral("name2"));

    qevercloud::SavedSearch savedSearch;
    savedSearch.setName(theirs.name());
    savedSearch.setGuid(UidGenerator::Generate());

    EXPECT_CALL(
        *m_mockLocalStorage,
        findSavedSearchByName(theirs.name().value()))
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::SavedSearch>>(
                savedSearch)));

    const QString newName =
        theirs.name().value() + QStringLiteral(" - conflicting");

    EXPECT_CALL(
        *m_mockLocalStorage,
        findSavedSearchByName(newName))
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::SavedSearch>>(
                std::nullopt)));

    const auto mineGuid = mine.guid();

    auto future =
        resolver.resolveSavedSearchConflict(std::move(theirs), std::move(mine));

    waitForFuture(future);
    ASSERT_EQ(future.resultCount(), 1);

    using MoveMine = ISyncConflictResolver::ConflictResolution::MoveMine<
        qevercloud::SavedSearch>;

    ASSERT_TRUE(std::holds_alternative<MoveMine>(future.result()));

    const auto resolution = std::get<MoveMine>(future.result());
    ASSERT_TRUE(resolution.mine.name());
    EXPECT_EQ(*resolution.mine.name(), newName);
    EXPECT_EQ(resolution.mine.guid(), savedSearch.guid());
}

TEST_F(
    SimpleSavedSearchSyncConflictResolverTest,
    HandleSelfDeletionDuringConflictingNameCheckingOnConflictByName)
{
    auto resolver = std::make_shared<SimpleSavedSearchSyncConflictResolver>(
        m_mockLocalStorage);

    qevercloud::SavedSearch theirs;
    theirs.setGuid(UidGenerator::Generate());
    theirs.setName(QStringLiteral("name"));

    qevercloud::SavedSearch mine;
    mine.setGuid(UidGenerator::Generate());
    mine.setName(QStringLiteral("name"));

    const QString newName =
        theirs.name().value() + QStringLiteral(" - conflicting");

    auto signalToResetPromise = std::make_shared<QPromise<void>>();
    auto signalToResetFuture = signalToResetPromise->future();
    signalToResetPromise->start();

    auto waitForResetPromise = std::make_shared<QPromise<void>>();

    auto findSavedSearchPromise =
        std::make_shared<QPromise<std::optional<qevercloud::SavedSearch>>>();

    auto findSavedSearchFuture = findSavedSearchPromise->future();

    std::weak_ptr<SimpleSavedSearchSyncConflictResolver> resolverWeak{resolver};

    EXPECT_CALL(
        *m_mockLocalStorage,
        findSavedSearchByName(newName))
        .WillOnce([=, signalToResetPromise = std::move(signalToResetPromise)](
                      QString newName) mutable // NOLINT
                  {
                      Q_UNUSED(newName)

                      EXPECT_FALSE(resolverWeak.expired());

                      threading::then(
                          waitForResetPromise->future(),
                          [=] () mutable {
                              EXPECT_TRUE(resolverWeak.expired());

                              // Now can fulfill the promise to find saved
                              // search
                              findSavedSearchPromise->start();
                              findSavedSearchPromise->addResult(std::nullopt);
                              findSavedSearchPromise->finish();

                              // Trigger the execution of lambda attached to the
                              // fulfilled promise's future via watcher
                              QCoreApplication::processEvents();
                          });

                      signalToResetPromise->finish();

                      // Trigger the execution of lambda attached to the
                      // fulfilled promise's future via watcher
                      QCoreApplication::processEvents();

                      return findSavedSearchFuture;
                  });

    auto resultFuture = resolver->resolveSavedSearchConflict(
        std::move(theirs), std::move(mine));

    threading::then(
        std::move(signalToResetFuture),
        [=, resolver = std::move(resolver)]() mutable {
            resolver.reset();

            waitForResetPromise->start();
            waitForResetPromise->finish();

            // Trigger the execution of lambda attached to the
            // fulfilled promise's future via watcher
            QCoreApplication::processEvents();
        });

    threading::then(
        std::move(findSavedSearchFuture),
        [=](std::optional<qevercloud::SavedSearch> savedSearch) mutable { // NOLINT
            Q_UNUSED(savedSearch)
            // Trigger the execution of lambda inside
            // SimpleGenericSyncConflictResolver::processConflictByName
            QCoreApplication::processEvents();
        });

    waitForFuture(resultFuture);
    EXPECT_THROW(resultFuture.waitForFinished(), RuntimeError);
}

TEST_F(
    SimpleSavedSearchSyncConflictResolverTest,
    HandleSelfDeletionDuringConflictingNameCheckingOnConflictByGuid)
{
    auto resolver = std::make_shared<SimpleSavedSearchSyncConflictResolver>(
        m_mockLocalStorage);

    const auto guid = UidGenerator::Generate();

    qevercloud::SavedSearch theirs;
    theirs.setGuid(guid);
    theirs.setName(QStringLiteral("name1"));

    qevercloud::SavedSearch mine;
    mine.setGuid(guid);
    mine.setName(QStringLiteral("name2"));

    auto signalToResetPromise = std::make_shared<QPromise<void>>();
    auto signalToResetFuture = signalToResetPromise->future();
    signalToResetPromise->start();

    auto waitForResetPromise = std::make_shared<QPromise<void>>();

    auto findSavedSearchPromise =
        std::make_shared<QPromise<std::optional<qevercloud::SavedSearch>>>();

    auto findSavedSearchFuture = findSavedSearchPromise->future();

    std::weak_ptr<SimpleSavedSearchSyncConflictResolver> resolverWeak{resolver};

    EXPECT_CALL(
        *m_mockLocalStorage,
        findSavedSearchByName(theirs.name().value()))
        .WillOnce([=, signalToResetPromise = std::move(signalToResetPromise)](
                      QString name) mutable // NOLINT
                  {
                      Q_UNUSED(name)

                      EXPECT_FALSE(resolverWeak.expired());

                      threading::then(
                          waitForResetPromise->future(),
                          [=] () mutable {
                              EXPECT_TRUE(resolverWeak.expired());

                              // Now can fulfill the promise to find notebook
                              findSavedSearchPromise->start();
                              findSavedSearchPromise->addResult(std::nullopt);
                              findSavedSearchPromise->finish();

                              // Trigger the execution of lambda attached to the
                              // fulfilled promise's future via watcher
                              QCoreApplication::processEvents();
                          });

                      signalToResetPromise->finish();

                      // Trigger the execution of lambda attached to the
                      // fulfilled promise's future via watcher
                      QCoreApplication::processEvents();

                      return findSavedSearchFuture;
                  });

    auto resultFuture =
        resolver->resolveSavedSearchConflict(std::move(theirs), std::move(mine));

    threading::then(
        std::move(signalToResetFuture),
        [=, resolver = std::move(resolver)]() mutable {
            resolver.reset();

            waitForResetPromise->start();
            waitForResetPromise->finish();

            // Trigger the execution of lambda attached to the
            // fulfilled promise's future via watcher
            QCoreApplication::processEvents();
        });

    threading::then(
        std::move(findSavedSearchFuture),
        [=](std::optional<qevercloud::SavedSearch> savedSearch) mutable { // NOLINT
            Q_UNUSED(savedSearch)
            // Trigger the execution of lambda inside
            // SimpleGenericSyncConflictResolver::processConflictByName
            QCoreApplication::processEvents();
        });

    waitForFuture(resultFuture);
    EXPECT_THROW(resultFuture.waitForFinished(), RuntimeError);
}

TEST_F(
    SimpleSavedSearchSyncConflictResolverTest,
    ForwardFindSavedSearchByNameErrorOnConflictByName)
{
    SimpleSavedSearchSyncConflictResolver resolver{m_mockLocalStorage};

    qevercloud::SavedSearch theirs;
    theirs.setGuid(UidGenerator::Generate());
    theirs.setName(QStringLiteral("name"));

    qevercloud::SavedSearch mine;
    mine.setGuid(UidGenerator::Generate());
    mine.setName(QStringLiteral("name"));

    const QString newName =
        theirs.name().value() + QStringLiteral(" - conflicting");

    EXPECT_CALL(
        *m_mockLocalStorage,
        findSavedSearchByName(newName))
        .WillOnce(Return(threading::makeExceptionalFuture<
                         std::optional<qevercloud::SavedSearch>>(
            RuntimeError{ErrorString{QStringLiteral("error")}})));

    auto resultFuture =
        resolver.resolveSavedSearchConflict(std::move(theirs), std::move(mine));

    EXPECT_THROW(resultFuture.waitForFinished(), RuntimeError);
}

TEST_F(
    SimpleSavedSearchSyncConflictResolverTest,
    ForwardFindSavedSearchByNameErrorOnConflictByGuid)
{
    SimpleSavedSearchSyncConflictResolver resolver{m_mockLocalStorage};

    const auto guid = UidGenerator::Generate();

    qevercloud::SavedSearch theirs;
    theirs.setGuid(guid);
    theirs.setName(QStringLiteral("name1"));

    qevercloud::SavedSearch mine;
    mine.setGuid(guid);
    mine.setName(QStringLiteral("name2"));

    EXPECT_CALL(
        *m_mockLocalStorage,
        findSavedSearchByName(theirs.name().value()))
        .WillOnce(Return(threading::makeExceptionalFuture<
                         std::optional<qevercloud::SavedSearch>>(
            RuntimeError{ErrorString{QStringLiteral("error")}})));

    auto resultFuture =
        resolver.resolveSavedSearchConflict(std::move(theirs), std::move(mine));

    waitForFuture(resultFuture);
    EXPECT_THROW(resultFuture.waitForFinished(), RuntimeError);
}

} // namespace quentier::synchronization::tests
