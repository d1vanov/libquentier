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

#include <synchronization/Downloader.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/local_storage/tests/mocks/MockILocalStorage.h>
#include <quentier/synchronization/tests/mocks/MockISyncStateStorage.h>
#include <quentier/synchronization/types/IDownloadNotesStatus.h>
#include <quentier/utility/FileSystem.h>
#include <quentier/utility/cancelers/ManualCanceler.h>

#include <synchronization/tests/mocks/MockIAccountLimitsProvider.h>
#include <synchronization/tests/mocks/MockIAuthenticationInfoProvider.h>
#include <synchronization/tests/mocks/MockILinkedNotebooksProcessor.h>
#include <synchronization/tests/mocks/MockINotebooksProcessor.h>
#include <synchronization/tests/mocks/MockINotesProcessor.h>
#include <synchronization/tests/mocks/MockIProtocolVersionChecker.h>
#include <synchronization/tests/mocks/MockIResourcesProcessor.h>
#include <synchronization/tests/mocks/MockISavedSearchesProcessor.h>
#include <synchronization/tests/mocks/MockISyncChunksProvider.h>
#include <synchronization/tests/mocks/MockISyncChunksStorage.h>
#include <synchronization/tests/mocks/MockITagsProcessor.h>
#include <synchronization/tests/mocks/MockIUserInfoProvider.h>
#include <synchronization/tests/mocks/qevercloud/services/MockINoteStore.h>

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
    const Account m_account = Account{
        QStringLiteral("Full Name"),
        Account::Type::Evernote,
        qevercloud::UserID{42},
        Account::EvernoteAccountType::Free,
        QStringLiteral("https://www.evernote.com"),
        QStringLiteral("shard id")};

    const std::shared_ptr<mocks::MockIAuthenticationInfoProvider>
        m_mockAuthenticationInfoProvider = std::make_shared<
            StrictMock<mocks::MockIAuthenticationInfoProvider>>();

    const std::shared_ptr<mocks::MockIProtocolVersionChecker>
        m_mockProtocolVersionChecker =
            std::make_shared<StrictMock<mocks::MockIProtocolVersionChecker>>();

    const std::shared_ptr<mocks::MockIUserInfoProvider> m_mockUserInfoProvider =
        std::make_shared<StrictMock<mocks::MockIUserInfoProvider>>();

    const std::shared_ptr<mocks::MockIAccountLimitsProvider>
        m_mockAccountLimitsProvider =
            std::make_shared<StrictMock<mocks::MockIAccountLimitsProvider>>();

    const std::shared_ptr<mocks::MockISyncStateStorage> m_mockSyncStateStorage =
        std::make_shared<StrictMock<mocks::MockISyncStateStorage>>();

    const std::shared_ptr<mocks::MockISyncChunksProvider>
        m_mockSyncChunksProvider =
            std::make_shared<StrictMock<mocks::MockISyncChunksProvider>>();

    const std::shared_ptr<mocks::MockISyncChunksStorage>
        m_mockSyncChunksStorage =
            std::make_shared<StrictMock<mocks::MockISyncChunksStorage>>();

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

    const qevercloud::IRequestContextPtr m_ctx =
        qevercloud::newRequestContext();

    const std::shared_ptr<mocks::qevercloud::MockINoteStore> m_mockNoteStore =
        std::make_shared<StrictMock<mocks::qevercloud::MockINoteStore>>();

    const std::shared_ptr<local_storage::tests::mocks::MockILocalStorage>
        m_mockLocalStorage = std::make_shared<
            StrictMock<local_storage::tests::mocks::MockILocalStorage>>();

    const utility::cancelers::ManualCancelerPtr m_manualCanceler =
        std::make_shared<utility::cancelers::ManualCanceler>();

    QTemporaryDir m_temporaryDir;
};

