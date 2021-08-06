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

#include "ResourcesHandler.h"
#include "ConnectionPool.h"
#include "ErrorHandling.h"
#include "Notifier.h"
#include "Tasks.h"
#include "TypeChecks.h"

#include "utils/Common.h"
#include "utils/FillFromSqlRecordUtils.h"
#include "utils/PutToDatabaseUtils.h"
#include "utils/ResourceDataFilesUtils.h"
#include "utils/ResourceUtils.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/RuntimeError.h>
#include <quentier/logging/QuentierLogger.h>

#include <utility/Threading.h>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include <utility/Qt5Promise.h>
#endif

#include <QCryptographicHash>
#include <QReadLocker>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QThreadPool>
#include <QWriteLocker>

namespace quentier::local_storage::sql {

ResourcesHandler::ResourcesHandler(
    ConnectionPoolPtr connectionPool, QThreadPool * threadPool,
    Notifier * notifier, QThreadPtr writerThread,
    const QString & localStorageDirPath) :
    m_connectionPool{std::move(connectionPool)},
    m_threadPool{threadPool}, m_notifier{notifier},
    m_writerThread{std::move(writerThread)}, m_localStorageDir{
                                                 localStorageDirPath}
{
    if (Q_UNLIKELY(!m_connectionPool)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "local_storage::sql::ResourcesHandler",
            "ResourcesHandler ctor: connection pool is null")}};
    }

    if (Q_UNLIKELY(!m_threadPool)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "local_storage::sql::ResourcesHandler",
            "ResourcesHandler ctor: thread pool is null")}};
    }

    if (Q_UNLIKELY(!m_notifier)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "local_storage::sql::ResourcesHandler",
            "ResourcesHandler ctor: notifier is null")}};
    }

    if (Q_UNLIKELY(!m_writerThread)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "local_storage::sql::ResourcesHandler",
            "ResourcesHandler ctor: writer thread is null")}};
    }

    if (Q_UNLIKELY(!m_localStorageDir.isReadable())) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "local_storage::sql::ResourcesHandler",
            "ResourcesHandler ctor: local storage dir is not readable")}};
    }

    if (Q_UNLIKELY(
            !m_localStorageDir.exists() &&
            !m_localStorageDir.mkpath(m_localStorageDir.absolutePath())))
    {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "local_storage::sql::ResourcesHandler",
            "ResourcesHandler ctor: local storage dir does not exist and "
            "cannot be created")}};
    }
}

QFuture<quint32> ResourcesHandler::resourceCount(NoteCountOptions options) const
{
    return makeReadTask<quint32>(
        makeTaskContext(), weak_from_this(),
        [options](
            const ResourcesHandler & handler, QSqlDatabase & database,
            ErrorString & errorDescription) {
            return handler.resourceCountImpl(
                options, database, errorDescription);
        });
}

QFuture<quint32> ResourcesHandler::resourceCountPerNoteLocalId(
    QString noteLocalId) const
{
    return makeReadTask<quint32>(
        makeTaskContext(), weak_from_this(),
        [noteLocalId = std::move(noteLocalId)](
            const ResourcesHandler & handler, QSqlDatabase & database,
            ErrorString & errorDescription) {
            return handler.resourceCountPerNoteLocalIdImpl(
                noteLocalId, database, errorDescription);
        });
}

QFuture<void> ResourcesHandler::putResource(
    qevercloud::Resource resource, const int indexInNote)
{
    return makeWriteTask<void>(
        makeTaskContext(), weak_from_this(),
        [this, resource = std::move(resource), indexInNote](
            const ResourcesHandler & handler, QSqlDatabase & database,
            ErrorString & errorDescription) mutable {
            QWriteLocker locker{&handler.m_resourceDataFilesLock};
            bool res = utils::putResource(
                m_localStorageDir, resource, indexInNote, database,
                errorDescription);
            if (res) {
                handler.m_notifier->notifyResourcePut(resource);
            }
            return res;
        });
}

