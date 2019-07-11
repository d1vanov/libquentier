/*
 * Copyright 2017 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_NOTE_EDITOR_JAVASCRIPT_GLUE_TODO_CHECKBOX_AUTOMATIC_INSERTION_HANDLER_H
#define LIB_QUENTIER_NOTE_EDITOR_JAVASCRIPT_GLUE_TODO_CHECKBOX_AUTOMATIC_INSERTION_HANDLER_H

#include <quentier/utility/Macros.h>

#include <QObject>

namespace quentier {

class ToDoCheckboxAutomaticInsertionHandler: public QObject
{
    Q_OBJECT
public:
    explicit ToDoCheckboxAutomaticInsertionHandler(QObject * parent = Q_NULLPTR);

Q_SIGNALS:
    void notifyToDoCheckboxInsertedAutomatically();

public Q_SLOTS:
    void onToDoCheckboxInsertedAutomatically();
};

} // namespace quentier

#endif // LIB_QUENTIER_NOTE_EDITOR_JAVASCRIPT_GLUE_TODO_CHECKBOX_AUTOMATIC_INSERTION_HANDLER_H