TEST_F(DownloaderTest, Ctor)
{
    EXPECT_NO_THROW(
        const auto downloader = std::make_shared<Downloader>(
            m_account, m_mockAuthenticationInfoProvider,
            m_mockProtocolVersionChecker, m_mockUserInfoProvider,
            m_mockAccountLimitsProvider, m_mockSyncStateStorage,
            m_mockSyncChunksProvider, m_mockSyncChunksStorage,
            m_mockLinkedNotebooksProcessor, m_mockNotebooksProcessor,
            m_mockNotesProcessor, m_mockResourcesProcessor,
            m_mockSavedSearchesProcessor, m_mockTagsProcessor, m_ctx,
            m_mockNoteStore, m_mockLocalStorage, m_manualCanceler,
            QDir{m_temporaryDir.path()}));
}

TEST_F(DownloaderTest, CtorEmptyAccount)
{
    EXPECT_THROW(
        const auto downloader = std::make_shared<Downloader>(
            Account{}, m_mockAuthenticationInfoProvider,
            m_mockProtocolVersionChecker, m_mockUserInfoProvider,
            m_mockAccountLimitsProvider, m_mockSyncStateStorage,
            m_mockSyncChunksProvider, m_mockSyncChunksStorage,
            m_mockLinkedNotebooksProcessor, m_mockNotebooksProcessor,
            m_mockNotesProcessor, m_mockResourcesProcessor,
            m_mockSavedSearchesProcessor, m_mockTagsProcessor, m_ctx,
            m_mockNoteStore, m_mockLocalStorage, m_manualCanceler,
            QDir{m_temporaryDir.path()}),
        InvalidArgument);
}

TEST_F(DownloaderTest, CtorNonEvernoteAccount)
{
    Account account{QStringLiteral("Full Name"), Account::Type::Local};

    EXPECT_THROW(
        const auto downloader = std::make_shared<Downloader>(
            std::move(account), m_mockAuthenticationInfoProvider,
            m_mockProtocolVersionChecker, m_mockUserInfoProvider,
            m_mockAccountLimitsProvider, m_mockSyncStateStorage,
            m_mockSyncChunksProvider, m_mockSyncChunksStorage,
            m_mockLinkedNotebooksProcessor, m_mockNotebooksProcessor,
            m_mockNotesProcessor, m_mockResourcesProcessor,
            m_mockSavedSearchesProcessor, m_mockTagsProcessor, m_ctx,
            m_mockNoteStore, m_mockLocalStorage, m_manualCanceler,
            QDir{m_temporaryDir.path()}),
        InvalidArgument);
}

TEST_F(DownloaderTest, CtorNullAuthenticationInfoProvider)
{
    EXPECT_THROW(
        const auto downloader = std::make_shared<Downloader>(
            m_account, nullptr, m_mockProtocolVersionChecker,
            m_mockUserInfoProvider, m_mockAccountLimitsProvider,
            m_mockSyncStateStorage, m_mockSyncChunksProvider,
            m_mockSyncChunksStorage, m_mockLinkedNotebooksProcessor,
            m_mockNotebooksProcessor, m_mockNotesProcessor,
            m_mockResourcesProcessor, m_mockSavedSearchesProcessor,
            m_mockTagsProcessor, m_ctx, m_mockNoteStore, m_mockLocalStorage,
            m_manualCanceler, QDir{m_temporaryDir.path()}),
        InvalidArgument);
}

TEST_F(DownloaderTest, CtorNullProtocolVersionChecker)
{
    EXPECT_THROW(
        const auto downloader = std::make_shared<Downloader>(
            m_account, m_mockAuthenticationInfoProvider, nullptr,
            m_mockUserInfoProvider, m_mockAccountLimitsProvider,
            m_mockSyncStateStorage, m_mockSyncChunksProvider,
            m_mockSyncChunksStorage, m_mockLinkedNotebooksProcessor,
            m_mockNotebooksProcessor, m_mockNotesProcessor,
            m_mockResourcesProcessor, m_mockSavedSearchesProcessor,
            m_mockTagsProcessor, m_ctx, m_mockNoteStore, m_mockLocalStorage,
            m_manualCanceler, QDir{m_temporaryDir.path()}),
        InvalidArgument);
}

