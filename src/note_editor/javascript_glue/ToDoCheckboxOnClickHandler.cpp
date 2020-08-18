/*
 * Copyright 2016-2020 Dmitry Ivanov
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

#include "ToDoCheckboxOnClickHandler.h"

#include <quentier/logging/QuentierLogger.h>

namespace quentier {

ToDoCheckboxOnClickHandler::ToDoCheckboxOnClickHandler(QObject * parent) :
    QObject(parent)
{}

void ToDoCheckboxOnClickHandler::onToDoCheckboxClicked(QString enToDoCheckboxId)
{
    QNDEBUG(
        "note_editor:js_glue",
        "ToDoCheckboxOnClickHandler"
            << "::onToDoCheckboxClicked: " << enToDoCheckboxId);

    bool conversionResult = false;
    quint64 id = enToDoCheckboxId.toULongLong(&conversionResult);
    if (Q_UNLIKELY(!conversionResult)) {
        ErrorString error(
            QT_TR_NOOP("Error handling todo checkbox click event: "
                       "can't convert id from string to number"));
        QNWARNING("note_editor:js_glue", error);
        Q_EMIT notifyError(error);
        return;
    }

    Q_EMIT toDoCheckboxClicked(id);
}

} // namespace quentier
