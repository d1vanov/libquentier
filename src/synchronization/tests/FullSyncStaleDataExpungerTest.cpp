/*
 * Copyright 2022-2023 Dmitry Ivanov
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

#include <synchronization/FullSyncStaleDataExpunger.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/local_storage/tests/mocks/MockILocalStorage.h>
#include <quentier/threading/Future.h>
#include <quentier/utility/UidGenerator.h>
#include <quentier/utility/cancelers/ManualCanceler.h>

#include <qevercloud/types/Note.h>
#include <qevercloud/types/Notebook.h>
#include <qevercloud/types/SavedSearch.h>
#include <qevercloud/types/Tag.h>
#include <qevercloud/types/builders/DataBuilder.h>
#include <qevercloud/types/builders/NoteBuilder.h>
#include <qevercloud/types/builders/NotebookBuilder.h>
#include <qevercloud/types/builders/ResourceBuilder.h>
#include <qevercloud/types/builders/SavedSearchBuilder.h>
#include <qevercloud/types/builders/TagBuilder.h>
#include <qevercloud/utility/ToRange.h>

#include <QCryptographicHash>
#include <QFlags>
#include <QHash>

#include <gtest/gtest.h>

#include <array>
#include <utility>

// clazy:excludeall=non-pod-global-static
// clazy:excludeall=returning-void-expression

namespace quentier::synchronization::tests {

using testing::AnyNumber;
using testing::StrictMock;

class FullSyncStaleDataExpungerTest : public testing::Test
{
protected:
    const std::shared_ptr<local_storage::tests::mocks::MockILocalStorage>
        m_mockLocalStorage = std::make_shared<
            StrictMock<local_storage::tests::mocks::MockILocalStorage>>();

    const utility::cancelers::ManualCancelerPtr m_manualCanceler =
        std::make_shared<utility::cancelers::ManualCanceler>();
};

TEST_F(FullSyncStaleDataExpungerTest, Ctor)
{
    EXPECT_NO_THROW(
        const auto fullSyncStaleDataExpunger =
            std::make_shared<FullSyncStaleDataExpunger>(m_mockLocalStorage));
}

TEST_F(FullSyncStaleDataExpungerTest, CtorNullLocalStorage)
{
    EXPECT_THROW(
        const auto fullSyncStaleDataExpunger =
            std::make_shared<FullSyncStaleDataExpunger>(nullptr),
        InvalidArgument);
}

struct FullSyncStaleDataExpungerTestData
{
    QHash<qevercloud::Guid, qevercloud::Notebook> m_unmodifiedNotebooks;
    QHash<qevercloud::Guid, qevercloud::Notebook> m_modifiedNotebooks;
    QHash<qevercloud::Guid, qevercloud::Tag> m_unmodifiedTags;
    QHash<qevercloud::Guid, qevercloud::Tag> m_modifiedTags;
    QHash<qevercloud::Guid, qevercloud::Note> m_unmodifiedNotes;
    QHash<qevercloud::Guid, qevercloud::Note> m_modifiedNotes;
    QHash<qevercloud::Guid, qevercloud::SavedSearch> m_unmodifiedSavedSearches;
    QHash<qevercloud::Guid, qevercloud::SavedSearch> m_modifiedSavedSearches;

    QSet<qevercloud::Guid> m_preservedNotebookGuids;
    QSet<qevercloud::Guid> m_preservedTagGuids;
    QSet<qevercloud::Guid> m_preservedNoteGuids;
    QSet<qevercloud::Guid> m_preservedSavedSearchGuids;

    std::optional<qevercloud::Guid> m_linkedNotebookGuid;
};

enum class FullSyncStaleDataExpungerTestDataOption
{
    WithUnmodifiedNotebooks = 1 << 0,
    WithModifiedNotebooks = 1 << 1,
    WithUnmodifiedTags = 1 << 2,
    WithModifiedTags = 1 << 3,
    WithUnmodifiedNotes = 1 << 4,
    WithModifiedNotes = 1 << 5,
    WithUnmodifiedSavedSearches = 1 << 6,
    WithModifiedSavedSearches = 1 << 7,
    WithPreservedNotebookGuids = 1 << 8,
    WithPreservedTagGuids = 1 << 9,
    WithPreservedNoteGuids = 1 << 10,
    WithPreservedSavedSearchGuids = 1 << 11,
    WithLinkedNotebookGuid = 1 << 12
};

Q_DECLARE_FLAGS(
    FullSyncStaleDataExpungerTestDataOptions,
    FullSyncStaleDataExpungerTestDataOption);

[[nodiscard]] FullSyncStaleDataExpungerTestData
    createFullSyncStaleDataExpungerTestData(
        FullSyncStaleDataExpungerTestDataOptions options)
{
    const auto createNotebook =
        [](const bool modified, const int counter,
           std::optional<qevercloud::Guid> linkedNotebookGuid) {
            return qevercloud::NotebookBuilder{}
                .setGuid(UidGenerator::Generate())
                .setLocalId(UidGenerator::Generate())
                .setUpdateSequenceNum(counter)
                .setName(QString::fromUtf8("Notebook #%1").arg(counter + 1))
                .setLocallyModified(modified)
                .setLinkedNotebookGuid(std::move(linkedNotebookGuid))
                .build();
        };

    const auto createNote = [](const bool modified, const int counter,
                               qevercloud::Guid notebookGuid,
                               QString notebookLocalId,
                               QList<qevercloud::Guid> tagGuids,
                               QStringList tagLocalIds) {
        const QByteArray sampleBodyData =
            QString::fromUtf8("some_data").toUtf8();
        const qint32 sampleBodyDataSize = sampleBodyData.size();
        const auto sampleBodyDataHash =
            QCryptographicHash::hash(sampleBodyData, QCryptographicHash::Md5);
        return qevercloud::NoteBuilder{}
            .setGuid(UidGenerator::Generate())
            .setLocalId(UidGenerator::Generate())
            .setUpdateSequenceNum(counter)
            .setTitle(QString::fromUtf8("Note #%1").arg(counter + 1))
            .setLocallyModified(modified)
            .setNotebookGuid(std::move(notebookGuid))
            .setNotebookLocalId(std::move(notebookLocalId))
            .setTagGuids(std::move(tagGuids))
            .setTagLocalIds(std::move(tagLocalIds))
            .setResources(
                QList<qevercloud::Resource>{}
                << qevercloud::ResourceBuilder{}
                       .setGuid(UidGenerator::Generate())
                       .setLocalId(UidGenerator::Generate())
                       .setData(qevercloud::DataBuilder{}
                                    .setBody(sampleBodyData)
                                    .setSize(sampleBodyDataSize)
                                    .setBodyHash(sampleBodyDataHash)
                                    .build())
                       .setUpdateSequenceNum(counter + 100)
                       .build())
            .build();
    };

    const auto createTag =
        [](const bool modified, const int counter,
           std::optional<qevercloud::Guid> linkedNotebookGuid) {
            return qevercloud::TagBuilder{}
                .setGuid(UidGenerator::Generate())
                .setLocalId(UidGenerator::Generate())
                .setUpdateSequenceNum(counter)
                .setName(QString::fromUtf8("Tag #%1").arg(counter + 1))
                .setLocallyModified(modified)
                .setLinkedNotebookGuid(std::move(linkedNotebookGuid))
                .build();
        };

    const auto createSavedSearch = [](const bool modified, const int counter) {
        return qevercloud::SavedSearchBuilder{}
            .setGuid(UidGenerator::Generate())
            .setLocalId(UidGenerator::Generate())
            .setUpdateSequenceNum(counter)
            .setName(QString::fromUtf8("Saved search #%1").arg(counter + 1))
            .setLocallyModified(modified)
            .build();
    };

    FullSyncStaleDataExpungerTestData result;
    constexpr int itemCount = 3;

    const std::optional<qevercloud::Guid> linkedNotebookGuid =
        (options.testFlag(
             FullSyncStaleDataExpungerTestDataOption::WithLinkedNotebookGuid)
             ? std::make_optional(UidGenerator::Generate())
             : std::optional<qevercloud::Guid>{});

    result.m_linkedNotebookGuid = linkedNotebookGuid;

    int notebookCounter = 0;
    if (options.testFlag(
            FullSyncStaleDataExpungerTestDataOption::WithUnmodifiedNotebooks))
    {
        for (int i = 0; i < itemCount; ++i) {
            auto notebook =
                createNotebook(false, notebookCounter, linkedNotebookGuid);
            Q_ASSERT(notebook.guid());

            const auto guid = *notebook.guid();
            result.m_unmodifiedNotebooks[guid] = std::move(notebook);

            ++notebookCounter;
        }
    }

    if (options.testFlag(
            FullSyncStaleDataExpungerTestDataOption::WithModifiedNotebooks))
    {
        for (int i = 0; i < itemCount; ++i) {
            auto notebook =
                createNotebook(true, notebookCounter, linkedNotebookGuid);
            Q_ASSERT(notebook.guid());

            const auto guid = *notebook.guid();
            result.m_modifiedNotebooks[guid] = std::move(notebook);

            ++notebookCounter;
        }
    }

    int tagCounter = 0;
    if (options.testFlag(
            FullSyncStaleDataExpungerTestDataOption::WithUnmodifiedTags))
    {
        for (int i = 0; i < itemCount; ++i) {
            auto tag = createTag(false, tagCounter, linkedNotebookGuid);
            Q_ASSERT(tag.guid());

            const auto guid = *tag.guid();
            result.m_unmodifiedTags[guid] = std::move(tag);

            ++tagCounter;
        }
    }

    if (options.testFlag(
            FullSyncStaleDataExpungerTestDataOption::WithModifiedTags)) {
        for (int i = 0; i < itemCount; ++i) {
            auto tag = createTag(true, tagCounter, linkedNotebookGuid);
            Q_ASSERT(tag.guid());

            const auto guid = *tag.guid();
            result.m_modifiedTags[guid] = std::move(tag);

            ++tagCounter;
        }
    }

    const QList<qevercloud::Notebook> notebooks = [&] {
        QList<qevercloud::Notebook> notebooks;
        for (const auto it:
             qevercloud::toRange(std::as_const(result.m_unmodifiedNotebooks))) {
            notebooks << it.value();
        }
        for (const auto it:
             qevercloud::toRange(std::as_const(result.m_modifiedNotebooks))) {
            notebooks << it.value();
        }
        return notebooks;
    }();

    const QList<qevercloud::Tag> tags = [&] {
        QList<qevercloud::Tag> tags;
        for (const auto it:
             qevercloud::toRange(std::as_const(result.m_unmodifiedTags))) {
            tags << it.value();
        }
        for (const auto it:
             qevercloud::toRange(std::as_const(result.m_modifiedTags))) {
            tags << it.value();
        }
        return tags;
    }();

    int noteCounter = 0;
    if (options.testFlag(
            FullSyncStaleDataExpungerTestDataOption::WithUnmodifiedNotes))
    {
        for (int i = 0; i < itemCount; ++i) {
            auto note = createNote(
                false, noteCounter,
                (notebooks.size() > i ? notebooks[i].guid().value()
                                      : UidGenerator::Generate()),
                (notebooks.size() > i ? notebooks[i].localId()
                                      : UidGenerator::Generate()),
                QList<qevercloud::Guid>{}
                    << (tags.size() > i ? tags[i].guid().value()
                                        : UidGenerator::Generate()),
                QStringList{}
                    << (tags.size() > i ? tags[i].localId()
                                        : UidGenerator::Generate()));
            Q_ASSERT(note.guid());

            const auto guid = *note.guid();
            result.m_unmodifiedNotes[guid] = std::move(note);

            ++noteCounter;
        }
    }

    if (options.testFlag(
            FullSyncStaleDataExpungerTestDataOption::WithModifiedNotes))
    {
        for (int i = 0; i < itemCount; ++i) {
            auto note = createNote(
                true, noteCounter,
                (notebooks.size() > i ? notebooks[i].guid().value()
                                      : UidGenerator::Generate()),
                (notebooks.size() > i ? notebooks[i].localId()
                                      : UidGenerator::Generate()),
                QList<qevercloud::Guid>{}
                    << (tags.size() > i ? tags[i].guid().value()
                                        : UidGenerator::Generate()),
                QStringList{}
                    << (tags.size() > i ? tags[i].localId()
                                        : UidGenerator::Generate()));
            Q_ASSERT(note.guid());

            const auto guid = *note.guid();
            result.m_modifiedNotes[guid] = std::move(note);

            ++noteCounter;
        }
    }

    int savedSearchCounter = 0;
    if (options.testFlag(FullSyncStaleDataExpungerTestDataOption::
                             WithUnmodifiedSavedSearches))
    {
        for (int i = 0; i < itemCount; ++i) {
            auto savedSearch = createSavedSearch(false, savedSearchCounter);
            Q_ASSERT(savedSearch.guid());

            const auto guid = *savedSearch.guid();
            result.m_unmodifiedSavedSearches[guid] = std::move(savedSearch);

            ++savedSearchCounter;
        }
    }

    if (options.testFlag(
            FullSyncStaleDataExpungerTestDataOption::WithModifiedSavedSearches))
    {
        for (int i = 0; i < itemCount; ++i) {
            auto savedSearch = createSavedSearch(true, savedSearchCounter);
            Q_ASSERT(savedSearch.guid());

            const auto guid = *savedSearch.guid();
            result.m_modifiedSavedSearches[guid] = std::move(savedSearch);

            ++savedSearchCounter;
        }
    }

    if (options.testFlag(FullSyncStaleDataExpungerTestDataOption::
                             WithPreservedNotebookGuids))
    {
        int counter = 0;
        for (const auto it:
             qevercloud::toRange(std::as_const(result.m_unmodifiedNotebooks))) {
            ++counter;
            if (counter % 2 == 0) {
                continue;
            }

            result.m_preservedNotebookGuids.insert(it.key());
        }

        counter = 0;
        for (const auto it:
             qevercloud::toRange(std::as_const(result.m_modifiedNotebooks))) {
            ++counter;
            if (counter % 2 == 0) {
                continue;
            }

            result.m_preservedNotebookGuids.insert(it.key());
        }

        if (result.m_preservedNotebookGuids.isEmpty()) {
            // Just add some random guids then to make sure nothing bad would
            // happen with them present
            for (int i = 0; i < itemCount; ++i) {
                result.m_preservedNotebookGuids.insert(
                    UidGenerator::Generate());
            }
        }
    }

    if (options.testFlag(
            FullSyncStaleDataExpungerTestDataOption::WithPreservedTagGuids))
    {
        int counter = 0;
        for (const auto it:
             qevercloud::toRange(std::as_const(result.m_unmodifiedTags))) {
            ++counter;
            if (counter % 2 == 0) {
                continue;
            }

            result.m_preservedTagGuids.insert(it.key());
        }

        for (const auto it:
             qevercloud::toRange(std::as_const(result.m_modifiedTags))) {
            ++counter;
            if (counter % 2 == 0) {
                continue;
            }

            result.m_preservedTagGuids.insert(it.key());
        }

        if (result.m_preservedTagGuids.isEmpty()) {
            // Just add some random guids then to make sure nothing bad would
            // happen with them present
            for (int i = 0; i < itemCount; ++i) {
                result.m_preservedTagGuids.insert(UidGenerator::Generate());
            }
        }
    }

    if (options.testFlag(
            FullSyncStaleDataExpungerTestDataOption::WithPreservedNoteGuids))
    {
        int counter = 0;
        for (const auto it:
             qevercloud::toRange(std::as_const(result.m_unmodifiedNotes))) {
            ++counter;
            if (counter % 2 == 0) {
                continue;
            }

            result.m_preservedNoteGuids.insert(it.key());
        }

        for (const auto it:
             qevercloud::toRange(std::as_const(result.m_modifiedNotes))) {
            ++counter;
            if (counter % 2 == 0) {
                continue;
            }

            result.m_preservedNoteGuids.insert(it.key());
        }

        if (result.m_preservedNoteGuids.isEmpty()) {
            // Just add some random guids then to make sure nothing bad would
            // happen with them present
            for (int i = 0; i < itemCount; ++i) {
                result.m_preservedNoteGuids.insert(UidGenerator::Generate());
            }
        }
    }

    if (options.testFlag(FullSyncStaleDataExpungerTestDataOption::
                             WithPreservedSavedSearchGuids))
    {
        int counter = 0;
        for (const auto it: qevercloud::toRange(
                 std::as_const(result.m_unmodifiedSavedSearches)))
        {
            ++counter;
            if (counter % 2 == 0) {
                continue;
            }

            result.m_preservedSavedSearchGuids.insert(it.key());
        }

        for (const auto it:
             qevercloud::toRange(std::as_const(result.m_modifiedSavedSearches)))
        {
            ++counter;
            if (counter % 2 == 0) {
                continue;
            }

            result.m_preservedSavedSearchGuids.insert(it.key());
        }

        if (result.m_preservedSavedSearchGuids.isEmpty()) {
            // Just add some random guids then to make sure nothing bad would
            // happen with them present
            for (int i = 0; i < itemCount; ++i) {
                result.m_preservedSavedSearchGuids.insert(
                    UidGenerator::Generate());
            }
        }
    }

    return result;
}

class FullSyncStaleDataExpungerDataTest :
    public FullSyncStaleDataExpungerTest,
    public testing::WithParamInterface<FullSyncStaleDataExpungerTestData>
{};

const std::array gFullSyncStaleDataExpungerTestData{
    FullSyncStaleDataExpungerTestData{},
    createFullSyncStaleDataExpungerTestData(
        FullSyncStaleDataExpungerTestDataOptions{
            FullSyncStaleDataExpungerTestDataOption::WithUnmodifiedNotebooks}),
    createFullSyncStaleDataExpungerTestData(
        FullSyncStaleDataExpungerTestDataOptions{
            FullSyncStaleDataExpungerTestDataOption::WithUnmodifiedTags}),
    createFullSyncStaleDataExpungerTestData(
        FullSyncStaleDataExpungerTestDataOptions{
            FullSyncStaleDataExpungerTestDataOption::WithUnmodifiedNotes}),
    createFullSyncStaleDataExpungerTestData(
        FullSyncStaleDataExpungerTestDataOptions{
            FullSyncStaleDataExpungerTestDataOption::
                WithUnmodifiedSavedSearches}),
    createFullSyncStaleDataExpungerTestData(
        FullSyncStaleDataExpungerTestDataOptions{
            FullSyncStaleDataExpungerTestDataOption::WithUnmodifiedNotebooks} |
        FullSyncStaleDataExpungerTestDataOption::WithUnmodifiedTags |
        FullSyncStaleDataExpungerTestDataOption::WithUnmodifiedNotes |
        FullSyncStaleDataExpungerTestDataOption::WithUnmodifiedSavedSearches),
    createFullSyncStaleDataExpungerTestData(
        FullSyncStaleDataExpungerTestDataOptions{
            FullSyncStaleDataExpungerTestDataOption::WithUnmodifiedNotebooks} |
        FullSyncStaleDataExpungerTestDataOption::WithLinkedNotebookGuid),
    createFullSyncStaleDataExpungerTestData(
        FullSyncStaleDataExpungerTestDataOptions{
            FullSyncStaleDataExpungerTestDataOption::WithUnmodifiedTags} |
        FullSyncStaleDataExpungerTestDataOption::WithLinkedNotebookGuid),
    createFullSyncStaleDataExpungerTestData(
        FullSyncStaleDataExpungerTestDataOptions{
            FullSyncStaleDataExpungerTestDataOption::WithUnmodifiedNotes} |
        FullSyncStaleDataExpungerTestDataOption::WithLinkedNotebookGuid),
    createFullSyncStaleDataExpungerTestData(
        FullSyncStaleDataExpungerTestDataOptions{
            FullSyncStaleDataExpungerTestDataOption::WithUnmodifiedNotebooks} |
        FullSyncStaleDataExpungerTestDataOption::WithUnmodifiedTags |
        FullSyncStaleDataExpungerTestDataOption::WithUnmodifiedNotes |
        FullSyncStaleDataExpungerTestDataOption::WithLinkedNotebookGuid),
    createFullSyncStaleDataExpungerTestData(
        FullSyncStaleDataExpungerTestDataOptions{
            FullSyncStaleDataExpungerTestDataOption::WithModifiedNotebooks}),
    createFullSyncStaleDataExpungerTestData(
        FullSyncStaleDataExpungerTestDataOptions{
            FullSyncStaleDataExpungerTestDataOption::WithModifiedTags}),
    createFullSyncStaleDataExpungerTestData(
        FullSyncStaleDataExpungerTestDataOptions{
            FullSyncStaleDataExpungerTestDataOption::WithModifiedNotes}),
    createFullSyncStaleDataExpungerTestData(
        FullSyncStaleDataExpungerTestDataOptions{
            FullSyncStaleDataExpungerTestDataOption::
                WithModifiedSavedSearches}),
    createFullSyncStaleDataExpungerTestData(
        FullSyncStaleDataExpungerTestDataOptions{
            FullSyncStaleDataExpungerTestDataOption::WithModifiedNotebooks} |
        FullSyncStaleDataExpungerTestDataOption::WithModifiedTags |
        FullSyncStaleDataExpungerTestDataOption::WithModifiedNotes |
        FullSyncStaleDataExpungerTestDataOption::WithModifiedSavedSearches),
    createFullSyncStaleDataExpungerTestData(
        FullSyncStaleDataExpungerTestDataOptions{
            FullSyncStaleDataExpungerTestDataOption::WithModifiedNotebooks} |
        FullSyncStaleDataExpungerTestDataOption::WithLinkedNotebookGuid),
    createFullSyncStaleDataExpungerTestData(
        FullSyncStaleDataExpungerTestDataOptions{
            FullSyncStaleDataExpungerTestDataOption::WithModifiedTags} |
        FullSyncStaleDataExpungerTestDataOption::WithLinkedNotebookGuid),
    createFullSyncStaleDataExpungerTestData(
        FullSyncStaleDataExpungerTestDataOptions{
            FullSyncStaleDataExpungerTestDataOption::WithModifiedNotes} |
        FullSyncStaleDataExpungerTestDataOption::WithLinkedNotebookGuid),
    createFullSyncStaleDataExpungerTestData(
        FullSyncStaleDataExpungerTestDataOptions{
            FullSyncStaleDataExpungerTestDataOption::WithModifiedNotebooks} |
        FullSyncStaleDataExpungerTestDataOption::WithModifiedTags |
        FullSyncStaleDataExpungerTestDataOption::WithModifiedNotes |
        FullSyncStaleDataExpungerTestDataOption::WithLinkedNotebookGuid),
    createFullSyncStaleDataExpungerTestData(
        FullSyncStaleDataExpungerTestDataOptions{
            FullSyncStaleDataExpungerTestDataOption::WithUnmodifiedNotebooks} |
        FullSyncStaleDataExpungerTestDataOption::WithModifiedNotebooks),
    createFullSyncStaleDataExpungerTestData(
        FullSyncStaleDataExpungerTestDataOptions{
            FullSyncStaleDataExpungerTestDataOption::WithUnmodifiedTags} |
        FullSyncStaleDataExpungerTestDataOption::WithModifiedTags),
    createFullSyncStaleDataExpungerTestData(
        FullSyncStaleDataExpungerTestDataOptions{
            FullSyncStaleDataExpungerTestDataOption::WithUnmodifiedNotes} |
        FullSyncStaleDataExpungerTestDataOption::WithModifiedNotes),
    createFullSyncStaleDataExpungerTestData(
        FullSyncStaleDataExpungerTestDataOptions{
            FullSyncStaleDataExpungerTestDataOption::
                WithUnmodifiedSavedSearches} |
        FullSyncStaleDataExpungerTestDataOption::WithModifiedSavedSearches),
    createFullSyncStaleDataExpungerTestData(
        FullSyncStaleDataExpungerTestDataOptions{
            FullSyncStaleDataExpungerTestDataOption::WithUnmodifiedNotebooks} |
        FullSyncStaleDataExpungerTestDataOption::WithModifiedNotebooks |
        FullSyncStaleDataExpungerTestDataOption::WithLinkedNotebookGuid),
    createFullSyncStaleDataExpungerTestData(
        FullSyncStaleDataExpungerTestDataOptions{
            FullSyncStaleDataExpungerTestDataOption::WithUnmodifiedTags} |
        FullSyncStaleDataExpungerTestDataOption::WithModifiedTags |
        FullSyncStaleDataExpungerTestDataOption::WithLinkedNotebookGuid),
    createFullSyncStaleDataExpungerTestData(
        FullSyncStaleDataExpungerTestDataOptions{
            FullSyncStaleDataExpungerTestDataOption::WithUnmodifiedNotes} |
        FullSyncStaleDataExpungerTestDataOption::WithModifiedNotes |
        FullSyncStaleDataExpungerTestDataOption::WithLinkedNotebookGuid),
    createFullSyncStaleDataExpungerTestData(
        FullSyncStaleDataExpungerTestDataOptions{
            FullSyncStaleDataExpungerTestDataOption::WithUnmodifiedNotebooks} |
        FullSyncStaleDataExpungerTestDataOption::WithModifiedNotebooks |
        FullSyncStaleDataExpungerTestDataOption::WithUnmodifiedNotes |
        FullSyncStaleDataExpungerTestDataOption::WithModifiedNotes |
        FullSyncStaleDataExpungerTestDataOption::WithUnmodifiedTags |
        FullSyncStaleDataExpungerTestDataOption::WithModifiedTags |
        FullSyncStaleDataExpungerTestDataOption::WithUnmodifiedSavedSearches |
        FullSyncStaleDataExpungerTestDataOption::WithModifiedSavedSearches),
    createFullSyncStaleDataExpungerTestData(
        FullSyncStaleDataExpungerTestDataOptions{
            FullSyncStaleDataExpungerTestDataOption::WithUnmodifiedNotebooks} |
        FullSyncStaleDataExpungerTestDataOption::WithModifiedNotebooks |
        FullSyncStaleDataExpungerTestDataOption::WithUnmodifiedNotes |
        FullSyncStaleDataExpungerTestDataOption::WithModifiedNotes |
        FullSyncStaleDataExpungerTestDataOption::WithUnmodifiedTags |
        FullSyncStaleDataExpungerTestDataOption::WithModifiedTags |
        FullSyncStaleDataExpungerTestDataOption::WithLinkedNotebookGuid),
    createFullSyncStaleDataExpungerTestData(
        FullSyncStaleDataExpungerTestDataOptions{
            FullSyncStaleDataExpungerTestDataOption::WithUnmodifiedNotebooks} |
        FullSyncStaleDataExpungerTestDataOption::WithPreservedNotebookGuids),
    createFullSyncStaleDataExpungerTestData(
        FullSyncStaleDataExpungerTestDataOptions{
            FullSyncStaleDataExpungerTestDataOption::WithModifiedNotebooks} |
        FullSyncStaleDataExpungerTestDataOption::WithPreservedNotebookGuids),
    createFullSyncStaleDataExpungerTestData(
        FullSyncStaleDataExpungerTestDataOptions{
            FullSyncStaleDataExpungerTestDataOption::WithUnmodifiedNotebooks} |
        FullSyncStaleDataExpungerTestDataOption::WithModifiedNotebooks |
        FullSyncStaleDataExpungerTestDataOption::WithPreservedNotebookGuids),
    createFullSyncStaleDataExpungerTestData(
        FullSyncStaleDataExpungerTestDataOptions{
            FullSyncStaleDataExpungerTestDataOption::WithUnmodifiedNotebooks} |
        FullSyncStaleDataExpungerTestDataOption::WithModifiedNotebooks |
        FullSyncStaleDataExpungerTestDataOption::WithPreservedNotebookGuids |
        FullSyncStaleDataExpungerTestDataOption::WithLinkedNotebookGuid),
    createFullSyncStaleDataExpungerTestData(
        FullSyncStaleDataExpungerTestDataOptions{
            FullSyncStaleDataExpungerTestDataOption::WithUnmodifiedTags} |
        FullSyncStaleDataExpungerTestDataOption::WithPreservedTagGuids),
    createFullSyncStaleDataExpungerTestData(
        FullSyncStaleDataExpungerTestDataOptions{
            FullSyncStaleDataExpungerTestDataOption::WithModifiedTags} |
        FullSyncStaleDataExpungerTestDataOption::WithPreservedTagGuids),
    createFullSyncStaleDataExpungerTestData(
        FullSyncStaleDataExpungerTestDataOptions{
            FullSyncStaleDataExpungerTestDataOption::WithUnmodifiedTags} |
        FullSyncStaleDataExpungerTestDataOption::WithModifiedTags |
        FullSyncStaleDataExpungerTestDataOption::WithPreservedTagGuids),
    createFullSyncStaleDataExpungerTestData(
        FullSyncStaleDataExpungerTestDataOptions{
            FullSyncStaleDataExpungerTestDataOption::WithUnmodifiedTags} |
        FullSyncStaleDataExpungerTestDataOption::WithModifiedTags |
        FullSyncStaleDataExpungerTestDataOption::WithPreservedTagGuids |
        FullSyncStaleDataExpungerTestDataOption::WithLinkedNotebookGuid),
    createFullSyncStaleDataExpungerTestData(
        FullSyncStaleDataExpungerTestDataOptions{
            FullSyncStaleDataExpungerTestDataOption::WithUnmodifiedNotes} |
        FullSyncStaleDataExpungerTestDataOption::WithPreservedNoteGuids),
    createFullSyncStaleDataExpungerTestData(
        FullSyncStaleDataExpungerTestDataOptions{
            FullSyncStaleDataExpungerTestDataOption::WithModifiedNotes} |
        FullSyncStaleDataExpungerTestDataOption::WithPreservedNoteGuids),
    createFullSyncStaleDataExpungerTestData(
        FullSyncStaleDataExpungerTestDataOptions{
            FullSyncStaleDataExpungerTestDataOption::WithUnmodifiedNotes} |
        FullSyncStaleDataExpungerTestDataOption::WithModifiedNotes |
        FullSyncStaleDataExpungerTestDataOption::WithPreservedNoteGuids),
    createFullSyncStaleDataExpungerTestData(
        FullSyncStaleDataExpungerTestDataOptions{
            FullSyncStaleDataExpungerTestDataOption::WithUnmodifiedNotes} |
        FullSyncStaleDataExpungerTestDataOption::WithModifiedNotes |
        FullSyncStaleDataExpungerTestDataOption::WithPreservedNoteGuids |
        FullSyncStaleDataExpungerTestDataOption::WithLinkedNotebookGuid),
    createFullSyncStaleDataExpungerTestData(
        FullSyncStaleDataExpungerTestDataOptions{
            FullSyncStaleDataExpungerTestDataOption::
                WithUnmodifiedSavedSearches} |
        FullSyncStaleDataExpungerTestDataOption::WithPreservedSavedSearchGuids),
    createFullSyncStaleDataExpungerTestData(
        FullSyncStaleDataExpungerTestDataOptions{
            FullSyncStaleDataExpungerTestDataOption::
                WithModifiedSavedSearches} |
        FullSyncStaleDataExpungerTestDataOption::WithPreservedSavedSearchGuids),
    createFullSyncStaleDataExpungerTestData(
        FullSyncStaleDataExpungerTestDataOptions{
            FullSyncStaleDataExpungerTestDataOption::
                WithUnmodifiedSavedSearches} |
        FullSyncStaleDataExpungerTestDataOption::WithModifiedSavedSearches |
        FullSyncStaleDataExpungerTestDataOption::WithPreservedSavedSearchGuids),
    createFullSyncStaleDataExpungerTestData(
        FullSyncStaleDataExpungerTestDataOptions{
            FullSyncStaleDataExpungerTestDataOption::WithUnmodifiedNotebooks} |
        FullSyncStaleDataExpungerTestDataOption::WithModifiedNotebooks |
        FullSyncStaleDataExpungerTestDataOption::WithPreservedNotebookGuids |
        FullSyncStaleDataExpungerTestDataOption::WithUnmodifiedNotes |
        FullSyncStaleDataExpungerTestDataOption::WithModifiedNotes |
        FullSyncStaleDataExpungerTestDataOption::WithPreservedNoteGuids |
        FullSyncStaleDataExpungerTestDataOption::WithUnmodifiedTags |
        FullSyncStaleDataExpungerTestDataOption::WithModifiedTags |
        FullSyncStaleDataExpungerTestDataOption::WithPreservedTagGuids |
        FullSyncStaleDataExpungerTestDataOption::WithUnmodifiedSavedSearches |
        FullSyncStaleDataExpungerTestDataOption::WithModifiedSavedSearches |
        FullSyncStaleDataExpungerTestDataOption::WithPreservedSavedSearchGuids),
    createFullSyncStaleDataExpungerTestData(
        FullSyncStaleDataExpungerTestDataOptions{
            FullSyncStaleDataExpungerTestDataOption::WithUnmodifiedNotebooks} |
        FullSyncStaleDataExpungerTestDataOption::WithModifiedNotebooks |
        FullSyncStaleDataExpungerTestDataOption::WithPreservedNotebookGuids |
        FullSyncStaleDataExpungerTestDataOption::WithUnmodifiedNotes |
        FullSyncStaleDataExpungerTestDataOption::WithModifiedNotes |
        FullSyncStaleDataExpungerTestDataOption::WithPreservedNoteGuids |
        FullSyncStaleDataExpungerTestDataOption::WithUnmodifiedTags |
        FullSyncStaleDataExpungerTestDataOption::WithModifiedTags |
        FullSyncStaleDataExpungerTestDataOption::WithPreservedTagGuids |
        FullSyncStaleDataExpungerTestDataOption::WithLinkedNotebookGuid),
};

INSTANTIATE_TEST_SUITE_P(
    FullSyncStaleDataExpungerDataTestInstance,
    FullSyncStaleDataExpungerDataTest,
    testing::ValuesIn(gFullSyncStaleDataExpungerTestData));

TEST_P(FullSyncStaleDataExpungerDataTest, ProcessData)
{
    const auto fullSyncStaleDataExpunger =
        std::make_shared<FullSyncStaleDataExpunger>(m_mockLocalStorage);

    const auto & testData = GetParam();

    // === List expectations ===

    const local_storage::ILocalStorage::ListGuidsFilters modifiedFilters{
        local_storage::ILocalStorage::ListObjectsFilter::Include, // modified
        std::nullopt,                                             // favorited
    };

    const local_storage::ILocalStorage::ListGuidsFilters unmodifiedFilters{
        local_storage::ILocalStorage::ListObjectsFilter::Exclude, // modified
        std::nullopt,                                             // favorited
    };

    EXPECT_CALL(
        *m_mockLocalStorage,
        listNotebookGuids(modifiedFilters, testData.m_linkedNotebookGuid))
        .WillOnce([&]([[maybe_unused]] const local_storage::ILocalStorage::
                          ListGuidsFilters & filters,
                      [[maybe_unused]] const std::optional<qevercloud::Guid> &
                          linkedNotebookGuid) {
            QSet<qevercloud::Guid> modifiedNotebookGuids;
            modifiedNotebookGuids.reserve(testData.m_modifiedNotebooks.size());
            for (const auto it: qevercloud::toRange(
                     std::as_const(testData.m_modifiedNotebooks)))
            {
                modifiedNotebookGuids.insert(it.key());
            }
            return threading::makeReadyFuture<QSet<qevercloud::Guid>>(
                std::move(modifiedNotebookGuids));
        });

    EXPECT_CALL(
        *m_mockLocalStorage,
        listNotebookGuids(unmodifiedFilters, testData.m_linkedNotebookGuid))
        .WillOnce([&]([[maybe_unused]] const local_storage::ILocalStorage::
                          ListGuidsFilters & filters,
                      [[maybe_unused]] const std::optional<qevercloud::Guid> &
                          linkedNotebookGuid) {
            QSet<qevercloud::Guid> unmodifiedNotebookGuids;
            unmodifiedNotebookGuids.reserve(
                testData.m_unmodifiedNotebooks.size());
            for (const auto it: qevercloud::toRange(
                     std::as_const(testData.m_unmodifiedNotebooks)))
            {
                unmodifiedNotebookGuids.insert(it.key());
            }
            return threading::makeReadyFuture<QSet<qevercloud::Guid>>(
                std::move(unmodifiedNotebookGuids));
        });

    EXPECT_CALL(
        *m_mockLocalStorage,
        listTagGuids(modifiedFilters, testData.m_linkedNotebookGuid))
        .WillOnce([&]([[maybe_unused]] const local_storage::ILocalStorage::
                          ListGuidsFilters & filters,
                      [[maybe_unused]] const std::optional<qevercloud::Guid> &
                          linkedNotebookGuid) {
            QSet<qevercloud::Guid> modifiedTagGuids;
            modifiedTagGuids.reserve(testData.m_modifiedTags.size());
            for (const auto it:
                 qevercloud::toRange(std::as_const(testData.m_modifiedTags))) {
                modifiedTagGuids.insert(it.key());
            }
            return threading::makeReadyFuture<QSet<qevercloud::Guid>>(
                std::move(modifiedTagGuids));
        });

    EXPECT_CALL(
        *m_mockLocalStorage,
        listTagGuids(unmodifiedFilters, testData.m_linkedNotebookGuid))
        .WillOnce([&]([[maybe_unused]] const local_storage::ILocalStorage::
                          ListGuidsFilters & filters,
                      [[maybe_unused]] const std::optional<qevercloud::Guid> &
                          linkedNotebookGuid) {
            QSet<qevercloud::Guid> unmodifiedTagGuids;
            unmodifiedTagGuids.reserve(testData.m_unmodifiedTags.size());
            for (const auto it:
                 qevercloud::toRange(std::as_const(testData.m_unmodifiedTags)))
            {
                unmodifiedTagGuids.insert(it.key());
            }
            return threading::makeReadyFuture<QSet<qevercloud::Guid>>(
                std::move(unmodifiedTagGuids));
        });

    EXPECT_CALL(
        *m_mockLocalStorage,
        listNoteGuids(modifiedFilters, testData.m_linkedNotebookGuid))
        .WillOnce([&]([[maybe_unused]] const local_storage::ILocalStorage::
                          ListGuidsFilters & filters,
                      [[maybe_unused]] const std::optional<qevercloud::Guid> &
                          linkedNotebookGuid) {
            QSet<qevercloud::Guid> modifiedNoteGuids;
            modifiedNoteGuids.reserve(testData.m_modifiedNotes.size());
            for (const auto it:
                 qevercloud::toRange(std::as_const(testData.m_modifiedNotes))) {
                modifiedNoteGuids.insert(it.key());
            }
            return threading::makeReadyFuture<QSet<qevercloud::Guid>>(
                std::move(modifiedNoteGuids));
        });

    EXPECT_CALL(
        *m_mockLocalStorage,
        listNoteGuids(unmodifiedFilters, testData.m_linkedNotebookGuid))
        .WillOnce([&]([[maybe_unused]] const local_storage::ILocalStorage::
                          ListGuidsFilters & filters,
                      [[maybe_unused]] const std::optional<qevercloud::Guid> &
                          linkedNotebookGuid) {
            QSet<qevercloud::Guid> unmodifiedNoteGuids;
            unmodifiedNoteGuids.reserve(testData.m_unmodifiedNotes.size());
            for (const auto it:
                 qevercloud::toRange(std::as_const(testData.m_unmodifiedNotes)))
            {
                unmodifiedNoteGuids.insert(it.key());
            }
            return threading::makeReadyFuture<QSet<qevercloud::Guid>>(
                std::move(unmodifiedNoteGuids));
        });

    if (!testData.m_linkedNotebookGuid) {
        EXPECT_CALL(*m_mockLocalStorage, listSavedSearchGuids(modifiedFilters))
            .WillOnce([&]([[maybe_unused]] const local_storage::ILocalStorage::
                              ListGuidsFilters & filters) {
                QSet<qevercloud::Guid> modifiedSavedSearchGuids;
                modifiedSavedSearchGuids.reserve(
                    testData.m_modifiedSavedSearches.size());
                for (const auto it: qevercloud::toRange(
                         std::as_const(testData.m_modifiedSavedSearches)))
                {
                    modifiedSavedSearchGuids.insert(it.key());
                }
                return threading::makeReadyFuture<QSet<qevercloud::Guid>>(
                    std::move(modifiedSavedSearchGuids));
            });

        EXPECT_CALL(
            *m_mockLocalStorage, listSavedSearchGuids(unmodifiedFilters))
            .WillOnce([&]([[maybe_unused]] const local_storage::ILocalStorage::
                              ListGuidsFilters & filters) {
                QSet<qevercloud::Guid> unmodifiedSavedSearchGuids;
                unmodifiedSavedSearchGuids.reserve(
                    testData.m_unmodifiedSavedSearches.size());
                for (const auto it: qevercloud::toRange(
                         std::as_const(testData.m_unmodifiedSavedSearches)))
                {
                    unmodifiedSavedSearchGuids.insert(it.key());
                }
                return threading::makeReadyFuture<QSet<qevercloud::Guid>>(
                    std::move(unmodifiedSavedSearchGuids));
            });
    }

    // === Expunge expectations ===

    QSet<qevercloud::Guid> expungedNotebookGuids;
    EXPECT_CALL(*m_mockLocalStorage, expungeNotebookByGuid)
        .WillRepeatedly([&](const qevercloud::Guid & notebookGuid) {
            expungedNotebookGuids.insert(notebookGuid);
            return threading::makeReadyFuture();
        });

    QSet<qevercloud::Guid> expungedTagGuids;
    EXPECT_CALL(*m_mockLocalStorage, expungeTagByGuid)
        .WillRepeatedly([&](const qevercloud::Guid & tagGuid) {
            expungedTagGuids.insert(tagGuid);
            return threading::makeReadyFuture();
        });

    QSet<qevercloud::Guid> expungedNoteGuids;
    EXPECT_CALL(*m_mockLocalStorage, expungeNoteByGuid)
        .WillRepeatedly([&](const qevercloud::Guid & noteGuid) {
            expungedNoteGuids.insert(noteGuid);
            return threading::makeReadyFuture();
        });

    QSet<qevercloud::Guid> expungedSavedSearchGuids;
    EXPECT_CALL(*m_mockLocalStorage, expungeSavedSearchByGuid)
        .WillRepeatedly([&](const qevercloud::Guid & savedSearchGuid) {
            expungedSavedSearchGuids.insert(savedSearchGuid);
            return threading::makeReadyFuture();
        });

    // === Find expectations ===

    EXPECT_CALL(*m_mockLocalStorage, findNotebookByGuid)
        .WillRepeatedly([&](const qevercloud::Guid & guid) {
            const auto it = testData.m_modifiedNotebooks.constFind(guid);
            if (it == testData.m_modifiedNotebooks.constEnd()) {
                return threading::makeReadyFuture<
                    std::optional<qevercloud::Notebook>>(std::nullopt);
            }
            return threading::makeReadyFuture<
                std::optional<qevercloud::Notebook>>(it.value());
        });

    EXPECT_CALL(*m_mockLocalStorage, findTagByGuid)
        .WillRepeatedly([&](const qevercloud::Guid & guid) {
            const auto it = testData.m_modifiedTags.constFind(guid);
            if (it == testData.m_modifiedTags.constEnd()) {
                return threading::makeReadyFuture<
                    std::optional<qevercloud::Tag>>(std::nullopt);
            }
            return threading::makeReadyFuture<std::optional<qevercloud::Tag>>(
                it.value());
        });

    EXPECT_CALL(*m_mockLocalStorage, findNoteByGuid)
        .WillRepeatedly([&](const qevercloud::Guid & guid,
                            const local_storage::ILocalStorage::FetchNoteOptions
                                fetchNoteOptions) {
            EXPECT_EQ(
                fetchNoteOptions,
                local_storage::ILocalStorage::FetchNoteOptions{} |
                    local_storage::ILocalStorage::FetchNoteOption::
                        WithResourceMetadata |
                    local_storage::ILocalStorage::FetchNoteOption::
                        WithResourceBinaryData);
            const auto it = testData.m_modifiedNotes.constFind(guid);
            if (it == testData.m_modifiedNotes.constEnd()) {
                return threading::makeReadyFuture<
                    std::optional<qevercloud::Note>>(std::nullopt);
            }
            return threading::makeReadyFuture<std::optional<qevercloud::Note>>(
                it.value());
        });

    EXPECT_CALL(*m_mockLocalStorage, findSavedSearchByGuid)
        .WillRepeatedly([&](const qevercloud::Guid & guid) {
            const auto it = testData.m_modifiedSavedSearches.constFind(guid);
            if (it == testData.m_modifiedSavedSearches.constEnd()) {
                return threading::makeReadyFuture<
                    std::optional<qevercloud::SavedSearch>>(std::nullopt);
            }
            return threading::makeReadyFuture<
                std::optional<qevercloud::SavedSearch>>(it.value());
        });

    // === Put expectations ===

    QHash<QString, qevercloud::Notebook> putNotebooksByName;
    EXPECT_CALL(*m_mockLocalStorage, putNotebook)
        .WillRepeatedly([&](qevercloud::Notebook notebook) {
            Q_ASSERT(notebook.name());
            const auto notebookName = *notebook.name();
            putNotebooksByName[notebookName] = std::move(notebook);
            return threading::makeReadyFuture();
        });

    QHash<QString, qevercloud::Note> putNotesByTitle;
    EXPECT_CALL(*m_mockLocalStorage, putNote)
        .WillRepeatedly([&](qevercloud::Note note) {
            Q_ASSERT(note.title());
            const auto noteTitle = *note.title();
            putNotesByTitle[noteTitle] = std::move(note);
            return threading::makeReadyFuture();
        });

    QHash<QString, qevercloud::Tag> putTagsByName;
    EXPECT_CALL(*m_mockLocalStorage, putTag)
        .WillRepeatedly([&](qevercloud::Tag tag) {
            Q_ASSERT(tag.name());
            const auto tagName = *tag.name();
            putTagsByName[tagName] = std::move(tag);
            return threading::makeReadyFuture();
        });

    QHash<QString, qevercloud::SavedSearch> putSavedSearchesByName;
    EXPECT_CALL(*m_mockLocalStorage, putSavedSearch)
        .WillRepeatedly([&](qevercloud::SavedSearch savedSearch) {
            Q_ASSERT(savedSearch.name());
            const auto savedSearchName = *savedSearch.name();
            putSavedSearchesByName[savedSearchName] = std::move(savedSearch);
            return threading::makeReadyFuture();
        });

    // === Expunge stale data and check results ===

    auto future = fullSyncStaleDataExpunger->expungeStaleData(
        IFullSyncStaleDataExpunger::PreservedGuids{
            testData.m_preservedNotebookGuids, testData.m_preservedTagGuids,
            testData.m_preservedNoteGuids,
            testData.m_preservedSavedSearchGuids},
        m_manualCanceler, testData.m_linkedNotebookGuid);
    waitForFuture(future);
    EXPECT_NO_THROW(future.waitForFinished());

    // === Check expunge expectations ===

    const QSet<qevercloud::Guid> expectedExpungedNotebookGuids = [&] {
        QSet<qevercloud::Guid> guids;

        for (const auto it:
             qevercloud::toRange(std::as_const(testData.m_unmodifiedNotebooks)))
        {
            const auto & guid = it.key();
            if (!testData.m_preservedNotebookGuids.contains(guid)) {
                guids.insert(guid);
            }
        }

        for (const auto it:
             qevercloud::toRange(std::as_const(testData.m_modifiedNotebooks))) {
            const auto & guid = it.key();
            if (!testData.m_preservedNotebookGuids.contains(guid)) {
                guids.insert(guid);
            }
        }

        return guids;
    }();

    EXPECT_EQ(expungedNotebookGuids, expectedExpungedNotebookGuids);

    const QSet<qevercloud::Guid> expectedExpungedNoteGuids = [&] {
        QSet<qevercloud::Guid> guids;

        for (const auto it:
             qevercloud::toRange(std::as_const(testData.m_unmodifiedNotes))) {
            const auto & guid = it.key();
            if (!testData.m_preservedNoteGuids.contains(guid)) {
                guids.insert(guid);
            }
        }

        for (const auto it:
             qevercloud::toRange(std::as_const(testData.m_modifiedNotes))) {
            const auto & guid = it.key();
            if (!testData.m_preservedNoteGuids.contains(guid)) {
                guids.insert(guid);
            }
        }

        return guids;
    }();

    EXPECT_EQ(expungedNoteGuids, expectedExpungedNoteGuids);

    const QSet<qevercloud::Guid> expectedExpungedTagGuids = [&] {
        QSet<qevercloud::Guid> guids;

        for (const auto it:
             qevercloud::toRange(std::as_const(testData.m_unmodifiedTags))) {
            const auto & guid = it.key();
            if (!testData.m_preservedTagGuids.contains(guid)) {
                guids.insert(guid);
            }
        }

        for (const auto it:
             qevercloud::toRange(std::as_const(testData.m_modifiedTags))) {
            const auto & guid = it.key();
            if (!testData.m_preservedTagGuids.contains(guid)) {
                guids.insert(guid);
            }
        }

        return guids;
    }();

    EXPECT_EQ(expungedTagGuids, expectedExpungedTagGuids);

    const QSet<qevercloud::Guid> expectedExpungedSavedSearchGuids = [&] {
        QSet<qevercloud::Guid> guids;

        for (const auto it: qevercloud::toRange(
                 std::as_const(testData.m_unmodifiedSavedSearches)))
        {
            const auto & guid = it.key();
            if (!testData.m_preservedSavedSearchGuids.contains(guid)) {
                guids.insert(guid);
            }
        }

        for (const auto it: qevercloud::toRange(
                 std::as_const(testData.m_modifiedSavedSearches)))
        {
            const auto & guid = it.key();
            if (!testData.m_preservedSavedSearchGuids.contains(guid)) {
                guids.insert(guid);
            }
        }

        return guids;
    }();

    EXPECT_EQ(expungedSavedSearchGuids, expectedExpungedSavedSearchGuids);

    // === Check put expectations ===

    for (const auto it:
         qevercloud::toRange(std::as_const(testData.m_modifiedNotebooks))) {
        auto originalNotebook = it.value();
        ASSERT_TRUE(originalNotebook.name());

        const auto pit = putNotebooksByName.constFind(*originalNotebook.name());
        if (testData.m_preservedNotebookGuids.contains(
                originalNotebook.guid().value())) {
            EXPECT_EQ(pit, putNotebooksByName.constEnd());
            continue;
        }

        ASSERT_NE(pit, putNotebooksByName.constEnd());

        const auto & putNotebook = pit.value();
        EXPECT_NE(putNotebook.localId(), originalNotebook.localId());

        originalNotebook.setLocalId(putNotebook.localId());
        originalNotebook.setGuid(std::nullopt);
        originalNotebook.setUpdateSequenceNum(std::nullopt);
        originalNotebook.setRestrictions(std::nullopt);
        originalNotebook.setContact(std::nullopt);
        originalNotebook.setPublished(std::nullopt);
        originalNotebook.setPublishing(std::nullopt);
        originalNotebook.setDefaultNotebook(std::nullopt);
        originalNotebook.setLocallyModified(true);

        EXPECT_EQ(putNotebook, originalNotebook);
    }

    for (const auto it:
         qevercloud::toRange(std::as_const(testData.m_modifiedNotes)))
    {
        auto originalNote = it.value();
        ASSERT_TRUE(originalNote.title());

        const auto pit = putNotesByTitle.constFind(*originalNote.title());
        if (testData.m_preservedNoteGuids.contains(originalNote.guid().value()))
        {
            EXPECT_EQ(pit, putNotesByTitle.constEnd());
            continue;
        }

        ASSERT_NE(pit, putNotesByTitle.constEnd());

        const auto & putNote = pit.value();
        EXPECT_NE(putNote.localId(), originalNote.localId());

        const auto originalNotebookGuid = originalNote.notebookGuid().value();
        const auto originalTagGuids = originalNote.tagGuids().value();

        originalNote.setLocalId(putNote.localId());
        originalNote.setGuid(std::nullopt);
        originalNote.setUpdateSequenceNum(std::nullopt);
        originalNote.setNotebookGuid(std::nullopt);
        originalNote.setNotebookLocalId(putNote.notebookLocalId());
        originalNote.setLocallyModified(true);

        if (originalNote.resources()) {
            ASSERT_TRUE(putNote.resources());
            ASSERT_EQ(
                putNote.resources()->size(), originalNote.resources()->size());

            for (int i = 0, size = putNote.resources()->size(); i < size; ++i) {
                const auto & src = (*putNote.resources())[i];
                auto & dst = (*originalNote.mutableResources())[i];
                dst.setLocalId(src.localId());
                dst.setNoteLocalId(src.noteLocalId());
                dst.setNoteGuid(std::nullopt);
                dst.setGuid(std::nullopt);
                dst.setUpdateSequenceNum(std::nullopt);
                dst.setLocallyModified(true);
            }
        }

        originalNote.setTagGuids(std::nullopt);
        originalNote.setTagLocalIds(putNote.tagLocalIds());

        EXPECT_EQ(putNote, originalNote);

        if (const auto nit =
                testData.m_modifiedNotebooks.constFind(originalNotebookGuid);
            nit != testData.m_modifiedNotebooks.constEnd())
        {
            const auto & originalNotebook = nit.value();
            ASSERT_TRUE(originalNotebook.name());
            const auto pit =
                putNotebooksByName.constFind(*originalNotebook.name());
            ASSERT_NE(pit, putNotebooksByName.constEnd());
            EXPECT_EQ(putNote.notebookLocalId(), pit.value().localId());
        }

        for (const auto & tagGuid: std::as_const(originalTagGuids)) {
            if (const auto nit = testData.m_modifiedTags.constFind(tagGuid);
                nit != testData.m_modifiedTags.constEnd())
            {
                const auto & originalTag = nit.value();
                ASSERT_TRUE(originalTag.name());
                const auto pit = putTagsByName.constFind(*originalTag.name());
                ASSERT_NE(pit, putTagsByName.constEnd());
                EXPECT_TRUE(
                    putNote.tagLocalIds().contains(pit.value().localId()));
            }
        }
    }

    for (const auto it:
         qevercloud::toRange(std::as_const(testData.m_modifiedTags)))
    {
        auto originalTag = it.value();
        ASSERT_TRUE(originalTag.name());

        const auto pit = putTagsByName.constFind(*originalTag.name());
        if (testData.m_preservedTagGuids.contains(originalTag.guid().value())) {
            EXPECT_EQ(pit, putTagsByName.constEnd());
            continue;
        }

        ASSERT_NE(pit, putTagsByName.constEnd());

        const auto & putTag = pit.value();
        EXPECT_NE(putTag.localId(), originalTag.localId());

        originalTag.setLocalId(putTag.localId());
        originalTag.setGuid(std::nullopt);
        originalTag.setUpdateSequenceNum(std::nullopt);
        originalTag.setParentGuid(std::nullopt);
        originalTag.setParentTagLocalId(QString{});
        originalTag.setLocallyModified(true);

        EXPECT_EQ(putTag, originalTag);
    }

    for (const auto it:
         qevercloud::toRange(std::as_const(testData.m_modifiedSavedSearches)))
    {
        auto originalSavedSearch = it.value();
        ASSERT_TRUE(originalSavedSearch.name());

        const auto pit =
            putSavedSearchesByName.constFind(*originalSavedSearch.name());
        if (testData.m_preservedSavedSearchGuids.contains(
                originalSavedSearch.guid().value()))
        {
            EXPECT_EQ(pit, putSavedSearchesByName.constEnd());
            continue;
        }

        ASSERT_NE(pit, putSavedSearchesByName.constEnd());

        const auto & putSavedSearch = pit.value();
        EXPECT_NE(putSavedSearch.localId(), originalSavedSearch.localId());

        originalSavedSearch.setLocalId(putSavedSearch.localId());
        originalSavedSearch.setGuid(std::nullopt);
        originalSavedSearch.setUpdateSequenceNum(std::nullopt);
        originalSavedSearch.setLocallyModified(true);

        EXPECT_EQ(putSavedSearch, originalSavedSearch);
    }
}

} // namespace quentier::synchronization::tests