TEST_F(DownloaderTest, CtorNullUserInfoProvider)
{
    EXPECT_THROW(
        const auto downloader = std::make_shared<Downloader>(
            m_account, m_mockAuthenticationInfoProvider,
            m_mockProtocolVersionChecker, nullptr, m_mockAccountLimitsProvider,
            m_mockSyncStateStorage, m_mockSyncChunksProvider,
            m_mockSyncChunksStorage, m_mockLinkedNotebooksProcessor,
            m_mockNotebooksProcessor, m_mockNotesProcessor,
            m_mockResourcesProcessor, m_mockSavedSearchesProcessor,
            m_mockTagsProcessor, m_ctx, m_mockNoteStore, m_mockLocalStorage,
            m_manualCanceler, QDir{m_temporaryDir.path()}),
        InvalidArgument);
}

TEST_F(DownloaderTest, CtorNullAccountLimitsProvider)
{
    EXPECT_THROW(
        const auto downloader = std::make_shared<Downloader>(
            m_account, m_mockAuthenticationInfoProvider,
            m_mockProtocolVersionChecker, m_mockUserInfoProvider, nullptr,
            m_mockSyncStateStorage, m_mockSyncChunksProvider,
            m_mockSyncChunksStorage, m_mockLinkedNotebooksProcessor,
            m_mockNotebooksProcessor, m_mockNotesProcessor,
            m_mockResourcesProcessor, m_mockSavedSearchesProcessor,
            m_mockTagsProcessor, m_ctx, m_mockNoteStore, m_mockLocalStorage,
            m_manualCanceler, QDir{m_temporaryDir.path()}),
        InvalidArgument);
}

TEST_F(DownloaderTest, CtorNullSyncStateStorage)
{
    EXPECT_THROW(
        const auto downloader = std::make_shared<Downloader>(
            m_account, m_mockAuthenticationInfoProvider,
            m_mockProtocolVersionChecker, m_mockUserInfoProvider,
            m_mockAccountLimitsProvider, nullptr, m_mockSyncChunksProvider,
            m_mockSyncChunksStorage, m_mockLinkedNotebooksProcessor,
            m_mockNotebooksProcessor, m_mockNotesProcessor,
            m_mockResourcesProcessor, m_mockSavedSearchesProcessor,
            m_mockTagsProcessor, m_ctx, m_mockNoteStore, m_mockLocalStorage,
            m_manualCanceler, QDir{m_temporaryDir.path()}),
        InvalidArgument);
}

TEST_F(DownloaderTest, CtorNullSyncChunksProvider)
{
    EXPECT_THROW(
        const auto downloader = std::make_shared<Downloader>(
            m_account, m_mockAuthenticationInfoProvider,
            m_mockProtocolVersionChecker, m_mockUserInfoProvider,
            m_mockAccountLimitsProvider, m_mockSyncStateStorage, nullptr,
            m_mockSyncChunksStorage, m_mockLinkedNotebooksProcessor,
            m_mockNotebooksProcessor, m_mockNotesProcessor,
            m_mockResourcesProcessor, m_mockSavedSearchesProcessor,
            m_mockTagsProcessor, m_ctx, m_mockNoteStore, m_mockLocalStorage,
            m_manualCanceler, QDir{m_temporaryDir.path()}),
        InvalidArgument);
}

TEST_F(DownloaderTest, CtorNullSyncChunksStorage)
{
    EXPECT_THROW(
        const auto downloader = std::make_shared<Downloader>(
            m_account, m_mockAuthenticationInfoProvider,
            m_mockProtocolVersionChecker, m_mockUserInfoProvider,
            m_mockAccountLimitsProvider, m_mockSyncStateStorage,
            m_mockSyncChunksProvider, nullptr, m_mockLinkedNotebooksProcessor,
            m_mockNotebooksProcessor, m_mockNotesProcessor,
            m_mockResourcesProcessor, m_mockSavedSearchesProcessor,
            m_mockTagsProcessor, m_ctx, m_mockNoteStore, m_mockLocalStorage,
            m_manualCanceler, QDir{m_temporaryDir.path()}),
        InvalidArgument);
}

