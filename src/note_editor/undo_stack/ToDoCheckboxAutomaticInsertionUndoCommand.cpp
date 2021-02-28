/*
 * Copyright 2017-2020 Dmitry Ivanov
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

#include "ToDoCheckboxAutomaticInsertionUndoCommand.h"

#include "../NoteEditor_p.h"

#include <quentier/logging/QuentierLogger.h>

namespace quentier {

#define GET_PAGE()                                                             \
    auto * page = qobject_cast<NoteEditorPage *>(m_noteEditorPrivate.page());  \
    if (Q_UNLIKELY(!page)) {                                                   \
        ErrorString error(QT_TRANSLATE_NOOP(                                   \
            "ToDoCheckboxAutomaticInsertionUndoCommand",                       \
            "Can't undo/redo the automatic insertion "                         \
            "of a TODO checkbox: no note editor page"));                       \
        QNWARNING("note_editor:undo", error);                                  \
        Q_EMIT notifyError(error);                                             \
        return;                                                                \
    }

ToDoCheckboxAutomaticInsertionUndoCommand::
    ToDoCheckboxAutomaticInsertionUndoCommand(
        NoteEditorPrivate & noteEditor, const Callback & callback,
        QUndoCommand * parent) :
    INoteEditorUndoCommand(noteEditor, parent),
    m_callback(callback)
{
    setText(tr("Insert ToDo checkbox automatically"));
}

ToDoCheckboxAutomaticInsertionUndoCommand::
    ToDoCheckboxAutomaticInsertionUndoCommand(
        NoteEditorPrivate & noteEditor, const Callback & callback,
        const QString & text, QUndoCommand * parent) :
    INoteEditorUndoCommand(noteEditor, text, parent),
    m_callback(callback)
{}

ToDoCheckboxAutomaticInsertionUndoCommand::
    ~ToDoCheckboxAutomaticInsertionUndoCommand()
{}

void ToDoCheckboxAutomaticInsertionUndoCommand::redoImpl()
{
    QNDEBUG(
        "note_editor:undo",
        "ToDoCheckboxAutomaticInsertionUndoCommand"
            << "::redoImpl");

    GET_PAGE()
    page->executeJavaScript(
        QStringLiteral("toDoCheckboxAutomaticInserter.redo()"), m_callback);
}

void ToDoCheckboxAutomaticInsertionUndoCommand::undoImpl()
{
    QNDEBUG(
        "note_editor:undo",
        "ToDoCheckboxAutomaticInsertionUndoCommand"
            << "::undoImpl");

    GET_PAGE()
    page->executeJavaScript(
        QStringLiteral("toDoCheckboxAutomaticInserter.undo()"), m_callback);
}

} // namespace quentier
