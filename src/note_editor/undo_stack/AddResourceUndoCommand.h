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

#ifndef LIB_QUENTIER_NOTE_EDITOR_UNDO_STACK_ADD_RESOURCE_UNDO_COMMAND_H
#define LIB_QUENTIER_NOTE_EDITOR_UNDO_STACK_ADD_RESOURCE_UNDO_COMMAND_H

#include "INoteEditorUndoCommand.h"

#include "../NoteEditorPage.h"

#include <quentier/types/Resource.h>

namespace quentier {

class Q_DECL_HIDDEN AddResourceUndoCommand final : public INoteEditorUndoCommand
{
    Q_OBJECT
public:
    using Callback = NoteEditorPage::Callback;

public:
    AddResourceUndoCommand(
        const Resource & resource, const Callback & callback,
        NoteEditorPrivate & noteEditorPrivate, QUndoCommand * parent = nullptr);

    AddResourceUndoCommand(
        const Resource & resource, const Callback & callback,
        NoteEditorPrivate & noteEditorPrivate, const QString & text,
        QUndoCommand * parent = nullptr);

    virtual ~AddResourceUndoCommand();

    virtual void undoImpl() override;
    virtual void redoImpl() override;

private:
    void init();

private:
    Resource m_resource;
    Callback m_callback;
};

} // namespace quentier

#endif // LIB_QUENTIER_NOTE_EDITOR_UNDO_STACK_ADD_RESOURCE_UNDO_COMMAND_H
