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

#ifndef LIB_QUENTIER_LOCAL_STORAGE_LISTS_H
#define LIB_QUENTIER_LOCAL_STORAGE_LISTS_H

#include <QVector>

namespace quentier {

QT_FORWARD_DECLARE_CLASS(LinkedNotebook)
QT_FORWARD_DECLARE_CLASS(Note)
QT_FORWARD_DECLARE_CLASS(Notebook)
QT_FORWARD_DECLARE_CLASS(Resource)
QT_FORWARD_DECLARE_CLASS(SavedSearch)
QT_FORWARD_DECLARE_CLASS(SharedNotebook)
QT_FORWARD_DECLARE_CLASS(Tag)
QT_FORWARD_DECLARE_CLASS(User)

using LinkedNotebookList = QList<LinkedNotebook>;
using NoteList = QList<Note>;
using NotebookList = QList<Notebook>;
using ResourceList = QList<Resource>;
using SavedSearchList = QList<SavedSearch>;
using SharedNotebookList = QList<SharedNotebook>;
using TagList = QList<Tag>;
using UserList = QList<User>;

} // namespace quentier

#endif // LIB_QUENTIER_LOCAL_STORAGE_LISTS_H
