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

#include "AddResourceUndoCommand.h"

#include "../NoteEditor_p.h"

#include <quentier/logging/QuentierLogger.h>

namespace quentier {

#define GET_PAGE()                                                             \
    auto * page = qobject_cast<NoteEditorPage *>(m_noteEditorPrivate.page());  \
    if (Q_UNLIKELY(!page)) {                                                   \
        ErrorString error(QT_TRANSLATE_NOOP(                                   \
            "AddResourceUndoCommand",                                          \
            "Can't undo/redo adding the attachment: "                          \
            "no note editor page"));                                           \
        QNWARNING("note_editor:undo", error);                                  \
        Q_EMIT notifyError(error);                                             \
        return;                                                                \
    }

AddResourceUndoCommand::AddResourceUndoCommand(
    const Resource & resource, const Callback & callback,
    NoteEditorPrivate & noteEditorPrivate, QUndoCommand * parent) :
    INoteEditorUndoCommand(noteEditorPrivate, parent),
    m_resource(resource), m_callback(callback)
{
    setText(tr("Add attachment"));
}

AddResourceUndoCommand::AddResourceUndoCommand(
    const Resource & resource, const Callback & callback,
    NoteEditorPrivate & noteEditorPrivate, const QString & text,
    QUndoCommand * parent) :
    INoteEditorUndoCommand(noteEditorPrivate, text, parent),
    m_resource(resource), m_callback(callback)
{}

AddResourceUndoCommand::~AddResourceUndoCommand() {}

void AddResourceUndoCommand::undoImpl()
{
    QNDEBUG("note_editor:undo", "AddResourceUndoCommand::undoImpl");

    m_noteEditorPrivate.removeResourceFromNote(m_resource);

    GET_PAGE()
    page->executeJavaScript(
        QStringLiteral("resourceManager.undo();"), m_callback);
}

void AddResourceUndoCommand::redoImpl()
{
    QNDEBUG("note_editor:undo", "AddResourceUndoCommand::redoImpl");

    m_noteEditorPrivate.addResourceToNote(m_resource);

    GET_PAGE()
    page->executeJavaScript(
        QStringLiteral("resourceManager.redo();"), m_callback);
}

} // namespace quentier
