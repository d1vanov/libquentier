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

#include <synchronization/conflict_resolvers/SimpleTagSyncConflictResolver.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/RuntimeError.h>
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

class SimpleTagSyncConflictResolverTest : public testing::Test
{
protected:
    std::shared_ptr<local_storage::tests::mocks::MockILocalStorage>
        m_mockLocalStorage = std::make_shared<
            StrictMock<local_storage::tests::mocks::MockILocalStorage>>();
};

TEST_F(SimpleTagSyncConflictResolverTest, Ctor)
{
    EXPECT_NO_THROW(SimpleTagSyncConflictResolver{m_mockLocalStorage});
}

TEST_F(SimpleTagSyncConflictResolverTest, CtorNullLocalStorage)
{
    EXPECT_THROW(SimpleTagSyncConflictResolver{nullptr}, InvalidArgument);
}

TEST_F(SimpleTagSyncConflictResolverTest, ConflictWhenTheirsHasNoGuid)
{
    SimpleTagSyncConflictResolver resolver{m_mockLocalStorage};

    qevercloud::Tag theirs;
    theirs.setName(QStringLiteral("theirs"));

    qevercloud::Tag mine;
    mine.setName(QStringLiteral("mine"));
    mine.setGuid(UidGenerator::Generate());

    auto future =
        resolver.resolveTagConflict(std::move(theirs), std::move(mine));

    waitForFuture(future);
    EXPECT_THROW(future.result(), InvalidArgument);
}

TEST_F(SimpleTagSyncConflictResolverTest, ConflictWhenTheirsHasNoName)
{
    SimpleTagSyncConflictResolver resolver{m_mockLocalStorage};

    qevercloud::Tag theirs;
    theirs.setGuid(UidGenerator::Generate());

    qevercloud::Tag mine;
    mine.setName(QStringLiteral("mine"));
    mine.setGuid(UidGenerator::Generate());

    auto future =
        resolver.resolveTagConflict(std::move(theirs), std::move(mine));

    waitForFuture(future);
    EXPECT_THROW(future.result(), InvalidArgument);
}

TEST_F(SimpleTagSyncConflictResolverTest, ConflictWhenMineHasNoNameOrGuid)
{
    SimpleTagSyncConflictResolver resolver{m_mockLocalStorage};

    qevercloud::Tag theirs;
    theirs.setGuid(UidGenerator::Generate());
    theirs.setName(QStringLiteral("theirs"));

    qevercloud::Tag mine;

    auto future =
        resolver.resolveTagConflict(std::move(theirs), std::move(mine));

    waitForFuture(future);
    EXPECT_THROW(future.result(), InvalidArgument);
}

TEST_F(SimpleTagSyncConflictResolverTest, ConflictWithSameNameAndGuid)
{
    SimpleTagSyncConflictResolver resolver{m_mockLocalStorage};

    qevercloud::Tag theirs;
    theirs.setGuid(UidGenerator::Generate());
    theirs.setName(QStringLiteral("name"));

    qevercloud::Tag mine;
    mine.setGuid(theirs.guid());
    mine.setName(QStringLiteral("name"));

    auto future =
        resolver.resolveTagConflict(std::move(theirs), std::move(mine));

    waitForFuture(future);
    ASSERT_EQ(future.resultCount(), 1);

    EXPECT_TRUE(std::holds_alternative<
                ISyncConflictResolver::ConflictResolution::UseTheirs>(
        future.result()));
}

TEST_F(SimpleTagSyncConflictResolverTest, ConflictWithSameNameButDifferentGuid)
{
    SimpleTagSyncConflictResolver resolver{m_mockLocalStorage};

    qevercloud::Tag theirs;
    theirs.setGuid(UidGenerator::Generate());
    theirs.setName(QStringLiteral("name"));

    qevercloud::Tag mine;
    mine.setGuid(UidGenerator::Generate());
    mine.setName(QStringLiteral("name"));

    const QString newName =
        theirs.name().value() + QStringLiteral(" - conflicting");

    EXPECT_CALL(
        *m_mockLocalStorage,
        findTagByName(newName, std::optional<qevercloud::Guid>{}))
        .WillOnce(
            Return(threading::makeReadyFuture<std::optional<qevercloud::Tag>>(
                std::nullopt)));

    const auto mineGuid = mine.guid();

    auto future =
        resolver.resolveTagConflict(std::move(theirs), std::move(mine));

    waitForFuture(future);
    ASSERT_EQ(future.resultCount(), 1);

    using MoveMine =
        ISyncConflictResolver::ConflictResolution::MoveMine<qevercloud::Tag>;

    ASSERT_TRUE(std::holds_alternative<MoveMine>(future.result()));

    const auto resolution = std::get<MoveMine>(future.result());
    ASSERT_TRUE(resolution.mine.name());
    EXPECT_EQ(*resolution.mine.name(), newName);
    EXPECT_EQ(resolution.mine.guid(), mineGuid);
}

