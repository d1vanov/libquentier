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
#include "SavedSearchesHandler.h"
#include "Tasks.h"
#include "TypeChecks.h"

#include "utils/FillFromSqlRecordUtils.h"
#include "utils/ListFromDatabaseUtils.h"
#include "utils/PutToDatabaseUtils.h"
#include "utils/SavedSearchUtils.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/RuntimeError.h>
#include <quentier/logging/QuentierLogger.h>

#include <utility/Threading.h>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include <utility/Qt5Promise.h>
#endif

#include <QSqlRecord>
#include <QSqlQuery>
#include <QThreadPool>

namespace quentier::local_storage::sql {

SavedSearchesHandler::SavedSearchesHandler(
    ConnectionPoolPtr connectionPool, QThreadPool * threadPool,
    Notifier * notifier, QThreadPtr writerThread) :
    m_connectionPool{std::move(connectionPool)},
    m_threadPool{threadPool},
    m_notifier{notifier},
    m_writerThread{std::move(writerThread)}
{
    if (Q_UNLIKELY(!m_connectionPool)) {
        throw InvalidArgument{ErrorString{
            QT_TRANSLATE_NOOP(
                "local_storage::sql::SavedSearchesHandler",
                "SavedSearchesHandler ctor: connection pool is null")}};
    }

    if (Q_UNLIKELY(!m_threadPool)) {
        throw InvalidArgument{ErrorString{
            QT_TRANSLATE_NOOP(
                "local_storage::sql::SavedSearchesHandler",
                "SavedSearchesHandler ctor: thread pool is null")}};
    }

    if (Q_UNLIKELY(!m_notifier)) {
        throw InvalidArgument{ErrorString{
            QT_TRANSLATE_NOOP(
                "local_storage::sql::SavedSearchesHandler",
                "SavedSearchesHandler ctor: notifier is null")}};
    }

    if (Q_UNLIKELY(!m_writerThread)) {
        throw InvalidArgument{ErrorString{
            QT_TRANSLATE_NOOP(
                "local_storage::sql::SavedSearchesHandler",
                "SavedSearchesHandler ctor: writer thread is null")}};
    }
}

QFuture<quint32> SavedSearchesHandler::savedSearchCount() const
{
    return makeReadTask<quint32>(
        makeTaskContext(),
        weak_from_this(),
        [](const SavedSearchesHandler & handler, QSqlDatabase & database,
           ErrorString & errorDescription)
        {
            return handler.savedSearchCountImpl(database, errorDescription);
        });
}

QFuture<void> SavedSearchesHandler::putSavedSearch(
    qevercloud::SavedSearch savedSearch)
{
    return makeWriteTask<void>(
        makeTaskContext(),
        weak_from_this(),
        [savedSearch = std::move(savedSearch)]
        (SavedSearchesHandler & handler, QSqlDatabase & database,
         ErrorString & errorDescription) mutable
        {
            const bool res = utils::putSavedSearch(
                savedSearch, database, errorDescription);

            if (res) {
                handler.m_notifier->notifySavedSearchPut(savedSearch);
            }

            return res;
        });
}

QFuture<qevercloud::SavedSearch> SavedSearchesHandler::findSavedSearchByLocalId(
    QString localId) const
{
    return makeReadTask<qevercloud::SavedSearch>(
        makeTaskContext(),
        weak_from_this(),
        [localId = std::move(localId)]
        (const SavedSearchesHandler & handler, QSqlDatabase & database,
         ErrorString & errorDescription)
        {
            return handler.findSavedSearchByLocalIdImpl(
                localId, database, errorDescription);
        });
}

QFuture<qevercloud::SavedSearch> SavedSearchesHandler::findSavedSearchByGuid(
    qevercloud::Guid guid) const
{
    return makeReadTask<qevercloud::SavedSearch>(
        makeTaskContext(),
        weak_from_this(),
        [guid = std::move(guid)]
        (const SavedSearchesHandler & handler, QSqlDatabase & database,
         ErrorString & errorDescription)
        {
            return handler.findSavedSearchByGuidImpl(
                guid, database, errorDescription);
        });
}

QFuture<QList<qevercloud::SavedSearch>> SavedSearchesHandler::listSavedSearches(
    ListOptions<ListSavedSearchesOrder> options) const
{
    return makeReadTask<QList<qevercloud::SavedSearch>>(
        makeTaskContext(),
        weak_from_this(),
        [options = std::move(options)]
        (const SavedSearchesHandler & handler, QSqlDatabase & database,
         ErrorString & errorDescription)
        {
            return handler.listSavedSearchesImpl(
                options, database, errorDescription);
        });
}

QFuture<void> SavedSearchesHandler::expungeSavedSearchByLocalId(
    QString localId)
{
    return makeWriteTask<void>(
        makeTaskContext(),
        weak_from_this(),
        [localId = std::move(localId)]
        (SavedSearchesHandler & handler, QSqlDatabase & database,
         ErrorString & errorDescription)
        {
            const bool res = handler.expungeSavedSearchByLocalIdImpl(
                localId, database, errorDescription);

            if (res) {
                handler.m_notifier->notifySavedSearchExpunged(localId);
            }

            return res;
        });
}

QFuture<void> SavedSearchesHandler::expungeSavedSearchByGuid(
    qevercloud::Guid guid)
{
    return makeWriteTask<void>(
        makeTaskContext(),
        weak_from_this(),
        [guid = std::move(guid)]
        (SavedSearchesHandler & handler, QSqlDatabase & database,
         ErrorString & errorDescription)
        {
            return handler.expungeSavedSearchByGuidImpl(
                guid, database, errorDescription);
        });
}

std::optional<quint32> SavedSearchesHandler::savedSearchCountImpl(
    QSqlDatabase & database, ErrorString & errorDescription) const
{
    static const QString queryString =
        QStringLiteral("SELECT COUNT(localUid) FROM SavedSearches");

    QSqlQuery query{database};
    const bool res = query.exec(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::SavedSearchesHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::SavedSearchesHandler",
            "Cannot count saved searches in the local storage database"),
        std::nullopt);

