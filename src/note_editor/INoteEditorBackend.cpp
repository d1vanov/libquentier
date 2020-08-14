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

#include <quentier/logging/QuentierLogger.h>
#include <quentier/note_editor/INoteEditorBackend.h>
#include <quentier/note_editor/NoteEditor.h>

#include <QUndoStack>

namespace quentier {

namespace {

////////////////////////////////////////////////////////////////////////////////

template <typename T>
T & printRotation(T & t, const INoteEditorBackend::Rotation rotation)
{
    switch (rotation) {
    case INoteEditorBackend::Rotation::Clockwise:
        t << "Clockwise";
        break;
    case INoteEditorBackend::Rotation::Counterclockwise:
        t << "Counterclockwise";
        break;
    default:
        t << "Unknown (" << static_cast<qint64>(rotation) << ")";
        break;
    }

    return t;
}

} // namespace

////////////////////////////////////////////////////////////////////////////////

INoteEditorBackend::~INoteEditorBackend() {}

INoteEditorBackend::INoteEditorBackend(NoteEditor * parent) :
    m_pNoteEditor(parent)
{}

QTextStream & operator<<(
    QTextStream & strm, const INoteEditorBackend::Rotation rotationDirection)
{
    return printRotation(strm, rotationDirection);
}

QDebug & operator<<(
    QDebug & dbg, const INoteEditorBackend::Rotation rotationDirection)
{
    return printRotation(dbg, rotationDirection);
}

} // namespace quentier
