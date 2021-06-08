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
#include <quentier/exception/RuntimeError.h>

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
    auto promise = std::make_shared<QPromise<quint32>>();
    auto future = promise->future();

    promise->start();

    auto * runnable = utility::createFunctionRunnable(
        [promise = std::move(promise),
         self_weak = weak_from_this()]
         {
             const auto self = self_weak.lock();
             if (!self) {
                 promise->setException(RuntimeError(ErrorString{
                     QT_TRANSLATE_NOOP(
                         "local_storage::sql::NotebooksHandler",
                         "NotebooksHandler is already destroyed")}));
                 promise->finish();
                 return;
             }

             auto databaseConnection = self->m_connectionPool->database();

             ErrorString errorDescription;
             const auto userCount = self->notebookCountImpl(
                 databaseConnection, errorDescription);

             if (!userCount) {
                 promise->setException(
                     DatabaseRequestException{errorDescription});
                 promise->finish();
                 return;
             }

             promise->addResult(*userCount);
             promise->finish();
         });

    m_threadPool->start(runnable);
    return future;
}

QFuture<void> NotebooksHandler::putNotebook(qevercloud::Notebook notebook)
{
    auto promise = std::make_shared<QPromise<void>>();
    auto future = promise->future();

    promise->start();

    utility::postToThread(
        m_writerThread.get(),
        [promise = std::move(promise),
         self_weak = weak_from_this(),
         notebook = std::move(notebook)]
         {
             const auto self = self_weak.lock();
             if (!self) {
                 promise->setException(RuntimeError(ErrorString{
                     QT_TRANSLATE_NOOP(
                         "local_storage::sql::NotebooksHandler",
                         "NotebooksHandler is already destroyed")}));
                 promise->finish();
                 return;
             }

             auto databaseConnection = self->m_connectionPool->database();

             ErrorString errorDescription;
             const bool res = self->putNotebookImpl(
                 notebook, databaseConnection, errorDescription);

             if (!res) {
                 promise->setException(
                     DatabaseRequestException{errorDescription});
             }

             promise->finish();
         });

    return future;
}

QFuture<qevercloud::Notebook> NotebooksHandler::findNotebookByLocalId(
    QString localId) const
{
    auto promise = std::make_shared<QPromise<qevercloud::Notebook>>();
    auto future = promise->future();

    promise->start();

    auto * runnable = utility::createFunctionRunnable(
        [promise = std::move(promise), self_weak = weak_from_this(),
         localId = std::move(localId)] () mutable
         {
             const auto self = self_weak.lock();
             if (!self) {
                 promise->setException(RuntimeError(ErrorString{
                     QT_TRANSLATE_NOOP(
                         "local_storage::sql::NotebooksHandler",
                         "NotebooksHandler is already destroyed")}));
                 promise->finish();
                 return;
             }

             auto databaseConnection = self->m_connectionPool->database();

             ErrorString errorDescription;
             auto notebook = self->findNotebookByLocalIdImpl(
                 std::move(localId), databaseConnection, errorDescription);

             if (notebook) {
                 promise->addResult(std::move(*notebook));
             }
             else if (!errorDescription.isEmpty()) {
                 promise->setException(
                     DatabaseRequestException{errorDescription});
             }

             promise->finish();
         });

    m_threadPool->start(runnable);
    return future;
}

QFuture<qevercloud::Notebook> NotebooksHandler::findNotebookByGuid(
    qevercloud::Guid guid) const
{
    auto promise = std::make_shared<QPromise<qevercloud::Notebook>>();
    auto future = promise->future();

    promise->start();

    auto * runnable = utility::createFunctionRunnable(
        [promise = std::move(promise), self_weak = weak_from_this(),
         guid = std::move(guid)] () mutable
         {
             const auto self = self_weak.lock();
             if (!self) {
                 promise->setException(RuntimeError(ErrorString{
                     QT_TRANSLATE_NOOP(
                         "local_storage::sql::NotebooksHandler",
                         "NotebooksHandler is already destroyed")}));
                 promise->finish();
                 return;
             }

             auto databaseConnection = self->m_connectionPool->database();

             ErrorString errorDescription;
             auto notebook = self->findNotebookByGuidImpl(
                 std::move(guid), databaseConnection, errorDescription);

             if (notebook) {
                 promise->addResult(std::move(*notebook));
             }
             else if (!errorDescription.isEmpty()) {
                 promise->setException(
                     DatabaseRequestException{errorDescription});
             }

             promise->finish();
         });

    m_threadPool->start(runnable);
    return future;
}

