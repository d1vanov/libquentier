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

#include <qevercloud/types/Fwd.h>
#include <qevercloud/types/TypeAliases.h>

#include <optional>

#include <QFlags>

class QDir;
class QSqlDatabase;
class QString;

namespace quentier {

class ErrorString;

} // namespace quentier

namespace quentier::local_storage::sql::utils {

[[nodiscard]] QString noteLocalIdByResourceLocalId(
    const QString & resourceLocalId, QSqlDatabase & database,
    ErrorString & errorDescription);

[[nodiscard]] QString resourceLocalId(
    const qevercloud::Resource & resource, QSqlDatabase & database,
    ErrorString & errorDescription);

[[nodiscard]] QString resourceLocalIdByGuid(
    const QString & resourceGuid, QSqlDatabase & database,
    ErrorString & errorDescription);

enum class FetchResourceOption
{
    WithBinaryData = 1 << 0
};
Q_DECLARE_FLAGS(FetchResourceOptions, FetchResourceOption);

[[nodiscard]] std::optional<qevercloud::Resource> findResourceByLocalId(
    const QString & resourceLocalId, FetchResourceOptions options,
    const QDir & localStorageDir,
    const QReadWriteLockPtr & resourceDataFilesLock, QSqlDatabase & database,
    ErrorString & errorDescription);

[[nodiscard]] std::optional<qevercloud::Resource> findResourceByGuid(
    const qevercloud::Guid & resourceGuid, FetchResourceOptions options,
    const QDir & localStorageDir,
    const QReadWriteLockPtr & resourceDataFilesLock, QSqlDatabase & database,
    ErrorString & errorDescription);

[[nodiscard]] bool fillResourceData(
    qevercloud::Resource & resource, const QDir & localStorageDir,
    QSqlDatabase & database, ErrorString & errorDescription);

[[nodiscard]] bool findResourceAttributesApplicationDataKeysOnlyByLocalId(
    const QString & localId, qevercloud::ResourceAttributes & attributes,
    QSqlDatabase & database, ErrorString & errorDescription);

[[nodiscard]] bool findResourceAttributesApplicationDataFullMapByLocalId(
    const QString & localId, qevercloud::ResourceAttributes & attributes,
    QSqlDatabase & database, ErrorString & errorDescription);

} // namespace quentier::local_storage::sql::utils
