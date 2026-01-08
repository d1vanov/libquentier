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

#include <qevercloud/exceptions/EDAMUserException.h>
#include <qevercloud/types/Fwd.h>

#include <optional>

namespace quentier::synchronization::tests::note_store {

[[nodiscard]] std::optional<qevercloud::EDAMUserException> checkNotebook(
    const qevercloud::Notebook & notebook);

[[nodiscard]] std::optional<qevercloud::EDAMUserException> checkNote(
    const qevercloud::Note & note, quint32 maxNumResourcesPerNote,
    quint32 maxTagsPerNote);

[[nodiscard]] std::optional<qevercloud::EDAMUserException> checkTag(
    const qevercloud::Tag & tag);

[[nodiscard]] std::optional<qevercloud::EDAMUserException> checkSavedSearch(
    const qevercloud::SavedSearch & savedSearch);

} // namespace quentier::synchronization::tests::note_store
