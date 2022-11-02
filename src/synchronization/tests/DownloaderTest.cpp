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
#include <quentier/threading/Future.h>
#include <quentier/utility/FileSystem.h>
#include <quentier/utility/UidGenerator.h>
#include <quentier/utility/cancelers/ManualCanceler.h>

#include <synchronization/tests/mocks/MockIAuthenticationInfoProvider.h>
#include <synchronization/tests/mocks/MockIDownloader.h>
#include <synchronization/tests/mocks/MockIDurableNotesProcessor.h>
#include <synchronization/tests/mocks/MockIDurableResourcesProcessor.h>
#include <synchronization/tests/mocks/MockIFullSyncStaleDataExpunger.h>
#include <synchronization/tests/mocks/MockILinkedNotebooksProcessor.h>
#include <synchronization/tests/mocks/MockINotebooksProcessor.h>
#include <synchronization/tests/mocks/MockIProtocolVersionChecker.h>
#include <synchronization/tests/mocks/MockISavedSearchesProcessor.h>
#include <synchronization/tests/mocks/MockISyncChunksProvider.h>
#include <synchronization/tests/mocks/MockISyncChunksStorage.h>
#include <synchronization/tests/mocks/MockITagsProcessor.h>
#include <synchronization/tests/mocks/qevercloud/services/MockINoteStore.h>

#include <synchronization/types/AuthenticationInfo.h>
#include <synchronization/types/DownloadNotesStatus.h>
#include <synchronization/types/DownloadResourcesStatus.h>
#include <synchronization/types/SyncState.h>

#include <qevercloud/types/builders/DataBuilder.h>
#include <qevercloud/types/builders/LinkedNotebookBuilder.h>
#include <qevercloud/types/builders/NoteBuilder.h>
#include <qevercloud/types/builders/NotebookBuilder.h>
#include <qevercloud/types/builders/ResourceBuilder.h>
#include <qevercloud/types/builders/SavedSearchBuilder.h>
#include <qevercloud/types/builders/SyncChunkBuilder.h>
#include <qevercloud/types/builders/TagBuilder.h>
#include <qevercloud/types/builders/UserBuilder.h>

#include <QCryptographicHash>
#include <QDateTime>
#include <QFlags>
#include <QList>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <array>

// clazy:excludeall=non-pod-global-static
// clazy:excludeall=returning-void-expression

