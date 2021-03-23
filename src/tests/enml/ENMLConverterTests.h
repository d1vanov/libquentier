/*
 * Copyright 2016-2021 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_TESTS_ENML_CONVERTER_TESTS_H
#define LIB_QUENTIER_TESTS_ENML_CONVERTER_TESTS_H

#include <QString>

namespace quentier::test {

[[nodiscard]] bool convertSimpleNoteToHtmlAndBack(QString & error);
[[nodiscard]] bool convertNoteWithToDoTagsToHtmlAndBack(QString & error);
[[nodiscard]] bool convertNoteWithEncryptionToHtmlAndBack(QString & error);
[[nodiscard]] bool convertNoteWithResourcesToHtmlAndBack(QString & error);
[[nodiscard]] bool convertComplexNoteToHtmlAndBack(QString & error);
[[nodiscard]] bool convertComplexNote2ToHtmlAndBack(QString & error);
[[nodiscard]] bool convertComplexNote3ToHtmlAndBack(QString & error);
[[nodiscard]] bool convertComplexNote4ToHtmlAndBack(QString & error);
[[nodiscard]] bool convertHtmlWithModifiedDecryptedTextToEnml(QString & error);
[[nodiscard]] bool convertHtmlWithTableHelperTagsToEnml(QString & error);

[[nodiscard]] bool convertHtmlWithTableAndHilitorHelperTagsToEnml(
    QString & error);

} // namespace quentier::test

#endif // LIB_QUENTIER_TESTS_ENML_CONVERTER_TESTS_H
