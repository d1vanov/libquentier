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

#include "NoteEditorContentEditUndoCommand.h"

#include "../NoteEditor_p.h"

#include <quentier/logging/QuentierLogger.h>

namespace quentier {

NoteEditorContentEditUndoCommand::NoteEditorContentEditUndoCommand(
    NoteEditorPrivate & noteEditorPrivate,
    QList<qevercloud::Resource> resources,
    QUndoCommand * parent) :
    INoteEditorUndoCommand(noteEditorPrivate, parent),
    m_resources(std::move(resources))
{
    init();
}

NoteEditorContentEditUndoCommand::NoteEditorContentEditUndoCommand(
    NoteEditorPrivate & noteEditorPrivate,
    QList<qevercloud::Resource> resources,
    const QString & text, QUndoCommand * parent) :
    INoteEditorUndoCommand(noteEditorPrivate, text, parent),
    m_resources(std::move(resources))
{
    init();
}

NoteEditorContentEditUndoCommand::~NoteEditorContentEditUndoCommand() noexcept =
    default;

void NoteEditorContentEditUndoCommand::redoImpl()
{
    QNDEBUG(
        "note_editor:undo",
        "NoteEditorContentEditUndoCommand::redoImpl (" << text() << ")");

    m_noteEditorPrivate.redoPageAction();
}

void NoteEditorContentEditUndoCommand::undoImpl()
{
    QNDEBUG(
        "note_editor:undo",
        "NoteEditorContentEditUndoCommand::undoImpl (" << text() << ")");

    m_noteEditorPrivate.undoPageAction();
    m_noteEditorPrivate.setNoteResources(m_resources);
}

void NoteEditorContentEditUndoCommand::init()
{
    setText(tr("Note text edit"));
}

} // namespace quentier
