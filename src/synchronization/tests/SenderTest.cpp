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

#include <synchronization/Sender.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/local_storage/tests/mocks/MockILocalStorage.h>
#include <quentier/synchronization/tests/mocks/MockISyncStateStorage.h>
#include <quentier/utility/UidGenerator.h>

#include <synchronization/tests/mocks/MockINoteStoreProvider.h>

#include <qevercloud/DurableService.h>
#include <qevercloud/RequestContext.h>
#include <qevercloud/types/builders/LinkedNotebookBuilder.h>
#include <qevercloud/types/builders/NoteBuilder.h>
#include <qevercloud/types/builders/NotebookBuilder.h>
#include <qevercloud/types/builders/SavedSearchBuilder.h>
#include <qevercloud/types/builders/TagBuilder.h>

#include <QFlags>
#include <QHash>
#include <QList>

#include <gtest/gtest.h>

#include <algorithm>

// clazy:excludeall=non-pod-global-static
// clazy:excludeall=returning-void-expression

namespace quentier::synchronization::tests {

using testing::StrictMock;

enum class SenderTestFlag
{
    WithNewSavedSearches = 1 << 0,
    WithUpdatedSavedSearches = 1 << 2,
    WithNewUserOwnNotebooks = 1 << 3,
    WithUpdatedUserOwnNotebooks = 1 << 4,
    WithNewUserOwnNotes = 1 << 5,
    WithUpdatedUserOwnNotes = 1 << 6,
    WithNewUserOwnTags = 1 << 7,
    WithUpdatedUserOwnTags = 1 << 8,
    WithUpdatedLinkedNotebooks = 1 << 9,
    WithNewLinkedNotebooksNotes = 1 << 10,
    WithUpdatedLinkedNotebooksNotes = 1 << 11,
    WithNewLinkedNotebooksTags = 1 << 12,
    WithUpdatedLinkedNotebooksTags = 1 << 13,
};

Q_DECLARE_FLAGS(SenderTestFlags, SenderTestFlag);

struct SenderTestData
{
    QList<qevercloud::SavedSearch> m_newSavedSearches;
    QList<qevercloud::SavedSearch> m_updatedSavedSearches;

    QList<qevercloud::Notebook> m_newUserOwnNotebooks;
    QList<qevercloud::Notebook> m_updatedUserOwnNotebooks;
    QList<qevercloud::Note> m_newUserOwnNotes;
    QList<qevercloud::Note> m_updatedUserOwnNotes;
    QList<qevercloud::Tag> m_newUserOwnTags;
    QList<qevercloud::Tag> m_updatedUserOwnTags;

    QList<qevercloud::LinkedNotebook> m_linkedNotebooks;

