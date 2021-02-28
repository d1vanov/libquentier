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
#include <quentier/utility/QuentierUndoCommand.h>

namespace quentier {

QuentierUndoCommand::QuentierUndoCommand(QUndoCommand * parent) :
    QObject(nullptr), QUndoCommand(parent), m_onceUndoExecuted(false)
{}

QuentierUndoCommand::QuentierUndoCommand(
    const QString & text, QUndoCommand * parent) :
    QObject(nullptr),
    QUndoCommand(text, parent), m_onceUndoExecuted(false)
{}

QuentierUndoCommand::~QuentierUndoCommand() {}

void QuentierUndoCommand::undo()
{
    QNTRACE("utility:undo", "QuentierUndoCommand::undo");
    m_onceUndoExecuted = true;
    undoImpl();
}

void QuentierUndoCommand::redo()
{
    QNTRACE("utility:undo", "QuentierUndoCommand::redo");

    if (Q_UNLIKELY(!m_onceUndoExecuted)) {
        QNTRACE(
            "utility:undo",
            "Ignoring the attempt to execute redo for "
                << "command " << text() << " as there was no previous undo");
        return;
    }

    redoImpl();
}

} // namespace quentier
