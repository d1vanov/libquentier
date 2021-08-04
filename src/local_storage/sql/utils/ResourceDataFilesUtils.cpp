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

namespace {

enum class ResourceDataKind
{
    Data,
    AlternateData
};

void removeStaleResourceBodyFiles(
    const QDir & localStorageDir, const ResourceDataKind resourceDataKind,
    const QString & noteLocalId, const QString & resourceLocalId,
    const QString & actualVersionId)
{
    if (Q_UNLIKELY(noteLocalId.isEmpty())) {
        QNWARNING(
            "local_storage::sql::utils",
            "Cannot remove stale resource body files: note local id is empty; "
                << "resource local id = " << resourceLocalId
                << ", actual version id = " << actualVersionId);
        return;
    }

    if (Q_UNLIKELY(resourceLocalId.isEmpty())) {
        QNWARNING(
            "local_storage::sql::utils",
            "Cannot remove stale resource body files: resource local id is "
                << "empty; note local id = " << noteLocalId
                << ", actual version id = " << actualVersionId);
        return;
    }

    if (Q_UNLIKELY(actualVersionId.isEmpty())) {
        QNWARNING(
            "local_storage::sql::utils",
            "Cannot remove stale resource body files: actual version is "
                << "empty; note local id = " << noteLocalId
                << ", resource local id = " << resourceLocalId);
        return;
    }

    const QString dirPath = [&]
    {
        QString res;
        QTextStream strm{&res};
        strm << localStorageDir.absolutePath() << "/Resources/";
        if (resourceDataKind == ResourceDataKind::Data) {
            strm << "data";
        }
        else {
            strm << "AlternateData";
        }
        strm << "/" << noteLocalId << "/" << resourceLocalId;
        return res;
    }();

    QDir resourceDir{dirPath};
    if (!resourceDir.exists()) {
        QNDEBUG("local_storage::sql::utils", "Dir doesn't exist: " << dirPath);
        return;
    }

    const auto entries =
        resourceDir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);

    for (const auto & entry: qAsConst(entries)) {
        if (entry.baseName() == actualVersionId) {
            continue;
        }

        if (!QFile::remove(resourceDir.absoluteFilePath(entry.fileName()))) {
            QNWARNING(
                "local_storage::sql::utils",
                "Cannot delete stale response body file: "
                    << resourceDir.absoluteFilePath(entry.fileName()));
        }
    }
}

bool readResourceBodyFromFile(
    const QDir & localStorageDir, const ResourceDataKind resourceDataKind,
    const QString & noteLocalId, const QString & resourceLocalId,
    const QString & versionId, QByteArray & body,
    ErrorString & errorDescription)
{
    if (Q_UNLIKELY(noteLocalId.isEmpty())) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot read resource body from file: note local id is "
            "empty"));
        errorDescription.details() = QStringLiteral("resource local id = ");
        errorDescription.details() += resourceLocalId;
        QNWARNING("local_storage::sql::utils", errorDescription);
        return false;
    }

    if (Q_UNLIKELY(resourceLocalId.isEmpty())) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot read resource body from file: resource local id is "
            "empty"));
        errorDescription.details() = QStringLiteral("note local id = ");
        errorDescription.details() += noteLocalId;
        QNWARNING("local_storage::sql::utils", errorDescription);
        return false;
    }

    if (Q_UNLIKELY(versionId.isEmpty())) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot read resource body from file: version id is empty"));
        errorDescription.details() = QStringLiteral("note local id = ");
        errorDescription.details() += noteLocalId;
        errorDescription.details() += QStringLiteral(", resource local id = ");
        errorDescription.details() += resourceLocalId;
        QNWARNING("local_storage::sql::utils", errorDescription);
        return false;
    }

    QString resourceDataFilePath;
    {
        QTextStream strm{&resourceDataFilePath};
        strm << localStorageDir.absolutePath();
        strm << "/Resources/";
        if (resourceDataKind == ResourceDataKind::Data) {
            strm << "data";
        }
        else {
            strm << "alternateData";
        }
        strm << "/" << noteLocalId << "/" << resourceLocalId << "/" << versionId
            << ".dat";
    }

    QFile resourceDataFile{resourceDataFilePath};
    if (!resourceDataFile.exists()) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Resource body file does not exist"));
        errorDescription.details() =
            QDir::toNativeSeparators(resourceDataFilePath);
        QNWARNING("local_storage::sql::utils", errorDescription);
        return false;
    }

    if (Q_UNLIKELY(!resourceDataFile.open(QIODevice::ReadOnly))) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Failed to open resource body file for reading"));
        errorDescription.details() =
            QDir::toNativeSeparators(resourceDataFilePath);
        errorDescription.details() += QStringLiteral(": ");
        errorDescription.details() += resourceDataFile.errorString();
        QNWARNING("local_storage::sql::utils", errorDescription);
        return false;
    }

    body = resourceDataFile.readAll();
    return true;
}

