/*
 * Copyright 2021-2024 Dmitry Ivanov
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

#include <qevercloud/types/Note.h>
#include <qevercloud/types/Notebook.h>
#include <qevercloud/types/SavedSearch.h>
#include <qevercloud/types/Tag.h>

#include <QFuture>

#include <variant>

class QDebug;
class QTextStream;

namespace quentier::synchronization {

/**
 * @brief The ISyncConflictResolver interface provides methods used to resolve
 * conflicts between local and remote versions of the same data item.
 */
class QUENTIER_EXPORT ISyncConflictResolver
{
public:
    /**
     * @brief The ConflictResolution struct is a namespace inside which
     * several other structs determining actual conflict resolutions.
     */
    struct QUENTIER_EXPORT ConflictResolution
    {
        /**
         * @brief The UseTheirs conflict resolution means "override mine version
         * with theirs version".
         */
        struct QUENTIER_EXPORT UseTheirs
        {};

        /**
         * @brief The UseMine conflict resolution means "override theirs version
         * with mine version".
         */
        struct QUENTIER_EXPORT UseMine
        {};

        /**
         * @brief The IgnoreMine conflict resolution means "use theirs version
         * and ignore mine version as it doesn't really conflict with theirs
         * version".
         */
        struct QUENTIER_EXPORT IgnoreMine
        {};

        /**
         * @brief The MoveMine conflict resolution means "before using theirs
         * version change mine version as specified". Note: the data item inside
         * this conflict resolution might refer to something different than mine
         * version passed to the conflict resolution function. It can be that
         * way because the actual conflict might be with another local data item
         * instead of the passed one.
         */
        template <class T>
        struct MoveMine
        {
            using value_type = T;

            /**
             * The changed value of mine data item.
             */
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
        ConflictResolution::IgnoreMine, ConflictResolution::UseTheirs,
        ConflictResolution::UseMine,
        ConflictResolution::MoveMine<qevercloud::Tag>>;

public:
    virtual ~ISyncConflictResolver() noexcept;

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

QUENTIER_EXPORT QTextStream & operator<<(
    QTextStream & strm,
    const ISyncConflictResolver::NotebookConflictResolution & resolution);

QUENTIER_EXPORT QDebug & operator<<(
    QDebug & dbg,
    const ISyncConflictResolver::NotebookConflictResolution & resolution);

QUENTIER_EXPORT QTextStream & operator<<(
    QTextStream & strm,
    const ISyncConflictResolver::NoteConflictResolution & resolution);

QUENTIER_EXPORT QDebug & operator<<(
    QDebug & dbg,
    const ISyncConflictResolver::NoteConflictResolution & resolution);

QUENTIER_EXPORT QTextStream & operator<<(
    QTextStream & strm,
    const ISyncConflictResolver::SavedSearchConflictResolution & resolution);

QUENTIER_EXPORT QDebug & operator<<(
    QDebug & dbg,
    const ISyncConflictResolver::SavedSearchConflictResolution & resolution);

QUENTIER_EXPORT QTextStream & operator<<(
    QTextStream & strm,
    const ISyncConflictResolver::TagConflictResolution & resolution);

QUENTIER_EXPORT QDebug & operator<<(
    QDebug & dbg,
    const ISyncConflictResolver::TagConflictResolution & resolution);

} // namespace quentier::synchronization
