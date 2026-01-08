/*
 * Copyright 2022 Dmitry Ivanov
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

#include "Utils.h"

#include <quentier/types/NoteUtils.h>

#include <qevercloud/types/Note.h>

#include <QCoreApplication>

namespace quentier::synchronization::utils {

QString makeLocalConflictingNoteTitle(const qevercloud::Note & conflictingNote)
{
    if (conflictingNote.title()) {
        return *conflictingNote.title() + QStringLiteral(" - ") +
            QCoreApplication::translate(
                "synchronization::utils", "conflicting");
    }

    QString previewText =
        (conflictingNote.content()
             ? noteContentToPlainText(*conflictingNote.content())
             : QString{});

    if (!previewText.isEmpty()) {
        previewText.truncate(12);
        return previewText + QStringLiteral("... - ") +
            QCoreApplication::translate(
                   "synchronization::utils", "conflicting");
    }

    return QCoreApplication::translate(
        "synchronization::utils", "Conflicting note");
}

} // namespace quentier::synchronization::utils
