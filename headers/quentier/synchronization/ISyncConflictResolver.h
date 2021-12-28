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

#include <quentier/utility/Linkage.h>

#include <qevercloud/types/Notebook.h>
#include <qevercloud/types/Note.h>
#include <qevercloud/types/SavedSearch.h>
#include <qevercloud/types/Tag.h>

#include <QFuture>

#include <variant>

namespace quentier::synchronization {

class QUENTIER_EXPORT ISyncConflictResolver
{
public:
    struct ConflictResolution
    {
        struct UseTheirs
        {};

        struct UseMine
        {};

        struct IgnoreMine
        {};

        template <class T>
        struct MoveMine
        {
            T mine;
        };
    };

    using NotebookConflictResolution = std::variant<
        ConflictResolution::UseTheirs, ConflictResolution::UseMine,
        ConflictResolution::IgnoreMine,
        ConflictResolution::MoveMine<qevercloud::Notebook>>;

    using NoteConflictResolution = std::variant<
        ConflictResolution::UseTheirs, ConflictResolution::UseMine,
        ConflictResolution::IgnoreMine,
        ConflictResolution::MoveMine<qevercloud::Note>>;

    using SavedSearchConflictResolution = std::variant<
        ConflictResolution::UseTheirs, ConflictResolution::UseMine,
        ConflictResolution::IgnoreMine,
        ConflictResolution::MoveMine<qevercloud::SavedSearch>>;

    using TagConflictResolution = std::variant<
        ConflictResolution::IgnoreMine,
        ConflictResolution::UseTheirs, ConflictResolution::UseMine,
        ConflictResolution::MoveMine<qevercloud::Tag>>;

public:
    virtual ~ISyncConflictResolver() = default;

    [[nodiscard]] virtual QFuture<NotebookConflictResolution>
        resolveNotebookConflict(
            qevercloud::Notebook theirs, qevercloud::Notebook mine) = 0;

    [[nodiscard]] virtual QFuture<NoteConflictResolution> resolveNoteConflict(
        qevercloud::Note theirs, qevercloud::Note mine) = 0;

    [[nodiscard]] virtual QFuture<SavedSearchConflictResolution>
        resolveSavedSearchConflict(
            qevercloud::SavedSearch theirs, qevercloud::SavedSearch mine) = 0;

    [[nodiscard]] virtual QFuture<TagConflictResolution> resolveTagConflict(
        qevercloud::Tag theirs, qevercloud::Tag mine) = 0;
};

} // namespace quentier::synchronization
