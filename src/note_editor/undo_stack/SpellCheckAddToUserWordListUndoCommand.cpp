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

#include "SpellCheckAddToUserWordListUndoCommand.h"

#include "../NoteEditor_p.h"

#include <quentier/logging/QuentierLogger.h>
#include <quentier/note_editor/SpellChecker.h>

namespace quentier {

SpellCheckAddToUserWordListUndoCommand::SpellCheckAddToUserWordListUndoCommand(
    NoteEditorPrivate & noteEditor, const QString & word,
    SpellChecker * pSpellChecker, QUndoCommand * parent) :
    INoteEditorUndoCommand(noteEditor, parent),
    m_pSpellChecker(pSpellChecker), m_word(word)
{
    setText(tr("Add to user word list"));
}

SpellCheckAddToUserWordListUndoCommand::SpellCheckAddToUserWordListUndoCommand(
    NoteEditorPrivate & noteEditor, const QString & word,
    SpellChecker * pSpellChecker, const QString & text, QUndoCommand * parent) :
    INoteEditorUndoCommand(noteEditor, text, parent),
    m_pSpellChecker(pSpellChecker), m_word(word)
{}

SpellCheckAddToUserWordListUndoCommand::
    ~SpellCheckAddToUserWordListUndoCommand()
{}

void SpellCheckAddToUserWordListUndoCommand::redoImpl()
{
    QNDEBUG(
        "note_editor:undo",
        "SpellCheckAddToUserWordListUndoCommand"
            << "::redoImpl");

    if (Q_UNLIKELY(m_pSpellChecker.isNull())) {
        QNTRACE("note_editor:undo", "No spell checker");
        return;
    }

    m_pSpellChecker->addToUserWordlist(m_word);

    if (m_noteEditorPrivate.spellCheckEnabled()) {
        m_noteEditorPrivate.refreshMisSpelledWordsList();
        m_noteEditorPrivate.applySpellCheck();
    }
}

void SpellCheckAddToUserWordListUndoCommand::undoImpl()
{
    QNDEBUG(
        "note_editor:undo",
        "SpellCheckAddToUserWordListUndoCommand"
            << "::undoImpl");

    if (Q_UNLIKELY(m_pSpellChecker.isNull())) {
        QNTRACE("note_editor:undo", "No spell checker");
        return;
    }

    m_pSpellChecker->removeFromUserWordList(m_word);

    if (m_noteEditorPrivate.spellCheckEnabled()) {
        m_noteEditorPrivate.refreshMisSpelledWordsList();
        m_noteEditorPrivate.applySpellCheck();
    }
}

} // namespace quentier