namespace quentier::synchronization::tests {

using testing::_;
using testing::Return;
using testing::StrictMock;

namespace {

enum class SyncChunksFlag
{
    WithLinkedNotebooks = 1 << 0,
    WithNotes = 1 << 1,
    WithNotebooks = 1 << 2,
    WithResources = 1 << 3,
    WithSavedSearches = 1 << 4,
    WithTags = 1 << 5,
};

Q_DECLARE_FLAGS(SyncChunksFlags, SyncChunksFlag);

[[nodiscard]] QList<qevercloud::SyncChunk> generateSynChunks(
    const SyncChunksFlags flags, const qint32 afterUsn = 0, // NOLINT
    const qint32 syncChunksCount = 3,
    const qint32 itemCountPerSyncChunk = 3) // NOLINT
{
    Q_ASSERT(syncChunksCount > 0);
    Q_ASSERT(itemCountPerSyncChunk > 0);
    Q_ASSERT(afterUsn >= 0);

    QList<qevercloud::SyncChunk> result;
    result.reserve(syncChunksCount);

    qint32 usn = afterUsn + 1;

    for (qint32 i = 0; i < syncChunksCount; ++i) {
        qevercloud::SyncChunkBuilder builder;

        if (flags.testFlag(SyncChunksFlag::WithLinkedNotebooks)) {
            QList<qevercloud::LinkedNotebook> linkedNotebooks;
            linkedNotebooks.reserve(itemCountPerSyncChunk);
            for (qint32 j = 0; j < itemCountPerSyncChunk; ++j) {
                linkedNotebooks
                    << qevercloud::LinkedNotebookBuilder{}
                           .setGuid(UidGenerator::Generate())
                           .setUpdateSequenceNum(usn++)
                           .setUsername(QString::fromUtf8("Linked notebook #%1")
                                            .arg(j + 1))
                           .build();
            }
            builder.setLinkedNotebooks(std::move(linkedNotebooks));
        }

        if (flags.testFlag(SyncChunksFlag::WithNotes)) {
            QList<qevercloud::Note> notes;
            notes.reserve(itemCountPerSyncChunk);
            for (qint32 j = 0; j < itemCountPerSyncChunk; ++j) {
                notes << qevercloud::NoteBuilder{}
                             .setGuid(UidGenerator::Generate())
                             .setUpdateSequenceNum(usn++)
                             .setTitle(QString::fromUtf8("Note #%1").arg(j + 1))
                             .setNotebookGuid(UidGenerator::Generate())
                             .build();
            }
            builder.setNotes(std::move(notes));
        }

        if (flags.testFlag(SyncChunksFlag::WithNotebooks)) {
            QList<qevercloud::Notebook> notebooks;
            notebooks.reserve(itemCountPerSyncChunk);
            for (qint32 j = 0; j < itemCountPerSyncChunk; ++j) {
                notebooks << qevercloud::NotebookBuilder{}
                                 .setGuid(UidGenerator::Generate())
                                 .setUpdateSequenceNum(usn++)
                                 .setName(QString::fromUtf8("Notebook #%1")
                                              .arg(j + 1))
                                 .build();
            }
            builder.setNotebooks(std::move(notebooks));
        }

        if (flags.testFlag(SyncChunksFlag::WithResources)) {
            QList<qevercloud::Resource> resources;
            resources.reserve(itemCountPerSyncChunk);
            for (qint32 j = 0; j < itemCountPerSyncChunk; ++j) {
                QByteArray dataBody =
                    QString::fromUtf8("Resource #%1").arg(j + 1).toUtf8();
                const int dataBodySize = dataBody.size();
                QByteArray dataBodyHash =
                    QCryptographicHash::hash(dataBody, QCryptographicHash::Md5);

                resources << qevercloud::ResourceBuilder{}
                                 .setGuid(UidGenerator::Generate())
                                 .setUpdateSequenceNum(usn++)
                                 .setNoteGuid(UidGenerator::Generate())
                                 .setData(
                                     qevercloud::DataBuilder{}
                                         .setBody(std::move(dataBody))
                                         .setSize(dataBodySize)
                                         .setBodyHash(std::move(dataBodyHash))
                                         .build())
                                 .build();
            }
            builder.setResources(std::move(resources));
        }

        if (flags.testFlag(SyncChunksFlag::WithSavedSearches)) {
            QList<qevercloud::SavedSearch> savedSearches;
            savedSearches.reserve(itemCountPerSyncChunk);
            for (qint32 j = 0; j < itemCountPerSyncChunk; ++j) {
                savedSearches
                    << qevercloud::SavedSearchBuilder{}
                           .setGuid(UidGenerator::Generate())
                           .setUpdateSequenceNum(usn++)
                           .setName(
                               QString::fromUtf8("Saved search #%1").arg(j + 1))
                           .build();
            }
            builder.setSearches(std::move(savedSearches));
        }

        if (flags.testFlag(SyncChunksFlag::WithTags)) {
            QList<qevercloud::Tag> tags;
            tags.reserve(itemCountPerSyncChunk);
            for (qint32 j = 0; j < itemCountPerSyncChunk; ++j) {
                tags << qevercloud::TagBuilder{}
                            .setGuid(UidGenerator::Generate())
                            .setUpdateSequenceNum(usn++)
                            .setName(QString::fromUtf8("Tag #%1").arg(j + 1))
                            .build();
            }
            builder.setTags(std::move(tags));
        }

        result << builder.build();
    }

    return result;
}

} // namespace

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

