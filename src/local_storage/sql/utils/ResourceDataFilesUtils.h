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

#include "../Fwd.h"

#include <QByteArray>
#include <QDir>
#include <QString>

namespace quentier {

class ErrorString;

} // namespace quentier

namespace quentier::local_storage::sql::utils {

[[nodiscard]] bool findResourceDataBodyVersionId(
    const QString & resourceLocalId, QSqlDatabase & database,
    QString & versionId, ErrorString & errorDescription);

[[nodiscard]] bool findResourceAlternateDataBodyVersionId(
    const QString & resourceLocalId, QSqlDatabase & database,
    QString & versionId, ErrorString & errorDescription);

[[nodiscard]] bool putResourceDataBodyVersionId(
    const QString & resourceLocalId, const QString & versionId,
    QSqlDatabase & database, ErrorString & errorDescription);

[[nodiscard]] bool putResourceAlternateDataBodyVersionId(
    const QString & resourceLocalId, const QString & versionId,
    QSqlDatabase & database, ErrorString & errorDescription);

[[nodiscard]] bool readResourceDataBodyFromFile(
    const QDir & localStorageDir, const QString & noteLocalId,
    const QString & resourceLocalId, const QString & versionId,
    QByteArray & resourceDataBody, ErrorString & errorDescription);

[[nodiscard]] bool readResourceAlternateDataBodyFromFile(
    const QDir & localStorageDir, const QString & noteLocalId,
    const QString & resourceLocalId, const QString & versionId,
    QByteArray & resourceAlternateDataBody, ErrorString & errorDescription);

[[nodiscard]] bool removeResourceDataFilesForNote(
    const QString & noteLocalId, const QDir & localStorageDir,
    ErrorString & errorDescription);

[[nodiscard]] bool removeResourceDataFiles(
    const QString & noteLocalId, const QString & resourceLocalId,
    const QDir & localStorageDir, ErrorString & errorDescription);

} // namespace quentier::local_storage::sql::utils