    if (!query.next()) {
        QNDEBUG(
            "local_storage::sql::SavedSearchesHandler",
            "Found no saved searches in the local storage database");
        return 0;
    }

    bool conversionResult = false;
    const int count = query.value(0).toInt(&conversionResult);
    if (Q_UNLIKELY(!conversionResult)) {
        errorDescription.setBase(
            QT_TRANSLATE_NOOP(
                "local_storage::sql::SavedSearchesHandler",
                "Cannot count saved searches in the local storage database: "
                "failed to convert saved search count to int"));
        QNWARNING("local_storage::sql::SavedSearchesHandler", errorDescription);
        return std::nullopt;
    }

    return count;
}

std::optional<qevercloud::SavedSearch>
    SavedSearchesHandler::findSavedSearchByLocalIdImpl(
        const QString & localId, QSqlDatabase & database,
        ErrorString & errorDescription) const
{
    static const QString queryString = QStringLiteral(
        "SELECT localUid, guid, name, query, format, "
        "updateSequenceNumber, isDirty, isLocal, "
        "includeAccount, includePersonalLinkedNotebooks, "
        "includeBusinessLinkedNotebooks, isFavorited FROM "
        "SavedSearches WHERE localUid = :localUid");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::SavedSearchesHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::SavedSearchesHandler",
            "Cannot find saved search in the local storage database by local "
            "id: failed to prepare query"),
        std::nullopt);

    query.bindValue(QStringLiteral(":localUid"), localId);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::SavedSearchesHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::SavedSearchesHandler",
            "Cannot find saved search in the local storage database by local "
            "id"),
        std::nullopt);

    if (!query.next()) {
        return std::nullopt;
    }

    const auto record = query.record();
    qevercloud::SavedSearch savedSearch;
    ErrorString error;
    if (!utils::fillSavedSearchFromSqlRecord(record, savedSearch, error)) {
        errorDescription.setBase(
            QT_TRANSLATE_NOOP(
                "local_storage::sql::SavedSearchesHandler",
                "Failed to find saved search by local id in the local storage "
                "database"));
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING("local_storage::sql::SavedSearchesHandler", errorDescription);
        return std::nullopt;
    }

    return savedSearch;
}

