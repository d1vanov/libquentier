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

#include <synchronization/FullSyncStaleDataExpunger.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/local_storage/tests/mocks/MockILocalStorage.h>
#include <quentier/threading/Future.h>
#include <quentier/utility/cancelers/ManualCanceler.h>
#include <quentier/utility/UidGenerator.h>

#include <qevercloud/types/builders/NoteBuilder.h>
#include <qevercloud/types/builders/NotebookBuilder.h>
#include <qevercloud/types/builders/SavedSearchBuilder.h>
#include <qevercloud/types/builders/TagBuilder.h>
#include <qevercloud/types/Note.h>
#include <qevercloud/types/Notebook.h>
#include <qevercloud/types/SavedSearch.h>
#include <qevercloud/types/Tag.h>
#include <qevercloud/utility/ToRange.h>

#include <QFlags>
#include <QHash>

#include <gtest/gtest.h>

#include <array>

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
            std::make_shared<FullSyncStaleDataExpunger>(
                m_mockLocalStorage, m_manualCanceler));
}

TEST_F(FullSyncStaleDataExpungerTest, CtorNullLocalStorage)
{
    EXPECT_THROW(
        const auto fullSyncStaleDataExpunger =
            std::make_shared<FullSyncStaleDataExpunger>(
                nullptr, m_manualCanceler),
        InvalidArgument);
}

TEST_F(FullSyncStaleDataExpungerTest, CtorNullCanceler)
{
    EXPECT_THROW(
        const auto fullSyncStaleDataExpunger =
            std::make_shared<FullSyncStaleDataExpunger>(
                m_mockLocalStorage, nullptr),
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
                .setName(QString::fromUtf8("Notebook #%1").arg(counter))
                .setLocallyModified(modified)
                .setLinkedNotebookGuid(std::move(linkedNotebookGuid))
                .build();
        };

    const auto createNote =
        [](const bool modified, const int counter,
           qevercloud::Guid notebookGuid, QString notebookLocalId,
           QList<qevercloud::Guid> tagGuids, QStringList tagLocalIds) {
            return qevercloud::NoteBuilder{}
                .setGuid(UidGenerator::Generate())
                .setLocalId(UidGenerator::Generate())
                .setUpdateSequenceNum(counter)
                .setTitle(QString::fromUtf8("Note #%1").arg(counter))
                .setLocallyModified(modified)
                .setNotebookGuid(std::move(notebookGuid))
                .setNotebookLocalId(std::move(notebookLocalId))
                .setTagGuids(std::move(tagGuids))
                .setTagLocalIds(std::move(tagLocalIds))
                .build();
        };

    const auto createTag = 
        [](const bool modified, const int counter) {
            return qevercloud::TagBuilder{}
                .setGuid(UidGenerator::Generate())
                .setLocalId(UidGenerator::Generate())
                .setUpdateSequenceNum(counter)
                .setName(QString::fromUtf8("Tag #%1").arg(counter))
                .setLocallyModified(modified)
                .build();
        };

    const auto createSavedSearch =
        [](const bool modified, const int counter) {
            return qevercloud::SavedSearchBuilder{}
                .setGuid(UidGenerator::Generate())
                .setLocalId(UidGenerator::Generate())
                .setUpdateSequenceNum(counter)
                .setName(QString::fromUtf8("Saved search #%1").arg(counter))
                .setLocallyModified(modified)
                .build();
        };

    FullSyncStaleDataExpungerTestData result;
    constexpr int itemCount = 3;

    const std::optional<qevercloud::Guid> linkedNotebookGuid =
        (options.testFlag(FullSyncStaleDataExpungerTestDataOption::WithLinkedNotebookGuid)
         ? std::make_optional(UidGenerator::Generate())
         : std::optional<qevercloud::Guid>{});

    int notebookCounter = 1;
    if (options.testFlag(
            FullSyncStaleDataExpungerTestDataOption::WithUnmodifiedNotebooks)) {
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
            FullSyncStaleDataExpungerTestDataOption::WithModifiedNotebooks)) {
        for (int i = 0; i < itemCount; ++i) {
            auto notebook =
                createNotebook(true, notebookCounter, linkedNotebookGuid);
            Q_ASSERT(notebook.guid());

            const auto guid = *notebook.guid();
            result.m_modifiedNotebooks[guid] = std::move(notebook);

            ++notebookCounter;
        }
    }

    // TODO: implement further
    return result;
}

class FullSyncStaleDataExpungerDataTest :
    public FullSyncStaleDataExpungerTest,
    public testing::WithParamInterface<FullSyncStaleDataExpungerTestData>
{};

const std::array gFullSyncStaleDataExpungerTestData{
    FullSyncStaleDataExpungerTestData{},
};

INSTANTIATE_TEST_SUITE_P(
    FullSyncStaleDataExpungerDataTestInstance,
    FullSyncStaleDataExpungerDataTest,
    testing::ValuesIn(gFullSyncStaleDataExpungerTestData));

