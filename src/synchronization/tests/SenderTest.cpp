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
#include <quentier/exception/RuntimeError.h>
#include <quentier/local_storage/tests/mocks/MockILocalStorage.h>
#include <quentier/synchronization/tests/mocks/MockISyncStateStorage.h>
#include <quentier/synchronization/types/Errors.h>
#include <quentier/threading/Future.h>
#include <quentier/utility/UidGenerator.h>
#include <quentier/utility/Unreachable.h>
#include <quentier/utility/cancelers/ManualCanceler.h>

#include <synchronization/tests/mocks/MockINoteStoreProvider.h>
#include <synchronization/tests/mocks/qevercloud/services/MockINoteStore.h>
#include <synchronization/types/SyncState.h>

#include <qevercloud/DurableService.h>
#include <qevercloud/RequestContext.h>
#include <qevercloud/exceptions/builders/EDAMSystemExceptionBuilder.h>
#include <qevercloud/types/builders/LinkedNotebookBuilder.h>
#include <qevercloud/types/builders/NoteBuilder.h>
#include <qevercloud/types/builders/NotebookBuilder.h>
#include <qevercloud/types/builders/SavedSearchBuilder.h>
#include <qevercloud/types/builders/TagBuilder.h>
#include <qevercloud/utility/ToRange.h>

#include <QDateTime>
#include <QFlags>
#include <QHash>
#include <QList>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>

// clazy:excludeall=non-pod-global-static
// clazy:excludeall=returning-void-expression

namespace quentier::synchronization::tests {

using testing::AtMost;
using testing::Return;
using testing::ReturnRef;
using testing::StrictMock;

using MockNoteStorePtr = std::shared_ptr<mocks::qevercloud::MockINoteStore>;

namespace {

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

