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

#ifndef LIB_QUENTIER_NOTE_EDITOR_UNDO_STACK_I_NOTE_EDITOR_UNDO_COMMAND_H
#define LIB_QUENTIER_NOTE_EDITOR_UNDO_STACK_I_NOTE_EDITOR_UNDO_COMMAND_H

#include <quentier/utility/QuentierUndoCommand.h>

namespace quentier {

QT_FORWARD_DECLARE_CLASS(NoteEditorPrivate)

class Q_DECL_HIDDEN INoteEditorUndoCommand : public QuentierUndoCommand
{
    Q_OBJECT
public:
    virtual ~INoteEditorUndoCommand();

protected:
    INoteEditorUndoCommand(
        NoteEditorPrivate & noteEditorPrivate, QUndoCommand * parent = nullptr);

    INoteEditorUndoCommand(
        NoteEditorPrivate & noteEditorPrivate, const QString & text,
        QUndoCommand * parent = nullptr);

    NoteEditorPrivate & m_noteEditorPrivate;
};

} // namespace quentier

#endif // LIB_QUENTIER_NOTE_EDITOR_UNDO_STACK_I_NOTE_EDITOR_UNDO_COMMAND_H