QFuture<void> ResourcesHandler::putResourceMetadata(
    qevercloud::Resource resource, const int indexInNote)
{
    return makeWriteTask<void>(
        makeTaskContext(), weak_from_this(),
        [this, resource = std::move(resource), indexInNote](
            const ResourcesHandler & handler, QSqlDatabase & database,
            ErrorString & errorDescription) mutable {
            bool res = utils::putResource(
                m_localStorageDir, resource, indexInNote, database, errorDescription,
                utils::PutResourceBinaryDataOption::WithoutBinaryData);
            if (res) {
                handler.m_notifier->notifyResourcePut(resource);
            }
            return res;
        });
}

QFuture<qevercloud::Resource> ResourcesHandler::findResourceByLocalId(
    QString resourceLocalId, FetchResourceOptions options) const
{
    return makeReadTask<qevercloud::Resource>(
        makeTaskContext(), weak_from_this(),
        [resourceLocalId = std::move(resourceLocalId), options](
            const ResourcesHandler & handler, QSqlDatabase & database,
            ErrorString & errorDescription) {
            return handler.findResourceByLocalIdImpl(
                resourceLocalId, options, database, errorDescription);
        });
}

QFuture<qevercloud::Resource> ResourcesHandler::findResourceByGuid(
    qevercloud::Guid resourceGuid, FetchResourceOptions options) const
{
    return makeReadTask<qevercloud::Resource>(
        makeTaskContext(), weak_from_this(),
        [resourceGuid = std::move(resourceGuid), options](
            const ResourcesHandler & handler, QSqlDatabase & database,
            ErrorString & errorDescription) {
            return handler.findResourceByGuidImpl(
                resourceGuid, options, database, errorDescription);
        });
}

QFuture<void> ResourcesHandler::expungeResourceByLocalId(
    QString resourceLocalId)
{
    return makeWriteTask<void>(
        makeTaskContext(), weak_from_this(),
        [resourceLocalId = std::move(resourceLocalId)](
            ResourcesHandler & handler, QSqlDatabase & database,
            ErrorString & errorDescription) {
            QWriteLocker locker{&handler.m_resourceDataFilesLock};
            return handler.expungeResourceByLocalIdImpl(
                resourceLocalId, database, errorDescription);
        });
}

QFuture<void> ResourcesHandler::expungeResourceByGuid(
    qevercloud::Guid resourceGuid)
{
    return makeWriteTask<void>(
        makeTaskContext(), weak_from_this(),
        [resourceGuid = std::move(resourceGuid)](
            ResourcesHandler & handler, QSqlDatabase & database,
            ErrorString & errorDescription) {
            QWriteLocker locker{&handler.m_resourceDataFilesLock};
            return handler.expungeResourceByGuidImpl(
                resourceGuid, database, errorDescription);
        });
}

std::optional<quint32> ResourcesHandler::resourceCountImpl(
    NoteCountOptions noteCountOptions, QSqlDatabase & database,
    ErrorString & errorDescription) const
{
    QSqlQuery query{database};
    QString queryString;

    if (noteCountOptions.testFlag(NoteCountOption::IncludeDeletedNotes) &&
        noteCountOptions.testFlag(NoteCountOption::IncludeNonDeletedNotes))
    {
        queryString =
            QStringLiteral("SELECT COUNT(resourceLocalUid) FROM Resources");
    }
    else {
        queryString = QStringLiteral(
            "SELECT COUNT(resourceLocalUid) FROM Resources "
            "WHERE resourceLocalUid IN (SELECT resourceLocalUid "
            "FROM Resources LEFT OUTER JOIN Notes "
            "ON Resources.noteLocalUid = Notes.localUid "
            "WHERE Notes.deletionTimestamp IS ");

        if (noteCountOptions.testFlag(NoteCountOption::IncludeNonDeletedNotes))
        {
            queryString += QStringLiteral("NULL)");
        }
        else {
            queryString += QStringLiteral("NOT NULL)");
        }
    }

    const bool res = query.exec(queryString);
    ENSURE_DB_REQUEST_THROW(
        res, query, "local_storage::sql::ResourcesHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::ResourcesHandler",
            "Cannot count resources in the local storage database"));

    if (!query.next()) {
        QNDEBUG(
            "local_storage::sql::ResourcesHandler",
            "Found no resources corresponding to note count options "
            "in the local storage database");
        return 0;
    }

    bool conversionResult = false;
    const int count = query.value(0).toInt(&conversionResult);
    if (Q_UNLIKELY(!conversionResult)) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "local_storage::sql::ResourcesHandler",
            "Cannot count resources corresponding to note count options "
            "in the local storage database: failed to convert resource "
            "count to int"));
        QNWARNING("local_storage::sql::ResourcesHandler", errorDescription);
        return std::nullopt;
    }

    return count;
}

