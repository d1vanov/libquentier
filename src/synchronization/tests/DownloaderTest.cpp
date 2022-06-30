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

#include "synchronization/Downloader.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/synchronization/types/IDownloadNotesStatus.h>
#include <quentier/utility/FileSystem.h>

#include <synchronization/tests/mocks/MockILinkedNotebooksProcessor.h>
#include <synchronization/tests/mocks/MockINotebooksProcessor.h>
#include <synchronization/tests/mocks/MockINotesProcessor.h>
#include <synchronization/tests/mocks/MockIResourcesProcessor.h>
#include <synchronization/tests/mocks/MockISavedSearchesProcessor.h>
#include <synchronization/tests/mocks/MockISyncChunksProvider.h>
#include <synchronization/tests/mocks/MockITagsProcessor.h>

#include <QTemporaryDir>

#include <gtest/gtest.h>

// clazy:excludeall=non-pod-global-static
// clazy:excludeall=returning-void-expression

namespace quentier::synchronization::tests {

using testing::StrictMock;

class DownloaderTest : public testing::Test
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
    const std::shared_ptr<mocks::MockISyncChunksProvider>
        m_mockSyncChunksProvider =
            std::make_shared<StrictMock<mocks::MockISyncChunksProvider>>();

    const std::shared_ptr<mocks::MockILinkedNotebooksProcessor>
        m_mockLinkedNotebooksProcessor = std::make_shared<
            StrictMock<mocks::MockILinkedNotebooksProcessor>>();

    const std::shared_ptr<mocks::MockINotebooksProcessor>
        m_mockNotebooksProcessor =
            std::make_shared<StrictMock<mocks::MockINotebooksProcessor>>();

    const std::shared_ptr<mocks::MockINotesProcessor> m_mockNotesProcessor =
        std::make_shared<StrictMock<mocks::MockINotesProcessor>>();

    const std::shared_ptr<mocks::MockIResourcesProcessor>
        m_mockResourcesProcessor =
            std::make_shared<StrictMock<mocks::MockIResourcesProcessor>>();

    const std::shared_ptr<mocks::MockISavedSearchesProcessor>
        m_mockSavedSearchesProcessor =
            std::make_shared<StrictMock<mocks::MockISavedSearchesProcessor>>();

    const std::shared_ptr<mocks::MockITagsProcessor> m_mockTagsProcessor =
        std::make_shared<StrictMock<mocks::MockITagsProcessor>>();

    QTemporaryDir m_temporaryDir;
};

TEST_F(DownloaderTest, Ctor)
{
    EXPECT_NO_THROW(
        const auto downloader = std::make_shared<Downloader>(
            m_mockSyncChunksProvider,
            m_mockLinkedNotebooksProcessor,
            m_mockNotebooksProcessor,
            m_mockNotesProcessor,
            m_mockResourcesProcessor,
            m_mockSavedSearchesProcessor,
            m_mockTagsProcessor,
            QDir{m_temporaryDir.path()}));
}

TEST_F(DownloaderTest, CtorNullSyncChunksProvider)
{
    EXPECT_THROW(
        const auto downloader = std::make_shared<Downloader>(
            nullptr,
            m_mockLinkedNotebooksProcessor,
            m_mockNotebooksProcessor,
            m_mockNotesProcessor,
            m_mockResourcesProcessor,
            m_mockSavedSearchesProcessor,
            m_mockTagsProcessor,
            QDir{m_temporaryDir.path()}),
        InvalidArgument);
}

TEST_F(DownloaderTest, CtorNullLinkedNotebooksProcessor)
{
    EXPECT_THROW(
        const auto downloader = std::make_shared<Downloader>(
            m_mockSyncChunksProvider,
            nullptr,
            m_mockNotebooksProcessor,
            m_mockNotesProcessor,
            m_mockResourcesProcessor,
            m_mockSavedSearchesProcessor,
            m_mockTagsProcessor,
            QDir{m_temporaryDir.path()}),
        InvalidArgument);
}

TEST_F(DownloaderTest, CtorNullNotebooksProcessor)
{
    EXPECT_THROW(
        const auto downloader = std::make_shared<Downloader>(
            m_mockSyncChunksProvider,
            m_mockLinkedNotebooksProcessor,
            nullptr,
            m_mockNotesProcessor,
            m_mockResourcesProcessor,
            m_mockSavedSearchesProcessor,
            m_mockTagsProcessor,
            QDir{m_temporaryDir.path()}),
        InvalidArgument);
}

TEST_F(DownloaderTest, CtorNullNotesProcessor)
{
    EXPECT_THROW(
        const auto downloader = std::make_shared<Downloader>(
            m_mockSyncChunksProvider,
            m_mockLinkedNotebooksProcessor,
            m_mockNotebooksProcessor,
            nullptr,
            m_mockResourcesProcessor,
            m_mockSavedSearchesProcessor,
            m_mockTagsProcessor,
            QDir{m_temporaryDir.path()}),
        InvalidArgument);
}

TEST_F(DownloaderTest, CtorNullResourcesProcessor)
{
    EXPECT_THROW(
        const auto downloader = std::make_shared<Downloader>(
            m_mockSyncChunksProvider,
            m_mockLinkedNotebooksProcessor,
            m_mockNotebooksProcessor,
            m_mockNotesProcessor,
            nullptr,
            m_mockSavedSearchesProcessor,
            m_mockTagsProcessor,
            QDir{m_temporaryDir.path()}),
        InvalidArgument);
}

TEST_F(DownloaderTest, CtorNullSavedSearchesProcessor)
{
    EXPECT_THROW(
        const auto downloader = std::make_shared<Downloader>(
            m_mockSyncChunksProvider,
            m_mockLinkedNotebooksProcessor,
            m_mockNotebooksProcessor,
            m_mockNotesProcessor,
            m_mockResourcesProcessor,
            nullptr,
            m_mockTagsProcessor,
            QDir{m_temporaryDir.path()}),
        InvalidArgument);
}

TEST_F(DownloaderTest, CtorNullTagsProcessor)
{
    EXPECT_THROW(
        const auto downloader = std::make_shared<Downloader>(
            m_mockSyncChunksProvider,
            m_mockLinkedNotebooksProcessor,
            m_mockNotebooksProcessor,
            m_mockNotesProcessor,
            m_mockResourcesProcessor,
            m_mockSavedSearchesProcessor,
            nullptr,
            QDir{m_temporaryDir.path()}),
        InvalidArgument);
}

} // namespace quentier::synchronization::tests
