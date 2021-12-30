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

#include <synchronization/conflict_resolvers/SimpleNoteSyncConflictResolver.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/utility/UidGenerator.h>

#include <gtest/gtest.h>

// clazy:excludeall=returning-void-expression

namespace quentier::synchronization::tests {

TEST(SimpleNoteSyncConflictResolverTest, ConflictWhenTheirsHasNoGuid)
{
    SimpleNoteSyncConflictResolver resolver;

    qevercloud::Note theirs;
    theirs.setUpdateSequenceNum(42);

    qevercloud::Note mine;
    mine.setGuid(UidGenerator::Generate());
    mine.setUpdateSequenceNum(41);

    auto future =
        resolver.resolveNoteConflict(std::move(theirs), std::move(mine));

    ASSERT_TRUE(future.isFinished());
    EXPECT_THROW(future.result(), InvalidArgument);
}

TEST(
    SimpleNoteSyncConflictResolverTest,
    ConflictWhenTheirsHasNoUpdateSequenceNumber)
{
    SimpleNoteSyncConflictResolver resolver;

    const auto guid = UidGenerator::Generate();

    qevercloud::Note theirs;
    theirs.setGuid(guid);

    qevercloud::Note mine;
    mine.setGuid(guid);
    mine.setUpdateSequenceNum(41);

    auto future =
        resolver.resolveNoteConflict(std::move(theirs), std::move(mine));

    ASSERT_TRUE(future.isFinished());
    EXPECT_THROW(future.result(), InvalidArgument);
}

TEST(SimpleNoteSyncConflictResolverTest, ConflictWhenMineHasNoGuid)
{
    SimpleNoteSyncConflictResolver resolver;

    qevercloud::Note theirs;
    theirs.setGuid(UidGenerator::Generate());
    theirs.setUpdateSequenceNum(42);

    qevercloud::Note mine;
    mine.setUpdateSequenceNum(41);

    auto future =
        resolver.resolveNoteConflict(std::move(theirs), std::move(mine));

    ASSERT_TRUE(future.isFinished());
    EXPECT_THROW(future.result(), InvalidArgument);
}

TEST(SimpleNoteSyncConflictResolverTest, ConflictWhenGuidsDontMatch)
{
    SimpleNoteSyncConflictResolver resolver;

    qevercloud::Note theirs;
    theirs.setGuid(UidGenerator::Generate());
    theirs.setUpdateSequenceNum(42);

    qevercloud::Note mine;
    mine.setGuid(UidGenerator::Generate());
    mine.setUpdateSequenceNum(41);

    auto future =
        resolver.resolveNoteConflict(std::move(theirs), std::move(mine));

    ASSERT_TRUE(future.isFinished());
    EXPECT_TRUE(std::holds_alternative<
                ISyncConflictResolver::ConflictResolution::IgnoreMine>(
                    future.result()));
}

TEST(
    SimpleNoteSyncConflictResolverTest,
    PreferMineWhenMineUpdateSequenceNumberIsGreater)
{
    SimpleNoteSyncConflictResolver resolver;

    const auto guid = UidGenerator::Generate();

    qevercloud::Note theirs;
    theirs.setGuid(guid);
    theirs.setUpdateSequenceNum(42);

    qevercloud::Note mine;
    mine.setGuid(guid);
    mine.setUpdateSequenceNum(theirs.updateSequenceNum().value() + 1);

    auto future =
        resolver.resolveNoteConflict(std::move(theirs), std::move(mine));

    ASSERT_TRUE(future.isFinished());
    EXPECT_TRUE(std::holds_alternative<
                ISyncConflictResolver::ConflictResolution::UseMine>(
                    future.result()));
}

TEST(
    SimpleNoteSyncConflictResolverTest,
    PreferMineWhenMineUpdateSequenceNumberIsEqual)
{
    SimpleNoteSyncConflictResolver resolver;

    const auto guid = UidGenerator::Generate();

    qevercloud::Note theirs;
    theirs.setGuid(guid);
    theirs.setUpdateSequenceNum(42);

    qevercloud::Note mine;
    mine.setGuid(guid);
    mine.setUpdateSequenceNum(theirs.updateSequenceNum().value());

    auto future =
        resolver.resolveNoteConflict(std::move(theirs), std::move(mine));

    ASSERT_TRUE(future.isFinished());
    EXPECT_TRUE(std::holds_alternative<
                ISyncConflictResolver::ConflictResolution::UseMine>(
                    future.result()));
}

TEST(
    SimpleNoteSyncConflictResolverTest,
    PreferTheirsWhenMineUpdateSequenceNumberIsLessAndMineIsNotLocallyModified)
{
    SimpleNoteSyncConflictResolver resolver;

    const auto guid = UidGenerator::Generate();

    qevercloud::Note theirs;
    theirs.setGuid(guid);
    theirs.setUpdateSequenceNum(42);

    qevercloud::Note mine;
    mine.setGuid(guid);
    mine.setUpdateSequenceNum(theirs.updateSequenceNum().value() - 1);
    mine.setLocallyModified(false);

    auto future =
        resolver.resolveNoteConflict(std::move(theirs), std::move(mine));

    ASSERT_TRUE(future.isFinished());
    EXPECT_TRUE(std::holds_alternative<
                ISyncConflictResolver::ConflictResolution::UseTheirs>(
                    future.result()));
}

TEST(
    SimpleNoteSyncConflictResolverTest,
    MoveMineWhenMineUpdateSequenceNumberIsLessAndMineIsLocallyModified)
{
    SimpleNoteSyncConflictResolver resolver;

    const auto guid = UidGenerator::Generate();

    qevercloud::Note theirs;
    theirs.setGuid(guid);
    theirs.setUpdateSequenceNum(42);

    qevercloud::Note mine;
    mine.setGuid(guid);
    mine.setUpdateSequenceNum(theirs.updateSequenceNum().value() - 1);
    mine.setLocallyModified(true);

    qevercloud::Resource mineResource;
    mineResource.setGuid(UidGenerator::Generate());
    mineResource.setNoteLocalId(mine.localId());
    mineResource.setNoteGuid(mine.guid());
    mineResource.setUpdateSequenceNum(30);
    mineResource.setLocallyModified(false);

    mine.setResources(QList<qevercloud::Resource>{} << mineResource);

    qevercloud::Note expectedMovedMine = mine;
    expectedMovedMine.setGuid(std::nullopt);
    expectedMovedMine.setUpdateSequenceNum(std::nullopt);
    expectedMovedMine.setAttributes(qevercloud::NoteAttributes{});
    expectedMovedMine.mutableAttributes()->setConflictSourceNoteGuid(
        theirs.guid());
    expectedMovedMine.setTitle(QStringLiteral("Conflicting note"));

    for (auto & resource: *expectedMovedMine.mutableResources()) {
        resource.setGuid(std::nullopt);
        resource.setNoteGuid(std::nullopt);
        resource.setUpdateSequenceNum(std::nullopt);
        resource.setLocallyModified(true);
    }

    auto future =
        resolver.resolveNoteConflict(std::move(theirs), std::move(mine));

    ASSERT_TRUE(future.isFinished());

    using Resolution =
        ISyncConflictResolver::ConflictResolution::MoveMine<qevercloud::Note>;

    const auto resolution = future.result();
    EXPECT_TRUE(std::holds_alternative<Resolution>(resolution));
    const auto & movedMine = std::get<Resolution>(resolution);
    EXPECT_EQ(movedMine.mine, expectedMovedMine);
}

} // namespace quentier::synchronization::tests