TEST_F(
    SimpleTagSyncConflictResolverTest,
    ConflictWithSameNameButDifferentGuidWithTwoStagesOfRenaming)
{
    SimpleTagSyncConflictResolver resolver{m_mockLocalStorage};

    qevercloud::Tag theirs;
    theirs.setGuid(UidGenerator::Generate());
    theirs.setName(QStringLiteral("name"));

    qevercloud::Tag mine;
    mine.setGuid(UidGenerator::Generate());
    mine.setName(QStringLiteral("name"));

    const QString newName1 =
        theirs.name().value() + QStringLiteral(" - conflicting");

    qevercloud::Tag tag;
    tag.setName(newName1);

    EXPECT_CALL(
        *m_mockLocalStorage,
        findTagByName(newName1, std::optional<qevercloud::Guid>{}))
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::Tag>>(tag)));

    const QString newName2 = newName1 + QStringLiteral(" (2)");

    EXPECT_CALL(
        *m_mockLocalStorage,
        findTagByName(newName2, std::optional<qevercloud::Guid>{}))
        .WillOnce(
            Return(threading::makeReadyFuture<std::optional<qevercloud::Tag>>(
                std::nullopt)));

    const auto mineGuid = mine.guid();

    auto future =
        resolver.resolveTagConflict(std::move(theirs), std::move(mine));

    waitForFuture(future);
    ASSERT_EQ(future.resultCount(), 1);

    using MoveMine =
        ISyncConflictResolver::ConflictResolution::MoveMine<qevercloud::Tag>;

    ASSERT_TRUE(std::holds_alternative<MoveMine>(future.result()));

    const auto resolution = std::get<MoveMine>(future.result());
    ASSERT_TRUE(resolution.mine.name());
    EXPECT_EQ(*resolution.mine.name(), newName2);
    EXPECT_EQ(resolution.mine.guid(), mineGuid);
}

TEST_F(
    SimpleTagSyncConflictResolverTest,
    ConflictWithSameNameButDifferentGuidWithThreeStagesOfRenaming)
{
    SimpleTagSyncConflictResolver resolver{m_mockLocalStorage};

    qevercloud::Tag theirs;
    theirs.setGuid(UidGenerator::Generate());
    theirs.setName(QStringLiteral("name"));

    qevercloud::Tag mine;
    mine.setGuid(UidGenerator::Generate());
    mine.setName(QStringLiteral("name"));

    const QString newName1 =
        theirs.name().value() + QStringLiteral(" - conflicting");

    qevercloud::Tag tag;
    tag.setName(newName1);

    EXPECT_CALL(
        *m_mockLocalStorage,
        findTagByName(newName1, std::optional<qevercloud::Guid>{}))
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::Tag>>(tag)));

    const QString newName2 = newName1 + QStringLiteral(" (2)");
    tag.setName(newName2);

    EXPECT_CALL(
        *m_mockLocalStorage,
        findTagByName(newName2, std::optional<qevercloud::Guid>{}))
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::Tag>>(tag)));

    const QString newName3 = newName1 + QStringLiteral(" (3)");

    EXPECT_CALL(
        *m_mockLocalStorage,
        findTagByName(newName3, std::optional<qevercloud::Guid>{}))
        .WillOnce(
            Return(threading::makeReadyFuture<std::optional<qevercloud::Tag>>(
                std::nullopt)));

    const auto mineGuid = mine.guid();

    auto future =
        resolver.resolveTagConflict(std::move(theirs), std::move(mine));

    waitForFuture(future);
    ASSERT_EQ(future.resultCount(), 1);

    using MoveMine =
        ISyncConflictResolver::ConflictResolution::MoveMine<qevercloud::Tag>;

    ASSERT_TRUE(std::holds_alternative<MoveMine>(future.result()));

    const auto resolution = std::get<MoveMine>(future.result());
    ASSERT_TRUE(resolution.mine.name());
    EXPECT_EQ(*resolution.mine.name(), newName3);
    EXPECT_EQ(resolution.mine.guid(), mineGuid);
}

