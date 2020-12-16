/*
 * Copyright 2017-2020 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_LOCAL_STORAGE_DEFAULT_LOCAL_STORAGE_CHECK_EXPIRY_CHECKER_CONFIG_H
#define LIB_QUENTIER_LOCAL_STORAGE_DEFAULT_LOCAL_STORAGE_CHECK_EXPIRY_CHECKER_CONFIG_H

#include <QtGlobal>

namespace quentier::local_storage {

constexpr quint32 maxNotesToCache = 5;
constexpr quint32 maxResourcesToCache = 5;
constexpr quint32 maxNotebooksToCache = 5;
constexpr quint32 maxTagsToCache = 5;
constexpr quint32 maxLinkedNotebooksToCache = 5;
constexpr quint32 maxSavedSearchesToCache = 5;

} // namespace quentier::local_storage

#endif // LIB_QUENTIER_LOCAL_STORAGE_DEFAULT_LOCAL_STORAGE_CHECK_EXPIRY_CHECKER_CONFIG_H
