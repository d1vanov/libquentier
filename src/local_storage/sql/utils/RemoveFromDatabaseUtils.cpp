/*
 * Copyright 2021-2023 Dmitry Ivanov
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

#include "RemoveFromDatabaseUtils.h"

#include "../ErrorHandling.h"

#include <quentier/types/ErrorString.h>

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>

namespace quentier::local_storage::sql::utils {

bool removeUserAttributesViewedPromotions(
    const QString & userId, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    static const QString queryString = QStringLiteral(
        "DELETE FROM UserAttributesViewedPromotions WHERE id=:id");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QStringLiteral("Cannot remove user' viewed promotions from "
                       "the local storage database: failed to prepare query"),
        false);

    query.bindValue(QStringLiteral(":id"), userId);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QStringLiteral("Cannot remove user' viewed promotions from "
                       "the local storage database"),
        false);

    return true;
}

bool removeUserAttributesRecentMailedAddresses(
    const QString & userId, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    static const QString queryString = QStringLiteral(
        "DELETE FROM UserAttributesRecentMailedAddresses WHERE id=:id");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QStringLiteral("Cannot remove user' recent mailed addresses from "
                       "the local storage database: failed to prepare query"),
        false);

    query.bindValue(QStringLiteral(":id"), userId);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QStringLiteral("Cannot remove user' recent mailed addresses from "
                       "the local storage database"),
        false);

    return true;
}

bool removeUserAttributes(
    const QString & userId, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    if (!removeUserAttributesViewedPromotions(
            userId, database, errorDescription)) {
        return false;
    }

    if (!removeUserAttributesRecentMailedAddresses(
            userId, database, errorDescription))
    {
        return false;
    }

    // Clear entries from UserAttributes table
    {
        static const QString queryString =
            QStringLiteral("DELETE FROM UserAttributes WHERE id=:id");

        QSqlQuery query{database};
        bool res = query.prepare(queryString);
        ENSURE_DB_REQUEST_RETURN(
            res, query, "local_storage::sql::utils",
            QStringLiteral(
                "Cannot remove user attributes from "
                "the local storage database: failed to prepare query"),
            false);

        query.bindValue(QStringLiteral(":id"), userId);

        res = query.exec();
        ENSURE_DB_REQUEST_RETURN(
            res, query, "local_storage::sql::utils",
            QStringLiteral(
                "Cannot remove user attributes from the local storage "
                "database"),
            false);
    }

    return true;
}

bool removeAccounting(
    const QString & userId, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    static const QString queryString =
        QStringLiteral("DELETE FROM Accounting WHERE id=:id");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QStringLiteral("Cannot remove user' accounting data from "
                       "the local storage database: failed to prepare query"),
        false);

    query.bindValue(QStringLiteral(":id"), userId);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QStringLiteral(
            "Cannot remove user's accounting data from the local storage "
            "database"),
        false);

    return true;
}

bool removeAccountLimits(
    const QString & userId, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    static const QString queryString =
        QStringLiteral("DELETE FROM AccountLimits WHERE id=:id");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QStringLiteral(
            "Cannot remove user' account limits from the local storage "
            "database: failed to prepare query"),
        false);

    query.bindValue(QStringLiteral(":id"), userId);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QStringLiteral(
            "Cannot remove user's account limits from the local storage "
            "database"),
        false);

    return true;
}

bool removeBusinessUserInfo(
    const QString & userId, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    static const QString queryString =
        QStringLiteral("DELETE FROM BusinessUserInfo WHERE id=:id");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QStringLiteral(
            "Cannot remove business user info from the local storage "
            "database: failed to prepare query"),
        false);

    query.bindValue(QStringLiteral(":id"), userId);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QStringLiteral(
            "Cannot remove business user info from the local storage "
            "database"),
        false);

    return true;
}

bool removeNotebookRestrictions(
    const QString & localId, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    static const QString queryString = QStringLiteral(
        "DELETE FROM NotebookRestrictions WHERE localUid=:localUid");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QStringLiteral(
            "Cannot remove notebook restrictions from the local storage "
            "database: failed to prepare query"),
        false);

    query.bindValue(QStringLiteral(":localUid"), localId);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QStringLiteral(
            "Cannot remove notebook restrictions from the local storage "
            "database"),
        false);

    return true;
}

bool removeSharedNotebooks(
    const QString & notebookGuid, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    static const QString queryString = QStringLiteral(
        "DELETE FROM SharedNotebooks WHERE sharedNotebookNotebookGuid=:guid");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QStringLiteral("Cannot remove shared notebooks from the local storage "
                       "database: failed to prepare query"),
        false);

    query.bindValue(QStringLiteral(":guid"), notebookGuid);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QStringLiteral(
            "Cannot remove shared notebooks from the local storage database"),
        false);

    return true;
}

bool removeResourceRecognitionData(
    const QString & resourceLocalId, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    static const QString queryString = QStringLiteral(
        "DELETE FROM ResourceRecognitionData "
        "WHERE resourceLocalUid = :resourceLocalUid");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QStringLiteral(
            "Cannot delete resource recognition data by resource local id: "
            "failed to prepare query"),
        false);

    query.bindValue(QStringLiteral(":resourceLocalUid"), resourceLocalId);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QStringLiteral(
            "Cannot delete resource recognition data by resource local id"),
        false);

    return true;
}

bool removeResourceAttributes(
    const QString & resourceLocalId, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    static const QString queryString = QStringLiteral(
        "DELETE FROM ResourceAttributes "
        "WHERE resourceLocalUid = :resourceLocalUid");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QStringLiteral(
            "Cannot delete resource attributes by resource local id: "
            "failed to prepare query"),
        false);

    query.bindValue(QStringLiteral(":resourceLocalUid"), resourceLocalId);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QStringLiteral(
            "Cannot delete resource attributes by resource local id"),
        false);

    return true;
}

bool removeResourceAttributesAppDataKeysOnly(
    const QString & resourceLocalId, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    static const QString queryString = QStringLiteral(
        "DELETE FROM ResourceAttributesApplicationDataKeysOnly "
        "WHERE resourceLocalUid = :resourceLocalUid");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QStringLiteral(
            "Cannot delete resource attributes app data keys only by resource "
            "local id: failed to prepare query"),
        false);

    query.bindValue(QStringLiteral(":resourceLocalUid"), resourceLocalId);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QStringLiteral(
            "Cannot delete resource attributes app data keys only by resource "
            "local id"),
        false);

    return true;
}

bool removeResourceAttributesAppDataFullMap(
    const QString & resourceLocalId, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    static const QString queryString = QStringLiteral(
        "DELETE FROM ResourceAttributesApplicationDataFullMap "
        "WHERE resourceLocalUid = :resourceLocalUid");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QStringLiteral(
            "Cannot delete resource attributes app data full map by resource "
            "local id: failed to prepare query"),
        false);

    query.bindValue(QStringLiteral(":resourceLocalUid"), resourceLocalId);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QStringLiteral(
            "Cannot delete resource attributes app data full map by resource "
            "local id"),
        false);

    return true;
}

bool removeNoteRestrictions(
    const QString & noteLocalId, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    static const QString queryString = QStringLiteral(
        "DELETE FROM NoteRestrictions WHERE noteLocalUid = :noteLocalUid");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QStringLiteral(
            "Can't remove note restrictions from the local storage database: "
            "failed to prepare query"),
        false);

    query.bindValue(QStringLiteral(":noteLocalUid"), noteLocalId);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QStringLiteral(
            "Can't remove note restrictions from the local storage database"),
        false);

    return true;
}

bool removeNoteLimits(
    const QString & noteLocalId, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    static const QString queryString = QStringLiteral(
        "DELETE FROM NoteLimits WHERE noteLocalUid = :noteLocalUid");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QStringLiteral(
            "Can't remove note limits from the local storage database: "
            "failed to prepare query"),
        false);

    query.bindValue(QStringLiteral(":noteLocalUid"), noteLocalId);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QStringLiteral(
            "Can't remove note limits from the local storage database"),
        false);

    return true;
}

bool removeSharedNotes(
    const qevercloud::Guid & noteGuid, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    static const QString queryString = QStringLiteral(
        "DELETE FROM SharedNotes "
        "WHERE sharedNoteNoteGuid = :sharedNoteNoteGuid");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QStringLiteral(
            "Can't remove shared notes from the local storage database: "
            "failed to prepare query"),
        false);

    query.bindValue(QStringLiteral(":sharedNoteNoteGuid"), noteGuid);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QStringLiteral(
            "Can't remove shared notes from the local storage database"),
        false);

    return true;
}

} // namespace quentier::local_storage::sql::utils