bool removeResourceBodyFile(
    const QDir & localStorageDir, const ResourceDataKind resourceDataKind,
    const QString & noteLocalId, const QString & resourceLocalId,
    const QString & versionId, ErrorString & errorDescription)
{
    if (Q_UNLIKELY(noteLocalId.isEmpty())) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot remove resource body file: note local id is empty"));
        errorDescription.details() = QStringLiteral(", resource local id = ");
        errorDescription.details() += resourceLocalId;
        errorDescription.details() += QStringLiteral(", version id = ");
        errorDescription.details() += versionId;
        QNWARNING("local_storage::sql::utils", errorDescription);
        return false;
    }

    if (Q_UNLIKELY(resourceLocalId.isEmpty())) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot remove resource body file: resource local id is empty"));
        errorDescription.details() = QStringLiteral(", note local id = ");
        errorDescription.details() += noteLocalId;
        errorDescription.details() += QStringLiteral(", version id = ");
        errorDescription.details() += versionId;
        QNWARNING("local_storage::sql::utils", errorDescription);
        return false;
    }

    if (Q_UNLIKELY(versionId.isEmpty())) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot remove resource body file: note local id is empty"));
        errorDescription.details() = QStringLiteral(", note local id = ");
        errorDescription.details() += noteLocalId;
        errorDescription.details() += QStringLiteral(", resource local id = ");
        errorDescription.details() += resourceLocalId;
        QNWARNING("local_storage::sql::utils", errorDescription);
        return false;
    }

    const QString filePath = [&]
    {
        QString res;
        QTextStream strm{&res};
        strm << localStorageDir.absolutePath() << "/Resources/";
        if (resourceDataKind == ResourceDataKind::Data) {
            strm << "data";
        }
        else {
            strm << "alternateData";
        }
        strm << "/" << noteLocalId << "/" << resourceLocalId
            << "/" << versionId << ".dat";
        return res;
    }();

    QFile file{filePath};
    if (!file.exists()) {
        QNDEBUG(
            "local_storage::sql::utils",
            "Resource body file already doesn't exist: " << filePath);
        return true;
    }

    if (!file.remove()) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot remove resource body version file"));
        errorDescription.details() = file.errorString();
        QNWARNING("local_storage::sql::utils", errorDescription);
        return false;
    }

    return true;
}

bool writeResourceBodyToFile(
    const QDir & localStorageDir, const ResourceDataKind resourceDataKind,
    const QString & noteLocalId, const QString & resourceLocalId,
    const QString & versionId, const QByteArray & body,
    ErrorString & errorDescription)
{
    const QString dirPath = [&]
    {
        QString res;
        QTextStream strm{&res};
        strm << localStorageDir.absolutePath();
        strm << "/Resources/";
        if (resourceDataKind == ResourceDataKind::Data) {
            strm << "data";
        }
        else {
            strm << "alternateData";
        }

        strm << "/" << noteLocalId << "/" << resourceLocalId;
        return res;
    }();

    QDir dir{dirPath};
    if (!dir.exists() && !dir.mkpath(dirPath)) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot put resource body data to file: failed to create dir"));
        errorDescription.details() = dirPath;
        QNWARNING("local_storage::sql::utils", errorDescription);
        return false;
    }

    QFile file{dirPath + QStringLiteral("/") + versionId};
    if (Q_UNLIKELY(!file.open(QIODevice::WriteOnly))) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot put resource body to file: failed to open file for "
            "writing"));
        errorDescription.details() = file.fileName();
        QNWARNING("local_storage::sql::utils", errorDescription);
        return false;
    }

    file.write(body);
    file.flush();
    file.close();
    return true;
}

} // namespace

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

