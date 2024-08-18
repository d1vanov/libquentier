/*
 * Copyright 2024 Dmitry Ivanov
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

#include "AccountSyncPersistenceDirProvider.h"

#include <quentier/exception/RuntimeError.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/types/ErrorString.h>
#include <quentier/utility/StandardPaths.h>

#include <QFileInfo>

namespace quentier::synchronization {

QDir AccountSyncPersistenceDirProvider::syncPersistenceDir(
    const Account & account) const
{
    QDir dir{
        accountPersistentStoragePath(account) + QStringLiteral("/sync_data")};

    if (!dir.exists()) {
        if (!dir.mkpath(dir.absolutePath())) {
            ErrorString error{QT_TR_NOOP(
                "Cannot create dir for synchronization data persistence")};
            error.details() = dir.absolutePath();
            QNWARNING(
                "synchronization::AccountSyncPersistenceDirProvider", error);
            throw RuntimeError{std::move(error)};
        }
    }
    else {
        const QFileInfo rootDirInfo{dir.absolutePath()};

        if (Q_UNLIKELY(!rootDirInfo.isReadable())) {
            ErrorString error{QT_TR_NOOP(
                "Dir for synchronization data persistence is not readable")};
            error.details() = dir.absolutePath();
            QNWARNING(
                "synchronization::AccountSyncPersistenceDirProvider", error);
            throw RuntimeError{std::move(error)};
        }

        if (Q_UNLIKELY(!rootDirInfo.isWritable())) {
            ErrorString error{QT_TR_NOOP(
                "Dir for synchronization data persistence is not writable")};
            error.details() = dir.absolutePath();
            QNWARNING(
                "synchronization::AccountSyncPersistenceDirProvider", error);
            throw RuntimeError{std::move(error)};
        }
    }

    return dir;
}

} // namespace quentier::synchronization
