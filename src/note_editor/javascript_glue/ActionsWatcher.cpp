/*
 * Copyright 2017-2022 Dmitry Ivanov
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

#include "ActionsWatcher.h"

#include <quentier/logging/QuentierLogger.h>

namespace quentier {

ActionsWatcher::ActionsWatcher(QObject * parent) : QObject(parent) {}

void ActionsWatcher::onCutActionToggled()
{
    QNDEBUG("note_editor:js_glue", "ActionsWatcher::onCutActionToggled");
    Q_EMIT cutActionToggled();
}

void ActionsWatcher::onPasteActionToggled()
{
    QNDEBUG("note_editor:js_glue", "ActionsWatcher::onPasteActionToggled");
    Q_EMIT pasteActionToggled();
}

void ActionsWatcher::onUndoActionToggled()
{
    QNDEBUG("note_editor:js_glue", "ActionsWatcher::onUndoActionToggled");
    Q_EMIT undoActionToggled();
}

void ActionsWatcher::onRedoActionToggled()
{
    QNDEBUG("note_editor:js_glue", "ActionsWatcher::onRedoActionToggled");
    Q_EMIT redoActionToggled();
}

} // namespace quentier
