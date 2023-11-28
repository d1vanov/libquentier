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

#include "Utils.h"

#include <synchronization/AccountSynchronizer.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/RuntimeError.h>
#include <quentier/synchronization/tests/mocks/MockISyncStateStorage.h>
#include <quentier/synchronization/types/Errors.h>
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
#include <synchronization/tests/mocks/MockISyncChunksStorage.h>
#include <synchronization/types/DownloadNotesStatus.h>
#include <synchronization/types/DownloadResourcesStatus.h>
#include <synchronization/types/SendStatus.h>
#include <synchronization/types/SyncState.h>

#include <qevercloud/exceptions/EDAMSystemExceptionAuthExpired.h>
#include <qevercloud/exceptions/EDAMSystemExceptionRateLimitReached.h>
#include <qevercloud/types/builders/LinkedNotebookBuilder.h>
#include <qevercloud/types/builders/NoteBuilder.h>
#include <qevercloud/types/builders/NotebookBuilder.h>
#include <qevercloud/types/builders/ResourceBuilder.h>
#include <qevercloud/types/builders/SavedSearchBuilder.h>
#include <qevercloud/types/builders/TagBuilder.h>

#include <QCoreApplication>
#include <QDateTime>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include <quentier/threading/Qt5Promise.h>
#endif

#include <gtest/gtest.h>

#include <utility>

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

[[nodiscard]] QList<qevercloud::Guid> generateLinkedNotebookGuids(
    const int linkedNotebookCount = 3)
{
    QList<qevercloud::Guid> linkedNotebookGuids;
    linkedNotebookGuids.reserve(linkedNotebookCount);
    for (int i = 0; i < linkedNotebookCount; ++i) {
        linkedNotebookGuids << UidGenerator::Generate();
    }
    return linkedNotebookGuids;
}

[[nodiscard]] IDownloader::Result generateSampleDownloaderResult(
    const QList<qevercloud::Guid> & linkedNotebookGuids)
{
    IDownloader::Result downloadResult;
    downloadResult.userOwnResult.syncChunksDataCounters =
        generateSampleSyncChunksDataCounters(1);
    downloadResult.userOwnResult.downloadNotesStatus =
        generateSampleDownloadNotesStatus(1);
    downloadResult.userOwnResult.downloadResourcesStatus =
        generateSampleDownloadResourcesStatus(1);

    qint32 counter = 1;
    for (const auto & linkedNotebookGuid: std::as_const(linkedNotebookGuids)) {
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
    for (const auto & linkedNotebookGuid: std::as_const(linkedNotebookGuids)) {
        downloadSyncState->m_linkedNotebookUpdateCounts[linkedNotebookGuid] =
            84 + counter * 2;

        downloadSyncState->m_linkedNotebookLastSyncTimes[linkedNotebookGuid] =
            now + counter;

        ++counter;
    }

    downloadResult.syncState = downloadSyncState;
    return downloadResult;
}

[[nodiscard]] ISender::Result generateSampleSendResult(
    const QList<qevercloud::Guid> & linkedNotebookGuids)
{
    const auto now = QDateTime::currentMSecsSinceEpoch();

    ISender::Result sendResult;
    sendResult.userOwnResult = generateSampleSendStatus(1);

    int counter = 1;
    for (const auto & linkedNotebookGuid: std::as_const(linkedNotebookGuids)) {
        sendResult.linkedNotebookResults[linkedNotebookGuid] =
            generateSampleSendStatus(static_cast<quint64>(counter) * 5);
    }

    auto sendSyncState = std::make_shared<SyncState>();
    sendSyncState->m_userDataUpdateCount = 43;
    sendSyncState->m_userDataLastSyncTime = now + 1;

    counter = 1;
    for (const auto & linkedNotebookGuid: std::as_const(linkedNotebookGuids)) {
        sendSyncState->m_linkedNotebookUpdateCounts[linkedNotebookGuid] =
            120 + counter * 3;

        sendSyncState->m_linkedNotebookLastSyncTimes[linkedNotebookGuid] =
            now + counter * 2L;

        ++counter;
    }

    sendResult.syncState = sendSyncState;
    return sendResult;
}

} // namespace

class AccountSynchronizerTest : public testing::Test
{
protected:
    void expectSetSyncState(ISyncStatePtr expectedSyncState)
    {
        ASSERT_TRUE(expectedSyncState);
        EXPECT_CALL(*m_mockSyncStateStorage, setSyncState)
            .WillOnce(
                [this, expectedSyncState = std::move(expectedSyncState)](
                    const Account & account, const ISyncStatePtr & state) {
                    EXPECT_EQ(account, m_account);
                    EXPECT_TRUE(state);
                    if (state) {
                        EXPECT_EQ(
                            state->userDataUpdateCount(),
                            expectedSyncState->userDataUpdateCount());

                        EXPECT_EQ(
                            state->userDataLastSyncTime(),
                            expectedSyncState->userDataLastSyncTime());

                        EXPECT_EQ(
                            state->linkedNotebookUpdateCounts(),
                            expectedSyncState->linkedNotebookUpdateCounts());

                        EXPECT_EQ(
                            state->linkedNotebookLastSyncTimes(),
                            expectedSyncState->linkedNotebookLastSyncTimes());
                    }
                });
    }

    void checkResultSyncState(
        const ISyncResult & result, const ISyncState & expectedSyncState,
        const QList<qevercloud::Guid> & linkedNotebookGuids)
    {
        const auto resultSyncState = result.syncState();
        ASSERT_TRUE(resultSyncState);

        EXPECT_EQ(
            resultSyncState->userDataUpdateCount(),
            expectedSyncState.userDataUpdateCount());

        EXPECT_EQ(
            resultSyncState->userDataLastSyncTime(),
            expectedSyncState.userDataLastSyncTime());

        const auto resultLinkedNotebookLastSyncTimes =
            resultSyncState->linkedNotebookLastSyncTimes();

        ASSERT_EQ(
            resultLinkedNotebookLastSyncTimes.size(),
            linkedNotebookGuids.size());

        for (const auto & linkedNotebookGuid:
             std::as_const(linkedNotebookGuids))
        {
            const auto it =
                resultLinkedNotebookLastSyncTimes.constFind(linkedNotebookGuid);
            ASSERT_NE(it, resultLinkedNotebookLastSyncTimes.constEnd());

            const auto & linkedNotebookLastSyncTimes =
                expectedSyncState.linkedNotebookLastSyncTimes();

            const auto rit =
                linkedNotebookLastSyncTimes.constFind(linkedNotebookGuid);
            ASSERT_NE(rit, linkedNotebookLastSyncTimes.constEnd());

            EXPECT_EQ(it.value(), rit.value());
        }

        const auto resultLinkedNotebookUpdateCounts =
            resultSyncState->linkedNotebookUpdateCounts();

        ASSERT_EQ(
            resultLinkedNotebookUpdateCounts.size(),
            linkedNotebookGuids.size());

        for (const auto & linkedNotebookGuid:
             std::as_const(linkedNotebookGuids))
        {
            const auto it =
                resultLinkedNotebookUpdateCounts.constFind(linkedNotebookGuid);
            ASSERT_NE(it, resultLinkedNotebookUpdateCounts.constEnd());

            const auto & linkedNotebookUpdateCounts =
                expectedSyncState.linkedNotebookUpdateCounts();

            const auto rit =
                linkedNotebookUpdateCounts.constFind(linkedNotebookGuid);
            ASSERT_NE(rit, linkedNotebookUpdateCounts.constEnd());

            EXPECT_EQ(it.value(), rit.value());
        }
    }