QFuture<qevercloud::Notebook> NotebooksHandler::findNotebookByName(
    QString name) const
{
    auto promise = std::make_shared<QPromise<qevercloud::Notebook>>();
    auto future = promise->future();

    promise->start();

    auto * runnable = utility::createFunctionRunnable(
        [promise = std::move(promise), self_weak = weak_from_this(),
         name = std::move(name)] () mutable
         {
             const auto self = self_weak.lock();
             if (!self) {
                 promise->setException(RuntimeError(ErrorString{
                     QT_TRANSLATE_NOOP(
                         "local_storage::sql::NotebooksHandler",
                         "NotebooksHandler is already destroyed")}));
                 promise->finish();
                 return;
             }

             auto databaseConnection = self->m_connectionPool->database();

             ErrorString errorDescription;
             auto notebook = self->findNotebookByNameImpl(
                 std::move(name), databaseConnection, errorDescription);

             if (notebook) {
                 promise->addResult(std::move(*notebook));
             }
             else if (!errorDescription.isEmpty()) {
                 promise->setException(
                     DatabaseRequestException{errorDescription});
             }

             promise->finish();
         });

    m_threadPool->start(runnable);
    return future;
}

QFuture<qevercloud::Notebook> NotebooksHandler::findDefaultNotebook() const
{
    auto promise = std::make_shared<QPromise<qevercloud::Notebook>>();
    auto future = promise->future();

    promise->start();

    auto * runnable = utility::createFunctionRunnable(
        [promise = std::move(promise), self_weak = weak_from_this()]
         {
             const auto self = self_weak.lock();
             if (!self) {
                 promise->setException(RuntimeError(ErrorString{
                     QT_TRANSLATE_NOOP(
                         "local_storage::sql::NotebooksHandler",
                         "NotebooksHandler is already destroyed")}));
                 promise->finish();
                 return;
             }

             auto databaseConnection = self->m_connectionPool->database();

             ErrorString errorDescription;
             auto notebook = self->findDefaultNotebookImpl(
                 databaseConnection, errorDescription);

             if (notebook) {
                 promise->addResult(std::move(*notebook));
             }
             else if (!errorDescription.isEmpty()) {
                 promise->setException(
                     DatabaseRequestException{errorDescription});
             }

             promise->finish();
         });

    m_threadPool->start(runnable);
    return future;
}

QFuture<void> NotebooksHandler::expungeNotebookByLocalId(QString localId)
{
    auto promise = std::make_shared<QPromise<void>>();
    auto future = promise->future();

    promise->start();

    utility::postToThread(
        m_writerThread.get(),
        [promise = std::move(promise), self_weak = weak_from_this(),
         localId = std::move(localId)] () mutable
        {
             const auto self = self_weak.lock();
             if (!self) {
                 promise->setException(RuntimeError(ErrorString{
                     QT_TRANSLATE_NOOP(
                         "local_storage::sql::NotebooksHandler",
                         "NotebooksHandler is already destroyed")}));
                 promise->finish();
                 return;
             }

             auto databaseConnection = self->m_connectionPool->database();

             ErrorString errorDescription;
             const bool res = self->expungeNotebookByLocalIdImpl(
                 std::move(localId), databaseConnection, errorDescription);

             if (!res) {
                 promise->setException(
                     DatabaseRequestException{errorDescription});
             }

             promise->finish();
        });

    return future;
}

QFuture<void> NotebooksHandler::expungeNotebookByGuid(qevercloud::Guid guid)
{
    auto promise = std::make_shared<QPromise<void>>();
    auto future = promise->future();

    promise->start();

    utility::postToThread(
        m_writerThread.get(),
        [promise = std::move(promise), self_weak = weak_from_this(),
         guid = std::move(guid)] () mutable
        {
             const auto self = self_weak.lock();
             if (!self) {
                 promise->setException(RuntimeError(ErrorString{
                     QT_TRANSLATE_NOOP(
                         "local_storage::sql::NotebooksHandler",
                         "NotebooksHandler is already destroyed")}));
                 promise->finish();
                 return;
             }

             auto databaseConnection = self->m_connectionPool->database();

             ErrorString errorDescription;
             const bool res = self->expungeNotebookByGuidImpl(
                 std::move(guid), databaseConnection, errorDescription);

             if (!res) {
                 promise->setException(
                     DatabaseRequestException{errorDescription});
             }

             promise->finish();
        });

    return future;
}

QFuture<void> NotebooksHandler::expungeNotebookByName(QString name)
{
    auto promise = std::make_shared<QPromise<void>>();
    auto future = promise->future();

    promise->start();

    utility::postToThread(
        m_writerThread.get(),
        [promise = std::move(promise), self_weak = weak_from_this(),
         name = std::move(name)] () mutable
        {
             const auto self = self_weak.lock();
             if (!self) {
                 promise->setException(RuntimeError(ErrorString{
                     QT_TRANSLATE_NOOP(
                         "local_storage::sql::NotebooksHandler",
                         "NotebooksHandler is already destroyed")}));
                 promise->finish();
                 return;
             }

             auto databaseConnection = self->m_connectionPool->database();

             ErrorString errorDescription;
             const bool res = self->expungeNotebookByNameImpl(
                 std::move(name), databaseConnection, errorDescription);

             if (!res) {
                 promise->setException(
                     DatabaseRequestException{errorDescription});
             }

             promise->finish();
        });

    return future;
}