    QList<qevercloud::Notebook> m_updatedLinkedNotebooks;
    QList<qevercloud::Note> m_newLinkedNotebooksNotes;
    QList<qevercloud::Note> m_updatedLinkedNotebooksNotes;
    QList<qevercloud::Tag> m_newLinkedNotebooksTags;
    QList<qevercloud::Tag> m_updatedLinkedNotebooksTags;
};

enum class WithEvernoteFields
{
    Yes,
    No
};

[[nodiscard]] qevercloud::SavedSearch generateSavedSearch(
    const int index, const WithEvernoteFields withEvernoteFields, qint32 & usn)
{
    qevercloud::SavedSearchBuilder builder;
    builder
        .setLocalId(UidGenerator::Generate())
        .setName(
            (withEvernoteFields == WithEvernoteFields::Yes)
            ? QString::fromUtf8("Updated saved search #%1").arg(index + 1)
            : QString::fromUtf8("New saved search #%1").arg(index + 1))
        .setQuery(QStringLiteral("query"));

    if (withEvernoteFields == WithEvernoteFields::Yes) {
        builder.setUpdateSequenceNum(usn++);
        builder.setGuid(UidGenerator::Generate());
    }

    return builder.build();
}

[[nodiscard]] qevercloud::Notebook generateNotebook(
    const int index, const WithEvernoteFields withEvernoteFields, qint32 & usn)
{
    qevercloud::NotebookBuilder builder;
    builder
        .setLocalId(UidGenerator::Generate())
        .setName(
            (withEvernoteFields == WithEvernoteFields::Yes)
            ? QString::fromUtf8("Updated notebook #%1").arg(index + 1)
            : QString::fromUtf8("New notebook #%1").arg(index + 1));

    if (withEvernoteFields == WithEvernoteFields::Yes) {
        builder.setUpdateSequenceNum(usn++);
        builder.setGuid(UidGenerator::Generate());
    }

    return builder.build();
}

[[nodiscard]] qevercloud::Note generateNote(
    const int index, const WithEvernoteFields withEvernoteFields,
    const SenderTestData & testData, const SenderTestFlags flags,
    qint32 & usn)
{
    qevercloud::NoteBuilder builder;
    builder.setLocalId(UidGenerator::Generate())
        .setTitle(
            (withEvernoteFields == WithEvernoteFields::Yes
             ? QString::fromUtf8("Updated note #%1").arg(index + 1)
             : QString::fromUtf8("New note #%1").arg(index + 1)));

    if (flags.testFlag(SenderTestFlag::WithNewUserOwnNotebooks)) {
        Q_ASSERT(testData.m_newUserOwnNotebooks.size() > index);
        builder
            .setNotebookLocalId(
                testData.m_newUserOwnNotebooks[index].localId())
            .setNotebookGuid(testData.m_newUserOwnNotebooks[index].guid());
    }
    else if (
        flags.testFlag(SenderTestFlag::WithUpdatedUserOwnNotebooks)) {
        Q_ASSERT(testData.m_updatedUserOwnNotebooks.size() > index);
        builder
            .setNotebookLocalId(
                testData.m_updatedUserOwnNotebooks[index].localId())
            .setNotebookGuid(
                testData.m_updatedUserOwnNotebooks[index].guid());
    }
    else {
        builder.setNotebookLocalId(UidGenerator::Generate());
        builder.setNotebookGuid(UidGenerator::Generate());
    }

    if (flags.testFlag(SenderTestFlag::WithNewUserOwnTags)) {
        QStringList tagLocalIds = [&] {
            QStringList res;
            res.reserve(testData.m_newUserOwnTags.size());
            for (const auto & tag : qAsConst(testData.m_newUserOwnTags)) {
                res << tag.localId();
            }
            return res;
        }();
        builder.setTagLocalIds(std::move(tagLocalIds));
    }
    else if (flags.testFlag(SenderTestFlag::WithUpdatedUserOwnTags)) {
        QStringList tagLocalIds = [&] {
            QStringList res;
            res.reserve(testData.m_updatedUserOwnTags.size());
            for (const auto & tag : qAsConst(testData.m_updatedUserOwnTags)) {
                res << tag.localId();
            }
            return res;
        }();
        builder.setTagLocalIds(std::move(tagLocalIds));

        QList<qevercloud::Guid> tagGuids = [&] {
            QList<qevercloud::Guid> res;
            res.reserve(testData.m_updatedUserOwnTags.size());
            for (const auto & tag : qAsConst(testData.m_updatedUserOwnTags)) {
                res << tag.guid().value();
            }
            return res;
        }();
        builder.setTagGuids(std::move(tagGuids));
    }

    if (withEvernoteFields == WithEvernoteFields::Yes) {
        builder.setUpdateSequenceNum(usn++);
        builder.setGuid(UidGenerator::Generate());
    }

    return builder.build();
}

[[nodiscard]] qevercloud::Tag generateTag(
    const int index, const WithEvernoteFields withEvernoteFields,
    const QList<qevercloud::Tag> & previousTags, qint32 & usn)
{
    qevercloud::TagBuilder builder;
    builder
        .setLocalId(UidGenerator::Generate())
        .setName(
            (withEvernoteFields == WithEvernoteFields::Yes)
            ? QString::fromUtf8("Updated tag #%1").arg(index + 1)
            : QString::fromUtf8("New tag #%1").arg(index + 1));

    if (!previousTags.isEmpty()) {
        builder.setParentTagLocalId(previousTags.constLast().localId());
    }

    if (withEvernoteFields == WithEvernoteFields::Yes) {
        builder.setUpdateSequenceNum(usn++);
        builder.setGuid(UidGenerator::Generate());

        if (!previousTags.isEmpty()) {
            builder.setParentGuid(previousTags.constLast().guid());
        }
    }

    return builder.build();
}

[[nodiscard]] SenderTestData generateTestData(
    const SenderTestFlags flags, const int itemCount = 3)
{
    SenderTestData result;
    qint32 usn = 42;

    if (flags.testFlag(SenderTestFlag::WithNewSavedSearches)) {
        result.m_newSavedSearches.reserve(itemCount);
        for (int i = 0; i < itemCount; ++i) {
            result.m_newSavedSearches
                << generateSavedSearch(i, WithEvernoteFields::No, usn);
        }
    }

    if (flags.testFlag(SenderTestFlag::WithUpdatedSavedSearches)) {
        result.m_updatedSavedSearches.reserve(itemCount);
        for (int i = 0; i < itemCount; ++i) {
            result.m_updatedSavedSearches
                << generateSavedSearch(i, WithEvernoteFields::Yes, usn);
        }
    }

    if (flags.testFlag(SenderTestFlag::WithNewUserOwnNotebooks)) {
        result.m_newUserOwnNotebooks.reserve(itemCount);
        for (int i = 0; i < itemCount; ++i) {
            result.m_newUserOwnNotebooks
                << generateNotebook(i, WithEvernoteFields::No, usn);
        }
    }

    if (flags.testFlag(SenderTestFlag::WithUpdatedUserOwnNotebooks)) {
        result.m_updatedUserOwnNotebooks.reserve(itemCount);
        for (int i = 0; i < itemCount; ++i) {
            result.m_updatedUserOwnNotebooks
                << generateNotebook(i, WithEvernoteFields::Yes, usn);
        }
    }

    if (flags.testFlag(SenderTestFlag::WithNewUserOwnNotes)) {
        result.m_newUserOwnNotes.reserve(itemCount);
        for (int i = 0; i < itemCount; ++i) {
            result.m_newUserOwnNotes
                << generateNote(
                    i, WithEvernoteFields::No, result, flags, usn);
        }
    }

    if (flags.testFlag(SenderTestFlag::WithUpdatedUserOwnNotes)) {
        result.m_updatedUserOwnNotes.reserve(itemCount);
        for (int i = 0; i < itemCount; ++i) {
            result.m_updatedUserOwnNotes
                << generateNote(
                    i, WithEvernoteFields::Yes, result, flags, usn);
        }
    }

    if (flags.testFlag(SenderTestFlag::WithNewUserOwnTags)) {
        result.m_newUserOwnTags.reserve(itemCount);
        for (int i = 0; i < itemCount; ++i) {
            result.m_newUserOwnTags << generateTag(
                i, WithEvernoteFields::No, result.m_newUserOwnTags, usn);
        }
        // Putting child tags first to ensure in test that parents would be sent
        // first
        std::reverse(
            result.m_newUserOwnTags.begin(),
            result.m_newUserOwnTags.end());
    }

    if (flags.testFlag(SenderTestFlag::WithUpdatedUserOwnTags)) {
        result.m_updatedUserOwnTags.reserve(itemCount);
        for (int i = 0; i < itemCount; ++i) {
            result.m_updatedUserOwnTags << generateTag(
                i, WithEvernoteFields::Yes, result.m_updatedUserOwnTags, usn);
        }
        // Putting child tags first to ensure in test that parents would be sent
        // first
        std::reverse(
            result.m_updatedUserOwnTags.begin(),
            result.m_updatedUserOwnTags.end());
    }

    // TODO: generate linked notebooks and their corresponding fields

    return result;
}

class SenderTest : public testing::Test
{
protected:
    const Account m_account = Account{
        QStringLiteral("Full Name"),
        Account::Type::Evernote,
        qevercloud::UserID{42},
        Account::EvernoteAccountType::Free,
        QStringLiteral("www.evernote.com"),
        QStringLiteral("shard id")};