TEST_P(FullSyncStaleDataExpungerDataTest, ProcessData)
{
    const auto fullSyncStaleDataExpunger =
        std::make_shared<FullSyncStaleDataExpunger>(
            m_mockLocalStorage, m_manualCanceler);

    const auto & testData = GetParam();

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
            for (const auto it:
                 qevercloud::toRange(qAsConst(testData.m_modifiedNotebooks))) {
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
            for (const auto it:
                 qevercloud::toRange(qAsConst(testData.m_unmodifiedNotebooks)))
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
                 qevercloud::toRange(qAsConst(testData.m_modifiedTags))) {
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
                 qevercloud::toRange(qAsConst(testData.m_unmodifiedTags))) {
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
                 qevercloud::toRange(qAsConst(testData.m_modifiedNotes))) {
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
                 qevercloud::toRange(qAsConst(testData.m_unmodifiedNotes))) {
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
                         qAsConst(testData.m_modifiedSavedSearches)))
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
                         qAsConst(testData.m_unmodifiedSavedSearches)))
                {
                    unmodifiedSavedSearchGuids.insert(it.key());
                }
                return threading::makeReadyFuture<QSet<qevercloud::Guid>>(
                    std::move(unmodifiedSavedSearchGuids));
            });
    }

    QSet<qevercloud::Guid> expungedNotebookGuids;
    EXPECT_CALL(*m_mockLocalStorage, expungeNotebookByGuid).WillRepeatedly(
        [&](const qevercloud::Guid & notebookGuid) {
            expungedNotebookGuids.insert(notebookGuid);
            return threading::makeReadyFuture();
        });

    QSet<qevercloud::Guid> expungedTagGuids;
    EXPECT_CALL(*m_mockLocalStorage, expungeTagByGuid).WillRepeatedly(
        [&](const qevercloud::Guid & tagGuid) {
            expungedTagGuids.insert(tagGuid);
            return threading::makeReadyFuture();
        });

    QSet<qevercloud::Guid> expungedNoteGuids;
    EXPECT_CALL(*m_mockLocalStorage, expungeNoteByGuid).WillRepeatedly(
        [&](const qevercloud::Guid & noteGuid) {
            expungedNoteGuids.insert(noteGuid);
            return threading::makeReadyFuture();
        });

    QSet<qevercloud::Guid> expungedSavedSearchGuids;
    EXPECT_CALL(*m_mockLocalStorage, expungeSavedSearchByGuid).WillRepeatedly(
        [&](const qevercloud::Guid & savedSearchGuid) {
            expungedSavedSearchGuids.insert(savedSearchGuid);
            return threading::makeReadyFuture();
        });

    // TODO: fill in other expectations

    auto future = fullSyncStaleDataExpunger->expungeStaleData(
        IFullSyncStaleDataExpunger::PreservedGuids{
            testData.m_preservedNotebookGuids, testData.m_preservedTagGuids,
            testData.m_preservedNoteGuids,
            testData.m_preservedSavedSearchGuids});
    ASSERT_TRUE(future.isFinished());
    EXPECT_NO_THROW(future.waitForFinished());

    const QSet<qevercloud::Guid> expectedExpungedNotebookGuids = [&]{
        QSet<qevercloud::Guid> guids;

        for (const auto it:
             qevercloud::toRange(qAsConst(testData.m_unmodifiedNotebooks))) {
            const auto & guid = it.key();
            if (!testData.m_preservedNotebookGuids.contains(guid)) {
                guids.insert(guid);
            }
        }

        for (const auto it:
             qevercloud::toRange(qAsConst(testData.m_modifiedNotebooks))) {
            const auto & guid = it.key();
            if (!testData.m_preservedNotebookGuids.contains(guid)) {
                guids.insert(guid);
            }
        }

        return guids;
    }();

    EXPECT_EQ(expungedNotebookGuids, expectedExpungedNotebookGuids);

    const QSet<qevercloud::Guid> expectedExpungedNoteGuids = [&]{
        QSet<qevercloud::Guid> guids;

        for (const auto it:
             qevercloud::toRange(qAsConst(testData.m_unmodifiedNotes))) {
            const auto & guid = it.key();
            if (!testData.m_preservedNoteGuids.contains(guid)) {
                guids.insert(guid);
            }
        }

        for (const auto it:
             qevercloud::toRange(qAsConst(testData.m_modifiedNotes))) {
            const auto & guid = it.key();
            if (!testData.m_preservedNoteGuids.contains(guid)) {
                guids.insert(guid);
            }
        }

        return guids;
    }();

    EXPECT_EQ(expungedNoteGuids, expectedExpungedNoteGuids);

    const QSet<qevercloud::Guid> expectedExpungedTagGuids = [&]{
        QSet<qevercloud::Guid> guids;

        for (const auto it:
             qevercloud::toRange(qAsConst(testData.m_unmodifiedTags))) {
            const auto & guid = it.key();
            if (!testData.m_preservedTagGuids.contains(guid)) {
                guids.insert(guid);
            }
        }

        for (const auto it:
             qevercloud::toRange(qAsConst(testData.m_modifiedTags))) {
            const auto & guid = it.key();
            if (!testData.m_preservedTagGuids.contains(guid)) {
                guids.insert(guid);
            }
        }

        return guids;
    }();

    EXPECT_EQ(expungedTagGuids, expectedExpungedTagGuids);

    const QSet<qevercloud::Guid> expectedExpungedSavedSearchGuids = [&]{
        QSet<qevercloud::Guid> guids;

        for (const auto it:
             qevercloud::toRange(qAsConst(testData.m_unmodifiedSavedSearches)))
        {
            const auto & guid = it.key();
            if (!testData.m_preservedSavedSearchGuids.contains(guid)) {
                guids.insert(guid);
            }
        }

        for (const auto it:
             qevercloud::toRange(qAsConst(testData.m_modifiedSavedSearches)))
        {
            const auto & guid = it.key();
            if (!testData.m_preservedSavedSearchGuids.contains(guid)) {
                guids.insert(guid);
            }
        }

        return guids;
    }();

    EXPECT_EQ(expungedSavedSearchGuids, expectedExpungedSavedSearchGuids);
}

} // namespace quentier::synchronization::tests
