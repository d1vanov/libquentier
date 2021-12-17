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

#include <qevercloud/types/TypeAliases.h>

class QSqlDatabase;

namespace quentier {

class ErrorString;

} // namespace quentier

namespace quentier::local_storage::sql::utils {

[[nodiscard]] bool removeUserAttributesViewedPromotions(
    const QString & userId, QSqlDatabase & database,
    ErrorString & errorDescription);

[[nodiscard]] bool removeUserAttributesRecentMailedAddresses(
    const QString & userId, QSqlDatabase & database,
    ErrorString & errorDescription);

[[nodiscard]] bool removeUserAttributes(
    const QString & userId, QSqlDatabase & database,
    ErrorString & errorDescription);

[[nodiscard]] bool removeAccounting(
    const QString & userId, QSqlDatabase & database,
    ErrorString & errorDescription);

[[nodiscard]] bool removeAccountLimits(
    const QString & userId, QSqlDatabase & database,
    ErrorString & errorDescription);

[[nodiscard]] bool removeBusinessUserInfo(
    const QString & userId, QSqlDatabase & database,
    ErrorString & errorDescription);

[[nodiscard]] bool removeNotebookRestrictions(
    const QString & localId, QSqlDatabase & database,
    ErrorString & errorDescription);

[[nodiscard]] bool removeSharedNotebooks(
    const QString & notebookGuid, QSqlDatabase & database,
    ErrorString & errorDescription);

[[nodiscard]] bool removeResourceRecognitionData(
    const QString & resourceLocalId, QSqlDatabase & database,
    ErrorString & errorDescription);

[[nodiscard]] bool removeResourceAttributes(
    const QString & resourceLocalId, QSqlDatabase & database,
    ErrorString & errorDescription);

[[nodiscard]] bool removeResourceAttributesAppDataKeysOnly(
    const QString & resourceLocalId, QSqlDatabase & database,
    ErrorString & errorDescription);

[[nodiscard]] bool removeResourceAttributesAppDataFullMap(
    const QString & resourceLocalId, QSqlDatabase & database,
    ErrorString & errorDescription);

[[nodiscard]] bool removeNoteRestrictions(
    const QString & noteLocalId, QSqlDatabase & database,
    ErrorString & errorDescription);

[[nodiscard]] bool removeNoteLimits(
    const QString & noteLocalId, QSqlDatabase & database,
    ErrorString & errorDescription);

[[nodiscard]] bool removeSharedNotes(
    const qevercloud::Guid & noteGuid, QSqlDatabase & database,
    ErrorString & errorDescription);

} // namespace quentier::local_storage::sql::utils