std::optional<qevercloud::SavedSearch>
    SavedSearchesHandler::findSavedSearchByGuidImpl(
        const qevercloud::Guid & guid, QSqlDatabase & database,
        ErrorString & errorDescription) const
{
    static const QString queryString = QStringLiteral(
        "SELECT localUid, guid, name, query, format, "
        "updateSequenceNumber, isDirty, isLocal, "
        "includeAccount, includePersonalLinkedNotebooks, "
        "includeBusinessLinkedNotebooks, isFavorited FROM "
        "SavedSearches WHERE guid = :guid");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::SavedSearchesHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::SavedSearchesHandler",
            "Cannot find saved search in the local storage database by guid: "
            "failed to prepare query"),
        std::nullopt);

    query.bindValue(QStringLiteral(":guid"), guid);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::SavedSearchesHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::SavedSearchesHandler",
            "Cannot find saved search in the local storage database by guid"),
        std::nullopt);

    if (!query.next()) {
        return std::nullopt;
    }

    const auto record = query.record();
    qevercloud::SavedSearch savedSearch;
    ErrorString error;
    if (!utils::fillSavedSearchFromSqlRecord(record, savedSearch, error)) {
        errorDescription.setBase(
            QT_TRANSLATE_NOOP(
                "local_storage::sql::SavedSearchesHandler",
                "Failed to find saved search by guid in the local storage "
                "database"));
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING("local_storage::sql::SavedSearchesHandler", errorDescription);
        return std::nullopt;
    }

    return savedSearch;
}

bool SavedSearchesHandler::expungeSavedSearchByLocalIdImpl(
    const QString & localId, QSqlDatabase & database,
    ErrorString & errorDescription, std::optional<Transaction> transaction)
{
    static const QString queryString = QStringLiteral(
        "DELETE FROM SavedSearches WHERE localUid=:localUid");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::SavedSearchesHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::SavedSearchesHandler",
            "Cannot expunge saved search from the local storage database by "
            "local id: failed to prepare query"),
        false);

    query.bindValue(QStringLiteral(":localUid"), localId);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::SavedSearchesHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::SavedSearchesHandler",
            "Cannot expunge saved search from the local storage database by "
            "local id"),
        false);

    if (transaction) {
        res = transaction->commit();
        ENSURE_DB_REQUEST_RETURN(
            res, database, "local_storage::sql::SavedSearchesHandler",
            QT_TRANSLATE_NOOP(
                "local_storage::sql::SavedSearchesHandler",
                "Cannot expunge saved search from the local storage database "
                "by local id: failed to commit transaction"),
            false);
    }

    return true;
}

bool SavedSearchesHandler::expungeSavedSearchByGuidImpl(
    const qevercloud::Guid & guid, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    QNDEBUG(
        "local_storage::sql::SavedSearchesHandler",
        "SavedSearchesHandler::expungeSavedSearchByGuidImpl: guid = " << guid);

    Transaction transaction{database, Transaction::Type::Exclusive};

    const auto localId =
        utils::savedSearchLocalIdByGuid(guid, database, errorDescription);

    if (!errorDescription.isEmpty()) {
        return false;
    }

    if (localId.isEmpty()) {
        QNDEBUG(
            "local_storage::sql::SavedSearchesHandler",
            "Found no saved search local id for guid " << guid);
        return true;
    }

    QNDEBUG(
        "local_storage::sql::SavedSearchesHandler",
        "Found saved search local id for guid " << guid << ": " << localId);

    const bool res = expungeSavedSearchByLocalIdImpl(
        localId, database, errorDescription, std::move(transaction));

    if (res) {
        m_notifier->notifySavedSearchExpunged(localId);
    }

    return res;
}

QList<qevercloud::SavedSearch> SavedSearchesHandler::listSavedSearchesImpl(
    const ListOptions<ListSavedSearchesOrder> & options,
    QSqlDatabase & database, ErrorString & errorDescription) const
{
    return utils::listObjects<
        qevercloud::SavedSearch, ILocalStorage::ListSavedSearchesOrder>(
            options.m_flags, options.m_limit, options.m_offset, options.m_order,
            options.m_direction, QString{}, database, errorDescription);
}

TaskContext SavedSearchesHandler::makeTaskContext() const
{
    return TaskContext{
        m_threadPool,
        m_writerThread,
        m_connectionPool,
        ErrorString{QT_TRANSLATE_NOOP(
                "local_storage::sql::SavedSearchesHandler",
                "SavedSearchesHandler is already destroyed")},
        ErrorString{QT_TRANSLATE_NOOP(
                "local_storage::sql::SavedSearchesHandler",
                "Request has been canceled")}};
}

} // namespace quentier::local_storage::sql
