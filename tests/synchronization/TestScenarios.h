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
        "Full sync with only saved searches"sv,      // name
        DataItemTypes{} | DataItemType::SavedSearch, // serverDataItemTypes
        ItemGroups{} | ItemGroup::Base,              // serverItemGroups
        ItemSources{} | ItemSource::UserOwnAccount,  // serverItemSources
        DataItemTypes{}, // serverExpungedDataItemTypes
        ItemSources{},   // serverExpungedDataItemSources
        DataItemTypes{}, // localDataItemTypes
        ItemGroups{},    // localItemGroups
        ItemSources{},   // localItemSources
        true,            // expectSomeUserOwnSyncChunks
        false,           // expectSomeLinkedNotebooksSyncChunks
        false,           // expectSomeUserOwnNotes
        false,           // expectSomeUserOwnResources
        false,           // expectSomeLinkedNotebookNotes
        false,           // expectSomeLinkedNotebookResources
        false,           // expectSomeUserOwnDataSent
        false,           // expectSomeLinkedNotebookDataSent
        false,           // expectFailure
    },
    TestScenarioData{
        "Full sync with only user own notebooks"sv, // name
        DataItemTypes{} | DataItemType::Notebook,   // serverDataItemTypes
        ItemGroups{} | ItemGroup::Base,             // serverItemGroups
        ItemSources{} | ItemSource::UserOwnAccount, // serverItemSources
        DataItemTypes{}, // serverExpungedDataItemTypes
        ItemSources{},   // serverExpungedDataItemSources
        DataItemTypes{}, // localDataItemTypes
        ItemGroups{},    // localItemGroups
        ItemSources{},   // localItemSources
        true,            // expectSomeUserOwnSyncChunks
        false,           // expectSomeLinkedNotebooksSyncChunks
        false,           // expectSomeUserOwnNotes
        false,           // expectSomeUserOwnResources
        false,           // expectSomeLinkedNotebookNotes
        false,           // expectSomeLinkedNotebookResources
        false,           // expectSomeUserOwnDataSent
        false,           // expectSomeLinkedNotebookDataSent
        false,           // expectFailure
    },
    TestScenarioData{
        "Full sync with only user own tags"sv,      // name
        DataItemTypes{} | DataItemType::Tag,        // serverDataItemTypes
        ItemGroups{} | ItemGroup::Base,             // serverItemGroups
        ItemSources{} | ItemSource::UserOwnAccount, // serverItemSources
        DataItemTypes{}, // serverExpungedDataItemTypes
        ItemSources{},   // serverExpungedDataItemSources
        DataItemTypes{}, // localDataItemTypes
        ItemGroups{},    // localItemGroups
        ItemSources{},   // localItemSources
        true,            // expectSomeUserOwnSyncChunks
        false,           // expectSomeLinkedNotebooksSyncChunks
        false,           // expectSomeUserOwnNotes
        false,           // expectSomeUserOwnResources
        false,           // expectSomeLinkedNotebookNotes
        false,           // expectSomeLinkedNotebookResources
        false,           // expectSomeUserOwnDataSent
        false,           // expectSomeLinkedNotebookDataSent
        false,           // expectFailure
    },
    TestScenarioData{
        "Full sync with user own notebooks and notes"sv, // name
        DataItemTypes{} | DataItemType::Notebook |
            DataItemType::Note,                     // serverDataItemTypes
        ItemGroups{} | ItemGroup::Base,             // serverItemGroups
        ItemSources{} | ItemSource::UserOwnAccount, // serverItemSources
        DataItemTypes{}, // serverExpungedDataItemTypes
        ItemSources{},   // serverExpungedDataItemSources
        DataItemTypes{}, // localDataItemTypes
        ItemGroups{},    // localItemGroups
        ItemSources{},   // localItemSources
        true,            // expectSomeUserOwnSyncChunks
        false,           // expectSomeLinkedNotebooksSyncChunks
        true,            // expectSomeUserOwnNotes
        false,           // expectSomeUserOwnResources
        false,           // expectSomeLinkedNotebookNotes
        false,           // expectSomeLinkedNotebookResources
        false,           // expectSomeUserOwnDataSent
        false,           // expectSomeLinkedNotebookDataSent
        false,           // expectFailure
    },
    TestScenarioData{
        "Full sync with user own saved searches, notebooks, tags and "
        "notes"sv, // name
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
        true,            // expectSomeUserOwnSyncChunks
        false,           // expectSomeLinkedNotebooksSyncChunks
        true,            // expectSomeUserOwnNotes
        false,           // expectSomeUserOwnResources
        false,           // expectSomeLinkedNotebookNotes
        false,           // expectSomeLinkedNotebookResources
        false,           // expectSomeUserOwnDataSent
        false,           // expectSomeLinkedNotebookDataSent
        false,           // expectFailure
    },
    TestScenarioData{
        "Full sync with linked notebooks' notebooks"sv, // name
        DataItemTypes{} | DataItemType::Notebook,       // serverDataItemTypes
        ItemGroups{} | ItemGroup::Base,                 // serverItemGroups
        ItemSources{} | ItemSource::LinkedNotebook,     // serverItemSources
        DataItemTypes{}, // serverExpungedDataItemTypes
        ItemSources{},   // serverExpungedDataItemSources
        DataItemTypes{}, // localDataItemTypes
        ItemGroups{},    // localItemGroups
        ItemSources{},   // localItemSources
        true,            // expectSomeUserOwnSyncChunks
        true,            // expectSomeLinkedNotebooksSyncChunks
        false,           // expectSomeUserOwnNotes
        false,           // expectSomeUserOwnResources
        false,           // expectSomeLinkedNotebookNotes
        false,           // expectSomeLinkedNotebookResources
        false,           // expectSomeUserOwnDataSent
        false,           // expectSomeLinkedNotebookDataSent
        false,           // expectFailure
    },
    TestScenarioData{
        "Full sync with linked notebooks' tags"sv,  // name
        DataItemTypes{} | DataItemType::Tag,        // serverDataItemTypes
        ItemGroups{} | ItemGroup::Base,             // serverItemGroups
        ItemSources{} | ItemSource::LinkedNotebook, // serverItemSources
        DataItemTypes{}, // serverExpungedDataItemTypes
        ItemSources{},   // serverExpungedDataItemSources
        DataItemTypes{}, // localDataItemTypes
        ItemGroups{},    // localItemGroups
        ItemSources{},   // localItemSources
        true,            // expectSomeUserOwnSyncChunks
        true,            // expectSomeLinkedNotebooksSyncChunks
        false,           // expectSomeUserOwnNotes
        false,           // expectSomeUserOwnResources
        false,           // expectSomeLinkedNotebookNotes
        false,           // expectSomeLinkedNotebookResources
        false,           // expectSomeUserOwnDataSent
        false,           // expectSomeLinkedNotebookDataSent
        false,           // expectFailure
    },
    TestScenarioData{
        "Full sync with linked notebooks' notebooks and notes"sv, // name
        DataItemTypes{} | DataItemType::Notebook |
            DataItemType::Note,                     // serverDataItemTypes
        ItemGroups{} | ItemGroup::Base,             // serverItemGroups
        ItemSources{} | ItemSource::LinkedNotebook, // serverItemSources
        DataItemTypes{}, // serverExpungedDataItemTypes
        ItemSources{},   // serverExpungedDataItemSources
        DataItemTypes{}, // localDataItemTypes
        ItemGroups{},    // localItemGroups
        ItemSources{},   // localItemSources
        true,            // expectSomeUserOwnSyncChunks
        true,            // expectSomeLinkedNotebooksSyncChunks
        false,           // expectSomeUserOwnNotes
        false,           // expectSomeUserOwnResources
        true,            // expectSomeLinkedNotebookNotes
        false,           // expectSomeLinkedNotebookResources
        false,           // expectSomeUserOwnDataSent
        false,           // expectSomeLinkedNotebookDataSent
        false,           // expectFailure
    },
    TestScenarioData{
        "Full sync with user own and linked notebooks' data"sv, // name
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
        true,                           // expectSomeUserOwnSyncChunks
        true,                           // expectSomeLinkedNotebooksSyncChunks
        true,                           // expectSomeUserOwnNotes
        false,                          // expectSomeUserOwnResources
        true,                           // expectSomeLinkedNotebookNotes
        false,                          // expectSomeLinkedNotebookResources
        false,                          // expectSomeUserOwnDataSent
        false,                          // expectSomeLinkedNotebookDataSent
        false,                          // expectFailure
    },
    TestScenarioData{
        "Incremental sync with nothing to sync' data"sv, // name
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
            ItemSource::LinkedNotebook, // localItemSources
        false,                          // expectSomeUserOwnSyncChunks
        false,                          // expectSomeLinkedNotebooksSyncChunks
        false,                          // expectSomeUserOwnNotes
        false,                          // expectSomeUserOwnResources
        false,                          // expectSomeLinkedNotebookNotes
        false,                          // expectSomeLinkedNotebookResources
        false,                          // expectSomeUserOwnDataSent
        false,                          // expectSomeLinkedNotebookDataSent
        false,                          // expectFailure
    },
    TestScenarioData{
        "Incremental sync with new server user own data"sv, // name
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch |
            DataItemType::Tag,                           // serverDataItemTypes
        ItemGroups{} | ItemGroup::Base | ItemGroup::New, // serverItemGroups
        ItemSources{} | ItemSource::UserOwnAccount,      // serverItemSources
        DataItemTypes{}, // serverExpungedDataItemTypes
        ItemSources{},   // serverExpungedDataItemSources
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch | DataItemType::Tag, // localDataItemTypes
        ItemGroups{} | ItemGroup::Base,                    // localItemGroups
        ItemSources{} | ItemSource::UserOwnAccount,        // localItemSources
        true,  // expectSomeUserOwnSyncChunks
        false, // expectSomeLinkedNotebooksSyncChunks
        true,  // expectSomeUserOwnNotes
        false, // expectSomeUserOwnResources
        false, // expectSomeLinkedNotebookNotes
        false, // expectSomeLinkedNotebookResources
        false, // expectSomeUserOwnDataSent
        false, // expectSomeLinkedNotebookDataSent
        false, // expectFailure
    },
    TestScenarioData{
        "Incremental sync with new server data from linked notebooks"sv, // name
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch |
            DataItemType::Tag,                           // serverDataItemTypes
        ItemGroups{} | ItemGroup::Base | ItemGroup::New, // serverItemGroups
        ItemSources{} | ItemSource::LinkedNotebook,      // serverItemSources
        DataItemTypes{}, // serverExpungedDataItemTypes
        ItemSources{},   // serverExpungedDataItemSources
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch | DataItemType::Tag, // localDataItemTypes
        ItemGroups{} | ItemGroup::Base,                    // localItemGroups
        ItemSources{} | ItemSource::LinkedNotebook,        // localItemSources
        true,  // expectSomeUserOwnSyncChunks
        true,  // expectSomeLinkedNotebooksSyncChunks
        false, // expectSomeUserOwnNotes
        false, // expectSomeUserOwnResources
        true,  // expectSomeLinkedNotebookNotes
        false, // expectSomeLinkedNotebookResources
        false, // expectSomeUserOwnDataSent
        false, // expectSomeLinkedNotebookDataSent
        false, // expectFailure
    },
    TestScenarioData{
        "Incremental sync with new server data from user's own account and "
        "linked notebooks"sv, // name
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch |
            DataItemType::Tag,                           // serverDataItemTypes
        ItemGroups{} | ItemGroup::Base | ItemGroup::New, // serverItemGroups
        ItemSources{} | ItemSource::UserOwnAccount |
            ItemSource::LinkedNotebook, // serverItemSources
        DataItemTypes{},                // serverExpungedDataItemTypes
        ItemSources{},                  // serverExpungedDataItemSources
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch | DataItemType::Tag, // localDataItemTypes
        ItemGroups{} | ItemGroup::Base,                    // localItemGroups
        ItemSources{} | ItemSource::UserOwnAccount |
            ItemSource::LinkedNotebook, // localItemSources
        true,                           // expectSomeUserOwnSyncChunks
        true,                           // expectSomeLinkedNotebooksSyncChunks
        true,                           // expectSomeUserOwnNotes
        false,                          // expectSomeUserOwnResources
        true,                           // expectSomeLinkedNotebookNotes
        false,                          // expectSomeLinkedNotebookResources
        false,                          // expectSomeUserOwnDataSent
        false,                          // expectSomeLinkedNotebookDataSent
        false,                          // expectFailure
    },
    TestScenarioData{
        "Incremental sync with modified server user own data"sv, // name
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::Resource | DataItemType::SavedSearch |
            DataItemType::Tag, // serverDataItemTypes
        ItemGroups{} | ItemGroup::Base |
            ItemGroup::Modified,                    // serverItemGroups
        ItemSources{} | ItemSource::UserOwnAccount, // serverItemSources
        DataItemTypes{}, // serverExpungedDataItemTypes
        ItemSources{},   // serverExpungedDataItemSources
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch | DataItemType::Resource |
            DataItemType::Tag,                      // localDataItemTypes
        ItemGroups{} | ItemGroup::Base,             // localItemGroups
        ItemSources{} | ItemSource::UserOwnAccount, // localItemSources
        true,  // expectSomeUserOwnSyncChunks
        false, // expectSomeLinkedNotebooksSyncChunks
        true,  // expectSomeUserOwnNotes
        true,  // expectSomeUserOwnResources
        false, // expectSomeLinkedNotebookNotes
        false, // expectSomeLinkedNotebookResources
        false, // expectSomeUserOwnDataSent
        false, // expectSomeLinkedNotebookDataSent
        false, // expectFailure
    },
    TestScenarioData{
        "Incremental sync with modified server data from linked "
        "notebooks"sv, // name
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::Resource | DataItemType::Tag, // serverDataItemTypes
        ItemGroups{} | ItemGroup::Base |
            ItemGroup::Modified,                    // serverItemGroups
        ItemSources{} | ItemSource::LinkedNotebook, // serverItemSources
        DataItemTypes{}, // serverExpungedDataItemTypes
        ItemSources{},   // serverExpungedDataItemSources
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::Resource | DataItemType::SavedSearch |
            DataItemType::Tag,                      // localDataItemTypes
        ItemGroups{} | ItemGroup::Base,             // localItemGroups
        ItemSources{} | ItemSource::LinkedNotebook, // localItemSources
        true,  // expectSomeUserOwnSyncChunks
        true,  // expectSomeLinkedNotebooksSyncChunks
        false, // expectSomeUserOwnNotes
        false, // expectSomeUserOwnResources
        true,  // expectSomeLinkedNotebookNotes
        true,  // expectSomeLinkedNotebookResources
        false, // expectSomeUserOwnDataSent
        false, // expectSomeLinkedNotebookDataSent
        false, // expectFailure
    },
    TestScenarioData{
        "Incremental sync with modified server data from user's own account "
        "and linked notebooks"sv, // name
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch |
            DataItemType::Tag, // serverDataItemTypes
        ItemGroups{} | ItemGroup::Base |
            ItemGroup::Modified, // serverItemGroups
        ItemSources{} | ItemSource::UserOwnAccount |
            ItemSource::LinkedNotebook, // serverItemSources
        DataItemTypes{},                // serverExpungedDataItemTypes
        ItemSources{},                  // serverExpungedDataItemSources
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch | DataItemType::Tag, // localDataItemTypes
        ItemGroups{} | ItemGroup::Base,                    // localItemGroups
        ItemSources{} | ItemSource::UserOwnAccount |
            ItemSource::LinkedNotebook, // localItemSources
        true,                           // expectSomeUserOwnSyncChunks
        true,                           // expectSomeLinkedNotebooksSyncChunks
        true,                           // expectSomeUserOwnNotes
        false,                          // expectSomeUserOwnResources
        true,                           // expectSomeLinkedNotebookNotes
        false,                          // expectSomeLinkedNotebookResources
        false,                          // expectSomeUserOwnDataSent
        false,                          // expectSomeLinkedNotebookDataSent
        false,                          // expectFailure
    },
    TestScenarioData{
        "Incremental sync with modified and new server user own data"sv, // name
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch |
            DataItemType::Tag, // serverDataItemTypes
        ItemGroups{} | ItemGroup::Base | ItemGroup::Modified |
            ItemGroup::New,                         // serverItemGroups
        ItemSources{} | ItemSource::UserOwnAccount, // serverItemSources
        DataItemTypes{}, // serverExpungedDataItemTypes
        ItemSources{},   // serverExpungedDataItemSources
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch | DataItemType::Tag, // localDataItemTypes
        ItemGroups{} | ItemGroup::Base,                    // localItemGroups
        ItemSources{} | ItemSource::UserOwnAccount,        // localItemSources
        true,  // expectSomeUserOwnSyncChunks
        false, // expectSomeLinkedNotebooksSyncChunks
        true,  // expectSomeUserOwnNotes
        false, // expectSomeUserOwnResources
        false, // expectSomeLinkedNotebookNotes
        false, // expectSomeLinkedNotebookResources
        false, // expectSomeUserOwnDataSent
        false, // expectSomeLinkedNotebookDataSent
        false, // expectFailure
    },
    TestScenarioData{
        "Incremental sync with modified and new server data from linked "
        "notebooks"sv, // name
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch |
            DataItemType::Tag, // serverDataItemTypes
        ItemGroups{} | ItemGroup::Base | ItemGroup::Modified |
            ItemGroup::New,                         // serverItemGroups
        ItemSources{} | ItemSource::LinkedNotebook, // serverItemSources
        DataItemTypes{}, // serverExpungedDataItemTypes
        ItemSources{},   // serverExpungedDataItemSources
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch | DataItemType::Tag, // localDataItemTypes
        ItemGroups{} | ItemGroup::Base,                    // localItemGroups
        ItemSources{} | ItemSource::LinkedNotebook,        // localItemSources
        true,  // expectSomeUserOwnSyncChunks
        true,  // expectSomeLinkedNotebooksSyncChunks
        false, // expectSomeUserOwnNotes
        false, // expectSomeUserOwnResources
        true,  // expectSomeLinkedNotebookNotes
        false, // expectSomeLinkedNotebookResources
        false, // expectSomeUserOwnDataSent
        false, // expectSomeLinkedNotebookDataSent
        false, // expectFailure
    },
    TestScenarioData{
        "Incremental sync with modified and new server data from user's own "
        "account and linked notebooks"sv, // name
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch |
            DataItemType::Tag, // serverDataItemTypes
        ItemGroups{} | ItemGroup::Base | ItemGroup::Modified |
            ItemGroup::New, // serverItemGroups
        ItemSources{} | ItemSource::UserOwnAccount |
            ItemSource::LinkedNotebook, // serverItemSources
        DataItemTypes{},                // serverExpungedDataItemTypes
        ItemSources{},                  // serverExpungedDataItemSources
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch | DataItemType::Tag, // localDataItemTypes
        ItemGroups{} | ItemGroup::Base,                    // localItemGroups
        ItemSources{} | ItemSource::UserOwnAccount |
            ItemSource::LinkedNotebook, // localItemSources
        true,                           // expectSomeUserOwnSyncChunks
        true,                           // expectSomeLinkedNotebooksSyncChunks
        true,                           // expectSomeUserOwnNotes
        false,                          // expectSomeUserOwnResources
        true,                           // expectSomeLinkedNotebookNotes
        false,                          // expectSomeLinkedNotebookResources
        false,                          // expectSomeUserOwnDataSent
        false,                          // expectSomeLinkedNotebookDataSent
        false,                          // expectFailure
    },
    TestScenarioData{
        "Incremental sync with new local data from user's own "
        "account"sv, // name
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch |
            DataItemType::Tag,                      // serverDataItemTypes
        ItemGroups{} | ItemGroup::Base,             // serverItemGroups
        ItemSources{} | ItemSource::UserOwnAccount, // serverItemSources
        DataItemTypes{}, // serverExpungedDataItemTypes
        ItemSources{},   // serverExpungedDataItemSources
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch | DataItemType::Tag, // localDataItemTypes
        ItemGroups{} | ItemGroup::Base | ItemGroup::New,   // localItemGroups
        ItemSources{} | ItemSource::UserOwnAccount,        // localItemSources
        false, // expectSomeUserOwnSyncChunks
        false, // expectSomeLinkedNotebooksSyncChunks
        false, // expectSomeUserOwnNotes
        false, // expectSomeUserOwnResources
        false, // expectSomeLinkedNotebookNotes
        false, // expectSomeLinkedNotebookResources
        true,  // expectSomeUserOwnDataSent
        false, // expectSomeLinkedNotebookDataSent
        false, // expectFailure
    },
    TestScenarioData{
        "Incremental sync with new local data from linked notebooks"sv, // name
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch |
            DataItemType::Tag,                      // serverDataItemTypes
        ItemGroups{} | ItemGroup::Base,             // serverItemGroups
        ItemSources{} | ItemSource::LinkedNotebook, // serverItemSources
        DataItemTypes{}, // serverExpungedDataItemTypes
        ItemSources{},   // serverExpungedDataItemSources
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch | DataItemType::Tag, // localDataItemTypes
        ItemGroups{} | ItemGroup::Base | ItemGroup::New,   // localItemGroups
        ItemSources{} | ItemSource::LinkedNotebook,        // localItemSources
        false, // expectSomeUserOwnSyncChunks
        false, // expectSomeLinkedNotebooksSyncChunks
        false, // expectSomeUserOwnNotes
        false, // expectSomeUserOwnResources
        false, // expectSomeLinkedNotebookNotes
        false, // expectSomeLinkedNotebookResources
        false, // expectSomeUserOwnDataSent
        true,  // expectSomeLinkedNotebookDataSent
        false, // expectFailure
    },
    TestScenarioData{
        "Incremental sync with new local data from user own account and linked "
        "notebooks"sv, // name
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
        ItemGroups{} | ItemGroup::Base | ItemGroup::New,   // localItemGroups
        ItemSources{} | ItemSource::UserOwnAccount |
            ItemSource::LinkedNotebook, // localItemSources
        false,                          // expectSomeUserOwnSyncChunks
        false,                          // expectSomeLinkedNotebooksSyncChunks
        false,                          // expectSomeUserOwnNotes
        false,                          // expectSomeUserOwnResources
        false,                          // expectSomeLinkedNotebookNotes
        false,                          // expectSomeLinkedNotebookResources
        true,                           // expectSomeUserOwnDataSent
        true,                           // expectSomeLinkedNotebookDataSent
        false,                          // expectFailure
    },
    TestScenarioData{
        "Incremental sync with modified local data from user's own "
        "account"sv, // name
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch |
            DataItemType::Tag, // serverDataItemTypes
        ItemGroups{} | ItemGroup::Base |
            ItemGroup::Modified,                    // serverItemGroups
        ItemSources{} | ItemSource::UserOwnAccount, // serverItemSources
        DataItemTypes{}, // serverExpungedDataItemTypes
        ItemSources{},   // serverExpungedDataItemSources
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch | DataItemType::Tag, // localDataItemTypes
        ItemGroups{} | ItemGroup::Base | ItemGroup::Modified, // localItemGroups
        ItemSources{} | ItemSource::UserOwnAccount, // localItemSources
        false, // expectSomeUserOwnSyncChunks
        false, // expectSomeLinkedNotebooksSyncChunks
        false, // expectSomeUserOwnNotes
        false, // expectSomeUserOwnResources
        false, // expectSomeLinkedNotebookNotes
        false, // expectSomeLinkedNotebookResources
        true,  // expectSomeUserOwnDataSent
        false, // expectSomeLinkedNotebookDataSent
        false, // expectFailure
    },
    TestScenarioData{
        "Incremental sync with modified local data from linked "
        "notebooks"sv, // name
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch |
            DataItemType::Tag, // serverDataItemTypes
        ItemGroups{} | ItemGroup::Base |
            ItemGroup::Modified,                    // serverItemGroups
        ItemSources{} | ItemSource::LinkedNotebook, // serverItemSources
        DataItemTypes{}, // serverExpungedDataItemTypes
        ItemSources{},   // serverExpungedDataItemSources
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch | DataItemType::Tag, // localDataItemTypes
        ItemGroups{} | ItemGroup::Base | ItemGroup::Modified, // localItemGroups
        ItemSources{} | ItemSource::LinkedNotebook, // localItemSources
        false, // expectSomeUserOwnSyncChunks
        false, // expectSomeLinkedNotebooksSyncChunks
        false, // expectSomeUserOwnNotes
        false, // expectSomeUserOwnResources
        false, // expectSomeLinkedNotebookNotes
        false, // expectSomeLinkedNotebookResources
        false, // expectSomeUserOwnDataSent
        true,  // expectSomeLinkedNotebookDataSent
        false, // expectFailure
    },
    TestScenarioData{
        "Incremental sync with modified local data from user's own account and "
        "linked notebooks"sv, // name
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch |
            DataItemType::Tag, // serverDataItemTypes
        ItemGroups{} | ItemGroup::Base |
            ItemGroup::Modified, // serverItemGroups
        ItemSources{} | ItemSource::UserOwnAccount |
            ItemSource::LinkedNotebook, // serverItemSources
        DataItemTypes{},                // serverExpungedDataItemTypes
        ItemSources{},                  // serverExpungedDataItemSources
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch | DataItemType::Tag, // localDataItemTypes
        ItemGroups{} | ItemGroup::Base | ItemGroup::Modified, // localItemGroups
        ItemSources{} | ItemSource::UserOwnAccount |
            ItemSource::LinkedNotebook, // localItemSources
        false,                          // expectSomeUserOwnSyncChunks
        false,                          // expectSomeLinkedNotebooksSyncChunks
        false,                          // expectSomeUserOwnNotes
        false,                          // expectSomeUserOwnResources
        false,                          // expectSomeLinkedNotebookNotes
        false,                          // expectSomeLinkedNotebookResources
        true,                           // expectSomeUserOwnDataSent
        true,                           // expectSomeLinkedNotebookDataSent
        false,                          // expectFailure
    },
    TestScenarioData{
        "Incremental sync with new and modified local data from user's own "
        "account"sv, // name
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch |
            DataItemType::Tag, // serverDataItemTypes
        ItemGroups{} | ItemGroup::Base |
            ItemGroup::Modified,                    // serverItemGroups
        ItemSources{} | ItemSource::UserOwnAccount, // serverItemSources
        DataItemTypes{}, // serverExpungedDataItemTypes
        ItemSources{},   // serverExpungedDataItemSources
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch | DataItemType::Tag, // localDataItemTypes
        ItemGroups{} | ItemGroup::Base | ItemGroup::Modified |
            ItemGroup::New,                         // localItemGroups
        ItemSources{} | ItemSource::UserOwnAccount, // localItemSources
        false, // expectSomeUserOwnSyncChunks
        false, // expectSomeLinkedNotebooksSyncChunks
        false, // expectSomeUserOwnNotes
        false, // expectSomeUserOwnResources
        false, // expectSomeLinkedNotebookNotes
        false, // expectSomeLinkedNotebookResources
        true,  // expectSomeUserOwnDataSent
        false, // expectSomeLinkedNotebookDataSent
        false, // expectFailure
    },
    TestScenarioData{
        "Incremental sync with new and modified local data from linked "
        "notebooks"sv, // name
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch |
            DataItemType::Tag, // serverDataItemTypes
        ItemGroups{} | ItemGroup::Base |
            ItemGroup::Modified,                    // serverItemGroups
        ItemSources{} | ItemSource::LinkedNotebook, // serverItemSources
        DataItemTypes{}, // serverExpungedDataItemTypes
        ItemSources{},   // serverExpungedDataItemSources
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch | DataItemType::Tag, // localDataItemTypes
        ItemGroups{} | ItemGroup::Base | ItemGroup::Modified |
            ItemGroup::New,                         // localItemGroups
        ItemSources{} | ItemSource::LinkedNotebook, // localItemSources
        false, // expectSomeUserOwnSyncChunks
        false, // expectSomeLinkedNotebooksSyncChunks
        false, // expectSomeUserOwnNotes
        false, // expectSomeUserOwnResources
        false, // expectSomeLinkedNotebookNotes
        false, // expectSomeLinkedNotebookResources
        false, // expectSomeUserOwnDataSent
        true,  // expectSomeLinkedNotebookDataSent
        false, // expectFailure
    },
    TestScenarioData{
        "Incremental sync with new and modified local data from user's own "
        "account and linked notebooks"sv, // name
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch |
            DataItemType::Tag, // serverDataItemTypes
        ItemGroups{} | ItemGroup::Base |
            ItemGroup::Modified, // serverItemGroups
        ItemSources{} | ItemSource::UserOwnAccount |
            ItemSource::LinkedNotebook, // serverItemSources
        DataItemTypes{},                // serverExpungedDataItemTypes
        ItemSources{},                  // serverExpungedDataItemSources
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch | DataItemType::Tag, // localDataItemTypes
        ItemGroups{} | ItemGroup::Base | ItemGroup::Modified |
            ItemGroup::New, // localItemGroups
        ItemSources{} | ItemSource::UserOwnAccount |
            ItemSource::LinkedNotebook, // localItemSources
        false,                          // expectSomeUserOwnSyncChunks
        false,                          // expectSomeLinkedNotebooksSyncChunks
        false,                          // expectSomeUserOwnNotes
        false,                          // expectSomeUserOwnResources
        false,                          // expectSomeLinkedNotebookNotes
        false,                          // expectSomeLinkedNotebookResources
        true,                           // expectSomeUserOwnDataSent
        true,                           // expectSomeLinkedNotebookDataSent
        false,                          // expectFailure
    },
    TestScenarioData{
        "Incremental sync with expunged data items from user's own "
        "account"sv, // name
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch |
            DataItemType::Tag,          // serverDataItemTypes
        ItemGroups{} | ItemGroup::Base, // serverItemGroups
        ItemSources{} | ItemSource::UserOwnAccount |
            ItemSource::LinkedNotebook, // serverItemSources
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch |
            DataItemType::Tag, // serverExpungedDataItemTypes
        ItemSources{} |
            ItemSource::UserOwnAccount, // serverExpungedDataItemSources
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch | DataItemType::Tag, // localDataItemTypes
        ItemGroups{} | ItemGroup::Base,                    // localItemGroups
        ItemSources{} | ItemSource::UserOwnAccount |
            ItemSource::LinkedNotebook, // localItemSources
        true,                           // expectSomeUserOwnSyncChunks
        false,                          // expectSomeLinkedNotebooksSyncChunks
        false,                          // expectSomeUserOwnNotes
        false,                          // expectSomeUserOwnResources
        false,                          // expectSomeLinkedNotebookNotes
        false,                          // expectSomeLinkedNotebookResources
        false,                          // expectSomeUserOwnDataSent
        false,                          // expectSomeLinkedNotebookDataSent
        false,                          // expectFailure
    },
    TestScenarioData{
        "Incremental sync with expunged data items from linked "
        "notebooks"sv, // name
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch |
            DataItemType::Tag,          // serverDataItemTypes
        ItemGroups{} | ItemGroup::Base, // serverItemGroups
        ItemSources{} | ItemSource::UserOwnAccount |
            ItemSource::LinkedNotebook, // serverItemSources
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::Tag, // serverExpungedDataItemTypes
        ItemSources{} |
            ItemSource::LinkedNotebook, // serverExpungedDataItemSources
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch | DataItemType::Tag, // localDataItemTypes
        ItemGroups{} | ItemGroup::Base,                    // localItemGroups
        ItemSources{} | ItemSource::UserOwnAccount |
            ItemSource::LinkedNotebook, // localItemSources
        false,                          // expectSomeUserOwnSyncChunks
        true,                           // expectSomeLinkedNotebooksSyncChunks
        false,                          // expectSomeUserOwnNotes
        false,                          // expectSomeUserOwnResources
        false,                          // expectSomeLinkedNotebookNotes
        false,                          // expectSomeLinkedNotebookResources
        false,                          // expectSomeUserOwnDataSent
        false,                          // expectSomeLinkedNotebookDataSent
        false,                          // expectFailure
    },
    TestScenarioData{
        "Incremental sync with expunged data items from user's own "
        "account and linked notebooks"sv, // name
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch |
            DataItemType::Tag,          // serverDataItemTypes
        ItemGroups{} | ItemGroup::Base, // serverItemGroups
        ItemSources{} | ItemSource::UserOwnAccount |
            ItemSource::LinkedNotebook, // serverItemSources
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch |
            DataItemType::Tag, // serverExpungedDataItemTypes
        ItemSources{} | ItemSource::UserOwnAccount |
            ItemSource::LinkedNotebook, // serverExpungedDataItemSources
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch | DataItemType::Tag, // localDataItemTypes
        ItemGroups{} | ItemGroup::Base,                    // localItemGroups
        ItemSources{} | ItemSource::UserOwnAccount |
            ItemSource::LinkedNotebook, // localItemSources
        true,                           // expectSomeUserOwnSyncChunks
        true,                           // expectSomeLinkedNotebooksSyncChunks
        false,                          // expectSomeUserOwnNotes
        false,                          // expectSomeUserOwnResources
        false,                          // expectSomeLinkedNotebookNotes
        false,                          // expectSomeLinkedNotebookResources
        false,                          // expectSomeUserOwnDataSent
        false,                          // expectSomeLinkedNotebookDataSent
        false,                          // expectFailure
    },
    TestScenarioData{
        "Incremental sync with modified, new and expunged server data items "
        "from user's own account"sv, // name
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch |
            DataItemType::Tag, // serverDataItemTypes
        ItemGroups{} | ItemGroup::Base | ItemGroup::Modified |
            ItemGroup::New,                         // serverItemGroups
        ItemSources{} | ItemSource::UserOwnAccount, // serverItemSources
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch |
            DataItemType::Tag, // serverExpungedDataItemTypes
        ItemSources{} |
            ItemSource::UserOwnAccount, // serverExpungedDataItemSources
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch | DataItemType::Tag, // localDataItemTypes
        ItemGroups{} | ItemGroup::Base,                    // localItemGroups
        ItemSources{} | ItemSource::UserOwnAccount,        // localItemSources
        true,  // expectSomeUserOwnSyncChunks
        false, // expectSomeLinkedNotebooksSyncChunks
        true,  // expectSomeUserOwnNotes
        false, // expectSomeUserOwnResources
        false, // expectSomeLinkedNotebookNotes
        false, // expectSomeLinkedNotebookResources
        false, // expectSomeUserOwnDataSent
        false, // expectSomeLinkedNotebookDataSent
        false, // expectFailure
    },
    TestScenarioData{
        "Incremental sync with modified, new and expunged server data items "
        "from linked notebooks"sv, // name
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::Tag, // serverDataItemTypes
        ItemGroups{} | ItemGroup::Base | ItemGroup::Modified |
            ItemGroup::New,                         // serverItemGroups
        ItemSources{} | ItemSource::LinkedNotebook, // serverItemSources
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::Tag, // serverExpungedDataItemTypes
        ItemSources{} |
            ItemSource::LinkedNotebook, // serverExpungedDataItemSources
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::Tag,                      // localDataItemTypes
        ItemGroups{} | ItemGroup::Base,             // localItemGroups
        ItemSources{} | ItemSource::LinkedNotebook, // localItemSources
        true,  // expectSomeUserOwnSyncChunks
        true,  // expectSomeLinkedNotebooksSyncChunks
        false, // expectSomeUserOwnNotes
        false, // expectSomeUserOwnResources
        true,  // expectSomeLinkedNotebookNotes
        false, // expectSomeLinkedNotebookResources
        false, // expectSomeUserOwnDataSent
        false, // expectSomeLinkedNotebookDataSent
        false, // expectFailure
    },
    TestScenarioData{
        "Incremental sync with modified, new and expunged server data items "
        "from user's own account and linked notebooks"sv, // name
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::Tag, // serverDataItemTypes
        ItemGroups{} | ItemGroup::Base | ItemGroup::Modified |
            ItemGroup::New, // serverItemGroups
        ItemSources{} | ItemSource::UserOwnAccount |
            ItemSource::LinkedNotebook, // serverItemSources
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::Tag, // serverExpungedDataItemTypes
        ItemSources{} | ItemSource::UserOwnAccount |
            ItemSource::LinkedNotebook, // serverExpungedDataItemSources
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::Tag,          // localDataItemTypes
        ItemGroups{} | ItemGroup::Base, // localItemGroups
        ItemSources{} | ItemSource::UserOwnAccount |
            ItemSource::LinkedNotebook, // localItemSources
        true,                           // expectSomeUserOwnSyncChunks
        true,                           // expectSomeLinkedNotebooksSyncChunks
        true,                           // expectSomeUserOwnNotes
        false,                          // expectSomeUserOwnResources
        true,                           // expectSomeLinkedNotebookNotes
        false,                          // expectSomeLinkedNotebookResources
        false,                          // expectSomeUserOwnDataSent
        false,                          // expectSomeLinkedNotebookDataSent
        false,                          // expectFailure
    },
    TestScenarioData{
        "Incremental sync with modified, new and expunged server data items "
        "and modified and new local data items from user's own "
        "account"sv, // name
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch |
            DataItemType::Tag, // serverDataItemTypes
        ItemGroups{} | ItemGroup::Base | ItemGroup::Modified |
            ItemGroup::New,                         // serverItemGroups
        ItemSources{} | ItemSource::UserOwnAccount, // serverItemSources
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch |
            DataItemType::Tag, // serverExpungedDataItemTypes
        ItemSources{} |
            ItemSource::UserOwnAccount, // serverExpungedDataItemSources
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch | DataItemType::Tag, // localDataItemTypes
        ItemGroups{} | ItemGroup::Base | ItemGroup::Modified |
            ItemGroup::New,                         // localItemGroups
        ItemSources{} | ItemSource::UserOwnAccount, // localItemSources
        true,  // expectSomeUserOwnSyncChunks
        false, // expectSomeLinkedNotebooksSyncChunks
        true,  // expectSomeUserOwnNotes
        false, // expectSomeUserOwnResources
        false, // expectSomeLinkedNotebookNotes
        false, // expectSomeLinkedNotebookResources
        true,  // expectSomeUserOwnDataSent
        false, // expectSomeLinkedNotebookDataSent
        false, // expectFailure
    },
    TestScenarioData{
        "Incremental sync with modified, new and expunged server data items "
        "and modified and new local data items from linked notebooks"sv, // name
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch |
            DataItemType::Tag, // serverDataItemTypes
        ItemGroups{} | ItemGroup::Base | ItemGroup::Modified |
            ItemGroup::New,                         // serverItemGroups
        ItemSources{} | ItemSource::LinkedNotebook, // serverItemSources
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch |
            DataItemType::Tag, // serverExpungedDataItemTypes
        ItemSources{} |
            ItemSource::LinkedNotebook, // serverExpungedDataItemSources
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch | DataItemType::Tag, // localDataItemTypes
        ItemGroups{} | ItemGroup::Base | ItemGroup::Modified |
            ItemGroup::New,                         // localItemGroups
        ItemSources{} | ItemSource::LinkedNotebook, // localItemSources
        true,  // expectSomeUserOwnSyncChunks
        true,  // expectSomeLinkedNotebooksSyncChunks
        false, // expectSomeUserOwnNotes
        false, // expectSomeUserOwnResources
        true,  // expectSomeLinkedNotebookNotes
        false, // expectSomeLinkedNotebookResources
        false, // expectSomeUserOwnDataSent
        true,  // expectSomeLinkedNotebookDataSent
        false, // expectFailure
    },
    TestScenarioData{
        "Incremental sync with modified, new and expunged server data items "
        "and modified and new local data items from user's own account and "
        "linked notebooks"sv, // name
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch |
            DataItemType::Tag, // serverDataItemTypes
        ItemGroups{} | ItemGroup::Base | ItemGroup::Modified |
            ItemGroup::New, // serverItemGroups
        ItemSources{} | ItemSource::UserOwnAccount |
            ItemSource::LinkedNotebook, // serverItemSources
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch |
            DataItemType::Tag, // serverExpungedDataItemTypes
        ItemSources{} | ItemSource::UserOwnAccount |
            ItemSource::LinkedNotebook, // serverExpungedDataItemSources
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch | DataItemType::Tag, // localDataItemTypes
        ItemGroups{} | ItemGroup::Base | ItemGroup::Modified |
            ItemGroup::New, // localItemGroups
        ItemSources{} | ItemSource::UserOwnAccount |
            ItemSource::LinkedNotebook, // localItemSources
        true,                           // expectSomeUserOwnSyncChunks
        true,                           // expectSomeLinkedNotebooksSyncChunks
        true,                           // expectSomeUserOwnNotes
        false,                          // expectSomeUserOwnResources
        true,                           // expectSomeLinkedNotebookNotes
        false,                          // expectSomeLinkedNotebookResources
        true,                           // expectSomeUserOwnDataSent
        true,                           // expectSomeLinkedNotebookDataSent
        false,                          // expectFailure
    },
    TestScenarioData{
        "EDAM version major mismatch"sv,             // name
        DataItemTypes{} | DataItemType::SavedSearch, // serverDataItemTypes
        ItemGroups{} | ItemGroup::Base,              // serverItemGroups
        ItemSources{} | ItemSource::UserOwnAccount,  // serverItemSources
        DataItemTypes{}, // serverExpungedDataItemTypes
        ItemSources{},   // serverExpungedDataItemSources
        DataItemTypes{}, // localDataItemTypes
        ItemGroups{},    // localItemGroups
        ItemSources{},   // localItemSources
        false,           // expectSomeUserOwnSyncChunks
        false,           // expectSomeLinkedNotebooksSyncChunks
        false,           // expectSomeUserOwnNotes
        false,           // expectSomeUserOwnResources
        false,           // expectSomeLinkedNotebookNotes
        false,           // expectSomeLinkedNotebookResources
        false,           // expectSomeUserOwnDataSent
        false,           // expectSomeLinkedNotebookDataSent
        true,            // expectFailure
        StopSynchronizationError{std::monostate{}}, // stopSyncError
        std::nullopt,                               // stopSyncErrorTrigger
        qint16{2},                                  // edamVersionMajor
        qevercloud::EDAM_VERSION_MINOR,             // edamVersionMinor
    },
    TestScenarioData{
        "EDAM version minor mismatch"sv,             // name
        DataItemTypes{} | DataItemType::SavedSearch, // serverDataItemTypes
        ItemGroups{} | ItemGroup::Base,              // serverItemGroups
        ItemSources{} | ItemSource::UserOwnAccount,  // serverItemSources
        DataItemTypes{}, // serverExpungedDataItemTypes
        ItemSources{},   // serverExpungedDataItemSources
        DataItemTypes{}, // localDataItemTypes
        ItemGroups{},    // localItemGroups
        ItemSources{},   // localItemSources
        false,           // expectSomeUserOwnSyncChunks
        false,           // expectSomeLinkedNotebooksSyncChunks
        false,           // expectSomeUserOwnNotes
        false,           // expectSomeUserOwnResources
        false,           // expectSomeLinkedNotebookNotes
        false,           // expectSomeLinkedNotebookResources
        false,           // expectSomeUserOwnDataSent
        false,           // expectSomeLinkedNotebookDataSent
        true,            // expectFailure
        StopSynchronizationError{std::monostate{}}, // stopSyncError
        std::nullopt,                               // stopSyncErrorTrigger
        qevercloud::EDAM_VERSION_MAJOR,             // edamVersionMajor
        qint16{99},                                 // edamVersionMinor
    },
    TestScenarioData{
        "Full sync with rate limit exceeding on getting user own sync "
        "state"sv, // name
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
        true,            // expectSomeUserOwnSyncChunks
        false,           // expectSomeLinkedNotebooksSyncChunks
        true,            // expectSomeUserOwnNotes
        false,           // expectSomeUserOwnResources
        false,           // expectSomeLinkedNotebookNotes
        false,           // expectSomeLinkedNotebookResources
        false,           // expectSomeUserOwnDataSent
        false,           // expectSomeLinkedNotebookDataSent
        false,           // expectFailure
        StopSynchronizationError{RateLimitReachedError{120}}, // stopSyncError
        StopSynchronizationErrorTrigger::
            OnGetUserOwnSyncState, // stopSyncErrorTrigger
    },
    TestScenarioData{
        "Full sync with rate limit exceeding on getting linked notebook sync "
        "state"sv, // name
        DataItemTypes{} | DataItemType::Notebook |
            DataItemType::Note,                     // serverDataItemTypes
        ItemGroups{} | ItemGroup::Base,             // serverItemGroups
        ItemSources{} | ItemSource::LinkedNotebook, // serverItemSources
        DataItemTypes{}, // serverExpungedDataItemTypes
        ItemSources{},   // serverExpungedDataItemSources
        DataItemTypes{}, // localDataItemTypes
        ItemGroups{},    // localItemGroups
        ItemSources{},   // localItemSources
        true,            // expectSomeUserOwnSyncChunks
        true,            // expectSomeLinkedNotebooksSyncChunks
        false,           // expectSomeUserOwnNotes
        false,           // expectSomeUserOwnResources
        true,            // expectSomeLinkedNotebookNotes
        false,           // expectSomeLinkedNotebookResources
        false,           // expectSomeUserOwnDataSent
        false,           // expectSomeLinkedNotebookDataSent
        false,           // expectFailure
        StopSynchronizationError{RateLimitReachedError{120}}, // stopSyncError
        StopSynchronizationErrorTrigger::
            OnGetLinkedNotebookSyncState, // stopSyncErrorTrigger
    },
    TestScenarioData{
        "Full sync with rate limit exceeding on getting user own sync "
        "chunk"sv, // name
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
        true,            // expectSomeUserOwnSyncChunks
        false,           // expectSomeLinkedNotebooksSyncChunks
        true,            // expectSomeUserOwnNotes
        false,           // expectSomeUserOwnResources
        false,           // expectSomeLinkedNotebookNotes
        false,           // expectSomeLinkedNotebookResources
        false,           // expectSomeUserOwnDataSent
        false,           // expectSomeLinkedNotebookDataSent
        false,           // expectFailure
        StopSynchronizationError{RateLimitReachedError{120}}, // stopSyncError
        StopSynchronizationErrorTrigger::
            OnGetUserOwnSyncChunk, // stopSyncErrorTrigger
    },
    TestScenarioData{
        "Full sync with rate limit exceeding on getting user own note"sv, // name
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
        true,            // expectSomeUserOwnSyncChunks
        false,           // expectSomeLinkedNotebooksSyncChunks
        true,            // expectSomeUserOwnNotes
        false,           // expectSomeUserOwnResources
        false,           // expectSomeLinkedNotebookNotes
        false,           // expectSomeLinkedNotebookResources
        false,           // expectSomeUserOwnDataSent
        false,           // expectSomeLinkedNotebookDataSent
        false,           // expectFailure
        StopSynchronizationError{RateLimitReachedError{120}}, // stopSyncError
        StopSynchronizationErrorTrigger::
            OnGetNoteAfterDownloadingUserOwnSyncChunks, // stopSyncErrorTrigger
    },
    TestScenarioData{
        "Incremental sync with rate limit exceeding on getting user own "
        "resource"sv, // name
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::Resource | DataItemType::SavedSearch |
            DataItemType::Tag, // serverDataItemTypes
        ItemGroups{} | ItemGroup::Base |
            ItemGroup::Modified,                    // serverItemGroups
        ItemSources{} | ItemSource::UserOwnAccount, // serverItemSources
        DataItemTypes{}, // serverExpungedDataItemTypes
        ItemSources{},   // serverExpungedDataItemSources
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch | DataItemType::Resource |
            DataItemType::Tag,                      // localDataItemTypes
        ItemGroups{} | ItemGroup::Base,             // localItemGroups
        ItemSources{} | ItemSource::UserOwnAccount, // localItemSources
        true,  // expectSomeUserOwnSyncChunks
        false, // expectSomeLinkedNotebooksSyncChunks
        false, // expectSomeUserOwnNotes
        true,  // expectSomeUserOwnResources
        false, // expectSomeLinkedNotebookNotes
        false, // expectSomeLinkedNotebookResources
        false, // expectSomeUserOwnDataSent
        false, // expectSomeLinkedNotebookDataSent
        false, // expectFailure
        StopSynchronizationError{RateLimitReachedError{120}}, // stopSyncError
        StopSynchronizationErrorTrigger::
            OnGetResourceAfterDownloadingUserOwnSyncChunks, // stopSyncErrorTrigger
    },
    TestScenarioData{
        "Full sync with rate limit exceeding on getting linked notebook "
        "sync chunk"sv, // name
        DataItemTypes{} | DataItemType::Notebook |
            DataItemType::Note,                     // serverDataItemTypes
        ItemGroups{} | ItemGroup::Base,             // serverItemGroups
        ItemSources{} | ItemSource::LinkedNotebook, // serverItemSources
        DataItemTypes{}, // serverExpungedDataItemTypes
        ItemSources{},   // serverExpungedDataItemSources
        DataItemTypes{}, // localDataItemTypes
        ItemGroups{},    // localItemGroups
        ItemSources{},   // localItemSources
        true,            // expectSomeUserOwnSyncChunks
        true,            // expectSomeLinkedNotebooksSyncChunks
        false,           // expectSomeUserOwnNotes
        false,           // expectSomeUserOwnResources
        true,            // expectSomeLinkedNotebookNotes
        false,           // expectSomeLinkedNotebookResources
        false,           // expectSomeUserOwnDataSent
        false,           // expectSomeLinkedNotebookDataSent
        false,           // expectFailure
        StopSynchronizationError{RateLimitReachedError{120}}, // stopSyncError
        StopSynchronizationErrorTrigger::
            OnGetLinkedNotebookSyncChunk, // stopSyncErrorTrigger
    },
    TestScenarioData{
        "Full sync with rate limit exceeding on getting linked notebook "
        "note"sv, // name
        DataItemTypes{} | DataItemType::Notebook |
            DataItemType::Note,                     // serverDataItemTypes
        ItemGroups{} | ItemGroup::Base,             // serverItemGroups
        ItemSources{} | ItemSource::LinkedNotebook, // serverItemSources
        DataItemTypes{}, // serverExpungedDataItemTypes
        ItemSources{},   // serverExpungedDataItemSources
        DataItemTypes{}, // localDataItemTypes
        ItemGroups{},    // localItemGroups
        ItemSources{},   // localItemSources
        true,            // expectSomeUserOwnSyncChunks
        true,            // expectSomeLinkedNotebooksSyncChunks
        false,           // expectSomeUserOwnNotes
        false,           // expectSomeUserOwnResources
        true,            // expectSomeLinkedNotebookNotes
        false,           // expectSomeLinkedNotebookResources
        false,           // expectSomeUserOwnDataSent
        false,           // expectSomeLinkedNotebookDataSent
        false,           // expectFailure
        StopSynchronizationError{RateLimitReachedError{120}}, // stopSyncError
        StopSynchronizationErrorTrigger::
            OnGetNoteAfterDownloadingLinkedNotebookSyncChunks, // stopSyncErrorTrigger
    },
    TestScenarioData{
        "Incremental sync with rate limit exceeding on getting linked notebook "
        "resource"sv, // name
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::Resource | DataItemType::Tag, // serverDataItemTypes
        ItemGroups{} | ItemGroup::Base |
            ItemGroup::Modified,                    // serverItemGroups
        ItemSources{} | ItemSource::LinkedNotebook, // serverItemSources
        DataItemTypes{}, // serverExpungedDataItemTypes
        ItemSources{},   // serverExpungedDataItemSources
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::Resource | DataItemType::SavedSearch |
            DataItemType::Tag,                      // localDataItemTypes
        ItemGroups{} | ItemGroup::Base,             // localItemGroups
        ItemSources{} | ItemSource::LinkedNotebook, // localItemSources
        true,  // expectSomeUserOwnSyncChunks
        true,  // expectSomeLinkedNotebooksSyncChunks
        false, // expectSomeUserOwnNotes
        false, // expectSomeUserOwnResources
        true,  // expectSomeLinkedNotebookNotes
        true,  // expectSomeLinkedNotebookResources
        false, // expectSomeUserOwnDataSent
        false, // expectSomeLinkedNotebookDataSent
        false, // expectFailure
        StopSynchronizationError{RateLimitReachedError{120}}, // stopSyncError
        StopSynchronizationErrorTrigger::
            OnGetResourceAfterDownloadingLinkedNotebookSyncChunks, // stopSyncErrorTrigger
    },
    TestScenarioData{
        "Incremental sync with rate limit exceeding on creating a saved "
        "search"sv, // name
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch |
            DataItemType::Tag,                      // serverDataItemTypes
        ItemGroups{} | ItemGroup::Base,             // serverItemGroups
        ItemSources{} | ItemSource::UserOwnAccount, // serverItemSources
        DataItemTypes{}, // serverExpungedDataItemTypes
        ItemSources{},   // serverExpungedDataItemSources
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch | DataItemType::Tag, // localDataItemTypes
        ItemGroups{} | ItemGroup::Base | ItemGroup::New,   // localItemGroups
        ItemSources{} | ItemSource::UserOwnAccount,        // localItemSources
        false, // expectSomeUserOwnSyncChunks
        false, // expectSomeLinkedNotebooksSyncChunks
        false, // expectSomeUserOwnNotes
        false, // expectSomeUserOwnResources
        false, // expectSomeLinkedNotebookNotes
        false, // expectSomeLinkedNotebookResources
        true,  // expectSomeUserOwnDataSent
        false, // expectSomeLinkedNotebookDataSent
        false, // expectFailure
        StopSynchronizationError{RateLimitReachedError{120}}, // stopSyncError
        StopSynchronizationErrorTrigger::
            OnCreateSavedSearch, // stopSyncErrorTrigger
    },
    TestScenarioData{
        "Incremental sync with rate limit exceeding on updating a saved "
        "search"sv, // name
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch |
            DataItemType::Tag, // serverDataItemTypes
        ItemGroups{} | ItemGroup::Base |
            ItemGroup::Modified,                    // serverItemGroups
        ItemSources{} | ItemSource::UserOwnAccount, // serverItemSources
        DataItemTypes{}, // serverExpungedDataItemTypes
        ItemSources{},   // serverExpungedDataItemSources
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch | DataItemType::Tag, // localDataItemTypes
        ItemGroups{} | ItemGroup::Base | ItemGroup::Modified, // localItemGroups
        ItemSources{} | ItemSource::UserOwnAccount, // localItemSources
        false, // expectSomeUserOwnSyncChunks
        false, // expectSomeLinkedNotebooksSyncChunks
        false, // expectSomeUserOwnNotes
        false, // expectSomeUserOwnResources
        false, // expectSomeLinkedNotebookNotes
        false, // expectSomeLinkedNotebookResources
        true,  // expectSomeUserOwnDataSent
        false, // expectSomeLinkedNotebookDataSent
        false, // expectFailure
        StopSynchronizationError{RateLimitReachedError{120}}, // stopSyncError
        StopSynchronizationErrorTrigger::
            OnUpdateSavedSearch, // stopSyncErrorTrigger
    },
    TestScenarioData{
        "Incremental sync with rate limit exceeding on creating a "
        "tag"sv, // name
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch |
            DataItemType::Tag,                      // serverDataItemTypes
        ItemGroups{} | ItemGroup::Base,             // serverItemGroups
        ItemSources{} | ItemSource::UserOwnAccount, // serverItemSources
        DataItemTypes{}, // serverExpungedDataItemTypes
        ItemSources{},   // serverExpungedDataItemSources
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch | DataItemType::Tag, // localDataItemTypes
        ItemGroups{} | ItemGroup::Base | ItemGroup::New,   // localItemGroups
        ItemSources{} | ItemSource::UserOwnAccount,        // localItemSources
        false, // expectSomeUserOwnSyncChunks
        false, // expectSomeLinkedNotebooksSyncChunks
        false, // expectSomeUserOwnNotes
        false, // expectSomeUserOwnResources
        false, // expectSomeLinkedNotebookNotes
        false, // expectSomeLinkedNotebookResources
        true,  // expectSomeUserOwnDataSent
        false, // expectSomeLinkedNotebookDataSent
        false, // expectFailure
        StopSynchronizationError{RateLimitReachedError{120}}, // stopSyncError
        StopSynchronizationErrorTrigger::OnCreateTag, // stopSyncErrorTrigger
    },
    TestScenarioData{
        "Incremental sync with rate limit exceeding on updating a "
        "tag"sv, // name
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch |
            DataItemType::Tag, // serverDataItemTypes
        ItemGroups{} | ItemGroup::Base |
            ItemGroup::Modified,                    // serverItemGroups
        ItemSources{} | ItemSource::UserOwnAccount, // serverItemSources
        DataItemTypes{}, // serverExpungedDataItemTypes
        ItemSources{},   // serverExpungedDataItemSources
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch | DataItemType::Tag, // localDataItemTypes
        ItemGroups{} | ItemGroup::Base | ItemGroup::Modified, // localItemGroups
        ItemSources{} | ItemSource::UserOwnAccount, // localItemSources
        false, // expectSomeUserOwnSyncChunks
        false, // expectSomeLinkedNotebooksSyncChunks
        false, // expectSomeUserOwnNotes
        false, // expectSomeUserOwnResources
        false, // expectSomeLinkedNotebookNotes
        false, // expectSomeLinkedNotebookResources
        true,  // expectSomeUserOwnDataSent
        false, // expectSomeLinkedNotebookDataSent
        false, // expectFailure
        StopSynchronizationError{RateLimitReachedError{120}}, // stopSyncError
        StopSynchronizationErrorTrigger::OnUpdateTag, // stopSyncErrorTrigger
    },
    TestScenarioData{
        "Incremental sync with rate limit exceeding on creating a "
        "notebook"sv, // name
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch |
            DataItemType::Tag,                      // serverDataItemTypes
        ItemGroups{} | ItemGroup::Base,             // serverItemGroups
        ItemSources{} | ItemSource::UserOwnAccount, // serverItemSources
        DataItemTypes{}, // serverExpungedDataItemTypes
        ItemSources{},   // serverExpungedDataItemSources
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch | DataItemType::Tag, // localDataItemTypes
        ItemGroups{} | ItemGroup::Base | ItemGroup::New,   // localItemGroups
        ItemSources{} | ItemSource::UserOwnAccount,        // localItemSources
        false, // expectSomeUserOwnSyncChunks
        false, // expectSomeLinkedNotebooksSyncChunks
        false, // expectSomeUserOwnNotes
        false, // expectSomeUserOwnResources
        false, // expectSomeLinkedNotebookNotes
        false, // expectSomeLinkedNotebookResources
        true,  // expectSomeUserOwnDataSent
        false, // expectSomeLinkedNotebookDataSent
        false, // expectFailure
        StopSynchronizationError{RateLimitReachedError{120}}, // stopSyncError
        StopSynchronizationErrorTrigger::
            OnCreateNotebook, // stopSyncErrorTrigger
    },
    TestScenarioData{
        "Incremental sync with rate limit exceeding on updating a "
        "notebook"sv, // name
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch |
            DataItemType::Tag, // serverDataItemTypes
        ItemGroups{} | ItemGroup::Base |
            ItemGroup::Modified,                    // serverItemGroups
        ItemSources{} | ItemSource::UserOwnAccount, // serverItemSources
        DataItemTypes{}, // serverExpungedDataItemTypes
        ItemSources{},   // serverExpungedDataItemSources
        DataItemTypes{} | DataItemType::Notebook | DataItemType::Note |
            DataItemType::SavedSearch | DataItemType::Tag, // localDataItemTypes
        ItemGroups{} | ItemGroup::Base | ItemGroup::Modified, // localItemGroups
        ItemSources{} | ItemSource::UserOwnAccount, // localItemSources
        false, // expectSomeUserOwnSyncChunks
        false, // expectSomeLinkedNotebooksSyncChunks
        false, // expectSomeUserOwnNotes
        false, // expectSomeUserOwnResources
        false, // expectSomeLinkedNotebookNotes
        false, // expectSomeLinkedNotebookResources
        true,  // expectSomeUserOwnDataSent
        false, // expectSomeLinkedNotebookDataSent
        false, // expectFailure
        StopSynchronizationError{RateLimitReachedError{120}}, // stopSyncError
        StopSynchronizationErrorTrigger::
            OnUpdateNotebook, // stopSyncErrorTrigger
    },
};

} // namespace quentier::synchronization::tests
