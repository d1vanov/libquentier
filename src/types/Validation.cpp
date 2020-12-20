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

#include <qevercloud/generated/Constants.h>
#include <qevercloud/generated/types/Notebook.h>
#include <qevercloud/generated/types/Tag.h>

#include <quentier/types/ErrorString.h>
#include <quentier/types/Validation.h>

#include <QString>

namespace quentier {

bool validateNoteTitle(
    const QString & noteTitle, ErrorString * errorDescription) noexcept
{
    if (noteTitle != noteTitle.trimmed()) {
        if (errorDescription) {
            errorDescription->setBase(QT_TRANSLATE_NOOP(
                "types:validation",
                "Note title cannot start or end with whitespace"));

            errorDescription->details() = noteTitle;
        }

        return false;
    }

    int len = noteTitle.length();
    if (len < qevercloud::EDAM_NOTE_TITLE_LEN_MIN) {
        if (errorDescription) {
            errorDescription->setBase(QT_TRANSLATE_NOOP(
                "types:validation", "Note title's length is too small"));

            errorDescription->details() = noteTitle;
        }

        return false;
    }
    else if (len > qevercloud::EDAM_NOTE_TITLE_LEN_MAX) {
        if (errorDescription) {
            errorDescription->setBase(QT_TRANSLATE_NOOP(
                "types:validation", "Note title's length is too large"));

            errorDescription->details() = noteTitle;
        }

        return false;
    }

    return true;
}

bool validateNotebookName(
    const QString & notebookName, ErrorString * errorDescription) noexcept
{
    if (notebookName != notebookName.trimmed()) {
        if (errorDescription) {
            errorDescription->setBase(QT_TRANSLATE_NOOP(
                "types:validation",
                "Notebook name cannot start or end with whitespace"));
        }

        return false;
    }

    const int len = notebookName.length();
    if (len < qevercloud::EDAM_NOTEBOOK_NAME_LEN_MIN) {
        if (errorDescription) {
            errorDescription->setBase(QT_TRANSLATE_NOOP(
                "types:validation",
                "Notebook name's length is too small"));

            errorDescription->details() = notebookName;
        }

        return false;
    }

    if (len > qevercloud::EDAM_NOTEBOOK_NAME_LEN_MAX) {
        if (errorDescription) {
            errorDescription->setBase(QT_TRANSLATE_NOOP(
                "types:validation",
                "Notebook name's length is too large"));

            errorDescription->details() = notebookName;
        }

        return false;
    }

    return true;
}

bool validateTagName(
    const QString & tagName, ErrorString * errorDescription) noexcept
{
    if (tagName != tagName.trimmed()) {
        if (errorDescription) {
            errorDescription->setBase(QT_TRANSLATE_NOOP(
                "types:validation",
                "Tag name cannot start or end with whitespace"));

            errorDescription->details() = tagName;
        }

        return false;
    }

    int len = tagName.length();
    if (len < qevercloud::EDAM_TAG_NAME_LEN_MIN) {
        if (errorDescription) {
            errorDescription->setBase(QT_TRANSLATE_NOOP(
                "types:validation", "Tag name's length is too small"));

            errorDescription->details() = tagName;
        }

        return false;
    }

    if (len > qevercloud::EDAM_TAG_NAME_LEN_MAX) {
        if (errorDescription) {
            errorDescription->setBase(QT_TRANSLATE_NOOP(
                "types:validation", "Tag name's length is too large"));

            errorDescription->details() = tagName;
        }

        return false;
    }

    if (tagName.contains(QChar::fromLatin1(','))) {
        if (errorDescription) {
            errorDescription->setBase(QT_TRANSLATE_NOOP(
                "types:validation", "Tag name cannot contain commas"));

            errorDescription->details() = tagName;
        }

        return false;
    }

    return true;
}

} // namespace quentier