TEST_F(
    SimpleTagSyncConflictResolverTest,
    ConflictWithSameNameButDifferentGuidAndDifferentAffiliation)
{
    SimpleTagSyncConflictResolver resolver{m_mockLocalStorage};

    // Theirs would be from some linked tag while mine would be
    // from user's own account

    qevercloud::Tag theirs;
    theirs.setGuid(UidGenerator::Generate());
    theirs.setName(QStringLiteral("name"));
    theirs.setLinkedNotebookGuid(UidGenerator::Generate());

    qevercloud::Tag mine;
    mine.setGuid(UidGenerator::Generate());
    mine.setName(QStringLiteral("name"));

    auto future =
        resolver.resolveTagConflict(std::move(theirs), std::move(mine));

    waitForFuture(future);
    ASSERT_EQ(future.resultCount(), 1);

    ASSERT_TRUE(std::holds_alternative<
                ISyncConflictResolver::ConflictResolution::IgnoreMine>(
        future.result()));
}

TEST_F(
    SimpleTagSyncConflictResolverTest,
    ConflictWithSameNameButDifferentGuidFromSameLinkedTag)
{
    SimpleTagSyncConflictResolver resolver{m_mockLocalStorage};

    qevercloud::Tag theirs;
    theirs.setGuid(UidGenerator::Generate());
    theirs.setName(QStringLiteral("name"));
    theirs.setLinkedNotebookGuid(UidGenerator::Generate());

    qevercloud::Tag mine;
    mine.setGuid(UidGenerator::Generate());
    mine.setName(QStringLiteral("name"));
    mine.setLinkedNotebookGuid(theirs.linkedNotebookGuid());

    const QString newName =
        theirs.name().value() + QStringLiteral(" - conflicting");

    EXPECT_CALL(
        *m_mockLocalStorage,
        findTagByName(newName, theirs.linkedNotebookGuid()))
        .WillOnce(
            Return(threading::makeReadyFuture<std::optional<qevercloud::Tag>>(
                std::nullopt)));

    const auto mineGuid = mine.guid();

    auto future =
        resolver.resolveTagConflict(std::move(theirs), std::move(mine));

    waitForFuture(future);
    ASSERT_EQ(future.resultCount(), 1);

    using MoveMine =
        ISyncConflictResolver::ConflictResolution::MoveMine<qevercloud::Tag>;

    ASSERT_TRUE(std::holds_alternative<MoveMine>(future.result()));

    const auto resolution = std::get<MoveMine>(future.result());
    ASSERT_TRUE(resolution.mine.name());
    EXPECT_EQ(*resolution.mine.name(), newName);
    EXPECT_EQ(resolution.mine.guid(), mineGuid);
}

TEST_F(
    SimpleTagSyncConflictResolverTest,
    ConflictWithSameNameButDifferentGuidFromSameLinkedTagWithTwoStagesOfRenaming)
{
    SimpleTagSyncConflictResolver resolver{m_mockLocalStorage};

    qevercloud::Tag theirs;
    theirs.setGuid(UidGenerator::Generate());
    theirs.setName(QStringLiteral("name"));
    theirs.setLinkedNotebookGuid(UidGenerator::Generate());

    qevercloud::Tag mine;
    mine.setGuid(UidGenerator::Generate());
    mine.setName(QStringLiteral("name"));
    mine.setLinkedNotebookGuid(theirs.linkedNotebookGuid());

    const QString newName1 =
        theirs.name().value() + QStringLiteral(" - conflicting");

    const auto linkedNotebookGuid = theirs.linkedNotebookGuid();

    qevercloud::Tag tag;
    tag.setName(newName1);
    tag.setLinkedNotebookGuid(linkedNotebookGuid);

    EXPECT_CALL(
        *m_mockLocalStorage, findTagByName(newName1, linkedNotebookGuid))
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::Tag>>(tag)));

    const QString newName2 = newName1 + QStringLiteral(" (2)");

    EXPECT_CALL(
        *m_mockLocalStorage, findTagByName(newName2, linkedNotebookGuid))
        .WillOnce(
            Return(threading::makeReadyFuture<std::optional<qevercloud::Tag>>(
                std::nullopt)));

    const auto mineGuid = mine.guid();

    auto future =
        resolver.resolveTagConflict(std::move(theirs), std::move(mine));

    waitForFuture(future);
    ASSERT_EQ(future.resultCount(), 1);

    using MoveMine =
        ISyncConflictResolver::ConflictResolution::MoveMine<qevercloud::Tag>;

    ASSERT_TRUE(std::holds_alternative<MoveMine>(future.result()));

    const auto resolution = std::get<MoveMine>(future.result());
    ASSERT_TRUE(resolution.mine.name());
    EXPECT_EQ(*resolution.mine.name(), newName2);
    EXPECT_EQ(resolution.mine.guid(), mineGuid);
}

