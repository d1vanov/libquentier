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

#include "FillFromSqlRecordUtils.h"
#include "NotebookUtils.h"

#include "../ErrorHandling.h"

#include <quentier/types/ErrorString.h>

#include <qevercloud/types/Notebook.h>
#include <qevercloud/utility/ToRange.h>

#include <QSqlDatabase>
#include <QSqlRecord>
#include <QSqlQuery>

namespace quentier::local_storage::sql::utils {

[[nodiscard]] QString notebookLocalIdByGuid(
    const qevercloud::Guid & guid, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    QSqlQuery query{database};
    bool res = query.prepare(
        QStringLiteral("SELECT localUid FROM Notebooks WHERE guid = :guid"));

    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot find notebook's local id by guid in the local storage "
            "database: failed to prepare query"),
        {});

    query.bindValue(QStringLiteral(":guid"), guid);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot find notebook's local id by guid in the local storage "
            "database"),
        {});

    if (!query.next()) {
        return {};
    }

    return query.value(0).toString();
}

[[nodiscard]] QString notebookLocalIdByName(
    const QString & name, const std::optional<QString> & linkedNotebookGuid,
    QSqlDatabase & database, ErrorString & errorDescription)
{
    QString queryString = QStringLiteral(
        "SELECT localUid FROM Notebooks "
        "WHERE (notebookNameUpper = :notebookNameUpper)");

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
            "Cannot find notebook's local id by name and linked notebook guid "
            "in the local storage database: failed to prepare query"),
        {});

    query.bindValue(QStringLiteral(":notebookNameUpper"), name.toUpper());

    if (linkedNotebookGuid && !linkedNotebookGuid->isEmpty()) {
        query.bindValue(
            QStringLiteral(":linkedNotebookGuid"), *linkedNotebookGuid);
    }

    if (!query.next()) {
        return {};
    }

    return query.value(0).toString();
}

[[nodiscard]] QString notebookLocalId(
    const qevercloud::Notebook & notebook, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    auto localId = notebook.localId();
    if (!localId.isEmpty())
    {
        return localId;
    }

    if (notebook.guid()) {
        return notebookLocalIdByGuid(
            *notebook.guid(), database, errorDescription);
    }

    if (notebook.name()) {
        return notebookLocalIdByName(
            *notebook.name(), notebook.linkedNotebookGuid(), database,
            errorDescription);
    }

    return {};
}

} // namespace quentier::local_storage::sql::utils
