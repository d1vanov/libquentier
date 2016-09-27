#ifndef LIB_QUENTIER_NOTE_EDITOR_UNDO_STACK_SPELL_CHECK_ADD_TO_USER_WORDLIST_UNDO_COMMAND_H
#define LIB_QUENTIER_NOTE_EDITOR_UNDO_STACK_SPELL_CHECK_ADD_TO_USER_WORDLIST_UNDO_COMMAND_H

#include "INoteEditorUndoCommand.h"
#include <QPointer>

namespace quentier {

QT_FORWARD_DECLARE_CLASS(SpellChecker)

class SpellCheckAddToUserWordListUndoCommand: public INoteEditorUndoCommand
{
    Q_OBJECT
public:
    SpellCheckAddToUserWordListUndoCommand(NoteEditorPrivate & noteEditor, const QString & word,
                                           SpellChecker * pSpellChecker, QUndoCommand * parent = Q_NULLPTR);
    SpellCheckAddToUserWordListUndoCommand(NoteEditorPrivate & noteEditor, const QString & word,
                                           SpellChecker * pSpellChecker, const QString & text,
                                           QUndoCommand * parent = Q_NULLPTR);
    virtual ~SpellCheckAddToUserWordListUndoCommand();

    virtual void redoImpl() Q_DECL_OVERRIDE;
    virtual void undoImpl() Q_DECL_OVERRIDE;

private:
    QPointer<SpellChecker>  m_pSpellChecker;
    QString                 m_word;
};

} // namespace quentier

#endif // LIB_QUENTIER_NOTE_EDITOR_UNDO_STACK_SPELL_CHECK_ADD_TO_USER_WORDLIST_UNDO_COMMAND_H
