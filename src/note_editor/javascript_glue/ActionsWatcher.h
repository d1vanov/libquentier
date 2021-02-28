/*
 * Copyright 2017-2019 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_NOTE_EDITOR_JAVASCRIPT_GLUE_ACTIONS_WATCHER_H
#define LIB_QUENTIER_NOTE_EDITOR_JAVASCRIPT_GLUE_ACTIONS_WATCHER_H

#include <QObject>

namespace quentier {

/**
 * @brief The ActionsWatcher class is a small class object of which is exposed
 * to JavaScript in order to notify the C++ code of certain events, including
 * 'cut' and 'paste' actions
 */
class ActionsWatcher final : public QObject
{
    Q_OBJECT
public:
    explicit ActionsWatcher(QObject * parent = nullptr);

Q_SIGNALS:
    void cutActionToggled();
    void pasteActionToggled();

public Q_SLOTS:
    void onCutActionToggled();
    void onPasteActionToggled();
};

} // namespace quentier

#endif // LIB_QUENTIER_NOTE_EDITOR_JAVASCRIPT_GLUE_ACTIONS_WATCHER_H
