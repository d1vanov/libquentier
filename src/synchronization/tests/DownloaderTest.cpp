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
#include <quentier/exception/RuntimeError.h>
#include <quentier/local_storage/tests/mocks/MockILocalStorage.h>
#include <quentier/synchronization/ISyncChunksDataCounters.h>
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
#include <qevercloud/types/builders/SyncStateBuilder.h>
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
using testing::AnyNumber;
using testing::AtMost;
using testing::InSequence;
using testing::Ne;
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

            QList<qevercloud::Guid> expungedLinkedNotebooks;
            expungedLinkedNotebooks.reserve(itemCountPerSyncChunk);

            for (qint32 j = 0; j < itemCountPerSyncChunk; ++j) {
                linkedNotebooks
                    << qevercloud::LinkedNotebookBuilder{}
                           .setGuid(UidGenerator::Generate())
                           .setUpdateSequenceNum(usn++)
                           .setUsername(QString::fromUtf8("Linked notebook #%1")
                                            .arg(j + 1))
                           .build();

                expungedLinkedNotebooks << UidGenerator::Generate();
            }
            builder.setChunkHighUSN(
                linkedNotebooks.last().updateSequenceNum().value());

            builder.setLinkedNotebooks(std::move(linkedNotebooks));

            builder.setExpungedLinkedNotebooks(
                std::move(expungedLinkedNotebooks));
        }

        if (flags.testFlag(SyncChunksFlag::WithNotes)) {
            QList<qevercloud::Note> notes;
            notes.reserve(itemCountPerSyncChunk);

            QList<qevercloud::Guid> expungedNotes;
            expungedNotes.reserve(itemCountPerSyncChunk);

            for (qint32 j = 0; j < itemCountPerSyncChunk; ++j) {
                notes << qevercloud::NoteBuilder{}
                             .setGuid(UidGenerator::Generate())
                             .setUpdateSequenceNum(usn++)
                             .setTitle(QString::fromUtf8("Note #%1").arg(j + 1))
                             .setNotebookGuid(UidGenerator::Generate())
                             .build();

                expungedNotes << UidGenerator::Generate();
            }
            builder.setChunkHighUSN(notes.last().updateSequenceNum().value());
            builder.setNotes(std::move(notes));
            builder.setExpungedNotes(std::move(expungedNotes));
        }

        if (flags.testFlag(SyncChunksFlag::WithNotebooks)) {
            QList<qevercloud::Notebook> notebooks;
            notebooks.reserve(itemCountPerSyncChunk);

            QList<qevercloud::Guid> expungedNotebooks;
            expungedNotebooks.reserve(itemCountPerSyncChunk);

            for (qint32 j = 0; j < itemCountPerSyncChunk; ++j) {
                notebooks << qevercloud::NotebookBuilder{}
                                 .setGuid(UidGenerator::Generate())
                                 .setUpdateSequenceNum(usn++)
                                 .setName(QString::fromUtf8("Notebook #%1")
                                              .arg(j + 1))
                                 .build();

                expungedNotebooks << UidGenerator::Generate();
            }
            builder.setChunkHighUSN(
                notebooks.last().updateSequenceNum().value());

            builder.setNotebooks(std::move(notebooks));
            builder.setExpungedNotebooks(std::move(expungedNotebooks));
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
            builder.setChunkHighUSN(
                resources.last().updateSequenceNum().value());

            builder.setResources(std::move(resources));
        }

        if (flags.testFlag(SyncChunksFlag::WithSavedSearches)) {
            QList<qevercloud::SavedSearch> savedSearches;
            savedSearches.reserve(itemCountPerSyncChunk);

            QList<qevercloud::Guid> expungedSavedSearches;
            expungedSavedSearches.reserve(itemCountPerSyncChunk);

            for (qint32 j = 0; j < itemCountPerSyncChunk; ++j) {
                savedSearches
                    << qevercloud::SavedSearchBuilder{}
                           .setGuid(UidGenerator::Generate())
                           .setUpdateSequenceNum(usn++)
                           .setName(
                               QString::fromUtf8("Saved search #%1").arg(j + 1))
                           .build();

                expungedSavedSearches << UidGenerator::Generate();
            }
            builder.setChunkHighUSN(
                savedSearches.last().updateSequenceNum().value());

            builder.setSearches(std::move(savedSearches));
            builder.setExpungedSearches(std::move(expungedSavedSearches));
        }

        if (flags.testFlag(SyncChunksFlag::WithTags)) {
            QList<qevercloud::Tag> tags;
            tags.reserve(itemCountPerSyncChunk);

            QList<qevercloud::Guid> expungedTags;
            expungedTags.reserve(itemCountPerSyncChunk);

            for (qint32 j = 0; j < itemCountPerSyncChunk; ++j) {
                tags << qevercloud::TagBuilder{}
                            .setGuid(UidGenerator::Generate())
                            .setUpdateSequenceNum(usn++)
                            .setName(QString::fromUtf8("Tag #%1").arg(j + 1))
                            .build();

                expungedTags << UidGenerator::Generate();
            }
            builder.setChunkHighUSN(tags.last().updateSequenceNum().value());
            builder.setTags(std::move(tags));
            builder.setExpungedTags(std::move(expungedTags));
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
            m_account, m_mockAuthenticationInfoProvider, m_mockSyncStateStorage,
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
            Account{}, m_mockAuthenticationInfoProvider, m_mockSyncStateStorage,
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
            m_mockSyncStateStorage, m_mockSyncChunksProvider,
            m_mockSyncChunksStorage, m_mockLinkedNotebooksProcessor,
            m_mockNotebooksProcessor, m_mockNotesProcessor,
            m_mockResourcesProcessor, m_mockSavedSearchesProcessor,
            m_mockTagsProcessor, m_mockFullSyncStaleDataExpunger, m_ctx,
            m_mockNoteStore, m_mockLocalStorage, QDir{m_temporaryDir.path()}),
        InvalidArgument);
}

