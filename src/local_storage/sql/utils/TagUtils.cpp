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

#include "TagUtils.h"

#include "../ErrorHandling.h"

#include <quentier/types/ErrorString.h>

#include <qevercloud/types/Tag.h>

#include <QSqlDatabase>
#include <QSqlRecord>
#include <QSqlQuery>

namespace quentier::local_storage::sql::utils {

[[nodiscard]] QString tagLocalIdByGuid(
    const qevercloud::Guid & guid, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    QSqlQuery query{database};
    bool res = query.prepare(
        QStringLiteral("SELECT localUid FROM Tags WHERE guid = :guid"));

    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot find tag's local id by guid in the local storage "
            "database: failed to prepare query"),
        {});

    query.bindValue(QStringLiteral(":guid"), guid);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot find tag's local id by guid in the local storage "
            "database"),
        {});

    if (!query.next()) {
        return {};
    }

    return query.value(0).toString();
}

[[nodiscard]] QString tagLocalIdByName(
    const QString & name, const std::optional<QString> & linkedNotebookGuid,
    QSqlDatabase & database, ErrorString & errorDescription)
{
    QString queryString = QStringLiteral(
        "SELECT localUid FROM Tags WHERE (nameLower = :nameLower)");

    if (linkedNotebookGuid) {
        queryString.chop(1);

        if (!linkedNotebookGuid->isEmpty()) {
            queryString += QStringLiteral(
                " AND linkedNotebookGuid = :linkedNotebookGuid)");
        }
        else {
            queryString += QStringLiteral(" AND linkedNotebookGuid IS NULL)");
        }
    }

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot find tag's local id by name and linked notebook guid "
            "in the local storage database: failed to prepare query"),
        {});

    query.bindValue(QStringLiteral(":nameLower"), name.toLower());

    if (linkedNotebookGuid && !linkedNotebookGuid->isEmpty()) {
        query.bindValue(
            QStringLiteral(":linkedNotebookGuid"), *linkedNotebookGuid);
    }

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot find tag's local id by name and linked notebook guid "
            "in the local storage database"),
        {});

    if (!query.next()) {
        return {};
    }

    return query.value(0).toString();
}

[[nodiscard]] QString tagLocalId(
    const qevercloud::Tag & tag, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    auto localId = tag.localId();
    if (!localId.isEmpty())
    {
        return localId;
    }

    if (tag.guid()) {
        return tagLocalIdByGuid(
            *tag.guid(), database, errorDescription);
    }

    if (tag.name()) {
        return tagLocalIdByName(
            *tag.name(), tag.linkedNotebookGuid(), database,
            errorDescription);
    }

    return {};
}

} // namespace quentier::local_storage::sql::utils
