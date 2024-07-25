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

#include <quentier/synchronization/types/serialization/json/DownloadNotesStatus.h>

#include <synchronization/types/DownloadNotesStatus.h>

#include <quentier/exception/RuntimeError.h>
#include <quentier/utility/UidGenerator.h>

#include <qevercloud/types/builders/NoteBuilder.h>

#include <gtest/gtest.h>

#include <array>

// clazy:excludeall=non-pod-global-static
// clazy:excludeall=returning-void-expression

namespace quentier::synchronization::tests {

class DownloadNotesStatusJsonSerializationTest :
    public testing::TestWithParam<StopSynchronizationError>
{};

constexpr std::array gStopSynchronizationErrors{
    StopSynchronizationError{std::monostate{}},
    StopSynchronizationError{RateLimitReachedError{}},
    StopSynchronizationError{RateLimitReachedError{42}},
    StopSynchronizationError{AuthenticationExpiredError{}},
};

INSTANTIATE_TEST_SUITE_P(
    DownloadNotesStatusJsonSerializationTestInstance,
    DownloadNotesStatusJsonSerializationTest,
    testing::ValuesIn(gStopSynchronizationErrors));

TEST_P(DownloadNotesStatusJsonSerializationTest, SerializeAndDeserialize)
{
    qint32 updateSequenceNumber = 300;
    qint32 noteCounter = 1;

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

    status->m_stopSynchronizationError = GetParam();

    const auto serialized = serializeDownloadNotesStatusToJson(*status);

    const auto deserialized =
        deserializeDownloadNotesStatusFromJson(serialized);
    ASSERT_TRUE(deserialized);

    const auto concreteDeserializedDownloadNotesStatus =
        std::dynamic_pointer_cast<DownloadNotesStatus>(deserialized);
    ASSERT_TRUE(concreteDeserializedDownloadNotesStatus);

    EXPECT_EQ(*concreteDeserializedDownloadNotesStatus, *status);
}

} // namespace quentier::synchronization::tests
