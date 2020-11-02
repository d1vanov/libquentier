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

#ifndef LIB_QUENTIER_UTILITY_STANDARD_PATHS_H
#define LIB_QUENTIER_UTILITY_STANDARD_PATHS_H

#include <quentier/types/Account.h>
#include <quentier/utility/Linkage.h>

/**
 * This macro defines the name of the environment variable which can be set to
 * override the default persistence storage path used by libquentier
 */
#define LIBQUENTIER_PERSISTENCE_STORAGE_PATH                                   \
    "LIBQUENTIER_PERSISTENCE_STORAGE_PATH"

namespace quentier {

/**
 * applicationPersistentStoragePath returns the path to folder in which
 * the application should store its persistent data. By default chooses
 * the appropriate system location but that can be overridden by setting
 * QUENTIER_PERSISTENCE_STORAGE_PATH environment variable. If the standard
 * location is overridden via the environment variable, the bool pointed to
 * by pNonStandardLocation (if any) is set to false
 */
const QString QUENTIER_EXPORT
applicationPersistentStoragePath(bool * pNonStandardLocation = nullptr);

/**
 * accountPersistentStoragePath returns the path to account-specific folder
 * in which the application should store the account-specific persistent data.
 * The path returned by this function is a sub-path within that returned by
 * applicationPersistentStoragePath function.
 *
 * @param account   The account for which the path needs to be returned; if
 *                  empty, the application persistent storage path is returned
 */
const QString QUENTIER_EXPORT
accountPersistentStoragePath(const Account & account);

/**
 * @return          The path to folder in which the application can store
 *                  temporary files
 */
const QString QUENTIER_EXPORT applicationTemporaryStoragePath();

/**
 * @return          The path to user's home directory - /home/<username> on
 *                  Linux/BSD, /Users/<username> on OS X/macOS,
 *                  C:/Users/<username> on Windows
 */
const QString QUENTIER_EXPORT homePath();

/**
 * @return          The path to user's documents storage directory
 */
const QString QUENTIER_EXPORT documentsPath();

} // namespace quentier

#endif // LIB_QUENTIER_UTILITY_STANDARD_PATHS_H