TEST_F(
    SimpleTagSyncConflictResolverTest,
    ConflictWithSameNameButDifferentGuidFromSameLinkedTagWithThreeStagesOfRenaming)
{
    SimpleTagSyncConflictResolver resolver{m_mockLocalStorage};

    qevercloud::Tag theirs;
    theirs.setGuid(UidGenerator::Generate());
    theirs.setName(QStringLiteral("name"));
    theirs.setLinkedNotebookGuid(UidGenerator::Generate());

    qevercloud::Tag mine;
    mine.setGuid(UidGenerator::Generate());
    mine.setName(QStringLiteral("name"));
    mine.setLinkedNotebookGuid(theirs.linkedNotebookGuid());

    const QString newName1 =
        theirs.name().value() + QStringLiteral(" - conflicting");

    const auto linkedNotebookGuid = theirs.linkedNotebookGuid();

    qevercloud::Tag tag;
    tag.setName(newName1);
    tag.setLinkedNotebookGuid(linkedNotebookGuid);

    EXPECT_CALL(
        *m_mockLocalStorage, findTagByName(newName1, linkedNotebookGuid))
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::Tag>>(tag)));

    const QString newName2 = newName1 + QStringLiteral(" (2)");
    tag.setName(newName2);

    EXPECT_CALL(
        *m_mockLocalStorage, findTagByName(newName2, linkedNotebookGuid))
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::Tag>>(tag)));

    const QString newName3 = newName1 + QStringLiteral(" (3)");

    EXPECT_CALL(
        *m_mockLocalStorage, findTagByName(newName3, linkedNotebookGuid))
        .WillOnce(
            Return(threading::makeReadyFuture<std::optional<qevercloud::Tag>>(
                std::nullopt)));

    const auto mineGuid = mine.guid();

    auto future =
        resolver.resolveTagConflict(std::move(theirs), std::move(mine));

    waitForFuture(future);
    ASSERT_EQ(future.resultCount(), 1);

    using MoveMine =
        ISyncConflictResolver::ConflictResolution::MoveMine<qevercloud::Tag>;

    ASSERT_TRUE(std::holds_alternative<MoveMine>(future.result()));

    const auto resolution = std::get<MoveMine>(future.result());
    ASSERT_TRUE(resolution.mine.name());
    EXPECT_EQ(*resolution.mine.name(), newName3);
    EXPECT_EQ(resolution.mine.guid(), mineGuid);
}

TEST_F(SimpleTagSyncConflictResolverTest, ConflictWithSameGuidButDifferentName)
{
    SimpleTagSyncConflictResolver resolver{m_mockLocalStorage};

    const auto guid = UidGenerator::Generate();

    qevercloud::Tag theirs;
    theirs.setGuid(guid);
    theirs.setName(QStringLiteral("name1"));

    qevercloud::Tag mine;
    mine.setGuid(guid);
    mine.setName(QStringLiteral("name2"));

    EXPECT_CALL(
        *m_mockLocalStorage,
        findTagByName(theirs.name().value(), std::optional<qevercloud::Guid>{}))
        .WillOnce(
            Return(threading::makeReadyFuture<std::optional<qevercloud::Tag>>(
                std::nullopt)));

    auto future =
        resolver.resolveTagConflict(std::move(theirs), std::move(mine));

    waitForFuture(future);
    ASSERT_EQ(future.resultCount(), 1);

    ASSERT_TRUE(std::holds_alternative<
                ISyncConflictResolver::ConflictResolution::UseTheirs>(
        future.result()));
}