TEST_F(DownloaderTest, CtorNullAuthenticationInfoProvider)
{
    EXPECT_THROW(
        const auto downloader = std::make_shared<Downloader>(
            m_account, nullptr, m_mockSyncStateStorage,
            m_mockSyncChunksProvider, m_mockSyncChunksStorage,
            m_mockLinkedNotebooksProcessor, m_mockNotebooksProcessor,
            m_mockNotesProcessor, m_mockResourcesProcessor,
            m_mockSavedSearchesProcessor, m_mockTagsProcessor,
            m_mockFullSyncStaleDataExpunger, m_ctx, m_mockNoteStore,
            m_mockLocalStorage, QDir{m_temporaryDir.path()}),
        InvalidArgument);
}

TEST_F(DownloaderTest, CtorNullSyncStateStorage)
{
    EXPECT_THROW(
        const auto downloader = std::make_shared<Downloader>(
            m_account, m_mockAuthenticationInfoProvider, nullptr,
            m_mockSyncChunksProvider, m_mockSyncChunksStorage,
            m_mockLinkedNotebooksProcessor, m_mockNotebooksProcessor,
            m_mockNotesProcessor, m_mockResourcesProcessor,
            m_mockSavedSearchesProcessor, m_mockTagsProcessor,
            m_mockFullSyncStaleDataExpunger, m_ctx, m_mockNoteStore,
            m_mockLocalStorage, QDir{m_temporaryDir.path()}),
        InvalidArgument);
}

