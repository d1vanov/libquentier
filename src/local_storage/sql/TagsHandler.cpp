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
#include "TagsHandler.h"
#include "Tasks.h"
#include "Transaction.h"
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

#include <QSqlQuery>
#include <QThreadPool>

namespace quentier::local_storage::sql {

TagsHandler::TagsHandler(
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
                "local_storage::sql::TagsHandler",
                "TagsHandler ctor: connection pool is null")}};
    }

    if (Q_UNLIKELY(!m_threadPool)) {
        throw InvalidArgument{ErrorString{
            QT_TRANSLATE_NOOP(
                "local_storage::sql::TagsHandler",
                "TagsHandler ctor: thread pool is null")}};
    }

    if (Q_UNLIKELY(!m_notifier)) {
        throw InvalidArgument{ErrorString{
            QT_TRANSLATE_NOOP(
                "local_storage::sql::TagsHandler",
                "TagsHandler ctor: notifier is null")}};
    }

    if (Q_UNLIKELY(!m_writerThread)) {
        throw InvalidArgument{ErrorString{
            QT_TRANSLATE_NOOP(
                "local_storage::sql::TagsHandler",
                "TagsHandler ctor: writer thread is null")}};
    }
}

QFuture<quint32> TagsHandler::tagCount() const
{
    return makeReadTask<quint32>(
        makeTaskContext(),
        weak_from_this(),
        [](const TagsHandler & handler, QSqlDatabase & database,
           ErrorString & errorDescription)
        {
            return handler.tagCountImpl(database, errorDescription);
        });
}

QFuture<void> TagsHandler::putTag(qevercloud::Tag tag)
{
    return makeWriteTask<void>(
        makeTaskContext(),
        weak_from_this(),
        [tag = std::move(tag)]
        (TagsHandler & handler, QSqlDatabase & database,
         ErrorString & errorDescription) mutable
        {
            const bool res = utils::putTag(
                tag, database, errorDescription);

            if (res) {
                handler.m_notifier->notifyTagPut(tag);
            }

            return res;
        });
}

QFuture<qevercloud::Tag> TagsHandler::findTagByLocalId(
    QString localId) const
{
    return makeReadTask<qevercloud::Tag>(
        makeTaskContext(),
        weak_from_this(),
        [localId = std::move(localId)]
        (const TagsHandler & handler, QSqlDatabase & database,
         ErrorString & errorDescription)
        {
            return handler.findTagByLocalIdImpl(
                localId, database, errorDescription);
        });
}

QFuture<qevercloud::Tag> TagsHandler::findTagByGuid(
    qevercloud::Guid guid) const
{
    return makeReadTask<qevercloud::Tag>(
        makeTaskContext(),
        weak_from_this(),
        [guid = std::move(guid)]
        (const TagsHandler & handler, QSqlDatabase & database,
         ErrorString & errorDescription) mutable
        {
            return handler.findTagByGuidImpl(
                guid, database, errorDescription);
        });
}

QFuture<qevercloud::Tag> TagsHandler::findTagByName(
    QString name, std::optional<QString> linkedNotebookGuid) const
{
    return makeReadTask<qevercloud::Tag>(
        makeTaskContext(),
        weak_from_this(),
        [name = std::move(name),
         linkedNotebookGuid = std::move(linkedNotebookGuid)]
        (const TagsHandler & handler, QSqlDatabase & database,
         ErrorString & errorDescription) mutable
        {
            return handler.findTagByNameImpl(
                name, linkedNotebookGuid, database, errorDescription);
        });
}

QFuture<void> TagsHandler::expungeTagByLocalId(QString localId)
{
    return makeWriteTask<void>(
        makeTaskContext(),
        weak_from_this(),
        [localId = std::move(localId)]
        (TagsHandler & handler, QSqlDatabase & database,
         ErrorString & errorDescription)
        {
            const auto res = handler.expungeTagByLocalIdImpl(
                localId, database, errorDescription);

            if (res.status) {
                handler.m_notifier->notifyTagExpunged(
                    localId, res.expungedChildTagLocalIds);
            }

            return res.status;
        });
}

QFuture<void> TagsHandler::expungeTagByGuid(qevercloud::Guid guid)
{
    return makeWriteTask<void>(
        makeTaskContext(),
        weak_from_this(),
        [guid = std::move(guid)]
        (TagsHandler & handler, QSqlDatabase & database,
         ErrorString & errorDescription)
        {
            const auto res = handler.expungeTagByGuidImpl(
                guid, database, errorDescription);

            if (res.status) {
                handler.m_notifier->notifyTagExpunged(
                    res.expungedTagLocalId, res.expungedChildTagLocalIds);
            }

            return res.status;
        });
}

QFuture<void> TagsHandler::expungeTagByName(
    QString name, std::optional<QString> linkedNotebookGuid)
{
    return makeWriteTask<void>(
        makeTaskContext(),
        weak_from_this(),
        [name = std::move(name),
         linkedNotebookGuid = std::move(linkedNotebookGuid)]
        (TagsHandler & handler, QSqlDatabase & database,
         ErrorString & errorDescription)
        {
            const auto res = handler.expungeTagByNameImpl(
                name, linkedNotebookGuid, database, errorDescription);

            if (res.status) {
                handler.m_notifier->notifyTagExpunged(
                    res.expungedTagLocalId, res.expungedChildTagLocalIds);
            }

            return res.status;
        });
}

