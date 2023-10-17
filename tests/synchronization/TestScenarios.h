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

#pragma once

#include "TestScenarioData.h"

#include <array>
#include <string_view>

namespace quentier::synchronization::tests {

using namespace std::string_view_literals;

static const std::array gTestScenarioData{
    TestScenarioData{
        DataItemTypes{} | DataItemType::SavedSearch, // serverDataItemTypes
        ItemGroups{} | ItemGroup::Base,              // serverItemGroups
        ItemSources{} | ItemSource::UserOwnAccount,  // serverItemSources
        DataItemTypes{}, // serverExpungedDataItemTypes
        ItemSources{},   // serverExpungedDataItemSources
        DataItemTypes{}, // localDataItemTypes
        ItemGroups{},    // localItemGroups
        ItemSources{},   // localItemSources
        StopSynchronizationError{std::monostate{}}, // stopSyncError
        false,                                      // expectFailure
        true,  // expectSomeUserOwnSyncChunks
        false, // expectSomeLinkedNotebooksSyncChunks
        false, // expectSomeUserOwnNotes
        false, // expectSomeUserOwnResources
        false, // expectSomeLinkedNotebookNotes
        false, // expectSomeLinkedNotebookResources
        false, // expectSomeUserOwnDataSent
        false, // expectSomeLinkedNotebookDataSent
        "Full sync with only saved searches"sv, // name
    },
    TestScenarioData{
        DataItemTypes{} | DataItemType::Notebook,   // serverDataItemTypes
        ItemGroups{} | ItemGroup::Base,             // serverItemGroups
        ItemSources{} | ItemSource::UserOwnAccount, // serverItemSources
        DataItemTypes{}, // serverExpungedDataItemTypes
        ItemSources{},   // serverExpungedDataItemSources
        DataItemTypes{}, // localDataItemTypes
        ItemGroups{},    // localItemGroups
        ItemSources{},   // localItemSources
        StopSynchronizationError{std::monostate{}}, // stopSyncError
        false,                                      // expectFailure
        true,  // expectSomeUserOwnSyncChunks
        false, // expectSomeLinkedNotebooksSyncChunks
        false, // expectSomeUserOwnNotes
        false, // expectSomeUserOwnResources
        false, // expectSomeLinkedNotebookNotes
        false, // expectSomeLinkedNotebookResources
        false, // expectSomeUserOwnDataSent
        false, // expectSomeLinkedNotebookDataSent
        "Full sync with only user own notebooks"sv, // name
    },
    TestScenarioData{
        DataItemTypes{} | DataItemType::Tag,        // serverDataItemTypes
        ItemGroups{} | ItemGroup::Base,             // serverItemGroups
        ItemSources{} | ItemSource::UserOwnAccount, // serverItemSources
        DataItemTypes{}, // serverExpungedDataItemTypes
        ItemSources{},   // serverExpungedDataItemSources
        DataItemTypes{}, // localDataItemTypes
        ItemGroups{},    // localItemGroups
        ItemSources{},   // localItemSources
        StopSynchronizationError{std::monostate{}}, // stopSyncError
        false,                                      // expectFailure
        true,  // expectSomeUserOwnSyncChunks
        false, // expectSomeLinkedNotebooksSyncChunks
        false, // expectSomeUserOwnNotes
        false, // expectSomeUserOwnResources
        false, // expectSomeLinkedNotebookNotes
        false, // expectSomeLinkedNotebookResources
        false, // expectSomeUserOwnDataSent
        false, // expectSomeLinkedNotebookDataSent
        "Full sync with only user own tags"sv, // name
    },
    TestScenarioData{
        DataItemTypes{} | DataItemType::Notebook |
            DataItemType::Note,                     // serverDataItemTypes
        ItemGroups{} | ItemGroup::Base,             // serverItemGroups
        ItemSources{} | ItemSource::UserOwnAccount, // serverItemSources
        DataItemTypes{}, // serverExpungedDataItemTypes
        ItemSources{},   // serverExpungedDataItemSources
        DataItemTypes{}, // localDataItemTypes
        ItemGroups{},    // localItemGroups
        ItemSources{},   // localItemSources
        StopSynchronizationError{std::monostate{}}, // stopSyncError
        false,                                      // expectFailure
        true,  // expectSomeUserOwnSyncChunks
        false, // expectSomeLinkedNotebooksSyncChunks
        true,  // expectSomeUserOwnNotes
        false, // expectSomeUserOwnResources
        false, // expectSomeLinkedNotebookNotes
        false, // expectSomeLinkedNotebookResources
        false, // expectSomeUserOwnDataSent
        false, // expectSomeLinkedNotebookDataSent
        "Full sync with user own notebooks and notes"sv, // name
    },
    TestScenarioData{
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch |
            DataItemType::Tag,                      // serverDataItemTypes
        ItemGroups{} | ItemGroup::Base,             // serverItemGroups
        ItemSources{} | ItemSource::UserOwnAccount, // serverItemSources
        DataItemTypes{}, // serverExpungedDataItemTypes
        ItemSources{},   // serverExpungedDataItemSources
        DataItemTypes{}, // localDataItemTypes
        ItemGroups{},    // localItemGroups
        ItemSources{},   // localItemSources
        StopSynchronizationError{std::monostate{}}, // stopSyncError
        false,                                      // expectFailure
        true,  // expectSomeUserOwnSyncChunks
        false, // expectSomeLinkedNotebooksSyncChunks
        true,  // expectSomeUserOwnNotes
        false, // expectSomeUserOwnResources
        false, // expectSomeLinkedNotebookNotes
        false, // expectSomeLinkedNotebookResources
        false, // expectSomeUserOwnDataSent
        false, // expectSomeLinkedNotebookDataSent
        "Full sync with user own saved searches, notebooks, tags and notes"sv, // name
    },
    TestScenarioData{
        DataItemTypes{} | DataItemType::Notebook,   // serverDataItemTypes
        ItemGroups{} | ItemGroup::Base,             // serverItemGroups
        ItemSources{} | ItemSource::LinkedNotebook, // serverItemSources
        DataItemTypes{}, // serverExpungedDataItemTypes
        ItemSources{},   // serverExpungedDataItemSources
        DataItemTypes{}, // localDataItemTypes
        ItemGroups{},    // localItemGroups
        ItemSources{},   // localItemSources
        StopSynchronizationError{std::monostate{}}, // stopSyncError
        false,                                      // expectFailure
        true,  // expectSomeUserOwnSyncChunks
        true,  // expectSomeLinkedNotebooksSyncChunks
        false, // expectSomeUserOwnNotes
        false, // expectSomeUserOwnResources
        false, // expectSomeLinkedNotebookNotes
        false, // expectSomeLinkedNotebookResources
        false, // expectSomeUserOwnDataSent
        false, // expectSomeLinkedNotebookDataSent
        "Full sync with linked notebooks' notebooks"sv, // name
    },
    TestScenarioData{
        DataItemTypes{} | DataItemType::Tag,        // serverDataItemTypes
        ItemGroups{} | ItemGroup::Base,             // serverItemGroups
        ItemSources{} | ItemSource::LinkedNotebook, // serverItemSources
        DataItemTypes{}, // serverExpungedDataItemTypes
        ItemSources{},   // serverExpungedDataItemSources
        DataItemTypes{}, // localDataItemTypes
        ItemGroups{},    // localItemGroups
        ItemSources{},   // localItemSources
        StopSynchronizationError{std::monostate{}}, // stopSyncError
        false,                                      // expectFailure
        true,  // expectSomeUserOwnSyncChunks
        true,  // expectSomeLinkedNotebooksSyncChunks
        false, // expectSomeUserOwnNotes
        false, // expectSomeUserOwnResources
        false, // expectSomeLinkedNotebookNotes
        false, // expectSomeLinkedNotebookResources
        false, // expectSomeUserOwnDataSent
        false, // expectSomeLinkedNotebookDataSent
        "Full sync with linked notebooks' tags"sv, // name
    },
    TestScenarioData{
        DataItemTypes{} | DataItemType::Notebook |
            DataItemType::Note,                     // serverDataItemTypes
        ItemGroups{} | ItemGroup::Base,             // serverItemGroups
        ItemSources{} | ItemSource::LinkedNotebook, // serverItemSources
        DataItemTypes{}, // serverExpungedDataItemTypes
        ItemSources{},   // serverExpungedDataItemSources
        DataItemTypes{}, // localDataItemTypes
        ItemGroups{},    // localItemGroups
        ItemSources{},   // localItemSources
        StopSynchronizationError{std::monostate{}}, // stopSyncError
        false,                                      // expectFailure
        true,  // expectSomeUserOwnSyncChunks
        true,  // expectSomeLinkedNotebooksSyncChunks
        false, // expectSomeUserOwnNotes
        false, // expectSomeUserOwnResources
        true,  // expectSomeLinkedNotebookNotes
        false, // expectSomeLinkedNotebookResources
        false, // expectSomeUserOwnDataSent
        false, // expectSomeLinkedNotebookDataSent
        "Full sync with linked notebooks' notebooks and notes"sv, // name
    },
    TestScenarioData{
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch |
            DataItemType::Tag,          // serverDataItemTypes
        ItemGroups{} | ItemGroup::Base, // serverItemGroups
        ItemSources{} | ItemSource::UserOwnAccount |
            ItemSource::LinkedNotebook, // serverItemSources
        DataItemTypes{},                // serverExpungedDataItemTypes
        ItemSources{},                  // serverExpungedDataItemSources
        DataItemTypes{},                // localDataItemTypes
        ItemGroups{},                   // localItemGroups
        ItemSources{},                  // localItemSources
        StopSynchronizationError{std::monostate{}}, // stopSyncError
        false,                                      // expectFailure
        true,  // expectSomeUserOwnSyncChunks
        true,  // expectSomeLinkedNotebooksSyncChunks
        true,  // expectSomeUserOwnNotes
        false, // expectSomeUserOwnResources
        true,  // expectSomeLinkedNotebookNotes
        false, // expectSomeLinkedNotebookResources
        false, // expectSomeUserOwnDataSent
        false, // expectSomeLinkedNotebookDataSent
        "Full sync with user own linked notebooks' data"sv, // name
    },
    TestScenarioData{
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch |
            DataItemType::Tag,          // serverDataItemTypes
        ItemGroups{} | ItemGroup::Base, // serverItemGroups
        ItemSources{} | ItemSource::UserOwnAccount |
            ItemSource::LinkedNotebook, // serverItemSources
        DataItemTypes{},                // serverExpungedDataItemTypes
        ItemSources{},                  // serverExpungedDataItemSources
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch | DataItemType::Tag, // localDataItemTypes
        ItemGroups{} | ItemGroup::Base,                    // localItemGroups
        ItemSources{} | ItemSource::UserOwnAccount |
            ItemSource::LinkedNotebook,             // localItemSources
        StopSynchronizationError{std::monostate{}}, // stopSyncError
        false,                                      // expectFailure
        false, // expectSomeUserOwnSyncChunks
        false, // expectSomeLinkedNotebooksSyncChunks
        false, // expectSomeUserOwnNotes
        false, // expectSomeUserOwnResources
        false, // expectSomeLinkedNotebookNotes
        false, // expectSomeLinkedNotebookResources
        false, // expectSomeUserOwnDataSent
        false, // expectSomeLinkedNotebookDataSent
        "Incremental sync with nothing to sync' data"sv, // name
    },
};

} // namespace quentier::synchronization::tests
