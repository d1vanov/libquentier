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

#include "ConnectionPool.h"
#include "ErrorHandling.h"
#include "Notifier.h"
#include "ResourcesHandler.h"
#include "Tasks.h"
#include "TypeChecks.h"

#include "utils/Common.h"
#include "utils/FillFromSqlRecordUtils.h"
#include "utils/PutToDatabaseUtils.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/RuntimeError.h>
#include <quentier/logging/QuentierLogger.h>

#include <utility/Threading.h>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include <utility/Qt5Promise.h>
#endif

#include <QSqlQuery>
#include <QSqlRecord>
#include <QThreadPool>

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

QFuture<void> ResourcesHandler::putResource(qevercloud::Resource resource)
{
    return makeWriteTask<void>(
        makeTaskContext(), weak_from_this(),
        [resource = std::move(resource)](
            const ResourcesHandler & handler, QSqlDatabase & database,
            ErrorString & errorDescription) {
            bool res = utils::putResource(resource, database, errorDescription);
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
                resourceLocalId, database, errorDescription);
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
                resourceGuid, database, errorDescription);
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
    else
    {
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
        errorDescription.setBase(
            QT_TRANSLATE_NOOP(
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
        errorDescription.setBase(
            QT_TRANSLATE_NOOP(
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
    const QString & resourceLocalId, QSqlDatabase & database,
    ErrorString & errorDescription) const
{
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
    resource.setLocalId(resourceLocalId);
    ErrorString error;
    if (!utils::fillResourceFromSqlRecord(record, resource, error)) {
        errorDescription.setBase(
            QT_TRANSLATE_NOOP(
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
                resourceLocalId, *resource.mutableAttributes(), database, errorDescription)) {
            return std::nullopt;
        }

        if (!findResourceAttributesApplicationDataFullMapByLocalId(
                resourceLocalId, *resource.mutableAttributes(), database, errorDescription)) {
            return std::nullopt;
        }
    }

    return resource;
}

} // namespace quentier::local_storage::sql