TEST_F(DownloaderTest, CtorNullLinkedNotebooksProcessor)
{
    EXPECT_THROW(
        const auto downloader = std::make_shared<Downloader>(
            m_account, m_mockAuthenticationInfoProvider,
            m_mockProtocolVersionChecker, m_mockUserInfoProvider,
            m_mockAccountLimitsProvider, m_mockSyncStateStorage,
            m_mockSyncChunksProvider, m_mockSyncChunksStorage, nullptr,
            m_mockNotebooksProcessor, m_mockNotesProcessor,
            m_mockResourcesProcessor, m_mockSavedSearchesProcessor,
            m_mockTagsProcessor, m_ctx, m_mockNoteStore, m_mockLocalStorage,
            m_manualCanceler, QDir{m_temporaryDir.path()}),
        InvalidArgument);
}

TEST_F(DownloaderTest, CtorNullNotebooksProcessor)
{
    EXPECT_THROW(
        const auto downloader = std::make_shared<Downloader>(
            m_account, m_mockAuthenticationInfoProvider,
            m_mockProtocolVersionChecker, m_mockUserInfoProvider,
            m_mockAccountLimitsProvider, m_mockSyncStateStorage,
            m_mockSyncChunksProvider, m_mockSyncChunksStorage,
            m_mockLinkedNotebooksProcessor, nullptr, m_mockNotesProcessor,
            m_mockResourcesProcessor, m_mockSavedSearchesProcessor,
            m_mockTagsProcessor, m_ctx, m_mockNoteStore, m_mockLocalStorage,
            m_manualCanceler, QDir{m_temporaryDir.path()}),
        InvalidArgument);
}

TEST_F(DownloaderTest, CtorNullNotesProcessor)
{
    EXPECT_THROW(
        const auto downloader = std::make_shared<Downloader>(
            m_account, m_mockAuthenticationInfoProvider,
            m_mockProtocolVersionChecker, m_mockUserInfoProvider,
            m_mockAccountLimitsProvider, m_mockSyncStateStorage,
            m_mockSyncChunksProvider, m_mockSyncChunksStorage,
            m_mockLinkedNotebooksProcessor, m_mockNotebooksProcessor, nullptr,
            m_mockResourcesProcessor, m_mockSavedSearchesProcessor,
            m_mockTagsProcessor, m_ctx, m_mockNoteStore, m_mockLocalStorage,
            m_manualCanceler, QDir{m_temporaryDir.path()}),
        InvalidArgument);
}

TEST_F(DownloaderTest, CtorNullResourcesProcessor)
{
    EXPECT_THROW(
        const auto downloader = std::make_shared<Downloader>(
            m_account, m_mockAuthenticationInfoProvider,
            m_mockProtocolVersionChecker, m_mockUserInfoProvider,
            m_mockAccountLimitsProvider, m_mockSyncStateStorage,
            m_mockSyncChunksProvider, m_mockSyncChunksStorage,
            m_mockLinkedNotebooksProcessor, m_mockNotebooksProcessor,
            m_mockNotesProcessor, nullptr, m_mockSavedSearchesProcessor,
            m_mockTagsProcessor, m_ctx, m_mockNoteStore, m_mockLocalStorage,
            m_manualCanceler, QDir{m_temporaryDir.path()}),
        InvalidArgument);
}

TEST_F(DownloaderTest, CtorNullSavedSearchesProcessor)
{
    EXPECT_THROW(
        const auto downloader = std::make_shared<Downloader>(
            m_account, m_mockAuthenticationInfoProvider,
            m_mockProtocolVersionChecker, m_mockUserInfoProvider,
            m_mockAccountLimitsProvider, m_mockSyncStateStorage,
            m_mockSyncChunksProvider, m_mockSyncChunksStorage,
            m_mockLinkedNotebooksProcessor, m_mockNotebooksProcessor,
            m_mockNotesProcessor, m_mockResourcesProcessor, nullptr,
            m_mockTagsProcessor, m_ctx, m_mockNoteStore, m_mockLocalStorage,
            m_manualCanceler, QDir{m_temporaryDir.path()}),
        InvalidArgument);
}

