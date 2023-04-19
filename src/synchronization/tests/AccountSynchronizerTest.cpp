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
#include <quentier/synchronization/tests/mocks/MockISyncStateStorage.h>
#include <quentier/synchronization/types/ISyncResult.h>
#include <quentier/threading/Factory.h>
#include <quentier/threading/Future.h>
#include <quentier/utility/UidGenerator.h>
#include <quentier/utility/cancelers/ManualCanceler.h>

#include <synchronization/SyncChunksDataCounters.h>
#include <synchronization/tests/mocks/MockIAccountSynchronizer.h>
#include <synchronization/tests/mocks/MockIAuthenticationInfoProvider.h>
#include <synchronization/tests/mocks/MockIDownloader.h>
#include <synchronization/tests/mocks/MockISender.h>
#include <synchronization/types/DownloadNotesStatus.h>
#include <synchronization/types/DownloadResourcesStatus.h>
#include <synchronization/types/SendStatus.h>
#include <synchronization/types/SyncState.h>

#include <qevercloud/types/builders/NoteBuilder.h>
#include <qevercloud/types/builders/NotebookBuilder.h>
#include <qevercloud/types/builders/ResourceBuilder.h>
#include <qevercloud/types/builders/SavedSearchBuilder.h>
#include <qevercloud/types/builders/TagBuilder.h>

#include <QDateTime>

// clazy:excludeall=non-pod-global-static
// clazy:excludeall=returning-void-expression

namespace quentier::synchronization::tests {

using testing::InSequence;
using testing::Return;
using testing::StrictMock;

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
                               .arg(startValue + 1))
                       .setUpdateSequenceNum(startValue + 2)
                       .setNotebookGuid(UidGenerator::Generate())
                       .setNotebookLocalId(UidGenerator::Generate())
                       .build(),
                   std::make_shared<RuntimeError>(
                       ErrorString{QStringLiteral("some error")})};
        startValue += 2;
    }

    for (int i = 0; i < count; ++i) {
        status->m_notesWhichFailedToProcess
            << IDownloadNotesStatus::NoteWithException{
                   qevercloud::NoteBuilder{}
                       .setLocalId(UidGenerator::Generate())
                       .setGuid(UidGenerator::Generate())
                       .setTitle(QString::fromUtf8("Note failed to process #%1")
                                     .arg(startValue + 1))
                       .setUpdateSequenceNum(startValue + 2)
                       .setNotebookGuid(UidGenerator::Generate())
                       .setNotebookLocalId(UidGenerator::Generate())
                       .build(),
                   std::make_shared<RuntimeError>(
                       ErrorString{QStringLiteral("some error")})};
        startValue += 2;
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
                              .arg(startValue + 1))
                .setUpdateSequenceNum(startValue + 2)
                .setNotebookGuid(UidGenerator::Generate())
                .setNotebookLocalId(UidGenerator::Generate())
                .build(),
            std::make_shared<RuntimeError>(
                ErrorString{QStringLiteral("some error")})};
        startValue += 2;
    }

    status->m_totalSuccessfullySentNotebooks = startValue++;
    for (int i = 0; i < count; ++i) {
        status->m_failedToSendNotebooks << ISendStatus::NotebookWithException{
            qevercloud::NotebookBuilder{}
                .setLocalId(UidGenerator::Generate())
                .setGuid(UidGenerator::Generate())
                .setName(QString::fromUtf8("Notebook failed to send #%1")
                             .arg(startValue + 1))
                .setUpdateSequenceNum(startValue + 2)
                .build(),
            std::make_shared<RuntimeError>(
                ErrorString{QStringLiteral("some error")})};
        startValue += 2;
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
                               .arg(startValue + 1))
                       .setUpdateSequenceNum(startValue + 2)
                       .build(),
                   std::make_shared<RuntimeError>(
                       ErrorString{QStringLiteral("some error")})};
        startValue += 2;
    }

    status->m_totalSuccessfullySentTags = startValue++;
    for (int i = 0; i < count; ++i) {
        status->m_failedToSendTags << ISendStatus::TagWithException{
            qevercloud::TagBuilder{}
                .setLocalId(UidGenerator::Generate())
                .setGuid(UidGenerator::Generate())
                .setName(QString::fromUtf8("Tag failed to send #%1")
                             .arg(startValue + 1))
                .setUpdateSequenceNum(startValue + 2)
                .build(),
            std::make_shared<RuntimeError>(
                ErrorString{QStringLiteral("some error")})};
        startValue += 2;
    }

    return status;
}

} // namespace

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

    const std::shared_ptr<mocks::MockISyncStateStorage> m_mockSyncStateStorage =
        std::make_shared<StrictMock<mocks::MockISyncStateStorage>>();

    const threading::QThreadPoolPtr m_threadPool =
        threading::globalThreadPool();
};

TEST_F(AccountSynchronizerTest, Ctor)
{
    EXPECT_NO_THROW(
        const auto accountSynchronizer = std::make_shared<AccountSynchronizer>(
            m_account, m_mockDownloader, m_mockSender,
            m_mockAuthenticationInfoProvider, m_mockSyncStateStorage,
            m_threadPool));
}

TEST_F(AccountSynchronizerTest, CtorEmptyAccount)
{
    EXPECT_THROW(
        const auto accountSynchronizer = std::make_shared<AccountSynchronizer>(
            Account{}, m_mockDownloader, m_mockSender,
            m_mockAuthenticationInfoProvider, m_mockSyncStateStorage,
            m_threadPool),
        InvalidArgument);
}

TEST_F(AccountSynchronizerTest, CtorNullDownloader)
{
    EXPECT_THROW(
        const auto accountSynchronizer = std::make_shared<AccountSynchronizer>(
            m_account, nullptr, m_mockSender, m_mockAuthenticationInfoProvider,
            m_mockSyncStateStorage, m_threadPool),
        InvalidArgument);
}

TEST_F(AccountSynchronizerTest, CtorNullSender)
{
    EXPECT_THROW(
        const auto accountSynchronizer = std::make_shared<AccountSynchronizer>(
            m_account, m_mockDownloader, nullptr,
            m_mockAuthenticationInfoProvider, m_mockSyncStateStorage,
            m_threadPool),
        InvalidArgument);
}