QFuture<QList<qevercloud::Tag>> TagsHandler::listTags(
    ListOptions<ListTagsOrder> options) const
{
    return makeReadTask<QList<qevercloud::Tag>>(
        makeTaskContext(),
        weak_from_this(),
        [options = std::move(options)]
        (const TagsHandler & handler, QSqlDatabase & database,
         ErrorString & errorDescription)
        {
            return handler.listTagsImpl(options, database, errorDescription);
        });
}

QFuture<QList<qevercloud::Tag>> TagsHandler::listTagsPerNoteLocalId(
    QString noteLocalId, ListOptions<ListTagsOrder> options) const
{
    return makeReadTask<QList<qevercloud::Tag>>(
        makeTaskContext(), weak_from_this(),
        [options = std::move(options), noteLocalId = std::move(noteLocalId)](
            const TagsHandler & handler, QSqlDatabase & database,
            ErrorString & errorDescription) {
            return handler.listTagsPerNoteLocalIdImpl(
                noteLocalId, options, database, errorDescription);
        });
}

std::optional<quint32> TagsHandler::tagCountImpl(
    QSqlDatabase & database, ErrorString & errorDescription) const
{
    // TODO: implement
    Q_UNUSED(database)
    Q_UNUSED(errorDescription)
    return std::nullopt;
}

std::optional<qevercloud::Tag> TagsHandler::findTagByLocalIdImpl(
    const QString & localId, QSqlDatabase & database,
    ErrorString & errorDescription) const
{
    // TODO: implement
    Q_UNUSED(localId)
    Q_UNUSED(database)
    Q_UNUSED(errorDescription)
    return std::nullopt;
}

std::optional<qevercloud::Tag> TagsHandler::findTagByGuidImpl(
    const qevercloud::Guid & guid, QSqlDatabase & database,
    ErrorString & errorDescription) const
{
    // TODO: implement
    Q_UNUSED(guid)
    Q_UNUSED(database)
    Q_UNUSED(errorDescription)
    return std::nullopt;
}

std::optional<qevercloud::Tag> TagsHandler::findTagByNameImpl(
    const QString & name, const std::optional<QString> & linkedNotebookGuid,
    QSqlDatabase & database, ErrorString & errorDescription) const
{
    // TODO: implement
    Q_UNUSED(name)
    Q_UNUSED(linkedNotebookGuid)
    Q_UNUSED(database)
    Q_UNUSED(errorDescription)
    return std::nullopt;
}

TagsHandler::ExpungeTagResult TagsHandler::expungeTagByLocalIdImpl(
    const QString & localId, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    // TODO: implement
    Q_UNUSED(localId)
    Q_UNUSED(database)
    Q_UNUSED(errorDescription)
    return {};
}

TagsHandler::ExpungeTagResult TagsHandler::expungeTagByGuidImpl(
    const qevercloud::Guid & guid, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    // TODO: implement
    Q_UNUSED(guid)
    Q_UNUSED(database)
    Q_UNUSED(errorDescription)
    return {};
}

TagsHandler::ExpungeTagResult TagsHandler::expungeTagByNameImpl(
    const QString & name, const std::optional<QString> & linkedNotebookGuid,
    QSqlDatabase & database, ErrorString & errorDescription)
{
    // TODO: implement
    Q_UNUSED(name)
    Q_UNUSED(linkedNotebookGuid)
    Q_UNUSED(database)
    Q_UNUSED(errorDescription)
    return {};
}

QList<qevercloud::Tag> TagsHandler::listTagsImpl(
    const ListOptions<ListTagsOrder> & options,
    QSqlDatabase & database, ErrorString & errorDescription) const
{
    // TODO: implement
    Q_UNUSED(options)
    Q_UNUSED(database)
    Q_UNUSED(errorDescription)
    return {};
}

QList<qevercloud::Tag> TagsHandler::listTagsPerNoteLocalIdImpl(
    const QString & noteLocalId, const ListOptions<ListTagsOrder> & options,
    QSqlDatabase & database, ErrorString & errorDescription) const
{
    // TODO: implement
    Q_UNUSED(noteLocalId)
    Q_UNUSED(options)
    Q_UNUSED(database)
    Q_UNUSED(errorDescription)
    return {};
}

TaskContext TagsHandler::makeTaskContext() const
{
    return TaskContext{
        m_threadPool,
        m_writerThread,
        m_connectionPool,
        ErrorString{QT_TRANSLATE_NOOP(
                "local_storage::sql::TagsHandler",
                "TagsHandler is already destroyed")},
        ErrorString{QT_TRANSLATE_NOOP(
                "local_storage::sql::TagsHandler",
                "Request has been canceled")}};
}

} // namespace quentier::local_storage::sql
