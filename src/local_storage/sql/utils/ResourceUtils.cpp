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

#include "Common.h"
#include "FillFromSqlRecordUtils.h"
#include "ResourceDataFilesUtils.h"
#include "ResourceUtils.h"

#include "../ErrorHandling.h"

#include <quentier/types/ErrorString.h>

#include <qevercloud/types/Resource.h>

#include <QCryptographicHash>
#include <QSqlRecord>
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
        QNDEBUG(
            "local_storage::sql::utils",
            "Could not find resource local id corresponding to resource guid "
                << resourceGuid);
        return {};
    }

    return query.value(0).toString();
}

std::optional<qevercloud::Resource> findResourceByLocalId(
    const QString & resourceLocalId, const FetchResourceOptions options,
    const QDir & localStorageDir, QSqlDatabase & database,
    ErrorString & errorDescription,
    const TransactionOption transactionOption)
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
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot find resource by local id in the local storage "
            "database: failed to prepare query"));

    query.bindValue(QStringLiteral(":resourceLocalUid"), resourceLocalId);

    res = query.exec();
    ENSURE_DB_REQUEST_THROW(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot find resource by local id in the local storage database"));

    if (!query.next()) {
        return std::nullopt;
    }

    const auto record = query.record();
    qevercloud::Resource resource;
    ErrorString error;
    int indexInNote = -1;
    if (!fillResourceFromSqlRecord(record, resource, indexInNote, error))
    {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Failed to find resource by local id in the local storage "
            "database"));
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING("local_storage::sql::utils", errorDescription);
        return std::nullopt;
    }

    if (resource.attributes()) {
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
            resource, localStorageDir, database, errorDescription)) {
        return std::nullopt;
    }

    return resource;
}

std::optional<qevercloud::Resource> findResourceByGuid(
    const qevercloud::Guid & resourceGuid, const FetchResourceOptions options,
    const QDir & localStorageDir,
    QSqlDatabase & database, ErrorString & errorDescription,
    const TransactionOption transactionOption)
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
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot find resource by guid in the local storage "
            "database: failed to prepare query"));

    query.bindValue(QStringLiteral(":resourceGuid"), resourceGuid);

    res = query.exec();
    ENSURE_DB_REQUEST_THROW(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot find resource by guid in the local storage database"));

    if (!query.next()) {
        return std::nullopt;
    }

    const auto record = query.record();
    qevercloud::Resource resource;
    ErrorString error;
    int indexInNote = -1;
    if (!fillResourceFromSqlRecord(record, resource, indexInNote, error))
    {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
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
            resource, localStorageDir, database, errorDescription)) {
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
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot find resource application data keys only part in the local "
            "storage database: failed to prepare query"),
        false);

    query.bindValue(QStringLiteral(":resourceLocalUid"), localId);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot find resource application data keys only part in the local "
            "storage database"),
        false);

    if (!query.next()) {
        return true;
    }

    if (!attributes.applicationData()) {
        attributes.setApplicationData(qevercloud::LazyMap{});
    }

    auto & appData = *attributes.mutableApplicationData();
    if (!appData.keysOnly()) {
        appData.setKeysOnly(QSet<QString>{});
    }

    appData.mutableKeysOnly()->insert(query.value(0).toString());
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
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot find resource application data full map part in the local "
            "storage database: failed to prepare query"),
        false);

    query.bindValue(QStringLiteral(":resourceLocalUid"), localId);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot find resource application data full map part in the local "
            "storage database"),
        false);

    if (!query.next()) {
        return true;
    }

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
        record.value(keyIndex).toString(), record.value(valueIndex).toString());

    return true;
}

} // namespace quentier::local_storage::sql::utils
