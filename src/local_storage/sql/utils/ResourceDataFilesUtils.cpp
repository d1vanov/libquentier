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

#include "../ErrorHandling.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/types/ErrorString.h>
#include <quentier/utility/FileSystem.h>

#include <QFile>
#include <QSqlQuery>
#include <QTextStream>

namespace quentier::local_storage::sql::utils {

bool findResourceDataBodyVersionId(
    const QString & resourceLocalId, QSqlDatabase & database,
    QString & versionId, ErrorString & errorDescription)
{
    static const QString queryString = QStringLiteral(
        "SELECT versionId FROM ResourceDataBodyVersionIds WHERE "
        "resourceLocalUid = :resourceLocalUid");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot find resource data body version id: failed to prepare "
            "query"),
        false);

    query.bindValue(QStringLiteral(":resourceLocalUid"), resourceLocalId);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot find resource data body version id"),
        false);

    if (query.next()) {
        versionId = query.value(0).toString();
    }

    return true;
}

bool findResourceAlternateDataBodyVersionId(
    const QString & resourceLocalId, QSqlDatabase & database,
    QString & versionId, ErrorString & errorDescription)
{
    static const QString queryString = QStringLiteral(
        "SELECT versionId FROM ResourceAlternateDataBodyVersionIds WHERE "
        "resourceLocalUid = :resourceLocalUid");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot find resource alternate data body version id: failed to "
            "prepare query"),
        false);

    query.bindValue(QStringLiteral(":resourceLocalUid"), resourceLocalId);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot find resource alternate data body version id"),
        false);

    if (query.next()) {
        versionId = query.value(0).toString();
    }

    return true;
}

bool readResourceDataBodyFromFile(
    const QDir & localStorageDir, const QString & noteLocalId,
    const QString & resourceLocalId, const QString & versionId,
    QByteArray & resourceDataBody, ErrorString & errorDescription)
{
    QString resourceDataFilePath;
    {
        QTextStream strm{&resourceDataFilePath};
        strm << localStorageDir.absolutePath() << "/Resources/data/"
            << noteLocalId << "/" << resourceLocalId << "/" << versionId
            << ".dat";
    }

    QFile resourceDataFile{resourceDataFilePath};
    if (!resourceDataFile.exists()) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Resource data body file does not exist"));
        errorDescription.details() =
            QDir::toNativeSeparators(resourceDataFilePath);
        QNWARNING("local_storage::sql::utils", errorDescription);
        return false;
    }

    if (!resourceDataFile.open(QIODevice::ReadOnly)) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Failed to open resource data body file for reading"));
        errorDescription.details() =
            QDir::toNativeSeparators(resourceDataFilePath);
        errorDescription.details() += QStringLiteral(": ");
        errorDescription.details() += resourceDataFile.errorString();
        QNWARNING("local_storage::sql::utils", errorDescription);
        return false;
    }

    resourceDataBody = resourceDataFile.readAll();
    return true;
}

bool readResourceAlternateDataBodyFromFile(
    const QDir & localStorageDir, const QString & noteLocalId,
    const QString & resourceLocalId, const QString & versionId,
    QByteArray & resourceAlternateDataBody, ErrorString & errorDescription)
{
    QString resourceAlternateDataFilePath;
    {
        QTextStream strm{&resourceAlternateDataFilePath};
        strm << localStorageDir.absolutePath() << "/Resources/alternateData/"
            << noteLocalId << "/" << resourceLocalId << "/" << versionId
            << ".dat";
    }

    QFile resourceAlternateDataFile{resourceAlternateDataFilePath};
    if (!resourceAlternateDataFile.exists()) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Resource alternate data body file does not exist"));
        errorDescription.details() =
            QDir::toNativeSeparators(resourceAlternateDataFilePath);
        QNWARNING("local_storage::sql::utils", errorDescription);
        return false;
    }

    if (!resourceAlternateDataFile.open(QIODevice::ReadOnly)) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Failed to open resource alternate data body file for reading"));
        errorDescription.details() =
            QDir::toNativeSeparators(resourceAlternateDataFilePath);
        errorDescription.details() += QStringLiteral(": ");
        errorDescription.details() += resourceAlternateDataFile.errorString();
        QNWARNING("local_storage::sql::utils", errorDescription);
        return false;
    }

    resourceAlternateDataBody = resourceAlternateDataFile.readAll();
    return true;
}

bool removeResourceDataFilesForNote(
    const QString & noteLocalId, const QDir & localStorageDir,
    ErrorString & errorDescription)
{
    QNDEBUG(
        "local_storage::sql::utils",
        "removeResourceDataFilesForNote: note local id = " << noteLocalId);

    const QString dataPath = localStorageDir.absolutePath() +
        QStringLiteral("/Resources/data/") + noteLocalId;

    if (!removeDir(dataPath)) {
        errorDescription.setBase(
            QT_TR_NOOP("failed to remove the folder containing "
                       "note's resource data bodies"));
        errorDescription.details() = dataPath;
        QNWARNING("local_storage::sql::utils", errorDescription);
        return false;
    }

    const QString alternateDataPath = localStorageDir.absolutePath() +
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

bool removeResourceDataFiles(
    const QString & noteLocalId, const QString & resourceLocalId,
    const QDir & localStorageDir, ErrorString & errorDescription)
{
    // TODO: implement
    Q_UNUSED(noteLocalId)
    Q_UNUSED(resourceLocalId)
    Q_UNUSED(localStorageDir)
    Q_UNUSED(errorDescription)
    return true;
}

} // namespace quentier::local_storage::sql::utils
