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

#include "Setup.h"

#include <quentier/synchronization/types/Errors.h>

#include <qevercloud/exceptions/Fwd.h>

#include <QMetaType>

#include <string_view>

namespace quentier::synchronization::tests {

struct TestScenarioData
{
    DataItemTypes serverDataItemTypes;
    ItemGroups serverItemGroups;
    ItemSources serverItemSources;

    DataItemTypes serverExpungedDataItemTypes;
    ItemSources serverExpungedDataItemSources;

    DataItemTypes localDataItemTypes;
    ItemGroups localItemGroups;
    ItemSources localItemSources;

    StopSynchronizationError stopSyncError;
    bool expectFailure = false;

    // Expectations of sync events received during the sync
    bool expectSomeUserOwnSyncChunks = false;
    bool expectSomeLinkedNotebooksSyncChunks = false;
    bool expectSomeUserOwnNotes = false;
    bool expectSomeUserOwnResources = false;
    bool expectSomeLinkedNotebookNotes = false;
    bool expectSomeLinkedNotebookResources = false;
    bool expectSomeUserOwnDataSent = false;
    bool expectSomeLinkedNotebookDataSent = false;

    // Name of the test scenario
    std::string_view name;
};

} // namespace quentier::synchronization::tests

Q_DECLARE_METATYPE(quentier::synchronization::tests::TestScenarioData); // NOLINT
