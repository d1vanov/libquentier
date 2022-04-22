/*
 * Copyright 2021 Dmitry Ivanov
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

#pragma once

#include <QtGlobal>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include <quentier/threading/Qt5Promise.h>
#endif

namespace quentier {

class ErrorString;

} // namespace quentier

namespace quentier::local_storage::sql::utils {

[[nodiscard]] bool backupLocalStorageDatabaseFiles(
    const QString & localStorageDirPath, const QString & backupDirPath,
    QPromise<void> & promise, // for progress notifications,
    ErrorString & errorDescription);

[[nodiscard]] bool restoreLocalStorageDatabaseFilesFromBackup(
    const QString & localStorageDirPath, const QString & backupDirPath,
    QPromise<void> & promise, // for progress notifications,
    ErrorString & errorDescription);

[[nodiscard]] bool removeLocalStorageDatabaseFilesBackup(
    const QString & backupDirPath, ErrorString & errorDescription);

} // namespace quentier::local_storage::sql::utils