bool putResourceDataBodyVersionId(
    const QString & resourceLocalId, const QString & versionId,
    QSqlDatabase & database, ErrorString & errorDescription)
{
    static const QString queryString = QStringLiteral(
        "INSERT OR REPLACE INTO ResourceDataBodyVersionIds"
        "(resourceLocalUid, versionId) VALUES(:resourceLocalUid, :versionId)");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot put resource data body version id: failed to prepare "
            "query"),
        false);

    query.bindValue(QStringLiteral(":resourceLocalUid"), resourceLocalId);
    query.bindValue(QStringLiteral(":versionId"), versionId);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot put resource data body version id"),
        false);

    return true;
}

bool putResourceAlternateDataBodyVersionId(
    const QString & resourceLocalId, const QString & versionId,
    QSqlDatabase & database, ErrorString & errorDescription)
{
    static const QString queryString = QStringLiteral(
        "INSERT OR REPLACE INTO ResourceAlternateDataBodyVersionIds"
        "(resourceLocalUid, versionId) VALUES(:resourceLocalUid, :versionId)");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot put resource alternate data body version id: failed to "
            "prepare query"),
        false);

    query.bindValue(QStringLiteral(":resourceLocalUid"), resourceLocalId);
    query.bindValue(QStringLiteral(":versionId"), versionId);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot put resource alternate data body version id"),
        false);

    return true;
}

bool readResourceDataBodyFromFile(
    const QDir & localStorageDir, const QString & noteLocalId,
    const QString & resourceLocalId, const QString & versionId,
    QByteArray & resourceDataBody, ErrorString & errorDescription)
{
    return readResourceBodyFromFile(
        localStorageDir, ResourceDataKind::Data, noteLocalId, resourceLocalId,
        versionId, resourceDataBody, errorDescription);
}

bool readResourceAlternateDataBodyFromFile(
    const QDir & localStorageDir, const QString & noteLocalId,
    const QString & resourceLocalId, const QString & versionId,
    QByteArray & resourceAlternateDataBody, ErrorString & errorDescription)
{
    return readResourceBodyFromFile(
        localStorageDir, ResourceDataKind::AlternateData, noteLocalId,
        resourceLocalId, versionId, resourceAlternateDataBody,
        errorDescription);
}

bool writeResourceDataBodyToFile(
    const QDir & localStorageDir, const QString & noteLocalId,
    const QString & resourceLocalId, const QString & versionId,
    const QByteArray & resourceDataBody, ErrorString & errorDescription)
{
    QNDEBUG(
        "local_storage::sql::utils",
        "writeResourceDataBodyToFile: note local id = " << noteLocalId
            << ", resource local id = " << resourceLocalId
            << ", version id = " << versionId);

    return writeResourceBodyToFile(
        localStorageDir, ResourceDataKind::Data, noteLocalId, resourceLocalId,
        versionId, resourceDataBody, errorDescription);
}

bool writeResourceAlternateDataBodyToFile(
    const QDir & localStorageDir, const QString & noteLocalId,
    const QString & resourceLocalId, const QString & versionId,
    const QByteArray & resourceAlternateDataBody,
    ErrorString & errorDescription)
{
    QNDEBUG(
        "local_storage::sql::utils",
        "writeResourceAlternateDataBodyToFile: note local id = " << noteLocalId
            << ", resource local id = " << resourceLocalId
            << ", version id = " << versionId);

    return writeResourceBodyToFile(
        localStorageDir, ResourceDataKind::AlternateData, noteLocalId,
        resourceLocalId, versionId, resourceAlternateDataBody,
        errorDescription);
}

bool removeResourceDataFilesForNote(
    const QDir & localStorageDir, const QString & noteLocalId,
    ErrorString & errorDescription)
{
    QNDEBUG(
        "local_storage::sql::utils",
        "removeResourceDataFilesForNote: note local id = " << noteLocalId);

    if (Q_UNLIKELY(noteLocalId.isEmpty())) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot remove resource data files for note: note local id is "
            "empty"));
        QNWARNING("local_storage::sql::utils", errorDescription);
        return false;
    }

    const QString dataPath = localStorageDir.absolutePath() +
        QStringLiteral("/Resources/data/") + noteLocalId;

    if (!removeDir(dataPath)) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot remove resource data files for note: failed to remove "
            "the folder containing note's resource data bodies"));
        errorDescription.details() = dataPath;
        QNWARNING("local_storage::sql::utils", errorDescription);
        return false;
    }

    const QString alternateDataPath = localStorageDir.absolutePath() +
        QStringLiteral("/Resources/alternateData/") + noteLocalId;

    if (!removeDir(alternateDataPath)) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "failed to remove the folder containing note's resource "
            "alternate data bodies"));
        errorDescription.details() = alternateDataPath;
        QNWARNING("local_storage::sql::utils", errorDescription);
        return false;
    }

    return true;
}