    const std::shared_ptr<mocks::MockIDurableNotesProcessor>
        m_mockNotesProcessor =
            std::make_shared<StrictMock<mocks::MockIDurableNotesProcessor>>();

    const std::shared_ptr<mocks::MockIDurableResourcesProcessor>
        m_mockResourcesProcessor = std::make_shared<
            StrictMock<mocks::MockIDurableResourcesProcessor>>();

    const std::shared_ptr<mocks::MockISavedSearchesProcessor>
        m_mockSavedSearchesProcessor =
            std::make_shared<StrictMock<mocks::MockISavedSearchesProcessor>>();

    const std::shared_ptr<mocks::MockITagsProcessor> m_mockTagsProcessor =
        std::make_shared<StrictMock<mocks::MockITagsProcessor>>();

    const std::shared_ptr<mocks::MockIFullSyncStaleDataExpunger>
        m_mockFullSyncStaleDataExpunger = std::make_shared<
            StrictMock<mocks::MockIFullSyncStaleDataExpunger>>();

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
            m_mockProtocolVersionChecker, m_mockSyncStateStorage,
            m_mockSyncChunksProvider, m_mockSyncChunksStorage,
            m_mockLinkedNotebooksProcessor, m_mockNotebooksProcessor,
            m_mockNotesProcessor, m_mockResourcesProcessor,
            m_mockSavedSearchesProcessor, m_mockTagsProcessor,
            m_mockFullSyncStaleDataExpunger, m_ctx, m_mockNoteStore,
            m_mockLocalStorage, QDir{m_temporaryDir.path()}));
}

TEST_F(DownloaderTest, CtorEmptyAccount)
{
    EXPECT_THROW(
        const auto downloader = std::make_shared<Downloader>(
            Account{}, m_mockAuthenticationInfoProvider,
            m_mockProtocolVersionChecker, m_mockSyncStateStorage,
            m_mockSyncChunksProvider, m_mockSyncChunksStorage,
            m_mockLinkedNotebooksProcessor, m_mockNotebooksProcessor,
            m_mockNotesProcessor, m_mockResourcesProcessor,
            m_mockSavedSearchesProcessor, m_mockTagsProcessor,
            m_mockFullSyncStaleDataExpunger, m_ctx, m_mockNoteStore,
            m_mockLocalStorage, QDir{m_temporaryDir.path()}),
        InvalidArgument);
}

TEST_F(DownloaderTest, CtorNonEvernoteAccount)
{
    Account account{QStringLiteral("Full Name"), Account::Type::Local};

    EXPECT_THROW(
        const auto downloader = std::make_shared<Downloader>(
            std::move(account), m_mockAuthenticationInfoProvider,
            m_mockProtocolVersionChecker, m_mockSyncStateStorage,
            m_mockSyncChunksProvider, m_mockSyncChunksStorage,
            m_mockLinkedNotebooksProcessor, m_mockNotebooksProcessor,
            m_mockNotesProcessor, m_mockResourcesProcessor,
            m_mockSavedSearchesProcessor, m_mockTagsProcessor,
            m_mockFullSyncStaleDataExpunger, m_ctx, m_mockNoteStore,
            m_mockLocalStorage, QDir{m_temporaryDir.path()}),
        InvalidArgument);
}

TEST_F(DownloaderTest, CtorNullAuthenticationInfoProvider)
{
    EXPECT_THROW(
        const auto downloader = std::make_shared<Downloader>(
            m_account, nullptr, m_mockProtocolVersionChecker,
            m_mockSyncStateStorage, m_mockSyncChunksProvider,
            m_mockSyncChunksStorage, m_mockLinkedNotebooksProcessor,
            m_mockNotebooksProcessor, m_mockNotesProcessor,
            m_mockResourcesProcessor, m_mockSavedSearchesProcessor,
            m_mockTagsProcessor, m_mockFullSyncStaleDataExpunger, m_ctx,
            m_mockNoteStore, m_mockLocalStorage, QDir{m_temporaryDir.path()}),
        InvalidArgument);
}