std::optional<quint32> ResourcesHandler::resourceCountPerNoteLocalIdImpl(
    const QString & noteLocalId, QSqlDatabase & database,
    ErrorString & errorDescription) const
{
    static const QString queryString = QStringLiteral(
        "SELECT COUNT(resourceLocalUid) FROM Resources LEFT OUTER JOIN Notes "
        "ON Resources.noteLocalUid = Notes.localUid "
        "WHERE Notes.localUid = :noteLocalUid");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_THROW(
        res, query, "local_storage::sql::ResourcesHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::ResourcesHandler",
            "Cannot count resources per note local id in the local storage "
            "database: failed to prepare query"));

    query.bindValue(QStringLiteral(":noteLocalUid"), noteLocalId);

    res = query.exec();
    ENSURE_DB_REQUEST_THROW(
        res, query, "local_storage::sql::ResourcesHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::ResourcesHandler",
            "Cannot count resources per note local id in the local storage "
            "database"));

    if (!query.next()) {
        QNDEBUG(
            "local_storage::sql::ResourcesHandler",
            "Found no resources corresponding to note local id "
            "in the local storage database");
        return 0;
    }

    bool conversionResult = false;
    const int count = query.value(0).toInt(&conversionResult);
    if (Q_UNLIKELY(!conversionResult)) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "local_storage::sql::ResourcesHandler",
            "Cannot count resources corresponding to note local id "
            "in the local storage database: failed to convert resource "
            "count to int"));
        QNWARNING("local_storage::sql::ResourcesHandler", errorDescription);
        return std::nullopt;
    }

    return count;
}

std::optional<qevercloud::Resource> ResourcesHandler::findResourceByLocalIdImpl(
    const QString & resourceLocalId, const FetchResourceOptions options,
    QSqlDatabase & database, ErrorString & errorDescription) const
{
    std::optional<QReadLocker> locker;
    if (options.testFlag(FetchResourceOption::WithBinaryData)) {
        locker.emplace(&m_resourceDataFilesLock);
    }

    utils::SelectTransactionGuard transactionGuard{database};

    static const QString queryString = QStringLiteral(
        "SELECT Resources.resourceLocalUid, resourceGuid, "
        "noteGuid, resourceUpdateSequenceNumber, resourceIsDirty, "
        "dataSize, dataHash, mime, width, height, recognitionDataSize, "
        "recognitionDataHash, alternateDataSize, alternateDataHash, "
        "resourceIndexInNote, resourceSourceURL, timestamp, "
        "resourceLatitude, resourceLongitude, resourceAltitude, "
        "cameraMake, cameraModel, clientWillIndex, fileName, "
        "attachment, resourceKey, resourceMapKey, resourceValue, "
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
        res, query, "local_storage::sql::ResourcesHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::ResourcesHandler",
            "Cannot find resource by local id in the local storage "
            "database: failed to prepare query"));

    query.bindValue(QStringLiteral(":resourceLocalUid"), resourceLocalId);

    res = query.exec();
    ENSURE_DB_REQUEST_THROW(
        res, query, "local_storage::sql::ResourcesHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::ResourcesHandler",
            "Cannot find resource by local id in the local storage database"));

    if (!query.next()) {
        return std::nullopt;
    }

    const auto record = query.record();
    qevercloud::Resource resource;
    ErrorString error;
    int indexInNote = -1;
    if (!utils::fillResourceFromSqlRecord(record, resource, indexInNote, error))
    {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "local_storage::sql::ResourcesHandler",
            "Failed to find resource by local id in the local storage "
            "database"));
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING("local_storage::sql::ResourcesHandler", errorDescription);
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

    if (!fillResourceData(resource, database, errorDescription)) {
        return std::nullopt;
    }

    return resource;
}