TEST_F(
    SimpleTagSyncConflictResolverTest,
    ConflictWithSameGuidButDifferentNameWithLocalConflictByName)
{
    SimpleTagSyncConflictResolver resolver{m_mockLocalStorage};

    const auto guid = UidGenerator::Generate();

    qevercloud::Tag theirs;
    theirs.setGuid(guid);
    theirs.setName(QStringLiteral("name1"));

    qevercloud::Tag mine;
    mine.setGuid(guid);
    mine.setName(QStringLiteral("name2"));

    qevercloud::Tag tag;
    tag.setName(theirs.name());
    tag.setGuid(UidGenerator::Generate());

    EXPECT_CALL(
        *m_mockLocalStorage,
        findTagByName(theirs.name().value(), std::optional<qevercloud::Guid>{}))
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::Tag>>(tag)));

    const QString newName =
        theirs.name().value() + QStringLiteral(" - conflicting");

    EXPECT_CALL(
        *m_mockLocalStorage,
        findTagByName(newName, theirs.linkedNotebookGuid()))
        .WillOnce(
            Return(threading::makeReadyFuture<std::optional<qevercloud::Tag>>(
                std::nullopt)));

    const auto mineGuid = mine.guid();

    auto future =
        resolver.resolveTagConflict(std::move(theirs), std::move(mine));

    waitForFuture(future);
    ASSERT_EQ(future.resultCount(), 1);

    using MoveMine =
        ISyncConflictResolver::ConflictResolution::MoveMine<qevercloud::Tag>;

    ASSERT_TRUE(std::holds_alternative<MoveMine>(future.result()));

    const auto resolution = std::get<MoveMine>(future.result());
    ASSERT_TRUE(resolution.mine.name());
    EXPECT_EQ(*resolution.mine.name(), newName);
    EXPECT_EQ(resolution.mine.guid(), tag.guid());
}

TEST_F(
    SimpleTagSyncConflictResolverTest,
    HandleSelfDeletionDuringConflictingNameCheckingOnConflictByName)
{
    auto resolver =
        std::make_shared<SimpleTagSyncConflictResolver>(m_mockLocalStorage);

    qevercloud::Tag theirs;
    theirs.setGuid(UidGenerator::Generate());
    theirs.setName(QStringLiteral("name"));

    qevercloud::Tag mine;
    mine.setGuid(UidGenerator::Generate());
    mine.setName(QStringLiteral("name"));

    const QString newName =
        theirs.name().value() + QStringLiteral(" - conflicting");

    auto signalToResetPromise = std::make_shared<QPromise<void>>();
    auto signalToResetFuture = signalToResetPromise->future();
    signalToResetPromise->start();

    auto waitForResetPromise = std::make_shared<QPromise<void>>();

    auto findTagPromise =
        std::make_shared<QPromise<std::optional<qevercloud::Tag>>>();

    auto findTagFuture = findTagPromise->future();

    std::weak_ptr<SimpleTagSyncConflictResolver> resolverWeak{resolver};

    EXPECT_CALL(
        *m_mockLocalStorage,
        findTagByName(newName, std::optional<qevercloud::Guid>{}))
        .WillOnce(
            [=, signalToResetPromise = std::move(signalToResetPromise)](
                QString newName, // NOLINT
                std::optional<qevercloud::Guid>
                    linkedNotebookGuid) mutable // NOLINT
            {
                Q_UNUSED(newName)
                Q_UNUSED(linkedNotebookGuid)

                EXPECT_FALSE(resolverWeak.expired());

                threading::then(waitForResetPromise->future(), [=]() mutable {
                    EXPECT_TRUE(resolverWeak.expired());

                    // Now can fulfill the promise to find tag
                    findTagPromise->start();
                    findTagPromise->addResult(std::nullopt);
                    findTagPromise->finish();

                    // Trigger the execution of lambda attached to the
                    // fulfilled promise's future via watcher
                    QCoreApplication::processEvents();
                });

                signalToResetPromise->finish();

                // Trigger the execution of lambda attached to the
                // fulfilled promise's future via watcher
                QCoreApplication::processEvents();

                return findTagFuture;
            });

    auto resultFuture =
        resolver->resolveTagConflict(std::move(theirs), std::move(mine));

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
        std::move(findTagFuture),
        [=](std::optional<qevercloud::Tag> tag) mutable { // NOLINT
            Q_UNUSED(tag)
            // Trigger the execution of lambda inside
            // SimpleGenericSyncConflictResolver::processConflictByName
            QCoreApplication::processEvents();
        });

    waitForFuture(resultFuture);
    EXPECT_THROW(resultFuture.waitForFinished(), RuntimeError);
}

