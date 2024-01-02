/*
 * Copyright 2016-2024 Dmitry Ivanov
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

#pragma once

#include <quentier/utility/QuentierUndoCommand.h>

namespace quentier {

class NoteEditorPrivate;

class INoteEditorUndoCommand : public QuentierUndoCommand
{
    Q_OBJECT
public:
    ~INoteEditorUndoCommand() override;

protected:
    INoteEditorUndoCommand(
        NoteEditorPrivate & noteEditorPrivate, QUndoCommand * parent = nullptr);

    INoteEditorUndoCommand(
        NoteEditorPrivate & noteEditorPrivate, const QString & text,
        QUndoCommand * parent = nullptr);

    NoteEditorPrivate & m_noteEditorPrivate;
};

} // namespace quentier
