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

#include "ReplaceAllUndoCommand.h"

#include "../NoteEditor_p.h"

#include <quentier/logging/QuentierLogger.h>

namespace quentier {

#define GET_PAGE()                                                             \
    auto * page = qobject_cast<NoteEditorPage *>(m_noteEditorPrivate.page());  \
    if (Q_UNLIKELY(!page)) {                                                   \
        ErrorString error(QT_TRANSLATE_NOOP(                                   \
            "ReplaceAllUndoCommand",                                           \
            "Can't undo/redo text replacement: "                               \
            "can't get note editor page"));                                    \
        QNWARNING("note_editor:undo", error);                                  \
        Q_EMIT notifyError(error);                                             \
        return;                                                                \
    }

ReplaceAllUndoCommand::ReplaceAllUndoCommand(
    const QString & textToReplace, const bool matchCase,
    NoteEditorPrivate & noteEditorPrivate, Callback callback,
    QUndoCommand * parent) :
    INoteEditorUndoCommand(noteEditorPrivate, parent),
    m_textToReplace(textToReplace), m_matchCase(matchCase), m_callback(callback)
{
    setText(tr("Replace all"));
}

ReplaceAllUndoCommand::ReplaceAllUndoCommand(
    const QString & textToReplace, const bool matchCase,
    NoteEditorPrivate & noteEditorPrivate, const QString & text,
    Callback callback, QUndoCommand * parent) :
    INoteEditorUndoCommand(noteEditorPrivate, text, parent),
    m_textToReplace(textToReplace), m_matchCase(matchCase), m_callback(callback)
{}

ReplaceAllUndoCommand::~ReplaceAllUndoCommand() {}

void ReplaceAllUndoCommand::redoImpl()
{
    QNDEBUG("note_editor:undo", "ReplaceAllUndoCommand::redoImpl");

    GET_PAGE()

    QString javascript = QStringLiteral("findReplaceManager.redoReplaceAll();");
    page->executeJavaScript(javascript, m_callback);

    if (m_noteEditorPrivate.searchHighlightEnabled()) {
        m_noteEditorPrivate.setSearchHighlight(
            m_textToReplace, m_matchCase,
            /* force = */ true);
    }
}

void ReplaceAllUndoCommand::undoImpl()
{
    QNDEBUG("note_editor:undo", "ReplaceAllUndoCommand::undoImpl");

    GET_PAGE()

    QString javascript = QStringLiteral("findReplaceManager.undoReplaceAll();");
    page->executeJavaScript(javascript, m_callback);

    if (m_noteEditorPrivate.searchHighlightEnabled()) {
        m_noteEditorPrivate.setSearchHighlight(
            m_textToReplace, m_matchCase,
            /* force = */ true);
    }
}

} // namespace quentier
