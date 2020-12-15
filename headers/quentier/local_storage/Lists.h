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

#include <QList>

namespace qevercloud {

class LinkedNotebook;
class Note;
class Notebook;
class Resource;
class SavedSearch;
class SharedNotebook;
class Tag;
class User;

} // namespace qevercloud

namespace quentier {

using LinkedNotebookList = QList<qevercloud::LinkedNotebook>;
using NoteList = QList<qevercloud::Note>;
using NotebookList = QList<qevercloud::Notebook>;
using ResourceList = QList<qevercloud::Resource>;
using SavedSearchList = QList<qevercloud::SavedSearch>;
using SharedNotebookList = QList<qevercloud::SharedNotebook>;
using TagList = QList<qevercloud::Tag>;
using UserList = QList<qevercloud::User>;

} // namespace quentier

#endif // LIB_QUENTIER_LOCAL_STORAGE_LISTS_H
