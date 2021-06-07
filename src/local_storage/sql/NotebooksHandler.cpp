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
#include "NotebooksHandler.h"

#include <quentier/exception/InvalidArgument.h>

#include <utility/Threading.h>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include <utility/Qt5Promise.h>
#endif

namespace quentier::local_storage::sql {

NotebooksHandler::NotebooksHandler(
    ConnectionPoolPtr connectionPool, QThreadPool * threadPool,
    QThreadPtr writerThread) :
    m_connectionPool{std::move(connectionPool)},
    m_threadPool{threadPool},
    m_writerThread{std::move(writerThread)}
{
    if (Q_UNLIKELY(!m_connectionPool)) {
        throw InvalidArgument{ErrorString{
            QT_TRANSLATE_NOOP(
                "local_storage::sql::NotebooksHandler",
                "NotebooksHandler ctor: connection pool is null")}};
    }

    if (Q_UNLIKELY(!m_threadPool)) {
        throw InvalidArgument{ErrorString{
            QT_TRANSLATE_NOOP(
                "local_storage::sql::NotebooksHandler",
                "NotebooksHandler ctor: thread pool is null")}};
    }

    if (Q_UNLIKELY(!m_writerThread)) {
        throw InvalidArgument{ErrorString{
            QT_TRANSLATE_NOOP(
                "local_storage::sql::NotebooksHandler",
                "NotebooksHandler ctor: writer thread is null")}};
    }
}

QFuture<quint32> NotebooksHandler::notebookCount() const
{
    // TODO: implement
    return utility::makeReadyFuture(0U);
}

QFuture<void> NotebooksHandler::putNotebook(qevercloud::Notebook notebook)
{
    // TODO: implement
    Q_UNUSED(notebook)
    return utility::makeReadyFuture();
}

QFuture<qevercloud::Notebook> NotebooksHandler::findNotebookByLocalId(
    QString localId) const
{
    // TODO: implement
    Q_UNUSED(localId)
    return utility::makeReadyFuture(qevercloud::Notebook{});
}

QFuture<qevercloud::Notebook> NotebooksHandler::findNotebookByGuid(
    qevercloud::Guid guid) const
{
    // TODO: implement
    Q_UNUSED(guid)
    return utility::makeReadyFuture(qevercloud::Notebook{});
}

QFuture<qevercloud::Notebook> NotebooksHandler::findNotebookByName(
    QString name) const
{
    // TODO: implement
    Q_UNUSED(name)
    return utility::makeReadyFuture(qevercloud::Notebook{});
}

QFuture<qevercloud::Notebook> NotebooksHandler::findDefaultNotebook() const
{
    // TODO: implement
    return utility::makeReadyFuture(qevercloud::Notebook{});
}

QFuture<void> NotebooksHandler::expungeNotebookByLocalId(QString localId)
{
    // TODO: implement
    Q_UNUSED(localId)
    return utility::makeReadyFuture();
}

QFuture<void> NotebooksHandler::expungeNotebookByGuid(qevercloud::Guid guid)
{
    // TODO: implement
    Q_UNUSED(guid)
    return utility::makeReadyFuture();
}

QFuture<void> NotebooksHandler::expungeNotebookByName(QString name)
{
    // TODO: implement
    Q_UNUSED(name)
    return utility::makeReadyFuture();
}

QFuture<QList<qevercloud::Notebook>> NotebooksHandler::listNotebooks(
    ListOptions<ListNotebooksOrder> options) const
{
    // TODO: implement
    Q_UNUSED(options)
    return utility::makeReadyFuture(QList<qevercloud::Notebook>{});
}

} // namespace quentier::local_storage::sql
