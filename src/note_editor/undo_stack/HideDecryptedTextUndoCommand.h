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

#ifndef LIB_QUENTIER_NOTE_EDITOR_UNDO_STACK_HIDE_DECRYPTED_TEXT_UNDO_COMMAND
#define LIB_QUENTIER_NOTE_EDITOR_UNDO_STACK_HIDE_DECRYPTED_TEXT_UNDO_COMMAND

#include "INoteEditorUndoCommand.h"

#include "../NoteEditorPage.h"

namespace quentier {

class Q_DECL_HIDDEN HideDecryptedTextUndoCommand final :
    public INoteEditorUndoCommand
{
    Q_OBJECT
public:
    using Callback = NoteEditorPage::Callback;

public:
    HideDecryptedTextUndoCommand(
        NoteEditorPrivate & noteEditorPrivate, const Callback & callback,
        QUndoCommand * parent = nullptr);

    HideDecryptedTextUndoCommand(
        NoteEditorPrivate & noteEditorPrivate, const Callback & callback,
        const QString & text, QUndoCommand * parent = nullptr);

    virtual ~HideDecryptedTextUndoCommand();

    virtual void redoImpl() override;
    virtual void undoImpl() override;

private:
    Callback m_callback;
};

} // namespace quentier

#endif // LIB_QUENTIER_NOTE_EDITOR_UNDO_STACK_HIDE_DECRYPTED_TEXT_UNDO_COMMAND
