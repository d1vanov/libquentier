/*
 * Copyright 2023-2024 Dmitry Ivanov
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

#include <synchronization/LinkedNotebookTagsCleaner.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/local_storage/tests/mocks/MockILocalStorage.h>
#include <quentier/threading/Future.h>
#include <quentier/utility/UidGenerator.h>

#include <qevercloud/types/builders/TagBuilder.h>

#include <QList>

#include <gtest/gtest.h>

#include <algorithm>

// clazy:excludeall=returning-void-expression

namespace quentier::synchronization::tests {

using testing::Return;
using testing::StrictMock;

class LinkedNotebookTagsCleanerTest : public testing::Test
{
protected:
    const std::shared_ptr<local_storage::tests::mocks::MockILocalStorage>
        m_mockLocalStorage = std::make_shared<
            StrictMock<local_storage::tests::mocks::MockILocalStorage>>();
};

TEST_F(LinkedNotebookTagsCleanerTest, Ctor)
{
    EXPECT_NO_THROW(
        const auto linkedNotebookTagsCleaner =
            std::make_shared<LinkedNotebookTagsCleaner>(m_mockLocalStorage));
}

TEST_F(LinkedNotebookTagsCleanerTest, CtorNullLocalStorage)
{
    EXPECT_THROW(
        const auto linkedNotebookTagsCleaner =
            std::make_shared<LinkedNotebookTagsCleaner>(nullptr),
        InvalidArgument);
}

TEST_F(LinkedNotebookTagsCleanerTest, ClearTags)
{
    const QList<qevercloud::Tag> tags = [] {
        QList<qevercloud::Tag> result;

        const int tagCount = 5;
        result.reserve(tagCount);

        for (int i = 0; i < tagCount; ++i) {
            result << qevercloud::TagBuilder{}
                          .setLocalId(UidGenerator::Generate())
                          .setGuid(UidGenerator::Generate())
                          .setName(QString::fromUtf8("Tag #%1").arg(i + 1))
                          .build();
        }

        return result;
    }();

    local_storage::ILocalStorage::ListTagsOptions options;
    options.m_affiliation =
        local_storage::ILocalStorage::Affiliation::AnyLinkedNotebook;
    options.m_tagNotesRelation =
        local_storage::ILocalStorage::TagNotesRelation::WithoutNotes;

    EXPECT_CALL(*m_mockLocalStorage, listTags(options))
        .WillOnce(Return(threading::makeReadyFuture(tags)));

    EXPECT_CALL(*m_mockLocalStorage, expungeTagByLocalId)
        .Times(static_cast<int>(tags.size()))
        .WillRepeatedly([&tags](const QString & localId) {
            const auto it = std::find_if(
                tags.constBegin(), tags.constEnd(),
                [&localId](const qevercloud::Tag & tag) {
                    return tag.localId() == localId;
                });
            EXPECT_NE(it, tags.constEnd());
            return threading::makeReadyFuture();
        });

    const auto linkedNotebookTagsCleaner =
        std::make_shared<LinkedNotebookTagsCleaner>(m_mockLocalStorage);

    auto future = linkedNotebookTagsCleaner->clearStaleLinkedNotebookTags();
    ASSERT_TRUE(future.isFinished());
    EXPECT_NO_THROW(future.waitForFinished());
}

} // namespace quentier::synchronization::tests
