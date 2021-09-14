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

#include "NoteUtils.h"

#include "../ErrorHandling.h"

#include <quentier/types/ErrorString.h>

#include <qevercloud/types/Note.h>

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>

namespace quentier::local_storage::sql::utils {

QString notebookLocalId(
    const qevercloud::Note & note, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    QString notebookLocalId = note.notebookLocalId();
    if (!notebookLocalId.isEmpty()) {
        return notebookLocalId;
    }

    QSqlQuery query{database};
    const auto & notebookGuid = note.notebookGuid();
    if (notebookGuid)
    {
        bool res = query.prepare(QStringLiteral(
            "SELECT localUid FROM Notebooks WHERE guid = :guid"));
        ENSURE_DB_REQUEST_RETURN(
            res, query, "local_storage::sql::utils",
            QT_TRANSLATE_NOOP(
                "local_storage::sql::utils",
                "Cannot determine notebook local id by notebook guid, failed "
                "to prepare query"),
            QString{});

        query.bindValue(QStringLiteral(":guid"), *notebookGuid);

        res = query.exec();
        ENSURE_DB_REQUEST_RETURN(
            res, query, "local_storage::sql::utils",
            QT_TRANSLATE_NOOP(
                "local_storage::sql::utils",
                "Cannot determine notebook local id by notebook guid"),
            QString{});

        if (!query.next()) {
            errorDescription.setBase(QT_TRANSLATE_NOOP(
                "local_storage::sql::utils",
                "Cannot find notebook local id for guid"));
            errorDescription.details() = *notebookGuid;
            QNWARNING("local_storage::sql::utils", errorDescription);
            return QString{};
        }

        return query.value(0).toString();
    }

    // No notebookGuid set to note, try to deduce notebook local id by note
    // local id
    bool res = query.prepare(QStringLiteral(
        "SELECT notebookLocalUid FROM Notes WHERE localUid = :localUid"));
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot determine notebook local id by note local id, failed "
            "to prepare query"),
        QString{});

    query.bindValue(QStringLiteral(":localUid"), note.localId());

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot determine notebook local id by note local id"),
        QString{});

    if (!query.next()) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
                "local_storage::sql::utils",
                "Cannot find notebook local id for note local id"));
        errorDescription.details() = note.localId();
        QNWARNING("local_storage::sql::utils", errorDescription);
        return QString{};
    }

    return query.value(0).toString();
}

QString notebookGuid(
    const qevercloud::Note & note, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    const auto & notebookGuid = note.notebookGuid();
    if (notebookGuid) {
        return *notebookGuid;
    }

    const auto localId = notebookLocalId(
        note, database, errorDescription);
    if (localId.isEmpty()) {
        return {};
    }

    QSqlQuery query{database};
    bool res = query.prepare(QStringLiteral(
        "SELECT guid FROM Notebooks WHERE localUid = :localUid"));
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot determine notebook guid by local id, failed to prepare "
            "query"),
        QString{});

    query.bindValue(QStringLiteral(":localUid"), localId);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot determine notebook guid by local id"),
        QString{});

    if (!query.next()) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
                "local_storage::sql::utils",
                "Cannot find notebook guid for local id"));
        errorDescription.details() = localId;
        QNWARNING("local_storage::sql::utils", errorDescription);
        return QString{};
    }

    return query.value(0).toString();
}

} // namespace quentier::local_storage::sql::utils