    const std::shared_ptr<local_storage::tests::mocks::MockILocalStorage>
        m_mockLocalStorage = std::make_shared<
            StrictMock<local_storage::tests::mocks::MockILocalStorage>>();

    const std::shared_ptr<mocks::MockISyncStateStorage> m_mockSyncStateStorage =
        std::make_shared<StrictMock<mocks::MockISyncStateStorage>>();

    const std::shared_ptr<mocks::MockINoteStoreProvider>
        m_mockNoteStoreProvider =
            std::make_shared<StrictMock<mocks::MockINoteStoreProvider>>();
};

TEST_F(SenderTest, Ctor)
{
    EXPECT_NO_THROW(
        const auto sender = std::make_shared<Sender>(
            m_account, m_mockLocalStorage, m_mockSyncStateStorage,
            m_mockNoteStoreProvider, qevercloud::newRequestContext(),
            qevercloud::newRetryPolicy()));
}

TEST_F(SenderTest, CtorEmptyAccount)
{
    EXPECT_THROW(
        const auto sender = std::make_shared<Sender>(
            Account{}, m_mockLocalStorage, m_mockSyncStateStorage,
            m_mockNoteStoreProvider, qevercloud::newRequestContext(),
            qevercloud::newRetryPolicy()),
        InvalidArgument);
}

TEST_F(SenderTest, CtorNullLocalStorage)
{
    EXPECT_THROW(
        const auto sender = std::make_shared<Sender>(
            m_account, nullptr, m_mockSyncStateStorage, m_mockNoteStoreProvider,
            qevercloud::newRequestContext(), qevercloud::newRetryPolicy()),
        InvalidArgument);
}

TEST_F(SenderTest, CtorNullSyncStateStorage)
{
    EXPECT_THROW(
        const auto sender = std::make_shared<Sender>(
            m_account, m_mockLocalStorage, nullptr, m_mockNoteStoreProvider,
            qevercloud::newRequestContext(), qevercloud::newRetryPolicy()),
        InvalidArgument);
}

TEST_F(SenderTest, CtorNullNoteStoreProvider)
{
    EXPECT_THROW(
        const auto sender = std::make_shared<Sender>(
            m_account, m_mockLocalStorage, m_mockSyncStateStorage, nullptr,
            qevercloud::newRequestContext(), qevercloud::newRetryPolicy()),
        InvalidArgument);
}

TEST_F(SenderTest, CtorNullRequestContext)
{
    EXPECT_NO_THROW(
        const auto sender = std::make_shared<Sender>(
            m_account, m_mockLocalStorage, m_mockSyncStateStorage,
            m_mockNoteStoreProvider, nullptr, qevercloud::newRetryPolicy()));
}

TEST_F(SenderTest, CtorNullRetryPolicy)
{
    EXPECT_NO_THROW(
        const auto sender = std::make_shared<Sender>(
            m_account, m_mockLocalStorage, m_mockSyncStateStorage,
            m_mockNoteStoreProvider, qevercloud::newRequestContext(), nullptr));
}

} // namespace quentier::synchronization::tests
