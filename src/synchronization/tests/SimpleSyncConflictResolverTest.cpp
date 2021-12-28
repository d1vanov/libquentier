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

#include <synchronization/conflict_resolvers/SimpleSyncConflictResolver.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/threading/Future.h>

#include <synchronization/tests/mocks/MockISimpleNoteSyncConflictResolver.h>
#include <synchronization/tests/mocks/MockISimpleNotebookSyncConflictResolver.h>
#include <synchronization/tests/mocks/MockISimpleSavedSearchSyncConflictResolver.h>
#include <synchronization/tests/mocks/MockISimpleTagSyncConflictResolver.h>

#include <gtest/gtest.h>

// clazy:excludeall=returning-void-expression

namespace quentier::synchronization::tests {

using testing::Return;
using testing::StrictMock;

class SimpleSyncConflictResolverTest : public testing::Test
{
protected:
    std::shared_ptr<mocks::MockISimpleNotebookSyncConflictResolver>
        m_mockNotebookConflictResolver = std::make_shared<
            StrictMock<mocks::MockISimpleNotebookSyncConflictResolver>>();

    std::shared_ptr<mocks::MockISimpleNoteSyncConflictResolver>
        m_mockNoteConflictResolver = std::make_shared<
            StrictMock<mocks::MockISimpleNoteSyncConflictResolver>>();

    std::shared_ptr<mocks::MockISimpleSavedSearchSyncConflictResolver>
        m_mockSavedSearchConflictResolver = std::make_shared<
            StrictMock<mocks::MockISimpleSavedSearchSyncConflictResolver>>();