    void checkResultDownloadPart(
        const ISyncResult & result, const IDownloader::Result & downloadResult,
        const QList<qevercloud::Guid> & linkedNotebookGuids)
    {
        // Checking sync chunks data counters
        const auto resultSyncChunksDataCounters =
            result.userAccountSyncChunksDataCounters();
        ASSERT_TRUE(resultSyncChunksDataCounters);

        EXPECT_EQ(
            resultSyncChunksDataCounters,
            downloadResult.userOwnResult.syncChunksDataCounters);

        const auto resultLinkedNotebookSyncChunksDataCounters =
            result.linkedNotebookSyncChunksDataCounters();

        ASSERT_EQ(
            resultLinkedNotebookSyncChunksDataCounters.size(),
            downloadResult.linkedNotebookResults.size());

        for (const auto & linkedNotebookGuid:
             std::as_const(linkedNotebookGuids))
        {
            const auto it =
                resultLinkedNotebookSyncChunksDataCounters.constFind(
                    linkedNotebookGuid);
            ASSERT_NE(
                it, resultLinkedNotebookSyncChunksDataCounters.constEnd());

            const auto rit = downloadResult.linkedNotebookResults.constFind(
                linkedNotebookGuid);
            ASSERT_NE(rit, downloadResult.linkedNotebookResults.constEnd());

            EXPECT_EQ(it.value(), rit.value().syncChunksDataCounters);
        }

        // Checking download notes status
        const auto resultDownloadNotesStatus =
            result.userAccountDownloadNotesStatus();
        ASSERT_TRUE(resultDownloadNotesStatus);

        EXPECT_EQ(
            resultDownloadNotesStatus,
            downloadResult.userOwnResult.downloadNotesStatus);

        const auto resultLinkedNotebookDownloadNotesStatuses =
            result.linkedNotebookDownloadNotesStatuses();
        ASSERT_EQ(
            resultLinkedNotebookDownloadNotesStatuses.size(),
            linkedNotebookGuids.size());

        for (const auto & linkedNotebookGuid:
             std::as_const(linkedNotebookGuids))
        {
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
            result.userAccountDownloadResourcesStatus();
        ASSERT_TRUE(resultDownloadResourcesStatus);

        EXPECT_EQ(
            resultDownloadResourcesStatus,
            downloadResult.userOwnResult.downloadResourcesStatus);

        const auto resultLinkedNotebookDownloadResourcesStatuses =
            result.linkedNotebookDownloadResourcesStatuses();
        ASSERT_EQ(
            resultLinkedNotebookDownloadResourcesStatuses.size(),
            linkedNotebookGuids.size());

        for (const auto & linkedNotebookGuid:
             std::as_const(linkedNotebookGuids))
        {
            const auto it =
                resultLinkedNotebookDownloadResourcesStatuses.constFind(
                    linkedNotebookGuid);
            ASSERT_NE(
                it, resultLinkedNotebookDownloadResourcesStatuses.constEnd());

            const auto rit = downloadResult.linkedNotebookResults.constFind(
                linkedNotebookGuid);
            ASSERT_NE(rit, downloadResult.linkedNotebookResults.constEnd());

            EXPECT_EQ(it.value(), rit.value().downloadResourcesStatus);
        }
    }

    void checkResultSendPart(
        const ISyncResult & result, const ISender::Result & sendResult,
        const QList<qevercloud::Guid> & linkedNotebookGuids)
    {
        const auto resultSendStatus = result.userAccountSendStatus();
        ASSERT_TRUE(resultSendStatus);

        EXPECT_EQ(resultSendStatus, sendResult.userOwnResult);

        const auto resultLinkedNotebookSendStatuses =
            result.linkedNotebookSendStatuses();
        ASSERT_EQ(
            resultLinkedNotebookSendStatuses.size(),
            linkedNotebookGuids.size());

        for (const auto & linkedNotebookGuid:
             std::as_const(linkedNotebookGuids))
        {
            const auto it =
                resultLinkedNotebookSendStatuses.constFind(linkedNotebookGuid);
            ASSERT_NE(it, resultLinkedNotebookSendStatuses.constEnd());

            const auto rit =
                sendResult.linkedNotebookResults.constFind(linkedNotebookGuid);
            ASSERT_NE(rit, sendResult.linkedNotebookResults.constEnd());

            EXPECT_EQ(it.value(), rit.value());
        }
    }

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

    const std::shared_ptr<mocks::MockISyncChunksStorage>
        m_mockSyncChunksStorage =
            std::make_shared<StrictMock<mocks::MockISyncChunksStorage>>();
};

TEST_F(AccountSynchronizerTest, Ctor)
{
    EXPECT_NO_THROW(
        const auto accountSynchronizer = std::make_shared<AccountSynchronizer>(
            m_account, m_mockDownloader, m_mockSender,
            m_mockAuthenticationInfoProvider, m_mockSyncStateStorage,
            m_mockSyncChunksStorage));
}

TEST_F(AccountSynchronizerTest, CtorEmptyAccount)
{
    EXPECT_THROW(
        const auto accountSynchronizer = std::make_shared<AccountSynchronizer>(
            Account{}, m_mockDownloader, m_mockSender,
            m_mockAuthenticationInfoProvider, m_mockSyncStateStorage,
            m_mockSyncChunksStorage),
        InvalidArgument);
}

TEST_F(AccountSynchronizerTest, CtorNullDownloader)
{
    EXPECT_THROW(
        const auto accountSynchronizer = std::make_shared<AccountSynchronizer>(
            m_account, nullptr, m_mockSender, m_mockAuthenticationInfoProvider,
            m_mockSyncStateStorage, m_mockSyncChunksStorage),
        InvalidArgument);
}

TEST_F(AccountSynchronizerTest, CtorNullSender)
{
    EXPECT_THROW(
        const auto accountSynchronizer = std::make_shared<AccountSynchronizer>(
            m_account, m_mockDownloader, nullptr,
            m_mockAuthenticationInfoProvider, m_mockSyncStateStorage,
            m_mockSyncChunksStorage),
        InvalidArgument);
}

TEST_F(AccountSynchronizerTest, CtorNullAuthenticationInfoProvider)
{
    EXPECT_THROW(
        const auto accountSynchronizer = std::make_shared<AccountSynchronizer>(
            m_account, m_mockDownloader, m_mockSender, nullptr,
            m_mockSyncStateStorage, m_mockSyncChunksStorage),
        InvalidArgument);
}

TEST_F(AccountSynchronizerTest, CtorNullSyncStateStorage)
{
    EXPECT_THROW(
        const auto accountSynchronizer = std::make_shared<AccountSynchronizer>(
            m_account, m_mockDownloader, m_mockSender,
            m_mockAuthenticationInfoProvider, nullptr, m_mockSyncChunksStorage),
        InvalidArgument);
}

TEST_F(AccountSynchronizerTest, CtorNullSyncChunksStorage)
{
    EXPECT_THROW(
        const auto accountSynchronizer = std::make_shared<AccountSynchronizer>(
            m_account, m_mockDownloader, m_mockSender,
            m_mockAuthenticationInfoProvider, m_mockSyncStateStorage, nullptr),
        InvalidArgument);
}

TEST_F(AccountSynchronizerTest, NothingToDownloadOrSend)
{
    const auto accountSynchronizer = std::make_shared<AccountSynchronizer>(
        m_account, m_mockDownloader, m_mockSender,
        m_mockAuthenticationInfoProvider, m_mockSyncStateStorage,
        m_mockSyncChunksStorage);

    EXPECT_CALL(*m_mockDownloader, download)
        .WillOnce(Return(threading::makeReadyFuture(IDownloader::Result{})));

    EXPECT_CALL(*m_mockSender, send)
        .WillOnce(Return(threading::makeReadyFuture(ISender::Result{})));

    const std::shared_ptr<mocks::MockIAccountSynchronizerCallback>
        mockCallback = std::make_shared<
            StrictMock<mocks::MockIAccountSynchronizerCallback>>();

    const auto canceler =
        std::make_shared<utility::cancelers::ManualCanceler>();

    auto syncResult = accountSynchronizer->synchronize(mockCallback, canceler);
    waitForFuture(syncResult);

    ASSERT_EQ(syncResult.resultCount(), 1);
    auto result = syncResult.result();

    ASSERT_TRUE(result);
    EXPECT_FALSE(result->syncState());
    EXPECT_FALSE(result->userAccountSyncChunksDataCounters());
    EXPECT_FALSE(result->userAccountDownloadNotesStatus());
    EXPECT_FALSE(result->userAccountDownloadResourcesStatus());
    EXPECT_FALSE(result->userAccountSendStatus());
    EXPECT_TRUE(result->linkedNotebookSyncChunksDataCounters().isEmpty());
    EXPECT_TRUE(result->linkedNotebookDownloadNotesStatuses().isEmpty());
    EXPECT_TRUE(result->linkedNotebookDownloadResourcesStatuses().isEmpty());
    EXPECT_TRUE(result->linkedNotebookSendStatuses().isEmpty());
    EXPECT_TRUE(std::holds_alternative<std::monostate>(
        result->stopSynchronizationError()));
}

TEST_F(AccountSynchronizerTest, DownloadWithNothingToSend)
{
    const auto accountSynchronizer = std::make_shared<AccountSynchronizer>(
        m_account, m_mockDownloader, m_mockSender,
        m_mockAuthenticationInfoProvider, m_mockSyncStateStorage,
        m_mockSyncChunksStorage);

    const auto linkedNotebookGuids = generateLinkedNotebookGuids();

    const auto downloadResult =
        generateSampleDownloaderResult(linkedNotebookGuids);

    InSequence s;

    EXPECT_CALL(*m_mockDownloader, download)
        .WillOnce(Return(threading::makeReadyFuture(downloadResult)));

    expectSetSyncState(downloadResult.syncState);

    EXPECT_CALL(*m_mockSender, send)
        .WillOnce(Return(threading::makeReadyFuture(ISender::Result{})));

    const std::shared_ptr<mocks::MockIAccountSynchronizerCallback>
        mockCallback = std::make_shared<
            StrictMock<mocks::MockIAccountSynchronizerCallback>>();

    const auto canceler =
        std::make_shared<utility::cancelers::ManualCanceler>();

    auto syncResult = accountSynchronizer->synchronize(mockCallback, canceler);
    waitForFuture(syncResult);

    ASSERT_EQ(syncResult.resultCount(), 1);
    auto result = syncResult.result();

    // Checking the result
    ASSERT_TRUE(result);

    ASSERT_TRUE(downloadResult.syncState);
    checkResultSyncState(
        *result, *downloadResult.syncState, linkedNotebookGuids);

    checkResultDownloadPart(*result, downloadResult, linkedNotebookGuids);

    EXPECT_FALSE(result->userAccountSendStatus());
    EXPECT_TRUE(result->linkedNotebookSendStatuses().isEmpty());
}

TEST_F(AccountSynchronizerTest, SendWithNothingToDownload)
{
    const auto accountSynchronizer = std::make_shared<AccountSynchronizer>(
        m_account, m_mockDownloader, m_mockSender,
        m_mockAuthenticationInfoProvider, m_mockSyncStateStorage,
        m_mockSyncChunksStorage);

    const auto linkedNotebookGuids = generateLinkedNotebookGuids();
    const auto sendResult = generateSampleSendResult(linkedNotebookGuids);

    InSequence s;

    EXPECT_CALL(*m_mockDownloader, download)
        .WillOnce(Return(threading::makeReadyFuture(IDownloader::Result{})));

    EXPECT_CALL(*m_mockSender, send)
        .WillOnce(Return(threading::makeReadyFuture(sendResult)));

    expectSetSyncState(sendResult.syncState);

    const std::shared_ptr<mocks::MockIAccountSynchronizerCallback>
        mockCallback = std::make_shared<
            StrictMock<mocks::MockIAccountSynchronizerCallback>>();

    const auto canceler =
        std::make_shared<utility::cancelers::ManualCanceler>();

    auto syncResult = accountSynchronizer->synchronize(mockCallback, canceler);
    waitForFuture(syncResult);

    ASSERT_EQ(syncResult.resultCount(), 1);
    auto result = syncResult.result();

    // Checking the result

    ASSERT_TRUE(result);

    ASSERT_TRUE(sendResult.syncState);
    checkResultSyncState(*result, *sendResult.syncState, linkedNotebookGuids);

    EXPECT_FALSE(result->userAccountSyncChunksDataCounters());
    EXPECT_FALSE(result->userAccountDownloadNotesStatus());
    EXPECT_FALSE(result->userAccountDownloadResourcesStatus());
    EXPECT_TRUE(result->linkedNotebookSyncChunksDataCounters().isEmpty());
    EXPECT_TRUE(result->linkedNotebookDownloadNotesStatuses().isEmpty());
    EXPECT_TRUE(result->linkedNotebookDownloadResourcesStatuses().isEmpty());

    checkResultSendPart(*result, sendResult, linkedNotebookGuids);
}

TEST_F(AccountSynchronizerTest, DownloadAndSend)
{
    const auto accountSynchronizer = std::make_shared<AccountSynchronizer>(
        m_account, m_mockDownloader, m_mockSender,
        m_mockAuthenticationInfoProvider, m_mockSyncStateStorage,
        m_mockSyncChunksStorage);

    const auto linkedNotebookGuids = generateLinkedNotebookGuids();

    const auto downloadResult =
        generateSampleDownloaderResult(linkedNotebookGuids);

    const auto sendResult = generateSampleSendResult(linkedNotebookGuids);

    InSequence s;

    EXPECT_CALL(*m_mockDownloader, download)
        .WillOnce(Return(threading::makeReadyFuture(downloadResult)));

    expectSetSyncState(downloadResult.syncState);

    EXPECT_CALL(*m_mockSender, send)
        .WillOnce(Return(threading::makeReadyFuture(sendResult)));

    expectSetSyncState(sendResult.syncState);

    const std::shared_ptr<mocks::MockIAccountSynchronizerCallback>
        mockCallback = std::make_shared<
            StrictMock<mocks::MockIAccountSynchronizerCallback>>();

    const auto canceler =
        std::make_shared<utility::cancelers::ManualCanceler>();

    auto syncResult = accountSynchronizer->synchronize(mockCallback, canceler);
    waitForFuture(syncResult);

    ASSERT_EQ(syncResult.resultCount(), 1);
    auto result = syncResult.result();

    // Checking the result

    ASSERT_TRUE(result);

    ASSERT_TRUE(sendResult.syncState);
    checkResultSyncState(*result, *sendResult.syncState, linkedNotebookGuids);

    checkResultDownloadPart(*result, downloadResult, linkedNotebookGuids);

    checkResultSendPart(*result, sendResult, linkedNotebookGuids);
}

TEST_F(
    AccountSynchronizerTest,
    DownloadSendAndDownloadAgainIfRequiredForUserOwnAccount)
{
    const auto accountSynchronizer = std::make_shared<AccountSynchronizer>(
        m_account, m_mockDownloader, m_mockSender,
        m_mockAuthenticationInfoProvider, m_mockSyncStateStorage,
        m_mockSyncChunksStorage);

    const auto linkedNotebookGuids = generateLinkedNotebookGuids();

    const auto downloadResult =
        generateSampleDownloaderResult(linkedNotebookGuids);

    auto sendResult = generateSampleSendResult(linkedNotebookGuids);
    sendResult.userOwnResult->m_needToRepeatIncrementalSync = true;

    InSequence s;

    EXPECT_CALL(*m_mockDownloader, download)
        .WillOnce(Return(threading::makeReadyFuture(downloadResult)));

    expectSetSyncState(downloadResult.syncState);

    EXPECT_CALL(*m_mockSender, send)
        .WillOnce(Return(threading::makeReadyFuture(sendResult)));

    expectSetSyncState(sendResult.syncState);

    auto downloadSecondResult = downloadResult;
    downloadSecondResult.userOwnResult.syncChunksDataCounters =
        generateSampleSyncChunksDataCounters(10);
    downloadSecondResult.userOwnResult.downloadNotesStatus =
        generateSampleDownloadNotesStatus(10);
    downloadSecondResult.userOwnResult.downloadResourcesStatus =
        generateSampleDownloadResourcesStatus(10);

    ASSERT_TRUE(downloadResult.syncState);
    auto downloadSecondSyncState =
        std::make_shared<SyncState>(*downloadResult.syncState);
    downloadSecondSyncState->m_userDataUpdateCount = 43;
    downloadSecondSyncState->m_userDataLastSyncTime =
        downloadResult.syncState->m_userDataLastSyncTime + 10;

    downloadSecondResult.syncState = downloadSecondSyncState;

    EXPECT_CALL(*m_mockDownloader, download)
        .WillOnce(Return(threading::makeReadyFuture(downloadSecondResult)));

    expectSetSyncState(downloadSecondSyncState);

    const std::shared_ptr<mocks::MockIAccountSynchronizerCallback>
        mockCallback = std::make_shared<
            StrictMock<mocks::MockIAccountSynchronizerCallback>>();

    const auto canceler =
        std::make_shared<utility::cancelers::ManualCanceler>();

    auto syncResult = accountSynchronizer->synchronize(mockCallback, canceler);
    waitForFuture(syncResult);

    ASSERT_EQ(syncResult.resultCount(), 1);
    auto result = syncResult.result();

    // Checking the result

    ASSERT_TRUE(result);

    checkResultSyncState(
        *result, *downloadSecondSyncState, linkedNotebookGuids);

    auto mergedDownloadResult = downloadResult;

    // Sync chunks data counters should have been merged from the first download
    EXPECT_NE(
        result->userAccountSyncChunksDataCounters(),
        downloadSecondResult.userOwnResult.syncChunksDataCounters);

    mergedDownloadResult.userOwnResult.syncChunksDataCounters =
        std::dynamic_pointer_cast<SyncChunksDataCounters>(
            result->userAccountSyncChunksDataCounters());

    checkResultDownloadPart(*result, mergedDownloadResult, linkedNotebookGuids);
    checkResultSendPart(*result, sendResult, linkedNotebookGuids);
}

TEST_F(
    AccountSynchronizerTest,
    DownloadSendAndDownloadAgainIfRequiredForOneOfLinkedNotebooks)
{
    const auto accountSynchronizer = std::make_shared<AccountSynchronizer>(
        m_account, m_mockDownloader, m_mockSender,
        m_mockAuthenticationInfoProvider, m_mockSyncStateStorage,
        m_mockSyncChunksStorage);

    const auto linkedNotebookGuids = generateLinkedNotebookGuids();

    const auto downloadResult =
        generateSampleDownloaderResult(linkedNotebookGuids);

    auto sendResult = generateSampleSendResult(linkedNotebookGuids);
    ASSERT_FALSE(sendResult.linkedNotebookResults.isEmpty());

    const auto frontLinkedNotebookIt = sendResult.linkedNotebookResults.begin();
    (*frontLinkedNotebookIt)->m_needToRepeatIncrementalSync = true;

    InSequence s;

    EXPECT_CALL(*m_mockDownloader, download)
        .WillOnce(Return(threading::makeReadyFuture(downloadResult)));

    expectSetSyncState(downloadResult.syncState);

    EXPECT_CALL(*m_mockSender, send)
        .WillOnce(Return(threading::makeReadyFuture(sendResult)));

    expectSetSyncState(sendResult.syncState);

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
        std::make_shared<SyncState>(*downloadResult.syncState);
    {
        const auto & guid = linkedNotebookGuids.constFirst();
        downloadSecondSyncState->m_linkedNotebookUpdateCounts[guid] = 43;
        downloadSecondSyncState->m_linkedNotebookLastSyncTimes[guid] =
            downloadResult.syncState->m_linkedNotebookLastSyncTimes[guid] + 10;
    }

    downloadSecondResult.syncState = downloadSecondSyncState;

    EXPECT_CALL(*m_mockDownloader, download)
        .WillOnce(Return(threading::makeReadyFuture(downloadSecondResult)));

    expectSetSyncState(downloadSecondSyncState);

    const std::shared_ptr<mocks::MockIAccountSynchronizerCallback>
        mockCallback = std::make_shared<
            StrictMock<mocks::MockIAccountSynchronizerCallback>>();

    const auto canceler =
        std::make_shared<utility::cancelers::ManualCanceler>();

    auto syncResult = accountSynchronizer->synchronize(mockCallback, canceler);
    waitForFuture(syncResult);

    ASSERT_EQ(syncResult.resultCount(), 1);
    auto result = syncResult.result();

    // Checking the result

    ASSERT_TRUE(result);

    const auto mergedSyncState =
        std::make_shared<SyncState>(*downloadSecondSyncState);
    mergedSyncState->m_linkedNotebookUpdateCounts =
        sendResult.syncState->m_linkedNotebookUpdateCounts;
    mergedSyncState->m_linkedNotebookLastSyncTimes =
        sendResult.syncState->m_linkedNotebookLastSyncTimes;

    checkResultSyncState(*result, *mergedSyncState, linkedNotebookGuids);

    auto mergedDownloadResult = downloadSecondResult;

    mergedDownloadResult.userOwnResult.downloadNotesStatus =
        downloadResult.userOwnResult.downloadNotesStatus;

    mergedDownloadResult.userOwnResult.downloadResourcesStatus =
        downloadResult.userOwnResult.downloadResourcesStatus;

    mergedDownloadResult.linkedNotebookResults =
        downloadResult.linkedNotebookResults;

    const auto & linkedNotebookResultSyncChunksDataCounters =
        result->linkedNotebookSyncChunksDataCounters();

    for (const auto it:
         qevercloud::toRange(mergedDownloadResult.linkedNotebookResults))
    {
        const auto & linkedNotebookGuid = it.key();

        const auto rit = linkedNotebookResultSyncChunksDataCounters.constFind(
            linkedNotebookGuid);
        ASSERT_NE(rit, linkedNotebookResultSyncChunksDataCounters.constEnd());

        EXPECT_EQ(rit.value(), it.value().syncChunksDataCounters);
        it.value().syncChunksDataCounters =
            std::dynamic_pointer_cast<SyncChunksDataCounters>(rit.value());
    }

    checkResultDownloadPart(*result, mergedDownloadResult, linkedNotebookGuids);
    checkResultSendPart(*result, sendResult, linkedNotebookGuids);
}

TEST_F(
    AccountSynchronizerTest,
    HandleAuthenticationErrorDuringSyncChunksDownloading)
{
    const auto accountSynchronizer = std::make_shared<AccountSynchronizer>(
        m_account, m_mockDownloader, m_mockSender,
        m_mockAuthenticationInfoProvider, m_mockSyncStateStorage,
        m_mockSyncChunksStorage);

    const auto linkedNotebookGuids = generateLinkedNotebookGuids();

    const auto downloadResult =
        generateSampleDownloaderResult(linkedNotebookGuids);

    InSequence s;

    EXPECT_CALL(*m_mockDownloader, download)
        .WillOnce(Return(threading::makeExceptionalFuture<IDownloader::Result>(
            qevercloud::EDAMSystemExceptionAuthExpired{})));

    EXPECT_CALL(*m_mockAuthenticationInfoProvider, clearCaches)
        .WillOnce(
            [](const IAuthenticationInfoProvider::ClearCacheOptions & options) {
                EXPECT_TRUE(std::holds_alternative<
                            IAuthenticationInfoProvider::ClearCacheOption::All>(
                    options));
            });

    EXPECT_CALL(*m_mockDownloader, download)
        .WillOnce(Return(threading::makeReadyFuture(downloadResult)));

    expectSetSyncState(downloadResult.syncState);

    EXPECT_CALL(*m_mockSender, send)
        .WillOnce(Return(threading::makeReadyFuture(ISender::Result{})));

    const std::shared_ptr<mocks::MockIAccountSynchronizerCallback>
        mockCallback = std::make_shared<
            StrictMock<mocks::MockIAccountSynchronizerCallback>>();

    const auto canceler =
        std::make_shared<utility::cancelers::ManualCanceler>();

    auto syncResult = accountSynchronizer->synchronize(mockCallback, canceler);
    waitForFuture(syncResult);

    ASSERT_EQ(syncResult.resultCount(), 1);
    auto result = syncResult.result();

    // Checking the result

    ASSERT_TRUE(result);

    ASSERT_TRUE(downloadResult.syncState);
    checkResultSyncState(
        *result, *downloadResult.syncState, linkedNotebookGuids);

    checkResultDownloadPart(*result, downloadResult, linkedNotebookGuids);

    EXPECT_FALSE(result->userAccountSendStatus());
    EXPECT_TRUE(result->linkedNotebookSendStatuses().isEmpty());
}

TEST_F(
    AccountSynchronizerTest,
    HandleAuthenticationErrorDuringUserOwnNotesDownloading)
{
    const auto accountSynchronizer = std::make_shared<AccountSynchronizer>(
        m_account, m_mockDownloader, m_mockSender,
        m_mockAuthenticationInfoProvider, m_mockSyncStateStorage,
        m_mockSyncChunksStorage);

    const auto linkedNotebookGuids = generateLinkedNotebookGuids();

    auto downloadResult = generateSampleDownloaderResult(linkedNotebookGuids);
    downloadResult.userOwnResult.downloadNotesStatus
        ->m_stopSynchronizationError =
        StopSynchronizationError{AuthenticationExpiredError{}};

    InSequence s;

    EXPECT_CALL(*m_mockDownloader, download)
        .WillOnce(Return(threading::makeReadyFuture(downloadResult)));

    EXPECT_CALL(*m_mockAuthenticationInfoProvider, clearCaches)
        .WillOnce([this](const IAuthenticationInfoProvider::ClearCacheOptions &
                             options) {
            EXPECT_TRUE(std::holds_alternative<
                        IAuthenticationInfoProvider::ClearCacheOption::User>(
                options));
            const auto & option =
                std::get<IAuthenticationInfoProvider::ClearCacheOption::User>(
                    options);
            EXPECT_EQ(option.id, m_account.id());
        });

    auto downloadSecondResult = downloadResult;
    ASSERT_TRUE(downloadResult.userOwnResult.downloadNotesStatus);
    downloadSecondResult.userOwnResult.downloadNotesStatus =
        std::make_shared<DownloadNotesStatus>(
            *downloadResult.userOwnResult.downloadNotesStatus);
    downloadSecondResult.userOwnResult.downloadNotesStatus
        ->m_stopSynchronizationError =
        StopSynchronizationError{std::monostate{}};

    EXPECT_CALL(*m_mockDownloader, download)
        .WillOnce(Return(threading::makeReadyFuture(downloadSecondResult)));

    expectSetSyncState(downloadSecondResult.syncState);

    EXPECT_CALL(*m_mockSender, send)
        .WillOnce(Return(threading::makeReadyFuture(ISender::Result{})));

    const std::shared_ptr<mocks::MockIAccountSynchronizerCallback>
        mockCallback = std::make_shared<
            StrictMock<mocks::MockIAccountSynchronizerCallback>>();

    const auto canceler =
        std::make_shared<utility::cancelers::ManualCanceler>();

    auto syncResult = accountSynchronizer->synchronize(mockCallback, canceler);
    waitForFuture(syncResult);

    ASSERT_EQ(syncResult.resultCount(), 1);
    auto result = syncResult.result();

    // Checking the result

    ASSERT_TRUE(result);

    ASSERT_TRUE(downloadSecondResult.syncState);
    checkResultSyncState(
        *result, *downloadSecondResult.syncState, linkedNotebookGuids);

    auto mergedDownloadResult = downloadSecondResult;
    mergedDownloadResult.userOwnResult.downloadNotesStatus =
        downloadResult.userOwnResult.downloadNotesStatus;

    checkResultDownloadPart(*result, mergedDownloadResult, linkedNotebookGuids);

    EXPECT_FALSE(result->userAccountSendStatus());
    EXPECT_TRUE(result->linkedNotebookSendStatuses().isEmpty());
}

TEST_F(
    AccountSynchronizerTest,
    HandleAuthenticationErrorDuringUserOwnResourcesDownloading)
{
    const auto accountSynchronizer = std::make_shared<AccountSynchronizer>(
        m_account, m_mockDownloader, m_mockSender,
        m_mockAuthenticationInfoProvider, m_mockSyncStateStorage,
        m_mockSyncChunksStorage);

    const auto linkedNotebookGuids = generateLinkedNotebookGuids();

    auto downloadResult = generateSampleDownloaderResult(linkedNotebookGuids);
    downloadResult.userOwnResult.downloadResourcesStatus
        ->m_stopSynchronizationError =
        StopSynchronizationError{AuthenticationExpiredError{}};

    InSequence s;

    EXPECT_CALL(*m_mockDownloader, download)
        .WillOnce(Return(threading::makeReadyFuture(downloadResult)));

    EXPECT_CALL(*m_mockAuthenticationInfoProvider, clearCaches)
        .WillOnce([this](const IAuthenticationInfoProvider::ClearCacheOptions &
                             options) {
            EXPECT_TRUE(std::holds_alternative<
                        IAuthenticationInfoProvider::ClearCacheOption::User>(
                options));
            const auto & option =
                std::get<IAuthenticationInfoProvider::ClearCacheOption::User>(
                    options);
            EXPECT_EQ(option.id, m_account.id());
        });

    auto downloadSecondResult = downloadResult;
    ASSERT_TRUE(downloadResult.userOwnResult.downloadResourcesStatus);
    downloadSecondResult.userOwnResult.downloadResourcesStatus =
        std::make_shared<DownloadResourcesStatus>(
            *downloadResult.userOwnResult.downloadResourcesStatus);
    downloadSecondResult.userOwnResult.downloadResourcesStatus
        ->m_stopSynchronizationError =
        StopSynchronizationError{std::monostate{}};

    EXPECT_CALL(*m_mockDownloader, download)
        .WillOnce(Return(threading::makeReadyFuture(downloadSecondResult)));

    expectSetSyncState(downloadSecondResult.syncState);

    EXPECT_CALL(*m_mockSender, send)
        .WillOnce(Return(threading::makeReadyFuture(ISender::Result{})));

    const std::shared_ptr<mocks::MockIAccountSynchronizerCallback>
        mockCallback = std::make_shared<
            StrictMock<mocks::MockIAccountSynchronizerCallback>>();

    const auto canceler =
        std::make_shared<utility::cancelers::ManualCanceler>();

    auto syncResult = accountSynchronizer->synchronize(mockCallback, canceler);
    waitForFuture(syncResult);

    ASSERT_EQ(syncResult.resultCount(), 1);
    auto result = syncResult.result();

    // Checking the result

    ASSERT_TRUE(result);

    ASSERT_TRUE(downloadSecondResult.syncState);
    checkResultSyncState(
        *result, *downloadSecondResult.syncState, linkedNotebookGuids);

    auto mergedDownloadResult = downloadSecondResult;
    mergedDownloadResult.userOwnResult.downloadResourcesStatus =
        downloadResult.userOwnResult.downloadResourcesStatus;

    checkResultDownloadPart(*result, mergedDownloadResult, linkedNotebookGuids);

    EXPECT_FALSE(result->userAccountSendStatus());
    EXPECT_TRUE(result->linkedNotebookSendStatuses().isEmpty());
}

TEST_F(
    AccountSynchronizerTest,
    HandleAuthenticationErrorDuringLinkedNotebookNotesDownloading)
{
    const auto accountSynchronizer = std::make_shared<AccountSynchronizer>(
        m_account, m_mockDownloader, m_mockSender,
        m_mockAuthenticationInfoProvider, m_mockSyncStateStorage,
        m_mockSyncChunksStorage);

    const auto linkedNotebookGuids = generateLinkedNotebookGuids();
    ASSERT_FALSE(linkedNotebookGuids.isEmpty());

    const auto & linkedNotebookGuid = linkedNotebookGuids.constFirst();
    auto downloadResult = generateSampleDownloaderResult(linkedNotebookGuids);
    {
        auto & linkedNotebookResult =
            downloadResult.linkedNotebookResults[linkedNotebookGuid];

        ASSERT_TRUE(linkedNotebookResult.downloadNotesStatus);
        linkedNotebookResult.downloadNotesStatus->m_stopSynchronizationError =
            StopSynchronizationError{AuthenticationExpiredError{}};
    }

    InSequence s;

    EXPECT_CALL(*m_mockDownloader, download)
        .WillOnce(Return(threading::makeReadyFuture(downloadResult)));

    EXPECT_CALL(*m_mockAuthenticationInfoProvider, clearCaches)
        .WillOnce([&](const IAuthenticationInfoProvider::ClearCacheOptions &
                          options) {
            EXPECT_TRUE(
                std::holds_alternative<IAuthenticationInfoProvider::
                                           ClearCacheOption::LinkedNotebook>(
                    options));
            const auto & option = std::get<
                IAuthenticationInfoProvider::ClearCacheOption::LinkedNotebook>(
                options);
            EXPECT_EQ(option.guid, linkedNotebookGuid);
        });

    auto downloadSecondResult = downloadResult;
    {
        auto & linkedNotebookResult =
            downloadSecondResult.linkedNotebookResults[linkedNotebookGuid];
        linkedNotebookResult.downloadNotesStatus =
            std::make_shared<DownloadNotesStatus>(
                *linkedNotebookResult.downloadNotesStatus);
        linkedNotebookResult.downloadNotesStatus->m_stopSynchronizationError =
            StopSynchronizationError{std::monostate{}};
    }

    EXPECT_CALL(*m_mockDownloader, download)
        .WillOnce(Return(threading::makeReadyFuture(downloadSecondResult)));

    expectSetSyncState(downloadSecondResult.syncState);

    EXPECT_CALL(*m_mockSender, send)
        .WillOnce(Return(threading::makeReadyFuture(ISender::Result{})));

    const std::shared_ptr<mocks::MockIAccountSynchronizerCallback>
        mockCallback = std::make_shared<
            StrictMock<mocks::MockIAccountSynchronizerCallback>>();

    const auto canceler =
        std::make_shared<utility::cancelers::ManualCanceler>();

    auto syncResult = accountSynchronizer->synchronize(mockCallback, canceler);
    waitForFuture(syncResult);

    ASSERT_EQ(syncResult.resultCount(), 1);
    auto result = syncResult.result();

    // Checking the result

    ASSERT_TRUE(result);

    ASSERT_TRUE(downloadSecondResult.syncState);
    checkResultSyncState(
        *result, *downloadSecondResult.syncState, linkedNotebookGuids);

    auto mergedDownloadResult = downloadSecondResult;
    {
        auto & linkedNotebookResult =
            mergedDownloadResult.linkedNotebookResults[linkedNotebookGuid];
        linkedNotebookResult.downloadNotesStatus =
            downloadResult.linkedNotebookResults[linkedNotebookGuid]
                .downloadNotesStatus;
    }

    checkResultDownloadPart(*result, mergedDownloadResult, linkedNotebookGuids);

    EXPECT_FALSE(result->userAccountSendStatus());
    EXPECT_TRUE(result->linkedNotebookSendStatuses().isEmpty());
}

TEST_F(
    AccountSynchronizerTest,
    HandleAuthenticationErrorDuringLinkedNotebookResourcesDownloading)
{
    const auto accountSynchronizer = std::make_shared<AccountSynchronizer>(
        m_account, m_mockDownloader, m_mockSender,
        m_mockAuthenticationInfoProvider, m_mockSyncStateStorage,
        m_mockSyncChunksStorage);

    const auto linkedNotebookGuids = generateLinkedNotebookGuids();
    ASSERT_FALSE(linkedNotebookGuids.isEmpty());

    const auto & linkedNotebookGuid = linkedNotebookGuids.constFirst();
    auto downloadResult = generateSampleDownloaderResult(linkedNotebookGuids);
    {
        auto & linkedNotebookResult =
            downloadResult.linkedNotebookResults[linkedNotebookGuid];

        ASSERT_TRUE(linkedNotebookResult.downloadResourcesStatus);
        linkedNotebookResult.downloadResourcesStatus
            ->m_stopSynchronizationError =
            StopSynchronizationError{AuthenticationExpiredError{}};
    }

    InSequence s;

    EXPECT_CALL(*m_mockDownloader, download)
        .WillOnce(Return(threading::makeReadyFuture(downloadResult)));

    EXPECT_CALL(*m_mockAuthenticationInfoProvider, clearCaches)
        .WillOnce([&](const IAuthenticationInfoProvider::ClearCacheOptions &
                          options) {
            EXPECT_TRUE(
                std::holds_alternative<IAuthenticationInfoProvider::
                                           ClearCacheOption::LinkedNotebook>(
                    options));
            const auto & option = std::get<
                IAuthenticationInfoProvider::ClearCacheOption::LinkedNotebook>(
                options);
            EXPECT_EQ(option.guid, linkedNotebookGuid);
        });

    auto downloadSecondResult = downloadResult;
    {
        auto & linkedNotebookResult =
            downloadSecondResult.linkedNotebookResults[linkedNotebookGuid];
        ASSERT_TRUE(linkedNotebookResult.downloadResourcesStatus);
        linkedNotebookResult.downloadResourcesStatus =
            std::make_shared<DownloadResourcesStatus>(
                *linkedNotebookResult.downloadResourcesStatus);
        linkedNotebookResult.downloadResourcesStatus
            ->m_stopSynchronizationError =
            StopSynchronizationError{std::monostate{}};
    }

    EXPECT_CALL(*m_mockDownloader, download)
        .WillOnce(Return(threading::makeReadyFuture(downloadSecondResult)));

    expectSetSyncState(downloadSecondResult.syncState);

    EXPECT_CALL(*m_mockSender, send)
        .WillOnce(Return(threading::makeReadyFuture(ISender::Result{})));

    const std::shared_ptr<mocks::MockIAccountSynchronizerCallback>
        mockCallback = std::make_shared<
            StrictMock<mocks::MockIAccountSynchronizerCallback>>();

    const auto canceler =
        std::make_shared<utility::cancelers::ManualCanceler>();

    auto syncResult = accountSynchronizer->synchronize(mockCallback, canceler);
    waitForFuture(syncResult);

    ASSERT_EQ(syncResult.resultCount(), 1);
    auto result = syncResult.result();

    // Checking the result

    ASSERT_TRUE(result);

    ASSERT_TRUE(downloadSecondResult.syncState);
    checkResultSyncState(
        *result, *downloadSecondResult.syncState, linkedNotebookGuids);

    auto mergedDownloadResult = downloadSecondResult;
    {
        auto & linkedNotebookResult =
            mergedDownloadResult.linkedNotebookResults[linkedNotebookGuid];
        linkedNotebookResult.downloadResourcesStatus =
            downloadResult.linkedNotebookResults[linkedNotebookGuid]
                .downloadResourcesStatus;
    }

    checkResultDownloadPart(*result, mergedDownloadResult, linkedNotebookGuids);

    EXPECT_FALSE(result->userAccountSendStatus());
    EXPECT_TRUE(result->linkedNotebookSendStatuses().isEmpty());
}

TEST_F(
    AccountSynchronizerTest, HandleAuthenticationErrorDuringUserOwnDataSending)
{
    const auto accountSynchronizer = std::make_shared<AccountSynchronizer>(
        m_account, m_mockDownloader, m_mockSender,
        m_mockAuthenticationInfoProvider, m_mockSyncStateStorage,
        m_mockSyncChunksStorage);

    const auto linkedNotebookGuids = generateLinkedNotebookGuids();

    auto sendResult = generateSampleSendResult(linkedNotebookGuids);
    sendResult.userOwnResult->m_stopSynchronizationError =
        StopSynchronizationError{AuthenticationExpiredError{}};

    InSequence s;

    EXPECT_CALL(*m_mockDownloader, download)
        .WillOnce(Return(threading::makeReadyFuture(IDownloader::Result{})));

    EXPECT_CALL(*m_mockSender, send)
        .WillOnce(Return(threading::makeReadyFuture(sendResult)));

    EXPECT_CALL(*m_mockAuthenticationInfoProvider, clearCaches)
        .WillOnce([this](const IAuthenticationInfoProvider::ClearCacheOptions &
                             options) {
            EXPECT_TRUE(std::holds_alternative<
                        IAuthenticationInfoProvider::ClearCacheOption::User>(
                options));
            const auto & option =
                std::get<IAuthenticationInfoProvider::ClearCacheOption::User>(
                    options);
            EXPECT_EQ(option.id, m_account.id());
        });

    EXPECT_CALL(*m_mockDownloader, download)
        .WillOnce(Return(threading::makeReadyFuture(IDownloader::Result{})));

    auto sendSecondResult = sendResult;
    sendSecondResult.userOwnResult =
        std::make_shared<SendStatus>(*sendResult.userOwnResult);
    sendSecondResult.userOwnResult->m_stopSynchronizationError =
        StopSynchronizationError{std::monostate{}};

    EXPECT_CALL(*m_mockSender, send)
        .WillOnce(Return(threading::makeReadyFuture(sendSecondResult)));

    expectSetSyncState(sendSecondResult.syncState);

    const std::shared_ptr<mocks::MockIAccountSynchronizerCallback>
        mockCallback = std::make_shared<
            StrictMock<mocks::MockIAccountSynchronizerCallback>>();

    const auto canceler =
        std::make_shared<utility::cancelers::ManualCanceler>();

    auto syncResult = accountSynchronizer->synchronize(mockCallback, canceler);
    waitForFuture(syncResult);

    ASSERT_EQ(syncResult.resultCount(), 1);
    auto result = syncResult.result();

    // Checking the result

    ASSERT_TRUE(result);

    ASSERT_TRUE(sendSecondResult.syncState);
    checkResultSyncState(
        *result, *sendSecondResult.syncState, linkedNotebookGuids);

    EXPECT_FALSE(result->userAccountSyncChunksDataCounters());
    EXPECT_FALSE(result->userAccountDownloadNotesStatus());
    EXPECT_FALSE(result->userAccountDownloadResourcesStatus());
    EXPECT_TRUE(result->linkedNotebookSyncChunksDataCounters().isEmpty());
    EXPECT_TRUE(result->linkedNotebookDownloadNotesStatuses().isEmpty());
    EXPECT_TRUE(result->linkedNotebookDownloadResourcesStatuses().isEmpty());

    auto mergedSecondResult = sendSecondResult;
    mergedSecondResult.userOwnResult = sendResult.userOwnResult;
    checkResultSendPart(*result, mergedSecondResult, linkedNotebookGuids);
}

TEST_F(
    AccountSynchronizerTest,
    HandleAuthenticationErrorDuringLinkedNotebookDataSending)
{
    const auto accountSynchronizer = std::make_shared<AccountSynchronizer>(
        m_account, m_mockDownloader, m_mockSender,
        m_mockAuthenticationInfoProvider, m_mockSyncStateStorage,
        m_mockSyncChunksStorage);

    const auto linkedNotebookGuids = generateLinkedNotebookGuids();
    ASSERT_FALSE(linkedNotebookGuids.isEmpty());

    const auto & linkedNotebookGuid = linkedNotebookGuids.constFirst();
    auto sendResult = generateSampleSendResult(linkedNotebookGuids);
    {
        auto & linkedNotebookResult =
            sendResult.linkedNotebookResults[linkedNotebookGuid];

        ASSERT_TRUE(linkedNotebookResult);
        linkedNotebookResult->m_stopSynchronizationError =
            StopSynchronizationError{AuthenticationExpiredError{}};
    }

    InSequence s;

    EXPECT_CALL(*m_mockDownloader, download)
        .WillOnce(Return(threading::makeReadyFuture(IDownloader::Result{})));

    EXPECT_CALL(*m_mockSender, send)
        .WillOnce(Return(threading::makeReadyFuture(sendResult)));

    EXPECT_CALL(*m_mockAuthenticationInfoProvider, clearCaches)
        .WillOnce([&](const IAuthenticationInfoProvider::ClearCacheOptions &
                          options) {
            EXPECT_TRUE(
                std::holds_alternative<IAuthenticationInfoProvider::
                                           ClearCacheOption::LinkedNotebook>(
                    options));
            const auto & option = std::get<
                IAuthenticationInfoProvider::ClearCacheOption::LinkedNotebook>(
                options);
            EXPECT_EQ(option.guid, linkedNotebookGuid);
        });

    EXPECT_CALL(*m_mockDownloader, download)
        .WillOnce(Return(threading::makeReadyFuture(IDownloader::Result{})));

    auto sendSecondResult = sendResult;
    {
        auto & linkedNotebookResult =
            sendSecondResult.linkedNotebookResults[linkedNotebookGuid];
        ASSERT_TRUE(linkedNotebookResult);
        linkedNotebookResult =
            std::make_shared<SendStatus>(*linkedNotebookResult);
        linkedNotebookResult->m_stopSynchronizationError =
            StopSynchronizationError{std::monostate{}};
    }

    EXPECT_CALL(*m_mockSender, send)
        .WillOnce(Return(threading::makeReadyFuture(sendSecondResult)));

    expectSetSyncState(sendSecondResult.syncState);

    const std::shared_ptr<mocks::MockIAccountSynchronizerCallback>
        mockCallback = std::make_shared<
            StrictMock<mocks::MockIAccountSynchronizerCallback>>();

    const auto canceler =
        std::make_shared<utility::cancelers::ManualCanceler>();

    auto syncResult = accountSynchronizer->synchronize(mockCallback, canceler);
    waitForFuture(syncResult);

    ASSERT_EQ(syncResult.resultCount(), 1);
    auto result = syncResult.result();

    // Checking the result

    ASSERT_TRUE(result);

    ASSERT_TRUE(sendSecondResult.syncState);
    checkResultSyncState(
        *result, *sendSecondResult.syncState, linkedNotebookGuids);

    EXPECT_FALSE(result->userAccountSyncChunksDataCounters());
    EXPECT_FALSE(result->userAccountDownloadNotesStatus());
    EXPECT_FALSE(result->userAccountDownloadResourcesStatus());
    EXPECT_TRUE(result->linkedNotebookSyncChunksDataCounters().isEmpty());
    EXPECT_TRUE(result->linkedNotebookDownloadNotesStatuses().isEmpty());
    EXPECT_TRUE(result->linkedNotebookDownloadResourcesStatuses().isEmpty());

    auto mergedSecondResult = sendSecondResult;
    {
        auto & linkedNotebookResult =
            mergedSecondResult.linkedNotebookResults[linkedNotebookGuid];
        linkedNotebookResult =
            sendResult.linkedNotebookResults[linkedNotebookGuid];
    }
    checkResultSendPart(*result, mergedSecondResult, linkedNotebookGuids);
}

TEST_F(
    AccountSynchronizerTest,
    PropagateRateLimitExceededErrorWhenDownloadingSyncChunks)
{
    const auto accountSynchronizer = std::make_shared<AccountSynchronizer>(
        m_account, m_mockDownloader, m_mockSender,
        m_mockAuthenticationInfoProvider, m_mockSyncStateStorage,
        m_mockSyncChunksStorage);

    const qint32 rateLimitDuration = 1000;

    InSequence s;

    EXPECT_CALL(*m_mockDownloader, download)
        .WillOnce(Return(threading::makeExceptionalFuture<IDownloader::Result>(
            [rateLimitDuration] {
                qevercloud::EDAMSystemExceptionRateLimitReached e;
                e.setRateLimitDuration(rateLimitDuration);
                return e;
            }())));

    const std::shared_ptr<mocks::MockIAccountSynchronizerCallback>
        mockCallback = std::make_shared<
            StrictMock<mocks::MockIAccountSynchronizerCallback>>();

    const auto canceler =
        std::make_shared<utility::cancelers::ManualCanceler>();

    auto syncResult = accountSynchronizer->synchronize(mockCallback, canceler);
    waitForFuture(syncResult);

    ASSERT_EQ(syncResult.resultCount(), 1);
    auto result = syncResult.result();

    // Checking the result

    ASSERT_TRUE(result);

    EXPECT_FALSE(result->userAccountSyncChunksDataCounters());
    EXPECT_FALSE(result->userAccountDownloadNotesStatus());
    EXPECT_FALSE(result->userAccountDownloadResourcesStatus());
    EXPECT_FALSE(result->userAccountSendStatus());
    EXPECT_TRUE(result->linkedNotebookSyncChunksDataCounters().isEmpty());
    EXPECT_TRUE(result->linkedNotebookDownloadNotesStatuses().isEmpty());
    EXPECT_TRUE(result->linkedNotebookDownloadResourcesStatuses().isEmpty());
    EXPECT_TRUE(result->linkedNotebookSendStatuses().isEmpty());

    ASSERT_TRUE(std::holds_alternative<RateLimitReachedError>(
        result->stopSynchronizationError()));

    const auto error =
        std::get<RateLimitReachedError>(result->stopSynchronizationError());
    EXPECT_EQ(error.rateLimitDurationSec, rateLimitDuration);
}

TEST_F(
    AccountSynchronizerTest,
    PropagateRateLimitExceededErrorWhenDownloadingUserOwnNotes)
{
    const auto accountSynchronizer = std::make_shared<AccountSynchronizer>(
        m_account, m_mockDownloader, m_mockSender,
        m_mockAuthenticationInfoProvider, m_mockSyncStateStorage,
        m_mockSyncChunksStorage);

    const auto linkedNotebookGuids = generateLinkedNotebookGuids();
    const qint32 rateLimitDuration = 1000;

    auto downloadResult = generateSampleDownloaderResult(linkedNotebookGuids);
    downloadResult.userOwnResult.downloadNotesStatus
        ->m_stopSynchronizationError = RateLimitReachedError{rateLimitDuration};

    InSequence s;

    EXPECT_CALL(*m_mockDownloader, download)
        .WillOnce(Return(threading::makeReadyFuture(downloadResult)));

    const std::shared_ptr<mocks::MockIAccountSynchronizerCallback>
        mockCallback = std::make_shared<
            StrictMock<mocks::MockIAccountSynchronizerCallback>>();

    const auto canceler =
        std::make_shared<utility::cancelers::ManualCanceler>();

    auto syncResult = accountSynchronizer->synchronize(mockCallback, canceler);
    waitForFuture(syncResult);

    ASSERT_EQ(syncResult.resultCount(), 1);
    auto result = syncResult.result();

    // Checking the result

    ASSERT_TRUE(result);

    checkResultDownloadPart(*result, downloadResult, linkedNotebookGuids);

    EXPECT_FALSE(result->userAccountSendStatus());
    EXPECT_TRUE(result->linkedNotebookSendStatuses().isEmpty());

    ASSERT_TRUE(std::holds_alternative<RateLimitReachedError>(
        result->stopSynchronizationError()));

    const auto error =
        std::get<RateLimitReachedError>(result->stopSynchronizationError());
    EXPECT_EQ(error.rateLimitDurationSec, rateLimitDuration);
}

TEST_F(
    AccountSynchronizerTest,
    PropagateRateLimitExceededErrorWhenDownloadingUserOwnResources)
{
    const auto accountSynchronizer = std::make_shared<AccountSynchronizer>(
        m_account, m_mockDownloader, m_mockSender,
        m_mockAuthenticationInfoProvider, m_mockSyncStateStorage,
        m_mockSyncChunksStorage);

    const auto linkedNotebookGuids = generateLinkedNotebookGuids();
    const qint32 rateLimitDuration = 1000;

    auto downloadResult = generateSampleDownloaderResult(linkedNotebookGuids);
    downloadResult.userOwnResult.downloadResourcesStatus
        ->m_stopSynchronizationError = RateLimitReachedError{rateLimitDuration};

    InSequence s;

    EXPECT_CALL(*m_mockDownloader, download)
        .WillOnce(Return(threading::makeReadyFuture(downloadResult)));

    const std::shared_ptr<mocks::MockIAccountSynchronizerCallback>
        mockCallback = std::make_shared<
            StrictMock<mocks::MockIAccountSynchronizerCallback>>();

    const auto canceler =
        std::make_shared<utility::cancelers::ManualCanceler>();

    auto syncResult = accountSynchronizer->synchronize(mockCallback, canceler);
    waitForFuture(syncResult);

    ASSERT_EQ(syncResult.resultCount(), 1);
    auto result = syncResult.result();

    // Checking the result

    ASSERT_TRUE(result);

    checkResultDownloadPart(*result, downloadResult, linkedNotebookGuids);

    EXPECT_FALSE(result->userAccountSendStatus());
    EXPECT_TRUE(result->linkedNotebookSendStatuses().isEmpty());

    ASSERT_TRUE(std::holds_alternative<RateLimitReachedError>(
        result->stopSynchronizationError()));

    const auto error =
        std::get<RateLimitReachedError>(result->stopSynchronizationError());
    EXPECT_EQ(error.rateLimitDurationSec, rateLimitDuration);
}

TEST_F(
    AccountSynchronizerTest,
    PropagateRateLimitExceededErrorWhenDownloadingLinkedNotebookNotes)
{
    const auto accountSynchronizer = std::make_shared<AccountSynchronizer>(
        m_account, m_mockDownloader, m_mockSender,
        m_mockAuthenticationInfoProvider, m_mockSyncStateStorage,
        m_mockSyncChunksStorage);

    const auto linkedNotebookGuids = generateLinkedNotebookGuids();
    ASSERT_FALSE(linkedNotebookGuids.isEmpty());

    const auto & linkedNotebookGuid = linkedNotebookGuids.constFirst();
    const qint32 rateLimitDuration = 1000;

    auto downloadResult = generateSampleDownloaderResult(linkedNotebookGuids);
    {
        auto & linkedNotebookResult =
            downloadResult.linkedNotebookResults[linkedNotebookGuid];

        ASSERT_TRUE(linkedNotebookResult.downloadNotesStatus);
        linkedNotebookResult.downloadNotesStatus->m_stopSynchronizationError =
            StopSynchronizationError{RateLimitReachedError{rateLimitDuration}};
    }

    EXPECT_CALL(*m_mockDownloader, download)
        .WillOnce(Return(threading::makeReadyFuture(downloadResult)));

    const std::shared_ptr<mocks::MockIAccountSynchronizerCallback>
        mockCallback = std::make_shared<
            StrictMock<mocks::MockIAccountSynchronizerCallback>>();

    const auto canceler =
        std::make_shared<utility::cancelers::ManualCanceler>();

    auto syncResult = accountSynchronizer->synchronize(mockCallback, canceler);
    waitForFuture(syncResult);

    ASSERT_EQ(syncResult.resultCount(), 1);
    auto result = syncResult.result();

    // Checking the result

    ASSERT_TRUE(result);

    checkResultDownloadPart(*result, downloadResult, linkedNotebookGuids);

    EXPECT_FALSE(result->userAccountSendStatus());
    EXPECT_TRUE(result->linkedNotebookSendStatuses().isEmpty());

    ASSERT_TRUE(std::holds_alternative<RateLimitReachedError>(
        result->stopSynchronizationError()));

    const auto error =
        std::get<RateLimitReachedError>(result->stopSynchronizationError());
    EXPECT_EQ(error.rateLimitDurationSec, rateLimitDuration);
}

TEST_F(
    AccountSynchronizerTest,
    PropagateRateLimitExceededErrorWhenDownloadingLinkedNotebookResources)
{
    const auto accountSynchronizer = std::make_shared<AccountSynchronizer>(
        m_account, m_mockDownloader, m_mockSender,
        m_mockAuthenticationInfoProvider, m_mockSyncStateStorage,
        m_mockSyncChunksStorage);

    const auto linkedNotebookGuids = generateLinkedNotebookGuids();
    ASSERT_FALSE(linkedNotebookGuids.isEmpty());

    const auto & linkedNotebookGuid = linkedNotebookGuids.constFirst();
    const qint32 rateLimitDuration = 1000;

    auto downloadResult = generateSampleDownloaderResult(linkedNotebookGuids);
    {
        auto & linkedNotebookResult =
            downloadResult.linkedNotebookResults[linkedNotebookGuid];

        ASSERT_TRUE(linkedNotebookResult.downloadResourcesStatus);
        linkedNotebookResult.downloadResourcesStatus
            ->m_stopSynchronizationError =
            StopSynchronizationError{RateLimitReachedError{rateLimitDuration}};
    }

    EXPECT_CALL(*m_mockDownloader, download)
        .WillOnce(Return(threading::makeReadyFuture(downloadResult)));

    const std::shared_ptr<mocks::MockIAccountSynchronizerCallback>
        mockCallback = std::make_shared<
            StrictMock<mocks::MockIAccountSynchronizerCallback>>();

    const auto canceler =
        std::make_shared<utility::cancelers::ManualCanceler>();

    auto syncResult = accountSynchronizer->synchronize(mockCallback, canceler);
    waitForFuture(syncResult);

    ASSERT_EQ(syncResult.resultCount(), 1);
    auto result = syncResult.result();

    // Checking the result

    ASSERT_TRUE(result);

    checkResultDownloadPart(*result, downloadResult, linkedNotebookGuids);

    EXPECT_FALSE(result->userAccountSendStatus());
    EXPECT_TRUE(result->linkedNotebookSendStatuses().isEmpty());

    ASSERT_TRUE(std::holds_alternative<RateLimitReachedError>(
        result->stopSynchronizationError()));

    const auto error =
        std::get<RateLimitReachedError>(result->stopSynchronizationError());
    EXPECT_EQ(error.rateLimitDurationSec, rateLimitDuration);
}

TEST_F(
    AccountSynchronizerTest,
    PropagateRateLimitExceededErrorWhenSendingUserOwnData)
{
    const auto accountSynchronizer = std::make_shared<AccountSynchronizer>(
        m_account, m_mockDownloader, m_mockSender,
        m_mockAuthenticationInfoProvider, m_mockSyncStateStorage,
        m_mockSyncChunksStorage);

    const auto linkedNotebookGuids = generateLinkedNotebookGuids();
    const qint32 rateLimitDuration = 1000;

    auto sendResult = generateSampleSendResult(linkedNotebookGuids);
    sendResult.userOwnResult->m_stopSynchronizationError =
        StopSynchronizationError{RateLimitReachedError{rateLimitDuration}};

    EXPECT_CALL(*m_mockDownloader, download)
        .WillOnce(Return(threading::makeReadyFuture(IDownloader::Result{})));

    EXPECT_CALL(*m_mockSender, send)
        .WillOnce(Return(threading::makeReadyFuture(sendResult)));

    const std::shared_ptr<mocks::MockIAccountSynchronizerCallback>
        mockCallback = std::make_shared<
            StrictMock<mocks::MockIAccountSynchronizerCallback>>();

    const auto canceler =
        std::make_shared<utility::cancelers::ManualCanceler>();

    auto syncResult = accountSynchronizer->synchronize(mockCallback, canceler);
    waitForFuture(syncResult);

    ASSERT_EQ(syncResult.resultCount(), 1);
    auto result = syncResult.result();

    // Checking the result

    ASSERT_TRUE(result);

    EXPECT_FALSE(result->userAccountSyncChunksDataCounters());
    EXPECT_FALSE(result->userAccountDownloadNotesStatus());
    EXPECT_FALSE(result->userAccountDownloadResourcesStatus());
    EXPECT_TRUE(result->linkedNotebookSyncChunksDataCounters().isEmpty());
    EXPECT_TRUE(result->linkedNotebookDownloadNotesStatuses().isEmpty());
    EXPECT_TRUE(result->linkedNotebookDownloadResourcesStatuses().isEmpty());

    checkResultSendPart(*result, sendResult, linkedNotebookGuids);

    ASSERT_TRUE(std::holds_alternative<RateLimitReachedError>(
        result->stopSynchronizationError()));

    const auto error =
        std::get<RateLimitReachedError>(result->stopSynchronizationError());
    EXPECT_EQ(error.rateLimitDurationSec, rateLimitDuration);
}

TEST_F(
    AccountSynchronizerTest,
    PropagateRateLimitExceededErrorWhenSendingLinkedNotebookData)
{
    const auto accountSynchronizer = std::make_shared<AccountSynchronizer>(
        m_account, m_mockDownloader, m_mockSender,
        m_mockAuthenticationInfoProvider, m_mockSyncStateStorage,
        m_mockSyncChunksStorage);

    const auto linkedNotebookGuids = generateLinkedNotebookGuids();
    ASSERT_FALSE(linkedNotebookGuids.isEmpty());

    const auto & linkedNotebookGuid = linkedNotebookGuids.constFirst();
    const qint32 rateLimitDuration = 1000;
    auto sendResult = generateSampleSendResult(linkedNotebookGuids);
    {
        auto & linkedNotebookResult =
            sendResult.linkedNotebookResults[linkedNotebookGuid];

        ASSERT_TRUE(linkedNotebookResult);
        linkedNotebookResult->m_stopSynchronizationError =
            StopSynchronizationError{RateLimitReachedError{rateLimitDuration}};
    }

    EXPECT_CALL(*m_mockDownloader, download)
        .WillOnce(Return(threading::makeReadyFuture(IDownloader::Result{})));

    EXPECT_CALL(*m_mockSender, send)
        .WillOnce(Return(threading::makeReadyFuture(sendResult)));

    const std::shared_ptr<mocks::MockIAccountSynchronizerCallback>
        mockCallback = std::make_shared<
            StrictMock<mocks::MockIAccountSynchronizerCallback>>();

    const auto canceler =
        std::make_shared<utility::cancelers::ManualCanceler>();

    auto syncResult = accountSynchronizer->synchronize(mockCallback, canceler);
    waitForFuture(syncResult);

    ASSERT_EQ(syncResult.resultCount(), 1);
    auto result = syncResult.result();

    // Checking the result

    ASSERT_TRUE(result);

    EXPECT_FALSE(result->userAccountSyncChunksDataCounters());
    EXPECT_FALSE(result->userAccountDownloadNotesStatus());
    EXPECT_FALSE(result->userAccountDownloadResourcesStatus());
    EXPECT_TRUE(result->linkedNotebookSyncChunksDataCounters().isEmpty());
    EXPECT_TRUE(result->linkedNotebookDownloadNotesStatuses().isEmpty());
    EXPECT_TRUE(result->linkedNotebookDownloadResourcesStatuses().isEmpty());

    checkResultSendPart(*result, sendResult, linkedNotebookGuids);

    ASSERT_TRUE(std::holds_alternative<RateLimitReachedError>(
        result->stopSynchronizationError()));

    const auto error =
        std::get<RateLimitReachedError>(result->stopSynchronizationError());
    EXPECT_EQ(error.rateLimitDurationSec, rateLimitDuration);
}

TEST_F(AccountSynchronizerTest, PropagateCallbackCallsFromDownloader)
{
    const auto accountSynchronizer = std::make_shared<AccountSynchronizer>(
        m_account, m_mockDownloader, m_mockSender,
        m_mockAuthenticationInfoProvider, m_mockSyncStateStorage,
        m_mockSyncChunksStorage);

    const auto linkedNotebookGuids = generateLinkedNotebookGuids();
    ASSERT_FALSE(linkedNotebookGuids.isEmpty());

    const auto downloadResult =
        generateSampleDownloaderResult(linkedNotebookGuids);

    InSequence s;

    std::shared_ptr<IDownloader::ICallback> downloaderCallback;
    auto downloaderPromise = std::make_shared<QPromise<IDownloader::Result>>();
    downloaderPromise->start();
    EXPECT_CALL(*m_mockDownloader, download)
        .WillOnce([&]([[maybe_unused]] const utility::cancelers::ICancelerPtr &
                          canceler,
                      const IDownloader::ICallbackWeakPtr & callbackWeak) {
            downloaderCallback = callbackWeak.lock();
            return downloaderPromise->future();
        });

    const std::shared_ptr<mocks::MockIAccountSynchronizerCallback>
        mockCallback = std::make_shared<
            StrictMock<mocks::MockIAccountSynchronizerCallback>>();

    const auto canceler =
        std::make_shared<utility::cancelers::ManualCanceler>();

    auto syncResult = accountSynchronizer->synchronize(mockCallback, canceler);
    ASSERT_FALSE(syncResult.isFinished());

    ASSERT_TRUE(downloaderCallback);

    // Checking propagation of callback calls from downloader's callback to
    // AccountSynchronizer's callback
    {
        const qint32 highestDownloadedUsn = 42;
        const qint32 highestServerUsn = 43;
        const qint32 lastPreviousUsn = 41;
        EXPECT_CALL(
            *mockCallback,
            onSyncChunksDownloadProgress(
                highestDownloadedUsn, highestServerUsn, lastPreviousUsn));

        downloaderCallback->onSyncChunksDownloadProgress(
            highestDownloadedUsn, highestServerUsn, lastPreviousUsn);
    }

    const QList<qevercloud::SyncChunk> syncChunks =
        QList<qevercloud::SyncChunk>{} << qevercloud::SyncChunk{};

    EXPECT_CALL(*mockCallback, onSyncChunksDownloaded(syncChunks));
    downloaderCallback->onSyncChunksDownloaded(syncChunks);

    EXPECT_CALL(
        *mockCallback,
        onSyncChunksDataProcessingProgress(
            downloadResult.userOwnResult.syncChunksDataCounters));

    downloaderCallback->onSyncChunksDataProcessingProgress(
        downloadResult.userOwnResult.syncChunksDataCounters);

    {
        const QList<qevercloud::LinkedNotebook> linkedNotebooks = [&] {
            QList<qevercloud::LinkedNotebook> result;
            result.reserve(linkedNotebookGuids.size());
            int i = 0;
            for (const auto & guid: std::as_const(linkedNotebookGuids)) {
                result
                    << qevercloud::LinkedNotebookBuilder{}
                           .setGuid(guid)
                           .setUsername(
                               QString::fromUtf8("Linked notebook #%1").arg(i))
                           .build();
                ++i;
            }
            return result;
        }();

        EXPECT_CALL(
            *mockCallback,
            onStartLinkedNotebooksDataDownloading(linkedNotebooks));

        downloaderCallback->onStartLinkedNotebooksDataDownloading(
            linkedNotebooks);
    }

    const auto & linkedNotebookGuid = linkedNotebookGuids.constFirst();
    const auto linkedNotebook =
        qevercloud::LinkedNotebookBuilder{}
            .setGuid(linkedNotebookGuid)
            .setUsername(QStringLiteral("Linked notebook"))
            .build();

    {
        const qint32 highestDownloadedUsn = 42;
        const qint32 highestServerUsn = 43;
        const qint32 lastPreviousUsn = 41;

        EXPECT_CALL(
            *mockCallback,
            onLinkedNotebookSyncChunksDownloadProgress(
                highestDownloadedUsn, highestServerUsn, lastPreviousUsn,
                linkedNotebook));

        downloaderCallback->onLinkedNotebookSyncChunksDownloadProgress(
            highestDownloadedUsn, highestServerUsn, lastPreviousUsn,
            linkedNotebook);
    }

    EXPECT_CALL(
        *mockCallback,
        onLinkedNotebookSyncChunksDownloaded(linkedNotebook, syncChunks));

    downloaderCallback->onLinkedNotebookSyncChunksDownloaded(
        linkedNotebook, syncChunks);

    EXPECT_CALL(
        *mockCallback,
        onLinkedNotebookSyncChunksDataProcessingProgress(
            downloadResult.userOwnResult.syncChunksDataCounters,
            linkedNotebook));

    downloaderCallback->onLinkedNotebookSyncChunksDataProcessingProgress(
        downloadResult.userOwnResult.syncChunksDataCounters, linkedNotebook);

    {
        const qint32 notesDownloaded = 10;
        const qint32 totalNotesToDownload = 100;
        EXPECT_CALL(
            *mockCallback,
            onNotesDownloadProgress(notesDownloaded, totalNotesToDownload));

        downloaderCallback->onNotesDownloadProgress(
            notesDownloaded, totalNotesToDownload);
    }

    {
        const qint32 notesDownloaded = 10;
        const qint32 totalNotesToDownload = 100;
        EXPECT_CALL(
            *mockCallback,
            onLinkedNotebookNotesDownloadProgress(
                notesDownloaded, totalNotesToDownload, linkedNotebook));

        downloaderCallback->onLinkedNotebookNotesDownloadProgress(
            notesDownloaded, totalNotesToDownload, linkedNotebook);
    }

    {
        const qint32 resourcesDownloaded = 10;
        const qint32 totalResourcesToDownload = 100;
        EXPECT_CALL(
            *mockCallback,
            onResourcesDownloadProgress(
                resourcesDownloaded, totalResourcesToDownload));

        downloaderCallback->onResourcesDownloadProgress(
            resourcesDownloaded, totalResourcesToDownload);
    }

    {
        const qint32 resourcesDownloaded = 10;
        const qint32 totalResourcesToDownload = 100;
        EXPECT_CALL(
            *mockCallback,
            onLinkedNotebookResourcesDownloadProgress(
                resourcesDownloaded, totalResourcesToDownload, linkedNotebook));

        downloaderCallback->onLinkedNotebookResourcesDownloadProgress(
            resourcesDownloaded, totalResourcesToDownload, linkedNotebook);
    }

    expectSetSyncState(downloadResult.syncState);

    EXPECT_CALL(*m_mockSender, send)
        .WillOnce(Return(threading::makeReadyFuture(ISender::Result{})));

    downloaderPromise->addResult(downloadResult);
    downloaderPromise->finish();

    waitForFuture(syncResult);

    ASSERT_EQ(syncResult.resultCount(), 1);
    auto result = syncResult.result();

    // Checking the result
    ASSERT_TRUE(result);

    ASSERT_TRUE(downloadResult.syncState);
    checkResultSyncState(
        *result, *downloadResult.syncState, linkedNotebookGuids);

    checkResultDownloadPart(*result, downloadResult, linkedNotebookGuids);

    EXPECT_FALSE(result->userAccountSendStatus());
    EXPECT_TRUE(result->linkedNotebookSendStatuses().isEmpty());
}

TEST_F(AccountSynchronizerTest, PropagateCallbackCallsFromSender)
{
    const auto accountSynchronizer = std::make_shared<AccountSynchronizer>(
        m_account, m_mockDownloader, m_mockSender,
        m_mockAuthenticationInfoProvider, m_mockSyncStateStorage,
        m_mockSyncChunksStorage);

    const auto linkedNotebookGuids = generateLinkedNotebookGuids();
    ASSERT_FALSE(linkedNotebookGuids.isEmpty());

    const auto sendResult = generateSampleSendResult(linkedNotebookGuids);

    const std::shared_ptr<mocks::MockIAccountSynchronizerCallback>
        mockCallback = std::make_shared<
            StrictMock<mocks::MockIAccountSynchronizerCallback>>();

    EXPECT_CALL(*m_mockDownloader, download)
        .WillOnce(Return(threading::makeReadyFuture(IDownloader::Result{})));

    EXPECT_CALL(*m_mockSender, send)
        .WillOnce([&]([[maybe_unused]] const utility::cancelers::ICancelerPtr &
                          canceler,
                      const ISender::ICallbackWeakPtr & callbackWeak) {
            auto callback = callbackWeak.lock();
            EXPECT_TRUE(callback);
            if (callback) {
                EXPECT_CALL(
                    *mockCallback,
                    onUserOwnSendStatusUpdate(sendResult.userOwnResult));

                callback->onUserOwnSendStatusUpdate(sendResult.userOwnResult);

                const auto & linkedNotebookGuid =
                    linkedNotebookGuids.constFirst();

                EXPECT_CALL(
                    *mockCallback,
                    onLinkedNotebookSendStatusUpdate(
                        linkedNotebookGuid, sendResult.userOwnResult));

                callback->onLinkedNotebookSendStatusUpdate(
                    linkedNotebookGuid, sendResult.userOwnResult);
            }
            return threading::makeReadyFuture(sendResult);
        });

    expectSetSyncState(sendResult.syncState);

    const auto canceler =
        std::make_shared<utility::cancelers::ManualCanceler>();

    auto syncResult = accountSynchronizer->synchronize(mockCallback, canceler);
    waitForFuture(syncResult);

    ASSERT_EQ(syncResult.resultCount(), 1);
    auto result = syncResult.result();

    // Checking the result

    ASSERT_TRUE(result);

    ASSERT_TRUE(sendResult.syncState);
    checkResultSyncState(*result, *sendResult.syncState, linkedNotebookGuids);

    EXPECT_FALSE(result->userAccountSyncChunksDataCounters());
    EXPECT_FALSE(result->userAccountDownloadNotesStatus());
    EXPECT_FALSE(result->userAccountDownloadResourcesStatus());
    EXPECT_TRUE(result->linkedNotebookSyncChunksDataCounters().isEmpty());
    EXPECT_TRUE(result->linkedNotebookDownloadNotesStatuses().isEmpty());
    EXPECT_TRUE(result->linkedNotebookDownloadResourcesStatuses().isEmpty());

    checkResultSendPart(*result, sendResult, linkedNotebookGuids);
}

} // namespace quentier::synchronization::tests