bool removeResourceDataFiles(
    const QDir & localStorageDir, const QString & noteLocalId,
    const QString & resourceLocalId, ErrorString & errorDescription)
{
    QNDEBUG(
        "local_storage::sql::utils",
        "removeResourceDataFiles: note local id = " << noteLocalId
            << ", resource local id = " << resourceLocalId);

    if (Q_UNLIKELY(noteLocalId.isEmpty())) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot remove resource data files: note local id is empty"));
        errorDescription.details() = QStringLiteral("resource local id = ");
        errorDescription.details() += resourceLocalId;
        QNWARNING("local_storage::sql::utils", errorDescription);
        return false;
    }

    if (Q_UNLIKELY(resourceLocalId.isEmpty())) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot remove resource data files: resource local id is empty"));
        errorDescription.details() = QStringLiteral("note local id = ");
        errorDescription.details() += noteLocalId;
        QNWARNING("local_storage::sql::utils", errorDescription);
        return false;
    }

    const QString dataPath = [&]
    {
        QString res;
        QTextStream strm{&res};
        strm << localStorageDir.absolutePath() << "/Resources/data/"
            << noteLocalId << "/" << resourceLocalId;
        return res;
    }();

    if (!removeDir(dataPath)) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "failed to remove the folder containing resource data body "
            "versions"));
        errorDescription.details() = dataPath;
        QNWARNING("local_storage::sql::utils", errorDescription);
        return false;
    }

    const QString alternateDataPath = [&]
    {
        QString res;
        QTextStream strm{&res};
        strm << localStorageDir.absolutePath() << "/Resources/alternateData/"
            << noteLocalId << "/" << resourceLocalId;
        return res;
    }();

    if (!removeDir(alternateDataPath)) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "failed to remove the folder containing resource alternate "
            "data body versions"));
        errorDescription.details() = dataPath;
        QNWARNING("local_storage::sql::utils", errorDescription);
        return false;
    }

    return true;
}

bool removeResourceDataBodyFile(
    const QDir & localStorageDir, const QString & noteLocalId,
    const QString & resourceLocalId, const QString & versionId,
    ErrorString & errorDescription)
{
    QNDEBUG(
        "local_storage::sql::utils",
        "removeResourceDataBodyFile: note local id = "
            << noteLocalId << ", resource local id = "
            << resourceLocalId << " version id = " << versionId);

    return removeResourceBodyFile(
        localStorageDir, ResourceDataKind::Data, noteLocalId, resourceLocalId,
        versionId, errorDescription);
}

bool removeResourceAlternateDataBodyFile(
    const QDir & localStorageDir, const QString & noteLocalId,
    const QString & resourceLocalId, const QString & versionId,
    ErrorString & errorDescription)
{
    QNDEBUG(
        "local_storage::sql::utils",
        "removeResourceAlternateDataBodyFile: note local id = "
            << noteLocalId << ", resource local id = "
            << resourceLocalId << " version id = " << versionId);

    return removeResourceBodyFile(
        localStorageDir, ResourceDataKind::AlternateData, noteLocalId,
        resourceLocalId, versionId, errorDescription);
}

void removeStaleResourceDataBodyFiles(
    const QDir & localStorageDir, const QString & noteLocalId,
    const QString & resourceLocalId, const QString & actualVersionId)
{
    QNDEBUG(
        "local_storage::sql::utils",
        "removeStaleResourceDataBodyFiles: note local id = " << noteLocalId
            << ", resource local id = " << resourceLocalId
            << ", actual version id = " << actualVersionId);

    removeStaleResourceBodyFiles(
        localStorageDir, ResourceDataKind::Data, noteLocalId, resourceLocalId,
        actualVersionId);
}

void removeStaleResourceAlternateDataBodyFiles(
    const QDir & localStorageDir, const QString & noteLocalId,
    const QString & resourceLocalId, const QString & actualVersionId)
{
    QNDEBUG(
        "local_storage::sql::utils",
        "removeStaleResourceAlternateDataBodyFiles: note local id = "
            << noteLocalId << ", resource local id = " << resourceLocalId
            << ", actual version id = " << actualVersionId);

    removeStaleResourceBodyFiles(
        localStorageDir, ResourceDataKind::AlternateData, noteLocalId,
        resourceLocalId, actualVersionId);
}

} // namespace quentier::local_storage::sql::utils