QFuture<QList<qevercloud::Notebook>> NotebooksHandler::listNotebooks(
    ListOptions<ListNotebooksOrder> options) const
{
    auto promise = std::make_shared<QPromise<QList<qevercloud::Notebook>>>();
    auto future = promise->future();

    promise->start();

    auto * runnable = utility::createFunctionRunnable(
        [promise = std::move(promise), self_weak = weak_from_this(),
         options = std::move(options)] () mutable
         {
             const auto self = self_weak.lock();
             if (!self) {
                 promise->setException(RuntimeError(ErrorString{
                     QT_TRANSLATE_NOOP(
                         "local_storage::sql::NotebooksHandler",
                         "NotebooksHandler is already destroyed")}));
                 promise->finish();
                 return;
             }

             auto databaseConnection = self->m_connectionPool->database();

             ErrorString errorDescription;
             auto notebooks = self->listNotebooksImpl(
                 std::move(options), databaseConnection, errorDescription);

             if (!notebooks.isEmpty()) {
                 promise->addResult(std::move(notebooks));
             }
             else if (!errorDescription.isEmpty()) {
                 promise->setException(
                     DatabaseRequestException{errorDescription});
             }

             promise->finish();
         });

    m_threadPool->start(runnable);
    return future;
}

std::optional<quint32> NotebooksHandler::notebookCountImpl(
    QSqlDatabase & database, ErrorString & errorDescription) const
{
    // TODO: implement
    Q_UNUSED(database)
    Q_UNUSED(errorDescription)
    return std::nullopt;
}

bool NotebooksHandler::putNotebookImpl(
    const qevercloud::Notebook & notebook, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    // TODO: implement
    Q_UNUSED(notebook)
    Q_UNUSED(database)
    Q_UNUSED(errorDescription)
    return true;
}

std::optional<qevercloud::Notebook> NotebooksHandler::findNotebookByLocalIdImpl(
    QString localId, QSqlDatabase & database,
    ErrorString & errorDescription) const
{
    // TODO: implement
    Q_UNUSED(localId)
    Q_UNUSED(database)
    Q_UNUSED(errorDescription)
    return std::nullopt;
}

std::optional<qevercloud::Notebook> NotebooksHandler::findNotebookByGuidImpl(
    qevercloud::Guid guid, QSqlDatabase & database,
    ErrorString & errorDescription) const
{
    // TODO: implement
    Q_UNUSED(guid)
    Q_UNUSED(database)
    Q_UNUSED(errorDescription)
    return std::nullopt;
}

std::optional<qevercloud::Notebook> NotebooksHandler::findNotebookByNameImpl(
    QString name, QSqlDatabase & database,
    ErrorString & errorDescription) const
{
    // TODO: implement
    Q_UNUSED(name)
    Q_UNUSED(database)
    Q_UNUSED(errorDescription)
    return std::nullopt;
}

std::optional<qevercloud::Notebook> NotebooksHandler::findDefaultNotebookImpl(
    QSqlDatabase & database, ErrorString & errorDescription) const
{
    // TODO: implement
    Q_UNUSED(database)
    Q_UNUSED(errorDescription)
    return std::nullopt;
}

bool NotebooksHandler::expungeNotebookByLocalIdImpl(
    QString localId, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    // TODO: implement
    Q_UNUSED(localId)
    Q_UNUSED(database)
    Q_UNUSED(errorDescription)
    return true;
}

bool NotebooksHandler::expungeNotebookByGuidImpl(
    qevercloud::Guid guid, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    // TODO: implement
    Q_UNUSED(guid)
    Q_UNUSED(database)
    Q_UNUSED(errorDescription)
    return true;
}

bool NotebooksHandler::expungeNotebookByNameImpl(
    QString name, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    // TODO: implement
    Q_UNUSED(name)
    Q_UNUSED(database)
    Q_UNUSED(errorDescription)
    return true;
}

QList<qevercloud::Notebook> NotebooksHandler::listNotebooksImpl(
    ListOptions<ListNotebooksOrder> options, QSqlDatabase & database,
    ErrorString & errorDescription) const
{
    // TODO: implement
    Q_UNUSED(options)
    Q_UNUSED(database)
    Q_UNUSED(errorDescription)
    return {};
}

} // namespace quentier::local_storage::sql