    std::shared_ptr<mocks::MockISimpleTagSyncConflictResolver>
        m_mockTagConflictResolver = std::make_shared<
            StrictMock<mocks::MockISimpleTagSyncConflictResolver>>();
};

TEST_F(SimpleSyncConflictResolverTest, Ctor)
{
    EXPECT_NO_THROW(SimpleSyncConflictResolver(
        m_mockNotebookConflictResolver, m_mockNoteConflictResolver,
        m_mockSavedSearchConflictResolver, m_mockTagConflictResolver));
}

TEST_F(SimpleSyncConflictResolverTest, CtorNullNotebookConflictResolver)
{
    EXPECT_THROW(
        SimpleSyncConflictResolver(
            nullptr, m_mockNoteConflictResolver,
            m_mockSavedSearchConflictResolver, m_mockTagConflictResolver),
        InvalidArgument);
}

TEST_F(SimpleSyncConflictResolverTest, CtorNullNoteConflictResolver)
{
    EXPECT_THROW(
        SimpleSyncConflictResolver(
            m_mockNotebookConflictResolver, nullptr,
            m_mockSavedSearchConflictResolver, m_mockTagConflictResolver),
        InvalidArgument);
}

TEST_F(SimpleSyncConflictResolverTest, CtorNullSavedSearchConflictResolver)
{
    EXPECT_THROW(
        SimpleSyncConflictResolver(
            m_mockNotebookConflictResolver, m_mockNoteConflictResolver, nullptr,
            m_mockTagConflictResolver),
        InvalidArgument);
}

TEST_F(SimpleSyncConflictResolverTest, CtorNullTagConflictResolver)
{
    EXPECT_THROW(
        SimpleSyncConflictResolver(
            m_mockNotebookConflictResolver, m_mockNoteConflictResolver,
            m_mockSavedSearchConflictResolver, nullptr),
        InvalidArgument);
}

TEST_F(SimpleSyncConflictResolverTest, DelegateToNotebookConflictResolver)
{
    SimpleSyncConflictResolver resolver{
        m_mockNotebookConflictResolver, m_mockNoteConflictResolver,
        m_mockSavedSearchConflictResolver, m_mockTagConflictResolver};

    qevercloud::Notebook theirs;
    theirs.setName(QStringLiteral("theirs"));

    qevercloud::Notebook mine;
    mine.setName(QStringLiteral("mine"));

    const auto resolution = ISyncConflictResolver::NotebookConflictResolution{
        ISyncConflictResolver::ConflictResolution::UseTheirs{}};

    EXPECT_CALL(
        *m_mockNotebookConflictResolver, resolveNotebookConflict(theirs, mine))
        .WillOnce(Return(threading::makeReadyFuture(resolution)));

    auto future =
        resolver.resolveNotebookConflict(std::move(theirs), std::move(mine));

    ASSERT_TRUE(future.isFinished());
    EXPECT_TRUE(std::holds_alternative<
                ISyncConflictResolver::ConflictResolution::UseTheirs>(
        future.result()));
}

TEST_F(SimpleSyncConflictResolverTest, DelegateToNoteConflictResolver)
{
    SimpleSyncConflictResolver resolver{
        m_mockNotebookConflictResolver, m_mockNoteConflictResolver,
        m_mockSavedSearchConflictResolver, m_mockTagConflictResolver};

    qevercloud::Note theirs;
    theirs.setTitle(QStringLiteral("theirs"));

    qevercloud::Note mine;
    mine.setTitle(QStringLiteral("mine"));

    const auto resolution = ISyncConflictResolver::NoteConflictResolution{
        ISyncConflictResolver::ConflictResolution::UseTheirs{}};

    EXPECT_CALL(*m_mockNoteConflictResolver, resolveNoteConflict(theirs, mine))
        .WillOnce(Return(threading::makeReadyFuture(resolution)));

    auto future =
        resolver.resolveNoteConflict(std::move(theirs), std::move(mine));

    ASSERT_TRUE(future.isFinished());
    EXPECT_TRUE(std::holds_alternative<
                ISyncConflictResolver::ConflictResolution::UseTheirs>(
        future.result()));
}

TEST_F(SimpleSyncConflictResolverTest, DelegateToSavedSearchConflictResolver)
{
    SimpleSyncConflictResolver resolver{
        m_mockNotebookConflictResolver, m_mockNoteConflictResolver,
        m_mockSavedSearchConflictResolver, m_mockTagConflictResolver};

    qevercloud::SavedSearch theirs;
    theirs.setName(QStringLiteral("theirs"));

    qevercloud::SavedSearch mine;
    mine.setName(QStringLiteral("mine"));

    const auto resolution =
        ISyncConflictResolver::SavedSearchConflictResolution{
            ISyncConflictResolver::ConflictResolution::UseTheirs{}};

    EXPECT_CALL(
        *m_mockSavedSearchConflictResolver,
        resolveSavedSearchConflict(theirs, mine))
        .WillOnce(Return(threading::makeReadyFuture(resolution)));

    auto future =
        resolver.resolveSavedSearchConflict(std::move(theirs), std::move(mine));

    ASSERT_TRUE(future.isFinished());
    EXPECT_TRUE(std::holds_alternative<
                ISyncConflictResolver::ConflictResolution::UseTheirs>(
        future.result()));
}

TEST_F(SimpleSyncConflictResolverTest, DelegateToTagConflictResolver)
{
    SimpleSyncConflictResolver resolver{
        m_mockNotebookConflictResolver, m_mockNoteConflictResolver,
        m_mockSavedSearchConflictResolver, m_mockTagConflictResolver};

    qevercloud::Tag theirs;
    theirs.setName(QStringLiteral("theirs"));

    qevercloud::Tag mine;
    mine.setName(QStringLiteral("mine"));

    const auto resolution =
        ISyncConflictResolver::TagConflictResolution{
            ISyncConflictResolver::ConflictResolution::UseTheirs{}};

    EXPECT_CALL(*m_mockTagConflictResolver, resolveTagConflict(theirs, mine))
        .WillOnce(Return(threading::makeReadyFuture(resolution)));

    auto future =
        resolver.resolveTagConflict(std::move(theirs), std::move(mine));

    ASSERT_TRUE(future.isFinished());
    EXPECT_TRUE(std::holds_alternative<
                ISyncConflictResolver::ConflictResolution::UseTheirs>(
        future.result()));
}

} // namespace quentier::synchronization::tests
