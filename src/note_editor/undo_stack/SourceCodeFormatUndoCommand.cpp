#include "SourceCodeFormatUndoCommand.h"
#include "../NoteEditor_p.h"
#include <quentier/logging/QuentierLogger.h>

namespace quentier {

#define GET_PAGE() \
    NoteEditorPage * page = qobject_cast<NoteEditorPage*>(m_noteEditorPrivate.page()); \
    if (Q_UNLIKELY(!page)) { \
        ErrorString error(QT_TR_NOOP("Can't undo/redo source code formatting: no note editor page")); \
        QNWARNING(error); \
        emit notifyError(error); \
        return; \
    }

SourceCodeFormatUndoCommand::SourceCodeFormatUndoCommand(NoteEditorPrivate & noteEditor, const Callback & callback, QUndoCommand * parent) :
    INoteEditorUndoCommand(noteEditor, parent),
    m_callback(callback)
{
    setText(tr("Format as source code"));
}

SourceCodeFormatUndoCommand::SourceCodeFormatUndoCommand(NoteEditorPrivate & noteEditor, const Callback & callback, const QString & text, QUndoCommand * parent) :
    INoteEditorUndoCommand(noteEditor, text, parent),
    m_callback(callback)
{}

SourceCodeFormatUndoCommand::~SourceCodeFormatUndoCommand()
{}

void SourceCodeFormatUndoCommand::redoImpl()
{
    QNDEBUG(QStringLiteral("SourceCodeFormatUndoCommand::redoImpl"));

    GET_PAGE()
    page->executeJavaScript(QStringLiteral("sourceCodeFormatter.redo();"), m_callback);
}

void SourceCodeFormatUndoCommand::undoImpl()
{
    QNDEBUG(QStringLiteral("SourceCodeFormatUndoCommand::undoImpl"));

    GET_PAGE()
    page->executeJavaScript(QStringLiteral("sourceCodeFormatter.undo();"), m_callback);
}

} // namespace quentier
