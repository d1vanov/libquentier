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
#include <synchronization/processors/TagsProcessor.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/RuntimeError.h>
#include <quentier/local_storage/tests/mocks/MockILocalStorage.h>
#include <quentier/synchronization/tests/mocks/MockISyncConflictResolver.h>
#include <quentier/threading/Future.h>
#include <quentier/utility/TagSortByParentChildRelations.h>
#include <quentier/utility/UidGenerator.h>

#include <qevercloud/types/builders/SyncChunkBuilder.h>
#include <qevercloud/types/builders/TagBuilder.h>

#include <QSet>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <iterator>

// clazy:excludeall=non-pod-global-static
// clazy:excludeall=returning-void-expression

namespace quentier::synchronization::tests {

using testing::StrictMock;

class TagsProcessorTest : public testing::Test
{
protected:
    const std::shared_ptr<local_storage::tests::mocks::MockILocalStorage>
        m_mockLocalStorage = std::make_shared<
            StrictMock<local_storage::tests::mocks::MockILocalStorage>>();

    const std::shared_ptr<mocks::MockISyncConflictResolver>
        m_mockSyncConflictResolver =
            std::make_shared<StrictMock<mocks::MockISyncConflictResolver>>();

    const SyncChunksDataCountersPtr m_syncChunksDataCounters =
        std::make_shared<SyncChunksDataCounters>();
};

TEST_F(TagsProcessorTest, Ctor)
{
    EXPECT_NO_THROW(
        const auto tagsProcessor = std::make_shared<TagsProcessor>(
            m_mockLocalStorage, m_mockSyncConflictResolver,
            m_syncChunksDataCounters));
}

TEST_F(TagsProcessorTest, CtorNullLocalStorage)
{
    EXPECT_THROW(
        const auto tagsProcessor = std::make_shared<TagsProcessor>(
            nullptr, m_mockSyncConflictResolver, m_syncChunksDataCounters),
        InvalidArgument);
}

TEST_F(TagsProcessorTest, CtorNullSyncConflictResolver)
{
    EXPECT_THROW(
        const auto tagsProcessor = std::make_shared<TagsProcessor>(
            m_mockLocalStorage, nullptr, m_syncChunksDataCounters),
        InvalidArgument);
}

TEST_F(TagsProcessorTest, CtorNullSyncChunksDataCounters)
{
    EXPECT_THROW(
        const auto tagsProcessor = std::make_shared<TagsProcessor>(
            m_mockLocalStorage, m_mockSyncConflictResolver, nullptr),
        InvalidArgument);
}

TEST_F(TagsProcessorTest, ProcessSyncChunksWithoutTagsToProcess)
{
    const auto syncChunks = QList<qevercloud::SyncChunk>{}
        << qevercloud::SyncChunkBuilder{}.build();

    const auto tagsProcessor = std::make_shared<TagsProcessor>(
        m_mockLocalStorage, m_mockSyncConflictResolver,
        m_syncChunksDataCounters);

    auto future = tagsProcessor->processTags(syncChunks);
    ASSERT_TRUE(future.isFinished());
    EXPECT_NO_THROW(future.waitForFinished());

    EXPECT_EQ(m_syncChunksDataCounters->totalTags(), 0UL);
    EXPECT_EQ(m_syncChunksDataCounters->totalExpungedTags(), 0UL);
    EXPECT_EQ(m_syncChunksDataCounters->addedTags(), 0UL);
    EXPECT_EQ(m_syncChunksDataCounters->updatedTags(), 0UL);
    EXPECT_EQ(m_syncChunksDataCounters->expungedTags(), 0UL);
}

TEST_F(TagsProcessorTest, ProcessTagsWithoutConflicts)
{
    // Put tags into such an order that child tags come before parent ones,
    // to ensure that TagsProcessor would properly sort them and put parent ones
    // into the local storage first
    const auto tag4 = qevercloud::TagBuilder{}
                          .setGuid(UidGenerator::Generate())
                          .setName(QStringLiteral("Tag #4"))
                          .setUpdateSequenceNum(36)
                          .build();

    const auto tag1 = qevercloud::TagBuilder{}
                          .setGuid(UidGenerator::Generate())
                          .setName(QStringLiteral("Tag #1"))
                          .setUpdateSequenceNum(32)
                          .setParentGuid(tag4.guid())
                          .build();

    const auto tag3 = qevercloud::TagBuilder{}
                          .setGuid(UidGenerator::Generate())
                          .setName(QStringLiteral("Tag #3"))
                          .setUpdateSequenceNum(35)
                          .build();

    const auto tag2 = qevercloud::TagBuilder{}
                          .setGuid(UidGenerator::Generate())
                          .setName(QStringLiteral("Tag #2"))
                          .setUpdateSequenceNum(33)
                          .build();

    // Add some tags from a linked notebook
    const auto linkedNotebookGuid = UidGenerator::Generate();

    const auto tag6 = qevercloud::TagBuilder{}
                          .setGuid(UidGenerator::Generate())
                          .setName(QStringLiteral("Tag #6"))
                          .setUpdateSequenceNum(37)
                          .setLinkedNotebookGuid(linkedNotebookGuid)
                          .build();

    const auto tag5 = qevercloud::TagBuilder{}
                          .setGuid(UidGenerator::Generate())
                          .setName(QStringLiteral("Tag #5"))
                          .setUpdateSequenceNum(38)
                          .setLinkedNotebookGuid(linkedNotebookGuid)
                          .build();

    const auto tags = QList<qevercloud::Tag>{} << tag1 << tag2 << tag3 << tag4
                                               << tag5 << tag6;

    QList<qevercloud::Tag> tagsPutIntoLocalStorage;
    QSet<qevercloud::Guid> triedGuids;
    QSet<QString> triedNames;

    EXPECT_CALL(*m_mockLocalStorage, findTagByGuid)
        .WillRepeatedly([&](const qevercloud::Guid & guid) {
            EXPECT_FALSE(triedGuids.contains(guid));
            triedGuids.insert(guid);

            const auto it = std::find_if(
                tagsPutIntoLocalStorage.constBegin(),
                tagsPutIntoLocalStorage.constEnd(),
                [&](const qevercloud::Tag & tag) {
                    return tag.guid() && (*tag.guid() == guid);
                });
            if (it != tagsPutIntoLocalStorage.constEnd()) {
                return threading::makeReadyFuture<
                    std::optional<qevercloud::Tag>>(*it);
            }

            return threading::makeReadyFuture<std::optional<qevercloud::Tag>>(
                std::nullopt);
        });

    EXPECT_CALL(*m_mockLocalStorage, findTagByName)
        .WillRepeatedly([&](const QString & name,
                            const std::optional<QString> & linkedNotebookGuid) {
            EXPECT_FALSE(triedNames.contains(name));
            triedNames.insert(name);

            const bool isLinkedNotebookTag =
                (name == QStringLiteral("Tag #5") ||
                 name == QStringLiteral("Tag #6"));

            if (isLinkedNotebookTag) {
                EXPECT_TRUE(linkedNotebookGuid);
            }
            else {
                EXPECT_FALSE(linkedNotebookGuid);
            }

            const auto it = std::find_if(
                tagsPutIntoLocalStorage.constBegin(),
                tagsPutIntoLocalStorage.constEnd(),
                [&](const qevercloud::Tag & tag) {
                    return tag.name() && (*tag.name() == name);
                });
            if (it != tagsPutIntoLocalStorage.constEnd()) {
                return threading::makeReadyFuture<
                    std::optional<qevercloud::Tag>>(*it);
            }

            return threading::makeReadyFuture<std::optional<qevercloud::Tag>>(
                std::nullopt);
        });

    EXPECT_CALL(*m_mockLocalStorage, putTag)
        .WillRepeatedly([&](const qevercloud::Tag & tag) {
            if (Q_UNLIKELY(!tag.guid())) {
                return threading::makeExceptionalFuture<void>(
                    RuntimeError{ErrorString{"Detected tag without guid"}});
            }

            EXPECT_TRUE(triedGuids.contains(*tag.guid()));

            if (Q_UNLIKELY(!tag.name())) {
                return threading::makeExceptionalFuture<void>(
                    RuntimeError{ErrorString{"Detected tag without name"}});
            }

            EXPECT_TRUE(triedNames.contains(*tag.name()));

            if (tag.parentGuid()) {
                const auto it = std::find_if(
                    tagsPutIntoLocalStorage.constBegin(),
                    tagsPutIntoLocalStorage.constEnd(),
                    [parentGuid =
                         *tag.parentGuid()](const qevercloud::Tag & tag) {
                        return tag.guid() && (*tag.guid() == parentGuid);
                    });
                if (it == tagsPutIntoLocalStorage.constEnd()) {
                    return threading::makeExceptionalFuture<
                        void>(RuntimeError{ErrorString{
                        "Detected attempt to put child tag before parent"}});
                }
            }

            tagsPutIntoLocalStorage << tag;
            return threading::makeReadyFuture();
        });

    const auto syncChunks = QList<qevercloud::SyncChunk>{}
        << qevercloud::SyncChunkBuilder{}.setTags(tags).build();

    const auto tagsProcessor = std::make_shared<TagsProcessor>(
        m_mockLocalStorage, m_mockSyncConflictResolver,
        m_syncChunksDataCounters);

    auto future = tagsProcessor->processTags(syncChunks);
    ASSERT_TRUE(future.isFinished());
    EXPECT_NO_THROW(future.waitForFinished());

    auto sortedTags = tags;
    ErrorString error;
    ASSERT_TRUE(sortTagsByParentChildRelations(sortedTags, error))
        << error.nonLocalizedString().toStdString();

    EXPECT_EQ(tagsPutIntoLocalStorage, sortedTags);

    EXPECT_EQ(
        m_syncChunksDataCounters->totalTags(),
        static_cast<quint64>(tags.size()));

    EXPECT_EQ(m_syncChunksDataCounters->totalExpungedTags(), 0UL);

    EXPECT_EQ(
        m_syncChunksDataCounters->addedTags(),
        static_cast<quint64>(tags.size()));

    EXPECT_EQ(m_syncChunksDataCounters->updatedTags(), 0UL);
    EXPECT_EQ(m_syncChunksDataCounters->expungedTags(), 0UL);
}

TEST_F(TagsProcessorTest, ProcessExpungedTags)
{
    const auto expungedTagGuids = QList<qevercloud::Guid>{}
        << UidGenerator::Generate() << UidGenerator::Generate()
        << UidGenerator::Generate();

    const auto syncChunks = QList<qevercloud::SyncChunk>{}
        << qevercloud::SyncChunkBuilder{}
               .setExpungedTags(expungedTagGuids)
               .build();

    const auto tagsProcessor = std::make_shared<TagsProcessor>(
        m_mockLocalStorage, m_mockSyncConflictResolver,
        m_syncChunksDataCounters);

    QList<qevercloud::Guid> processedTagGuids;
    EXPECT_CALL(*m_mockLocalStorage, expungeTagByGuid)
        .WillRepeatedly([&](const qevercloud::Guid & tagGuid) {
            processedTagGuids << tagGuid;
            return threading::makeReadyFuture();
        });

    auto future = tagsProcessor->processTags(syncChunks);
    ASSERT_TRUE(future.isFinished());
    EXPECT_NO_THROW(future.waitForFinished());

    EXPECT_EQ(processedTagGuids, expungedTagGuids);

    EXPECT_EQ(m_syncChunksDataCounters->totalTags(), 0UL);

    EXPECT_EQ(
        m_syncChunksDataCounters->totalExpungedTags(),
        static_cast<quint64>(expungedTagGuids.size()));

    EXPECT_EQ(m_syncChunksDataCounters->addedTags(), 0UL);
    EXPECT_EQ(m_syncChunksDataCounters->updatedTags(), 0UL);

    EXPECT_EQ(
        m_syncChunksDataCounters->expungedTags(),
        static_cast<quint64>(expungedTagGuids.size()));
}

TEST_F(TagsProcessorTest, FilterOutExpungedTagsFromSyncChunkTags)
{
    const auto tags = QList<qevercloud::Tag>{}
        << qevercloud::TagBuilder{}
               .setGuid(UidGenerator::Generate())
               .setName(QStringLiteral("Tag #1"))
               .setUpdateSequenceNum(31)
               .build()
        << qevercloud::TagBuilder{}
               .setGuid(UidGenerator::Generate())
               .setName(QStringLiteral("Tag #2"))
               .setUpdateSequenceNum(32)
               .build()
        << qevercloud::TagBuilder{}
               .setGuid(UidGenerator::Generate())
               .setName(QStringLiteral("Tag #3"))
               .setUpdateSequenceNum(33)
               .build()
        << qevercloud::TagBuilder{}
               .setGuid(UidGenerator::Generate())
               .setName(QStringLiteral("Tag #4"))
               .setUpdateSequenceNum(34)
               .build();

    const auto expungedTagGuids = [&] {
        QList<qevercloud::Guid> guids;
        guids.reserve(tags.size());
        for (const auto & tag: qAsConst(tags)) {
            guids << tag.guid().value();
        }
        return guids;
    }();

    const auto syncChunks = QList<qevercloud::SyncChunk>{}
        << qevercloud::SyncChunkBuilder{}
               .setTags(tags)
               .setExpungedTags(expungedTagGuids)
               .build();

    const auto tagsProcessor = std::make_shared<TagsProcessor>(
        m_mockLocalStorage, m_mockSyncConflictResolver,
        m_syncChunksDataCounters);

    QList<qevercloud::Guid> processedTagGuids;
    EXPECT_CALL(*m_mockLocalStorage, expungeTagByGuid)
        .WillRepeatedly([&](const qevercloud::Guid & tagGuid) {
            processedTagGuids << tagGuid;
            return threading::makeReadyFuture();
        });

    auto future = tagsProcessor->processTags(syncChunks);
    ASSERT_TRUE(future.isFinished());
    EXPECT_NO_THROW(future.waitForFinished());

    EXPECT_EQ(processedTagGuids, expungedTagGuids);

    EXPECT_EQ(m_syncChunksDataCounters->totalTags(), 0UL);

    EXPECT_EQ(
        m_syncChunksDataCounters->totalExpungedTags(),
        static_cast<quint64>(expungedTagGuids.size()));

    EXPECT_EQ(m_syncChunksDataCounters->addedTags(), 0UL);
    EXPECT_EQ(m_syncChunksDataCounters->updatedTags(), 0UL);

    EXPECT_EQ(
        m_syncChunksDataCounters->expungedTags(),
        static_cast<quint64>(expungedTagGuids.size()));
}

class TagsProcessorTestWithConflict :
    public TagsProcessorTest,
    public testing::WithParamInterface<
        ISyncConflictResolver::TagConflictResolution>
{};

const std::array gConflictResolutions{
    ISyncConflictResolver::TagConflictResolution{
        ISyncConflictResolver::ConflictResolution::UseTheirs{}},
    ISyncConflictResolver::TagConflictResolution{
        ISyncConflictResolver::ConflictResolution::UseMine{}},
    ISyncConflictResolver::TagConflictResolution{
        ISyncConflictResolver::ConflictResolution::IgnoreMine{}},
    ISyncConflictResolver::TagConflictResolution{
        ISyncConflictResolver::ConflictResolution::MoveMine<qevercloud::Tag>{
            qevercloud::Tag{}}}};

INSTANTIATE_TEST_SUITE_P(
    TagsProcessorTestWithConflictInstance, TagsProcessorTestWithConflict,
    testing::ValuesIn(gConflictResolutions));

TEST_P(TagsProcessorTestWithConflict, HandleConflictByGuid)
{
    auto tag = qevercloud::TagBuilder{}
                   .setGuid(UidGenerator::Generate())
                   .setName(QStringLiteral("Tag #1"))
                   .setUpdateSequenceNum(1)
                   .build();

    const auto localConflict =
        qevercloud::TagBuilder{}
            .setGuid(tag.guid())
            .setName(tag.name())
            .setUpdateSequenceNum(tag.updateSequenceNum().value() - 1)
            .build();

    QList<qevercloud::Tag> tagsPutIntoLocalStorage;
    QSet<qevercloud::Guid> triedGuids;
    QSet<QString> triedNames;

    EXPECT_CALL(*m_mockLocalStorage, findTagByGuid)
        .WillRepeatedly([&](const qevercloud::Guid & guid) {
            EXPECT_FALSE(triedGuids.contains(guid));
            triedGuids.insert(guid);

            const auto it = std::find_if(
                tagsPutIntoLocalStorage.constBegin(),
                tagsPutIntoLocalStorage.constEnd(),
                [&](const qevercloud::Tag & tag) {
                    return tag.guid() && (*tag.guid() == guid);
                });
            if (it != tagsPutIntoLocalStorage.constEnd()) {
                return threading::makeReadyFuture<
                    std::optional<qevercloud::Tag>>(*it);
            }

            if (guid == tag.guid()) {
                return threading::makeReadyFuture<
                    std::optional<qevercloud::Tag>>(localConflict);
            }

            return threading::makeReadyFuture<std::optional<qevercloud::Tag>>(
                std::nullopt);
        });

    auto resolution = GetParam();
    std::optional<qevercloud::Tag> movedLocalConflict;
    if (std::holds_alternative<ISyncConflictResolver::ConflictResolution::
                                   MoveMine<qevercloud::Tag>>(resolution))
    {
        movedLocalConflict =
            qevercloud::TagBuilder{}
                .setName(
                    localConflict.name().value() + QStringLiteral("_moved"))
                .build();

        resolution = ISyncConflictResolver::TagConflictResolution{
            ISyncConflictResolver::ConflictResolution::MoveMine<
                qevercloud::Tag>{*movedLocalConflict}};
    }

    EXPECT_CALL(*m_mockSyncConflictResolver, resolveTagConflict)
        .WillOnce([&, resolution](
                      const qevercloud::Tag & theirs,
                      const qevercloud::Tag & mine) mutable {
            EXPECT_EQ(theirs, tag);
            EXPECT_EQ(mine, localConflict);
            return threading::makeReadyFuture<
                ISyncConflictResolver::TagConflictResolution>(
                std::move(resolution));
        });

    EXPECT_CALL(*m_mockLocalStorage, findTagByName)
        .WillRepeatedly([&](const QString & name,
                            const std::optional<QString> & linkedNotebookGuid) {
            EXPECT_FALSE(triedNames.contains(name));
            triedNames.insert(name);

            EXPECT_FALSE(linkedNotebookGuid);

            const auto it = std::find_if(
                tagsPutIntoLocalStorage.constBegin(),
                tagsPutIntoLocalStorage.constEnd(),
                [&](const qevercloud::Tag & tag) {
                    return tag.name() && (*tag.name() == name);
                });
            if (it != tagsPutIntoLocalStorage.constEnd()) {
                return threading::makeReadyFuture<
                    std::optional<qevercloud::Tag>>(*it);
            }

            return threading::makeReadyFuture<std::optional<qevercloud::Tag>>(
                std::nullopt);
        });

    EXPECT_CALL(*m_mockLocalStorage, putTag)
        .WillRepeatedly(
            [&, conflictGuid = tag.guid()](const qevercloud::Tag & tag) {
                if (Q_UNLIKELY(!tag.guid())) {
                    if (std::holds_alternative<
                            ISyncConflictResolver::ConflictResolution::MoveMine<
                                qevercloud::Tag>>(resolution))
                    {
                        tagsPutIntoLocalStorage << tag;
                        return threading::makeReadyFuture();
                    }

                    return threading::makeExceptionalFuture<void>(
                        RuntimeError{ErrorString{"Detected tag without guid"}});
                }

                EXPECT_TRUE(
                    triedGuids.contains(*tag.guid()) ||
                    (movedLocalConflict && movedLocalConflict == tag));

                if (Q_UNLIKELY(!tag.name())) {
                    return threading::makeExceptionalFuture<void>(
                        RuntimeError{ErrorString{"Detected tag without name"}});
                }

                EXPECT_TRUE(
                    triedNames.contains(*tag.name()) ||
                    tag.guid() == conflictGuid ||
                    (movedLocalConflict && movedLocalConflict == tag));

                tagsPutIntoLocalStorage << tag;
                return threading::makeReadyFuture();
            });

    if (std::holds_alternative<
            ISyncConflictResolver::ConflictResolution::UseTheirs>(resolution))
    {
        tag.setLocalId(localConflict.localId());
    }

    auto tags = QList<qevercloud::Tag>{}
        << tag
        << qevercloud::TagBuilder{}
               .setGuid(UidGenerator::Generate())
               .setName(QStringLiteral("Tag #2"))
               .setUpdateSequenceNum(35)
               .build()
        << qevercloud::TagBuilder{}
               .setGuid(UidGenerator::Generate())
               .setName(QStringLiteral("Tag #3"))
               .setUpdateSequenceNum(36)
               .build()
        << qevercloud::TagBuilder{}
               .setGuid(UidGenerator::Generate())
               .setName(QStringLiteral("Tag #4"))
               .setUpdateSequenceNum(54)
               .build();

    const auto originalTagsSize = tags.size();

    const auto syncChunks = QList<qevercloud::SyncChunk>{}
        << qevercloud::SyncChunkBuilder{}.setTags(tags).build();

    const auto tagsProcessor = std::make_shared<TagsProcessor>(
        m_mockLocalStorage, m_mockSyncConflictResolver,
        m_syncChunksDataCounters);

    auto future = tagsProcessor->processTags(syncChunks);
    ASSERT_TRUE(future.isFinished());
    EXPECT_NO_THROW(future.waitForFinished());

    if (std::holds_alternative<
            ISyncConflictResolver::ConflictResolution::UseMine>(resolution))
    {
        tags.removeAt(0);
    }

    auto sortedTags = tags;
    ErrorString error;
    ASSERT_TRUE(sortTagsByParentChildRelations(sortedTags, error))
        << error.nonLocalizedString().toStdString();

    if (std::holds_alternative<ISyncConflictResolver::ConflictResolution::
                                   MoveMine<qevercloud::Tag>>(resolution))
    {
        ASSERT_TRUE(movedLocalConflict);
        sortedTags.insert(std::prev(sortedTags.end()), *movedLocalConflict);
    }

    EXPECT_EQ(tagsPutIntoLocalStorage, sortedTags);

    EXPECT_EQ(
        m_syncChunksDataCounters->totalTags(),
        static_cast<quint64>(originalTagsSize));

    EXPECT_EQ(m_syncChunksDataCounters->totalExpungedTags(), 0UL);

    if (std::holds_alternative<
            ISyncConflictResolver::ConflictResolution::UseTheirs>(resolution) ||
        std::holds_alternative<
            ISyncConflictResolver::ConflictResolution::IgnoreMine>(
            resolution) ||
        std::holds_alternative<
            ISyncConflictResolver::ConflictResolution::UseMine>(resolution))
    {
        EXPECT_EQ(
            m_syncChunksDataCounters->addedTags(),
            static_cast<quint64>(originalTagsSize - 1));

        if (std::holds_alternative<
                ISyncConflictResolver::ConflictResolution::UseMine>(resolution))
        {
            EXPECT_EQ(m_syncChunksDataCounters->updatedTags(), 0UL);
        }
        else {
            EXPECT_EQ(m_syncChunksDataCounters->updatedTags(), 1UL);
        }
    }
    else {
        EXPECT_EQ(
            m_syncChunksDataCounters->addedTags(),
            static_cast<quint64>(originalTagsSize));

        EXPECT_EQ(m_syncChunksDataCounters->updatedTags(), 0UL);
    }
}

TEST_P(TagsProcessorTestWithConflict, HandleConflictByName)
{
    const auto tag = qevercloud::TagBuilder{}
                         .setGuid(UidGenerator::Generate())
                         .setName(QStringLiteral("Tag #1"))
                         .setUpdateSequenceNum(1)
                         .build();

    const auto localConflict =
        qevercloud::TagBuilder{}.setName(tag.name()).build();

    QList<qevercloud::Tag> tagsPutIntoLocalStorage;
    QSet<qevercloud::Guid> triedGuids;
    QSet<QString> triedNames;

    EXPECT_CALL(*m_mockLocalStorage, findTagByGuid)
        .WillRepeatedly([&](const qevercloud::Guid & guid) {
            EXPECT_FALSE(triedGuids.contains(guid));
            triedGuids.insert(guid);

            const auto it = std::find_if(
                tagsPutIntoLocalStorage.constBegin(),
                tagsPutIntoLocalStorage.constEnd(),
                [&](const qevercloud::Tag & tag) {
                    return tag.guid() && (*tag.guid() == guid);
                });
            if (it != tagsPutIntoLocalStorage.constEnd()) {
                return threading::makeReadyFuture<
                    std::optional<qevercloud::Tag>>(*it);
            }

            return threading::makeReadyFuture<std::optional<qevercloud::Tag>>(
                std::nullopt);
        });

    EXPECT_CALL(*m_mockLocalStorage, findTagByName)
        .WillRepeatedly([&, conflictName = tag.name()](
                            const QString & name,
                            const std::optional<QString> & linkedNotebookGuid) {
            EXPECT_FALSE(triedNames.contains(name));
            triedNames.insert(name);

            EXPECT_FALSE(linkedNotebookGuid);

            const auto it = std::find_if(
                tagsPutIntoLocalStorage.constBegin(),
                tagsPutIntoLocalStorage.constEnd(),
                [&](const qevercloud::Tag & tag) {
                    return tag.name() && (*tag.name() == name);
                });
            if (it != tagsPutIntoLocalStorage.constEnd()) {
                return threading::makeReadyFuture<
                    std::optional<qevercloud::Tag>>(*it);
            }

            if (name == conflictName) {
                return threading::makeReadyFuture<
                    std::optional<qevercloud::Tag>>(localConflict);
            }

            return threading::makeReadyFuture<std::optional<qevercloud::Tag>>(
                std::nullopt);
        });

    auto resolution = GetParam();
    std::optional<qevercloud::Tag> movedLocalConflict;
    if (std::holds_alternative<ISyncConflictResolver::ConflictResolution::
                                   MoveMine<qevercloud::Tag>>(resolution))
    {
        movedLocalConflict =
            qevercloud::TagBuilder{}
                .setName(
                    localConflict.name().value() + QStringLiteral("_moved"))
                .build();

        resolution = ISyncConflictResolver::TagConflictResolution{
            ISyncConflictResolver::ConflictResolution::MoveMine<
                qevercloud::Tag>{*movedLocalConflict}};
    }

    EXPECT_CALL(*m_mockSyncConflictResolver, resolveTagConflict)
        .WillOnce([&, resolution](
                      const qevercloud::Tag & theirs,
                      const qevercloud::Tag & mine) mutable {
            EXPECT_EQ(theirs, tag);
            EXPECT_EQ(mine, localConflict);
            return threading::makeReadyFuture<
                ISyncConflictResolver::TagConflictResolution>(
                std::move(resolution));
        });

    EXPECT_CALL(*m_mockLocalStorage, putTag)
        .WillRepeatedly(
            [&, conflictGuid = tag.guid()](const qevercloud::Tag & tag) {
                if (Q_UNLIKELY(!tag.guid())) {
                    if (std::holds_alternative<
                            ISyncConflictResolver::ConflictResolution::MoveMine<
                                qevercloud::Tag>>(resolution))
                    {
                        tagsPutIntoLocalStorage << tag;
                        return threading::makeReadyFuture();
                    }

                    return threading::makeExceptionalFuture<void>(
                        RuntimeError{ErrorString{"Detected tag without guid"}});
                }

                EXPECT_TRUE(
                    triedGuids.contains(*tag.guid()) ||
                    (movedLocalConflict && movedLocalConflict == tag));

                if (Q_UNLIKELY(!tag.name())) {
                    return threading::makeExceptionalFuture<void>(
                        RuntimeError{ErrorString{"Detected tag without name"}});
                }

                EXPECT_TRUE(
                    triedNames.contains(*tag.name()) ||
                    tag.guid() == conflictGuid ||
                    (movedLocalConflict && movedLocalConflict == tag));

                tagsPutIntoLocalStorage << tag;
                return threading::makeReadyFuture();
            });

    auto tags = QList<qevercloud::Tag>{}
        << tag
        << qevercloud::TagBuilder{}
               .setGuid(UidGenerator::Generate())
               .setName(QStringLiteral("Tag #2"))
               .setUpdateSequenceNum(35)
               .build()
        << qevercloud::TagBuilder{}
               .setGuid(UidGenerator::Generate())
               .setName(QStringLiteral("Tag #3"))
               .setUpdateSequenceNum(36)
               .build()
        << qevercloud::TagBuilder{}
               .setGuid(UidGenerator::Generate())
               .setName(QStringLiteral("Tag #4"))
               .setUpdateSequenceNum(54)
               .build();

    const auto originalTagsSize = tags.size();

    const auto syncChunks = QList<qevercloud::SyncChunk>{}
        << qevercloud::SyncChunkBuilder{}.setTags(tags).build();

    const auto tagsProcessor = std::make_shared<TagsProcessor>(
        m_mockLocalStorage, m_mockSyncConflictResolver,
        m_syncChunksDataCounters);

    auto future = tagsProcessor->processTags(syncChunks);
    ASSERT_TRUE(future.isFinished());
    EXPECT_NO_THROW(future.waitForFinished());

    if (std::holds_alternative<
            ISyncConflictResolver::ConflictResolution::UseMine>(resolution))
    {
        tags.removeAt(0);
    }

    auto sortedTags = tags;
    ErrorString error;
    ASSERT_TRUE(sortTagsByParentChildRelations(sortedTags, error))
        << error.nonLocalizedString().toStdString();

    if (std::holds_alternative<ISyncConflictResolver::ConflictResolution::
                                   MoveMine<qevercloud::Tag>>(resolution))
    {
        ASSERT_TRUE(movedLocalConflict);
        sortedTags.insert(std::prev(sortedTags.end()), *movedLocalConflict);
    }

    EXPECT_EQ(tagsPutIntoLocalStorage, sortedTags);

    EXPECT_EQ(
        m_syncChunksDataCounters->totalTags(),
        static_cast<quint64>(originalTagsSize));

    EXPECT_EQ(m_syncChunksDataCounters->totalExpungedTags(), 0UL);

    if (std::holds_alternative<
            ISyncConflictResolver::ConflictResolution::UseTheirs>(resolution) ||
        std::holds_alternative<
            ISyncConflictResolver::ConflictResolution::IgnoreMine>(
            resolution) ||
        std::holds_alternative<
            ISyncConflictResolver::ConflictResolution::UseMine>(resolution))
    {
        EXPECT_EQ(
            m_syncChunksDataCounters->addedTags(),
            static_cast<quint64>(originalTagsSize - 1));

        if (std::holds_alternative<
                ISyncConflictResolver::ConflictResolution::UseMine>(resolution))
        {
            EXPECT_EQ(m_syncChunksDataCounters->updatedTags(), 0UL);
        }
        else {
            EXPECT_EQ(m_syncChunksDataCounters->updatedTags(), 1UL);
        }
    }
    else {
        EXPECT_EQ(
            m_syncChunksDataCounters->addedTags(),
            static_cast<quint64>(originalTagsSize));

        EXPECT_EQ(m_syncChunksDataCounters->updatedTags(), 0UL);
    }
}

} // namespace quentier::synchronization::tests