TEST_F(DownloaderTest, CtorNullSyncChunksProvider)
{
    EXPECT_THROW(
        const auto downloader = std::make_shared<Downloader>(
            m_account, m_mockAuthenticationInfoProvider, m_mockSyncStateStorage,
            nullptr, m_mockSyncChunksStorage, m_mockLinkedNotebooksProcessor,
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
            m_account, m_mockAuthenticationInfoProvider, m_mockSyncStateStorage,
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
            m_account, m_mockAuthenticationInfoProvider, m_mockSyncStateStorage,
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
            m_account, m_mockAuthenticationInfoProvider, m_mockSyncStateStorage,
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
            m_account, m_mockAuthenticationInfoProvider, m_mockSyncStateStorage,
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
            m_account, m_mockAuthenticationInfoProvider, m_mockSyncStateStorage,
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
            m_account, m_mockAuthenticationInfoProvider, m_mockSyncStateStorage,
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
            m_account, m_mockAuthenticationInfoProvider, m_mockSyncStateStorage,
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
            m_account, m_mockAuthenticationInfoProvider, m_mockSyncStateStorage,
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
            m_account, m_mockAuthenticationInfoProvider, m_mockSyncStateStorage,
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
            m_account, m_mockAuthenticationInfoProvider, m_mockSyncStateStorage,
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
            m_account, m_mockAuthenticationInfoProvider, m_mockSyncStateStorage,
            m_mockSyncChunksProvider, m_mockSyncChunksStorage,
            m_mockLinkedNotebooksProcessor, m_mockNotebooksProcessor,
            m_mockNotesProcessor, m_mockResourcesProcessor,
            m_mockSavedSearchesProcessor, m_mockTagsProcessor,
            m_mockFullSyncStaleDataExpunger, m_ctx, m_mockNoteStore, nullptr,
            QDir{m_temporaryDir.path()}),
        InvalidArgument);
}

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
        m_account, m_mockAuthenticationInfoProvider, m_mockSyncStateStorage,
        m_mockSyncChunksProvider, m_mockSyncChunksStorage,
        m_mockLinkedNotebooksProcessor, m_mockNotebooksProcessor,
        m_mockNotesProcessor, m_mockResourcesProcessor,
        m_mockSavedSearchesProcessor, m_mockTagsProcessor,
        m_mockFullSyncStaleDataExpunger, m_ctx, m_mockNoteStore,
        m_mockLocalStorage, QDir{m_temporaryDir.path()});

    const auto now = QDateTime::currentMSecsSinceEpoch();

    auto authenticationInfo = std::make_shared<AuthenticationInfo>();
    authenticationInfo->m_userId = m_account.id();
    authenticationInfo->m_authToken = QStringLiteral("authToken");
    authenticationInfo->m_authTokenExpirationTime = now + 100000000;
    authenticationInfo->m_authenticationTime = now;
    authenticationInfo->m_shardId = QStringLiteral("shardId");
    authenticationInfo->m_noteStoreUrl = QStringLiteral("noteStoreUrl");
    authenticationInfo->m_webApiUrlPrefix = QStringLiteral("webApiUrlPrefix");

    const auto syncChunksFlags = GetParam();
    const auto syncChunks = generateSynChunks(syncChunksFlags);
    ASSERT_FALSE(syncChunks.isEmpty());

    std::shared_ptr<mocks::MockIDownloaderICallback> mockDownloaderCallback =
        std::make_shared<StrictMock<mocks::MockIDownloaderICallback>>();

    {
        InSequence s;

        EXPECT_CALL(*m_mockSyncStateStorage, getSyncState(m_account))
            .WillOnce(Return(std::make_shared<SyncState>()));

        EXPECT_CALL(
            *m_mockAuthenticationInfoProvider,
            authenticateAccount(
                m_account, IAuthenticationInfoProvider::Mode::Cache))
            .WillOnce(Return(threading::makeReadyFuture<IAuthenticationInfoPtr>(
                authenticationInfo)));

        EXPECT_CALL(*m_mockNoteStore, getSyncStateAsync)
            .WillOnce([&](const qevercloud::IRequestContextPtr & ctx) {
                EXPECT_TRUE(ctx);
                if (ctx) {
                    EXPECT_EQ(
                        ctx->authenticationToken(),
                        authenticationInfo->authToken());
                }
                return threading::makeReadyFuture<qevercloud::SyncState>(
                    qevercloud::SyncStateBuilder{}
                        .setUpdateCount(
                            syncChunks.last().chunkHighUSN().value())
                        .build());
            });

        EXPECT_CALL(*m_mockSyncChunksProvider, fetchSyncChunks)
            .WillOnce([&](const qint32 afterUsn,
                          const qevercloud::IRequestContextPtr & ctx,
                          [[maybe_unused]] const utility::cancelers::
                              ICancelerPtr & canceler,
                          const ISyncChunksProvider::ICallbackWeakPtr &
                              callbackWeak) {
                EXPECT_EQ(afterUsn, 0);
                EXPECT_TRUE(ctx);
                if (ctx) {
                    EXPECT_EQ(
                        ctx->authenticationToken(),
                        authenticationInfo->authToken());
                }

                EXPECT_FALSE(callbackWeak.expired());
                if (const auto callback = callbackWeak.lock()) {
                    const qint32 lastPreviousUsn = afterUsn;
                    const qint32 highestServerUsn =
                        syncChunks.last().chunkHighUSN().value();
                    for (const auto & syncChunk: qAsConst(syncChunks)) {
                        callback->onUserOwnSyncChunksDownloadProgress(
                            syncChunk.chunkHighUSN().value(), highestServerUsn,
                            lastPreviousUsn);
                    }
                }

                return threading::makeReadyFuture<QList<qevercloud::SyncChunk>>(
                    syncChunks);
            });

        {
            const qint32 lastPreviousUsn = 0;
            const qint32 highestServerUsn =
                syncChunks.last().chunkHighUSN().value();
            for (const auto & syncChunk: qAsConst(syncChunks)) {
                EXPECT_CALL(
                    *mockDownloaderCallback,
                    onSyncChunksDownloadProgress(
                        syncChunk.chunkHighUSN().value(), highestServerUsn,
                        lastPreviousUsn))
                    .Times(1);
            }
        }
    }

    ISyncChunksDataCountersPtr lastSyncChunksDataCounters;
    EXPECT_CALL(*mockDownloaderCallback, onSyncChunksDataProcessingProgress)
        .Times(AnyNumber())
        .WillRepeatedly([&](ISyncChunksDataCountersPtr counters) {
            ASSERT_TRUE(counters);
            if (!lastSyncChunksDataCounters) {
                lastSyncChunksDataCounters = counters;
                return;
            }

            EXPECT_GE(
                counters->totalSavedSearches(),
                lastSyncChunksDataCounters->totalSavedSearches());

            EXPECT_GE(
                counters->totalExpungedSavedSearches(),
                lastSyncChunksDataCounters->totalExpungedSavedSearches());

            EXPECT_GE(
                counters->addedSavedSearches(),
                lastSyncChunksDataCounters->addedSavedSearches());

            EXPECT_GE(
                counters->updatedSavedSearches(),
                lastSyncChunksDataCounters->updatedSavedSearches());

            EXPECT_GE(
                counters->expungedSavedSearches(),
                lastSyncChunksDataCounters->expungedSavedSearches());

            EXPECT_GE(
                counters->totalTags(), lastSyncChunksDataCounters->totalTags());

            EXPECT_GE(
                counters->totalExpungedTags(),
                lastSyncChunksDataCounters->totalExpungedTags());

            EXPECT_GE(
                counters->addedTags(), lastSyncChunksDataCounters->addedTags());

            EXPECT_GE(
                counters->updatedTags(),
                lastSyncChunksDataCounters->updatedTags());

            EXPECT_GE(
                counters->expungedTags(),
                lastSyncChunksDataCounters->expungedTags());

            EXPECT_GE(
                counters->totalLinkedNotebooks(),
                lastSyncChunksDataCounters->totalLinkedNotebooks());

            EXPECT_GE(
                counters->totalExpungedLinkedNotebooks(),
                lastSyncChunksDataCounters->totalExpungedLinkedNotebooks());

            EXPECT_GE(
                counters->addedLinkedNotebooks(),
                lastSyncChunksDataCounters->addedLinkedNotebooks());

            EXPECT_GE(
                counters->updatedLinkedNotebooks(),
                lastSyncChunksDataCounters->updatedLinkedNotebooks());

            EXPECT_GE(
                counters->expungedLinkedNotebooks(),
                lastSyncChunksDataCounters->expungedLinkedNotebooks());

            EXPECT_GE(
                counters->totalNotebooks(),
                lastSyncChunksDataCounters->totalNotebooks());

            EXPECT_GE(
                counters->totalExpungedNotebooks(),
                lastSyncChunksDataCounters->totalExpungedNotebooks());

            EXPECT_GE(
                counters->addedNotebooks(),
                lastSyncChunksDataCounters->addedNotebooks());

            EXPECT_GE(
                counters->updatedNotebooks(),
                lastSyncChunksDataCounters->updatedNotebooks());

            EXPECT_GE(
                counters->expungedNotebooks(),
                lastSyncChunksDataCounters->expungedNotebooks());

            lastSyncChunksDataCounters = counters;
        });

    quint32 lastDownloadedNotes = 0U;
    quint32 lastTotalNotesToDownload = 0U;

    const qint32 noteCount = [&] {
        qint32 result = 0;
        for (const auto & syncChunk: qAsConst(syncChunks)) {
            if (syncChunk.notes()) {
                result += syncChunk.notes()->size();
            }
        }
        return result;
    }();

    if (noteCount != 0) {
        EXPECT_CALL(*mockDownloaderCallback, onNotesDownloadProgress)
            .Times(AtMost(noteCount))
            .WillRepeatedly([&](const quint32 notesDownloaded,
                                const quint32 totalNotesToDownload) {
                EXPECT_GT(notesDownloaded, lastDownloadedNotes);

                EXPECT_TRUE(
                    lastTotalNotesToDownload == 0U ||
                    lastTotalNotesToDownload == totalNotesToDownload);

                lastDownloadedNotes = notesDownloaded;
                lastTotalNotesToDownload = totalNotesToDownload;
            });
    }

    quint32 lastDownloadedResources = 0U;
    quint32 lastTotalResourcesToDownload = 0U;

    const qint32 resourceCount = [&] {
        qint32 result = 0;
        for (const auto & syncChunk: qAsConst(syncChunks)) {
            if (syncChunk.resources()) {
                result += syncChunk.resources()->size();
            }
        }
        return result;
    }();

    if (resourceCount != 0) {
        EXPECT_CALL(*mockDownloaderCallback, onResourcesDownloadProgress)
            .Times(AtMost(resourceCount))
            .WillRepeatedly([&](const quint32 resourcesDownloaded,
                                const quint32 totalResourcesToDownload) {
                EXPECT_GT(resourcesDownloaded, lastDownloadedResources);

                EXPECT_TRUE(
                    lastTotalResourcesToDownload == 0U ||
                    lastTotalResourcesToDownload == totalResourcesToDownload);

                lastDownloadedResources = resourcesDownloaded;
                lastTotalResourcesToDownload = totalResourcesToDownload;
            });
    }

    EXPECT_CALL(*mockDownloaderCallback, onSyncChunksDownloaded);

    {
        InSequence s;

        EXPECT_CALL(*m_mockNotebooksProcessor, processNotebooks(syncChunks, _))
            .WillOnce(
                [&](const QList<qevercloud::SyncChunk> & chunks,
                    const INotebooksProcessor::ICallbackWeakPtr callbackWeak) {
                    const auto callback = callbackWeak.lock();
                    EXPECT_TRUE(callback);
                    if (callback) {
                        const qint32 totalNotebooks = [&] {
                            qint32 result = 0;
                            for (const auto & syncChunk: qAsConst(chunks)) {
                                if (syncChunk.notebooks()) {
                                    result += syncChunk.notebooks()->size();
                                }
                            }
                            return result;
                        }();

                        const qint32 totalExpungedNotebooks = [&] {
                            qint32 result = 0;
                            for (const auto & syncChunk: qAsConst(chunks)) {
                                if (syncChunk.expungedNotebooks()) {
                                    result +=
                                        syncChunk.expungedNotebooks()->size();
                                }
                            }
                            return result;
                        }();

                        const qint32 addedNotebooks = totalNotebooks / 2;
                        const qint32 updatedNotebooks =
                            totalNotebooks - addedNotebooks;

                        callback->onNotebooksProcessingProgress(
                            totalNotebooks, totalExpungedNotebooks,
                            addedNotebooks, updatedNotebooks,
                            totalExpungedNotebooks);
                    }

                    return threading::makeReadyFuture();
                });

        EXPECT_CALL(*m_mockTagsProcessor, processTags(syncChunks, _))
            .WillOnce(
                [&](const QList<qevercloud::SyncChunk> & chunks,
                    const ITagsProcessor::ICallbackWeakPtr & callbackWeak) {
                    const auto callback = callbackWeak.lock();
                    EXPECT_TRUE(callback);
                    if (callback) {
                        const qint32 totalTags = [&] {
                            qint32 result = 0;
                            for (const auto & syncChunk: qAsConst(chunks)) {
                                if (syncChunk.tags()) {
                                    result += syncChunk.tags()->size();
                                }
                            }
                            return result;
                        }();

                        const qint32 totalExpungedTags = [&] {
                            qint32 result = 0;
                            for (const auto & syncChunk: qAsConst(chunks)) {
                                if (syncChunk.expungedTags()) {
                                    result += syncChunk.expungedTags()->size();
                                }
                            }
                            return result;
                        }();

                        const qint32 addedTags = totalTags / 2;
                        const qint32 updatedTags = totalTags - addedTags;

                        callback->onTagsProcessingProgress(
                            totalTags, totalExpungedTags, addedTags,
                            updatedTags, totalExpungedTags);
                    }

                    return threading::makeReadyFuture();
                });

        EXPECT_CALL(
            *m_mockSavedSearchesProcessor, processSavedSearches(syncChunks, _))
            .WillOnce([&](const QList<qevercloud::SyncChunk> & chunks,
                          const ISavedSearchesProcessor::ICallbackWeakPtr &
                              callbackWeak) {
                const auto callback = callbackWeak.lock();
                EXPECT_TRUE(callback);
                if (callback) {
                    const qint32 totalSavedSearches = [&] {
                        qint32 result = 0;
                        for (const auto & syncChunk: qAsConst(chunks)) {
                            if (syncChunk.searches()) {
                                result += syncChunk.searches()->size();
                            }
                        }
                        return result;
                    }();

                    const qint32 totalExpungedSavedSearches = [&] {
                        qint32 result = 0;
                        for (const auto & syncChunk: qAsConst(chunks)) {
                            if (syncChunk.expungedSearches()) {
                                result += syncChunk.expungedSearches()->size();
                            }
                        }
                        return result;
                    }();

                    const qint32 addedSavedSearches = totalSavedSearches / 2;
                    const qint32 updatedSavedSearches =
                        totalSavedSearches - addedSavedSearches;

                    callback->onSavedSearchesProcessingProgress(
                        totalSavedSearches, totalExpungedSavedSearches,
                        addedSavedSearches, updatedSavedSearches,
                        totalExpungedSavedSearches);
                }

                return threading::makeReadyFuture();
            });

        EXPECT_CALL(
            *m_mockLinkedNotebooksProcessor,
            processLinkedNotebooks(syncChunks, _))
            .WillOnce([&](const QList<qevercloud::SyncChunk> & chunks,
                          const ILinkedNotebooksProcessor::ICallbackWeakPtr &
                              callbackWeak) {
                const auto callback = callbackWeak.lock();
                EXPECT_TRUE(callback);
                if (callback) {
                    const qint32 totalLinkedNotebooks = [&] {
                        qint32 result = 0;
                        for (const auto & syncChunk: qAsConst(chunks)) {
                            if (syncChunk.linkedNotebooks()) {
                                result += syncChunk.linkedNotebooks()->size();
                            }
                        }
                        return result;
                    }();

                    const qint32 totalExpungedLinkedNotebooks = [&] {
                        qint32 result = 0;
                        for (const auto & syncChunk: qAsConst(chunks)) {
                            if (syncChunk.expungedLinkedNotebooks()) {
                                result +=
                                    syncChunk.expungedLinkedNotebooks()->size();
                            }
                        }
                        return result;
                    }();

                    callback->onLinkedNotebooksProcessingProgress(
                        totalLinkedNotebooks, totalExpungedLinkedNotebooks,
                        totalLinkedNotebooks, totalExpungedLinkedNotebooks);
                }

                return threading::makeReadyFuture();
            });

        EXPECT_CALL(*m_mockNotesProcessor, processNotes(syncChunks, _, _))
            .WillOnce([&](const QList<qevercloud::SyncChunk> & chunks,
                          const utility::cancelers::ICancelerPtr & canceler,
                          const IDurableNotesProcessor::ICallbackWeakPtr &
                              callbackWeak) {
                EXPECT_TRUE(canceler);

                const auto callback = callbackWeak.lock();
                EXPECT_TRUE(callback);

                auto downloadNotesStatus =
                    std::make_shared<DownloadNotesStatus>();

                int noteIndex = 0;
                for (const auto & syncChunk: qAsConst(chunks)) {
                    if (!syncChunk.notes()) {
                        continue;
                    }

                    for (const auto & note: qAsConst(*syncChunk.notes())) {
                        if (noteIndex % 4 == 0) {
                            const auto noteGuid = note.guid().value();
                            const auto noteUsn =
                                note.updateSequenceNum().value();

                            if (callback) {
                                callback->onProcessedNote(noteGuid, noteUsn);
                            }

                            downloadNotesStatus
                                ->m_processedNoteGuidsAndUsns[noteGuid] =
                                noteUsn;
                        }
                        else if (noteIndex % 4 == 1) {
                            auto exc = std::make_shared<RuntimeError>(
                                ErrorString{"some error"});

                            if (callback) {
                                callback->onNoteFailedToDownload(note, *exc);
                            }

                            downloadNotesStatus->m_notesWhichFailedToDownload
                                << DownloadNotesStatus::NoteWithException{
                                       note, std::move(exc)};
                        }
                        else if (noteIndex % 4 == 2) {
                            auto exc = std::make_shared<RuntimeError>(
                                ErrorString{"some error"});

                            if (callback) {
                                callback->onNoteFailedToProcess(note, *exc);
                            }

                            downloadNotesStatus->m_notesWhichFailedToProcess
                                << DownloadNotesStatus::NoteWithException{
                                       note, std::move(exc)};
                        }
                        else if (noteIndex % 4 == 3) {
                            if (callback) {
                                callback->onNoteProcessingCancelled(note);
                            }

                            const auto noteGuid = note.guid().value();
                            const auto noteUsn =
                                note.updateSequenceNum().value();

                            downloadNotesStatus
                                ->m_cancelledNoteGuidsAndUsns[noteGuid] =
                                noteUsn;
                        }
                        else {
                            EXPECT_FALSE(true) << "Unreachable code";
                        }

                        ++noteIndex;
                    }
                }

                downloadNotesStatus->m_totalNewNotes = noteIndex / 2;
                downloadNotesStatus->m_totalUpdatedNotes =
                    noteIndex - downloadNotesStatus->m_totalNewNotes;

                noteIndex = 0;
                for (const auto & syncChunk: qAsConst(chunks)) {
                    if (!syncChunk.expungedNotes()) {
                        continue;
                    }

                    for (const auto & expungedNoteGuid:
                         qAsConst(*syncChunk.expungedNotes())) {
                        if (noteIndex % 2 == 0) {
                            if (callback) {
                                callback->onExpungedNote(expungedNoteGuid);
                            }

                            downloadNotesStatus->m_expungedNoteGuids
                                << expungedNoteGuid;
                        }
                        else if (noteIndex % 2 == 1) {
                            auto exc = std::make_shared<RuntimeError>(
                                ErrorString{"some error"});

                            if (callback) {
                                callback->onFailedToExpungeNote(
                                    expungedNoteGuid, *exc);
                            }

                            downloadNotesStatus->m_noteGuidsWhichFailedToExpunge
                                << DownloadNotesStatus::GuidWithException{
                                       expungedNoteGuid, std::move(exc)};
                        }
                        else {
                            EXPECT_FALSE(true) << "Unreachable code";
                        }

                        ++noteIndex;
                    }
                }

                downloadNotesStatus->m_totalExpungedNotes = noteIndex;

                return threading::makeReadyFuture<DownloadNotesStatusPtr>(
                    std::move(downloadNotesStatus));
            });

        EXPECT_CALL(
            *m_mockResourcesProcessor, processResources(syncChunks, _, _))
            .WillOnce([&](const QList<qevercloud::SyncChunk> & chunks,
                          const utility::cancelers::ICancelerPtr & canceler,
                          const IDurableResourcesProcessor::ICallbackWeakPtr &
                              callbackWeak) {
                EXPECT_TRUE(canceler);

                const auto callback = callbackWeak.lock();
                EXPECT_TRUE(callback);

                auto downloadResourcesStatus =
                    std::make_shared<DownloadResourcesStatus>();

                int resourceIndex = 0;
                for (const auto & syncChunk: qAsConst(chunks)) {
                    if (!syncChunk.resources()) {
                        continue;
                    }

                    for (const auto & resource:
                         qAsConst(*syncChunk.resources())) {
                        if (resourceIndex % 4 == 0) {
                            const auto resourceGuid = resource.guid().value();
                            const auto resourceUsn =
                                resource.updateSequenceNum().value();

                            if (callback) {
                                callback->onProcessedResource(
                                    resourceGuid, resourceUsn);
                            }

                            downloadResourcesStatus
                                ->m_processedResourceGuidsAndUsns
                                    [resourceGuid] = resourceUsn;
                        }
                        else if (resourceIndex % 4 == 1) {
                            auto exc = std::make_shared<RuntimeError>(
                                ErrorString{"some error"});

                            if (callback) {
                                callback->onResourceFailedToDownload(
                                    resource, *exc);
                            }

                            downloadResourcesStatus
                                    ->m_resourcesWhichFailedToDownload
                                << DownloadResourcesStatus::
                                       ResourceWithException{
                                           resource, std::move(exc)};
                        }
                        else if (resourceIndex % 4 == 2) {
                            auto exc = std::make_shared<RuntimeError>(
                                ErrorString{"some error"});

                            if (callback) {
                                callback->onResourceFailedToProcess(
                                    resource, *exc);
                            }

                            downloadResourcesStatus
                                    ->m_resourcesWhichFailedToProcess
                                << DownloadResourcesStatus::
                                       ResourceWithException{
                                           resource, std::move(exc)};
                        }
                        else if (resourceIndex % 4 == 3) {
                            if (callback) {
                                callback->onResourceProcessingCancelled(
                                    resource);
                            }

                            const auto resourceGuid = resource.guid().value();
                            const auto resourceUsn =
                                resource.updateSequenceNum().value();

                            downloadResourcesStatus
                                ->m_cancelledResourceGuidsAndUsns
                                    [resourceGuid] = resourceUsn;
                        }
                        else {
                            EXPECT_FALSE(true) << "Unreachable code";
                        }

                        ++resourceIndex;
                    }
                }

                downloadResourcesStatus->m_totalNewResources =
                    resourceIndex / 2;

                downloadResourcesStatus->m_totalUpdatedResources =
                    resourceIndex -
                    downloadResourcesStatus->m_totalNewResources;

                return threading::makeReadyFuture<DownloadResourcesStatusPtr>(
                    std::move(downloadResourcesStatus));
            });

        EXPECT_CALL(*m_mockLocalStorage, listLinkedNotebooks)
            .WillOnce(Return(
                threading::makeReadyFuture<QList<qevercloud::LinkedNotebook>>(
                    {})));
    }

    auto result =
        downloader->download(m_manualCanceler, mockDownloaderCallback);
    ASSERT_TRUE(result.isFinished());

    // Checking the state of the last reported sync chunks data counters
    ASSERT_TRUE(lastSyncChunksDataCounters);

    // Saved searches

    const qint32 savedSearchCount = [&] {
        qint32 result = 0;
        for (const auto & syncChunk: qAsConst(syncChunks)) {
            if (syncChunk.searches()) {
                result += syncChunk.searches()->size();
            }
        }
        return result;
    }();

    EXPECT_EQ(
        lastSyncChunksDataCounters->totalSavedSearches(), savedSearchCount);

    EXPECT_EQ(
        lastSyncChunksDataCounters->addedSavedSearches() +
            lastSyncChunksDataCounters->updatedSavedSearches(),
        savedSearchCount);

    const qint32 expungedSavedSearchCount = [&] {
        qint32 result = 0;
        for (const auto & syncChunk: qAsConst(syncChunks)) {
            if (syncChunk.expungedSearches()) {
                result += syncChunk.expungedSearches()->size();
            }
        }
        return result;
    }();

    EXPECT_EQ(
        lastSyncChunksDataCounters->totalExpungedSavedSearches(),
        expungedSavedSearchCount);

    EXPECT_EQ(
        lastSyncChunksDataCounters->expungedSavedSearches(),
        expungedSavedSearchCount);

    // Tags

    const qint32 tagCount = [&] {
        qint32 result = 0;
        for (const auto & syncChunk: qAsConst(syncChunks)) {
            if (syncChunk.tags()) {
                result += syncChunk.tags()->size();
            }
        }
        return result;
    }();

    EXPECT_EQ(lastSyncChunksDataCounters->totalTags(), tagCount);

    EXPECT_EQ(
        lastSyncChunksDataCounters->addedTags() +
            lastSyncChunksDataCounters->updatedTags(),
        tagCount);

    const qint32 expungedTagCount = [&] {
        qint32 result = 0;
        for (const auto & syncChunk: qAsConst(syncChunks)) {
            if (syncChunk.expungedTags()) {
                result += syncChunk.expungedTags()->size();
            }
        }
        return result;
    }();

    EXPECT_EQ(
        lastSyncChunksDataCounters->totalExpungedTags(), expungedTagCount);

    EXPECT_EQ(lastSyncChunksDataCounters->expungedTags(), expungedTagCount);

    // Linked notebooks

    const qint32 linkedNotebookCount = [&] {
        qint32 result = 0;
        for (const auto & syncChunk: qAsConst(syncChunks)) {
            if (syncChunk.linkedNotebooks()) {
                result += syncChunk.linkedNotebooks()->size();
            }
        }
        return result;
    }();

    EXPECT_EQ(
        lastSyncChunksDataCounters->totalLinkedNotebooks(),
        linkedNotebookCount);

    EXPECT_EQ(
        lastSyncChunksDataCounters->addedLinkedNotebooks() +
            lastSyncChunksDataCounters->updatedLinkedNotebooks(),
        linkedNotebookCount);

    const qint32 expungedLinkedNotebookCount = [&] {
        qint32 result = 0;
        for (const auto & syncChunk: qAsConst(syncChunks)) {
            if (syncChunk.expungedLinkedNotebooks()) {
                result += syncChunk.expungedLinkedNotebooks()->size();
            }
        }
        return result;
    }();

    EXPECT_EQ(
        lastSyncChunksDataCounters->totalExpungedLinkedNotebooks(),
        expungedLinkedNotebookCount);

    EXPECT_EQ(
        lastSyncChunksDataCounters->expungedLinkedNotebooks(),
        expungedLinkedNotebookCount);

    // Notebooks

    const qint32 notebookCount = [&] {
        qint32 result = 0;
        for (const auto & syncChunk: qAsConst(syncChunks)) {
            if (syncChunk.notebooks()) {
                result += syncChunk.notebooks()->size();
            }
        }
        return result;
    }();

    EXPECT_EQ(lastSyncChunksDataCounters->totalNotebooks(), notebookCount);

    EXPECT_EQ(
        lastSyncChunksDataCounters->addedNotebooks() +
            lastSyncChunksDataCounters->updatedNotebooks(),
        notebookCount);

    const qint32 expungedNotebookCount = [&] {
        qint32 result = 0;
        for (const auto & syncChunk: qAsConst(syncChunks)) {
            if (syncChunk.expungedNotebooks()) {
                result += syncChunk.expungedNotebooks()->size();
            }
        }
        return result;
    }();

    EXPECT_EQ(
        lastSyncChunksDataCounters->totalExpungedNotebooks(),
        expungedNotebookCount);

    EXPECT_EQ(
        lastSyncChunksDataCounters->expungedNotebooks(), expungedNotebookCount);

    // Checking the returned status

    ASSERT_EQ(result.resultCount(), 1);
    const auto status = result.result();

    EXPECT_TRUE(status.linkedNotebookResults.isEmpty());

    // Checking sync chunk data counters (similar to the above but for
    // counters from callback)

    // Saved searches

    EXPECT_EQ(
        status.userOwnResult.syncChunksDataCounters->totalSavedSearches(),
        savedSearchCount);

    EXPECT_EQ(
        status.userOwnResult.syncChunksDataCounters->addedSavedSearches() +
            status.userOwnResult.syncChunksDataCounters->updatedSavedSearches(),
        savedSearchCount);

    EXPECT_EQ(
        status.userOwnResult.syncChunksDataCounters
            ->totalExpungedSavedSearches(),
        expungedSavedSearchCount);

    EXPECT_EQ(
        status.userOwnResult.syncChunksDataCounters->expungedSavedSearches(),
        expungedSavedSearchCount);

    // Tags

    EXPECT_EQ(
        status.userOwnResult.syncChunksDataCounters->totalTags(), tagCount);

    EXPECT_EQ(
        status.userOwnResult.syncChunksDataCounters->addedTags() +
            status.userOwnResult.syncChunksDataCounters->updatedTags(),
        tagCount);

    EXPECT_EQ(
        status.userOwnResult.syncChunksDataCounters->totalExpungedTags(),
        expungedTagCount);

    EXPECT_EQ(
        status.userOwnResult.syncChunksDataCounters->expungedTags(),
        expungedTagCount);

    // Linked notebooks

    EXPECT_EQ(
        status.userOwnResult.syncChunksDataCounters->totalLinkedNotebooks(),
        linkedNotebookCount);

    EXPECT_EQ(
        status.userOwnResult.syncChunksDataCounters->addedLinkedNotebooks() +
            status.userOwnResult.syncChunksDataCounters
                ->updatedLinkedNotebooks(),
        linkedNotebookCount);

    EXPECT_EQ(
        status.userOwnResult.syncChunksDataCounters
            ->totalExpungedLinkedNotebooks(),
        expungedLinkedNotebookCount);

    EXPECT_EQ(
        status.userOwnResult.syncChunksDataCounters->expungedLinkedNotebooks(),
        expungedLinkedNotebookCount);

    // Notebooks

    EXPECT_EQ(
        status.userOwnResult.syncChunksDataCounters->totalNotebooks(),
        notebookCount);

    EXPECT_EQ(
        status.userOwnResult.syncChunksDataCounters->addedNotebooks() +
            status.userOwnResult.syncChunksDataCounters->updatedNotebooks(),
        notebookCount);

    EXPECT_EQ(
        status.userOwnResult.syncChunksDataCounters->totalExpungedNotebooks(),
        expungedNotebookCount);

    EXPECT_EQ(
        status.userOwnResult.syncChunksDataCounters->expungedNotebooks(),
        expungedNotebookCount);

    // Notes

    EXPECT_EQ(lastTotalNotesToDownload, noteCount);

    EXPECT_EQ(
        status.userOwnResult.downloadNotesStatus->totalNewNotes() +
            status.userOwnResult.downloadNotesStatus->totalUpdatedNotes(),
        noteCount);

    const qint32 expungedNoteCount = [&] {
        qint32 result = 0;
        for (const auto & syncChunk: qAsConst(syncChunks)) {
            if (syncChunk.expungedNotes()) {
                result += syncChunk.expungedNotes()->size();
            }
        }
        return result;
    }();

    EXPECT_EQ(
        status.userOwnResult.downloadNotesStatus->totalExpungedNotes(),
        expungedNoteCount);

    EXPECT_EQ(
        status.userOwnResult.downloadNotesStatus->notesWhichFailedToDownload()
                .size() +
            status.userOwnResult.downloadNotesStatus
                ->notesWhichFailedToProcess()
                .size() +
            status.userOwnResult.downloadNotesStatus
                ->processedNoteGuidsAndUsns()
                .size() +
            status.userOwnResult.downloadNotesStatus
                ->cancelledNoteGuidsAndUsns()
                .size(),
        noteCount);

    EXPECT_EQ(
        status.userOwnResult.downloadNotesStatus->expungedNoteGuids().size() +
            status.userOwnResult.downloadNotesStatus
                ->noteGuidsWhichFailedToExpunge()
                .size(),
        expungedNoteCount);

    // Resources

    EXPECT_EQ(lastTotalResourcesToDownload, resourceCount);

    EXPECT_EQ(
        status.userOwnResult.downloadResourcesStatus->totalNewResources() +
            status.userOwnResult.downloadResourcesStatus
                ->totalUpdatedResources(),
        resourceCount);

    EXPECT_EQ(
        status.userOwnResult.downloadResourcesStatus
                ->resourcesWhichFailedToDownload()
                .size() +
            status.userOwnResult.downloadResourcesStatus
                ->resourcesWhichFailedToProcess()
                .size() +
            status.userOwnResult.downloadResourcesStatus
                ->processedResourceGuidsAndUsns()
                .size() +
            status.userOwnResult.downloadResourcesStatus
                ->cancelledResourceGuidsAndUsns()
                .size(),
        resourceCount);
}

} // namespace quentier::synchronization::tests
