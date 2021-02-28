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

#include "EncryptUndoCommand.h"

#include "../NoteEditor_p.h"

#include <quentier/logging/QuentierLogger.h>

namespace quentier {

#define GET_PAGE()                                                             \
    auto * page = qobject_cast<NoteEditorPage *>(m_noteEditorPrivate.page());  \
    if (Q_UNLIKELY(!page)) {                                                   \
        ErrorString error(QT_TRANSLATE_NOOP(                                   \
            "EncryptUndoCommand",                                              \
            "Can't undo/redo the text encryption: "                            \
            "can't get note editor page"));                                    \
        QNWARNING("note_editor:undo", error);                                  \
        Q_EMIT notifyError(error);                                             \
        return;                                                                \
    }

EncryptUndoCommand::EncryptUndoCommand(
    NoteEditorPrivate & noteEditorPrivate, const Callback & callback,
    QUndoCommand * parent) :
    INoteEditorUndoCommand(noteEditorPrivate, parent),
    m_callback(callback)
{
    setText(tr("Encrypt selected text"));
}

EncryptUndoCommand::EncryptUndoCommand(
    NoteEditorPrivate & noteEditorPrivate, const Callback & callback,
    const QString & text, QUndoCommand * parent) :
    INoteEditorUndoCommand(noteEditorPrivate, text, parent),
    m_callback(callback)
{}

EncryptUndoCommand::~EncryptUndoCommand() {}

void EncryptUndoCommand::redoImpl()
{
    QNDEBUG("note_editor:undo", "EncryptUndoCommand::redoImpl");

    GET_PAGE()
    page->executeJavaScript(
        QStringLiteral("encryptDecryptManager.redo();"), m_callback);
}

void EncryptUndoCommand::undoImpl()
{
    QNDEBUG("note_editor:undo", "EncryptUndoCommand::undoImpl");

    GET_PAGE()
    page->executeJavaScript(
        QStringLiteral("encryptDecryptManager.undo();"), m_callback);
}

} // namespace quentier
