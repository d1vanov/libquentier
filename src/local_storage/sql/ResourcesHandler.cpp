/*
 * Copyright 2021-2022 Dmitry Ivanov
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
#include "utils/ResourceDataFilesUtils.h"
#include "utils/ResourceUtils.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/RuntimeError.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/threading/Future.h>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include <quentier/threading/Qt5Promise.h>
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
    Notifier * notifier, threading::QThreadPtr writerThread,
    const QString & localStorageDirPath,
    QReadWriteLockPtr resourceDataFilesLock) :
    m_connectionPool{std::move(connectionPool)},
    m_threadPool{threadPool}, m_notifier{notifier}, m_writerThread{std::move(
                                                        writerThread)},
    m_localStorageDir{localStorageDirPath}, m_resourceDataFilesLock{std::move(
                                                resourceDataFilesLock)}
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

    if (Q_UNLIKELY(!m_resourceDataFilesLock)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "local_storage::sql::ResourcesHandler",
            "ResourcesHandler ctor: resource data files lock is null")}};
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
    qevercloud::Resource resource)
{
    return makeWriteTask<void>(
        makeTaskContext(), weak_from_this(),
        [this, resource = std::move(resource)](
            const ResourcesHandler & handler, QSqlDatabase & database,
            ErrorString & errorDescription) mutable {
            QWriteLocker locker{handler.m_resourceDataFilesLock.get()};
            bool res = utils::putResource(
                m_localStorageDir, resource, database, errorDescription);
            if (res) {
                handler.m_notifier->notifyResourcePut(resource);
            }
            return res;
        });
}

QFuture<void> ResourcesHandler::putResourceMetadata(
    qevercloud::Resource resource)
{
    return makeWriteTask<void>(
        makeTaskContext(), weak_from_this(),
        [this, resource = std::move(resource)](
            const ResourcesHandler & handler, QSqlDatabase & database,
            ErrorString & errorDescription) mutable {
            bool res = utils::putResource(
                m_localStorageDir, resource, database, errorDescription,
                utils::PutResourceBinaryDataOption::WithoutBinaryData);
            if (res) {
                handler.m_notifier->notifyResourceMetadataPut(resource);
            }
            return res;
        });
}

QFuture<std::optional<qevercloud::Resource>>
    ResourcesHandler::findResourceByLocalId(
        QString resourceLocalId, FetchResourceOptions options) const
{
    return makeReadTask<std::optional<qevercloud::Resource>>(
        makeTaskContext(), weak_from_this(),
        [resourceLocalId = std::move(resourceLocalId), options](
            const ResourcesHandler & handler, QSqlDatabase & database,
            ErrorString & errorDescription) {
            utils::FetchResourceOptions resourceOptions;
            std::optional<QReadLocker> locker;
            if (options.testFlag(FetchResourceOption::WithBinaryData)) {
                resourceOptions.setFlag(
                    utils::FetchResourceOption::WithBinaryData);
                locker.emplace(handler.m_resourceDataFilesLock.get());
            }
            int indexInNote = -1;
            return utils::findResourceByLocalId(
                resourceLocalId, resourceOptions, handler.m_localStorageDir,
                indexInNote, database, errorDescription);
        });
}

QFuture<std::optional<qevercloud::Resource>>
    ResourcesHandler::findResourceByGuid(
        qevercloud::Guid resourceGuid, FetchResourceOptions options) const
{
    return makeReadTask<std::optional<qevercloud::Resource>>(
        makeTaskContext(), weak_from_this(),
        [resourceGuid = std::move(resourceGuid), options](
            const ResourcesHandler & handler, QSqlDatabase & database,
            ErrorString & errorDescription) {
            utils::FetchResourceOptions resourceOptions;
            std::optional<QReadLocker> locker;
            if (options.testFlag(FetchResourceOption::WithBinaryData)) {
                resourceOptions.setFlag(
                    utils::FetchResourceOption::WithBinaryData);
                locker.emplace(handler.m_resourceDataFilesLock.get());
            }
            int indexInNote = -1;
            return utils::findResourceByGuid(
                resourceGuid, resourceOptions, handler.m_localStorageDir,
                indexInNote, database, errorDescription);
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
            QWriteLocker locker{handler.m_resourceDataFilesLock.get()};
            const bool res = handler.expungeResourceByLocalIdImpl(
                resourceLocalId, database, errorDescription);
            if (res) {
                handler.m_notifier->notifyResourceExpunged(resourceLocalId);
            }
            return res;
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
            QWriteLocker locker{handler.m_resourceDataFilesLock.get()};
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

bool ResourcesHandler::expungeResourceByLocalIdImpl(
    const QString & localId, QSqlDatabase & database,
    ErrorString & errorDescription, std::optional<Transaction> transaction)
{
    if (!transaction) {
        transaction.emplace(database, Transaction::Type::Exclusive);
    }

    ErrorString error;
    const auto noteLocalId =
        utils::noteLocalIdByResourceLocalId(localId, database, error);

    if (noteLocalId.isEmpty() && !error.isEmpty()) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "local_storage::sql::ResourcesHandler",
            "Cannot expunge resource from the local storage database"));
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING("local_storage::sql::ResourcesHandler", errorDescription);
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

    if (localId.isEmpty()) {
        return errorDescription.isEmpty();
    }

    const bool res = expungeResourceByLocalIdImpl(
        localId, database, errorDescription, std::move(transaction));

    if (res) {
        m_notifier->notifyResourceExpunged(localId);
    }

    return res;
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
