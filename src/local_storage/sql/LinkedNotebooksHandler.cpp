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

#include "LinkedNotebooksHandler.h"
#include "ConnectionPool.h"
#include "ErrorHandling.h"
#include "Notifier.h"
#include "Task.h"
#include "TypeChecks.h"

#include "utils/FillFromSqlRecordUtils.h"
#include "utils/ListFromDatabaseUtils.h"
#include "utils/PutToDatabaseUtils.h"
#include "utils/ResourceDataFilesUtils.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/RuntimeError.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/threading/Future.h>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include <quentier/threading/Qt5Promise.h>
#endif

#include <QSqlQuery>
#include <QSqlRecord>
#include <QThreadPool>

#include <utility>

namespace quentier::local_storage::sql {

LinkedNotebooksHandler::LinkedNotebooksHandler(
    ConnectionPoolPtr connectionPool, Notifier * notifier,
    threading::QThreadPtr thread, const QString & localStorageDirPath) :
    m_connectionPool{std::move(connectionPool)},
    // clang-format off
    m_thread{std::move(thread)},
    m_localStorageDir{localStorageDirPath},
    m_notifier{notifier}
// clang-format on
{
    if (Q_UNLIKELY(!m_connectionPool)) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "LinkedNotebooksHandler ctor: connection pool is null")}};
    }

    if (Q_UNLIKELY(!m_notifier)) {
        throw InvalidArgument{ErrorString{
            QStringLiteral("LinkedNotebooksHandler ctor: notifier is null")}};
    }

    if (Q_UNLIKELY(!m_thread)) {
        throw InvalidArgument{ErrorString{
            QStringLiteral("LinkedNotebooksHandler ctor: thread is null")}};
    }

    if (Q_UNLIKELY(!m_localStorageDir.isReadable())) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "LinkedNotebooksHandler ctor: local storage dir is not "
            "readable")}};
    }

    if (Q_UNLIKELY( // NOLINT
            !m_localStorageDir.exists() &&
            !m_localStorageDir.mkpath(m_localStorageDir.absolutePath())))
    {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "LinkedNotebooksHandler ctor: local storage dir does not exist and "
            "cannot be created")}};
    }
}

QFuture<quint32> LinkedNotebooksHandler::linkedNotebookCount() const
{
    return makeReadTask<quint32>(
        makeTaskContext(), weak_from_this(),
        [](const LinkedNotebooksHandler & handler, QSqlDatabase & database,
           ErrorString & errorDescription) {
            return handler.linkedNotebookCountImpl(database, errorDescription);
        });
}

QFuture<void> LinkedNotebooksHandler::putLinkedNotebook(
    qevercloud::LinkedNotebook linkedNotebook)
{
    return makeWriteTask<void>(
        makeTaskContext(), weak_from_this(),
        [linkedNotebook = std::move(linkedNotebook)](
            LinkedNotebooksHandler & handler, QSqlDatabase & database,
            ErrorString & errorDescription) mutable {
            const bool res = utils::putLinkedNotebook(
                linkedNotebook, database, errorDescription);

            if (res) {
                handler.m_notifier->notifyLinkedNotebookPut(linkedNotebook);
            }

            return res;
        });
}

QFuture<std::optional<qevercloud::LinkedNotebook>>
    LinkedNotebooksHandler::findLinkedNotebookByGuid(
        qevercloud::Guid guid) const
{
    return makeReadTask<std::optional<qevercloud::LinkedNotebook>>(
        makeTaskContext(), weak_from_this(),
        [guid = std::move(guid)](
            const LinkedNotebooksHandler & handler, QSqlDatabase & database,
            ErrorString & errorDescription) {
            return handler.findLinkedNotebookByGuid(
                guid, database, errorDescription);
        });
}

QFuture<void> LinkedNotebooksHandler::expungeLinkedNotebookByGuid(
    qevercloud::Guid guid)
{
    return makeWriteTask<void>(
        makeTaskContext(), weak_from_this(),
        [guid = std::move(guid)](
            LinkedNotebooksHandler & handler, QSqlDatabase & database,
            ErrorString & errorDescription) {
            const bool res = handler.expungeLinkedNotebookByGuidImpl(
                guid, database, errorDescription);

            if (res) {
                handler.m_notifier->notifyLinkedNotebookExpunged(guid);
            }

            return res;
        });
}

QFuture<QList<qevercloud::LinkedNotebook>>
    LinkedNotebooksHandler::listLinkedNotebooks(
        ListLinkedNotebooksOptions options) const
{
    return makeReadTask<QList<qevercloud::LinkedNotebook>>(
        makeTaskContext(), weak_from_this(),
        [options](
            const LinkedNotebooksHandler & handler, QSqlDatabase & database,
            ErrorString & errorDescription) {
            return handler.listLinkedNotebooksImpl(
                options, database, errorDescription);
        });
}

std::optional<quint32> LinkedNotebooksHandler::linkedNotebookCountImpl(
    QSqlDatabase & database, ErrorString & errorDescription) const
{
    QSqlQuery query{database};
    const bool res =
        query.exec(QStringLiteral("SELECT COUNT(guid) FROM LinkedNotebooks"));

    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::LinkedNotebooksHandler",
        QStringLiteral(
            "Cannot count linked notebooks in the local storage database"),
        std::nullopt);

    if (!query.next()) {
        QNDEBUG(
            "local_storage::sql::LinkedNotebooksHandler",
            "Found no linked notebooks in the local storage database");
        return 0;
    }

    bool conversionResult = false;
    const int count = query.value(0).toInt(&conversionResult);
    if (Q_UNLIKELY(!conversionResult)) {
        errorDescription.setBase(QStringLiteral(
            "Cannot count linked notebooks in the local storage database: "
            "failed to convert linked notebook count to int"));
        QNWARNING("local_storage:sql", errorDescription);
        return std::nullopt;
    }

    return count;
}

