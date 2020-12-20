/*
 * Copyright 2020 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_LOCAL_STORAGE_TYPE_CHECKS_H
#define LIB_QUENTIER_LOCAL_STORAGE_TYPE_CHECKS_H

namespace qevercloud {

class LinkedNotebook;
class Note;
class Notebook;
class SavedSearch;
class Tag;
class User;

} // namespace qevercloud

namespace quentier {

class ErrorString;

[[nodiscard]] bool checkLinkedNotebook(
    const qevercloud::LinkedNotebook & linkedNotebook,
    ErrorString & errorDescription) noexcept;

[[nodiscard]] bool checkNote(
    const qevercloud::Note & note, ErrorString & errorDescription) noexcept;

[[nodiscard]] bool checkNotebook(
    const qevercloud::Notebook & notebook,
    ErrorString & errorDescription) noexcept;

[[nodiscard]] bool checkSavedSearch(
    const qevercloud::SavedSearch & savedSearch,
    ErrorString & errorDescription) noexcept;

[[nodiscard]] bool checkTag(
    const qevercloud::Tag & tag, ErrorString & errorDescription) noexcept;

[[nodiscard]] bool checkUser(
    const qevercloud::User & user, ErrorString & errorDescription) noexcept;

} // namespace quentier

#endif // LIB_QUENTIER_LOCAL_STORAGE_TYPE_CHECKS_H
