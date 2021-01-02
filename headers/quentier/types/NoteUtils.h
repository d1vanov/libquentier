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

#ifndef LIB_QUENTIER_TYPES_NOTE_UTILS_H
#define LIB_QUENTIER_TYPES_NOTE_UTILS_H

#include <quentier/utility/Linkage.h>

#include <QStringList>

#include <utility>

namespace qevercloud {

class Note;

} // namespace qevercloud

namespace quentier {

class ErrorString;

[[nodiscard]] QUENTIER_EXPORT bool noteContentContainsCheckedToDo(
    const QString & noteContent);

[[nodiscard]] QUENTIER_EXPORT bool noteContentContainsUncheckedToDo(
    const QString & noteContent);

[[nodiscard]] QUENTIER_EXPORT bool noteContentContainsToDo(
    const QString & noteContent);

[[nodiscard]] QUENTIER_EXPORT bool noteContentContainsEncryptedFragments(
    const QString & noteContent);

[[nodiscard]] QUENTIER_EXPORT QString noteContentToPlainText(
    const QString & noteContent, ErrorString * errorDescription = nullptr);

[[nodiscard]] QUENTIER_EXPORT QStringList noteContentToListOfWords(
    const QString & noteContent, ErrorString * errorDescription = nullptr);

[[nodiscard]] QUENTIER_EXPORT std::pair<QString, QStringList>
noteContentToPlainTextAndListOfWords(
    const QString & noteContent, ErrorString * errorDescription = nullptr);

[[nodiscard]] QUENTIER_EXPORT int noteResourceCount(
    const qevercloud::Note & note);

} // namespace quentier

#endif // LIB_QUENTIER_TYPES_NOTE_UTILS_H