TEST_F(DownloaderTest, CtorNullTagsProcessor)
{
    EXPECT_THROW(
        const auto downloader = std::make_shared<Downloader>(
            m_account, m_mockAuthenticationInfoProvider,
            m_mockProtocolVersionChecker, m_mockUserInfoProvider,
            m_mockAccountLimitsProvider, m_mockSyncStateStorage,
            m_mockSyncChunksProvider, m_mockSyncChunksStorage,
            m_mockLinkedNotebooksProcessor, m_mockNotebooksProcessor,
            m_mockNotesProcessor, m_mockResourcesProcessor,
            m_mockSavedSearchesProcessor, nullptr, m_ctx, m_mockNoteStore,
            m_mockLocalStorage, m_manualCanceler, QDir{m_temporaryDir.path()}),
        InvalidArgument);
}

TEST_F(DownloaderTest, CtorNullRequestContext)
{
    EXPECT_THROW(
        const auto downloader = std::make_shared<Downloader>(
            m_account, m_mockAuthenticationInfoProvider,
            m_mockProtocolVersionChecker, m_mockUserInfoProvider,
            m_mockAccountLimitsProvider, m_mockSyncStateStorage,
            m_mockSyncChunksProvider, m_mockSyncChunksStorage,
            m_mockLinkedNotebooksProcessor, m_mockNotebooksProcessor,
            m_mockNotesProcessor, m_mockResourcesProcessor,
            m_mockSavedSearchesProcessor, m_mockTagsProcessor, nullptr,
            m_mockNoteStore, m_mockLocalStorage, m_manualCanceler,
            QDir{m_temporaryDir.path()}),
        InvalidArgument);
}

TEST_F(DownloaderTest, CtorNullNoteStore)
{
    EXPECT_THROW(
        const auto downloader = std::make_shared<Downloader>(
            m_account, m_mockAuthenticationInfoProvider,
            m_mockProtocolVersionChecker, m_mockUserInfoProvider,
            m_mockAccountLimitsProvider, m_mockSyncStateStorage,
            m_mockSyncChunksProvider, m_mockSyncChunksStorage,
            m_mockLinkedNotebooksProcessor, m_mockNotebooksProcessor,
            m_mockNotesProcessor, m_mockResourcesProcessor,
            m_mockSavedSearchesProcessor, m_mockTagsProcessor, m_ctx, nullptr,
            m_mockLocalStorage, m_manualCanceler, QDir{m_temporaryDir.path()}),
        InvalidArgument);
}

TEST_F(DownloaderTest, CtorNullLocalStorage)
{
    EXPECT_THROW(
        const auto downloader = std::make_shared<Downloader>(
            m_account, m_mockAuthenticationInfoProvider,
            m_mockProtocolVersionChecker, m_mockUserInfoProvider,
            m_mockAccountLimitsProvider, m_mockSyncStateStorage,
            m_mockSyncChunksProvider, m_mockSyncChunksStorage,
            m_mockLinkedNotebooksProcessor, m_mockNotebooksProcessor,
            m_mockNotesProcessor, m_mockResourcesProcessor,
            m_mockSavedSearchesProcessor, m_mockTagsProcessor, m_ctx,
            m_mockNoteStore, nullptr, m_manualCanceler,
            QDir{m_temporaryDir.path()}),
        InvalidArgument);
}

TEST_F(DownloaderTest, CtorNullCanceler)
{
    EXPECT_THROW(
        const auto downloader = std::make_shared<Downloader>(
            m_account, m_mockAuthenticationInfoProvider,
            m_mockProtocolVersionChecker, m_mockUserInfoProvider,
            m_mockAccountLimitsProvider, m_mockSyncStateStorage,
            m_mockSyncChunksProvider, m_mockSyncChunksStorage,
            m_mockLinkedNotebooksProcessor, m_mockNotebooksProcessor,
            m_mockNotesProcessor, m_mockResourcesProcessor,
            m_mockSavedSearchesProcessor, m_mockTagsProcessor, m_ctx,
            m_mockNoteStore, m_mockLocalStorage, nullptr,
            QDir{m_temporaryDir.path()}),
        InvalidArgument);
}

} // namespace quentier::synchronization::tests