    mutable qint32 m_maxUserOwnUsn = 0;
    mutable QHash<qevercloud::Guid, qint32> m_maxLinkedNotebookUsns;
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
    builder.setLocalId(UidGenerator::Generate())
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
    builder.setLocalId(UidGenerator::Generate())
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
    const QList<qevercloud::Notebook> & newNotebooks,
    const QList<qevercloud::Notebook> & updatedNotebooks,
    const QList<qevercloud::Tag> & newTags,
    const QList<qevercloud::Tag> & updatedTags, qint32 & usn)
{
    qevercloud::NoteBuilder builder;
    builder.setLocalId(UidGenerator::Generate())
        .setTitle(
            (withEvernoteFields == WithEvernoteFields::Yes
                 ? QString::fromUtf8("Updated note #%1").arg(index + 1)
                 : QString::fromUtf8("New note #%1").arg(index + 1)));

    if (newNotebooks.size() > index) {
        builder.setNotebookLocalId(newNotebooks[index].localId())
            .setNotebookGuid(newNotebooks[index].guid());
    }
    else if (updatedNotebooks.size() > index) {
        builder.setNotebookLocalId(updatedNotebooks[index].localId())
            .setNotebookGuid(updatedNotebooks[index].guid());
    }
    else {
        builder.setNotebookLocalId(UidGenerator::Generate());
        builder.setNotebookGuid(UidGenerator::Generate());
    }

    if (!newTags.isEmpty()) {
        QStringList tagLocalIds = [&] {
            QStringList res;
            res.reserve(newTags.size());
            for (const auto & tag: qAsConst(newTags)) {
                res << tag.localId();
            }
            return res;
        }();
        builder.setTagLocalIds(std::move(tagLocalIds));
    }
    else if (!updatedTags.isEmpty()) {
        QStringList tagLocalIds = [&] {
            QStringList res;
            res.reserve(updatedTags.size());
            for (const auto & tag: qAsConst(updatedTags)) {
                res << tag.localId();
            }
            return res;
        }();
        builder.setTagLocalIds(std::move(tagLocalIds));

        QList<qevercloud::Guid> tagGuids = [&] {
            QList<qevercloud::Guid> res;
            res.reserve(updatedTags.size());
            for (const auto & tag: qAsConst(updatedTags)) {
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

enum class AddParentToTag
{
    Yes,
    No
};

[[nodiscard]] qevercloud::Tag generateTag(
    const int index, const WithEvernoteFields withEvernoteFields,
    const QList<qevercloud::Tag> & previousTags, qint32 & usn,
    AddParentToTag addParentToTag = AddParentToTag::Yes)
{
    qevercloud::TagBuilder builder;
    builder.setLocalId(UidGenerator::Generate())
        .setName(
            (withEvernoteFields == WithEvernoteFields::Yes)
                ? QString::fromUtf8("Updated tag #%1").arg(index + 1)
                : QString::fromUtf8("New tag #%1").arg(index + 1));

    if (!previousTags.isEmpty() && (addParentToTag == AddParentToTag::Yes)) {
        builder.setParentTagLocalId(previousTags.constLast().localId());
    }

    if (withEvernoteFields == WithEvernoteFields::Yes) {
        builder.setUpdateSequenceNum(usn++);
        builder.setGuid(UidGenerator::Generate());

        if (!previousTags.isEmpty() && (addParentToTag == AddParentToTag::Yes))
        {
            builder.setParentGuid(previousTags.constLast().guid());
        }
    }

    return builder.build();
}

[[nodiscard]] qevercloud::LinkedNotebook generateLinkedNotebook(
    const int index, qint32 & usn)
{
    return qevercloud::LinkedNotebookBuilder{}
        .setGuid(UidGenerator::Generate())
        .setUpdateSequenceNum(usn++)
        .setUsername(QString::fromUtf8("Linked notebook #%1").arg(index + 1))
        .build();
}

[[nodiscard]] SenderTestData generateTestData(
    const SenderTestFlags flags, const int itemCount = 6)
{
    SenderTestData result;
    result.m_maxUserOwnUsn = 42;

    if (flags.testFlag(SenderTestFlag::WithNewSavedSearches)) {
        result.m_newSavedSearches.reserve(itemCount);
        for (int i = 0; i < itemCount; ++i) {
            result.m_newSavedSearches << generateSavedSearch(
                i, WithEvernoteFields::No, result.m_maxUserOwnUsn);
        }
    }

    if (flags.testFlag(SenderTestFlag::WithUpdatedSavedSearches)) {
        result.m_updatedSavedSearches.reserve(itemCount);
        for (int i = 0; i < itemCount; ++i) {
            result.m_updatedSavedSearches << generateSavedSearch(
                i, WithEvernoteFields::Yes, result.m_maxUserOwnUsn);
        }
    }

    if (flags.testFlag(SenderTestFlag::WithNewUserOwnNotebooks)) {
        result.m_newUserOwnNotebooks.reserve(itemCount);
        for (int i = 0; i < itemCount; ++i) {
            result.m_newUserOwnNotebooks << generateNotebook(
                i, WithEvernoteFields::No, result.m_maxUserOwnUsn);
        }
    }

    if (flags.testFlag(SenderTestFlag::WithUpdatedUserOwnNotebooks)) {
        result.m_updatedUserOwnNotebooks.reserve(itemCount);
        for (int i = 0; i < itemCount; ++i) {
            result.m_updatedUserOwnNotebooks << generateNotebook(
                i, WithEvernoteFields::Yes, result.m_maxUserOwnUsn);
        }
    }

    if (flags.testFlag(SenderTestFlag::WithNewUserOwnTags)) {
        result.m_newUserOwnTags.reserve(itemCount);
        for (int i = 0; i < itemCount; ++i) {
            result.m_newUserOwnTags << generateTag(
                i, WithEvernoteFields::No, result.m_newUserOwnTags,
                result.m_maxUserOwnUsn);
        }
        // Putting child tags first to ensure in test that parents would be sent
        // first
        std::reverse(
            result.m_newUserOwnTags.begin(), result.m_newUserOwnTags.end());
    }

    if (flags.testFlag(SenderTestFlag::WithUpdatedUserOwnTags)) {
        result.m_updatedUserOwnTags.reserve(itemCount);
        for (int i = 0; i < itemCount; ++i) {
            result.m_updatedUserOwnTags << generateTag(
                i, WithEvernoteFields::Yes, result.m_updatedUserOwnTags,
                result.m_maxUserOwnUsn);
        }
        // Putting child tags first to ensure in test that parents would be sent
        // first
        std::reverse(
            result.m_updatedUserOwnTags.begin(),
            result.m_updatedUserOwnTags.end());
    }

    if (flags.testFlag(SenderTestFlag::WithNewUserOwnNotes)) {
        result.m_newUserOwnNotes.reserve(itemCount);
        for (int i = 0; i < itemCount; ++i) {
            result.m_newUserOwnNotes << generateNote(
                i, WithEvernoteFields::No, result.m_newUserOwnNotebooks,
                result.m_updatedUserOwnNotebooks, result.m_newUserOwnTags,
                result.m_updatedUserOwnTags, result.m_maxUserOwnUsn);
        }
    }

    if (flags.testFlag(SenderTestFlag::WithUpdatedUserOwnNotes)) {
        result.m_updatedUserOwnNotes.reserve(itemCount);
        for (int i = 0; i < itemCount; ++i) {
            result.m_updatedUserOwnNotes << generateNote(
                i, WithEvernoteFields::Yes, result.m_newUserOwnNotebooks,
                result.m_updatedUserOwnNotebooks, result.m_newUserOwnTags,
                result.m_updatedUserOwnTags, result.m_maxUserOwnUsn);
        }
    }

    const bool hasLinkedNotebooksStuff =
        flags.testFlag(SenderTestFlag::WithUpdatedLinkedNotebooks) ||
        flags.testFlag(SenderTestFlag::WithNewLinkedNotebooksNotes) ||
        flags.testFlag(SenderTestFlag::WithUpdatedLinkedNotebooksNotes) ||
        flags.testFlag(SenderTestFlag::WithNewLinkedNotebooksTags) ||
        flags.testFlag(SenderTestFlag::WithUpdatedLinkedNotebooksTags);

    if (hasLinkedNotebooksStuff) {
        result.m_linkedNotebooks.reserve(itemCount);
        for (int i = 0; i < itemCount; ++i) {
            result.m_linkedNotebooks
                << generateLinkedNotebook(i, result.m_maxUserOwnUsn);
            result.m_maxLinkedNotebookUsns
                [result.m_linkedNotebooks.constLast().guid().value()] = 42;
        }

        if (flags.testFlag(SenderTestFlag::WithUpdatedLinkedNotebooks)) {
            result.m_updatedLinkedNotebooks.reserve(itemCount);
            for (int i = 0; i < itemCount; ++i) {
                const auto & linkedNotebookGuid =
                    result.m_linkedNotebooks[i].guid().value();

                auto notebook = generateNotebook(
                    i, WithEvernoteFields::Yes,
                    result.m_maxLinkedNotebookUsns[linkedNotebookGuid]);

                notebook.setLinkedNotebookGuid(linkedNotebookGuid);
                result.m_updatedLinkedNotebooks << notebook;
            }
        }

        if (flags.testFlag(SenderTestFlag::WithNewLinkedNotebooksTags)) {
            result.m_newLinkedNotebooksTags.reserve(itemCount);
            for (int i = 0; i < itemCount; ++i) {
                const auto & linkedNotebookGuid =
                    result.m_linkedNotebooks[i].guid().value();

                auto tag = generateTag(
                    i, WithEvernoteFields::No, result.m_newLinkedNotebooksTags,
                    result.m_maxLinkedNotebookUsns[linkedNotebookGuid],
                    AddParentToTag::No);

                tag.setLinkedNotebookGuid(linkedNotebookGuid);
                result.m_newLinkedNotebooksTags << tag;
            }
            // Putting child tags first to ensure in test that parents would be
            // sent first
            std::reverse(
                result.m_newLinkedNotebooksTags.begin(),
                result.m_newLinkedNotebooksTags.end());
        }

        if (flags.testFlag(SenderTestFlag::WithUpdatedLinkedNotebooksTags)) {
            result.m_updatedLinkedNotebooksTags.reserve(itemCount);
            for (int i = 0; i < itemCount; ++i) {
                const auto & linkedNotebookGuid =
                    result.m_linkedNotebooks[i].guid().value();

                auto tag = generateTag(
                    i, WithEvernoteFields::Yes,
                    result.m_updatedLinkedNotebooksTags,
                    result.m_maxLinkedNotebookUsns[linkedNotebookGuid],
                    AddParentToTag::No);

                tag.setLinkedNotebookGuid(linkedNotebookGuid);
                result.m_updatedLinkedNotebooksTags << tag;
            }
            // Putting child tags first to ensure in test that parents would be
            // sent first
            std::reverse(
                result.m_updatedLinkedNotebooksTags.begin(),
                result.m_updatedLinkedNotebooksTags.end());
        }

        if (flags.testFlag(SenderTestFlag::WithNewLinkedNotebooksNotes)) {
            result.m_newLinkedNotebooksNotes.reserve(itemCount);
            for (int i = 0; i < itemCount; ++i) {
                const auto & linkedNotebookGuid =
                    result.m_linkedNotebooks[i].guid().value();

                result.m_newLinkedNotebooksNotes << generateNote(
                    i, WithEvernoteFields::No, {},
                    result.m_updatedLinkedNotebooks,
                    result.m_newLinkedNotebooksTags,
                    result.m_updatedLinkedNotebooksTags,
                    result.m_maxLinkedNotebookUsns[linkedNotebookGuid]);
            }
        }

        if (flags.testFlag(SenderTestFlag::WithUpdatedLinkedNotebooksNotes)) {
            result.m_updatedLinkedNotebooksNotes.reserve(itemCount);
            for (int i = 0; i < itemCount; ++i) {
                const auto & linkedNotebookGuid =
                    result.m_linkedNotebooks[i].guid().value();

                result.m_updatedLinkedNotebooksNotes << generateNote(
                    i, WithEvernoteFields::Yes, {},
                    result.m_updatedLinkedNotebooks,
                    result.m_newLinkedNotebooksTags,
                    result.m_updatedLinkedNotebooksTags,
                    result.m_maxLinkedNotebookUsns[linkedNotebookGuid]);
            }
        }
    }

    return result;
}

[[nodiscard]] bool findAndSetNoteNotebookGuid(
    qevercloud::Note & note, const QList<qevercloud::Notebook> & notebooks)
{
    if (const auto it = std::find_if(
            notebooks.constBegin(), notebooks.constEnd(),
            [&](const qevercloud::Notebook & notebook) {
                return notebook.localId() == note.notebookLocalId();
            });
        it != notebooks.constEnd())
    {
        note.setNotebookGuid(it->guid());
        return true;
    }

    return false;
}

void findAndSetNoteTagGuids(
    qevercloud::Note & note, const QList<qevercloud::Tag> & tags)
{
    QList<qevercloud::Guid> tagGuids =
        note.tagGuids().value_or(QList<qevercloud::Guid>{});

    for (const auto & tagLocalId: qAsConst(note.tagLocalIds())) {
        if (const auto it = std::find_if(
                tags.constBegin(), tags.constEnd(),
                [&](const qevercloud::Tag & tag) {
                    return tag.localId() == tagLocalId;
                });
            it != tags.constEnd() && it->guid())
        {
            tagGuids << *it->guid();
        }
    }

    note.setTagGuids(tagGuids);
}

[[nodiscard]] bool findAndSetParentTagGuid(
    qevercloud::Tag & tag, const QList<qevercloud::Tag> & tags)
{
    const auto & parentTagLocalId = tag.parentTagLocalId();
    if (parentTagLocalId.isEmpty()) {
        return true;
    }

    if (const auto it = std::find_if(
            tags.constBegin(), tags.constEnd(),
            [&](const qevercloud::Tag & t) {
                return tag.parentTagLocalId() == t.localId();
            });
        it != tags.constEnd())
    {
        tag.setParentGuid(it->guid());
        return true;
    }

    return false;
}

struct SentData
{
    QList<qevercloud::SavedSearch> m_sentSavedSearches;
    QList<qevercloud::Tag> m_sentTags;
    QList<qevercloud::Notebook> m_sentNotebooks;
    QList<qevercloud::Note> m_sentNotes;

    QList<qevercloud::SavedSearch> m_failedToSendSavedSearches;
    QList<qevercloud::Tag> m_failedToSendTags;
    QList<qevercloud::Notebook> m_failedToSendNotebooks;
    QList<qevercloud::Note> m_failedToSendNotes;
};

enum class NoteStoreBehaviour
{
    WithoutFailures,
    WithFailures,
    WithRateLimitExceeding,
    WithAuthenticationExpiring
};

void setupUserOwnNoteStoreMock(
    const SenderTestData & testData,
    const std::shared_ptr<mocks::qevercloud::MockINoteStore> & mockNoteStore,
    SentData & sentData,
    NoteStoreBehaviour noteStoreBehaviour = NoteStoreBehaviour::WithoutFailures)
{
    if (!testData.m_newSavedSearches.isEmpty()) {
        EXPECT_CALL(*mockNoteStore, createSearchAsync)
            .Times(AtMost(testData.m_newSavedSearches.size()))
            .WillRepeatedly([&, i = std::make_shared<int>(0),
                             itemCount = testData.m_newSavedSearches.size(),
                             noteStoreBehaviour](
                                const qevercloud::SavedSearch & savedSearch,
                                const qevercloud::IRequestContextPtr &
                                    ctx) mutable {
                EXPECT_FALSE(ctx);
                EXPECT_TRUE(testData.m_newSavedSearches.contains(savedSearch));
                qevercloud::SavedSearch createdSavedSearch = savedSearch;
                createdSavedSearch.setGuid(UidGenerator::Generate());
                createdSavedSearch.setUpdateSequenceNum(
                    testData.m_maxUserOwnUsn++);

                const int counter = *i;
                ++(*i);

                switch (noteStoreBehaviour) {
                case NoteStoreBehaviour::WithoutFailures:
                    sentData.m_sentSavedSearches << createdSavedSearch;
                    return threading::makeReadyFuture<qevercloud::SavedSearch>(
                        std::move(createdSavedSearch));
                case NoteStoreBehaviour::WithFailures:
                    if (counter % 2 == 0) {
                        sentData.m_sentSavedSearches << createdSavedSearch;
                        return threading::makeReadyFuture<
                            qevercloud::SavedSearch>(
                            std::move(createdSavedSearch));
                    }
                    sentData.m_failedToSendSavedSearches << createdSavedSearch;
                    return threading::makeExceptionalFuture<
                        qevercloud::SavedSearch>(RuntimeError{
                        ErrorString{QStringLiteral("some error")}});
                case NoteStoreBehaviour::WithRateLimitExceeding:
                    if (counter < itemCount / 2) {
                        sentData.m_sentSavedSearches << createdSavedSearch;
                        return threading::makeReadyFuture<
                            qevercloud::SavedSearch>(
                            std::move(createdSavedSearch));
                    }
                    sentData.m_failedToSendSavedSearches << createdSavedSearch;
                    return threading::makeExceptionalFuture<
                        qevercloud::SavedSearch>(
                        qevercloud::EDAMSystemExceptionBuilder{}
                            .setErrorCode(
                                qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED)
                            .setMessage(QStringLiteral("Rate limit reached"))
                            .setRateLimitDuration(300)
                            .build());
                case NoteStoreBehaviour::WithAuthenticationExpiring:
                    if (counter < itemCount / 2) {
                        sentData.m_sentSavedSearches << createdSavedSearch;
                        return threading::makeReadyFuture<
                            qevercloud::SavedSearch>(
                            std::move(createdSavedSearch));
                    }
                    sentData.m_failedToSendSavedSearches << createdSavedSearch;
                    return threading::makeExceptionalFuture<
                        qevercloud::SavedSearch>(
                        qevercloud::EDAMSystemExceptionBuilder{}
                            .setErrorCode(
                                qevercloud::EDAMErrorCode::AUTH_EXPIRED)
                            .setMessage(
                                QStringLiteral("Authentication expired"))
                            .build());
                }

                UNREACHABLE;
            });
    }

    if (!testData.m_updatedSavedSearches.isEmpty()) {
        EXPECT_CALL(*mockNoteStore, updateSearchAsync)
            .Times(AtMost(testData.m_updatedSavedSearches.size()))
            .WillRepeatedly([&, i = std::make_shared<int>(0),
                             itemCount = testData.m_updatedSavedSearches.size(),
                             noteStoreBehaviour](
                                const qevercloud::SavedSearch & savedSearch,
                                const qevercloud::IRequestContextPtr &
                                    ctx) mutable {
                EXPECT_FALSE(ctx);
                EXPECT_TRUE(
                    testData.m_updatedSavedSearches.contains(savedSearch));
                auto usn = testData.m_maxUserOwnUsn++;
                qevercloud::SavedSearch updatedSavedSearch = savedSearch;
                updatedSavedSearch.setUpdateSequenceNum(usn);

                const int counter = *i;
                ++(*i);

                switch (noteStoreBehaviour) {
                case NoteStoreBehaviour::WithoutFailures:
                    sentData.m_sentSavedSearches << updatedSavedSearch;
                    return threading::makeReadyFuture<qint32>(usn);
                case NoteStoreBehaviour::WithFailures:
                    if (counter % 2 == 0) {
                        sentData.m_sentSavedSearches << updatedSavedSearch;
                        return threading::makeReadyFuture<qint32>(usn);
                    }
                    sentData.m_failedToSendSavedSearches << updatedSavedSearch;
                    return threading::makeExceptionalFuture<qint32>(
                        RuntimeError{
                            ErrorString{QStringLiteral("some error")}});
                case NoteStoreBehaviour::WithRateLimitExceeding:
                    if (counter < itemCount / 2) {
                        sentData.m_sentSavedSearches << updatedSavedSearch;
                        return threading::makeReadyFuture<qint32>(usn);
                    }
                    sentData.m_failedToSendSavedSearches << updatedSavedSearch;
                    return threading::makeExceptionalFuture<qint32>(
                        qevercloud::EDAMSystemExceptionBuilder{}
                            .setErrorCode(
                                qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED)
                            .setMessage(QStringLiteral("Rate limit reached"))
                            .setRateLimitDuration(300)
                            .build());
                case NoteStoreBehaviour::WithAuthenticationExpiring:
                    if (counter < itemCount / 2) {
                        sentData.m_sentSavedSearches << updatedSavedSearch;
                        return threading::makeReadyFuture<qint32>(usn);
                    }
                    sentData.m_failedToSendSavedSearches << updatedSavedSearch;
                    return threading::makeExceptionalFuture<qint32>(
                        qevercloud::EDAMSystemExceptionBuilder{}
                            .setErrorCode(
                                qevercloud::EDAMErrorCode::AUTH_EXPIRED)
                            .setMessage(
                                QStringLiteral("Authentication expired"))
                            .build());
                }

                UNREACHABLE;
            });
    }

    if (!testData.m_newUserOwnNotebooks.isEmpty()) {
        EXPECT_CALL(*mockNoteStore, createNotebookAsync)
            .Times(AtMost(testData.m_newUserOwnNotebooks.size()))
            .WillRepeatedly([&, i = std::make_shared<int>(0),
                             itemCount = testData.m_newUserOwnNotebooks.size(),
                             noteStoreBehaviour](
                                const qevercloud::Notebook & notebook,
                                const qevercloud::IRequestContextPtr &
                                    ctx) mutable {
                EXPECT_FALSE(ctx);
                EXPECT_TRUE(testData.m_newUserOwnNotebooks.contains(notebook));
                qevercloud::Notebook createdNotebook = notebook;
                createdNotebook.setGuid(UidGenerator::Generate());
                createdNotebook.setUpdateSequenceNum(
                    testData.m_maxUserOwnUsn++);

                const int counter = *i;
                ++(*i);

                switch (noteStoreBehaviour) {
                case NoteStoreBehaviour::WithoutFailures:
                    sentData.m_sentNotebooks << createdNotebook;
                    return threading::makeReadyFuture<qevercloud::Notebook>(
                        std::move(createdNotebook));
                case NoteStoreBehaviour::WithFailures:
                    if (counter % 2 == 0) {
                        sentData.m_sentNotebooks << createdNotebook;
                        return threading::makeReadyFuture<qevercloud::Notebook>(
                            std::move(createdNotebook));
                    }
                    sentData.m_failedToSendNotebooks << createdNotebook;
                    return threading::makeExceptionalFuture<
                        qevercloud::Notebook>(RuntimeError{
                        ErrorString{QStringLiteral("some error")}});
                case NoteStoreBehaviour::WithRateLimitExceeding:
                    if (counter < itemCount / 2) {
                        sentData.m_sentNotebooks << createdNotebook;
                        return threading::makeReadyFuture<qevercloud::Notebook>(
                            std::move(createdNotebook));
                    }
                    sentData.m_failedToSendNotebooks << createdNotebook;
                    return threading::makeExceptionalFuture<
                        qevercloud::Notebook>(
                        qevercloud::EDAMSystemExceptionBuilder{}
                            .setErrorCode(
                                qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED)
                            .setMessage(QStringLiteral("Rate limit reached"))
                            .setRateLimitDuration(300)
                            .build());
                case NoteStoreBehaviour::WithAuthenticationExpiring:
                    if (counter < itemCount / 2) {
                        sentData.m_sentNotebooks << createdNotebook;
                        return threading::makeReadyFuture<qevercloud::Notebook>(
                            std::move(createdNotebook));
                    }
                    sentData.m_failedToSendNotebooks << createdNotebook;
                    return threading::makeExceptionalFuture<
                        qevercloud::Notebook>(
                        qevercloud::EDAMSystemExceptionBuilder{}
                            .setErrorCode(
                                qevercloud::EDAMErrorCode::AUTH_EXPIRED)
                            .setMessage(
                                QStringLiteral("Authentication expired"))
                            .build());
                }

                UNREACHABLE;
            });
    }

    if (!testData.m_updatedUserOwnNotebooks.isEmpty()) {
        EXPECT_CALL(*mockNoteStore, updateNotebookAsync)
            .Times(AtMost(testData.m_updatedUserOwnNotebooks.size()))
            .WillRepeatedly([&, i = std::make_shared<int>(0),
                             itemCount =
                                 testData.m_updatedUserOwnNotebooks.size(),
                             noteStoreBehaviour](
                                const qevercloud::Notebook & notebook,
                                const qevercloud::IRequestContextPtr &
                                    ctx) mutable {
                EXPECT_FALSE(ctx);
                EXPECT_TRUE(
                    testData.m_updatedUserOwnNotebooks.contains(notebook));
                auto usn = testData.m_maxUserOwnUsn++;
                qevercloud::Notebook updatedNotebook = notebook;
                updatedNotebook.setUpdateSequenceNum(usn);

                const int counter = *i;
                ++(*i);

                switch (noteStoreBehaviour) {
                case NoteStoreBehaviour::WithoutFailures:
                    sentData.m_sentNotebooks << updatedNotebook;
                    return threading::makeReadyFuture<qint32>(usn);
                case NoteStoreBehaviour::WithFailures:
                    if (counter % 2 == 0) {
                        sentData.m_sentNotebooks << updatedNotebook;
                        return threading::makeReadyFuture<qint32>(usn);
                    }
                    sentData.m_failedToSendNotebooks << updatedNotebook;
                    return threading::makeExceptionalFuture<qint32>(
                        RuntimeError{
                            ErrorString{QStringLiteral("some error")}});
                case NoteStoreBehaviour::WithRateLimitExceeding:
                    if (counter < itemCount / 2) {
                        sentData.m_sentNotebooks << updatedNotebook;
                        return threading::makeReadyFuture<qint32>(usn);
                    }
                    sentData.m_failedToSendNotebooks << updatedNotebook;
                    return threading::makeExceptionalFuture<qint32>(
                        qevercloud::EDAMSystemExceptionBuilder{}
                            .setErrorCode(
                                qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED)
                            .setMessage(QStringLiteral("Rate limit reached"))
                            .setRateLimitDuration(300)
                            .build());
                case NoteStoreBehaviour::WithAuthenticationExpiring:
                    if (counter < itemCount / 2) {
                        sentData.m_sentNotebooks << updatedNotebook;
                        return threading::makeReadyFuture<qint32>(usn);
                    }
                    sentData.m_failedToSendNotebooks << updatedNotebook;
                    return threading::makeExceptionalFuture<qint32>(
                        qevercloud::EDAMSystemExceptionBuilder{}
                            .setErrorCode(
                                qevercloud::EDAMErrorCode::AUTH_EXPIRED)
                            .setMessage(
                                QStringLiteral("Authentication expired"))
                            .build());
                }

                UNREACHABLE;
            });
    }

    if (!testData.m_newUserOwnTags.isEmpty()) {
        EXPECT_CALL(*mockNoteStore, createTagAsync)
            .Times(AtMost(testData.m_newUserOwnTags.size()))
            .WillRepeatedly([&, i = std::make_shared<int>(0),
                             itemCount = testData.m_newUserOwnTags.size(),
                             noteStoreBehaviour](
                                const qevercloud::Tag & tag,
                                const qevercloud::IRequestContextPtr &
                                    ctx) mutable {
                EXPECT_FALSE(ctx);

                qevercloud::Tag tagWithoutParentGuid = tag;
                tagWithoutParentGuid.setParentGuid(std::nullopt);
                EXPECT_TRUE(
                    testData.m_newUserOwnTags.contains(tagWithoutParentGuid));

                qevercloud::Tag createdTag = tag;
                createdTag.setGuid(UidGenerator::Generate());
                createdTag.setUpdateSequenceNum(testData.m_maxUserOwnUsn++);
                if (!findAndSetParentTagGuid(
                        createdTag, testData.m_newUserOwnTags)) {
                    Q_UNUSED(findAndSetParentTagGuid(
                        createdTag, testData.m_updatedUserOwnTags))
                }

                const int counter = *i;
                ++(*i);

                switch (noteStoreBehaviour) {
                case NoteStoreBehaviour::WithoutFailures:
                    sentData.m_sentTags << createdTag;
                    return threading::makeReadyFuture<qevercloud::Tag>(
                        std::move(createdTag));
                case NoteStoreBehaviour::WithFailures:
                    if (counter < itemCount / 2) {
                        sentData.m_sentTags << createdTag;
                        return threading::makeReadyFuture<qevercloud::Tag>(
                            std::move(createdTag));
                    }
                    sentData.m_failedToSendTags << createdTag;
                    return threading::makeExceptionalFuture<qevercloud::Tag>(
                        RuntimeError{
                            ErrorString{QStringLiteral("some error")}});
                case NoteStoreBehaviour::WithRateLimitExceeding:
                    if (counter < itemCount / 2) {
                        sentData.m_sentTags << createdTag;
                        return threading::makeReadyFuture<qevercloud::Tag>(
                            std::move(createdTag));
                    }
                    sentData.m_failedToSendTags << createdTag;
                    return threading::makeExceptionalFuture<qevercloud::Tag>(
                        qevercloud::EDAMSystemExceptionBuilder{}
                            .setErrorCode(
                                qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED)
                            .setMessage(QStringLiteral("Rate limit reached"))
                            .setRateLimitDuration(300)
                            .build());
                case NoteStoreBehaviour::WithAuthenticationExpiring:
                    if (counter < itemCount / 2) {
                        sentData.m_sentTags << createdTag;
                        return threading::makeReadyFuture<qevercloud::Tag>(
                            std::move(createdTag));
                    }
                    sentData.m_failedToSendTags << createdTag;
                    return threading::makeExceptionalFuture<qevercloud::Tag>(
                        qevercloud::EDAMSystemExceptionBuilder{}
                            .setErrorCode(
                                qevercloud::EDAMErrorCode::AUTH_EXPIRED)
                            .setMessage(
                                QStringLiteral("Authentication expired"))
                            .build());
                }

                UNREACHABLE;
            });
    }

    if (!testData.m_updatedUserOwnTags.isEmpty()) {
        EXPECT_CALL(*mockNoteStore, updateTagAsync)
            .Times(AtMost(testData.m_updatedUserOwnTags.size()))
            .WillRepeatedly([&, i = std::make_shared<int>(0),
                             itemCount = testData.m_updatedUserOwnTags.size(),
                             noteStoreBehaviour](
                                const qevercloud::Tag & tag,
                                const qevercloud::IRequestContextPtr &
                                    ctx) mutable {
                EXPECT_FALSE(ctx);
                EXPECT_TRUE(testData.m_updatedUserOwnTags.contains(tag));
                auto usn = testData.m_maxUserOwnUsn++;
                qevercloud::Tag updatedTag = tag;
                updatedTag.setUpdateSequenceNum(usn);

                const int counter = *i;
                ++(*i);

                switch (noteStoreBehaviour) {
                case NoteStoreBehaviour::WithoutFailures:
                    sentData.m_sentTags << updatedTag;
                    return threading::makeReadyFuture<qint32>(usn);
                case NoteStoreBehaviour::WithFailures:
                    if (counter < itemCount / 2) {
                        sentData.m_sentTags << updatedTag;
                        return threading::makeReadyFuture<qint32>(usn);
                    }
                    sentData.m_failedToSendTags << updatedTag;
                    return threading::makeExceptionalFuture<qint32>(
                        RuntimeError{
                            ErrorString{QStringLiteral("some error")}});
                case NoteStoreBehaviour::WithRateLimitExceeding:
                    if (counter < itemCount / 2) {
                        sentData.m_sentTags << updatedTag;
                        return threading::makeReadyFuture<qint32>(usn);
                    }
                    sentData.m_failedToSendTags << updatedTag;
                    return threading::makeExceptionalFuture<qint32>(
                        qevercloud::EDAMSystemExceptionBuilder{}
                            .setErrorCode(
                                qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED)
                            .setMessage(QStringLiteral("Rate limit reached"))
                            .setRateLimitDuration(300)
                            .build());
                case NoteStoreBehaviour::WithAuthenticationExpiring:
                    if (counter < itemCount / 2) {
                        sentData.m_sentTags << updatedTag;
                        return threading::makeReadyFuture<qint32>(usn);
                    }
                    sentData.m_failedToSendTags << updatedTag;
                    return threading::makeExceptionalFuture<qint32>(
                        qevercloud::EDAMSystemExceptionBuilder{}
                            .setErrorCode(
                                qevercloud::EDAMErrorCode::AUTH_EXPIRED)
                            .setMessage(
                                QStringLiteral("Authentication expired"))
                            .build());
                }

                UNREACHABLE;
            });
    }

    const auto setNoteNotebookGuid = [&testData](qevercloud::Note & note) {
        if (findAndSetNoteNotebookGuid(note, testData.m_newUserOwnNotebooks)) {
            return;
        }

        if (findAndSetNoteNotebookGuid(
                note, testData.m_updatedUserOwnNotebooks)) {
            return;
        }

        note.setNotebookGuid(UidGenerator::Generate());
    };

    if (!testData.m_newUserOwnNotes.isEmpty()) {
        EXPECT_CALL(*mockNoteStore, createNoteAsync)
            .Times(AtMost(testData.m_newUserOwnNotes.size()))
            .WillRepeatedly([&, i = std::make_shared<int>(0),
                             itemCount = testData.m_newUserOwnNotes.size(),
                             noteStoreBehaviour, setNoteNotebookGuid](
                                const qevercloud::Note & note,
                                const qevercloud::IRequestContextPtr &
                                    ctx) mutable {
                EXPECT_FALSE(ctx);
                EXPECT_TRUE(testData.m_newUserOwnNotes.contains(note));
                qevercloud::Note createdNote = note;
                createdNote.setGuid(UidGenerator::Generate());
                createdNote.setUpdateSequenceNum(testData.m_maxUserOwnUsn++);
                setNoteNotebookGuid(createdNote);
                findAndSetNoteTagGuids(createdNote, testData.m_newUserOwnTags);
                findAndSetNoteTagGuids(
                    createdNote, testData.m_updatedUserOwnTags);

                const int counter = *i;
                ++(*i);

                switch (noteStoreBehaviour) {
                case NoteStoreBehaviour::WithoutFailures:
                    sentData.m_sentNotes << createdNote;
                    return threading::makeReadyFuture<qevercloud::Note>(
                        std::move(createdNote));
                case NoteStoreBehaviour::WithFailures:
                    sentData.m_failedToSendNotes << createdNote;
                    return threading::makeExceptionalFuture<qevercloud::Note>(
                        RuntimeError{
                            ErrorString{QStringLiteral("some error")}});
                case NoteStoreBehaviour::WithRateLimitExceeding:
                    if (counter < itemCount / 2) {
                        sentData.m_sentNotes << createdNote;
                        return threading::makeReadyFuture<qevercloud::Note>(
                            std::move(createdNote));
                    }
                    sentData.m_failedToSendNotes << createdNote;
                    return threading::makeExceptionalFuture<qevercloud::Note>(
                        qevercloud::EDAMSystemExceptionBuilder{}
                            .setErrorCode(
                                qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED)
                            .setMessage(QStringLiteral("Rate limit reached"))
                            .setRateLimitDuration(300)
                            .build());
                case NoteStoreBehaviour::WithAuthenticationExpiring:
                    if (counter < itemCount / 2) {
                        sentData.m_sentNotes << createdNote;
                        return threading::makeReadyFuture<qevercloud::Note>(
                            std::move(createdNote));
                    }
                    sentData.m_failedToSendNotes << createdNote;
                    return threading::makeExceptionalFuture<qevercloud::Note>(
                        qevercloud::EDAMSystemExceptionBuilder{}
                            .setErrorCode(
                                qevercloud::EDAMErrorCode::AUTH_EXPIRED)
                            .setMessage(
                                QStringLiteral("Authentication expired"))
                            .build());
                }

                UNREACHABLE;
            });
    }

    if (!testData.m_updatedUserOwnNotes.isEmpty()) {
        EXPECT_CALL(*mockNoteStore, updateNoteAsync)
            .Times(AtMost(testData.m_updatedUserOwnNotes.size()))
            .WillRepeatedly([&, i = std::make_shared<int>(0),
                             itemCount = testData.m_updatedUserOwnNotes.size(),
                             noteStoreBehaviour, setNoteNotebookGuid](
                                const qevercloud::Note & note,
                                const qevercloud::IRequestContextPtr &
                                    ctx) mutable {
                EXPECT_FALSE(ctx);
                EXPECT_TRUE(testData.m_updatedUserOwnNotes.contains(note));
                auto usn = testData.m_maxUserOwnUsn++;
                qevercloud::Note updatedNote = note;
                updatedNote.setUpdateSequenceNum(usn);
                setNoteNotebookGuid(updatedNote);
                findAndSetNoteTagGuids(updatedNote, testData.m_newUserOwnTags);
                findAndSetNoteTagGuids(
                    updatedNote, testData.m_updatedUserOwnTags);

                const int counter = *i;
                ++(*i);

                switch (noteStoreBehaviour) {
                case NoteStoreBehaviour::WithoutFailures:
                    sentData.m_sentNotes << updatedNote;
                    return threading::makeReadyFuture<qevercloud::Note>(
                        std::move(updatedNote));
                case NoteStoreBehaviour::WithFailures:
                    if (counter % 2 == 0) {
                        sentData.m_sentNotes << updatedNote;
                        return threading::makeReadyFuture<qevercloud::Note>(
                            std::move(updatedNote));
                    }
                    sentData.m_failedToSendNotes << updatedNote;
                    return threading::makeExceptionalFuture<qevercloud::Note>(
                        RuntimeError{
                            ErrorString{QStringLiteral("some error")}});
                case NoteStoreBehaviour::WithRateLimitExceeding:
                    if (counter < itemCount / 2) {
                        sentData.m_sentNotes << updatedNote;
                        return threading::makeReadyFuture<qevercloud::Note>(
                            std::move(updatedNote));
                    }
                    sentData.m_failedToSendNotes << updatedNote;
                    return threading::makeExceptionalFuture<qevercloud::Note>(
                        qevercloud::EDAMSystemExceptionBuilder{}
                            .setErrorCode(
                                qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED)
                            .setMessage(QStringLiteral("Rate limit reached"))
                            .setRateLimitDuration(300)
                            .build());
                case NoteStoreBehaviour::WithAuthenticationExpiring:
                    if (counter < itemCount / 2) {
                        sentData.m_sentNotes << updatedNote;
                        return threading::makeReadyFuture<qevercloud::Note>(
                            std::move(updatedNote));
                    }
                    sentData.m_failedToSendNotes << updatedNote;
                    return threading::makeExceptionalFuture<qevercloud::Note>(
                        qevercloud::EDAMSystemExceptionBuilder{}
                            .setErrorCode(
                                qevercloud::EDAMErrorCode::AUTH_EXPIRED)
                            .setMessage(
                                QStringLiteral("Authentication expired"))
                            .build());
                }

                UNREACHABLE;
            });
    }
}

void setupLinkedNotebookNoteStoreMocks(
    const SenderTestData & testData,
    QHash<
        qevercloud::Guid, std::shared_ptr<mocks::qevercloud::MockINoteStore>> &
        mockNoteStores,
    SentData & sentData,
    NoteStoreBehaviour noteStoreBehaviour = NoteStoreBehaviour::WithoutFailures)
{
    auto i = std::make_shared<std::atomic<int>>(0);
    for (const auto & linkedNotebook: qAsConst(testData.m_linkedNotebooks)) {
        ASSERT_TRUE(linkedNotebook.guid());

        auto & mockNoteStore = mockNoteStores[*linkedNotebook.guid()];
        mockNoteStore =
            std::make_shared<StrictMock<mocks::qevercloud::MockINoteStore>>();

        EXPECT_CALL(*mockNoteStore, updateNotebookAsync)
            .WillRepeatedly([&, i, noteStoreBehaviour](
                                const qevercloud::Notebook & notebook,
                                const qevercloud::IRequestContextPtr &
                                    ctx) mutable {
                EXPECT_FALSE(ctx);
                EXPECT_TRUE(testData.m_maxLinkedNotebookUsns.contains(
                    *linkedNotebook.guid()));
                auto usn =
                    testData.m_maxLinkedNotebookUsns[*linkedNotebook.guid()]++;
                qevercloud::Notebook updatedNotebook = notebook;
                updatedNotebook.setUpdateSequenceNum(usn);

                switch (noteStoreBehaviour) {
                case NoteStoreBehaviour::WithoutFailures:
                    sentData.m_sentNotebooks << updatedNotebook;
                    return threading::makeReadyFuture<qint32>(usn);
                case NoteStoreBehaviour::WithFailures:
                {
                    const int counter =
                        i->fetch_add(1, std::memory_order_acq_rel);
                    if (counter % 2 == 0) {
                        sentData.m_sentNotebooks << updatedNotebook;
                        return threading::makeReadyFuture<qint32>(usn);
                    }
                    sentData.m_failedToSendNotebooks << updatedNotebook;
                    return threading::makeExceptionalFuture<qint32>(
                        RuntimeError{
                            ErrorString{QStringLiteral("some error")}});
                }
                case NoteStoreBehaviour::WithRateLimitExceeding:
                    sentData.m_failedToSendNotebooks << updatedNotebook;
                    return threading::makeExceptionalFuture<qint32>(
                        qevercloud::EDAMSystemExceptionBuilder{}
                            .setErrorCode(
                                qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED)
                            .setMessage(QStringLiteral("Rate limit reached"))
                            .setRateLimitDuration(300)
                            .build());
                case NoteStoreBehaviour::WithAuthenticationExpiring:
                    sentData.m_failedToSendNotebooks << updatedNotebook;
                    return threading::makeExceptionalFuture<qint32>(
                        qevercloud::EDAMSystemExceptionBuilder{}
                            .setErrorCode(
                                qevercloud::EDAMErrorCode::AUTH_EXPIRED)
                            .setMessage(
                                QStringLiteral("Authentication expired"))
                            .build());
                }

                UNREACHABLE;
            });

        const auto setNoteNotebookGuid = [&](qevercloud::Note & note) {
            const QList<qevercloud::Notebook> updatedNotebooks = [&] {
                QList<qevercloud::Notebook> notebooks;
                for (const auto & notebook:
                     qAsConst(testData.m_updatedLinkedNotebooks)) {
                    if (notebook.linkedNotebookGuid() == linkedNotebook.guid())
                    {
                        notebooks << notebook;
                    }
                }
                return notebooks;
            }();

            if (findAndSetNoteNotebookGuid(note, updatedNotebooks)) {
                return;
            }

            note.setNotebookGuid(UidGenerator::Generate());
        };

        EXPECT_CALL(*mockNoteStore, createTagAsync)
            .WillRepeatedly([&, i, noteStoreBehaviour](
                                const qevercloud::Tag & tag,
                                const qevercloud::IRequestContextPtr &
                                    ctx) mutable {
                EXPECT_FALSE(ctx);
                qevercloud::Tag createdTag = tag;
                createdTag.setGuid(UidGenerator::Generate());
                auto usn =
                    testData.m_maxLinkedNotebookUsns[*linkedNotebook.guid()]++;
                createdTag.setUpdateSequenceNum(usn);
                if (!findAndSetParentTagGuid(
                        createdTag, testData.m_newLinkedNotebooksTags)) {
                    Q_UNUSED(findAndSetParentTagGuid(
                        createdTag, testData.m_updatedLinkedNotebooksTags))
                }

                switch (noteStoreBehaviour) {
                case NoteStoreBehaviour::WithoutFailures:
                    sentData.m_sentTags << createdTag;
                    return threading::makeReadyFuture<qevercloud::Tag>(
                        std::move(createdTag));
                case NoteStoreBehaviour::WithFailures:
                {
                    const int counter =
                        i->fetch_add(1, std::memory_order_acq_rel);
                    if (counter % 2 == 0) {
                        sentData.m_sentTags << createdTag;
                        return threading::makeReadyFuture<qevercloud::Tag>(
                            std::move(createdTag));
                    }
                    sentData.m_failedToSendTags << createdTag;
                    return threading::makeExceptionalFuture<qevercloud::Tag>(
                        RuntimeError{
                            ErrorString{QStringLiteral("some error")}});
                }
                case NoteStoreBehaviour::WithRateLimitExceeding:
                    sentData.m_failedToSendTags << createdTag;
                    return threading::makeExceptionalFuture<qevercloud::Tag>(
                        qevercloud::EDAMSystemExceptionBuilder{}
                            .setErrorCode(
                                qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED)
                            .setMessage(QStringLiteral("Rate limit reached"))
                            .setRateLimitDuration(300)
                            .build());
                case NoteStoreBehaviour::WithAuthenticationExpiring:
                    sentData.m_failedToSendTags << createdTag;
                    return threading::makeExceptionalFuture<qevercloud::Tag>(
                        qevercloud::EDAMSystemExceptionBuilder{}
                            .setErrorCode(
                                qevercloud::EDAMErrorCode::AUTH_EXPIRED)
                            .setMessage(
                                QStringLiteral("Authentication expired"))
                            .build());
                }

                UNREACHABLE;
            });

        EXPECT_CALL(*mockNoteStore, updateTagAsync)
            .WillRepeatedly([&, i, noteStoreBehaviour](
                                const qevercloud::Tag & tag,
                                const qevercloud::IRequestContextPtr &
                                    ctx) mutable {
                EXPECT_FALSE(ctx);
                auto usn =
                    testData.m_maxLinkedNotebookUsns[*linkedNotebook.guid()]++;
                qevercloud::Tag updatedTag = tag;
                updatedTag.setUpdateSequenceNum(usn);

                switch (noteStoreBehaviour) {
                case NoteStoreBehaviour::WithoutFailures:
                    sentData.m_sentTags << updatedTag;
                    return threading::makeReadyFuture<qint32>(usn);
                case NoteStoreBehaviour::WithFailures:
                {
                    const int counter =
                        i->fetch_add(1, std::memory_order_acq_rel);
                    if (counter % 2 == 0) {
                        sentData.m_sentTags << updatedTag;
                        return threading::makeReadyFuture<qint32>(usn);
                    }

                    sentData.m_failedToSendTags << updatedTag;
                    return threading::makeExceptionalFuture<qint32>(
                        RuntimeError{
                            ErrorString{QStringLiteral("some error")}});
                }
                case NoteStoreBehaviour::WithRateLimitExceeding:
                    sentData.m_failedToSendTags << updatedTag;
                    return threading::makeExceptionalFuture<qint32>(
                        qevercloud::EDAMSystemExceptionBuilder{}
                            .setErrorCode(
                                qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED)
                            .setMessage(QStringLiteral("Rate limit reached"))
                            .setRateLimitDuration(300)
                            .build());
                case NoteStoreBehaviour::WithAuthenticationExpiring:
                    sentData.m_failedToSendTags << updatedTag;
                    return threading::makeExceptionalFuture<qint32>(
                        qevercloud::EDAMSystemExceptionBuilder{}
                            .setErrorCode(
                                qevercloud::EDAMErrorCode::AUTH_EXPIRED)
                            .setMessage(
                                QStringLiteral("Authentication expired"))
                            .build());
                }

                UNREACHABLE;
            });

        const auto setNoteTagGuids = [&](qevercloud::Note & note) {
            const QList<qevercloud::Tag> updatedTags = [&] {
                QList<qevercloud::Tag> tags;
                const auto processTags =
                    [&](const QList<qevercloud::Tag> & newOrUpdatedTags) {
                        for (const auto & tag: qAsConst(newOrUpdatedTags)) {
                            if (tag.linkedNotebookGuid() ==
                                linkedNotebook.guid()) {
                                tags << tag;
                            }
                        }
                    };
                processTags(testData.m_newLinkedNotebooksTags);
                processTags(testData.m_updatedLinkedNotebooksTags);
                return tags;
            }();

            findAndSetNoteTagGuids(note, updatedTags);
        };

        EXPECT_CALL(*mockNoteStore, createNoteAsync)
            .WillRepeatedly([&, i, noteStoreBehaviour, setNoteNotebookGuid,
                             setNoteTagGuids](
                                const qevercloud::Note & note,
                                const qevercloud::IRequestContextPtr &
                                    ctx) mutable {
                EXPECT_FALSE(ctx);
                qevercloud::Note createdNote = note;
                createdNote.setGuid(UidGenerator::Generate());
                auto usn =
                    testData.m_maxLinkedNotebookUsns[*linkedNotebook.guid()]++;
                createdNote.setUpdateSequenceNum(usn);
                setNoteNotebookGuid(createdNote);
                setNoteTagGuids(createdNote);

                switch (noteStoreBehaviour) {
                case NoteStoreBehaviour::WithoutFailures:
                    sentData.m_sentNotes << createdNote;
                    return threading::makeReadyFuture<qevercloud::Note>(
                        std::move(createdNote));
                case NoteStoreBehaviour::WithFailures:
                {
                    const int counter =
                        i->fetch_add(1, std::memory_order_acq_rel);
                    if (counter % 2 == 0) {
                        sentData.m_sentNotes << createdNote;
                        return threading::makeReadyFuture<qevercloud::Note>(
                            std::move(createdNote));
                    }

                    sentData.m_failedToSendNotes << createdNote;
                    return threading::makeExceptionalFuture<qevercloud::Note>(
                        RuntimeError{
                            ErrorString{QStringLiteral("some error")}});
                }
                case NoteStoreBehaviour::WithRateLimitExceeding:
                    sentData.m_failedToSendNotes << createdNote;
                    return threading::makeExceptionalFuture<qevercloud::Note>(
                        qevercloud::EDAMSystemExceptionBuilder{}
                            .setErrorCode(
                                qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED)
                            .setMessage(QStringLiteral("Rate limit reached"))
                            .setRateLimitDuration(300)
                            .build());
                case NoteStoreBehaviour::WithAuthenticationExpiring:
                    sentData.m_failedToSendNotes << createdNote;
                    return threading::makeExceptionalFuture<qevercloud::Note>(
                        qevercloud::EDAMSystemExceptionBuilder{}
                            .setErrorCode(
                                qevercloud::EDAMErrorCode::AUTH_EXPIRED)
                            .setMessage(
                                QStringLiteral("Authentication expired"))
                            .build());
                }
            });

        EXPECT_CALL(*mockNoteStore, updateNoteAsync)
            .WillRepeatedly([&, i, noteStoreBehaviour, setNoteNotebookGuid,
                             setNoteTagGuids](
                                const qevercloud::Note & note,
                                const qevercloud::IRequestContextPtr &
                                    ctx) mutable {
                EXPECT_FALSE(ctx);
                qevercloud::Note updatedNote = note;
                auto usn =
                    testData.m_maxLinkedNotebookUsns[*linkedNotebook.guid()]++;
                updatedNote.setUpdateSequenceNum(usn);
                setNoteNotebookGuid(updatedNote);
                setNoteTagGuids(updatedNote);

                switch (noteStoreBehaviour) {
                case NoteStoreBehaviour::WithoutFailures:
                    sentData.m_sentNotes << updatedNote;
                    return threading::makeReadyFuture<qevercloud::Note>(
                        std::move(updatedNote));
                case NoteStoreBehaviour::WithFailures:
                {
                    const int counter =
                        i->fetch_add(1, std::memory_order_acq_rel);
                    if (counter % 2 == 0) {
                        sentData.m_sentNotes << updatedNote;
                        return threading::makeReadyFuture<qevercloud::Note>(
                            std::move(updatedNote));
                    }

                    sentData.m_failedToSendNotes << updatedNote;
                    return threading::makeExceptionalFuture<qevercloud::Note>(
                        RuntimeError{
                            ErrorString{QStringLiteral("some error")}});
                }
                case NoteStoreBehaviour::WithRateLimitExceeding:
                    sentData.m_failedToSendNotes << updatedNote;
                    return threading::makeExceptionalFuture<qevercloud::Note>(
                        qevercloud::EDAMSystemExceptionBuilder{}
                            .setErrorCode(
                                qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED)
                            .setMessage(QStringLiteral("Rate limit reached"))
                            .setRateLimitDuration(300)
                            .build());
                case NoteStoreBehaviour::WithAuthenticationExpiring:
                    sentData.m_failedToSendNotes << updatedNote;
                    return threading::makeExceptionalFuture<qevercloud::Note>(
                        qevercloud::EDAMSystemExceptionBuilder{}
                            .setErrorCode(
                                qevercloud::EDAMErrorCode::AUTH_EXPIRED)
                            .setMessage(
                                QStringLiteral("Authentication expired"))
                            .build());
                }

                UNREACHABLE;
            });

        i->fetch_add(1, std::memory_order_acq_rel);
    }
}

void checkSendStatusUpdate(
    const ISendStatusPtr & previous, const ISendStatusPtr & updated)
{
    if (!previous) {
        return;
    }

    ASSERT_TRUE(updated);

    EXPECT_GE(
        updated->totalAttemptedToSendNotes(),
        previous->totalAttemptedToSendNotes());

    EXPECT_GE(
        updated->totalAttemptedToSendNotebooks(),
        previous->totalAttemptedToSendNotebooks());

    EXPECT_GE(
        updated->totalAttemptedToSendSavedSearches(),
        previous->totalAttemptedToSendSavedSearches());

    EXPECT_GE(
        updated->totalAttemptedToSendTags(),
        previous->totalAttemptedToSendTags());

    EXPECT_GE(
        updated->totalSuccessfullySentNotes(),
        previous->totalSuccessfullySentNotes());

    EXPECT_GE(
        updated->failedToSendNotes().size(),
        previous->failedToSendNotes().size());

    EXPECT_GE(
        updated->totalSuccessfullySentNotebooks(),
        previous->totalSuccessfullySentNotebooks());

    EXPECT_GE(
        updated->failedToSendNotebooks().size(),
        previous->failedToSendNotebooks().size());

    EXPECT_GE(
        updated->totalSuccessfullySentSavedSearches(),
        previous->totalSuccessfullySentSavedSearches());

    EXPECT_GE(
        updated->failedToSendSavedSearches().size(),
        previous->failedToSendSavedSearches().size());

    EXPECT_GE(
        updated->totalSuccessfullySentTags(),
        previous->totalSuccessfullySentTags());

    EXPECT_GE(
        updated->failedToSendTags().size(),
        previous->failedToSendTags().size());

    if (previous->needToRepeatIncrementalSync()) {
        EXPECT_TRUE(updated->needToRepeatIncrementalSync());
    }
}

} // namespace

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

const std::array gSenderTestData{
    generateTestData(SenderTestFlags{}),
    generateTestData(SenderTestFlags{} | SenderTestFlag::WithNewSavedSearches),
    generateTestData(
        SenderTestFlags{} | SenderTestFlag::WithUpdatedSavedSearches),
    generateTestData(
        SenderTestFlags{} | SenderTestFlag::WithNewSavedSearches |
        SenderTestFlag::WithUpdatedSavedSearches),
    generateTestData(
        SenderTestFlags{} | SenderTestFlag::WithNewUserOwnNotebooks),
    generateTestData(
        SenderTestFlags{} | SenderTestFlag::WithUpdatedUserOwnNotebooks),
    generateTestData(
        SenderTestFlags{} | SenderTestFlag::WithNewUserOwnNotebooks |
        SenderTestFlag::WithUpdatedUserOwnNotebooks),
    generateTestData(SenderTestFlags{} | SenderTestFlag::WithNewUserOwnTags),
    generateTestData(
        SenderTestFlags{} | SenderTestFlag::WithUpdatedUserOwnTags),
    generateTestData(
        SenderTestFlags{} | SenderTestFlag::WithNewUserOwnTags |
        SenderTestFlag::WithUpdatedUserOwnTags),
    generateTestData(SenderTestFlags{} | SenderTestFlag::WithNewUserOwnNotes),
    generateTestData(
        SenderTestFlags{} | SenderTestFlag::WithUpdatedUserOwnNotes),
    generateTestData(
        SenderTestFlags{} | SenderTestFlag::WithNewUserOwnNotes |
        SenderTestFlag::WithUpdatedUserOwnNotes),
    generateTestData(
        SenderTestFlags{} | SenderTestFlag::WithNewSavedSearches |
        SenderTestFlag::WithUpdatedSavedSearches |
        SenderTestFlag::WithNewUserOwnNotebooks |
        SenderTestFlag::WithUpdatedUserOwnNotebooks |
        SenderTestFlag::WithNewUserOwnTags |
        SenderTestFlag::WithUpdatedUserOwnTags |
        SenderTestFlag::WithNewUserOwnNotes |
        SenderTestFlag::WithUpdatedUserOwnNotes),
    generateTestData(
        SenderTestFlags{} | SenderTestFlag::WithUpdatedLinkedNotebooks),
    generateTestData(
        SenderTestFlags{} | SenderTestFlag::WithNewLinkedNotebooksTags),
    generateTestData(
        SenderTestFlags{} | SenderTestFlag::WithUpdatedLinkedNotebooksTags),
    generateTestData(
        SenderTestFlags{} | SenderTestFlag::WithNewLinkedNotebooksTags |
        SenderTestFlag::WithUpdatedLinkedNotebooksTags),
    generateTestData(
        SenderTestFlags{} | SenderTestFlag::WithUpdatedLinkedNotebooks |
        SenderTestFlag::WithNewLinkedNotebooksNotes),
    generateTestData(
        SenderTestFlags{} | SenderTestFlag::WithUpdatedLinkedNotebooks |
        SenderTestFlag::WithUpdatedLinkedNotebooksNotes),
    generateTestData(
        SenderTestFlags{} | SenderTestFlag::WithUpdatedLinkedNotebooks |
        SenderTestFlag::WithNewLinkedNotebooksNotes |
        SenderTestFlag::WithUpdatedLinkedNotebooksNotes),
    generateTestData(
        SenderTestFlags{} | SenderTestFlag::WithUpdatedLinkedNotebooks |
        SenderTestFlag::WithNewLinkedNotebooksTags |
        SenderTestFlag::WithUpdatedLinkedNotebooksTags |
        SenderTestFlag::WithNewLinkedNotebooksNotes |
        SenderTestFlag::WithUpdatedLinkedNotebooksNotes),
    generateTestData(
        SenderTestFlags{} | SenderTestFlag::WithNewSavedSearches |
        SenderTestFlag::WithUpdatedSavedSearches |
        SenderTestFlag::WithNewUserOwnNotebooks |
        SenderTestFlag::WithUpdatedUserOwnNotebooks |
        SenderTestFlag::WithNewUserOwnTags |
        SenderTestFlag::WithUpdatedUserOwnTags |
        SenderTestFlag::WithNewUserOwnNotes |
        SenderTestFlag::WithUpdatedUserOwnNotes |
        SenderTestFlag::WithUpdatedLinkedNotebooks |
        SenderTestFlag::WithNewLinkedNotebooksTags |
        SenderTestFlag::WithUpdatedLinkedNotebooksTags |
        SenderTestFlag::WithNewLinkedNotebooksNotes |
        SenderTestFlag::WithUpdatedLinkedNotebooksNotes)};

class SenderDataTest :
    public SenderTest,
    public testing::WithParamInterface<SenderTestData>
{};

INSTANTIATE_TEST_SUITE_P(
    SenderDataTestInstance, SenderDataTest, testing::ValuesIn(gSenderTestData));

TEST_P(SenderDataTest, SenderDataTest)
{
    const auto sender = std::make_shared<Sender>(
        m_account, m_mockLocalStorage, m_mockSyncStateStorage,
        m_mockNoteStoreProvider, qevercloud::newRequestContext(),
        qevercloud::newRetryPolicy());

    const auto & testData = GetParam();
    SentData sentData;

    const std::optional<QString> nullLinkedNotebookGuid;

    struct DataPutToLocalStorage
    {
        QList<qevercloud::SavedSearch> m_savedSearches;
        QList<qevercloud::Tag> m_tags;
        QList<qevercloud::Notebook> m_notebooks;
        QList<qevercloud::Note> m_notes;
    };

    DataPutToLocalStorage dataPutToLocalStorage;

    const auto now = QDateTime::currentMSecsSinceEpoch();
    QHash<qevercloud::Guid, qevercloud::Timestamp> linkedNotebookLastSyncTimes;
    for (const auto & linkedNotebook: qAsConst(testData.m_linkedNotebooks)) {
        linkedNotebookLastSyncTimes[linkedNotebook.guid().value()] = now;
    }

    EXPECT_CALL(*m_mockSyncStateStorage, getSyncState(m_account))
        .WillOnce(Return(std::make_shared<SyncState>(
            testData.m_maxUserOwnUsn, now, testData.m_maxLinkedNotebookUsns,
            linkedNotebookLastSyncTimes)));

    const std::shared_ptr<mocks::qevercloud::MockINoteStore>
        mockUserOwnNoteStore =
            std::make_shared<StrictMock<mocks::qevercloud::MockINoteStore>>();

    EXPECT_CALL(*mockUserOwnNoteStore, linkedNotebookGuid)
        .WillRepeatedly(ReturnRef(nullLinkedNotebookGuid));

    if (!testData.m_newSavedSearches.isEmpty() ||
        !testData.m_updatedSavedSearches.isEmpty() ||
        !testData.m_newUserOwnNotebooks.isEmpty() ||
        !testData.m_updatedUserOwnNotebooks.isEmpty() ||
        !testData.m_newUserOwnNotes.isEmpty() ||
        !testData.m_updatedUserOwnNotes.isEmpty() ||
        !testData.m_newUserOwnTags.isEmpty() ||
        !testData.m_updatedUserOwnTags.isEmpty())
    {
        setupUserOwnNoteStoreMock(testData, mockUserOwnNoteStore, sentData);

        if (!testData.m_newSavedSearches.isEmpty() ||
            !testData.m_updatedSavedSearches.isEmpty() ||
            !testData.m_newUserOwnNotebooks.isEmpty() ||
            !testData.m_updatedUserOwnNotebooks.isEmpty() ||
            !testData.m_newUserOwnTags.isEmpty() ||
            !testData.m_updatedUserOwnTags.isEmpty())
        {
            EXPECT_CALL(*m_mockNoteStoreProvider, userOwnNoteStore)
                .WillRepeatedly(Return(
                    threading::makeReadyFuture<qevercloud::INoteStorePtr>(
                        mockUserOwnNoteStore)));
        }

        if (!testData.m_newUserOwnNotes.isEmpty() ||
            !testData.m_updatedUserOwnNotes.isEmpty())
        {
            EXPECT_CALL(*m_mockNoteStoreProvider, noteStore)
                .WillRepeatedly(Return(
                    threading::makeReadyFuture<qevercloud::INoteStorePtr>(
                        mockUserOwnNoteStore)));
        }
    }

    QHash<qevercloud::Guid, std::shared_ptr<mocks::qevercloud::MockINoteStore>>
        linkedNotebookNoteStores;

    setupLinkedNotebookNoteStoreMocks(
        testData, linkedNotebookNoteStores, sentData);

    EXPECT_CALL(*m_mockNoteStoreProvider, linkedNotebookNoteStore)
        .WillRepeatedly(
            [&](const qevercloud::Guid & guid,
                [[maybe_unused]] const qevercloud::IRequestContextPtr & ctx,
                [[maybe_unused]] const qevercloud::IRetryPolicyPtr &
                    retryPolicy) {
                const auto nit = linkedNotebookNoteStores.constFind(guid);
                if (Q_UNLIKELY(nit == linkedNotebookNoteStores.constEnd())) {
                    return threading::makeExceptionalFuture<
                        qevercloud::INoteStorePtr>(
                        RuntimeError{ErrorString{QStringLiteral(
                            "Linked notebook note store not found")}});
                }

                EXPECT_CALL(*nit.value(), linkedNotebookGuid)
                    .WillRepeatedly(
                        [guid =
                             std::make_shared<std::optional<qevercloud::Guid>>(
                                 guid)]()
                            -> const std::optional<qevercloud::Guid> & {
                            return *guid;
                        });

                return threading::makeReadyFuture<qevercloud::INoteStorePtr>(
                    nit.value());
            });

    EXPECT_CALL(*m_mockLocalStorage, findNotebookByLocalId)
        .WillRepeatedly([&](const QString & notebookLocalId) {
            const auto findNotebook =
                [&notebookLocalId](
                    const QList<qevercloud::Notebook> & notebooks)
                -> std::optional<qevercloud::Notebook> {
                const auto it = std::find_if(
                    notebooks.constBegin(), notebooks.constEnd(),
                    [&](const qevercloud::Notebook & notebook) {
                        return notebook.localId() == notebookLocalId;
                    });
                if (it != notebooks.constEnd()) {
                    return *it;
                }

                return std::nullopt;
            };

            auto notebook = findNotebook(testData.m_newUserOwnNotebooks);

            if (!notebook) {
                notebook = findNotebook(testData.m_updatedUserOwnNotebooks);
            }

            if (!notebook) {
                notebook = findNotebook(testData.m_updatedLinkedNotebooks);
            }

            if (notebook) {
                return threading::makeReadyFuture<
                    std::optional<qevercloud::Notebook>>(std::move(notebook));
            }

            return threading::makeReadyFuture<
                std::optional<qevercloud::Notebook>>(
                qevercloud::NotebookBuilder{}
                    .setLocalId(notebookLocalId)
                    .setGuid(UidGenerator::Generate())
                    .setName(QStringLiteral("Notebook"))
                    .setUpdateSequenceNum(1)
                    .build());
        });

    EXPECT_CALL(*m_mockNoteStoreProvider, noteStore)
        .WillRepeatedly([&](const QString & notebookLocalId,
                            [[maybe_unused]] const qevercloud::
                                IRequestContextPtr & ctx,
                            [[maybe_unused]] const qevercloud::IRetryPolicyPtr &
                                retryPolicy) {
            const auto findNotebook =
                [&](const QList<qevercloud::Notebook> & notebooks)
                -> std::optional<qevercloud::Notebook> {
                const auto it = std::find_if(
                    notebooks.constBegin(), notebooks.constEnd(),
                    [&](const qevercloud::Notebook & notebook) {
                        return notebook.localId() == notebookLocalId;
                    });
                if (it != notebooks.constEnd()) {
                    return *it;
                }

                return std::nullopt;
            };

            auto notebook = findNotebook(testData.m_newUserOwnNotebooks);

            if (!notebook) {
                notebook = findNotebook(testData.m_updatedUserOwnNotebooks);
            }

            if (!notebook) {
                notebook = findNotebook(testData.m_updatedLinkedNotebooks);
            }

            if (!notebook) {
                return threading::makeReadyFuture<qevercloud::INoteStorePtr>(
                    mockUserOwnNoteStore);
            }

            if (!notebook->linkedNotebookGuid()) {
                return threading::makeReadyFuture<qevercloud::INoteStorePtr>(
                    mockUserOwnNoteStore);
            }

            const auto it = linkedNotebookNoteStores.constFind(
                *notebook->linkedNotebookGuid());
            if (Q_UNLIKELY(it == linkedNotebookNoteStores.constEnd())) {
                return threading::makeExceptionalFuture<
                    qevercloud::INoteStorePtr>(
                    RuntimeError{ErrorString{QStringLiteral(
                        "Note store for linked notebook not found")}});
            }

            EXPECT_CALL(*it.value(), linkedNotebookGuid)
                .WillRepeatedly(
                    [guid = std::make_shared<std::optional<qevercloud::Guid>>(
                         it.key())]()
                        -> const std::optional<qevercloud::Guid> & {
                        return *guid;
                    });

            return threading::makeReadyFuture<qevercloud::INoteStorePtr>(
                it.value());
        });

    const auto listSavedSearchesOptions = [] {
        local_storage::ILocalStorage::ListSavedSearchesOptions options;
        options.m_filters.m_locallyModifiedFilter =
            local_storage::ILocalStorage::ListObjectsFilter::Include;
        return options;
    }();

    EXPECT_CALL(
        *m_mockLocalStorage, listSavedSearches(listSavedSearchesOptions))
        .WillOnce(
            Return(threading::makeReadyFuture<QList<qevercloud::SavedSearch>>(
                QList<qevercloud::SavedSearch>{}
                << testData.m_newSavedSearches
                << testData.m_updatedSavedSearches)));

    if (!testData.m_newSavedSearches.isEmpty() ||
        !testData.m_updatedSavedSearches.isEmpty())
    {
        EXPECT_CALL(*m_mockLocalStorage, putSavedSearch)
            .WillRepeatedly([&](const qevercloud::SavedSearch & savedSearch) {
                dataPutToLocalStorage.m_savedSearches << savedSearch;
                return threading::makeReadyFuture();
            });
    }

    const auto listTagsOptions = [] {
        local_storage::ILocalStorage::ListTagsOptions options;
        options.m_filters.m_locallyModifiedFilter =
            local_storage::ILocalStorage::ListObjectsFilter::Include;
        return options;
    }();

    EXPECT_CALL(*m_mockLocalStorage, listTags(listTagsOptions))
        .WillOnce(Return(threading::makeReadyFuture<QList<qevercloud::Tag>>(
            QList<qevercloud::Tag>{}
            << testData.m_newUserOwnTags << testData.m_updatedUserOwnTags
            << testData.m_newLinkedNotebooksTags
            << testData.m_updatedLinkedNotebooksTags)));

    if (!testData.m_newUserOwnTags.isEmpty() ||
        !testData.m_updatedUserOwnTags.isEmpty() ||
        !testData.m_newLinkedNotebooksTags.isEmpty() ||
        !testData.m_updatedLinkedNotebooksTags.isEmpty())
    {
        EXPECT_CALL(*m_mockLocalStorage, putTag)
            .WillRepeatedly([&](const qevercloud::Tag & tag) {
                dataPutToLocalStorage.m_tags << tag;
                return threading::makeReadyFuture();
            });
    }

    const auto listNotebooksOptions = [] {
        local_storage::ILocalStorage::ListNotebooksOptions options;
        options.m_filters.m_locallyModifiedFilter =
            local_storage::ILocalStorage::ListObjectsFilter::Include;
        return options;
    }();

    EXPECT_CALL(*m_mockLocalStorage, listNotebooks(listNotebooksOptions))
        .WillOnce(
            Return(threading::makeReadyFuture<QList<qevercloud::Notebook>>(
                QList<qevercloud::Notebook>{}
                << testData.m_newUserOwnNotebooks
                << testData.m_updatedUserOwnNotebooks
                << testData.m_updatedLinkedNotebooks)));

    if (!testData.m_newUserOwnNotebooks.isEmpty() ||
        !testData.m_updatedUserOwnNotebooks.isEmpty() ||
        !testData.m_updatedLinkedNotebooks.isEmpty())
    {
        EXPECT_CALL(*m_mockLocalStorage, putNotebook)
            .WillRepeatedly([&](const qevercloud::Notebook & notebook) {
                dataPutToLocalStorage.m_notebooks << notebook;
                return threading::makeReadyFuture();
            });
    }

    const auto listNotesOptions = [] {
        local_storage::ILocalStorage::ListNotesOptions options;
        options.m_filters.m_locallyModifiedFilter =
            local_storage::ILocalStorage::ListObjectsFilter::Include;
        return options;
    }();

    const auto fetchNoteOptions =
        local_storage::ILocalStorage::FetchNoteOptions{} |
        local_storage::ILocalStorage::FetchNoteOption::WithResourceMetadata |
        local_storage::ILocalStorage::FetchNoteOption::WithResourceBinaryData;

    EXPECT_CALL(
        *m_mockLocalStorage, listNotes(fetchNoteOptions, listNotesOptions))
        .WillOnce(Return(threading::makeReadyFuture<QList<qevercloud::Note>>(
            QList<qevercloud::Note>{}
            << testData.m_newUserOwnNotes << testData.m_updatedUserOwnNotes
            << testData.m_newLinkedNotebooksNotes
            << testData.m_updatedLinkedNotebooksNotes)));

    if (!testData.m_newUserOwnNotes.isEmpty() ||
        !testData.m_updatedUserOwnNotes.isEmpty() ||
        !testData.m_newLinkedNotebooksNotes.isEmpty() ||
        !testData.m_updatedLinkedNotebooksNotes.isEmpty())
    {
        EXPECT_CALL(*m_mockLocalStorage, putNote)
            .WillRepeatedly([&](const qevercloud::Note & note) {
                dataPutToLocalStorage.m_notes << note;
                return threading::makeReadyFuture();
            });
    }

    const auto canceler =
        std::make_shared<utility::cancelers::ManualCanceler>();

    struct Callback : public ISender::ICallback
    {
        void onUserOwnSendStatusUpdate(ISendStatusPtr sendStatus) override
        {
            checkSendStatusUpdate(m_userOwnSendStatus, sendStatus);
            m_userOwnSendStatus = sendStatus;
        }

        void onLinkedNotebookSendStatusUpdate(
            const qevercloud::Guid & linkedNotebookGuid,
            ISendStatusPtr sendStatus) override
        {
            auto & linkedNotebookSendStatus =
                m_linkedNotebookSendStatuses[linkedNotebookGuid];

            checkSendStatusUpdate(linkedNotebookSendStatus, sendStatus);
            linkedNotebookSendStatus = sendStatus;
        }

        ISendStatusPtr m_userOwnSendStatus;
        QHash<qevercloud::Guid, ISendStatusPtr> m_linkedNotebookSendStatuses;
    };

    const auto callback = std::make_shared<Callback>();

    auto resultFuture = sender->send(canceler, callback);
    ASSERT_TRUE(resultFuture.isFinished());

    ASSERT_EQ(resultFuture.resultCount(), 1);
    const auto result = resultFuture.result();

    // === Checking the result

    ASSERT_TRUE(result.userOwnResult);

    // === Notes ===

    EXPECT_EQ(
        result.userOwnResult->totalAttemptedToSendNotes(),
        testData.m_newUserOwnNotes.size() +
            testData.m_updatedUserOwnNotes.size());

    EXPECT_EQ(
        result.userOwnResult->totalSuccessfullySentNotes(),
        result.userOwnResult->totalAttemptedToSendNotes());

    EXPECT_TRUE(result.userOwnResult->failedToSendNotes().isEmpty());

    // === Notebooks ===

    EXPECT_EQ(
        result.userOwnResult->totalAttemptedToSendNotebooks(),
        testData.m_newUserOwnNotebooks.size() +
            testData.m_updatedUserOwnNotebooks.size());

    EXPECT_EQ(
        result.userOwnResult->totalSuccessfullySentNotebooks(),
        result.userOwnResult->totalAttemptedToSendNotebooks());

    EXPECT_TRUE(result.userOwnResult->failedToSendNotebooks().isEmpty());

    // === Tags ===

    EXPECT_EQ(
        result.userOwnResult->totalAttemptedToSendTags(),
        testData.m_newUserOwnTags.size() +
            testData.m_updatedUserOwnTags.size());

    EXPECT_EQ(
        result.userOwnResult->totalSuccessfullySentTags(),
        result.userOwnResult->totalAttemptedToSendTags());

    EXPECT_TRUE(result.userOwnResult->failedToSendTags().isEmpty());

    // === Saved searches ===

    EXPECT_EQ(
        result.userOwnResult->totalAttemptedToSendSavedSearches(),
        testData.m_newSavedSearches.size() +
            testData.m_updatedSavedSearches.size());

    EXPECT_EQ(
        result.userOwnResult->totalSuccessfullySentSavedSearches(),
        result.userOwnResult->totalAttemptedToSendSavedSearches());

    EXPECT_TRUE(result.userOwnResult->failedToSendSavedSearches().isEmpty());

    EXPECT_FALSE(result.userOwnResult->needToRepeatIncrementalSync());

    // Stuff from linked notebooks

    EXPECT_LE(
        result.linkedNotebookResults.size(), testData.m_linkedNotebooks.size());

    const quint64 totalLinkedNotebooksAttemptedToSendNotes = [&] {
        quint64 count = 0;
        for (const auto it:
             qevercloud::toRange(qAsConst(result.linkedNotebookResults))) {
            count += it.value()->totalAttemptedToSendNotes();
        }
        return count;
    }();
    EXPECT_EQ(
        totalLinkedNotebooksAttemptedToSendNotes,
        testData.m_newLinkedNotebooksNotes.size() +
            testData.m_updatedLinkedNotebooksNotes.size());

    for (const auto it:
         qevercloud::toRange(qAsConst(result.linkedNotebookResults))) {
        const qevercloud::Guid & linkedNotebookGuid = it.key();
        const ISendStatusPtr & sendStatus = it.value();
        EXPECT_TRUE(sendStatus);
        if (Q_UNLIKELY(!sendStatus)) {
            continue;
        }

        const auto lit = std::find_if(
            testData.m_linkedNotebooks.constBegin(),
            testData.m_linkedNotebooks.constEnd(),
            [&](const qevercloud::LinkedNotebook & linkedNotebook) {
                return linkedNotebook.guid() == linkedNotebookGuid;
            });
        EXPECT_NE(lit, testData.m_linkedNotebooks.constEnd());

        const int tagCount = [&] {
            int count = 0;
            const auto countTags = [&](const QList<qevercloud::Tag> & tags) {
                for (const auto & tag: qAsConst(tags)) {
                    if (tag.linkedNotebookGuid() == linkedNotebookGuid) {
                        ++count;
                    }
                }
            };
            countTags(testData.m_newLinkedNotebooksTags);
            countTags(testData.m_updatedLinkedNotebooksTags);
            return count;
        }();
        EXPECT_EQ(sendStatus->totalAttemptedToSendTags(), tagCount);

        const int notebookCount = [&] {
            int count = 0;
            for (const auto & notebook:
                 qAsConst(testData.m_updatedLinkedNotebooks)) {
                if (notebook.linkedNotebookGuid() == linkedNotebookGuid) {
                    ++count;
                }
            }
            return count;
        }();
        EXPECT_EQ(sendStatus->totalAttemptedToSendNotebooks(), notebookCount);

        EXPECT_EQ(sendStatus->totalAttemptedToSendSavedSearches(), 0);
        EXPECT_FALSE(sendStatus->needToRepeatIncrementalSync());
    }

    // Checking sent data vs data sent to local storage

    ASSERT_EQ(
        dataPutToLocalStorage.m_savedSearches.size(),
        sentData.m_sentSavedSearches.size());

    ASSERT_EQ(dataPutToLocalStorage.m_tags.size(), sentData.m_sentTags.size());
    ASSERT_EQ(
        dataPutToLocalStorage.m_notebooks.size(),
        sentData.m_sentNotebooks.size());

    ASSERT_EQ(
        dataPutToLocalStorage.m_notes.size(), sentData.m_sentNotes.size());

    for (const auto & savedSearch:
         qAsConst(dataPutToLocalStorage.m_savedSearches)) {
        const auto it = std::find_if(
            sentData.m_sentSavedSearches.constBegin(),
            sentData.m_sentSavedSearches.constEnd(),
            [&](const qevercloud::SavedSearch & s) {
                return s.localId() == savedSearch.localId();
            });
        ASSERT_NE(it, sentData.m_sentSavedSearches.constEnd());
        EXPECT_EQ(*it, savedSearch);
    }

    for (const auto & notebook: qAsConst(dataPutToLocalStorage.m_notebooks)) {
        const auto it = std::find_if(
            sentData.m_sentNotebooks.constBegin(),
            sentData.m_sentNotebooks.constEnd(),
            [&](const qevercloud::Notebook & n) {
                return n.localId() == notebook.localId();
            });
        ASSERT_NE(it, sentData.m_sentNotebooks.constEnd());
        EXPECT_EQ(*it, notebook);
    }

    for (const auto & note: qAsConst(dataPutToLocalStorage.m_notes)) {
        const auto it = std::find_if(
            sentData.m_sentNotes.constBegin(), sentData.m_sentNotes.constEnd(),
            [&](const qevercloud::Note & n) {
                return n.localId() == note.localId();
            });
        ASSERT_NE(it, sentData.m_sentNotes.constEnd());
        EXPECT_EQ(*it, note);
    }

    for (const auto & tag: qAsConst(dataPutToLocalStorage.m_tags)) {
        const auto it = std::find_if(
            sentData.m_sentTags.constBegin(), sentData.m_sentTags.constEnd(),
            [&](const qevercloud::Tag & t) {
                return t.localId() == tag.localId();
            });
        ASSERT_NE(it, sentData.m_sentTags.constEnd());
        EXPECT_EQ(*it, tag);
    }
}

TEST_P(SenderDataTest, TolerateSendingFailures)
{
    const auto sender = std::make_shared<Sender>(
        m_account, m_mockLocalStorage, m_mockSyncStateStorage,
        m_mockNoteStoreProvider, qevercloud::newRequestContext(),
        qevercloud::newRetryPolicy());

    const auto & testData = GetParam();
    SentData sentData;

    const std::optional<QString> nullLinkedNotebookGuid;

    struct DataPutToLocalStorage
    {
        QList<qevercloud::SavedSearch> m_savedSearches;
        QList<qevercloud::Tag> m_tags;
        QList<qevercloud::Notebook> m_notebooks;
        QList<qevercloud::Note> m_notes;
    };

    DataPutToLocalStorage dataPutToLocalStorage;

    const auto now = QDateTime::currentMSecsSinceEpoch();
    QHash<qevercloud::Guid, qevercloud::Timestamp> linkedNotebookLastSyncTimes;
    for (const auto & linkedNotebook: qAsConst(testData.m_linkedNotebooks)) {
        linkedNotebookLastSyncTimes[linkedNotebook.guid().value()] = now;
    }

    EXPECT_CALL(*m_mockSyncStateStorage, getSyncState(m_account))
        .WillOnce(Return(std::make_shared<SyncState>(
            testData.m_maxUserOwnUsn, now, testData.m_maxLinkedNotebookUsns,
            linkedNotebookLastSyncTimes)));

    const std::shared_ptr<mocks::qevercloud::MockINoteStore>
        mockUserOwnNoteStore =
            std::make_shared<StrictMock<mocks::qevercloud::MockINoteStore>>();

    EXPECT_CALL(*mockUserOwnNoteStore, linkedNotebookGuid)
        .WillRepeatedly(ReturnRef(nullLinkedNotebookGuid));

    if (!testData.m_newSavedSearches.isEmpty() ||
        !testData.m_updatedSavedSearches.isEmpty() ||
        !testData.m_newUserOwnNotebooks.isEmpty() ||
        !testData.m_updatedUserOwnNotebooks.isEmpty() ||
        !testData.m_newUserOwnNotes.isEmpty() ||
        !testData.m_updatedUserOwnNotes.isEmpty() ||
        !testData.m_newUserOwnTags.isEmpty() ||
        !testData.m_updatedUserOwnTags.isEmpty())
    {
        setupUserOwnNoteStoreMock(
            testData, mockUserOwnNoteStore, sentData,
            NoteStoreBehaviour::WithFailures);

        if (!testData.m_newSavedSearches.isEmpty() ||
            !testData.m_updatedSavedSearches.isEmpty() ||
            !testData.m_newUserOwnNotebooks.isEmpty() ||
            !testData.m_updatedUserOwnNotebooks.isEmpty() ||
            !testData.m_newUserOwnTags.isEmpty() ||
            !testData.m_updatedUserOwnTags.isEmpty())
        {
            EXPECT_CALL(*m_mockNoteStoreProvider, userOwnNoteStore)
                .WillRepeatedly(Return(
                    threading::makeReadyFuture<qevercloud::INoteStorePtr>(
                        mockUserOwnNoteStore)));
        }

        if (!testData.m_newUserOwnNotes.isEmpty() ||
            !testData.m_updatedUserOwnNotes.isEmpty())
        {
            EXPECT_CALL(*m_mockNoteStoreProvider, noteStore)
                .WillRepeatedly(Return(
                    threading::makeReadyFuture<qevercloud::INoteStorePtr>(
                        mockUserOwnNoteStore)));
        }
    }

    QHash<qevercloud::Guid, std::shared_ptr<mocks::qevercloud::MockINoteStore>>
        linkedNotebookNoteStores;

    setupLinkedNotebookNoteStoreMocks(
        testData, linkedNotebookNoteStores, sentData,
        NoteStoreBehaviour::WithFailures);

    EXPECT_CALL(*m_mockNoteStoreProvider, linkedNotebookNoteStore)
        .WillRepeatedly(
            [&](const qevercloud::Guid & guid,
                [[maybe_unused]] const qevercloud::IRequestContextPtr & ctx,
                [[maybe_unused]] const qevercloud::IRetryPolicyPtr &
                    retryPolicy) {
                const auto nit = linkedNotebookNoteStores.constFind(guid);
                if (Q_UNLIKELY(nit == linkedNotebookNoteStores.constEnd())) {
                    return threading::makeExceptionalFuture<
                        qevercloud::INoteStorePtr>(
                        RuntimeError{ErrorString{QStringLiteral(
                            "Linked notebook note store not found")}});
                }

                EXPECT_CALL(*nit.value(), linkedNotebookGuid)
                    .WillRepeatedly(
                        [guid =
                             std::make_shared<std::optional<qevercloud::Guid>>(
                                 guid)]()
                            -> const std::optional<qevercloud::Guid> & {
                            return *guid;
                        });

                return threading::makeReadyFuture<qevercloud::INoteStorePtr>(
                    nit.value());
            });

    EXPECT_CALL(*m_mockLocalStorage, findNotebookByLocalId)
        .WillRepeatedly([&](const QString & notebookLocalId) {
            const auto findNotebook =
                [&notebookLocalId](
                    const QList<qevercloud::Notebook> & notebooks)
                -> std::optional<qevercloud::Notebook> {
                const auto it = std::find_if(
                    notebooks.constBegin(), notebooks.constEnd(),
                    [&](const qevercloud::Notebook & notebook) {
                        return notebook.localId() == notebookLocalId;
                    });
                if (it != notebooks.constEnd()) {
                    return *it;
                }

                return std::nullopt;
            };

            auto notebook = findNotebook(testData.m_newUserOwnNotebooks);

            if (!notebook) {
                notebook = findNotebook(testData.m_updatedUserOwnNotebooks);
            }

            if (!notebook) {
                notebook = findNotebook(testData.m_updatedLinkedNotebooks);
            }

            if (notebook) {
                return threading::makeReadyFuture<
                    std::optional<qevercloud::Notebook>>(std::move(notebook));
            }

            return threading::makeReadyFuture<
                std::optional<qevercloud::Notebook>>(
                qevercloud::NotebookBuilder{}
                    .setLocalId(notebookLocalId)
                    .setGuid(UidGenerator::Generate())
                    .setName(QStringLiteral("Notebook"))
                    .setUpdateSequenceNum(1)
                    .build());
        });

    EXPECT_CALL(*m_mockNoteStoreProvider, noteStore)
        .WillRepeatedly([&](const QString & notebookLocalId,
                            [[maybe_unused]] const qevercloud::
                                IRequestContextPtr & ctx,
                            [[maybe_unused]] const qevercloud::IRetryPolicyPtr &
                                retryPolicy) {
            const auto findNotebook =
                [&](const QList<qevercloud::Notebook> & notebooks)
                -> std::optional<qevercloud::Notebook> {
                const auto it = std::find_if(
                    notebooks.constBegin(), notebooks.constEnd(),
                    [&](const qevercloud::Notebook & notebook) {
                        return notebook.localId() == notebookLocalId;
                    });
                if (it != notebooks.constEnd()) {
                    return *it;
                }

                return std::nullopt;
            };

            auto notebook = findNotebook(testData.m_newUserOwnNotebooks);

            if (!notebook) {
                notebook = findNotebook(testData.m_updatedUserOwnNotebooks);
            }

            if (!notebook) {
                notebook = findNotebook(testData.m_updatedLinkedNotebooks);
            }

            if (!notebook) {
                return threading::makeReadyFuture<qevercloud::INoteStorePtr>(
                    mockUserOwnNoteStore);
            }

            if (!notebook->linkedNotebookGuid()) {
                return threading::makeReadyFuture<qevercloud::INoteStorePtr>(
                    mockUserOwnNoteStore);
            }

            const auto it = linkedNotebookNoteStores.constFind(
                *notebook->linkedNotebookGuid());
            if (Q_UNLIKELY(it == linkedNotebookNoteStores.constEnd())) {
                return threading::makeExceptionalFuture<
                    qevercloud::INoteStorePtr>(
                    RuntimeError{ErrorString{QStringLiteral(
                        "Note store for linked notebook not found")}});
            }

            EXPECT_CALL(*it.value(), linkedNotebookGuid)
                .WillRepeatedly(
                    [guid = std::make_shared<std::optional<qevercloud::Guid>>(
                         it.key())]()
                        -> const std::optional<qevercloud::Guid> & {
                        return *guid;
                    });

            return threading::makeReadyFuture<qevercloud::INoteStorePtr>(
                it.value());
        });

    const auto listSavedSearchesOptions = [] {
        local_storage::ILocalStorage::ListSavedSearchesOptions options;
        options.m_filters.m_locallyModifiedFilter =
            local_storage::ILocalStorage::ListObjectsFilter::Include;
        return options;
    }();

    EXPECT_CALL(
        *m_mockLocalStorage, listSavedSearches(listSavedSearchesOptions))
        .WillOnce(
            Return(threading::makeReadyFuture<QList<qevercloud::SavedSearch>>(
                QList<qevercloud::SavedSearch>{}
                << testData.m_newSavedSearches
                << testData.m_updatedSavedSearches)));

    if (!testData.m_newSavedSearches.isEmpty() ||
        !testData.m_updatedSavedSearches.isEmpty())
    {
        EXPECT_CALL(*m_mockLocalStorage, putSavedSearch)
            .WillRepeatedly([&](const qevercloud::SavedSearch & savedSearch) {
                dataPutToLocalStorage.m_savedSearches << savedSearch;
                return threading::makeReadyFuture();
            });
    }

    const auto listTagsOptions = [] {
        local_storage::ILocalStorage::ListTagsOptions options;
        options.m_filters.m_locallyModifiedFilter =
            local_storage::ILocalStorage::ListObjectsFilter::Include;
        return options;
    }();

    EXPECT_CALL(*m_mockLocalStorage, listTags(listTagsOptions))
        .WillOnce(Return(threading::makeReadyFuture<QList<qevercloud::Tag>>(
            QList<qevercloud::Tag>{}
            << testData.m_newUserOwnTags << testData.m_updatedUserOwnTags
            << testData.m_newLinkedNotebooksTags
            << testData.m_updatedLinkedNotebooksTags)));

    if (!testData.m_newUserOwnTags.isEmpty() ||
        !testData.m_updatedUserOwnTags.isEmpty() ||
        !testData.m_newLinkedNotebooksTags.isEmpty() ||
        !testData.m_updatedLinkedNotebooksTags.isEmpty())
    {
        EXPECT_CALL(*m_mockLocalStorage, putTag)
            .WillRepeatedly([&](const qevercloud::Tag & tag) {
                dataPutToLocalStorage.m_tags << tag;
                return threading::makeReadyFuture();
            });
    }

    const auto listNotebooksOptions = [] {
        local_storage::ILocalStorage::ListNotebooksOptions options;
        options.m_filters.m_locallyModifiedFilter =
            local_storage::ILocalStorage::ListObjectsFilter::Include;
        return options;
    }();

    EXPECT_CALL(*m_mockLocalStorage, listNotebooks(listNotebooksOptions))
        .WillOnce(
            Return(threading::makeReadyFuture<QList<qevercloud::Notebook>>(
                QList<qevercloud::Notebook>{}
                << testData.m_newUserOwnNotebooks
                << testData.m_updatedUserOwnNotebooks
                << testData.m_updatedLinkedNotebooks)));

    if (!testData.m_newUserOwnNotebooks.isEmpty() ||
        !testData.m_updatedUserOwnNotebooks.isEmpty() ||
        !testData.m_updatedLinkedNotebooks.isEmpty())
    {
        EXPECT_CALL(*m_mockLocalStorage, putNotebook)
            .WillRepeatedly([&](const qevercloud::Notebook & notebook) {
                dataPutToLocalStorage.m_notebooks << notebook;
                return threading::makeReadyFuture();
            });
    }

    const auto listNotesOptions = [] {
        local_storage::ILocalStorage::ListNotesOptions options;
        options.m_filters.m_locallyModifiedFilter =
            local_storage::ILocalStorage::ListObjectsFilter::Include;
        return options;
    }();

    const auto fetchNoteOptions =
        local_storage::ILocalStorage::FetchNoteOptions{} |
        local_storage::ILocalStorage::FetchNoteOption::WithResourceMetadata |
        local_storage::ILocalStorage::FetchNoteOption::WithResourceBinaryData;

    EXPECT_CALL(
        *m_mockLocalStorage, listNotes(fetchNoteOptions, listNotesOptions))
        .WillOnce(Return(threading::makeReadyFuture<QList<qevercloud::Note>>(
            QList<qevercloud::Note>{}
            << testData.m_newUserOwnNotes << testData.m_updatedUserOwnNotes
            << testData.m_newLinkedNotebooksNotes
            << testData.m_updatedLinkedNotebooksNotes)));

    if (!testData.m_newUserOwnNotes.isEmpty() ||
        !testData.m_updatedUserOwnNotes.isEmpty() ||
        !testData.m_newLinkedNotebooksNotes.isEmpty() ||
        !testData.m_updatedLinkedNotebooksNotes.isEmpty())
    {
        EXPECT_CALL(*m_mockLocalStorage, putNote)
            .WillRepeatedly([&](const qevercloud::Note & note) {
                dataPutToLocalStorage.m_notes << note;
                return threading::makeReadyFuture();
            });
    }

    const auto canceler =
        std::make_shared<utility::cancelers::ManualCanceler>();

    struct Callback : public ISender::ICallback
    {
        void onUserOwnSendStatusUpdate(ISendStatusPtr sendStatus) override
        {
            checkSendStatusUpdate(m_userOwnSendStatus, sendStatus);
            m_userOwnSendStatus = sendStatus;
        }

        void onLinkedNotebookSendStatusUpdate(
            const qevercloud::Guid & linkedNotebookGuid,
            ISendStatusPtr sendStatus) override
        {
            auto & linkedNotebookSendStatus =
                m_linkedNotebookSendStatuses[linkedNotebookGuid];

            checkSendStatusUpdate(linkedNotebookSendStatus, sendStatus);
            linkedNotebookSendStatus = sendStatus;
        }

        ISendStatusPtr m_userOwnSendStatus;
        QHash<qevercloud::Guid, ISendStatusPtr> m_linkedNotebookSendStatuses;
    };

    const auto callback = std::make_shared<Callback>();

    auto resultFuture = sender->send(canceler, callback);
    ASSERT_TRUE(resultFuture.isFinished());

    ASSERT_EQ(resultFuture.resultCount(), 1);
    const auto result = resultFuture.result();

    // === Checking the result

    ASSERT_TRUE(result.userOwnResult);

    // === Notes ===

    EXPECT_LE(
        result.userOwnResult->totalAttemptedToSendNotes(),
        testData.m_newUserOwnNotes.size() +
            testData.m_updatedUserOwnNotes.size());

    if (!testData.m_newUserOwnNotes.isEmpty() ||
        !testData.m_updatedUserOwnNotes.isEmpty())
    {
        EXPECT_FALSE(result.userOwnResult->failedToSendNotes().isEmpty());
    }

    EXPECT_GE(
        result.userOwnResult->totalSuccessfullySentNotes() +
            static_cast<quint64>(std::max<int>(
                result.userOwnResult->failedToSendNotes().size(), 0)),
        result.userOwnResult->totalAttemptedToSendNotes());

    // === Notebooks ===

    EXPECT_EQ(
        result.userOwnResult->totalAttemptedToSendNotebooks(),
        testData.m_newUserOwnNotebooks.size() +
            testData.m_updatedUserOwnNotebooks.size());

    if (!testData.m_newUserOwnNotebooks.isEmpty() ||
        !testData.m_updatedUserOwnNotebooks.isEmpty())
    {
        EXPECT_FALSE(result.userOwnResult->failedToSendNotebooks().isEmpty());
    }

    EXPECT_EQ(
        result.userOwnResult->totalSuccessfullySentNotebooks() +
            static_cast<quint64>(std::max<int>(
                result.userOwnResult->failedToSendNotebooks().size(), 0)),
        result.userOwnResult->totalAttemptedToSendNotebooks());

    // === Tags ===

    EXPECT_LE(
        result.userOwnResult->totalAttemptedToSendTags(),
        testData.m_newUserOwnTags.size() +
            testData.m_updatedUserOwnTags.size());

    if (!testData.m_newUserOwnTags.isEmpty() ||
        !testData.m_updatedUserOwnTags.isEmpty())
    {
        EXPECT_FALSE(result.userOwnResult->failedToSendTags().isEmpty());
    }

    EXPECT_GE(
        result.userOwnResult->totalSuccessfullySentTags() +
            static_cast<quint64>(std::max<int>(
                result.userOwnResult->failedToSendTags().size(), 0)),
        result.userOwnResult->totalAttemptedToSendTags());

    // === Saved searches ===

    EXPECT_EQ(
        result.userOwnResult->totalAttemptedToSendSavedSearches(),
        testData.m_newSavedSearches.size() +
            testData.m_updatedSavedSearches.size());

    if (!testData.m_newSavedSearches.isEmpty() ||
        !testData.m_updatedSavedSearches.isEmpty())
    {
        EXPECT_FALSE(
            result.userOwnResult->failedToSendSavedSearches().isEmpty());
    }

    EXPECT_EQ(
        result.userOwnResult->totalSuccessfullySentSavedSearches() +
            static_cast<quint64>(std::max<int>(
                result.userOwnResult->failedToSendSavedSearches().size(), 0)),
        result.userOwnResult->totalAttemptedToSendSavedSearches());

    EXPECT_FALSE(result.userOwnResult->needToRepeatIncrementalSync());

    // Stuff from linked notebooks

    EXPECT_LE(
        result.linkedNotebookResults.size(), testData.m_linkedNotebooks.size());

    const quint64 totalLinkedNotebooksAttemptedToSendNotes = [&] {
        quint64 count = 0;
        for (const auto it:
             qevercloud::toRange(qAsConst(result.linkedNotebookResults))) {
            count += it.value()->totalAttemptedToSendNotes();
        }
        return count;
    }();
    EXPECT_EQ(
        totalLinkedNotebooksAttemptedToSendNotes,
        testData.m_newLinkedNotebooksNotes.size() +
            testData.m_updatedLinkedNotebooksNotes.size());

    for (const auto it:
         qevercloud::toRange(qAsConst(result.linkedNotebookResults))) {
        const qevercloud::Guid & linkedNotebookGuid = it.key();
        const ISendStatusPtr & sendStatus = it.value();
        EXPECT_TRUE(sendStatus);
        if (Q_UNLIKELY(!sendStatus)) {
            continue;
        }

        const auto lit = std::find_if(
            testData.m_linkedNotebooks.constBegin(),
            testData.m_linkedNotebooks.constEnd(),
            [&](const qevercloud::LinkedNotebook & linkedNotebook) {
                return linkedNotebook.guid() == linkedNotebookGuid;
            });
        EXPECT_NE(lit, testData.m_linkedNotebooks.constEnd());

        const int tagCount = [&] {
            int count = 0;
            const auto countTags = [&](const QList<qevercloud::Tag> & tags) {
                for (const auto & tag: qAsConst(tags)) {
                    if (tag.linkedNotebookGuid() == linkedNotebookGuid) {
                        ++count;
                    }
                }
            };
            countTags(testData.m_newLinkedNotebooksTags);
            countTags(testData.m_updatedLinkedNotebooksTags);
            return count;
        }();
        EXPECT_LE(sendStatus->totalAttemptedToSendTags(), tagCount);

        const int notebookCount = [&] {
            int count = 0;
            for (const auto & notebook:
                 qAsConst(testData.m_updatedLinkedNotebooks)) {
                if (notebook.linkedNotebookGuid() == linkedNotebookGuid) {
                    ++count;
                }
            }
            return count;
        }();
        EXPECT_EQ(sendStatus->totalAttemptedToSendNotebooks(), notebookCount);

        EXPECT_EQ(sendStatus->totalAttemptedToSendSavedSearches(), 0);
        EXPECT_FALSE(sendStatus->needToRepeatIncrementalSync());
    }

    // Checking sent data vs data sent to local storage

    if (!testData.m_newSavedSearches.isEmpty() ||
        !testData.m_updatedSavedSearches.isEmpty())
    {
        EXPECT_FALSE(sentData.m_failedToSendSavedSearches.isEmpty());
    }

    ASSERT_EQ(
        dataPutToLocalStorage.m_savedSearches.size(),
        sentData.m_sentSavedSearches.size());

    if (!testData.m_newUserOwnTags.isEmpty() ||
        !testData.m_updatedUserOwnTags.isEmpty() ||
        !testData.m_newLinkedNotebooksTags.isEmpty() ||
        !testData.m_updatedLinkedNotebooksTags.isEmpty())
    {
        EXPECT_FALSE(sentData.m_failedToSendTags.isEmpty());
    }

    ASSERT_EQ(dataPutToLocalStorage.m_tags.size(), sentData.m_sentTags.size());

    if (!testData.m_newUserOwnNotebooks.isEmpty() ||
        !testData.m_updatedUserOwnNotebooks.isEmpty() ||
        !testData.m_updatedLinkedNotebooks.isEmpty())
    {
        EXPECT_FALSE(sentData.m_failedToSendNotebooks.isEmpty());
    }

    ASSERT_EQ(
        dataPutToLocalStorage.m_notebooks.size(),
        sentData.m_sentNotebooks.size());

    if (!testData.m_newUserOwnNotes.isEmpty() ||
        !testData.m_updatedUserOwnNotes.isEmpty() ||
        !testData.m_newLinkedNotebooksNotes.isEmpty() ||
        !testData.m_updatedLinkedNotebooksNotes.isEmpty())
    {
        EXPECT_FALSE(sentData.m_failedToSendNotes.isEmpty());
    }

    ASSERT_EQ(
        dataPutToLocalStorage.m_notes.size(), sentData.m_sentNotes.size());

    for (const auto & savedSearch:
         qAsConst(dataPutToLocalStorage.m_savedSearches)) {
        const auto it = std::find_if(
            sentData.m_sentSavedSearches.constBegin(),
            sentData.m_sentSavedSearches.constEnd(),
            [&](const qevercloud::SavedSearch & s) {
                return s.localId() == savedSearch.localId();
            });
        ASSERT_NE(it, sentData.m_sentSavedSearches.constEnd());
        EXPECT_EQ(*it, savedSearch);
    }

    for (const auto & notebook: qAsConst(dataPutToLocalStorage.m_notebooks)) {
        const auto it = std::find_if(
            sentData.m_sentNotebooks.constBegin(),
            sentData.m_sentNotebooks.constEnd(),
            [&](const qevercloud::Notebook & n) {
                return n.localId() == notebook.localId();
            });
        ASSERT_NE(it, sentData.m_sentNotebooks.constEnd());
        EXPECT_EQ(*it, notebook);
    }

    for (const auto & note: qAsConst(dataPutToLocalStorage.m_notes)) {
        const auto it = std::find_if(
            sentData.m_sentNotes.constBegin(), sentData.m_sentNotes.constEnd(),
            [&](const qevercloud::Note & n) {
                return n.localId() == note.localId();
            });
        ASSERT_NE(it, sentData.m_sentNotes.constEnd());

        // If note contains tag local ids corresponding to tags which failed
        // to be sent, its locally modified flag would stay enabled so that
        // during the next sync sending the note would be attempted again
        if (note.isLocallyModified()) {
            qevercloud::Note noteCopy{note};
            noteCopy.setLocallyModified(false);
            EXPECT_EQ(*it, noteCopy);
        }
        else {
            EXPECT_EQ(*it, note);
        }
    }

    for (const auto & tag: qAsConst(dataPutToLocalStorage.m_tags)) {
        const auto it = std::find_if(
            sentData.m_sentTags.constBegin(), sentData.m_sentTags.constEnd(),
            [&](const qevercloud::Tag & t) {
                return t.localId() == tag.localId();
            });
        ASSERT_NE(it, sentData.m_sentTags.constEnd());
        EXPECT_EQ(*it, tag);
    }
}

enum class StopSynchronizationReason
{
    RateLimitExceeded,
    AuthenticationExpired,
};

struct StopSynchronizationTestData
{
    SenderTestFlag flag;
    StopSynchronizationReason reason;
};

constexpr std::array gStopSynchronizationTestData{
    StopSynchronizationTestData{
        SenderTestFlag::WithNewSavedSearches,
        StopSynchronizationReason::RateLimitExceeded},
    StopSynchronizationTestData{
        SenderTestFlag::WithNewSavedSearches,
        StopSynchronizationReason::AuthenticationExpired},
    StopSynchronizationTestData{
        SenderTestFlag::WithUpdatedSavedSearches,
        StopSynchronizationReason::RateLimitExceeded},
    StopSynchronizationTestData{
        SenderTestFlag::WithUpdatedSavedSearches,
        StopSynchronizationReason::AuthenticationExpired},
    StopSynchronizationTestData{
        SenderTestFlag::WithNewUserOwnNotebooks,
        StopSynchronizationReason::RateLimitExceeded},
    StopSynchronizationTestData{
        SenderTestFlag::WithNewUserOwnNotebooks,
        StopSynchronizationReason::AuthenticationExpired},
    StopSynchronizationTestData{
        SenderTestFlag::WithUpdatedUserOwnNotebooks,
        StopSynchronizationReason::RateLimitExceeded},
    StopSynchronizationTestData{
        SenderTestFlag::WithUpdatedUserOwnNotebooks,
        StopSynchronizationReason::AuthenticationExpired},
    StopSynchronizationTestData{
        SenderTestFlag::WithNewUserOwnNotes,
        StopSynchronizationReason::RateLimitExceeded},
    StopSynchronizationTestData{
        SenderTestFlag::WithNewUserOwnNotes,
        StopSynchronizationReason::AuthenticationExpired},
    StopSynchronizationTestData{
        SenderTestFlag::WithUpdatedUserOwnNotes,
        StopSynchronizationReason::RateLimitExceeded},
    StopSynchronizationTestData{
        SenderTestFlag::WithUpdatedUserOwnNotes,
        StopSynchronizationReason::AuthenticationExpired},
    StopSynchronizationTestData{
        SenderTestFlag::WithNewUserOwnTags,
        StopSynchronizationReason::RateLimitExceeded},
    StopSynchronizationTestData{
        SenderTestFlag::WithNewUserOwnTags,
        StopSynchronizationReason::AuthenticationExpired},
    StopSynchronizationTestData{
        SenderTestFlag::WithUpdatedUserOwnTags,
        StopSynchronizationReason::RateLimitExceeded},
    StopSynchronizationTestData{
        SenderTestFlag::WithUpdatedUserOwnTags,
        StopSynchronizationReason::AuthenticationExpired},
    StopSynchronizationTestData{
        SenderTestFlag::WithUpdatedLinkedNotebooks,
        StopSynchronizationReason::RateLimitExceeded},
    StopSynchronizationTestData{
        SenderTestFlag::WithUpdatedLinkedNotebooks,
        StopSynchronizationReason::AuthenticationExpired},
    /*
    StopSynchronizationTestData{
        SenderTestFlag::WithNewLinkedNotebooksNotes,
        StopSynchronizationReason::RateLimitExceeded},
    StopSynchronizationTestData{
        SenderTestFlag::WithNewLinkedNotebooksNotes,
        StopSynchronizationReason::AuthenticationExpired},
    StopSynchronizationTestData{
        SenderTestFlag::WithUpdatedLinkedNotebooksNotes,
        StopSynchronizationReason::RateLimitExceeded},
    StopSynchronizationTestData{
        SenderTestFlag::WithUpdatedLinkedNotebooksNotes,
        StopSynchronizationReason::AuthenticationExpired},
    StopSynchronizationTestData{
        SenderTestFlag::WithNewLinkedNotebooksTags,
        StopSynchronizationReason::RateLimitExceeded},
    StopSynchronizationTestData{
        SenderTestFlag::WithNewLinkedNotebooksTags,
        StopSynchronizationReason::AuthenticationExpired},
    StopSynchronizationTestData{
        SenderTestFlag::WithUpdatedLinkedNotebooksTags,
        StopSynchronizationReason::RateLimitExceeded},
    StopSynchronizationTestData{
        SenderTestFlag::WithUpdatedLinkedNotebooksTags,
        StopSynchronizationReason::AuthenticationExpired},
    */
};

class SenderStopSynchronizationTest :
    public SenderTest,
    public testing::WithParamInterface<StopSynchronizationTestData>
{};

INSTANTIATE_TEST_SUITE_P(
    SenderStopSynchronizationTestInstance, SenderStopSynchronizationTest,
    testing::ValuesIn(gStopSynchronizationTestData));

TEST_P(SenderStopSynchronizationTest, StopSynchronizationOnRelevantError)
{
    const auto sender = std::make_shared<Sender>(
        m_account, m_mockLocalStorage, m_mockSyncStateStorage,
        m_mockNoteStoreProvider, qevercloud::newRequestContext(),
        qevercloud::newRetryPolicy());

    SentData sentData;

    const std::optional<QString> nullLinkedNotebookGuid;

    struct DataPutToLocalStorage
    {
        QList<qevercloud::SavedSearch> m_savedSearches;
        QList<qevercloud::Tag> m_tags;
        QList<qevercloud::Notebook> m_notebooks;
        QList<qevercloud::Note> m_notes;
    };

    DataPutToLocalStorage dataPutToLocalStorage;

    const auto & errorTestData = GetParam();
    const auto noteStoreBehaviour =
        errorTestData.reason == StopSynchronizationReason::RateLimitExceeded
        ? NoteStoreBehaviour::WithRateLimitExceeding
        : NoteStoreBehaviour::WithAuthenticationExpiring;

    const auto testData = generateTestData(SenderTestFlags{errorTestData.flag});

    const auto now = QDateTime::currentMSecsSinceEpoch();
    QHash<qevercloud::Guid, qevercloud::Timestamp> linkedNotebookLastSyncTimes;
    for (const auto & linkedNotebook: qAsConst(testData.m_linkedNotebooks)) {
        linkedNotebookLastSyncTimes[linkedNotebook.guid().value()] = now;
    }

    EXPECT_CALL(*m_mockSyncStateStorage, getSyncState(m_account))
        .WillOnce(Return(std::make_shared<SyncState>(
            testData.m_maxUserOwnUsn, now, testData.m_maxLinkedNotebookUsns,
            linkedNotebookLastSyncTimes)));

    const std::shared_ptr<mocks::qevercloud::MockINoteStore>
        mockUserOwnNoteStore =
            std::make_shared<StrictMock<mocks::qevercloud::MockINoteStore>>();

    EXPECT_CALL(*mockUserOwnNoteStore, linkedNotebookGuid)
        .WillRepeatedly(ReturnRef(nullLinkedNotebookGuid));

    if (!testData.m_newSavedSearches.isEmpty() ||
        !testData.m_updatedSavedSearches.isEmpty() ||
        !testData.m_newUserOwnNotebooks.isEmpty() ||
        !testData.m_updatedUserOwnNotebooks.isEmpty() ||
        !testData.m_newUserOwnNotes.isEmpty() ||
        !testData.m_updatedUserOwnNotes.isEmpty() ||
        !testData.m_newUserOwnTags.isEmpty() ||
        !testData.m_updatedUserOwnTags.isEmpty())
    {
        setupUserOwnNoteStoreMock(
            testData, mockUserOwnNoteStore, sentData, noteStoreBehaviour);

        if (!testData.m_newSavedSearches.isEmpty() ||
            !testData.m_updatedSavedSearches.isEmpty() ||
            !testData.m_newUserOwnNotebooks.isEmpty() ||
            !testData.m_updatedUserOwnNotebooks.isEmpty() ||
            !testData.m_newUserOwnTags.isEmpty() ||
            !testData.m_updatedUserOwnTags.isEmpty())
        {
            EXPECT_CALL(*m_mockNoteStoreProvider, userOwnNoteStore)
                .WillRepeatedly(Return(
                    threading::makeReadyFuture<qevercloud::INoteStorePtr>(
                        mockUserOwnNoteStore)));
        }

        if (!testData.m_newUserOwnNotes.isEmpty() ||
            !testData.m_updatedUserOwnNotes.isEmpty())
        {
            EXPECT_CALL(*m_mockNoteStoreProvider, noteStore)
                .WillRepeatedly(Return(
                    threading::makeReadyFuture<qevercloud::INoteStorePtr>(
                        mockUserOwnNoteStore)));
        }
    }

    QHash<qevercloud::Guid, std::shared_ptr<mocks::qevercloud::MockINoteStore>>
        linkedNotebookNoteStores;

    setupLinkedNotebookNoteStoreMocks(
        testData, linkedNotebookNoteStores, sentData, noteStoreBehaviour);

    EXPECT_CALL(*m_mockNoteStoreProvider, linkedNotebookNoteStore)
        .WillRepeatedly(
            [&](const qevercloud::Guid & guid,
                [[maybe_unused]] const qevercloud::IRequestContextPtr & ctx,
                [[maybe_unused]] const qevercloud::IRetryPolicyPtr &
                    retryPolicy) {
                const auto nit = linkedNotebookNoteStores.constFind(guid);
                if (Q_UNLIKELY(nit == linkedNotebookNoteStores.constEnd())) {
                    return threading::makeExceptionalFuture<
                        qevercloud::INoteStorePtr>(
                        RuntimeError{ErrorString{QStringLiteral(
                            "Linked notebook note store not found")}});
                }

                EXPECT_CALL(*nit.value(), linkedNotebookGuid)
                    .WillRepeatedly(
                        [guid =
                             std::make_shared<std::optional<qevercloud::Guid>>(
                                 guid)]()
                            -> const std::optional<qevercloud::Guid> & {
                            return *guid;
                        });

                return threading::makeReadyFuture<qevercloud::INoteStorePtr>(
                    nit.value());
            });

    EXPECT_CALL(*m_mockLocalStorage, findNotebookByLocalId)
        .WillRepeatedly([&](const QString & notebookLocalId) {
            const auto findNotebook =
                [&notebookLocalId](
                    const QList<qevercloud::Notebook> & notebooks)
                -> std::optional<qevercloud::Notebook> {
                const auto it = std::find_if(
                    notebooks.constBegin(), notebooks.constEnd(),
                    [&](const qevercloud::Notebook & notebook) {
                        return notebook.localId() == notebookLocalId;
                    });
                if (it != notebooks.constEnd()) {
                    return *it;
                }

                return std::nullopt;
            };

            auto notebook = findNotebook(testData.m_newUserOwnNotebooks);

            if (!notebook) {
                notebook = findNotebook(testData.m_updatedUserOwnNotebooks);
            }

            if (!notebook) {
                notebook = findNotebook(testData.m_updatedLinkedNotebooks);
            }

            if (notebook) {
                return threading::makeReadyFuture<
                    std::optional<qevercloud::Notebook>>(std::move(notebook));
            }

            return threading::makeReadyFuture<
                std::optional<qevercloud::Notebook>>(
                qevercloud::NotebookBuilder{}
                    .setLocalId(notebookLocalId)
                    .setGuid(UidGenerator::Generate())
                    .setName(QStringLiteral("Notebook"))
                    .setUpdateSequenceNum(1)
                    .build());
        });

    EXPECT_CALL(*m_mockNoteStoreProvider, noteStore)
        .WillRepeatedly([&](const QString & notebookLocalId,
                            [[maybe_unused]] const qevercloud::
                                IRequestContextPtr & ctx,
                            [[maybe_unused]] const qevercloud::IRetryPolicyPtr &
                                retryPolicy) {
            const auto findNotebook =
                [&](const QList<qevercloud::Notebook> & notebooks)
                -> std::optional<qevercloud::Notebook> {
                const auto it = std::find_if(
                    notebooks.constBegin(), notebooks.constEnd(),
                    [&](const qevercloud::Notebook & notebook) {
                        return notebook.localId() == notebookLocalId;
                    });
                if (it != notebooks.constEnd()) {
                    return *it;
                }

                return std::nullopt;
            };

            auto notebook = findNotebook(testData.m_newUserOwnNotebooks);

            if (!notebook) {
                notebook = findNotebook(testData.m_updatedUserOwnNotebooks);
            }

            if (!notebook) {
                notebook = findNotebook(testData.m_updatedLinkedNotebooks);
            }

            if (!notebook) {
                return threading::makeReadyFuture<qevercloud::INoteStorePtr>(
                    mockUserOwnNoteStore);
            }

            if (!notebook->linkedNotebookGuid()) {
                return threading::makeReadyFuture<qevercloud::INoteStorePtr>(
                    mockUserOwnNoteStore);
            }

            const auto it = linkedNotebookNoteStores.constFind(
                *notebook->linkedNotebookGuid());
            if (Q_UNLIKELY(it == linkedNotebookNoteStores.constEnd())) {
                return threading::makeExceptionalFuture<
                    qevercloud::INoteStorePtr>(
                    RuntimeError{ErrorString{QStringLiteral(
                        "Note store for linked notebook not found")}});
            }

            EXPECT_CALL(*it.value(), linkedNotebookGuid)
                .WillRepeatedly(
                    [guid = std::make_shared<std::optional<qevercloud::Guid>>(
                         it.key())]()
                        -> const std::optional<qevercloud::Guid> & {
                        return *guid;
                    });

            return threading::makeReadyFuture<qevercloud::INoteStorePtr>(
                it.value());
        });

    const auto listSavedSearchesOptions = [] {
        local_storage::ILocalStorage::ListSavedSearchesOptions options;
        options.m_filters.m_locallyModifiedFilter =
            local_storage::ILocalStorage::ListObjectsFilter::Include;
        return options;
    }();

    EXPECT_CALL(
        *m_mockLocalStorage, listSavedSearches(listSavedSearchesOptions))
        .Times(AtMost(1))
        .WillRepeatedly(
            Return(threading::makeReadyFuture<QList<qevercloud::SavedSearch>>(
                QList<qevercloud::SavedSearch>{}
                << testData.m_newSavedSearches
                << testData.m_updatedSavedSearches)));

    if (!testData.m_newSavedSearches.isEmpty() ||
        !testData.m_updatedSavedSearches.isEmpty())
    {
        EXPECT_CALL(*m_mockLocalStorage, putSavedSearch)
            .WillRepeatedly([&](const qevercloud::SavedSearch & savedSearch) {
                dataPutToLocalStorage.m_savedSearches << savedSearch;
                return threading::makeReadyFuture();
            });
    }

    const auto listTagsOptions = [] {
        local_storage::ILocalStorage::ListTagsOptions options;
        options.m_filters.m_locallyModifiedFilter =
            local_storage::ILocalStorage::ListObjectsFilter::Include;
        return options;
    }();

    EXPECT_CALL(*m_mockLocalStorage, listTags(listTagsOptions))
        .Times(AtMost(1))
        .WillRepeatedly(Return(threading::makeReadyFuture<QList<qevercloud::Tag>>(
            QList<qevercloud::Tag>{}
            << testData.m_newUserOwnTags << testData.m_updatedUserOwnTags
            << testData.m_newLinkedNotebooksTags
            << testData.m_updatedLinkedNotebooksTags)));

    if (!testData.m_newUserOwnTags.isEmpty() ||
        !testData.m_updatedUserOwnTags.isEmpty() ||
        !testData.m_newLinkedNotebooksTags.isEmpty() ||
        !testData.m_updatedLinkedNotebooksTags.isEmpty())
    {
        EXPECT_CALL(*m_mockLocalStorage, putTag)
            .WillRepeatedly([&](const qevercloud::Tag & tag) {
                dataPutToLocalStorage.m_tags << tag;
                return threading::makeReadyFuture();
            });
    }

    const auto listNotebooksOptions = [] {
        local_storage::ILocalStorage::ListNotebooksOptions options;
        options.m_filters.m_locallyModifiedFilter =
            local_storage::ILocalStorage::ListObjectsFilter::Include;
        return options;
    }();

    EXPECT_CALL(*m_mockLocalStorage, listNotebooks(listNotebooksOptions))
        .Times(AtMost(1))
        .WillRepeatedly(
            Return(threading::makeReadyFuture<QList<qevercloud::Notebook>>(
                QList<qevercloud::Notebook>{}
                << testData.m_newUserOwnNotebooks
                << testData.m_updatedUserOwnNotebooks
                << testData.m_updatedLinkedNotebooks)));

    if (!testData.m_newUserOwnNotebooks.isEmpty() ||
        !testData.m_updatedUserOwnNotebooks.isEmpty() ||
        !testData.m_updatedLinkedNotebooks.isEmpty())
    {
        EXPECT_CALL(*m_mockLocalStorage, putNotebook)
            .WillRepeatedly([&](const qevercloud::Notebook & notebook) {
                dataPutToLocalStorage.m_notebooks << notebook;
                return threading::makeReadyFuture();
            });
    }

    const auto listNotesOptions = [] {
        local_storage::ILocalStorage::ListNotesOptions options;
        options.m_filters.m_locallyModifiedFilter =
            local_storage::ILocalStorage::ListObjectsFilter::Include;
        return options;
    }();

    const auto fetchNoteOptions =
        local_storage::ILocalStorage::FetchNoteOptions{} |
        local_storage::ILocalStorage::FetchNoteOption::WithResourceMetadata |
        local_storage::ILocalStorage::FetchNoteOption::WithResourceBinaryData;

    EXPECT_CALL(
        *m_mockLocalStorage, listNotes(fetchNoteOptions, listNotesOptions))
        .Times(AtMost(1))
        .WillRepeatedly(Return(threading::makeReadyFuture<QList<qevercloud::Note>>(
            QList<qevercloud::Note>{}
            << testData.m_newUserOwnNotes << testData.m_updatedUserOwnNotes
            << testData.m_newLinkedNotebooksNotes
            << testData.m_updatedLinkedNotebooksNotes)));

    if (!testData.m_newUserOwnNotes.isEmpty() ||
        !testData.m_updatedUserOwnNotes.isEmpty() ||
        !testData.m_newLinkedNotebooksNotes.isEmpty() ||
        !testData.m_updatedLinkedNotebooksNotes.isEmpty())
    {
        EXPECT_CALL(*m_mockLocalStorage, putNote)
            .WillRepeatedly([&](const qevercloud::Note & note) {
                dataPutToLocalStorage.m_notes << note;
                return threading::makeReadyFuture();
            });
    }

    const auto canceler =
        std::make_shared<utility::cancelers::ManualCanceler>();

    struct Callback : public ISender::ICallback
    {
        void onUserOwnSendStatusUpdate(ISendStatusPtr sendStatus) override
        {
            checkSendStatusUpdate(m_userOwnSendStatus, sendStatus);
            m_userOwnSendStatus = sendStatus;
        }

        void onLinkedNotebookSendStatusUpdate(
            const qevercloud::Guid & linkedNotebookGuid,
            ISendStatusPtr sendStatus) override
        {
            auto & linkedNotebookSendStatus =
                m_linkedNotebookSendStatuses[linkedNotebookGuid];

            checkSendStatusUpdate(linkedNotebookSendStatus, sendStatus);
            linkedNotebookSendStatus = sendStatus;
        }

        ISendStatusPtr m_userOwnSendStatus;
        QHash<qevercloud::Guid, ISendStatusPtr> m_linkedNotebookSendStatuses;
    };

    const auto callback = std::make_shared<Callback>();

    auto resultFuture = sender->send(canceler, callback);
    ASSERT_TRUE(resultFuture.isFinished());

    // TODO: continue from here
    /*
    try {
        Q_UNUSED(resultFuture.result());
    }
    catch (const QException & e) {
        std::cerr << e.what() << std::endl;
    }
    */
}

} // namespace quentier::synchronization::tests