TEST_F(DownloaderTest, CtorNullProtocolVersionChecker)
{
    EXPECT_THROW(
        const auto downloader = std::make_shared<Downloader>(
            m_account, m_mockAuthenticationInfoProvider, nullptr,
            m_mockSyncStateStorage, m_mockSyncChunksProvider,
            m_mockSyncChunksStorage, m_mockLinkedNotebooksProcessor,
            m_mockNotebooksProcessor, m_mockNotesProcessor,
            m_mockResourcesProcessor, m_mockSavedSearchesProcessor,
            m_mockTagsProcessor, m_mockFullSyncStaleDataExpunger, m_ctx,
            m_mockNoteStore, m_mockLocalStorage, QDir{m_temporaryDir.path()}),
        InvalidArgument);
}

TEST_F(DownloaderTest, CtorNullSyncStateStorage)
{
    EXPECT_THROW(
        const auto downloader = std::make_shared<Downloader>(
            m_account, m_mockAuthenticationInfoProvider,
            m_mockProtocolVersionChecker, nullptr, m_mockSyncChunksProvider,
            m_mockSyncChunksStorage, m_mockLinkedNotebooksProcessor,
            m_mockNotebooksProcessor, m_mockNotesProcessor,
            m_mockResourcesProcessor, m_mockSavedSearchesProcessor,
            m_mockTagsProcessor, m_mockFullSyncStaleDataExpunger, m_ctx,
            m_mockNoteStore, m_mockLocalStorage, QDir{m_temporaryDir.path()}),
        InvalidArgument);
}

TEST_F(DownloaderTest, CtorNullSyncChunksProvider)
{
    EXPECT_THROW(
        const auto downloader = std::make_shared<Downloader>(
            m_account, m_mockAuthenticationInfoProvider,
            m_mockProtocolVersionChecker, m_mockSyncStateStorage, nullptr,
            m_mockSyncChunksStorage, m_mockLinkedNotebooksProcessor,
            m_mockNotebooksProcessor, m_mockNotesProcessor,
            m_mockResourcesProcessor, m_mockSavedSearchesProcessor,
            m_mockTagsProcessor, m_mockFullSyncStaleDataExpunger, m_ctx,
            m_mockNoteStore, m_mockLocalStorage, QDir{m_temporaryDir.path()}),
        InvalidArgument);
}

TEST_F(DownloaderTest, CtorNullSyncChunksStorage)
{
    EXPECT_THROW(
        const auto downloader = std::make_shared<Downloader>(
            m_account, m_mockAuthenticationInfoProvider,
            m_mockProtocolVersionChecker, m_mockSyncStateStorage,
            m_mockSyncChunksProvider, nullptr, m_mockLinkedNotebooksProcessor,
            m_mockNotebooksProcessor, m_mockNotesProcessor,
            m_mockResourcesProcessor, m_mockSavedSearchesProcessor,
            m_mockTagsProcessor, m_mockFullSyncStaleDataExpunger, m_ctx,
            m_mockNoteStore, m_mockLocalStorage, QDir{m_temporaryDir.path()}),
        InvalidArgument);
}

TEST_F(DownloaderTest, CtorNullLinkedNotebooksProcessor)
{
    EXPECT_THROW(
        const auto downloader = std::make_shared<Downloader>(
            m_account, m_mockAuthenticationInfoProvider,
            m_mockProtocolVersionChecker, m_mockSyncStateStorage,
            m_mockSyncChunksProvider, m_mockSyncChunksStorage, nullptr,
            m_mockNotebooksProcessor, m_mockNotesProcessor,
            m_mockResourcesProcessor, m_mockSavedSearchesProcessor,
            m_mockTagsProcessor, m_mockFullSyncStaleDataExpunger, m_ctx,
            m_mockNoteStore, m_mockLocalStorage, QDir{m_temporaryDir.path()}),
        InvalidArgument);
}

