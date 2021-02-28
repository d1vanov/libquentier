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

#include "ToDoCheckboxUndoCommand.h"

#include "../NoteEditor_p.h"

#include <quentier/logging/QuentierLogger.h>

namespace quentier {

ToDoCheckboxUndoCommand::ToDoCheckboxUndoCommand(
    const quint64 enToDoCheckboxId, NoteEditorPrivate & noteEditorPrivate,
    QUndoCommand * parent) :
    INoteEditorUndoCommand(noteEditorPrivate, parent),
    m_enToDoCheckboxId(enToDoCheckboxId)
{
    setText(tr("Change ToDo state"));
}

ToDoCheckboxUndoCommand::ToDoCheckboxUndoCommand(
    const quint64 enToDoCheckboxId, NoteEditorPrivate & noteEditorPrivate,
    const QString & text, QUndoCommand * parent) :
    INoteEditorUndoCommand(noteEditorPrivate, text, parent),
    m_enToDoCheckboxId(enToDoCheckboxId)
{}

ToDoCheckboxUndoCommand::~ToDoCheckboxUndoCommand() {}

void ToDoCheckboxUndoCommand::redoImpl()
{
    QNDEBUG("note_editor:undo", "ToDoCheckboxUndoCommand::redoImpl");
    m_noteEditorPrivate.flipEnToDoCheckboxState(m_enToDoCheckboxId);
}

void ToDoCheckboxUndoCommand::undoImpl()
{
    QNDEBUG("note_editor:undo", "ToDoCheckboxUndoCommand::undoImpl");
    m_noteEditorPrivate.flipEnToDoCheckboxState(m_enToDoCheckboxId);
}

} // namespace quentier