std::optional<qevercloud::Resource> ResourcesHandler::findResourceByGuidImpl(
    const qevercloud::Guid & resourceGuid, const FetchResourceOptions options,
    QSqlDatabase & database, ErrorString & errorDescription) const
{
    std::optional<QReadLocker> locker;
    if (options.testFlag(FetchResourceOption::WithBinaryData)) {
        locker.emplace(&m_resourceDataFilesLock);
    }

    utils::SelectTransactionGuard transactionGuard{database};

    static const QString queryString = QStringLiteral(
        "SELECT Resources.resourceLocalUid, resourceGuid, "
        "noteGuid, resourceUpdateSequenceNumber, resourceIsDirty, "
        "dataSize, dataHash, mime, width, height, recognitionDataSize, "
        "recognitionDataHash, alternateDataSize, alternateDataHash, "
        "resourceIndexInNote, resourceSourceURL, timestamp, "
        "resourceLatitude, resourceLongitude, resourceAltitude, "
        "cameraMake, cameraModel, clientWillIndex, fileName, "
        "attachment, resourceKey, resourceMapKey, resourceValue, "
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
        res, query, "local_storage::sql::ResourcesHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::ResourcesHandler",
            "Cannot find resource by guid in the local storage "
            "database: failed to prepare query"));

    query.bindValue(QStringLiteral(":resourceGuid"), resourceGuid);

    res = query.exec();
    ENSURE_DB_REQUEST_THROW(
        res, query, "local_storage::sql::ResourcesHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::ResourcesHandler",
            "Cannot find resource by guid in the local storage database"));

    if (!query.next()) {
        return std::nullopt;
    }

    const auto record = query.record();
    qevercloud::Resource resource;
    ErrorString error;
    int indexInNote = -1;
    if (!utils::fillResourceFromSqlRecord(record, resource, indexInNote, error))
    {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "local_storage::sql::ResourcesHandler",
            "Failed to find resource by guid in the local storage "
            "database"));
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING("local_storage::sql::ResourcesHandler", errorDescription);
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

    if (!fillResourceData(resource, database, errorDescription)) {
        return std::nullopt;
    }

    return resource;
}