TEST_F(DownloaderTest, CtorNullNotebooksProcessor)
{
    EXPECT_THROW(
        const auto downloader = std::make_shared<Downloader>(
            m_account, m_mockAuthenticationInfoProvider,
            m_mockProtocolVersionChecker, m_mockSyncStateStorage,
            m_mockSyncChunksProvider, m_mockSyncChunksStorage,
            m_mockLinkedNotebooksProcessor, nullptr, m_mockNotesProcessor,
            m_mockResourcesProcessor, m_mockSavedSearchesProcessor,
            m_mockTagsProcessor, m_mockFullSyncStaleDataExpunger, m_ctx,
            m_mockNoteStore, m_mockLocalStorage, QDir{m_temporaryDir.path()}),
        InvalidArgument);
}

TEST_F(DownloaderTest, CtorNullNotesProcessor)
{
    EXPECT_THROW(
        const auto downloader = std::make_shared<Downloader>(
            m_account, m_mockAuthenticationInfoProvider,
            m_mockProtocolVersionChecker, m_mockSyncStateStorage,
            m_mockSyncChunksProvider, m_mockSyncChunksStorage,
            m_mockLinkedNotebooksProcessor, m_mockNotebooksProcessor, nullptr,
            m_mockResourcesProcessor, m_mockSavedSearchesProcessor,
            m_mockTagsProcessor, m_mockFullSyncStaleDataExpunger, m_ctx,
            m_mockNoteStore, m_mockLocalStorage, QDir{m_temporaryDir.path()}),
        InvalidArgument);
}

TEST_F(DownloaderTest, CtorNullResourcesProcessor)
{
    EXPECT_THROW(
        const auto downloader = std::make_shared<Downloader>(
            m_account, m_mockAuthenticationInfoProvider,
            m_mockProtocolVersionChecker, m_mockSyncStateStorage,
            m_mockSyncChunksProvider, m_mockSyncChunksStorage,
            m_mockLinkedNotebooksProcessor, m_mockNotebooksProcessor,
            m_mockNotesProcessor, nullptr, m_mockSavedSearchesProcessor,
            m_mockTagsProcessor, m_mockFullSyncStaleDataExpunger, m_ctx,
            m_mockNoteStore, m_mockLocalStorage, QDir{m_temporaryDir.path()}),
        InvalidArgument);
}

TEST_F(DownloaderTest, CtorNullSavedSearchesProcessor)
{
    EXPECT_THROW(
        const auto downloader = std::make_shared<Downloader>(
            m_account, m_mockAuthenticationInfoProvider,
            m_mockProtocolVersionChecker, m_mockSyncStateStorage,
            m_mockSyncChunksProvider, m_mockSyncChunksStorage,
            m_mockLinkedNotebooksProcessor, m_mockNotebooksProcessor,
            m_mockNotesProcessor, m_mockResourcesProcessor, nullptr,
            m_mockTagsProcessor, m_mockFullSyncStaleDataExpunger, m_ctx,
            m_mockNoteStore, m_mockLocalStorage, QDir{m_temporaryDir.path()}),
        InvalidArgument);
}

TEST_F(DownloaderTest, CtorNullTagsProcessor)
{
    EXPECT_THROW(
        const auto downloader = std::make_shared<Downloader>(
            m_account, m_mockAuthenticationInfoProvider,
            m_mockProtocolVersionChecker, m_mockSyncStateStorage,
            m_mockSyncChunksProvider, m_mockSyncChunksStorage,
            m_mockLinkedNotebooksProcessor, m_mockNotebooksProcessor,
            m_mockNotesProcessor, m_mockResourcesProcessor,
            m_mockSavedSearchesProcessor, nullptr,
            m_mockFullSyncStaleDataExpunger, m_ctx, m_mockNoteStore,
            m_mockLocalStorage, QDir{m_temporaryDir.path()}),
        InvalidArgument);
}

