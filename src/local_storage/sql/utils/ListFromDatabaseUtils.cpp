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
#include "ListFromDatabaseUtils.h"

#include "../ErrorHandling.h"

#include <qevercloud/utility/ToRange.h>

#include <QSqlRecord>

namespace quentier::local_storage::sql::utils {

QList<qevercloud::SharedNotebook> listSharedNotebooks(
    const qevercloud::Guid & notebookGuid, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    QSqlQuery query{database};
    bool res = query.prepare(QStringLiteral(
        "SELECT * FROM SharedNotebooks "
        "WHERE sharedNotebookNotebookGuid = :sharedNotebookNotebookGuid"));

    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot list shared notebooks by notebook guid from the local "
            "storage database: failed to prepare query"),
        {});

    query.bindValue(
        QStringLiteral(":sharedNotebookNotebookGuid"), notebookGuid);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot list shared notebooks by notebook guid from the local "
            "storage database"),
        {});

    QMap<int, qevercloud::SharedNotebook> sharedNotebooksByIndex;
    while (query.next()) {
        qevercloud::SharedNotebook sharedNotebook;
        int indexInNotebook = -1;
        ErrorString error;
        if (!utils::fillSharedNotebookFromSqlRecord(
                query.record(), sharedNotebook, indexInNotebook,
                errorDescription)) {
            return {};
        }

        sharedNotebooksByIndex[indexInNotebook] = sharedNotebook;
    }

    QList<qevercloud::SharedNotebook> sharedNotebooks;
    sharedNotebooks.reserve(qMax(sharedNotebooksByIndex.size(), 0));
    for (const auto it: qevercloud::toRange(sharedNotebooksByIndex)) {
        sharedNotebooks << it.value();
    }

    return sharedNotebooks;
}

} // namespace quentier::local_storage::sql::utils