std::optional<qevercloud::LinkedNotebook>
    LinkedNotebooksHandler::findLinkedNotebookByGuid(
        const qevercloud::Guid & guid, QSqlDatabase & database,
        ErrorString & errorDescription) const
{
    static const QString queryString = QStringLiteral(
        "SELECT guid, updateSequenceNumber, isDirty, "
        "shareName, username, shardId, "
        "sharedNotebookGlobalId, uri, noteStoreUrl, "
        "webApiUrlPrefix, stack, businessId "
        "FROM LinkedNotebooks WHERE guid = :guid");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::LinkedNotebooksHandler",
        QStringLiteral(
            "Cannot find linked notebook in the local storage database by "
            "guid: "
            "failed to prepare query"),
        std::nullopt);

    query.bindValue(QStringLiteral(":guid"), guid);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::LinkedNotebooksHandler",
        QStringLiteral(
            "Cannot find linked notebook in the local storage database by "
            "guid"),
        std::nullopt);

    if (!query.next()) {
        return std::nullopt;
    }

    const auto record = query.record();
    qevercloud::LinkedNotebook linkedNotebook;
    ErrorString error;
    if (!utils::fillLinkedNotebookFromSqlRecord(record, linkedNotebook, error))
    {
        errorDescription.setBase(QStringLiteral(
            "Failed to find linked notebook by local id in the local "
            "storage database"));
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING(
            "local_storage::sql::LinkedNotebooksHandler", errorDescription);
        return std::nullopt;
    }

    return linkedNotebook;
}

QStringList LinkedNotebooksHandler::listNoteLocalIdsByLinkedNotebookGuid(
    const qevercloud::Guid & linkedNotebookGuid, QSqlDatabase & database,
    ErrorString & errorDescription) const
{
    static const QString queryString = QStringLiteral(
        "SELECT localUid FROM Notes WHERE notebookLocalUid IN "
        "(SELECT localUid FROM Notebooks WHERE linkedNotebookGuid = "
        ":linkedNotebookGuid)");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::LinkedNotebooksHandler",
        QStringLiteral(
            "Cannot list note local ids by linked notebook guid from the local "
            "storage database: failed to prepare query"),
        {});

    query.bindValue(QStringLiteral(":linkedNotebookGuid"), linkedNotebookGuid);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::LinkedNotebooksHandler",
        QStringLiteral(
            "Cannot list note local ids by linked notebook guid from the local "
            "storage database"),
        {});

    QStringList noteLocalIds;
    noteLocalIds.reserve(std::max(query.size(), 0));

    while (query.next()) {
        noteLocalIds << query.value(0).toString();
    }

    return noteLocalIds;
}

bool LinkedNotebooksHandler::expungeLinkedNotebookByGuidImpl(
    const qevercloud::Guid & guid, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    QNDEBUG(
        "local_storage::sql::LinkedNotebooksHandler",
        "LinkedNotebooksHandler::expungeLinkedNotebookByGuid: guid = " << guid);

    Transaction transaction{database, Transaction::Type::Exclusive};

    const auto noteLocalIds =
        listNoteLocalIdsByLinkedNotebookGuid(guid, database, errorDescription);
    if (!errorDescription.isEmpty()) {
        return false;
    }

    static const QString queryString =
        QStringLiteral("DELETE FROM LinkedNotebooks WHERE guid = :guid");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::LinkedNotebooksHandler",
        QStringLiteral(
            "Cannot expunge linked notebook by guid from the local storage "
            "database: failed to prepare query"),
        false);

    query.bindValue(QStringLiteral(":guid"), guid);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::LinkedNotebooksHandler",
        QStringLiteral(
            "Cannot expunge linked notebook by guid from the local storage "
            "database"),
        false);

    res = transaction.commit();
    ENSURE_DB_REQUEST_RETURN(
        res, database, "local_storage::sql::LinkedNotebooksHandler",
        QStringLiteral(
            "Cannot expunge linked notebook by guid from the local storage "
            "database: failed to commit transaction"),
        false);

    for (const auto & noteLocalId: std::as_const(noteLocalIds)) {
        if (!utils::removeResourceDataFilesForNote(
                m_localStorageDir, noteLocalId, errorDescription))
        {
            return false;
        }
    }

    return true;
}

QList<qevercloud::LinkedNotebook>
    LinkedNotebooksHandler::listLinkedNotebooksImpl(
        const ListLinkedNotebooksOptions & options, QSqlDatabase & database,
        ErrorString & errorDescription) const
{
    return utils::listObjects<
        qevercloud::LinkedNotebook, ILocalStorage::ListLinkedNotebooksOrder>(
        options.m_filters, options.m_limit, options.m_offset, options.m_order,
        options.m_direction, {}, database, errorDescription);
}

TaskContext LinkedNotebooksHandler::makeTaskContext() const
{
    return TaskContext{
        m_thread, m_connectionPool,
        ErrorString{
            QStringLiteral("LinkedNotebooksHandler is already destroyed")},
        ErrorString{QStringLiteral("Request has been canceled")}};
}

} // namespace quentier::local_storage::sql