TEST_F(
    SimpleTagSyncConflictResolverTest,
    HandleSelfDeletionDuringConflictingNameCheckingOnConflictByGuid)
{
    auto resolver =
        std::make_shared<SimpleTagSyncConflictResolver>(m_mockLocalStorage);

    const auto guid = UidGenerator::Generate();

    qevercloud::Tag theirs;
    theirs.setGuid(guid);
    theirs.setName(QStringLiteral("name1"));

    qevercloud::Tag mine;
    mine.setGuid(guid);
    mine.setName(QStringLiteral("name2"));

    auto signalToResetPromise = std::make_shared<QPromise<void>>();
    auto signalToResetFuture = signalToResetPromise->future();
    signalToResetPromise->start();

    auto waitForResetPromise = std::make_shared<QPromise<void>>();

    auto findTagPromise =
        std::make_shared<QPromise<std::optional<qevercloud::Tag>>>();

    auto findTagFuture = findTagPromise->future();

    std::weak_ptr<SimpleTagSyncConflictResolver> resolverWeak{resolver};

    EXPECT_CALL(
        *m_mockLocalStorage,
        findTagByName(theirs.name().value(), std::optional<qevercloud::Guid>{}))
        .WillOnce(
            [=, signalToResetPromise = std::move(signalToResetPromise)](
                QString name, // NOLINT
                std::optional<qevercloud::Guid>
                    linkedNotebookGuid) mutable // NOLINT
            {
                Q_UNUSED(name)
                Q_UNUSED(linkedNotebookGuid)

                EXPECT_FALSE(resolverWeak.expired());

                threading::then(waitForResetPromise->future(), [=]() mutable {
                    EXPECT_TRUE(resolverWeak.expired());

                    // Now can fulfill the promise to find tag
                    findTagPromise->start();
                    findTagPromise->addResult(std::nullopt);
                    findTagPromise->finish();

                    // Trigger the execution of lambda attached to the
                    // fulfilled promise's future via watcher
                    QCoreApplication::processEvents();
                });

                signalToResetPromise->finish();

                // Trigger the execution of lambda attached to the
                // fulfilled promise's future via watcher
                QCoreApplication::processEvents();

                return findTagFuture;
            });

    auto resultFuture =
        resolver->resolveTagConflict(std::move(theirs), std::move(mine));

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
        std::move(findTagFuture),
        [=](std::optional<qevercloud::Tag> tag) mutable { // NOLINT
            Q_UNUSED(tag)
            // Trigger the execution of lambda inside
            // SimpleGenericSyncConflictResolver::processConflictByName
            QCoreApplication::processEvents();
        });

    waitForFuture(resultFuture);
    EXPECT_THROW(resultFuture.waitForFinished(), RuntimeError);
}

TEST_F(
    SimpleTagSyncConflictResolverTest,
    ForwardFindTagByNameErrorOnConflictByName)
{
    SimpleTagSyncConflictResolver resolver{m_mockLocalStorage};

    qevercloud::Tag theirs;
    theirs.setGuid(UidGenerator::Generate());
    theirs.setName(QStringLiteral("name"));

    qevercloud::Tag mine;
    mine.setGuid(UidGenerator::Generate());
    mine.setName(QStringLiteral("name"));

    const QString newName =
        theirs.name().value() + QStringLiteral(" - conflicting");

    EXPECT_CALL(
        *m_mockLocalStorage,
        findTagByName(newName, std::optional<qevercloud::Guid>{}))
        .WillOnce(Return(
            threading::makeExceptionalFuture<std::optional<qevercloud::Tag>>(
                RuntimeError{ErrorString{QStringLiteral("error")}})));

    auto resultFuture =
        resolver.resolveTagConflict(std::move(theirs), std::move(mine));

    waitForFuture(resultFuture);
    EXPECT_THROW(resultFuture.waitForFinished(), RuntimeError);
}

TEST_F(
    SimpleTagSyncConflictResolverTest,
    ForwardFindTagByNameErrorOnConflictByGuid)
{
    SimpleTagSyncConflictResolver resolver{m_mockLocalStorage};

    const auto guid = UidGenerator::Generate();

    qevercloud::Tag theirs;
    theirs.setGuid(guid);
    theirs.setName(QStringLiteral("name1"));

    qevercloud::Tag mine;
    mine.setGuid(guid);
    mine.setName(QStringLiteral("name2"));

    EXPECT_CALL(
        *m_mockLocalStorage,
        findTagByName(theirs.name().value(), std::optional<qevercloud::Guid>{}))
        .WillOnce(Return(
            threading::makeExceptionalFuture<std::optional<qevercloud::Tag>>(
                RuntimeError{ErrorString{QStringLiteral("error")}})));

    auto resultFuture =
        resolver.resolveTagConflict(std::move(theirs), std::move(mine));

    waitForFuture(resultFuture);
    EXPECT_THROW(resultFuture.waitForFinished(), RuntimeError);
}

} // namespace quentier::synchronization::tests
