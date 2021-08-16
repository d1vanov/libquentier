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

std::optional<quint32> SavedSearchesHandler::savedSearchCountImpl(
    QSqlDatabase & database, ErrorString & errorDescription) const
{
    // TODO: implement
    Q_UNUSED(database)
    Q_UNUSED(errorDescription)
    return std::nullopt;
}

std::optional<qevercloud::SavedSearch>
    SavedSearchesHandler::findSavedSearchByLocalIdImpl(
        const QString & localId, QSqlDatabase & database,
        ErrorString & errorDescription) const
{
    // TODO: implement
    Q_UNUSED(localId)
    Q_UNUSED(database)
    Q_UNUSED(errorDescription)
    return std::nullopt;
}

bool SavedSearchesHandler::expungeSavedSearchByLocalIdImpl(
    const QString & localId, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    // TODO: implement
    Q_UNUSED(localId)
    Q_UNUSED(database)
    Q_UNUSED(errorDescription)
    return true;
}

QList<qevercloud::SavedSearch> SavedSearchesHandler::listSavedSearchesImpl(
    const ListOptions<ListSavedSearchesOrder> & options,
    QSqlDatabase & database, ErrorString & errorDescription) const
{
    Q_UNUSED(options)
    Q_UNUSED(database)
    Q_UNUSED(errorDescription)
    return {};
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
