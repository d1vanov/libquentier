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

#include "ISimpleNotebookSyncConflictResolver.h"
#include "ISimpleNoteSyncConflictResolver.h"
#include "ISimpleSavedSearchSyncConflictResolver.h"
#include "ISimpleTagSyncConflictResolver.h"
#include "SimpleSyncConflictResolver.h"

#include <quentier/exception/InvalidArgument.h>

namespace quentier::synchronization {

SimpleSyncConflictResolver::SimpleSyncConflictResolver(
    ISimpleNotebookSyncConflictResolverPtr notebookConflictResolver,
    ISimpleNoteSyncConflictResolverPtr noteConflictResolver,
    ISimpleSavedSearchSyncConflictResolverPtr savedSearchConflictResolver,
    ISimpleTagSyncConflictResolverPtr tagConflictResolver) :
    m_notebookConflictResolver{std::move(notebookConflictResolver)},
    m_noteConflictResolver{std::move(noteConflictResolver)},
    m_savedSearchConflictResolver{std::move(savedSearchConflictResolver)},
    m_tagConflictResolver{std::move(tagConflictResolver)}
{
    if (Q_UNLIKELY(!m_notebookConflictResolver)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::SimpleSyncConflictResolver",
            "SimpleSyncConflictResolver ctor: null notebook conflict "
            "resolver")}};
    }

    if (Q_UNLIKELY(!m_noteConflictResolver)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::SimpleSyncConflictResolver",
            "SimpleSyncConflictResolver ctor: null note conflict resolver")}};
    }

    if (Q_UNLIKELY(!m_savedSearchConflictResolver)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::SimpleSyncConflictResolver",
            "SimpleSyncConflictResolver ctor: null saved search conflict "
            "resolver")}};
    }

    if (Q_UNLIKELY(!m_tagConflictResolver)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::SimpleSyncConflictResolver",
            "SimpleSyncConflictResolver ctor: null tag conflict resolver")}};
    }
}

QFuture<ISyncConflictResolver::NotebookConflictResolution>
    SimpleSyncConflictResolver::resolveNotebooksConflict(
        qevercloud::Notebook theirs, qevercloud::Notebook mine)
{
    return m_notebookConflictResolver->resolveNotebooksConflict(
        std::move(theirs), std::move(mine));
}

QFuture<ISyncConflictResolver::NoteConflictResolution>
    SimpleSyncConflictResolver::resolveNoteConflict(
        qevercloud::Note theirs, qevercloud::Note mine)
{
    return m_noteConflictResolver->resolveNoteConflict(
        std::move(theirs), std::move(mine));
}

QFuture<ISyncConflictResolver::SavedSearchConflictResolution>
    SimpleSyncConflictResolver::resolveSavedSearchConflict(
        qevercloud::SavedSearch theirs, qevercloud::SavedSearch mine)
{
    return m_savedSearchConflictResolver->resolveSavedSearchConflict(
        std::move(theirs), std::move(mine));
}

QFuture<ISyncConflictResolver::TagConflictResolution>
    SimpleSyncConflictResolver::resolveTagConflict(
        qevercloud::Tag theirs, qevercloud::Tag mine)
{
    return m_tagConflictResolver->resolveTagConflict(
        std::move(theirs), std::move(mine));
}

} // namespace quentier::synchronization