TEST_F(AccountSynchronizerTest, CtorNullAuthenticationInfoProvider)
{
    EXPECT_THROW(
        const auto accountSynchronizer = std::make_shared<AccountSynchronizer>(
            m_account, m_mockDownloader, m_mockSender, nullptr,
            m_mockSyncStateStorage, m_threadPool),
        InvalidArgument);
}

TEST_F(AccountSynchronizerTest, CtorNullSyncStateStorage)
{
    EXPECT_THROW(
        const auto accountSynchronizer = std::make_shared<AccountSynchronizer>(
            m_account, m_mockDownloader, m_mockSender,
            m_mockAuthenticationInfoProvider, nullptr, m_threadPool),
        InvalidArgument);
}

TEST_F(AccountSynchronizerTest, CtorNullThreadPool)
{
    EXPECT_NO_THROW(
        const auto accountSynchronizer = std::make_shared<AccountSynchronizer>(
            m_account, m_mockDownloader, m_mockSender,
            m_mockAuthenticationInfoProvider, m_mockSyncStateStorage, nullptr));
}

TEST_F(AccountSynchronizerTest, DownloadAndSend)
{
    const auto accountSynchronizer = std::make_shared<AccountSynchronizer>(
        m_account, m_mockDownloader, m_mockSender,
        m_mockAuthenticationInfoProvider, m_mockSyncStateStorage, m_threadPool);

    const int linkedNotebookCount = 3;
    QList<qevercloud::Guid> linkedNotebookGuids;
    linkedNotebookGuids.reserve(linkedNotebookCount);
    for (int i = 0; i < linkedNotebookCount; ++i) {
        linkedNotebookGuids << UidGenerator::Generate();
    }

    IDownloader::Result downloadResult;
    downloadResult.userOwnResult.syncChunksDataCounters =
        generateSampleSyncChunksDataCounters(1);
    downloadResult.userOwnResult.downloadNotesStatus =
        generateSampleDownloadNotesStatus(1);
    downloadResult.userOwnResult.downloadResourcesStatus =
        generateSampleDownloadResourcesStatus(1);

    qint32 counter = 1;
    for (const auto & linkedNotebookGuid: qAsConst(linkedNotebookGuids)) {
        auto & result =
            downloadResult.linkedNotebookResults[linkedNotebookGuid];

        result.syncChunksDataCounters = generateSampleSyncChunksDataCounters(
            3 + static_cast<quint64>(counter) * 2);

        result.downloadNotesStatus = generateSampleDownloadNotesStatus(
            5 + static_cast<quint64>(counter) * 3);

        result.downloadResourcesStatus = generateSampleDownloadResourcesStatus(
            8 + static_cast<quint64>(counter) * 4);

        ++counter;
    }

    const auto now = QDateTime::currentMSecsSinceEpoch();

    auto downloadSyncState = std::make_shared<SyncState>();
    downloadSyncState->m_userDataUpdateCount = 42;
    downloadSyncState->m_userDataLastSyncTime = now;

    counter = 1;
    for (const auto & linkedNotebookGuid: qAsConst(linkedNotebookGuids)) {
        downloadSyncState->m_linkedNotebookUpdateCounts[linkedNotebookGuid] =
            84 + counter * 2;

        downloadSyncState->m_linkedNotebookLastSyncTimes[linkedNotebookGuid] =
            now + counter;

        ++counter;
    }

    downloadResult.syncState = downloadSyncState;

    ISender::Result sendResult;
    sendResult.userOwnResult = generateSampleSendStatus(1);

    counter = 1;
    for (const auto & linkedNotebookGuid: qAsConst(linkedNotebookGuids)) {
        sendResult.linkedNotebookResults[linkedNotebookGuid] =
            generateSampleSendStatus(static_cast<quint64>(counter) * 5);
    }

    auto sendSyncState = std::make_shared<SyncState>();
    sendSyncState->m_userDataUpdateCount = 43;
    sendSyncState->m_userDataLastSyncTime = now + 1;

    counter = 1;
    for (const auto & linkedNotebookGuid: qAsConst(linkedNotebookGuids)) {
        sendSyncState->m_linkedNotebookUpdateCounts[linkedNotebookGuid] =
            120 + counter * 3;

        sendSyncState->m_linkedNotebookLastSyncTimes[linkedNotebookGuid] =
            now + counter * 2L;

        ++counter;
    }

    sendResult.syncState = sendSyncState;

    InSequence s;

    EXPECT_CALL(*m_mockDownloader, download)
        .WillOnce(Return(threading::makeReadyFuture(downloadResult)));

    EXPECT_CALL(*m_mockSyncStateStorage, setSyncState)
        .WillOnce(
            [&, this](const Account & account, const ISyncStatePtr & state) {
                EXPECT_EQ(account, m_account);
                EXPECT_TRUE(state);
                if (state) {
                    EXPECT_EQ(
                        state->userDataUpdateCount(),
                        downloadSyncState->userDataUpdateCount());

                    EXPECT_EQ(
                        state->userDataLastSyncTime(),
                        downloadSyncState->userDataLastSyncTime());
                }
            });

    EXPECT_CALL(*m_mockSender, send)
        .WillOnce(Return(threading::makeReadyFuture(sendResult)));

    EXPECT_CALL(*m_mockSyncStateStorage, setSyncState)
        .WillOnce(
            [&, this](const Account & account, const ISyncStatePtr & state) {
                EXPECT_EQ(account, m_account);
                EXPECT_TRUE(state);
                if (state) {
                    EXPECT_EQ(
                        state->userDataUpdateCount(),
                        sendSyncState->userDataUpdateCount());

                    EXPECT_EQ(
                        state->userDataLastSyncTime(),
                        sendSyncState->userDataLastSyncTime());
                }
            });

    const std::shared_ptr<mocks::MockIAccountSynchronizerCallback>
        mockCallback = std::make_shared<
            StrictMock<mocks::MockIAccountSynchronizerCallback>>();

    const auto canceler =
        std::make_shared<utility::cancelers::ManualCanceler>();

    auto syncResult = accountSynchronizer->synchronize(mockCallback, canceler);
    syncResult.waitForFinished();

    ASSERT_EQ(syncResult.resultCount(), 1);
    auto result = syncResult.result();

    // Checking the result

    ASSERT_TRUE(result);

    // Checking sync state
    const auto resultSyncState = result->syncState();
    ASSERT_TRUE(resultSyncState);

    EXPECT_EQ(
        resultSyncState->userDataUpdateCount(),
        sendSyncState->userDataUpdateCount());

    EXPECT_EQ(
        resultSyncState->userDataLastSyncTime(),
        sendSyncState->userDataLastSyncTime());

    const auto resultLinkedNotebookLastSyncTimes =
        resultSyncState->linkedNotebookLastSyncTimes();

    ASSERT_EQ(
        resultLinkedNotebookLastSyncTimes.size(), linkedNotebookGuids.size());

    for (const auto & linkedNotebookGuid: qAsConst(linkedNotebookGuids)) {
        const auto it =
            resultLinkedNotebookLastSyncTimes.constFind(linkedNotebookGuid);
        ASSERT_NE(it, resultLinkedNotebookLastSyncTimes.constEnd());

        const auto rit = sendSyncState->m_linkedNotebookLastSyncTimes.constFind(
            linkedNotebookGuid);
        ASSERT_NE(rit, sendSyncState->m_linkedNotebookLastSyncTimes.constEnd());

        EXPECT_EQ(it.value(), rit.value());
    }

    const auto resultLinkedNotebookUpdateCounts =
        resultSyncState->linkedNotebookUpdateCounts();

    ASSERT_EQ(
        resultLinkedNotebookUpdateCounts.size(), linkedNotebookGuids.size());

    for (const auto & linkedNotebookGuid: qAsConst(linkedNotebookGuids)) {
        const auto it =
            resultLinkedNotebookUpdateCounts.constFind(linkedNotebookGuid);
        ASSERT_NE(it, resultLinkedNotebookUpdateCounts.constEnd());

        const auto rit = sendSyncState->m_linkedNotebookUpdateCounts.constFind(
            linkedNotebookGuid);
        ASSERT_NE(rit, sendSyncState->m_linkedNotebookUpdateCounts.constEnd());

        EXPECT_EQ(it.value(), rit.value());
    }

    // Checking sync chunks data counters
    const auto resultSyncChunksDataCounters =
        result->userAccountSyncChunksDataCounters();
    ASSERT_TRUE(resultSyncChunksDataCounters);

    EXPECT_EQ(
        resultSyncChunksDataCounters,
        downloadResult.userOwnResult.syncChunksDataCounters);

    const auto resultLinkedNotebookSyncChunksDataCounters =
        result->linkedNotebookSyncChunksDataCounters();

    ASSERT_EQ(
        resultLinkedNotebookSyncChunksDataCounters.size(),
        downloadResult.linkedNotebookResults.size());

    for (const auto & linkedNotebookGuid: qAsConst(linkedNotebookGuids)) {
        const auto it = resultLinkedNotebookSyncChunksDataCounters.constFind(
            linkedNotebookGuid);
        ASSERT_NE(it, resultLinkedNotebookSyncChunksDataCounters.constEnd());

        const auto rit =
            downloadResult.linkedNotebookResults.constFind(linkedNotebookGuid);
        ASSERT_NE(rit, downloadResult.linkedNotebookResults.constEnd());

        EXPECT_EQ(it.value(), rit.value().syncChunksDataCounters);
    }

    // Checking download notes status
    const auto resultDownloadNotesStatus =
        result->userAccountDownloadNotesStatus();
    ASSERT_TRUE(resultDownloadNotesStatus);

    EXPECT_EQ(
        resultDownloadNotesStatus,
        downloadResult.userOwnResult.downloadNotesStatus);

    const auto resultLinkedNotebookDownloadNotesStatuses =
        result->linkedNotebookDownloadNotesStatuses();
    ASSERT_EQ(
        resultLinkedNotebookDownloadNotesStatuses.size(),
        linkedNotebookGuids.size());

    for (const auto & linkedNotebookGuid: qAsConst(linkedNotebookGuids)) {
        const auto it = resultLinkedNotebookDownloadNotesStatuses.constFind(
            linkedNotebookGuid);
        ASSERT_NE(it, resultLinkedNotebookDownloadNotesStatuses.constEnd());

        const auto rit =
            downloadResult.linkedNotebookResults.constFind(linkedNotebookGuid);
        ASSERT_NE(rit, downloadResult.linkedNotebookResults.constEnd());

        EXPECT_EQ(it.value(), rit.value().downloadNotesStatus);
    }

    // Checking download resources status
    const auto resultDownloadResourcesStatus =
        result->userAccountDownloadResourcesStatus();
    ASSERT_TRUE(resultDownloadResourcesStatus);

    EXPECT_EQ(
        resultDownloadResourcesStatus,
        downloadResult.userOwnResult.downloadResourcesStatus);

    const auto resultLinkedNotebookDownloadResourcesStatuses =
        result->linkedNotebookDownloadResourcesStatuses();
    ASSERT_EQ(
        resultLinkedNotebookDownloadResourcesStatuses.size(),
        linkedNotebookGuids.size());

    for (const auto & linkedNotebookGuid: qAsConst(linkedNotebookGuids)) {
        const auto it = resultLinkedNotebookDownloadResourcesStatuses.constFind(
            linkedNotebookGuid);
        ASSERT_NE(it, resultLinkedNotebookDownloadResourcesStatuses.constEnd());

        const auto rit =
            downloadResult.linkedNotebookResults.constFind(linkedNotebookGuid);
        ASSERT_NE(rit, downloadResult.linkedNotebookResults.constEnd());

        EXPECT_EQ(it.value(), rit.value().downloadResourcesStatus);
    }

    // Checking send status
    const auto resultSendStatus = result->userAccountSendStatus();
    ASSERT_TRUE(resultSendStatus);

    EXPECT_EQ(resultSendStatus, sendResult.userOwnResult);

    const auto resultLinkedNotebookSendStatuses =
        result->linkedNotebookSendStatuses();
    ASSERT_EQ(
        resultLinkedNotebookSendStatuses.size(), linkedNotebookGuids.size());

    for (const auto & linkedNotebookGuid: qAsConst(linkedNotebookGuids)) {
        const auto it =
            resultLinkedNotebookSendStatuses.constFind(linkedNotebookGuid);
        ASSERT_NE(it, resultLinkedNotebookSendStatuses.constEnd());

        const auto rit =
            sendResult.linkedNotebookResults.constFind(linkedNotebookGuid);
        ASSERT_NE(rit, sendResult.linkedNotebookResults.constEnd());

        EXPECT_EQ(it.value(), rit.value());
    }
}

