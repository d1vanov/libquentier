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

#include <synchronization/processors/DurableResourcesProcessor.h>
#include <synchronization/processors/Utils.h>
#include <synchronization/sync_chunks/Utils.h>
#include <synchronization/tests/mocks/MockIResourcesProcessor.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/RuntimeError.h>
#include <quentier/threading/Future.h>
#include <quentier/utility/FileSystem.h>
#include <quentier/utility/UidGenerator.h>

#include <qevercloud/types/SyncChunk.h>
#include <qevercloud/types/builders/ResourceBuilder.h>
#include <qevercloud/types/builders/SyncChunkBuilder.h>
#include <qevercloud/utility/ToRange.h>

#include <QSettings>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <algorithm>
#include <optional>

// clazy:excludeall=non-pod-global-static
// clazy:excludeall=returning-void-expression

namespace quentier::synchronization::tests {

using testing::StrictMock;

namespace {

[[nodiscard]] QList<qevercloud::Resource> generateTestResources(
    const qint32 startUsn, const qint32 endUsn)
{
    EXPECT_GE(endUsn, startUsn);
    if (endUsn < startUsn) {
        return {};
    }

    const auto noteGuid = UidGenerator::Generate();

    QList<qevercloud::Resource> result;
    result.reserve(endUsn - startUsn + 1);
    for (qint32 i = startUsn; i <= endUsn; ++i) {
        result << qevercloud::ResourceBuilder{}
            .setGuid(UidGenerator::Generate())
            .setNoteGuid(noteGuid)
            .setUpdateSequenceNum(i)
            .build();
    }

    return result;
}

} // namespace

class DurableResourcesProcessorTest : public testing::Test
{
protected:
    void TearDown() override
    {
        QDir dir{m_temporaryDir.path()};
        const auto entries =
            dir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);

        for (const auto & entry: qAsConst(entries)) {
            if (entry.isDir()) {
                ASSERT_TRUE(removeDir(entry.absoluteFilePath()));
            }
            else {
                ASSERT_TRUE(removeFile(entry.absoluteFilePath()));
            }
        }
    }

protected:
    const std::shared_ptr<mocks::MockIResourcesProcessor>
        m_mockResourcesProcessor =
            std::make_shared<StrictMock<mocks::MockIResourcesProcessor>>();

    QTemporaryDir m_temporaryDir;
};

TEST_F(DurableResourcesProcessorTest, Ctor)
{
    EXPECT_NO_THROW(
        const auto durableResourcesProcessor =
            std::make_shared<DurableResourcesProcessor>(
                m_mockResourcesProcessor, QDir{m_temporaryDir.path()}));
}

TEST_F(DurableResourcesProcessorTest, CtorNullResourcesProcessor)
{
    EXPECT_THROW(
        const auto durableResourcesProcessor =
            std::make_shared<DurableResourcesProcessor>(
                nullptr, QDir{m_temporaryDir.path()}),
        InvalidArgument);
}

} // namespace quentier::synchronization::tests
