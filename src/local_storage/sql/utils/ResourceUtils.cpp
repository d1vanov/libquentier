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

#include "../ErrorHandling.h"

#include <quentier/types/ErrorString.h>

#include <QSqlQuery>

namespace quentier::local_storage::sql::utils {

QString noteLocalIdByResourceLocalId(
    const QString & resourceLocalId, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    static const QString queryString = QStringLiteral(
        "SELECT localNote FROM NoteResources "
        "WHERE localResource = :localResource");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot get note local id by resource local id: failed to prepare "
            "query"),
        {});

    query.bindValue(QStringLiteral(":localResource"), resourceLocalId);

    res = query.exec();

    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot get note local id by resource local id"),
        {});

    if (!query.next()) {
        errorDescription.setBase(
            QT_TRANSLATE_NOOP(
                "local_storage::sql::utils",
                "Could not find note local id corresponding to resource "
                "local id"));
        errorDescription.details() = resourceLocalId;
        QNWARNING("local_storage::sql::utils", errorDescription);
        return {};
    }

    return query.value(0).toString();
}

QString resourceLocalIdByGuid(
    const QString & resourceGuid, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    static const QString queryString = QStringLiteral(
        "SELECT resourceLocalUid FROM Resources "
        "WHERE resouceGuid = :resourceGuid");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot get resource local id by resource guid: failed to prepare "
            "query"),
        {});

    query.bindValue(QStringLiteral(":resourceGuid"), resourceGuid);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot get resource local id by resource guid: failed to prepare "
            "query"),
        {});

    if (!query.next()) {
        errorDescription.setBase(
            QT_TRANSLATE_NOOP(
                "local_storage::sql::utils",
                "Could not find resource local id corresponding to resource "
                "guid"));
        errorDescription.details() = resourceGuid;
        QNWARNING("local_storage::sql::utils", errorDescription);
        return {};
    }

    return query.value(0).toString();
}

} // namespace quentier::local_storage::sql::utils