TEST_F(
    AccountSynchronizerTest,
    DownloadSendAndDownloadAgainIfRequiredForUserOwnAccount)
{
    const auto accountSynchronizer = std::make_shared<AccountSynchronizer>(
        m_account, m_mockDownloader, m_mockSender,
        m_mockAuthenticationInfoProvider, m_mockSyncStateStorage, m_threadPool);

    const int linkedNotebookCount = 3;
    QList<qevercloud::Guid> linkedNotebookGuids;
    linkedNotebookGuids.reserve(linkedNotebookCount);
    for (int i = 0; i < linkedNotebookCount; ++i) {
        linkedNotebookGuids << UidGenerator::Generate();
    }

    IDownloader::Result downloadResult;
    downloadResult.userOwnResult.syncChunksDataCounters =
        generateSampleSyncChunksDataCounters(1);
    downloadResult.userOwnResult.downloadNotesStatus =
        generateSampleDownloadNotesStatus(1);
    downloadResult.userOwnResult.downloadResourcesStatus =
        generateSampleDownloadResourcesStatus(1);

    qint32 counter = 1;
    for (const auto & linkedNotebookGuid: qAsConst(linkedNotebookGuids)) {
        auto & result =
            downloadResult.linkedNotebookResults[linkedNotebookGuid];

        result.syncChunksDataCounters = generateSampleSyncChunksDataCounters(
            3 + static_cast<quint64>(counter) * 2);

        result.downloadNotesStatus = generateSampleDownloadNotesStatus(
            5 + static_cast<quint64>(counter) * 3);

        result.downloadResourcesStatus = generateSampleDownloadResourcesStatus(
            8 + static_cast<quint64>(counter) * 4);

        ++counter;
    }

    const auto now = QDateTime::currentMSecsSinceEpoch();

    auto downloadSyncState = std::make_shared<SyncState>();
    downloadSyncState->m_userDataUpdateCount = 42;
    downloadSyncState->m_userDataLastSyncTime = now;

    counter = 1;
    for (const auto & linkedNotebookGuid: qAsConst(linkedNotebookGuids)) {
        downloadSyncState->m_linkedNotebookUpdateCounts[linkedNotebookGuid] =
            84 + counter * 2;

        downloadSyncState->m_linkedNotebookLastSyncTimes[linkedNotebookGuid] =
            now + counter;

        ++counter;
    }

    downloadResult.syncState = downloadSyncState;

    ISender::Result sendResult;
    sendResult.userOwnResult = generateSampleSendStatus(1);
    sendResult.userOwnResult->m_needToRepeatIncrementalSync = true;

    counter = 1;
    for (const auto & linkedNotebookGuid: qAsConst(linkedNotebookGuids)) {
        sendResult.linkedNotebookResults[linkedNotebookGuid] =
            generateSampleSendStatus(static_cast<quint64>(counter) * 5);
    }

    auto sendSyncState = std::make_shared<SyncState>();
    sendSyncState->m_userDataUpdateCount = 43;
    sendSyncState->m_userDataLastSyncTime = now + 1;

    counter = 1;
    for (const auto & linkedNotebookGuid: qAsConst(linkedNotebookGuids)) {
        sendSyncState->m_linkedNotebookUpdateCounts[linkedNotebookGuid] =
            120 + counter * 3;

        sendSyncState->m_linkedNotebookLastSyncTimes[linkedNotebookGuid] =
            now + counter * 2L;

        ++counter;
    }

    sendResult.syncState = sendSyncState;

    InSequence s;

    EXPECT_CALL(*m_mockDownloader, download)
        .WillOnce(Return(threading::makeReadyFuture(downloadResult)));

    EXPECT_CALL(*m_mockSyncStateStorage, setSyncState)
        .WillOnce(
            [&, this](const Account & account, const ISyncStatePtr & state) {
                EXPECT_EQ(account, m_account);
                EXPECT_TRUE(state);
                if (state) {
                    EXPECT_EQ(
                        state->userDataUpdateCount(),
                        downloadSyncState->userDataUpdateCount());

                    EXPECT_EQ(
                        state->userDataLastSyncTime(),
                        downloadSyncState->userDataLastSyncTime());

                    EXPECT_EQ(
                        state->linkedNotebookUpdateCounts(),
                        downloadSyncState->linkedNotebookUpdateCounts());

                    EXPECT_EQ(
                        state->linkedNotebookLastSyncTimes(),
                        downloadSyncState->linkedNotebookLastSyncTimes());
                }
            });

    EXPECT_CALL(*m_mockSender, send)
        .WillOnce(Return(threading::makeReadyFuture(sendResult)));

    EXPECT_CALL(*m_mockSyncStateStorage, setSyncState)
        .WillOnce(
            [&, this](const Account & account, const ISyncStatePtr & state) {
                EXPECT_EQ(account, m_account);
                EXPECT_TRUE(state);
                if (state) {
                    EXPECT_EQ(
                        state->userDataUpdateCount(),
                        sendSyncState->userDataUpdateCount());

                    EXPECT_EQ(
                        state->userDataLastSyncTime(),
                        sendSyncState->userDataLastSyncTime());

                    EXPECT_EQ(
                        state->linkedNotebookUpdateCounts(),
                        sendSyncState->linkedNotebookUpdateCounts());

                    EXPECT_EQ(
                        state->linkedNotebookLastSyncTimes(),
                        sendSyncState->linkedNotebookLastSyncTimes());
                }
            });

    auto downloadSecondResult = downloadResult;
    downloadSecondResult.userOwnResult.syncChunksDataCounters =
        generateSampleSyncChunksDataCounters(10);
    downloadSecondResult.userOwnResult.downloadNotesStatus =
        generateSampleDownloadNotesStatus(10);
    downloadSecondResult.userOwnResult.downloadResourcesStatus =
        generateSampleDownloadResourcesStatus(10);

    auto downloadSecondSyncState =
        std::make_shared<SyncState>(*downloadSyncState);
    downloadSecondSyncState->m_userDataUpdateCount = 43;
    downloadSecondSyncState->m_userDataLastSyncTime = now + 10;

    downloadSecondResult.syncState = downloadSecondSyncState;

    EXPECT_CALL(*m_mockDownloader, download)
        .WillOnce(Return(threading::makeReadyFuture(downloadSecondResult)));

    EXPECT_CALL(*m_mockSyncStateStorage, setSyncState)
        .WillOnce(
            [&, this](const Account & account, const ISyncStatePtr & state) {
                EXPECT_EQ(account, m_account);
                EXPECT_TRUE(state);
                if (state) {
                    EXPECT_EQ(
                        state->userDataUpdateCount(),
                        downloadSecondSyncState->userDataUpdateCount());

                    EXPECT_EQ(
                        state->userDataLastSyncTime(),
                        downloadSecondSyncState->userDataLastSyncTime());

                    EXPECT_EQ(
                        state->linkedNotebookUpdateCounts(),
                        downloadSecondSyncState->linkedNotebookUpdateCounts());

                    EXPECT_EQ(
                        state->linkedNotebookLastSyncTimes(),
                        downloadSecondSyncState->linkedNotebookLastSyncTimes());
                }
            });

    const std::shared_ptr<mocks::MockIAccountSynchronizerCallback>
        mockCallback = std::make_shared<
            StrictMock<mocks::MockIAccountSynchronizerCallback>>();

    const auto canceler =
        std::make_shared<utility::cancelers::ManualCanceler>();

    auto syncResult = accountSynchronizer->synchronize(mockCallback, canceler);
    syncResult.waitForFinished();

    ASSERT_EQ(syncResult.resultCount(), 1);
    auto result = syncResult.result();

    // Checking the result

    ASSERT_TRUE(result);

    // Checking sync state
    const auto resultSyncState = result->syncState();
    ASSERT_TRUE(resultSyncState);

    EXPECT_EQ(
        resultSyncState->userDataUpdateCount(),
        downloadSecondSyncState->userDataUpdateCount());

    EXPECT_EQ(
        resultSyncState->userDataLastSyncTime(),
        downloadSecondSyncState->userDataLastSyncTime());

    const auto resultLinkedNotebookLastSyncTimes =
        resultSyncState->linkedNotebookLastSyncTimes();

    ASSERT_EQ(
        resultLinkedNotebookLastSyncTimes.size(), linkedNotebookGuids.size());

    for (const auto & linkedNotebookGuid: qAsConst(linkedNotebookGuids)) {
        const auto it =
            resultLinkedNotebookLastSyncTimes.constFind(linkedNotebookGuid);
        ASSERT_NE(it, resultLinkedNotebookLastSyncTimes.constEnd());

        const auto rit = sendSyncState->m_linkedNotebookLastSyncTimes.constFind(
            linkedNotebookGuid);
        ASSERT_NE(rit, sendSyncState->m_linkedNotebookLastSyncTimes.constEnd());

        EXPECT_EQ(it.value(), rit.value());
    }

    const auto resultLinkedNotebookUpdateCounts =
        resultSyncState->linkedNotebookUpdateCounts();

    ASSERT_EQ(
        resultLinkedNotebookUpdateCounts.size(), linkedNotebookGuids.size());

    for (const auto & linkedNotebookGuid: qAsConst(linkedNotebookGuids)) {
        const auto it =
            resultLinkedNotebookUpdateCounts.constFind(linkedNotebookGuid);
        ASSERT_NE(it, resultLinkedNotebookUpdateCounts.constEnd());

        const auto rit = sendSyncState->m_linkedNotebookUpdateCounts.constFind(
            linkedNotebookGuid);
        ASSERT_NE(rit, sendSyncState->m_linkedNotebookUpdateCounts.constEnd());

        EXPECT_EQ(it.value(), rit.value());
    }

    // Checking sync chunks data counters
    const auto resultSyncChunksDataCounters =
        result->userAccountSyncChunksDataCounters();
    ASSERT_TRUE(resultSyncChunksDataCounters);

    EXPECT_EQ(
        resultSyncChunksDataCounters,
        downloadSecondResult.userOwnResult.syncChunksDataCounters);

    const auto resultLinkedNotebookSyncChunksDataCounters =
        result->linkedNotebookSyncChunksDataCounters();

    ASSERT_EQ(
        resultLinkedNotebookSyncChunksDataCounters.size(),
        downloadSecondResult.linkedNotebookResults.size());

    for (const auto & linkedNotebookGuid: qAsConst(linkedNotebookGuids)) {
        const auto it = resultLinkedNotebookSyncChunksDataCounters.constFind(
            linkedNotebookGuid);
        ASSERT_NE(it, resultLinkedNotebookSyncChunksDataCounters.constEnd());

        const auto rit = downloadSecondResult.linkedNotebookResults.constFind(
            linkedNotebookGuid);
        ASSERT_NE(rit, downloadSecondResult.linkedNotebookResults.constEnd());

        EXPECT_EQ(it.value(), rit.value().syncChunksDataCounters);
    }

    // Checking download notes status
    const auto resultDownloadNotesStatus =
        result->userAccountDownloadNotesStatus();
    ASSERT_TRUE(resultDownloadNotesStatus);

    // The download notes status would be merged from the second download into
    // the first; as pointers are checked here instead of actual contents
    // of statuses, we check that the pointer from the first download is left
    // in the result.
    EXPECT_EQ(
        resultDownloadNotesStatus,
        downloadResult.userOwnResult.downloadNotesStatus);

    const auto resultLinkedNotebookDownloadNotesStatuses =
        result->linkedNotebookDownloadNotesStatuses();
    ASSERT_EQ(
        resultLinkedNotebookDownloadNotesStatuses.size(),
        linkedNotebookGuids.size());

    for (const auto & linkedNotebookGuid: qAsConst(linkedNotebookGuids)) {
        const auto it = resultLinkedNotebookDownloadNotesStatuses.constFind(
            linkedNotebookGuid);
        ASSERT_NE(it, resultLinkedNotebookDownloadNotesStatuses.constEnd());

        const auto rit = downloadResult.linkedNotebookResults.constFind(
            linkedNotebookGuid);
        ASSERT_NE(rit, downloadResult.linkedNotebookResults.constEnd());

        EXPECT_EQ(it.value(), rit.value().downloadNotesStatus);
    }

    // Checking download resources status
    const auto resultDownloadResourcesStatus =
        result->userAccountDownloadResourcesStatus();
    ASSERT_TRUE(resultDownloadResourcesStatus);

    // The download resources status would be merged from the second download
    // into the first; as pointers are checked here instead of actual contents
    // of statuses, we check that the pointer from the first download is left
    // in the result.
    EXPECT_EQ(
        resultDownloadResourcesStatus,
        downloadResult.userOwnResult.downloadResourcesStatus);

    const auto resultLinkedNotebookDownloadResourcesStatuses =
        result->linkedNotebookDownloadResourcesStatuses();
    ASSERT_EQ(
        resultLinkedNotebookDownloadResourcesStatuses.size(),
        linkedNotebookGuids.size());

    for (const auto & linkedNotebookGuid: qAsConst(linkedNotebookGuids)) {
        const auto it = resultLinkedNotebookDownloadResourcesStatuses.constFind(
            linkedNotebookGuid);
        ASSERT_NE(it, resultLinkedNotebookDownloadResourcesStatuses.constEnd());

        const auto rit = downloadResult.linkedNotebookResults.constFind(
            linkedNotebookGuid);
        ASSERT_NE(rit, downloadResult.linkedNotebookResults.constEnd());

        EXPECT_EQ(it.value(), rit.value().downloadResourcesStatus);
    }

    // Checking send status
    const auto resultSendStatus = result->userAccountSendStatus();
    ASSERT_TRUE(resultSendStatus);

    EXPECT_EQ(resultSendStatus, sendResult.userOwnResult);

    const auto resultLinkedNotebookSendStatuses =
        result->linkedNotebookSendStatuses();
    ASSERT_EQ(
        resultLinkedNotebookSendStatuses.size(), linkedNotebookGuids.size());

    for (const auto & linkedNotebookGuid: qAsConst(linkedNotebookGuids)) {
        const auto it =
            resultLinkedNotebookSendStatuses.constFind(linkedNotebookGuid);
        ASSERT_NE(it, resultLinkedNotebookSendStatuses.constEnd());

        const auto rit =
            sendResult.linkedNotebookResults.constFind(linkedNotebookGuid);
        ASSERT_NE(rit, sendResult.linkedNotebookResults.constEnd());

        EXPECT_EQ(it.value(), rit.value());
    }
}

