/*
 * Copyright 2021-2024 Dmitry Ivanov
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

#include "ResourceUtils.h"
#include "Common.h"
#include "FillFromSqlRecordUtils.h"
#include "ResourceDataFilesUtils.h"
#include "SqlUtils.h"

#include "../ErrorHandling.h"

#include <quentier/types/ErrorString.h>

#include <qevercloud/types/Resource.h>

#include <QCryptographicHash>
#include <QSqlQuery>
#include <QSqlRecord>

#include <algorithm>

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
        QStringLiteral(
            "Cannot get note local id by resource local id: failed to prepare "
            "query"),
        {});

    query.bindValue(QStringLiteral(":localResource"), resourceLocalId);

    res = query.exec();

    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QStringLiteral("Cannot get note local id by resource local id"), {});

    if (!query.next()) {
        QNDEBUG(
            "local_storage::sql::utils",
            "Could not find note local id corresponding to resource local id "
                << resourceLocalId);
        return {};
    }

    return query.value(0).toString();
}

QString resourceLocalId(
    const qevercloud::Resource & resource, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    auto localId = resource.localId();
    if (!localId.isEmpty()) {
        return localId;
    }

    if (resource.guid()) {
        return resourceLocalIdByGuid(
            *resource.guid(), database, errorDescription);
    }

    return {};
}

QString resourceLocalIdByGuid(
    const qevercloud::Guid & resourceGuid, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    static const QString queryString = QStringLiteral(
        "SELECT resourceLocalUid FROM Resources "
        "WHERE resourceGuid = :resourceGuid");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QStringLiteral(
            "Cannot get resource local id by resource guid: failed to prepare "
            "query"),
        {});

    query.bindValue(QStringLiteral(":resourceGuid"), resourceGuid);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QStringLiteral(
            "Cannot get resource local id by resource guid: failed to prepare "
            "query"),
        {});

    if (!query.next()) {
        QNDEBUG(
            "local_storage::sql::utils",
            "Could not find resource local id corresponding to resource guid "
                << resourceGuid);
        return {};
    }

    return query.value(0).toString();
}

std::optional<int> resourceIndexInNote(
    const QString & resourceLocalId, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    static const QString queryString = QStringLiteral(
        "SELECT resourceIndexInNote FROM Resources WHERE resourceLocalUid = "
        ":resourceLocalUid");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QStringLiteral(
            "Cannot get resource index in note by resource local id: failed to "
            "prepare query"),
        {});

    query.bindValue(QStringLiteral(":resourceLocalUid"), resourceLocalId);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QStringLiteral(
            "Cannot get resource index in note by resource local id: failed to "
            "prepare query"),
        {});

    if (!query.next()) {
        QNDEBUG(
            "local_storage::sql::utils",
            "Could not find resource index in note corresponding to resource "
                << "local id " << resourceLocalId);
        return std::nullopt;
    }

    bool conversionResult = false;
    int index = query.value(0).toInt(&conversionResult);
    if (Q_UNLIKELY(!conversionResult)) {
        errorDescription.setBase(QStringLiteral(
            "Could not find resource index in note corresponding to resource "
            "local id: failed to convert index in note to int"));
        QNWARNING("local_storage::sql::utils", errorDescription);
        return std::nullopt;
    }

    return index;
}

std::optional<qevercloud::Resource> findResourceByLocalId(
    const QString & resourceLocalId, const FetchResourceOptions options,
    const QDir & localStorageDir, int & indexInNote, QSqlDatabase & database,
    ErrorString & errorDescription, const TransactionOption transactionOption)
{
    std::optional<SelectTransactionGuard> transactionGuard;
    if (transactionOption == TransactionOption::UseSeparateTransaction) {
        transactionGuard.emplace(database);
    }

    static const QString queryString = QStringLiteral(
        "SELECT Resources.resourceLocalUid, resourceGuid, "
        "noteGuid, resourceUpdateSequenceNumber, resourceIsDirty, "
        "dataSize, dataHash, mime, width, height, recognitionDataSize, "
        "recognitionDataHash, alternateDataSize, alternateDataHash, "
        "resourceIndexInNote, resourceSourceURL, timestamp, "
        "resourceLatitude, resourceLongitude, resourceAltitude, "
        "cameraMake, cameraModel, clientWillIndex, fileName, attachment, "
        "localNote, recognitionDataBody FROM Resources "
        "LEFT OUTER JOIN NoteResources ON "
        "Resources.resourceLocalUid = NoteResources.localResource "
        "LEFT OUTER JOIN ResourceAttributes ON "
        "Resources.resourceLocalUid = "
        "ResourceAttributes.resourceLocalUid "
        "WHERE Resources.resourceLocalUid = :resourceLocalUid");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_THROW(
        res, query, "local_storage::sql::utils",
        QStringLiteral("Cannot find resource by local id in the local storage "
                       "database: failed to prepare query"));

    query.bindValue(QStringLiteral(":resourceLocalUid"), resourceLocalId);

    res = query.exec();
    ENSURE_DB_REQUEST_THROW(
        res, query, "local_storage::sql::utils",
        QStringLiteral(
            "Cannot find resource by local id in the local storage database"));

    if (!query.next()) {
        return std::nullopt;
    }

    const auto record = query.record();
    qevercloud::Resource resource;
    ErrorString error;
    indexInNote = -1;
    if (!fillResourceFromSqlRecord(record, resource, indexInNote, error)) {
        errorDescription.setBase(QStringLiteral(
            "Failed to find resource by local id in the local storage "
            "database"));
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING("local_storage::sql::utils", errorDescription);
        return std::nullopt;
    }

    if (!findResourceAttributesApplicationDataByLocalId(
            resource, database, errorDescription))
    {
        return std::nullopt;
    }

    if (!options.testFlag(FetchResourceOption::WithBinaryData)) {
        return resource;
    }

    if (!fillResourceData(
            resource, localStorageDir, database, errorDescription))
    {
        return std::nullopt;
    }

    return resource;
}

std::optional<qevercloud::Resource> findResourceByGuid(
    const qevercloud::Guid & resourceGuid, const FetchResourceOptions options,
    const QDir & localStorageDir, int & indexInNote, QSqlDatabase & database,
    ErrorString & errorDescription, const TransactionOption transactionOption)
{
    std::optional<SelectTransactionGuard> transactionGuard;
    if (transactionOption == TransactionOption::UseSeparateTransaction) {
        transactionGuard.emplace(database);
    }

    static const QString queryString = QStringLiteral(
        "SELECT Resources.resourceLocalUid, resourceGuid, "
        "noteGuid, resourceUpdateSequenceNumber, resourceIsDirty, "
        "dataSize, dataHash, mime, width, height, recognitionDataSize, "
        "recognitionDataHash, alternateDataSize, alternateDataHash, "
        "resourceIndexInNote, resourceSourceURL, timestamp, "
        "resourceLatitude, resourceLongitude, resourceAltitude, "
        "cameraMake, cameraModel, clientWillIndex, fileName, attachment, "
        "localNote, recognitionDataBody FROM Resources "
        "LEFT OUTER JOIN NoteResources ON "
        "Resources.resourceLocalUid = NoteResources.localResource "
        "LEFT OUTER JOIN ResourceAttributes ON "
        "Resources.resourceLocalUid = "
        "ResourceAttributes.resourceLocalUid "
        "WHERE Resources.resourceGuid = :resourceGuid");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_THROW(
        res, query, "local_storage::sql::utils",
        QStringLiteral("Cannot find resource by guid in the local storage "
                       "database: failed to prepare query"));

    query.bindValue(QStringLiteral(":resourceGuid"), resourceGuid);

    res = query.exec();
    ENSURE_DB_REQUEST_THROW(
        res, query, "local_storage::sql::utils",
        QStringLiteral(
            "Cannot find resource by guid in the local storage database"));

    if (!query.next()) {
        return std::nullopt;
    }

    const auto record = query.record();
    qevercloud::Resource resource;
    ErrorString error;
    indexInNote = -1;
    if (!fillResourceFromSqlRecord(record, resource, indexInNote, error)) {
        errorDescription.setBase(QStringLiteral(
            "Failed to find resource by guid in the local storage "
            "database"));
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING("local_storage::sql::utils", errorDescription);
        return std::nullopt;
    }

    if (resource.attributes()) {
        const auto & resourceLocalId = resource.localId();

        if (!findResourceAttributesApplicationDataKeysOnlyByLocalId(
                resourceLocalId, *resource.mutableAttributes(), database,
                errorDescription))
        {
            return std::nullopt;
        }

        if (!findResourceAttributesApplicationDataFullMapByLocalId(
                resourceLocalId, *resource.mutableAttributes(), database,
                errorDescription))
        {
            return std::nullopt;
        }
    }

    if (!options.testFlag(FetchResourceOption::WithBinaryData)) {
        return resource;
    }

    if (!fillResourceData(
            resource, localStorageDir, database, errorDescription))
    {
        return std::nullopt;
    }

    return resource;
}

bool fillResourceData(
    qevercloud::Resource & resource, const QDir & localStorageDir,
    QSqlDatabase & database, ErrorString & errorDescription)
{
    const QString & resourceLocalId = resource.localId();

    QString resourceDataBodyVersionId;
    if (!findResourceDataBodyVersionId(
            resourceLocalId, database, resourceDataBodyVersionId,
            errorDescription))
    {
        return false;
    }

    QString resourceAlternateDataBodyVersionId;
    if (!findResourceAlternateDataBodyVersionId(
            resourceLocalId, database, resourceAlternateDataBodyVersionId,
            errorDescription))
    {
        return false;
    }

    if (!resourceDataBodyVersionId.isEmpty()) {
        QByteArray resourceDataBody;
        if (!readResourceDataBodyFromFile(
                localStorageDir, resource.noteLocalId(), resourceLocalId,
                resourceDataBodyVersionId, resourceDataBody, errorDescription))
        {
            return false;
        }

        if (!resourceDataBody.isEmpty()) {
            if (!resource.data()) {
                resource.setData(qevercloud::Data{});
            }

            auto & data = *resource.mutableData();
            if (!data.size()) {
                data.setSize(resourceDataBody.size());
            }
            else {
                Q_ASSERT(*data.size() == resourceDataBody.size());
            }

            if (!data.bodyHash()) {
                data.setBodyHash(QCryptographicHash::hash(
                    resourceDataBody, QCryptographicHash::Md5));
            }
            else {
                Q_ASSERT(
                    *data.bodyHash() ==
                    QCryptographicHash::hash(
                        resourceDataBody, QCryptographicHash::Md5));
            }

            data.setBody(std::move(resourceDataBody));
        }
    }

    if (!resourceAlternateDataBodyVersionId.isEmpty()) {
        QByteArray resourceAlternateDataBody;
        if (!readResourceAlternateDataBodyFromFile(
                localStorageDir, resource.noteLocalId(), resourceLocalId,
                resourceAlternateDataBodyVersionId, resourceAlternateDataBody,
                errorDescription))
        {
            return false;
        }

        if (!resourceAlternateDataBody.isEmpty()) {
            if (!resource.alternateData()) {
                resource.setAlternateData(qevercloud::Data{});
            }

            auto & alternateData = *resource.mutableAlternateData();
            if (!alternateData.size()) {
                alternateData.setSize(resourceAlternateDataBody.size());
            }
            else {
                Q_ASSERT(
                    *alternateData.size() == resourceAlternateDataBody.size());
            }

            if (!alternateData.bodyHash()) {
                alternateData.setBodyHash(QCryptographicHash::hash(
                    resourceAlternateDataBody, QCryptographicHash::Md5));
            }
            else {
                Q_ASSERT(
                    *alternateData.bodyHash() ==
                    QCryptographicHash::hash(
                        resourceAlternateDataBody, QCryptographicHash::Md5));
            }

            alternateData.setBody(std::move(resourceAlternateDataBody));
        }
    }

    return true;
}

bool findResourceAttributesApplicationDataKeysOnlyByLocalId(
    const QString & localId, qevercloud::ResourceAttributes & attributes,
    QSqlDatabase & database, ErrorString & errorDescription)
{
    static const QString queryString = QStringLiteral(
        "SELECT resourceKey FROM ResourceAttributesApplicationDataKeysOnly "
        "WHERE resourceLocalUid = :resourceLocalUid");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QStringLiteral(
            "Cannot find resource application data keys only part in the local "
            "storage database: failed to prepare query"),
        false);

    query.bindValue(QStringLiteral(":resourceLocalUid"), localId);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QStringLiteral(
            "Cannot find resource application data keys only part in the local "
            "storage database"),
        false);

    while (query.next()) {
        if (!attributes.applicationData()) {
            attributes.setApplicationData(qevercloud::LazyMap{});
        }

        auto & appData = *attributes.mutableApplicationData();
        if (!appData.keysOnly()) {
            appData.setKeysOnly(QSet<QString>{});
        }

        appData.mutableKeysOnly()->insert(query.value(0).toString());
    }
    return true;
}

bool findResourceAttributesApplicationDataFullMapByLocalId(
    const QString & localId, qevercloud::ResourceAttributes & attributes,
    QSqlDatabase & database, ErrorString & errorDescription)
{
    static const QString queryString = QStringLiteral(
        "SELECT resourceMapKey, resourceValue "
        "FROM ResourceAttributesApplicationDataFullMap "
        "WHERE resourceLocalUid = :resourceLocalUid");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QStringLiteral(
            "Cannot find resource application data full map part in the local "
            "storage database: failed to prepare query"),
        false);

    query.bindValue(QStringLiteral(":resourceLocalUid"), localId);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QStringLiteral(
            "Cannot find resource application data full map part in the local "
            "storage database"),
        false);

    while (query.next()) {
        const auto record = query.record();
        const int keyIndex = record.indexOf(QStringLiteral("resourceMapKey"));
        const int valueIndex = record.indexOf(QStringLiteral("resourceValue"));

        if (keyIndex < 0 || valueIndex < 0) {
            return true;
        }

        if (!attributes.applicationData()) {
            attributes.setApplicationData(qevercloud::LazyMap{});
        }

        auto & appData = *attributes.mutableApplicationData();
        if (!appData.fullMap()) {
            appData.setFullMap(QMap<QString, QString>{});
        }

        appData.mutableFullMap()->insert(
            record.value(keyIndex).toString(),
            record.value(valueIndex).toString());
    }

    return true;
}

bool findResourceAttributesApplicationDataByLocalId(
    const QString & localId, qevercloud::ResourceAttributes & attributes,
    QSqlDatabase & database, ErrorString & errorDescription)
{
    if (!findResourceAttributesApplicationDataKeysOnlyByLocalId(
            localId, attributes, database, errorDescription))
    {
        return false;
    }

    if (!findResourceAttributesApplicationDataFullMapByLocalId(
            localId, attributes, database, errorDescription))
    {
        return false;
    }

    return true;
}

bool findResourceAttributesApplicationDataByLocalId(
    qevercloud::Resource & resource, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    if (resource.attributes()) {
        return findResourceAttributesApplicationDataByLocalId(
            resource.localId(), *resource.mutableAttributes(), database,
            errorDescription);
    }

    qevercloud::ResourceAttributes attributes;
    if (!findResourceAttributesApplicationDataByLocalId(
            resource.localId(), attributes, database, errorDescription))
    {
        return false;
    }

    if (attributes.applicationData()) {
        resource.setAttributes(std::move(attributes));
    }

    return true;
}

QStringList findResourceLocalIdsByMimeTypes(
    const QStringList & resourceMimeTypes, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    if (resourceMimeTypes.isEmpty()) {
        return {};
    }

    const ErrorString errorPrefix{
        QStringLiteral("can't get resource mime types for resource local ids")};

    QSqlQuery query{database};
    QString queryString;

    const bool singleMimeType = (resourceMimeTypes.size() == 1);
    if (singleMimeType) {
        bool res = query.prepare(
            QStringLiteral("SELECT resourceLocalUid FROM ResourceMimeFTS "
                           "WHERE mime MATCH :mimeTypes"));
        ENSURE_DB_REQUEST_RETURN(
            res, query, "local_storage::sql::utils",
            QStringLiteral(
                "Cannot get resource local ids by mime types: failed to "
                "prepare query"),
            {});

        QString mimeTypes = resourceMimeTypes.at(0);
        mimeTypes.prepend(QStringLiteral("\'"));
        mimeTypes.append(QStringLiteral("\'"));
        query.bindValue(QStringLiteral(":mimeTypes"), mimeTypes);
    }
    else {
        QTextStream strm{&queryString};

        bool someMimeTypeHasWhitespace = false;
        for (const auto & mimeType: resourceMimeTypes) {
            if (mimeType.contains(QStringLiteral(" "))) {
                someMimeTypeHasWhitespace = true;
                break;
            }
        }

        if (someMimeTypeHasWhitespace) {
            /**
             * Unfortunately, stardard SQLite at least from Qt 4.x has standard
             * query syntax for FTS which does not support whitespaces in search
             * terms and therefore MATCH function is simply inapplicable here,
             * have to use brute-force "equal to X1 or equal to X2 or ... equal
             * to XN
             */

            strm << "SELECT resourceLocalUid FROM Resources WHERE ";

            for (const auto & mimeType: resourceMimeTypes) {
                strm << "(mime = \'";
                strm << sqlEscape(mimeType);
                strm << "\')";
                if (&mimeType != &resourceMimeTypes.constLast()) {
                    strm << " OR ";
                }
            }
        }
        else {
            // For some reason statements like "MATCH 'x OR y'" don't work while
            // "SELECT ... MATCH 'x' UNION SELECT ... MATCH 'y'" work.

            for (const auto & mimeType: resourceMimeTypes) {
                strm << "SELECT resourceLocalUid FROM "
                     << "ResourceMimeFTS WHERE mime MATCH \'";
                strm << sqlEscape(mimeType);
                strm << "\'";
                if (&mimeType != &resourceMimeTypes.constLast()) {
                    strm << " UNION ";
                }
            }
        }
    }

    bool res = false;
    if (queryString.isEmpty()) {
        res = query.exec();
    }
    else {
        res = query.exec(queryString);
    }
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QStringLiteral("Cannot get resource local ids by mime types"), {});

    QStringList resourceLocalIds;
    resourceLocalIds.reserve(std::max(0, query.size()));

    while (query.next()) {
        QSqlRecord rec = query.record();
        const int index = rec.indexOf(QStringLiteral("resourceLocalUid"));
        if (Q_UNLIKELY(index < 0)) {
            errorDescription.setBase(QStringLiteral(
                "Cannot get resource local ids by mime types: resource local "
                "id is not present in the result of SQL query"));
            return {};
        }

        resourceLocalIds << rec.value(index).toString();
    }

    return resourceLocalIds;
}

} // namespace quentier::local_storage::sql::utils
