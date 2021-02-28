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

#ifndef LIB_QUENTIER_NOTE_EDITOR_UNDO_STACK_SPELL_CHECK_ADD_TO_USER_WORDLIST_UNDO_COMMAND_H
#define LIB_QUENTIER_NOTE_EDITOR_UNDO_STACK_SPELL_CHECK_ADD_TO_USER_WORDLIST_UNDO_COMMAND_H

#include "INoteEditorUndoCommand.h"

#include <QPointer>

namespace quentier {

QT_FORWARD_DECLARE_CLASS(SpellChecker)

class Q_DECL_HIDDEN SpellCheckAddToUserWordListUndoCommand final :
    public INoteEditorUndoCommand
{
    Q_OBJECT
public:
    SpellCheckAddToUserWordListUndoCommand(
        NoteEditorPrivate & noteEditor, const QString & word,
        SpellChecker * pSpellChecker, QUndoCommand * parent = nullptr);

    SpellCheckAddToUserWordListUndoCommand(
        NoteEditorPrivate & noteEditor, const QString & word,
        SpellChecker * pSpellChecker, const QString & text,
        QUndoCommand * parent = nullptr);

    virtual ~SpellCheckAddToUserWordListUndoCommand();

    virtual void redoImpl() override;
    virtual void undoImpl() override;

private:
    QPointer<SpellChecker> m_pSpellChecker;
    QString m_word;
};

} // namespace quentier

#endif // LIB_QUENTIER_NOTE_EDITOR_UNDO_STACK_SPELL_CHECK_ADD_TO_USER_WORDLIST_UNDO_COMMAND_H