TEST_F(DownloaderTest, CtorNullFullSyncStaleDataExpunger)
{
    EXPECT_THROW(
        const auto downloader = std::make_shared<Downloader>(
            m_account, m_mockAuthenticationInfoProvider,
            m_mockProtocolVersionChecker, m_mockSyncStateStorage,
            m_mockSyncChunksProvider, m_mockSyncChunksStorage,
            m_mockLinkedNotebooksProcessor, m_mockNotebooksProcessor,
            m_mockNotesProcessor, m_mockResourcesProcessor,
            m_mockSavedSearchesProcessor, m_mockTagsProcessor, nullptr, m_ctx,
            m_mockNoteStore, m_mockLocalStorage, QDir{m_temporaryDir.path()}),
        InvalidArgument);
}

TEST_F(DownloaderTest, CtorNullRequestContext)
{
    EXPECT_THROW(
        const auto downloader = std::make_shared<Downloader>(
            m_account, m_mockAuthenticationInfoProvider,
            m_mockProtocolVersionChecker, m_mockSyncStateStorage,
            m_mockSyncChunksProvider, m_mockSyncChunksStorage,
            m_mockLinkedNotebooksProcessor, m_mockNotebooksProcessor,
            m_mockNotesProcessor, m_mockResourcesProcessor,
            m_mockSavedSearchesProcessor, m_mockTagsProcessor,
            m_mockFullSyncStaleDataExpunger, nullptr, m_mockNoteStore,
            m_mockLocalStorage, QDir{m_temporaryDir.path()}),
        InvalidArgument);
}

TEST_F(DownloaderTest, CtorNullNoteStore)
{
    EXPECT_THROW(
        const auto downloader = std::make_shared<Downloader>(
            m_account, m_mockAuthenticationInfoProvider,
            m_mockProtocolVersionChecker, m_mockSyncStateStorage,
            m_mockSyncChunksProvider, m_mockSyncChunksStorage,
            m_mockLinkedNotebooksProcessor, m_mockNotebooksProcessor,
            m_mockNotesProcessor, m_mockResourcesProcessor,
            m_mockSavedSearchesProcessor, m_mockTagsProcessor,
            m_mockFullSyncStaleDataExpunger, m_ctx, nullptr, m_mockLocalStorage,
            QDir{m_temporaryDir.path()}),
        InvalidArgument);
}

TEST_F(DownloaderTest, CtorNullLocalStorage)
{
    EXPECT_THROW(
        const auto downloader = std::make_shared<Downloader>(
            m_account, m_mockAuthenticationInfoProvider,
            m_mockProtocolVersionChecker, m_mockSyncStateStorage,
            m_mockSyncChunksProvider, m_mockSyncChunksStorage,
            m_mockLinkedNotebooksProcessor, m_mockNotebooksProcessor,
            m_mockNotesProcessor, m_mockResourcesProcessor,
            m_mockSavedSearchesProcessor, m_mockTagsProcessor,
            m_mockFullSyncStaleDataExpunger, m_ctx, m_mockNoteStore, nullptr,
            QDir{m_temporaryDir.path()}),
        InvalidArgument);
}

