/*
 * Copyright 2023 Dmitry Ivanov
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

#include <synchronization/AccountSynchronizer.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/RuntimeError.h>
#include <quentier/threading/Factory.h>
#include <quentier/threading/Future.h>
#include <quentier/utility/UidGenerator.h>
#include <quentier/utility/cancelers/ManualCanceler.h>

#include <synchronization/SyncChunksDataCounters.h>
#include <synchronization/tests/mocks/MockIAuthenticationInfoProvider.h>
#include <synchronization/tests/mocks/MockIDownloader.h>
#include <synchronization/tests/mocks/MockISender.h>
#include <synchronization/types/DownloadNotesStatus.h>
#include <synchronization/types/DownloadResourcesStatus.h>
#include <synchronization/types/SendStatus.h>

#include <qevercloud/types/builders/NoteBuilder.h>
#include <qevercloud/types/builders/NotebookBuilder.h>
#include <qevercloud/types/builders/ResourceBuilder.h>
#include <qevercloud/types/builders/SavedSearchBuilder.h>
#include <qevercloud/types/builders/TagBuilder.h>

namespace quentier::synchronization::tests {

namespace {

[[nodiscard]] SyncChunksDataCountersPtr generateSampleSyncChunksDataCounters(
    quint64 startValue = 1)
{
    auto counters = std::make_shared<SyncChunksDataCounters>();

    counters->m_totalSavedSearches = startValue++;
    counters->m_totalExpungedSavedSearches = startValue++;
    counters->m_addedSavedSearches = startValue++;
    counters->m_updatedSavedSearches = startValue++;
    counters->m_expungedSavedSearches = startValue++;

    counters->m_totalTags = startValue++;
    counters->m_totalExpungedTags = startValue++;
    counters->m_addedTags = startValue++;
    counters->m_updatedTags = startValue++;
    counters->m_expungedTags = startValue++;

    counters->m_totalLinkedNotebooks = startValue++;
    counters->m_totalExpungedLinkedNotebooks = startValue++;
    counters->m_addedLinkedNotebooks = startValue++;
    counters->m_updatedLinkedNotebooks = startValue++;
    counters->m_expungedLinkedNotebooks = startValue++;

    counters->m_totalNotebooks = startValue++;
    counters->m_totalExpungedNotebooks = startValue++;
    counters->m_addedNotebooks = startValue++;
    counters->m_updatedNotebooks = startValue++;
    counters->m_expungedNotebooks = startValue++;

    return counters;
}

[[nodiscard]] DownloadNotesStatusPtr generateSampleDownloadNotesStatus(
    quint64 startValue)
{
    auto status = std::make_shared<DownloadNotesStatus>();
    status->m_totalNewNotes = startValue++;
    status->m_totalUpdatedNotes = startValue++;
    status->m_totalExpungedNotes = startValue++;

    constexpr int count = 3;
    for (int i = 0; i < count; ++i) {
        status->m_notesWhichFailedToDownload
            << IDownloadNotesStatus::NoteWithException{
                   qevercloud::NoteBuilder{}
                       .setLocalId(UidGenerator::Generate())
                       .setGuid(UidGenerator::Generate())
                       .setTitle(
                           QString::fromUtf8("Note failed to download #%1")
                               .arg(startValue++))
                       .setUpdateSequenceNum(startValue++)
                       .setNotebookGuid(UidGenerator::Generate())
                       .setNotebookLocalId(UidGenerator::Generate())
                       .build(),
                   std::make_shared<RuntimeError>(
                       ErrorString{QStringLiteral("some error")})};
    }

    for (int i = 0; i < count; ++i) {
        status->m_notesWhichFailedToProcess
            << IDownloadNotesStatus::NoteWithException{
                   qevercloud::NoteBuilder{}
                       .setLocalId(UidGenerator::Generate())
                       .setGuid(UidGenerator::Generate())
                       .setTitle(QString::fromUtf8("Note failed to process #%1")
                                     .arg(startValue++))
                       .setUpdateSequenceNum(startValue++)
                       .setNotebookGuid(UidGenerator::Generate())
                       .setNotebookLocalId(UidGenerator::Generate())
                       .build(),
                   std::make_shared<RuntimeError>(
                       ErrorString{QStringLiteral("some error")})};
    }

    for (int i = 0; i < count; ++i) {
        status->m_noteGuidsWhichFailedToExpunge
            << IDownloadNotesStatus::GuidWithException{
                   UidGenerator::Generate(),
                   std::make_shared<RuntimeError>(
                       ErrorString{QStringLiteral("some error")})};
    }

    for (int i = 0; i < count; ++i) {
        status->m_processedNoteGuidsAndUsns[UidGenerator::Generate()] =
            static_cast<qint32>(startValue++);
    }

    for (int i = 0; i < count; ++i) {
        status->m_cancelledNoteGuidsAndUsns[UidGenerator::Generate()] =
            static_cast<qint32>(startValue++);
    }

    for (int i = 0; i < count; ++i) {
        status->m_expungedNoteGuids << UidGenerator::Generate();
    }

    return status;
}

[[nodiscard]] DownloadResourcesStatusPtr generateSampleDownloadResourcesStatus(
    quint64 startValue)
{
    auto status = std::make_shared<DownloadResourcesStatus>();
    status->m_totalNewResources = startValue++;
    status->m_totalUpdatedResources = startValue++;

    constexpr int count = 3;
    for (int i = 0; i < count; ++i) {
        status->m_resourcesWhichFailedToDownload
            << IDownloadResourcesStatus::ResourceWithException{
                   qevercloud::ResourceBuilder{}
                       .setLocalId(UidGenerator::Generate())
                       .setGuid(UidGenerator::Generate())
                       .setUpdateSequenceNum(startValue++)
                       .setNoteGuid(UidGenerator::Generate())
                       .setNoteLocalId(UidGenerator::Generate())
                       .build(),
                   std::make_shared<RuntimeError>(
                       ErrorString{QStringLiteral("some error")})};
    }

    for (int i = 0; i < count; ++i) {
        status->m_resourcesWhichFailedToProcess
            << IDownloadResourcesStatus::ResourceWithException{
                   qevercloud::ResourceBuilder{}
                       .setLocalId(UidGenerator::Generate())
                       .setGuid(UidGenerator::Generate())
                       .setUpdateSequenceNum(startValue++)
                       .setNoteGuid(UidGenerator::Generate())
                       .setNoteLocalId(UidGenerator::Generate())
                       .build(),
                   std::make_shared<RuntimeError>(
                       ErrorString{QStringLiteral("some error")})};
    }

    for (int i = 0; i < count; ++i) {
        status->m_processedResourceGuidsAndUsns[UidGenerator::Generate()] =
            static_cast<qint32>(startValue++);
    }

    for (int i = 0; i < count; ++i) {
        status->m_cancelledResourceGuidsAndUsns[UidGenerator::Generate()] =
            static_cast<qint32>(startValue++);
    }

    return status;
}

[[nodiscard]] SendStatusPtr generateSampleSendStatus(quint64 startValue)
{
    auto status = std::make_shared<SendStatus>();

    status->m_totalAttemptedToSendNotes = startValue++;
    status->m_totalAttemptedToSendNotebooks = startValue++;
    status->m_totalAttemptedToSendSavedSearches = startValue++;
    status->m_totalAttemptedToSendTags = startValue++;

    status->m_totalSuccessfullySentNotes = startValue++;

    constexpr int count = 3;
    for (int i = 0; i < count; ++i) {
        status->m_failedToSendNotes << ISendStatus::NoteWithException{
            qevercloud::NoteBuilder{}
                .setLocalId(UidGenerator::Generate())
                .setGuid(UidGenerator::Generate())
                .setTitle(QString::fromUtf8("Note failed to send #%1")
                              .arg(startValue++))
                .setUpdateSequenceNum(startValue++)
                .setNotebookGuid(UidGenerator::Generate())
                .setNotebookLocalId(UidGenerator::Generate())
                .build(),
            std::make_shared<RuntimeError>(
                ErrorString{QStringLiteral("some error")})};
    }

    status->m_totalSuccessfullySentNotebooks = startValue++;
    for (int i = 0; i < count; ++i) {
        status->m_failedToSendNotebooks << ISendStatus::NotebookWithException{
            qevercloud::NotebookBuilder{}
                .setLocalId(UidGenerator::Generate())
                .setGuid(UidGenerator::Generate())
                .setName(QString::fromUtf8("Notebook failed to send #%1")
                             .arg(startValue++))
                .setUpdateSequenceNum(startValue++)
                .build(),
            std::make_shared<RuntimeError>(
                ErrorString{QStringLiteral("some error")})};
    }

    status->m_totalSuccessfullySentSavedSearches = startValue++;
    for (int i = 0; i < count; ++i) {
        status->m_failedToSendSavedSearches
            << ISendStatus::SavedSearchWithException{
                   qevercloud::SavedSearchBuilder{}
                       .setLocalId(UidGenerator::Generate())
                       .setGuid(UidGenerator::Generate())
                       .setName(
                           QString::fromUtf8("SavedSearch failed to send #%1")
                               .arg(startValue++))
                       .setUpdateSequenceNum(startValue++)
                       .build(),
                   std::make_shared<RuntimeError>(
                       ErrorString{QStringLiteral("some error")})};
    }

    status->m_totalSuccessfullySentTags = startValue++;
    for (int i = 0; i < count; ++i) {
        status->m_failedToSendTags << ISendStatus::TagWithException{
            qevercloud::TagBuilder{}
                .setLocalId(UidGenerator::Generate())
                .setGuid(UidGenerator::Generate())
                .setName(QString::fromUtf8("Tag failed to send #%1")
                             .arg(startValue++))
                .setUpdateSequenceNum(startValue++)
                .build(),
            std::make_shared<RuntimeError>(
                ErrorString{QStringLiteral("some error")})};
    }

    return status;
}

} // namespace

using testing::Return;
using testing::StrictMock;

class AccountSynchronizerTest : public testing::Test
{
protected:
    const Account m_account = Account{
        QStringLiteral("Full Name"),
        Account::Type::Evernote,
        qevercloud::UserID{42},
        Account::EvernoteAccountType::Free,
        QStringLiteral("www.evernote.com"),
        QStringLiteral("shard id")};

    const std::shared_ptr<mocks::MockIDownloader> m_mockDownloader =
        std::make_shared<StrictMock<mocks::MockIDownloader>>();

    const std::shared_ptr<mocks::MockISender> m_mockSender =
        std::make_shared<StrictMock<mocks::MockISender>>();

    const std::shared_ptr<mocks::MockIAuthenticationInfoProvider>
        m_mockAuthenticationInfoProvider = std::make_shared<
            StrictMock<mocks::MockIAuthenticationInfoProvider>>();

    const threading::QThreadPoolPtr m_threadPool =
        threading::globalThreadPool();
};

TEST_F(AccountSynchronizerTest, Ctor)
{
    EXPECT_NO_THROW(
        const auto accountSynchronizer = std::make_shared<AccountSynchronizer>(
            m_account, m_mockDownloader, m_mockSender,
            m_mockAuthenticationInfoProvider, m_threadPool));
}

TEST_F(AccountSynchronizerTest, CtorEmptyAccount)
{
    EXPECT_THROW(
        const auto accountSynchronizer = std::make_shared<AccountSynchronizer>(
            Account{}, m_mockDownloader, m_mockSender,
            m_mockAuthenticationInfoProvider, m_threadPool),
        InvalidArgument);
}

TEST_F(AccountSynchronizerTest, CtorNullDownloader)
{
    EXPECT_THROW(
        const auto accountSynchronizer = std::make_shared<AccountSynchronizer>(
            m_account, nullptr, m_mockSender, m_mockAuthenticationInfoProvider,
            m_threadPool),
        InvalidArgument);
}

TEST_F(AccountSynchronizerTest, CtorNullSender)
{
    EXPECT_THROW(
        const auto accountSynchronizer = std::make_shared<AccountSynchronizer>(
            m_account, m_mockDownloader, nullptr,
            m_mockAuthenticationInfoProvider, m_threadPool),
        InvalidArgument);
}

TEST_F(AccountSynchronizerTest, CtorNullAuthenticationInfoProvider)
{
    EXPECT_THROW(
        const auto accountSynchronizer = std::make_shared<AccountSynchronizer>(
            m_account, m_mockDownloader, m_mockSender, nullptr, m_threadPool),
        InvalidArgument);
}

TEST_F(AccountSynchronizerTest, CtorNullThreadPool)
{
    EXPECT_NO_THROW(
        const auto accountSynchronizer = std::make_shared<AccountSynchronizer>(
            m_account, m_mockDownloader, m_mockSender,
            m_mockAuthenticationInfoProvider, nullptr));
}

TEST_F(AccountSynchronizerTest, DownloadAndSend)
{
    const auto accountSynchronizer = std::make_shared<AccountSynchronizer>(
        m_account, m_mockDownloader, m_mockSender,
        m_mockAuthenticationInfoProvider, m_threadPool);

    IDownloader::Result downloadResult;
    downloadResult.userOwnResult.syncChunksDataCounters =
        generateSampleSyncChunksDataCounters(1);
    downloadResult.userOwnResult.downloadNotesStatus =
        generateSampleDownloadNotesStatus(1);
    downloadResult.userOwnResult.downloadResourcesStatus =
        generateSampleDownloadResourcesStatus(1);

    constexpr quint64 count = 3;
    for (quint64 i = 0; i < count; ++i) {
        auto & result =
            downloadResult.linkedNotebookResults[UidGenerator::Generate()];

        result.syncChunksDataCounters =
            generateSampleSyncChunksDataCounters(i + 1);

        result.downloadNotesStatus = generateSampleDownloadNotesStatus(i + 1);

        result.downloadResourcesStatus =
            generateSampleDownloadResourcesStatus(i + 1);
    }

    ISender::Result sendResult;
    sendResult.userOwnResult = generateSampleSendStatus(1);
    for (quint64 i = 0; i < count; ++i) {
        sendResult.linkedNotebookResults[UidGenerator::Generate()] =
            generateSampleSendStatus(i + 1);
    }

    EXPECT_CALL(*m_mockDownloader, download)
        .WillOnce(Return(threading::makeReadyFuture(downloadResult)));

    EXPECT_CALL(*m_mockSender, send)
        .WillOnce(Return(threading::makeReadyFuture(sendResult)));

    // FIXME: need a mock of the callback here
    auto callback = std::shared_ptr<IAccountSynchronizer::ICallback>();
    auto canceler = std::make_shared<utility::cancelers::ManualCanceler>();

    auto syncResult = accountSynchronizer->synchronize(callback, canceler);
    syncResult.waitForFinished();

    // TODO: check that the result is as expected
}

} // namespace quentier::synchronization::tests