TEST_F(
    AccountSynchronizerTest,
    DownloadSendAndDownloadAgainIfRequiredForOneOfLinkedNotebooks)
{
    const auto accountSynchronizer = std::make_shared<AccountSynchronizer>(
        m_account, m_mockDownloader, m_mockSender,
        m_mockAuthenticationInfoProvider, m_mockSyncStateStorage, m_threadPool);

    const int linkedNotebookCount = 3;
    QList<qevercloud::Guid> linkedNotebookGuids;
    linkedNotebookGuids.reserve(linkedNotebookCount);
    for (int i = 0; i < linkedNotebookCount; ++i) {
        linkedNotebookGuids << UidGenerator::Generate();
    }

    IDownloader::Result downloadResult;
    downloadResult.userOwnResult.syncChunksDataCounters =
        generateSampleSyncChunksDataCounters(1);
    downloadResult.userOwnResult.downloadNotesStatus =
        generateSampleDownloadNotesStatus(1);
    downloadResult.userOwnResult.downloadResourcesStatus =
        generateSampleDownloadResourcesStatus(1);

    qint32 counter = 1;
    for (const auto & linkedNotebookGuid: qAsConst(linkedNotebookGuids)) {
        auto & result =
            downloadResult.linkedNotebookResults[linkedNotebookGuid];

        result.syncChunksDataCounters = generateSampleSyncChunksDataCounters(
            3 + static_cast<quint64>(counter) * 2);

        result.downloadNotesStatus = generateSampleDownloadNotesStatus(
            5 + static_cast<quint64>(counter) * 3);

        result.downloadResourcesStatus = generateSampleDownloadResourcesStatus(
            8 + static_cast<quint64>(counter) * 4);

        ++counter;
    }

    const auto now = QDateTime::currentMSecsSinceEpoch();

    auto downloadSyncState = std::make_shared<SyncState>();
    downloadSyncState->m_userDataUpdateCount = 42;
    downloadSyncState->m_userDataLastSyncTime = now;

    counter = 1;
    for (const auto & linkedNotebookGuid: qAsConst(linkedNotebookGuids)) {
        downloadSyncState->m_linkedNotebookUpdateCounts[linkedNotebookGuid] =
            84 + counter * 2;

        downloadSyncState->m_linkedNotebookLastSyncTimes[linkedNotebookGuid] =
            now + counter;

        ++counter;
    }

    downloadResult.syncState = downloadSyncState;

    ISender::Result sendResult;
    sendResult.userOwnResult = generateSampleSendStatus(1);

    counter = 1;
    for (const auto & linkedNotebookGuid: qAsConst(linkedNotebookGuids)) {
        auto & result = sendResult.linkedNotebookResults[linkedNotebookGuid];
        result = generateSampleSendStatus(static_cast<quint64>(counter) * 5);

        if (&linkedNotebookGuid == &(*linkedNotebookGuids.constBegin())) {
            result->m_needToRepeatIncrementalSync = true;
        }
    }

    auto sendSyncState = std::make_shared<SyncState>();
    sendSyncState->m_userDataUpdateCount = 43;
    sendSyncState->m_userDataLastSyncTime = now + 1;

    counter = 1;
    for (const auto & linkedNotebookGuid: qAsConst(linkedNotebookGuids)) {
        sendSyncState->m_linkedNotebookUpdateCounts[linkedNotebookGuid] =
            120 + counter * 3;

        sendSyncState->m_linkedNotebookLastSyncTimes[linkedNotebookGuid] =
            now + counter * 2L;

        ++counter;
    }

    sendResult.syncState = sendSyncState;

    InSequence s;

    EXPECT_CALL(*m_mockDownloader, download)
        .WillOnce(Return(threading::makeReadyFuture(downloadResult)));

    EXPECT_CALL(*m_mockSyncStateStorage, setSyncState)
        .WillOnce(
            [&, this](const Account & account, const ISyncStatePtr & state) {
                EXPECT_EQ(account, m_account);
                EXPECT_TRUE(state);
                if (state) {
                    EXPECT_EQ(
                        state->userDataUpdateCount(),
                        downloadSyncState->userDataUpdateCount());

                    EXPECT_EQ(
                        state->userDataLastSyncTime(),
                        downloadSyncState->userDataLastSyncTime());

                    EXPECT_EQ(
                        state->linkedNotebookUpdateCounts(),
                        downloadSyncState->linkedNotebookUpdateCounts());

                    EXPECT_EQ(
                        state->linkedNotebookLastSyncTimes(),
                        downloadSyncState->linkedNotebookLastSyncTimes());
                }
            });

    EXPECT_CALL(*m_mockSender, send)
        .WillOnce(Return(threading::makeReadyFuture(sendResult)));

    EXPECT_CALL(*m_mockSyncStateStorage, setSyncState)
        .WillOnce(
            [&, this](const Account & account, const ISyncStatePtr & state) {
                EXPECT_EQ(account, m_account);
                EXPECT_TRUE(state);
                if (state) {
                    EXPECT_EQ(
                        state->userDataUpdateCount(),
                        sendSyncState->userDataUpdateCount());

                    EXPECT_EQ(
                        state->userDataLastSyncTime(),
                        sendSyncState->userDataLastSyncTime());

                    EXPECT_EQ(
                        state->linkedNotebookUpdateCounts(),
                        sendSyncState->linkedNotebookUpdateCounts());

                    EXPECT_EQ(
                        state->linkedNotebookLastSyncTimes(),
                        sendSyncState->linkedNotebookLastSyncTimes());
                }
            });

    auto downloadSecondResult = downloadResult;
    {
        const auto & linkedNotebookGuid = linkedNotebookGuids.constFirst();
        auto & linkedNotebookResult =
            downloadSecondResult.linkedNotebookResults[linkedNotebookGuid];

        linkedNotebookResult.syncChunksDataCounters =
            generateSampleSyncChunksDataCounters(10);

        linkedNotebookResult.downloadNotesStatus =
            generateSampleDownloadNotesStatus(10);

        linkedNotebookResult.downloadResourcesStatus =
            generateSampleDownloadResourcesStatus(10);
    }

    auto downloadSecondSyncState =
        std::make_shared<SyncState>(*downloadSyncState);
    {
        const auto & guid = linkedNotebookGuids.constFirst();
        downloadSecondSyncState->m_linkedNotebookUpdateCounts[guid] = 43;
        downloadSecondSyncState->m_linkedNotebookLastSyncTimes[guid] = now + 10;
    }

    downloadSecondResult.syncState = downloadSecondSyncState;

    EXPECT_CALL(*m_mockDownloader, download)
        .WillOnce(Return(threading::makeReadyFuture(downloadSecondResult)));

    EXPECT_CALL(*m_mockSyncStateStorage, setSyncState)
        .WillOnce(
            [&, this](const Account & account, const ISyncStatePtr & state) {
                EXPECT_EQ(account, m_account);
                EXPECT_TRUE(state);
                if (state) {
                    EXPECT_EQ(
                        state->userDataUpdateCount(),
                        downloadSecondSyncState->userDataUpdateCount());

                    EXPECT_EQ(
                        state->userDataLastSyncTime(),
                        downloadSecondSyncState->userDataLastSyncTime());

                    EXPECT_EQ(
                        state->linkedNotebookUpdateCounts(),
                        downloadSecondSyncState->linkedNotebookUpdateCounts());

                    EXPECT_EQ(
                        state->linkedNotebookLastSyncTimes(),
                        downloadSecondSyncState->linkedNotebookLastSyncTimes());
                }
            });

    const std::shared_ptr<mocks::MockIAccountSynchronizerCallback>
        mockCallback = std::make_shared<
            StrictMock<mocks::MockIAccountSynchronizerCallback>>();

    const auto canceler =
        std::make_shared<utility::cancelers::ManualCanceler>();

    auto syncResult = accountSynchronizer->synchronize(mockCallback, canceler);
    syncResult.waitForFinished();

    ASSERT_EQ(syncResult.resultCount(), 1);
    auto result = syncResult.result();

    // Checking the result

    ASSERT_TRUE(result);

    // Checking sync state
    const auto resultSyncState = result->syncState();
    ASSERT_TRUE(resultSyncState);

    EXPECT_EQ(
        resultSyncState->userDataUpdateCount(),
        downloadSecondSyncState->userDataUpdateCount());

    EXPECT_EQ(
        resultSyncState->userDataLastSyncTime(),
        downloadSecondSyncState->userDataLastSyncTime());

    const auto resultLinkedNotebookLastSyncTimes =
        resultSyncState->linkedNotebookLastSyncTimes();

    ASSERT_EQ(
        resultLinkedNotebookLastSyncTimes.size(), linkedNotebookGuids.size());

    for (const auto & linkedNotebookGuid: qAsConst(linkedNotebookGuids)) {
        const auto it =
            resultLinkedNotebookLastSyncTimes.constFind(linkedNotebookGuid);
        ASSERT_NE(it, resultLinkedNotebookLastSyncTimes.constEnd());

        const auto rit = sendSyncState->m_linkedNotebookLastSyncTimes.constFind(
            linkedNotebookGuid);
        ASSERT_NE(rit, sendSyncState->m_linkedNotebookLastSyncTimes.constEnd());

        EXPECT_EQ(it.value(), rit.value());
    }

    const auto resultLinkedNotebookUpdateCounts =
        resultSyncState->linkedNotebookUpdateCounts();

    ASSERT_EQ(
        resultLinkedNotebookUpdateCounts.size(), linkedNotebookGuids.size());

    for (const auto & linkedNotebookGuid: qAsConst(linkedNotebookGuids)) {
        const auto it =
            resultLinkedNotebookUpdateCounts.constFind(linkedNotebookGuid);
        ASSERT_NE(it, resultLinkedNotebookUpdateCounts.constEnd());

        const auto rit = sendSyncState->m_linkedNotebookUpdateCounts.constFind(
            linkedNotebookGuid);
        ASSERT_NE(rit, sendSyncState->m_linkedNotebookUpdateCounts.constEnd());

        EXPECT_EQ(it.value(), rit.value());
    }

    // Checking sync chunks data counters
    const auto resultSyncChunksDataCounters =
        result->userAccountSyncChunksDataCounters();
    ASSERT_TRUE(resultSyncChunksDataCounters);

    EXPECT_EQ(
        resultSyncChunksDataCounters,
        downloadSecondResult.userOwnResult.syncChunksDataCounters);

    const auto resultLinkedNotebookSyncChunksDataCounters =
        result->linkedNotebookSyncChunksDataCounters();

    ASSERT_EQ(
        resultLinkedNotebookSyncChunksDataCounters.size(),
        downloadSecondResult.linkedNotebookResults.size());

    for (const auto & linkedNotebookGuid: qAsConst(linkedNotebookGuids)) {
        const auto it = resultLinkedNotebookSyncChunksDataCounters.constFind(
            linkedNotebookGuid);
        ASSERT_NE(it, resultLinkedNotebookSyncChunksDataCounters.constEnd());

        const auto rit = downloadSecondResult.linkedNotebookResults.constFind(
            linkedNotebookGuid);
        ASSERT_NE(rit, downloadSecondResult.linkedNotebookResults.constEnd());

        EXPECT_EQ(it.value(), rit.value().syncChunksDataCounters);
    }

    // Checking download notes status
    const auto resultDownloadNotesStatus =
        result->userAccountDownloadNotesStatus();
    ASSERT_TRUE(resultDownloadNotesStatus);

    // The download notes status would be merged from the second download into
    // the first; as pointers are checked here instead of actual contents
    // of statuses, we check that the pointer from the first download is left
    // in the result.
    EXPECT_EQ(
        resultDownloadNotesStatus,
        downloadResult.userOwnResult.downloadNotesStatus);

    const auto resultLinkedNotebookDownloadNotesStatuses =
        result->linkedNotebookDownloadNotesStatuses();
    ASSERT_EQ(
        resultLinkedNotebookDownloadNotesStatuses.size(),
        linkedNotebookGuids.size());

    for (const auto & linkedNotebookGuid: qAsConst(linkedNotebookGuids)) {
        const auto it = resultLinkedNotebookDownloadNotesStatuses.constFind(
            linkedNotebookGuid);
        ASSERT_NE(it, resultLinkedNotebookDownloadNotesStatuses.constEnd());

        const auto rit = downloadResult.linkedNotebookResults.constFind(
            linkedNotebookGuid);
        ASSERT_NE(rit, downloadResult.linkedNotebookResults.constEnd());

        EXPECT_EQ(it.value(), rit.value().downloadNotesStatus);
    }

    // Checking download resources status
    const auto resultDownloadResourcesStatus =
        result->userAccountDownloadResourcesStatus();
    ASSERT_TRUE(resultDownloadResourcesStatus);

    // The download resources status would be merged from the second download
    // into the first; as pointers are checked here instead of actual contents
    // of statuses, we check that the pointer from the first download is left
    // in the result.
    EXPECT_EQ(
        resultDownloadResourcesStatus,
        downloadResult.userOwnResult.downloadResourcesStatus);

    const auto resultLinkedNotebookDownloadResourcesStatuses =
        result->linkedNotebookDownloadResourcesStatuses();
    ASSERT_EQ(
        resultLinkedNotebookDownloadResourcesStatuses.size(),
        linkedNotebookGuids.size());

    for (const auto & linkedNotebookGuid: qAsConst(linkedNotebookGuids)) {
        const auto it = resultLinkedNotebookDownloadResourcesStatuses.constFind(
            linkedNotebookGuid);
        ASSERT_NE(it, resultLinkedNotebookDownloadResourcesStatuses.constEnd());

        const auto rit = downloadResult.linkedNotebookResults.constFind(
            linkedNotebookGuid);
        ASSERT_NE(rit, downloadResult.linkedNotebookResults.constEnd());

        EXPECT_EQ(it.value(), rit.value().downloadResourcesStatus);
    }

    // Checking send status
    const auto resultSendStatus = result->userAccountSendStatus();
    ASSERT_TRUE(resultSendStatus);

    EXPECT_EQ(resultSendStatus, sendResult.userOwnResult);

    const auto resultLinkedNotebookSendStatuses =
        result->linkedNotebookSendStatuses();
    ASSERT_EQ(
        resultLinkedNotebookSendStatuses.size(), linkedNotebookGuids.size());

    for (const auto & linkedNotebookGuid: qAsConst(linkedNotebookGuids)) {
        const auto it =
            resultLinkedNotebookSendStatuses.constFind(linkedNotebookGuid);
        ASSERT_NE(it, resultLinkedNotebookSendStatuses.constEnd());

        const auto rit =
            sendResult.linkedNotebookResults.constFind(linkedNotebookGuid);
        ASSERT_NE(rit, sendResult.linkedNotebookResults.constEnd());

        EXPECT_EQ(it.value(), rit.value());
    }
}

} // namespace quentier::synchronization::tests
