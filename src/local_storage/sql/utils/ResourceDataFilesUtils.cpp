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

#include "ResourceDataFilesUtils.h"

#include <quentier/logging/QuentierLogger.h>
#include <quentier/types/ErrorString.h>
#include <quentier/utility/FileSystem.h>

#include <QDir>

namespace quentier::local_storage::sql::utils {

bool removeResourceDataFilesForNote(
    const QString & noteLocalId, const QDir & localStorageDir,
    ErrorString & errorDescription)
{
    QNDEBUG(
        "local_storage::sql::utils",
        "removeResourceDataFilesForNote: note local id = " << noteLocalId);

    const QString dataPath =
        localStorageDir.absolutePath() + QStringLiteral("/Resources/data/") +
        noteLocalId;

    if (!removeDir(dataPath)) {
        errorDescription.setBase(
            QT_TR_NOOP("failed to remove the folder containing "
                       "note's resource data bodies"));
        errorDescription.details() = dataPath;
        QNWARNING("local_storage::sql::utils", errorDescription);
        return false;
    }

    const QString alternateDataPath =
        localStorageDir.absolutePath() +
        QStringLiteral("/Resources/alternateData/") + noteLocalId;

    if (!removeDir(alternateDataPath)) {
        errorDescription.setBase(
            QT_TR_NOOP("failed to remove the folder containing "
                       "note's resource alternate data bodies"));
        errorDescription.details() = alternateDataPath;
        QNWARNING("local_storage::sql::utils", errorDescription);
        return false;
    }

    return true;
}

bool readResourceDataFromFiles(
    qevercloud::Resource & resource, const QDir & localStorageDir,
    ErrorString & errorDescription)
{
    // TODO: implement
    Q_UNUSED(resource)
    Q_UNUSED(localStorageDir)
    Q_UNUSED(errorDescription)
    return true;
}

bool removeResourceDataFiles(
    const QString & noteLocalId, const QString & resourceLocalId,
    const QDir & localStorageDir, ErrorString & errorDescription)
{
    Q_UNUSED(noteLocalId)
    Q_UNUSED(resourceLocalId)
    Q_UNUSED(localStorageDir)
    Q_UNUSED(errorDescription)
    return true;
}

} // namespace quentier::local_storage::sql::utils
