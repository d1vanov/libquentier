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

#include "SpellCheckIgnoreWordUndoCommand.h"

#include "../NoteEditor_p.h"

#include <quentier/logging/QuentierLogger.h>
#include <quentier/note_editor/SpellChecker.h>

namespace quentier {

SpellCheckIgnoreWordUndoCommand::SpellCheckIgnoreWordUndoCommand(
    NoteEditorPrivate & noteEditor, const QString & ignoredWord,
    SpellChecker * pSpellChecker, QUndoCommand * parent) :
    INoteEditorUndoCommand(noteEditor, parent),
    m_pSpellChecker(pSpellChecker), m_ignoredWord(ignoredWord)
{
    setText(tr("Ignore word"));
}

SpellCheckIgnoreWordUndoCommand::SpellCheckIgnoreWordUndoCommand(
    NoteEditorPrivate & noteEditor, const QString & ignoredWord,
    SpellChecker * pSpellChecker, const QString & text, QUndoCommand * parent) :
    INoteEditorUndoCommand(noteEditor, text, parent),
    m_pSpellChecker(pSpellChecker), m_ignoredWord(ignoredWord)
{}

SpellCheckIgnoreWordUndoCommand::~SpellCheckIgnoreWordUndoCommand() {}

void SpellCheckIgnoreWordUndoCommand::redoImpl()
{
    QNDEBUG("note_editor:undo", "SpellCheckIgnoreWordUndoCommand::redoImpl");

    if (Q_UNLIKELY(m_pSpellChecker.isNull())) {
        QNTRACE("note_editor:undo", "No spell checker");
        return;
    }

    m_pSpellChecker->ignoreWord(m_ignoredWord);

    if (m_noteEditorPrivate.spellCheckEnabled()) {
        m_noteEditorPrivate.refreshMisSpelledWordsList();
        m_noteEditorPrivate.applySpellCheck();
    }
}

void SpellCheckIgnoreWordUndoCommand::undoImpl()
{
    QNDEBUG("note_editor:undo", "SpellCheckIgnoreWordUndoCommand::undoImpl");

    if (Q_UNLIKELY(m_pSpellChecker.isNull())) {
        QNTRACE("note_editor:undo", "No spell checker");
        return;
    }

    m_pSpellChecker->removeWord(m_ignoredWord);

    if (m_noteEditorPrivate.spellCheckEnabled()) {
        m_noteEditorPrivate.refreshMisSpelledWordsList();
        m_noteEditorPrivate.applySpellCheck();
    }
}

} // namespace quentier
