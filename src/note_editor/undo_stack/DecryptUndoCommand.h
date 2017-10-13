/*
 * Copyright 2016 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_NOTE_EDITOR_UNDO_STACK_DECRYPT_UNDO_COMMAND_H
#define LIB_QUENTIER_NOTE_EDITOR_UNDO_STACK_DECRYPT_UNDO_COMMAND_H

#include "EncryptDecryptUndoCommandInfo.h"
#include "INoteEditorUndoCommand.h"
#include "../NoteEditorPage.h"
#include <quentier/enml/DecryptedTextManager.h>
#include <QSharedPointer>

namespace quentier {

class Q_DECL_HIDDEN DecryptUndoCommand: public INoteEditorUndoCommand
{
    Q_OBJECT
    typedef NoteEditorPage::Callback Callback;
public:
    DecryptUndoCommand(const EncryptDecryptUndoCommandInfo & info, const QSharedPointer<DecryptedTextManager> & decryptedTextManager,
                       NoteEditorPrivate & noteEditorPrivate, const Callback & callback, QUndoCommand * parent = Q_NULLPTR);
    DecryptUndoCommand(const EncryptDecryptUndoCommandInfo & info, const QSharedPointer<DecryptedTextManager> & decryptedTextManager,
                       NoteEditorPrivate & noteEditorPrivate, const Callback & callback, const QString & text, QUndoCommand * parent = Q_NULLPTR);
    virtual ~DecryptUndoCommand();

    virtual void redoImpl() Q_DECL_OVERRIDE;
    virtual void undoImpl() Q_DECL_OVERRIDE;

private:
    EncryptDecryptUndoCommandInfo           m_info;
    QSharedPointer<DecryptedTextManager>    m_decryptedTextManager;
    Callback                                m_callback;
};

} // namespace quentier

#endif // LIB_QUENTIER_NOTE_EDITOR_UNDO_STACK_DECRYPT_UNDO_COMMAND_H
