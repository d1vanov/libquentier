/*
 * Copyright 2018 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_TESTS_SYNCHRONIZATION_UTILITY_H
#define LIB_QUENTIER_TESTS_SYNCHRONIZATION_UTILITY_H

#include <QtGlobal>
#include <QHash>

#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
#include <qt5qevercloud/QEverCloud.h>
#else
#include <qt4qevercloud/QEverCloud.h>
#endif

namespace quentier {

QT_FORWARD_DECLARE_CLASS(ErrorString)
QT_FORWARD_DECLARE_CLASS(LocalStorageManagerAsync)

bool listSavedSearchesFromLocalStorage(const LocalStorageManagerAsync & localStorageManagerAsync,
                                       const qint32 afterUSN,
                                       QHash<QString, qevercloud::SavedSearch> & savedSearches,
                                       ErrorString & errorDescription);

bool listTagsFromLocalStorage(const LocalStorageManagerAsync & localStorageManagerAsync,
                              const qint32 afterUSN, const QString & linkedNotebookGuid,
                              QHash<QString, qevercloud::Tag> & tags,
                              ErrorString & errorDescription);

bool listNotebooksFromLocalStorage(const LocalStorageManagerAsync & localStorageManagerAsync,
                                   const qint32 afterUSN, const QString & linkedNotebookGuid,
                                   QHash<QString, qevercloud::Notebook> & notebooks,
                                   ErrorString & errorDescription);

bool listNotesFromLocalStorage(const LocalStorageManagerAsync & localStorageManagerAsync,
                               const qint32 afterUSN, const QString & linkedNotebookGuid,
                               QHash<QString, qevercloud::Note> & notes,
                               ErrorString & errorDescription);

bool listLinkedNotebooksFromLocalStorage(const LocalStorageManagerAsync & localStorageManagerAsync,
                                         const qint32 afterUSN, QHash<QString, qevercloud::LinkedNotebook> & linkedNotebooks,
                                         ErrorString & errorDescription);

} // namespace quentier

#endif // LIB_QUENTIER_TESTS_SYNCHRONIZATION_UTILITY_H