bool ResourcesHandler::fillResourceData(
    qevercloud::Resource & resource, QSqlDatabase & database,
    ErrorString & errorDescription) const
{
    const QString & resourceLocalId = resource.localId();

    QString resourceDataBodyVersionId;
    if (!utils::findResourceDataBodyVersionId(
            resourceLocalId, database, resourceDataBodyVersionId,
            errorDescription))
    {
        return false;
    }

    QString resourceAlternateDataBodyVersionId;
    if (!utils::findResourceAlternateDataBodyVersionId(
            resourceLocalId, database, resourceAlternateDataBodyVersionId,
            errorDescription))
    {
        return false;
    }

    if (!resourceDataBodyVersionId.isEmpty()) {
        QByteArray resourceDataBody;
        if (!utils::readResourceDataBodyFromFile(
                m_localStorageDir, resource.noteLocalId(), resourceLocalId,
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
        if (!utils::readResourceAlternateDataBodyFromFile(
                m_localStorageDir, resource.noteLocalId(), resourceLocalId,
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

bool ResourcesHandler::findResourceAttributesApplicationDataKeysOnlyByLocalId(
    const QString & localId, qevercloud::ResourceAttributes & attributes,
    QSqlDatabase & database, ErrorString & errorDescription) const
{
    static const QString queryString = QStringLiteral(
        "SELECT resourceKey FROM ResourceAttributesApplicationDataKeysOnly "
        "WHERE resourceLocalUid = :resourceLocalUid");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::ResourcesHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::ResourcesHandler",
            "Cannot find resource application data keys only part in the local "
            "storage database: failed to prepare query"),
        false);

    query.bindValue(QStringLiteral(":resourceLocalUid"), localId);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::ResourcesHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::ResourcesHandler",
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

bool ResourcesHandler::findResourceAttributesApplicationDataFullMapByLocalId(
    const QString & localId, qevercloud::ResourceAttributes & attributes,
    QSqlDatabase & database, ErrorString & errorDescription) const
{
    static const QString queryString = QStringLiteral(
        "SELECT resourceMapKey, resourceValue "
        "FROM ResourceAttributesApplicationDataFullMap "
        "WHERE resourceLocalUid = :resourceLocalUid");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::ResourcesHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::ResourcesHandler",
            "Cannot find resource application data full map part in the local "
            "storage database: failed to prepare query"),
        false);

    query.bindValue(QStringLiteral(":resourceLocalUid"), localId);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::ResourcesHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::ResourcesHandler",
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

bool ResourcesHandler::expungeResourceByLocalIdImpl(
    const QString & localId, QSqlDatabase & database,
    ErrorString & errorDescription, std::optional<Transaction> transaction)
{
    if (!transaction) {
        transaction.emplace(database, Transaction::Type::Exclusive);
    }

    const auto noteLocalId = utils::noteLocalIdByResourceLocalId(
        localId, database, errorDescription);

    if (!errorDescription.isEmpty()) {
        return false;
    }

    static const QString queryString = QStringLiteral(
        "DELETE FROM Resources WHERE resourceLocalUid = :resourceLocalUid");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::ResourcesHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::ResourcesHandler",
            "Cannot expunge resource from the local storage database: "
            "failed to prepare query"),
        false);

    query.bindValue(QStringLiteral(":resourceLocalUid"), localId);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::ResourcesHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::ResourcesHandler",
            "Cannot expunge resource from the local storage database"),
        false);

    res = transaction->commit();
    ENSURE_DB_REQUEST_RETURN(
        res, database, "local_storage::sql::ResourcesHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::ResourcesHandler",
            "Cannot expunge resource from the local storage database, failed "
            "to commit transaction"),
        false);

    if (!utils::removeResourceDataFiles(
            m_localStorageDir, noteLocalId, localId, errorDescription))
    {
        QNWARNING("local_storage::sql::ResourcesHandler", errorDescription);
    }

    return true;
}

bool ResourcesHandler::expungeResourceByGuidImpl(
    const qevercloud::Guid & guid, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    Transaction transaction{database, Transaction::Type::Exclusive};

    const auto localId =
        utils::resourceLocalIdByGuid(guid, database, errorDescription);

    if (!errorDescription.isEmpty()) {
        return false;
    }

    return expungeResourceByLocalIdImpl(
        localId, database, errorDescription, std::move(transaction));
}

TaskContext ResourcesHandler::makeTaskContext() const
{
    return TaskContext{
        m_threadPool, m_writerThread, m_connectionPool,
        ErrorString{QT_TRANSLATE_NOOP(
            "local_storage::sql::ResourcessHandler",
            "ResourcesHandler is already destroyed")},
        ErrorString{QT_TRANSLATE_NOOP(
            "local_storage::sql::ResourcesHandler",
            "Request has been canceled")}};
}

} // namespace quentier::local_storage::sql
