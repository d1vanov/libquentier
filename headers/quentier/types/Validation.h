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

#ifndef LIB_QUENTIER_TYPES_VALIDATIONS_H
#define LIB_QUENTIER_TYPES_VALIDATIONS_H

#include <quentier/utility/Linkage.h>

class QString;

namespace quentier {

class ErrorString;

/**
 * @brief checks note title for validity from Evernote service's rules POV
 *
 * @param noteTitle             Note title to validate
 * @param errorDescription      Pointer to error string to hold output error
 *                              description in case the note title is invalid.
 *                              If nullptr, error description is not propagated
 *                              to the caller of the function
 * @return true if note title is valid, false otherwise
 */
[[nodiscard]] QUENTIER_EXPORT bool validateNoteTitle(
    const QString & noteTitle,
    ErrorString * errorDescription = nullptr) noexcept;

/**
 * @brief checks notebook name for validity from Evernote service's rules POV
 *
 * @param notebookName          Notebook name to validate
 * @param errorDescription      Pointer to error string to hold output error
 *                              decription in case the notebook name is invalid.
 *                              If nullptr, error description is not propagated
 *                              to the caller of the function
 * @return true if notebook name is valid, false otherwise
 */
[[nodiscard]] QUENTIER_EXPORT bool validateNotebookName(
    const QString & notebookName,
    ErrorString * errorDescription = nullptr) noexcept;

/**
 * @brief checks tag name for validity from Evernote service's rules POV
 *
 * @param tagName               Tag name to validate
 * @param errorDescription      Pointer to error string to hold output error
 *                              description in case the tag name is invalid.
 *                              If nullptr, error description is not propagated
 *                              to the caller of the function
 * @return true if tag name is valid, false otherwise
 */
[[nodiscard]] QUENTIER_EXPORT bool validateTagName(
    const QString & tagName, ErrorString * errorDescription = nullptr) noexcept;

} // namespace quentier

#endif // LIB_QUENTIER_TYPES_VALIDATIONS_H
