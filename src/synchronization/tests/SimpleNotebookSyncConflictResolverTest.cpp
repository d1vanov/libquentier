/*
 * Copyright 2021 Dmitry Ivanov
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

#include <synchronization/conflict_resolvers/SimpleNotebookSyncConflictResolver.h>

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

class SimpleNotebookSyncConflictResolverTest : public testing::Test
{
protected:
    std::shared_ptr<local_storage::tests::mocks::MockILocalStorage>
        m_mockLocalStorage = std::make_shared<
            StrictMock<local_storage::tests::mocks::MockILocalStorage>>();
};

TEST_F(SimpleNotebookSyncConflictResolverTest, Ctor)
{
    EXPECT_NO_THROW(SimpleNotebookSyncConflictResolver{m_mockLocalStorage});
}

TEST_F(SimpleNotebookSyncConflictResolverTest, CtorNullLocalStorage)
{
    EXPECT_THROW(SimpleNotebookSyncConflictResolver{nullptr}, InvalidArgument);
}

TEST_F(SimpleNotebookSyncConflictResolverTest, ConflictWhenTheirsHasNoGuid)
{
    SimpleNotebookSyncConflictResolver resolver{m_mockLocalStorage};

    qevercloud::Notebook theirs;
    theirs.setName(QStringLiteral("theirs"));

    qevercloud::Notebook mine;
    mine.setName(QStringLiteral("mine"));
    mine.setGuid(UidGenerator::Generate());

    auto future =
        resolver.resolveNotebookConflict(std::move(theirs), std::move(mine));

    EXPECT_THROW(future.result(), InvalidArgument);
}

TEST_F(SimpleNotebookSyncConflictResolverTest, ConflictWhenTheirsHasNoName)
{
    SimpleNotebookSyncConflictResolver resolver{m_mockLocalStorage};

    qevercloud::Notebook theirs;
    theirs.setGuid(UidGenerator::Generate());

    qevercloud::Notebook mine;
    mine.setName(QStringLiteral("mine"));
    mine.setGuid(UidGenerator::Generate());

    auto future =
        resolver.resolveNotebookConflict(std::move(theirs), std::move(mine));

    EXPECT_THROW(future.result(), InvalidArgument);
}

TEST_F(SimpleNotebookSyncConflictResolverTest, ConflictWhenMineHasNoNameOrGuid)
{
    SimpleNotebookSyncConflictResolver resolver{m_mockLocalStorage};

    qevercloud::Notebook theirs;
    theirs.setGuid(UidGenerator::Generate());
    theirs.setName(QStringLiteral("theirs"));

    qevercloud::Notebook mine;

    auto future =
        resolver.resolveNotebookConflict(std::move(theirs), std::move(mine));

    EXPECT_THROW(future.result(), InvalidArgument);
}

TEST_F(SimpleNotebookSyncConflictResolverTest, ConflictWithSameNameAndGuid)
{
    SimpleNotebookSyncConflictResolver resolver{m_mockLocalStorage};

    qevercloud::Notebook theirs;
    theirs.setGuid(UidGenerator::Generate());
    theirs.setName(QStringLiteral("name"));

    qevercloud::Notebook mine;
    mine.setGuid(theirs.guid());
    mine.setName(QStringLiteral("name"));

    auto future =
        resolver.resolveNotebookConflict(std::move(theirs), std::move(mine));

    ASSERT_TRUE(future.isFinished());
    EXPECT_TRUE(std::holds_alternative<
                ISyncConflictResolver::ConflictResolution::UseTheirs>(
        future.result()));
}

TEST_F(
    SimpleNotebookSyncConflictResolverTest,
    ConflictWithSameNameButDifferentGuid)
{
    SimpleNotebookSyncConflictResolver resolver{m_mockLocalStorage};

    qevercloud::Notebook theirs;
    theirs.setGuid(UidGenerator::Generate());
    theirs.setName(QStringLiteral("name"));

    qevercloud::Notebook mine;
    mine.setGuid(UidGenerator::Generate());
    mine.setName(QStringLiteral("name"));

    const QString newName =
        theirs.name().value() + QStringLiteral(" - conflicting");

    EXPECT_CALL(
        *m_mockLocalStorage,
        findNotebookByName(newName, std::optional<qevercloud::Guid>{}))
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::Notebook>>(
                std::nullopt)));

    const auto mineGuid = mine.guid();

    auto future =
        resolver.resolveNotebookConflict(std::move(theirs), std::move(mine));

    ASSERT_TRUE(future.isFinished());

    using MoveMine = ISyncConflictResolver::ConflictResolution::MoveMine<
        qevercloud::Notebook>;

    ASSERT_TRUE(std::holds_alternative<MoveMine>(future.result()));

    const auto resolution = std::get<MoveMine>(future.result());
    ASSERT_TRUE(resolution.mine.name());
    EXPECT_EQ(*resolution.mine.name(), newName);
    EXPECT_EQ(resolution.mine.guid(), mineGuid);
}

TEST_F(
    SimpleNotebookSyncConflictResolverTest,
    ConflictWithSameNameButDifferentGuidWithTwoStagesOfRenaming)
{
    SimpleNotebookSyncConflictResolver resolver{m_mockLocalStorage};

    qevercloud::Notebook theirs;
    theirs.setGuid(UidGenerator::Generate());
    theirs.setName(QStringLiteral("name"));

    qevercloud::Notebook mine;
    mine.setGuid(UidGenerator::Generate());
    mine.setName(QStringLiteral("name"));

    const QString newName1 =
        theirs.name().value() + QStringLiteral(" - conflicting");

    qevercloud::Notebook notebook;
    notebook.setName(newName1);

    EXPECT_CALL(
        *m_mockLocalStorage,
        findNotebookByName(newName1, std::optional<qevercloud::Guid>{}))
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::Notebook>>(
                notebook)));

    const QString newName2 = newName1 + QStringLiteral(" (2)");

    EXPECT_CALL(
        *m_mockLocalStorage,
        findNotebookByName(newName2, std::optional<qevercloud::Guid>{}))
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::Notebook>>(
                std::nullopt)));

    const auto mineGuid = mine.guid();

    auto future =
        resolver.resolveNotebookConflict(std::move(theirs), std::move(mine));

    ASSERT_TRUE(future.isFinished());

    using MoveMine = ISyncConflictResolver::ConflictResolution::MoveMine<
        qevercloud::Notebook>;

    ASSERT_TRUE(std::holds_alternative<MoveMine>(future.result()));

    const auto resolution = std::get<MoveMine>(future.result());
    ASSERT_TRUE(resolution.mine.name());
    EXPECT_EQ(*resolution.mine.name(), newName2);
    EXPECT_EQ(resolution.mine.guid(), mineGuid);
}

TEST_F(
    SimpleNotebookSyncConflictResolverTest,
    ConflictWithSameNameButDifferentGuidWithThreeStagesOfRenaming)
{
    SimpleNotebookSyncConflictResolver resolver{m_mockLocalStorage};

    qevercloud::Notebook theirs;
    theirs.setGuid(UidGenerator::Generate());
    theirs.setName(QStringLiteral("name"));

    qevercloud::Notebook mine;
    mine.setGuid(UidGenerator::Generate());
    mine.setName(QStringLiteral("name"));

    const QString newName1 =
        theirs.name().value() + QStringLiteral(" - conflicting");

    qevercloud::Notebook notebook;
    notebook.setName(newName1);

    EXPECT_CALL(
        *m_mockLocalStorage,
        findNotebookByName(newName1, std::optional<qevercloud::Guid>{}))
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::Notebook>>(
                notebook)));

    const QString newName2 = newName1 + QStringLiteral(" (2)");
    notebook.setName(newName2);

    EXPECT_CALL(
        *m_mockLocalStorage,
        findNotebookByName(newName2, std::optional<qevercloud::Guid>{}))
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::Notebook>>(
                notebook)));

    const QString newName3 = newName1 + QStringLiteral(" (3)");

    EXPECT_CALL(
        *m_mockLocalStorage,
        findNotebookByName(newName3, std::optional<qevercloud::Guid>{}))
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::Notebook>>(
                std::nullopt)));

    const auto mineGuid = mine.guid();

    auto future =
        resolver.resolveNotebookConflict(std::move(theirs), std::move(mine));

    ASSERT_TRUE(future.isFinished());

    using MoveMine = ISyncConflictResolver::ConflictResolution::MoveMine<
        qevercloud::Notebook>;

    ASSERT_TRUE(std::holds_alternative<MoveMine>(future.result()));

    const auto resolution = std::get<MoveMine>(future.result());
    ASSERT_TRUE(resolution.mine.name());
    EXPECT_EQ(*resolution.mine.name(), newName3);
    EXPECT_EQ(resolution.mine.guid(), mineGuid);
}

TEST_F(
    SimpleNotebookSyncConflictResolverTest,
    ConflictWithSameNameButDifferentGuidAndDifferentAffiliation)
{
    SimpleNotebookSyncConflictResolver resolver{m_mockLocalStorage};

    // Theirs would be from some linked notebook while mine would be
    // from user's own account

    qevercloud::Notebook theirs;
    theirs.setGuid(UidGenerator::Generate());
    theirs.setName(QStringLiteral("name"));
    theirs.setLinkedNotebookGuid(UidGenerator::Generate());

    qevercloud::Notebook mine;
    mine.setGuid(UidGenerator::Generate());
    mine.setName(QStringLiteral("name"));

    auto future =
        resolver.resolveNotebookConflict(std::move(theirs), std::move(mine));

    ASSERT_TRUE(future.isFinished());

    ASSERT_TRUE(std::holds_alternative<
                ISyncConflictResolver::ConflictResolution::IgnoreMine>(
        future.result()));
}

TEST_F(
    SimpleNotebookSyncConflictResolverTest,
    ConflictWithSameNameButDifferentGuidFromSameLinkedNotebook)
{
    SimpleNotebookSyncConflictResolver resolver{m_mockLocalStorage};

    qevercloud::Notebook theirs;
    theirs.setGuid(UidGenerator::Generate());
    theirs.setName(QStringLiteral("name"));
    theirs.setLinkedNotebookGuid(UidGenerator::Generate());

    qevercloud::Notebook mine;
    mine.setGuid(UidGenerator::Generate());
    mine.setName(QStringLiteral("name"));
    mine.setLinkedNotebookGuid(theirs.linkedNotebookGuid());

    const QString newName =
        theirs.name().value() + QStringLiteral(" - conflicting");

    EXPECT_CALL(
        *m_mockLocalStorage,
        findNotebookByName(newName, theirs.linkedNotebookGuid()))
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::Notebook>>(
                std::nullopt)));

    const auto mineGuid = mine.guid();

    auto future =
        resolver.resolveNotebookConflict(std::move(theirs), std::move(mine));

    ASSERT_TRUE(future.isFinished());

    using MoveMine = ISyncConflictResolver::ConflictResolution::MoveMine<
        qevercloud::Notebook>;

    ASSERT_TRUE(std::holds_alternative<MoveMine>(future.result()));

    const auto resolution = std::get<MoveMine>(future.result());
    ASSERT_TRUE(resolution.mine.name());
    EXPECT_EQ(*resolution.mine.name(), newName);
    EXPECT_EQ(resolution.mine.guid(), mineGuid);
}

TEST_F(
    SimpleNotebookSyncConflictResolverTest,
    ConflictWithSameNameButDifferentGuidFromSameLinkedNotebookWithTwoStagesOfRenaming)
{
    SimpleNotebookSyncConflictResolver resolver{m_mockLocalStorage};

    qevercloud::Notebook theirs;
    theirs.setGuid(UidGenerator::Generate());
    theirs.setName(QStringLiteral("name"));
    theirs.setLinkedNotebookGuid(UidGenerator::Generate());

    qevercloud::Notebook mine;
    mine.setGuid(UidGenerator::Generate());
    mine.setName(QStringLiteral("name"));
    mine.setLinkedNotebookGuid(theirs.linkedNotebookGuid());

    const QString newName1 =
        theirs.name().value() + QStringLiteral(" - conflicting");

    const auto linkedNotebookGuid = theirs.linkedNotebookGuid();

    qevercloud::Notebook notebook;
    notebook.setName(newName1);
    notebook.setLinkedNotebookGuid(linkedNotebookGuid);

    EXPECT_CALL(
        *m_mockLocalStorage, findNotebookByName(newName1, linkedNotebookGuid))
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::Notebook>>(
                notebook)));

    const QString newName2 = newName1 + QStringLiteral(" (2)");

    EXPECT_CALL(
        *m_mockLocalStorage, findNotebookByName(newName2, linkedNotebookGuid))
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::Notebook>>(
                std::nullopt)));

    const auto mineGuid = mine.guid();

    auto future =
        resolver.resolveNotebookConflict(std::move(theirs), std::move(mine));

    ASSERT_TRUE(future.isFinished());

    using MoveMine = ISyncConflictResolver::ConflictResolution::MoveMine<
        qevercloud::Notebook>;

    ASSERT_TRUE(std::holds_alternative<MoveMine>(future.result()));

    const auto resolution = std::get<MoveMine>(future.result());
    ASSERT_TRUE(resolution.mine.name());
    EXPECT_EQ(*resolution.mine.name(), newName2);
    EXPECT_EQ(resolution.mine.guid(), mineGuid);
}

TEST_F(
    SimpleNotebookSyncConflictResolverTest,
    ConflictWithSameNameButDifferentGuidFromSameLinkedNotebookWithThreeStagesOfRenaming)
{
    SimpleNotebookSyncConflictResolver resolver{m_mockLocalStorage};

    qevercloud::Notebook theirs;
    theirs.setGuid(UidGenerator::Generate());
    theirs.setName(QStringLiteral("name"));
    theirs.setLinkedNotebookGuid(UidGenerator::Generate());

    qevercloud::Notebook mine;
    mine.setGuid(UidGenerator::Generate());
    mine.setName(QStringLiteral("name"));
    mine.setLinkedNotebookGuid(theirs.linkedNotebookGuid());

    const QString newName1 =
        theirs.name().value() + QStringLiteral(" - conflicting");

    const auto linkedNotebookGuid = theirs.linkedNotebookGuid();

    qevercloud::Notebook notebook;
    notebook.setName(newName1);
    notebook.setLinkedNotebookGuid(linkedNotebookGuid);

    EXPECT_CALL(
        *m_mockLocalStorage, findNotebookByName(newName1, linkedNotebookGuid))
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::Notebook>>(
                notebook)));

    const QString newName2 = newName1 + QStringLiteral(" (2)");
    notebook.setName(newName2);

    EXPECT_CALL(
        *m_mockLocalStorage, findNotebookByName(newName2, linkedNotebookGuid))
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::Notebook>>(
                notebook)));

    const QString newName3 = newName1 + QStringLiteral(" (3)");

    EXPECT_CALL(
        *m_mockLocalStorage, findNotebookByName(newName3, linkedNotebookGuid))
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::Notebook>>(
                std::nullopt)));

    const auto mineGuid = mine.guid();

    auto future =
        resolver.resolveNotebookConflict(std::move(theirs), std::move(mine));

    ASSERT_TRUE(future.isFinished());

    using MoveMine = ISyncConflictResolver::ConflictResolution::MoveMine<
        qevercloud::Notebook>;

    ASSERT_TRUE(std::holds_alternative<MoveMine>(future.result()));

    const auto resolution = std::get<MoveMine>(future.result());
    ASSERT_TRUE(resolution.mine.name());
    EXPECT_EQ(*resolution.mine.name(), newName3);
    EXPECT_EQ(resolution.mine.guid(), mineGuid);
}

TEST_F(
    SimpleNotebookSyncConflictResolverTest,
    ConflictWithSameGuidButDifferentName)
{
    SimpleNotebookSyncConflictResolver resolver{m_mockLocalStorage};

    const auto guid = UidGenerator::Generate();

    qevercloud::Notebook theirs;
    theirs.setGuid(guid);
    theirs.setName(QStringLiteral("name1"));

    qevercloud::Notebook mine;
    mine.setGuid(guid);
    mine.setName(QStringLiteral("name2"));

    EXPECT_CALL(
        *m_mockLocalStorage,
        findNotebookByName(
            theirs.name().value(), std::optional<qevercloud::Guid>{}))
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::Notebook>>(
                std::nullopt)));

    auto future =
        resolver.resolveNotebookConflict(std::move(theirs), std::move(mine));

    ASSERT_TRUE(future.isFinished());

    ASSERT_TRUE(std::holds_alternative<
                ISyncConflictResolver::ConflictResolution::UseTheirs>(
        future.result()));
}

TEST_F(
    SimpleNotebookSyncConflictResolverTest,
    ConflictWithSameGuidButDifferentNameWithLocalConflictByName)
{
    SimpleNotebookSyncConflictResolver resolver{m_mockLocalStorage};

    const auto guid = UidGenerator::Generate();

    qevercloud::Notebook theirs;
    theirs.setGuid(guid);
    theirs.setName(QStringLiteral("name1"));

    qevercloud::Notebook mine;
    mine.setGuid(guid);
    mine.setName(QStringLiteral("name2"));

    qevercloud::Notebook notebook;
    notebook.setName(theirs.name());
    notebook.setGuid(UidGenerator::Generate());

    EXPECT_CALL(
        *m_mockLocalStorage,
        findNotebookByName(
            theirs.name().value(), std::optional<qevercloud::Guid>{}))
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::Notebook>>(
                notebook)));

    const QString newName =
        theirs.name().value() + QStringLiteral(" - conflicting");

    EXPECT_CALL(
        *m_mockLocalStorage,
        findNotebookByName(newName, theirs.linkedNotebookGuid()))
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::Notebook>>(
                std::nullopt)));

    const auto mineGuid = mine.guid();

    auto future =
        resolver.resolveNotebookConflict(std::move(theirs), std::move(mine));

    ASSERT_TRUE(future.isFinished());

    using MoveMine = ISyncConflictResolver::ConflictResolution::MoveMine<
        qevercloud::Notebook>;

    ASSERT_TRUE(std::holds_alternative<MoveMine>(future.result()));

    const auto resolution = std::get<MoveMine>(future.result());
    ASSERT_TRUE(resolution.mine.name());
    EXPECT_EQ(*resolution.mine.name(), newName);
    EXPECT_EQ(resolution.mine.guid(), notebook.guid());
}

TEST_F(
    SimpleNotebookSyncConflictResolverTest,
    HandleSelfDeletionDuringConflictingNameCheckingOnConflictByName)
{
    auto resolver = std::make_shared<SimpleNotebookSyncConflictResolver>(
        m_mockLocalStorage);

    qevercloud::Notebook theirs;
    theirs.setGuid(UidGenerator::Generate());
    theirs.setName(QStringLiteral("name"));

    qevercloud::Notebook mine;
    mine.setGuid(UidGenerator::Generate());
    mine.setName(QStringLiteral("name"));

    const QString newName =
        theirs.name().value() + QStringLiteral(" - conflicting");

    auto signalToResetPromise = std::make_shared<QPromise<void>>();
    auto signalToResetFuture = signalToResetPromise->future();
    signalToResetPromise->start();

    auto waitForResetPromise = std::make_shared<QPromise<void>>();

    auto findNotebookPromise =
        std::make_shared<QPromise<std::optional<qevercloud::Notebook>>>();

    auto findNotebookFuture = findNotebookPromise->future();

    std::weak_ptr<SimpleNotebookSyncConflictResolver> resolverWeak{resolver};

    // NOTE: there's the only one place in this test where blocking waiting
    // is used - in its end. Attempts to use blocking waiting in other places
    // of the test lead to QFuture<T>::waitForFinished() calls returning
    // before the future is really finished or canceled.

    EXPECT_CALL(
        *m_mockLocalStorage,
        findNotebookByName(newName, std::optional<qevercloud::Guid>{}))
        .WillOnce([=, signalToResetPromise = std::move(signalToResetPromise)](
                      QString newName, // NOLINT
                      std::optional<qevercloud::Guid>
                          linkedNotebookGuid) mutable // NOLINT
                  {
                      Q_UNUSED(newName)
                      Q_UNUSED(linkedNotebookGuid)

                      EXPECT_FALSE(resolverWeak.expired());

                      threading::then(
                          waitForResetPromise->future(),
                          [=] () mutable {
                              EXPECT_TRUE(resolverWeak.expired());

                              // Now can fulfill the promise to find notebook
                              findNotebookPromise->start();
                              findNotebookPromise->addResult(std::nullopt);
                              findNotebookPromise->finish();

                              // Trigger the execution of lambda attached to the
                              // fulfilled promise's future via watcher
                              QCoreApplication::processEvents();
                          });

                      signalToResetPromise->finish();

                      // Trigger the execution of lambda attached to the
                      // fulfilled promise's future via watcher
                      QCoreApplication::processEvents();

                      return findNotebookFuture;
                  });

    auto resultFuture =
        resolver->resolveNotebookConflict(std::move(theirs), std::move(mine));

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
        std::move(findNotebookFuture),
        [=](std::optional<qevercloud::Notebook> notebook) mutable { // NOLINT
            Q_UNUSED(notebook)
            // Trigger the execution of lambda inside
            // SimpleGenericSyncConflictResolver::processConflictByName
            QCoreApplication::processEvents();
        });

    // Trigger the execution of lambda attached to findNotebookByName
    // future inside SimpleGenericSyncConflictResolver::renameConflictingItem
    QCoreApplication::processEvents();

    EXPECT_THROW(resultFuture.waitForFinished(), RuntimeError);
}

} // namespace quentier::synchronization::tests
