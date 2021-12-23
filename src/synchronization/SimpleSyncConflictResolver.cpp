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

#include "SimpleSyncConflictResolver.h"

#include <utility/Threading.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/local_storage/ILocalStorage.h>
#include <quentier/logging/QuentierLogger.h>

namespace quentier::synchronization {

SimpleSyncConflictResolver::SimpleSyncConflictResolver(
    local_storage::ILocalStoragePtr localStorage) :
    m_localStorage{std::move(localStorage)}
{
    if (Q_UNLIKELY(!m_localStorage)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::SimpleSyncConflictResolver",
            "SimpleSyncConflictResolver ctor: null local storage")}};
    }
}

QFuture<ISyncConflictResolver::NotebookConflictResolution>
    SimpleSyncConflictResolver::resolveNotebooksConflict(
        qevercloud::Notebook theirs, qevercloud::Notebook mine)
{
    QNDEBUG(
        "synchronization::SimpleSyncConflictResolver",
        "SimpleSyncConflictResolver::resolveNotebooksConflict: theirs: "
            << theirs << "\nMine: " << mine);

    using Result = ISyncConflictResolver::NotebookConflictResolution;

    if (Q_UNLIKELY(!theirs.guid())) {
        return utility::makeExceptionalFuture<Result>(
            InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
                "synchronization::SimpleSyncConflictResolver",
                "Cannot resolve notebook sync conflict: remote notebook "
                "has no guid")}});
    }

    if (Q_UNLIKELY(!theirs.name())) {
        return utility::makeExceptionalFuture<Result>(
            InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
                "synchronization::SimpleSyncConflictResolver",
                "Cannot resolve notebook sync conflict: remote notebook "
                "has no name")}});
    }

    if (Q_UNLIKELY(!mine.guid() && !mine.name())) {
        return utility::makeExceptionalFuture<Result>(
            InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
                "synchronization::SimpleSyncConflictResolver",
                "Cannot resolve notebook sync conflict: local notebook "
                "has neither name nor guid")}});
    }

    if (mine.name() && (*mine.name() == *theirs.name())) {
        return processNotebooksConflictByName(mine, theirs);
    }

    return processNotebooksConflictByGuid(mine, theirs);
}

QFuture<ISyncConflictResolver::NoteConflictResolution>
    SimpleSyncConflictResolver::resolveNoteConflict(
        qevercloud::Note theirs, qevercloud::Note mine)
{
    // TODO: implement
    return {};
}

QFuture<ISyncConflictResolver::SavedSearchConflictResolution>
    SimpleSyncConflictResolver::resolveSavedSearchConflict(
        qevercloud::SavedSearch theirs, qevercloud::SavedSearch mine)
{
    // TODO: implement
    return {};
}

QFuture<ISyncConflictResolver::TagConflictResolution>
    SimpleSyncConflictResolver::resolveTagConflict(
        qevercloud::Tag theirs, qevercloud::Tag mine)
{
    // TODO: implement
    return {};
}

QFuture<ISyncConflictResolver::NotebookConflictResolution>
    SimpleSyncConflictResolver::processNotebooksConflictByName(
        const qevercloud::Notebook & theirs, const qevercloud::Notebook & mine)
{
    if (mine.guid() && *mine.guid() == theirs.guid().value())
    {
        QNDEBUG(
            "synchronization::SimpleSyncConflictResolver",
            "Conflicting notebooks match by name and guid => taking the remote "
                << "version");
        return utility::makeReadyFuture<NotebookConflictResolution>(
            ConflictResolution::UseTheirs{});
    }

    QNDEBUG(
        "synchronization::SimpleSyncConflictResolver",
        "Conflicting notebooks match by name but not by guid");

    const auto & mineLinkedNotebookGuid = mine.linkedNotebookGuid();
    const auto & theirsLinkedNotebookGuid = theirs.linkedNotebookGuid();
    if (mineLinkedNotebookGuid != theirsLinkedNotebookGuid) {
        QNDEBUG(
            "synchronization::SimpleSyncConflictResolver",
            "Conflicting notebooks have the same name but their linked "
                << "notebook guids don't match => they are either from "
                << "different linked notebooks or one is from user's own "
                << "account while the other is from some linked notebook");
        return utility::makeReadyFuture<NotebookConflictResolution>(
            ConflictResolution::IgnoreMine{});
    }

    QNDEBUG(
        "synchronization::SimpleSyncConflictResolver",
        "Both conflicting notebooks are either from user's own account or from "
            << "the same linked notebook");

    // TODO: if got here, need to rename the local notebook
    // TODO: implement
    return {};
}

QFuture<ISyncConflictResolver::NotebookConflictResolution>
    SimpleSyncConflictResolver::processNotebooksConflictByGuid(
        const qevercloud::Notebook & theirs, const qevercloud::Notebook & mine)
{
    // TODO: implement
    return {};
}

} // namespace quentier::synchronization