/*
constexpr std::array gSyncChunksFlags{
    SyncChunksFlags{} | SyncChunksFlag::WithNotebooks,
    SyncChunksFlags{} | SyncChunksFlag::WithNotebooks |
        SyncChunksFlag::WithNotes,
    SyncChunksFlags{} | SyncChunksFlag::WithNotebooks |
        SyncChunksFlag::WithNotes | SyncChunksFlag::WithResources,
    SyncChunksFlags{} | SyncChunksFlag::WithSavedSearches,
    SyncChunksFlags{} | SyncChunksFlag::WithTags,
    SyncChunksFlags{} | SyncChunksFlag::WithSavedSearches |
        SyncChunksFlag::WithTags,
    SyncChunksFlags{} | SyncChunksFlag::WithNotebooks |
        SyncChunksFlag::WithSavedSearches,
    SyncChunksFlags{} | SyncChunksFlag::WithTags |
        SyncChunksFlag::WithSavedSearches,
    SyncChunksFlags{} | SyncChunksFlag::WithNotebooks |
        SyncChunksFlag::WithSavedSearches | SyncChunksFlag::WithTags,
    SyncChunksFlags{} | SyncChunksFlag::WithNotebooks |
        SyncChunksFlag::WithNotes | SyncChunksFlag::WithResources |
        SyncChunksFlag::WithSavedSearches | SyncChunksFlag::WithTags,
};

class DownloaderSyncChunksTest :
    public DownloaderTest,
    public testing::WithParamInterface<SyncChunksFlags>
{};

INSTANTIATE_TEST_SUITE_P(
    DownloaderSyncChunksTestInstance, DownloaderSyncChunksTest,
    testing::ValuesIn(gSyncChunksFlags));

TEST_P(DownloaderSyncChunksTest, DownloadUserOwnData)
{
    const auto downloader = std::make_shared<Downloader>(
        m_account, m_mockAuthenticationInfoProvider,
        m_mockProtocolVersionChecker, m_mockSyncStateStorage,
        m_mockSyncChunksProvider, m_mockSyncChunksStorage,
        m_mockLinkedNotebooksProcessor, m_mockNotebooksProcessor,
        m_mockNotesProcessor, m_mockResourcesProcessor,
        m_mockSavedSearchesProcessor, m_mockTagsProcessor,
        m_mockFullSyncStaleDataExpunger, m_ctx, m_mockNoteStore,
        m_mockLocalStorage, QDir{m_temporaryDir.path()});

    EXPECT_CALL(*m_mockSyncStateStorage, getSyncState(m_account))
        .WillOnce(Return(std::make_shared<SyncState>()));

    const auto now = QDateTime::currentMSecsSinceEpoch();

    auto authenticationInfo = std::make_shared<AuthenticationInfo>();
    authenticationInfo->m_userId = m_account.id();
    authenticationInfo->m_authToken = QStringLiteral("authToken");
    authenticationInfo->m_authTokenExpirationTime = now + 100000000;
    authenticationInfo->m_authenticationTime = now;
    authenticationInfo->m_shardId = QStringLiteral("shardId");
    authenticationInfo->m_noteStoreUrl = QStringLiteral("noteStoreUrl");
    authenticationInfo->m_webApiUrlPrefix = QStringLiteral("webApiUrlPrefix");

    EXPECT_CALL(
        *m_mockAuthenticationInfoProvider,
        authenticateAccount(
            m_account, IAuthenticationInfoProvider::Mode::Cache))
        .WillOnce(Return(threading::makeReadyFuture<IAuthenticationInfoPtr>(
            authenticationInfo)));

    EXPECT_CALL(*m_mockProtocolVersionChecker, checkProtocolVersion)
        .WillOnce([&](const IAuthenticationInfo & authInfo) {
            EXPECT_EQ(authInfo.userId(), authenticationInfo->userId());

            EXPECT_EQ(authInfo.authToken(), authenticationInfo->authToken());

            EXPECT_EQ(
                authInfo.authTokenExpirationTime(),
                authenticationInfo->authTokenExpirationTime());

            EXPECT_EQ(
                authInfo.authenticationTime(),
                authenticationInfo->authenticationTime());

            EXPECT_EQ(authInfo.shardId(), authenticationInfo->shardId());

            EXPECT_EQ(
                authInfo.noteStoreUrl(), authenticationInfo->noteStoreUrl());

            EXPECT_EQ(
                authInfo.webApiUrlPrefix(),
                authenticationInfo->webApiUrlPrefix());

            return threading::makeReadyFuture();
        });

    EXPECT_CALL(*m_mockNoteStore, getSyncStateAsync)
        .WillOnce([&](const qevercloud::IRequestContextPtr & ctx) {
            EXPECT_TRUE(ctx);
            if (ctx) {
                EXPECT_EQ(
                    ctx->authenticationToken(),
                    authenticationInfo->authToken());
            }
            return threading::makeReadyFuture<qevercloud::SyncState>({});
        });

    const auto syncChunksFlags = GetParam();
    const auto syncChunks = generateSynChunks(syncChunksFlags);

    EXPECT_CALL(*m_mockSyncChunksProvider, fetchSyncChunks)
        .WillOnce(
            [&](const qint32 afterUsn,
                const qevercloud::IRequestContextPtr & ctx,
                [[maybe_unused]] const utility::cancelers::ICancelerPtr &
                    canceler,
                const ISyncChunksProvider::ICallbackWeakPtr & callbackWeak) {
                EXPECT_EQ(afterUsn, 0);
                EXPECT_TRUE(ctx);
                if (ctx) {
                    EXPECT_EQ(
                        ctx->authenticationToken(),
                        authenticationInfo->authToken());
                }

                EXPECT_FALSE(callbackWeak.expired());

                return threading::makeReadyFuture<QList<qevercloud::SyncChunk>>(
                    syncChunks);
            });

    std::shared_ptr<mocks::MockIDownloaderICallback> downloaderCallback =
        std::make_shared<StrictMock<mocks::MockIDownloaderICallback>>();

    EXPECT_CALL(*downloaderCallback, onSyncChunksDownloaded);

    EXPECT_CALL(*m_mockNotebooksProcessor, processNotebooks(syncChunks, _))
        .WillOnce(Return(threading::makeReadyFuture()));

    EXPECT_CALL(*m_mockTagsProcessor, processTags(syncChunks, _))
        .WillOnce(Return(threading::makeReadyFuture()));

    EXPECT_CALL(
        *m_mockSavedSearchesProcessor, processSavedSearches(syncChunks, _))
        .WillOnce(Return(threading::makeReadyFuture()));

    EXPECT_CALL(
        *m_mockLinkedNotebooksProcessor, processLinkedNotebooks(syncChunks, _))
        .WillOnce(Return(threading::makeReadyFuture()));

    EXPECT_CALL(*m_mockNotesProcessor, processNotes)
        .WillOnce([&]([[maybe_unused]] const QList<qevercloud::SyncChunk> &
                          chunks,
                      [[maybe_unused]] const utility::cancelers::ICancelerPtr &
                          canceler,
                      [[maybe_unused]] const IDurableNotesProcessor::
                          ICallbackWeakPtr & callbackWeak) {
            EXPECT_EQ(chunks, syncChunks);
            return threading::makeReadyFuture<DownloadNotesStatusPtr>(
                std::make_shared<DownloadNotesStatus>());
        });

    EXPECT_CALL(*m_mockResourcesProcessor, processResources)
        .WillOnce([&]([[maybe_unused]] const QList<qevercloud::SyncChunk> &
                          chunks,
                      [[maybe_unused]] const utility::cancelers::ICancelerPtr &
                          canceler,
                      [[maybe_unused]] const IDurableResourcesProcessor::
                          ICallbackWeakPtr & callbackWeak) {
            EXPECT_EQ(chunks, syncChunks);
            return threading::makeReadyFuture<DownloadResourcesStatusPtr>(
                std::make_shared<DownloadResourcesStatus>());
        });

    EXPECT_CALL(*m_mockLocalStorage, listLinkedNotebooks)
        .WillOnce(Return(
            threading::makeReadyFuture<QList<qevercloud::LinkedNotebook>>({})));

    auto result = downloader->download(m_manualCanceler, downloaderCallback);
    ASSERT_TRUE(result.isFinished());
}
*/

} // namespace quentier::synchronization::tests
