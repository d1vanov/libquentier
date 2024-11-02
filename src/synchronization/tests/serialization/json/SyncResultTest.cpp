/*
 * Copyright 2024 Dmitry Ivanov
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

#include <quentier/synchronization/types/serialization/json/SyncResult.h>

#include <quentier/exception/RuntimeError.h>
#include <quentier/utility/UidGenerator.h>

#include <synchronization/types/DownloadNotesStatus.h>
#include <synchronization/types/DownloadResourcesStatus.h>
#include <synchronization/types/SendStatus.h>
#include <synchronization/types/SyncChunksDataCounters.h>
#include <synchronization/types/SyncResult.h>
#include <synchronization/types/SyncState.h>
#include <synchronization/types/SyncStateBuilder.h>

#include <qevercloud/types/builders/NoteBuilder.h>
#include <qevercloud/types/builders/NotebookBuilder.h>
#include <qevercloud/types/builders/ResourceBuilder.h>
#include <qevercloud/types/builders/SavedSearchBuilder.h>
#include <qevercloud/types/builders/TagBuilder.h>

#include <gtest/gtest.h>

// clazy:excludeall=non-pod-global-static
// clazy:excludeall=returning-void-expression

namespace quentier::synchronization::tests {

namespace {

[[nodiscard]] SyncChunksDataCountersPtr generateSyncChunksDataCounters()
{
    static quint64 counterValue = 42;

    auto syncChunksDataCounters = std::make_shared<SyncChunksDataCounters>();
    syncChunksDataCounters->m_totalSavedSearches = counterValue++;
    syncChunksDataCounters->m_totalExpungedSavedSearches = counterValue++;
    syncChunksDataCounters->m_addedSavedSearches = counterValue++;
    syncChunksDataCounters->m_updatedSavedSearches = counterValue++;
    syncChunksDataCounters->m_expungedSavedSearches = counterValue++;

    syncChunksDataCounters->m_totalTags = counterValue++;
    syncChunksDataCounters->m_totalExpungedTags = counterValue++;
    syncChunksDataCounters->m_addedTags = counterValue++;
    syncChunksDataCounters->m_updatedTags = counterValue++;
    syncChunksDataCounters->m_expungedTags = counterValue++;

    syncChunksDataCounters->m_totalLinkedNotebooks = counterValue++;
    syncChunksDataCounters->m_totalExpungedLinkedNotebooks = counterValue++;
    syncChunksDataCounters->m_addedLinkedNotebooks = counterValue++;
    syncChunksDataCounters->m_updatedLinkedNotebooks = counterValue++;
    syncChunksDataCounters->m_expungedLinkedNotebooks = counterValue++;

    syncChunksDataCounters->m_totalNotebooks = counterValue++;
    syncChunksDataCounters->m_totalExpungedNotebooks = counterValue++;
    syncChunksDataCounters->m_addedNotebooks = counterValue++;
    syncChunksDataCounters->m_updatedNotebooks = counterValue++;
    syncChunksDataCounters->m_expungedNotebooks = counterValue++;

    return syncChunksDataCounters;
}

[[nodiscard]] DownloadNotesStatusPtr generateDownloadNotesStatus()
{
    static qint32 updateSequenceNumber = 300;
    static qint32 noteCounter = 1;

    const auto generateNote = [&] {
        return qevercloud::NoteBuilder{}
            .setLocalId(UidGenerator::Generate())
            .setGuid(UidGenerator::Generate())
            .setTitle(QString::fromUtf8("Note #%1").arg(noteCounter++))
            .setUpdateSequenceNum(updateSequenceNumber++)
            .build();
    };

    auto status = std::make_shared<DownloadNotesStatus>();
    status->m_totalNewNotes = 42;
    status->m_totalUpdatedNotes = 43;
    status->m_totalExpungedNotes = 44;

    constexpr int itemCount = 3;

    status->m_notesWhichFailedToDownload.reserve(itemCount);
    for (int i = 0; i < itemCount; ++i) {
        status->m_notesWhichFailedToDownload
            << IDownloadNotesStatus::NoteWithException{
                   generateNote(),
                   std::make_shared<RuntimeError>(
                       ErrorString{QStringLiteral("Failed to download note")})};
    }

    status->m_notesWhichFailedToProcess.reserve(itemCount);
    for (int i = 0; i < itemCount; ++i) {
        status->m_notesWhichFailedToProcess
            << IDownloadNotesStatus::NoteWithException{
                   generateNote(),
                   std::make_shared<RuntimeError>(
                       ErrorString{QStringLiteral("Failed to process note")})};
    }

    status->m_noteGuidsWhichFailedToExpunge.reserve(itemCount);
    for (int i = 0; i < itemCount; ++i) {
        status->m_noteGuidsWhichFailedToExpunge
            << IDownloadNotesStatus::GuidWithException{
                   UidGenerator::Generate(),
                   std::make_shared<RuntimeError>(
                       ErrorString{QStringLiteral("Failed to expunge note")})};
    }

    status->m_processedNoteGuidsAndUsns.reserve(itemCount);
    for (int i = 0; i < itemCount; ++i) {
        status->m_processedNoteGuidsAndUsns[UidGenerator::Generate()] =
            updateSequenceNumber++;
    }

    status->m_cancelledNoteGuidsAndUsns.reserve(itemCount);
    for (int i = 0; i < itemCount; ++i) {
        status->m_cancelledNoteGuidsAndUsns[UidGenerator::Generate()] =
            updateSequenceNumber++;
    }

    status->m_expungedNoteGuids.reserve(itemCount);
    for (int i = 0; i < itemCount; ++i) {
        status->m_expungedNoteGuids << UidGenerator::Generate();
    }

    status->m_stopSynchronizationError =
        StopSynchronizationError{RateLimitReachedError{42}};

    return status;
}

[[nodiscard]] DownloadResourcesStatusPtr generateDownloadResourcesStatus()
{
    static qint32 updateSequenceNumber = 300;

    const auto generateResource = [&] {
        return qevercloud::ResourceBuilder{}
            .setLocalId(UidGenerator::Generate())
            .setGuid(UidGenerator::Generate())
            .setUpdateSequenceNum(updateSequenceNumber++)
            .build();
    };

    auto status = std::make_shared<DownloadResourcesStatus>();
    status->m_totalNewResources = 42;
    status->m_totalUpdatedResources = 43;

    constexpr int itemCount = 3;

    status->m_resourcesWhichFailedToDownload.reserve(itemCount);
    for (int i = 0; i < itemCount; ++i) {
        status->m_resourcesWhichFailedToDownload
            << IDownloadResourcesStatus::ResourceWithException{
                   generateResource(),
                   std::make_shared<RuntimeError>(ErrorString{
                       QStringLiteral("Failed to download resource")})};
    }

    status->m_resourcesWhichFailedToProcess.reserve(itemCount);
    for (int i = 0; i < itemCount; ++i) {
        status->m_resourcesWhichFailedToProcess
            << IDownloadResourcesStatus::ResourceWithException{
                   generateResource(),
                   std::make_shared<RuntimeError>(ErrorString{
                       QStringLiteral("Failed to process resource")})};
    }

    status->m_processedResourceGuidsAndUsns.reserve(itemCount);
    for (int i = 0; i < itemCount; ++i) {
        status->m_processedResourceGuidsAndUsns[UidGenerator::Generate()] =
            updateSequenceNumber++;
    }

    status->m_cancelledResourceGuidsAndUsns.reserve(itemCount);
    for (int i = 0; i < itemCount; ++i) {
        status->m_cancelledResourceGuidsAndUsns[UidGenerator::Generate()] =
            updateSequenceNumber++;
    }

    status->m_stopSynchronizationError =
        StopSynchronizationError{AuthenticationExpiredError{}};

    return status;
}

[[nodiscard]] SendStatusPtr generateSendStatus()
{
    static quint64 counter = 42UL;
    static quint32 noteCounter = 1U;
    static quint32 notebookCounter = 1U;
    static quint32 savedSearchCounter = 1U;
    static quint32 tagCounter = 1U;
    static quint32 exceptionCounter = 1U;
    static qint32 usn = 900;

    const auto sendStatus = std::make_shared<SendStatus>();
    sendStatus->m_totalAttemptedToSendNotes = counter++;
    sendStatus->m_totalAttemptedToSendNotebooks = counter++;
    sendStatus->m_totalAttemptedToSendSavedSearches = counter++;
    sendStatus->m_totalAttemptedToSendTags = counter++;

    sendStatus->m_totalSuccessfullySentNotes = counter++;
    sendStatus->m_totalSuccessfullySentNotebooks = counter++;
    sendStatus->m_totalSuccessfullySentSavedSearches = counter++;
    sendStatus->m_totalSuccessfullySentTags = counter++;

    constexpr int itemCount = 3;
    for (int i = 0; i < itemCount; ++i) {
        sendStatus->m_failedToSendNotes << ISendStatus::NoteWithException{
            qevercloud::NoteBuilder{}
                .setLocalId(UidGenerator::Generate())
                .setGuid(UidGenerator::Generate())
                .setTitle(QString::fromUtf8("Note #%1").arg(noteCounter++))
                .setUpdateSequenceNum(usn++)
                .build(),
            std::make_shared<RuntimeError>(ErrorString(
                QString::fromUtf8("Exception #%1").arg(exceptionCounter++)))};
    }

    for (int i = 0; i < itemCount; ++i) {
        sendStatus->m_failedToSendNotebooks
            << ISendStatus::NotebookWithException{
                   qevercloud::NotebookBuilder{}
                       .setLocalId(UidGenerator::Generate())
                       .setGuid(UidGenerator::Generate())
                       .setName(QString::fromUtf8("Notebook #%1")
                                    .arg(notebookCounter++))
                       .setUpdateSequenceNum(usn++)
                       .build(),
                   std::make_shared<RuntimeError>(
                       ErrorString(QString::fromUtf8("Exception #%1")
                                       .arg(exceptionCounter++)))};
    }

    for (int i = 0; i < itemCount; ++i) {
        sendStatus->m_failedToSendSavedSearches
            << ISendStatus::SavedSearchWithException{
                   qevercloud::SavedSearchBuilder{}
                       .setLocalId(UidGenerator::Generate())
                       .setGuid(UidGenerator::Generate())
                       .setName(QString::fromUtf8("Saved search #%1")
                                    .arg(savedSearchCounter++))
                       .setUpdateSequenceNum(usn++)
                       .build(),
                   std::make_shared<RuntimeError>(
                       ErrorString(QString::fromUtf8("Exception #%1")
                                       .arg(exceptionCounter++)))};
    }

    for (int i = 0; i < itemCount; ++i) {
        sendStatus->m_failedToSendTags << ISendStatus::TagWithException{
            qevercloud::TagBuilder{}
                .setLocalId(UidGenerator::Generate())
                .setGuid(UidGenerator::Generate())
                .setName(QString::fromUtf8("Tag #%1").arg(tagCounter++))
                .setUpdateSequenceNum(usn++)
                .build(),
            std::make_shared<RuntimeError>(ErrorString(
                QString::fromUtf8("Exception #%1").arg(exceptionCounter++)))};
    }

    return sendStatus;
}

} // namespace

TEST(SyncResultJsonSerializationTest, SerializeAndDeserializeSyncResult)
{
    auto syncResult = std::make_shared<SyncResult>();

    syncResult->m_syncState = std::dynamic_pointer_cast<SyncState>(
        SyncStateBuilder{}
            .setUserDataUpdateCount(43)
            .setUserDataLastSyncTime(qevercloud::Timestamp{1721405555000})
            .setLinkedNotebookUpdateCounts(QHash<qevercloud::Guid, qint32>{
                {UidGenerator::Generate(), 44},
                {UidGenerator::Generate(), 45},
                {UidGenerator::Generate(), 46},
            })
            .setLinkedNotebookLastSyncTimes(
                QHash<qevercloud::Guid, qevercloud::Timestamp>{
                    {UidGenerator::Generate(),
                     qevercloud::Timestamp{1721405556000}},
                    {UidGenerator::Generate(),
                     qevercloud::Timestamp{1721405557000}},
                    {UidGenerator::Generate(),
                     qevercloud::Timestamp{1721405558000}},
                })
            .build());
    ASSERT_TRUE(syncResult->m_syncState);

    syncResult->m_userAccountSyncChunksDataCounters =
        generateSyncChunksDataCounters();

    constexpr int linkedNotebookCount = 3;
    syncResult->m_linkedNotebookSyncChunksDataCounters.reserve(
        linkedNotebookCount);
    for (int i = 0; i < linkedNotebookCount; ++i) {
        const auto guid = UidGenerator::Generate();
        syncResult->m_linkedNotebookSyncChunksDataCounters[guid] =
            generateSyncChunksDataCounters();
    }

    syncResult->m_userAccountDownloadNotesStatus =
        generateDownloadNotesStatus();

    syncResult->m_linkedNotebookDownloadNotesStatuses.reserve(
        linkedNotebookCount);
    for (int i = 0; i < linkedNotebookCount; ++i) {
        const auto guid = UidGenerator::Generate();
        syncResult->m_linkedNotebookDownloadNotesStatuses[guid] =
            generateDownloadNotesStatus();
    }

    syncResult->m_userAccountDownloadResourcesStatus =
        generateDownloadResourcesStatus();

    syncResult->m_linkedNotebookDownloadResourcesStatuses.reserve(
        linkedNotebookCount);

    for (int i = 0; i < linkedNotebookCount; ++i) {
        const auto guid = UidGenerator::Generate();
        syncResult->m_linkedNotebookDownloadResourcesStatuses[guid] =
            generateDownloadResourcesStatus();
    }

    syncResult->m_userAccountSendStatus = generateSendStatus();
    syncResult->m_linkedNotebookSendStatuses.reserve(linkedNotebookCount);
    for (int i = 0; i < linkedNotebookCount; ++i) {
        const auto guid = UidGenerator::Generate();
        syncResult->m_linkedNotebookSendStatuses[guid] = generateSendStatus();
    }

    syncResult->m_stopSynchronizationError =
        StopSynchronizationError{RateLimitReachedError{}};

    const auto serialized = serializeSyncResultToJson(*syncResult);

    const auto deserialized = deserializeSyncResultFromJson(serialized);
    ASSERT_TRUE(deserialized);

    const auto concreteDeserializedSyncResult =
#ifdef Q_OS_MAC
        // NOTE: on macOS dynamic_cast across the shared library's boundary
        // is problematic, see
        // https://www.qt.io/blog/quality-assurance/one-way-dynamic_cast-across-library-boundaries-can-fail-and-how-to-fix-it
        // Using reinterpret_cast instead.
        std::reinterpret_pointer_cast<SyncResult>(deserialized);
#else
        std::dynamic_pointer_cast<SyncResult>(deserialized);
#endif
    ASSERT_TRUE(concreteDeserializedSyncResult);

    EXPECT_EQ(*concreteDeserializedSyncResult, *syncResult);
}

} // namespace quentier::synchronization::tests
