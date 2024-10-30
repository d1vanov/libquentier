/*
 * Copyright 2022-2024 Dmitry Ivanov
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

#include <quentier/synchronization/ISyncConflictResolver.h>

#include <gmock/gmock.h>

namespace quentier::synchronization::tests::mocks {

class MockISyncConflictResolver : public ISyncConflictResolver
{
public:
    MOCK_METHOD(
        QFuture<NotebookConflictResolution>, resolveNotebookConflict,
        (::qevercloud::Notebook theirs, ::qevercloud::Notebook mine), (override));

    MOCK_METHOD(
        QFuture<NoteConflictResolution>, resolveNoteConflict,
        (::qevercloud::Note theirs, ::qevercloud::Note mine), (override));

    MOCK_METHOD(
        QFuture<SavedSearchConflictResolution>, resolveSavedSearchConflict,
        (::qevercloud::SavedSearch theirs, ::qevercloud::SavedSearch mine),
        (override));

    MOCK_METHOD(
        QFuture<TagConflictResolution>, resolveTagConflict,
        (::qevercloud::Tag theirs, ::qevercloud::Tag mine), (override));
};

} // namespace quentier::synchronization::tests::mocks
