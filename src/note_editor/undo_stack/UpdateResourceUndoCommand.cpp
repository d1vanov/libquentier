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

#include "UpdateResourceUndoCommand.h"

#include "../NoteEditor_p.h"

#include <quentier/logging/QuentierLogger.h>

namespace quentier {

UpdateResourceUndoCommand::UpdateResourceUndoCommand(
    const Resource & resourceBefore, const Resource & resourceAfter,
    NoteEditorPrivate & noteEditorPrivate, QUndoCommand * parent) :
    INoteEditorUndoCommand(noteEditorPrivate, parent),
    m_resourceBefore(resourceBefore), m_resourceAfter(resourceAfter)
{
    init();
}

UpdateResourceUndoCommand::UpdateResourceUndoCommand(
    const Resource & resourceBefore, const Resource & resourceAfter,
    NoteEditorPrivate & noteEditorPrivate, const QString & text,
    QUndoCommand * parent) :
    INoteEditorUndoCommand(noteEditorPrivate, text, parent),
    m_resourceBefore(resourceBefore), m_resourceAfter(resourceAfter)
{
    init();
}

UpdateResourceUndoCommand::~UpdateResourceUndoCommand() {}

void UpdateResourceUndoCommand::undoImpl()
{
    QNDEBUG("note_editor:undo", "UpdateResourceUndoCommand::undoImpl");

    m_noteEditorPrivate.replaceResourceInNote(m_resourceBefore);
    m_noteEditorPrivate.updateFromNote();
}

void UpdateResourceUndoCommand::redoImpl()
{
    QNDEBUG("note_editor:undo", "UpdateResourceUndoCommand::redoImpl");

    m_noteEditorPrivate.replaceResourceInNote(m_resourceAfter);
    m_noteEditorPrivate.updateFromNote();
}

void UpdateResourceUndoCommand::init()
{
    setText(tr("Edit attachment"));
}

} // namespace quentier
