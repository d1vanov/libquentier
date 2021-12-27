/*
 * Copyright 2021 Dmitry Ivanov
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

#include "Fwd.h"

#include <quentier/synchronization/ISyncConflictResolver.h>

namespace quentier::synchronization {

class SimpleSyncConflictResolver final :
    public std::enable_shared_from_this<SimpleSyncConflictResolver>,
    public ISyncConflictResolver
{
public:
    explicit SimpleSyncConflictResolver(
        ISimpleNotebookSyncConflictResolverPtr notebookConflictResolver,
        ISimpleSavedSearchSyncConflictResolverPtr savedSearchConflictResolver);

    [[nodiscard]] QFuture<NotebookConflictResolution> resolveNotebooksConflict(
        qevercloud::Notebook theirs, qevercloud::Notebook mine) override;

    [[nodiscard]] QFuture<NoteConflictResolution> resolveNoteConflict(
        qevercloud::Note theirs, qevercloud::Note mine) override;

    [[nodiscard]] QFuture<SavedSearchConflictResolution>
        resolveSavedSearchConflict(
            qevercloud::SavedSearch theirs,
            qevercloud::SavedSearch mine) override;

    [[nodiscard]] QFuture<TagConflictResolution> resolveTagConflict(
        qevercloud::Tag theirs, qevercloud::Tag mine) override;

private:
    ISimpleNotebookSyncConflictResolverPtr m_notebookConflictResolver;
    ISimpleSavedSearchSyncConflictResolverPtr m_savedSearchConflictResolver;
};

} // namespace quentier::synchronization
