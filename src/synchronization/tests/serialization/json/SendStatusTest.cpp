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

#include <quentier/synchronization/types/serialization/json/SendStatus.h>

#include <quentier/exception/RuntimeError.h>
#include <quentier/utility/UidGenerator.h>

#include <synchronization/types/SendStatus.h>

#include <qevercloud/types/builders/NoteBuilder.h>
#include <qevercloud/types/builders/NotebookBuilder.h>
#include <qevercloud/types/builders/SavedSearchBuilder.h>
#include <qevercloud/types/builders/TagBuilder.h>

#include <gtest/gtest.h>

#include <array>

// clazy:excludeall=non-pod-global-static
// clazy:excludeall=returning-void-expression

namespace quentier::synchronization::tests {

struct TestData
{
    const char * m_testName = nullptr;
    StopSynchronizationError m_stopSynchronizationError{std::monostate{}};
    int m_failedToSendNoteCount = 0;
    int m_failedToSendNotebookCount = 0;
    int m_failedToSendSavedSearchCount = 0;
    int m_failedToSendTagCount = 0;
};

class SendStatusJsonSerializationTest : public testing::TestWithParam<TestData>
{};

constexpr std::array gTestData{
    TestData{
        "No stop sync error and no failed items",
        StopSynchronizationError{std::monostate{}},
        0,
        0,
        0,
        0,
    },
    TestData{
        "No stop sync error and failed notes",
        StopSynchronizationError{std::monostate{}},
        5,
        0,
        0,
        0,
    },
    TestData{
        "No stop sync error and failed notebooks",
        StopSynchronizationError{std::monostate{}},
        0,
        5,
        0,
        0,
    },
    TestData{
        "No stop sync error and failed saved searches",
        StopSynchronizationError{std::monostate{}},
        0,
        0,
        5,
        0,
    },
    TestData{
        "No stop sync error and failed tags",
        StopSynchronizationError{std::monostate{}},
        0,
        0,
        5,
        0,
    },
    TestData{
        "No stop sync error and failed items",
        StopSynchronizationError{std::monostate{}},
        5,
        5,
        5,
        5,
    },
    TestData{
        "Auth expired error and no failed items",
        StopSynchronizationError{AuthenticationExpiredError{}},
        0,
        0,
        0,
        0,
    },
    TestData{
        "Auth expired error and failed notes",
        StopSynchronizationError{AuthenticationExpiredError{}},
        5,
        0,
        0,
        0,
    },
    TestData{
        "Auth expired error and failed notebooks",
        StopSynchronizationError{AuthenticationExpiredError{}},
        0,
        5,
        0,
        0,
    },
    TestData{
        "Auth expired error and failed saved searches",
        StopSynchronizationError{AuthenticationExpiredError{}},
        0,
        0,
        5,
        0,
    },
    TestData{
        "Auth expired error and failed tags",
        StopSynchronizationError{AuthenticationExpiredError{}},
        0,
        0,
        5,
        0,
    },
    TestData{
        "Auth expired error and failed items",
        StopSynchronizationError{AuthenticationExpiredError{}},
        5,
        5,
        5,
        5,
    },
    TestData{
        "Rate limit reached error and no failed items",
        StopSynchronizationError{RateLimitReachedError{}},
        0,
        0,
        0,
        0,
    },
    TestData{
        "Rate limit reached error and failed notes",
        StopSynchronizationError{RateLimitReachedError{}},
        5,
        0,
        0,
        0,
    },
    TestData{
        "Rate limit reached and failed notebooks",
        StopSynchronizationError{RateLimitReachedError{}},
        0,
        5,
        0,
        0,
    },
    TestData{
        "Rate limit reached error and failed saved searches",
        StopSynchronizationError{RateLimitReachedError{}},
        0,
        0,
        5,
        0,
    },
    TestData{
        "Rate limit reached error and failed tags",
        StopSynchronizationError{RateLimitReachedError{}},
        0,
        0,
        5,
        0,
    },
    TestData{
        "Rate limit reached error and failed items",
        StopSynchronizationError{RateLimitReachedError{}},
        5,
        5,
        5,
        5,
    },
};

INSTANTIATE_TEST_SUITE_P(
    SendStatusJsonSerializationTestInstance, SendStatusJsonSerializationTest,
    testing::ValuesIn(gTestData));

TEST_P(SendStatusJsonSerializationTest, SerializeAndDeserializeSendStatus)
{
    const auto & testData = GetParam();
    quint64 counter = 42UL;
    quint32 noteCounter = 1U;
    quint32 notebookCounter = 1U;
    quint32 savedSearchCounter = 1U;
    quint32 tagCounter = 1U;
    quint32 exceptionCounter = 1U;
    qint32 usn = 900;

    const auto sendStatus = std::make_shared<SendStatus>();
    sendStatus->m_totalAttemptedToSendNotes = counter++;
    sendStatus->m_totalAttemptedToSendNotebooks = counter++;
    sendStatus->m_totalAttemptedToSendSavedSearches = counter++;
    sendStatus->m_totalAttemptedToSendTags = counter++;

    sendStatus->m_totalSuccessfullySentNotes = counter++;
    sendStatus->m_totalSuccessfullySentNotebooks = counter++;
    sendStatus->m_totalSuccessfullySentSavedSearches = counter++;
    sendStatus->m_totalSuccessfullySentTags = counter++;

    for (int i = 0; i < testData.m_failedToSendNoteCount; ++i) {
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

    for (int i = 0; i < testData.m_failedToSendNotebookCount; ++i) {
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

    for (int i = 0; i < testData.m_failedToSendSavedSearchCount; ++i) {
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

    for (int i = 0; i < testData.m_failedToSendTagCount; ++i) {
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

    sendStatus->m_stopSynchronizationError =
        testData.m_stopSynchronizationError;

    const auto serialized = serializeSendStatusToJson(*sendStatus);

    const auto deserialized = deserializeSendStatusFromJson(serialized);
    ASSERT_TRUE(deserialized) << testData.m_testName;

    const auto concreteDeserializedSendStatus =
        std::dynamic_pointer_cast<SendStatus>(deserialized);
    ASSERT_TRUE(concreteDeserializedSendStatus) << testData.m_testName;

    EXPECT_EQ(*concreteDeserializedSendStatus, *sendStatus)
        << testData.m_testName;
}

} // namespace quentier::synchronization::tests
