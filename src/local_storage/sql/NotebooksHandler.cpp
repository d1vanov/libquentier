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
#include "Transaction.h"
#include "TypeChecks.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/RuntimeError.h>
#include <quentier/logging/QuentierLogger.h>

#include <utility/Threading.h>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include <utility/Qt5Promise.h>
#endif

#include <QGlobalStatic>
#include <QSqlRecord>
#include <QSqlQuery>
#include <QThreadPool>

namespace quentier::local_storage::sql {

namespace {

Q_GLOBAL_STATIC(QVariant, gNullValue)

} // namespace

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
         notebook = std::move(notebook)] () mutable
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
                 std::move(notebook), databaseConnection, errorDescription);

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
    QSqlQuery query{database};
    const bool res = query.exec(
        QStringLiteral("SELECT COUNT(localUid) FROM Notebooks"));

    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::NotebooksHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::NotebooksHandler",
            "Cannot count notebooks in the local storage database"),
        std::nullopt);

    if (!query.next()) {
        QNDEBUG(
            "local_storage::sql::NotebooksHandler",
            "Found no notebooks in the local storage database");
        return 0;
    }

    bool conversionResult = false;
    const int count = query.value(0).toInt(&conversionResult);
    if (Q_UNLIKELY(!conversionResult)) {
        errorDescription.setBase(
            QT_TRANSLATE_NOOP(
                "local_storage::sql::NotebooksHandler",
                "Cannot count notebooks in the local storage database: failed "
                "to convert notebook count to int"));
        QNWARNING("local_storage:sql", errorDescription);
        return std::nullopt;
    }

    return count;
}

bool NotebooksHandler::putNotebookImpl(
    qevercloud::Notebook notebook, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    QNDEBUG(
        "local_storage::sql::NotebooksHandler",
        "NotebooksHandler::putNotebookImpl: " << notebook);

    const ErrorString errorPrefix(
        QT_TRANSLATE_NOOP(
            "local_storage::sql::UsersHandler",
            "Can't put notebook into the local storage database"));

    ErrorString error;
    if (!checkNotebook(notebook, error)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING(
            "local_storage:sql:NotebooksHandler",
            error << "\nNotebook: " << notebook);
        return false;
    }

    const auto localId = notebookLocalId(notebook, database, errorDescription);
    notebook.setLocalId(localId);

    Transaction transaction{database};

    if (!putCommonNotebookData(notebook, database, errorDescription)) {
        return false;
    }

    if (notebook.restrictions()) {
        if (!putNotebookRestrictions(
                localId, *notebook.restrictions(), database, errorDescription))
        {
            return false;
        }
    }
    else if (!removeNotebookRestrictions(localId, database, errorDescription)) {
        return false;
    }

    if (notebook.guid())
    {
        if (!removeSharedNotebooks(
                *notebook.guid(), database, errorDescription)) {
            return false;
        }

        if (notebook.sharedNotebooks() &&
            !notebook.sharedNotebooks()->isEmpty()) {
            int indexInNotebook = 0;
            for (const auto & sharedNotebook:
                 qAsConst(*notebook.sharedNotebooks())) {
                if (!sharedNotebook.id()) {
                    QNWARNING(
                        "local_storage::sql::NotebooksHandler",
                        "Found shared notebook without primary identifier "
                        "of the share set, skipping it: " << sharedNotebook);
                    continue;
                }

                if (!putSharedNotebook(
                        sharedNotebook, indexInNotebook, database,
                        errorDescription)) {
                    return false;
                }

                ++indexInNotebook;
            }
        }
    }

    return true;
}

bool NotebooksHandler::putCommonNotebookData(
    const qevercloud::Notebook & notebook, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    static const QString queryString = QStringLiteral(
        "INSERT OR REPLACE INTO Notebooks"
        "(localUid, guid, linkedNotebookGuid, "
        "updateSequenceNumber, notebookName, notebookNameUpper, "
        "creationTimestamp, modificationTimestamp, isDirty, "
        "isLocal, isDefault, isLastUsed, isFavorited, "
        "publishingUri, publishingNoteSortOrder, "
        "publishingAscendingSort, publicDescription, isPublished, "
        "stack, businessNotebookDescription, "
        "businessNotebookPrivilegeLevel, "
        "businessNotebookIsRecommended, contactId, "
        "recipientReminderNotifyEmail, recipientReminderNotifyInApp, "
        "recipientInMyList, recipientStack) "
        "VALUES(:localUid, :guid, :linkedNotebookGuid, "
        ":updateSequenceNumber, :notebookName, :notebookNameUpper, "
        ":creationTimestamp, :modificationTimestamp, :isDirty, "
        ":isLocal, :isDefault, :isLastUsed, :isFavorited, "
        ":publishingUri, :publishingNoteSortOrder, "
        ":publishingAscendingSort, :publicDescription, "
        ":isPublished, :stack, :businessNotebookDescription, "
        ":businessNotebookPrivilegeLevel, "
        ":businessNotebookIsRecommended, :contactId, "
        ":recipientReminderNotifyEmail, "
        ":recipientReminderNotifyInApp, :recipientInMyList, "
        ":recipientStack)");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::NotebooksHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::NotebooksHandler",
            "Cannot put common notebook data into the local storage database: "
            "failed to prepare query"),
        false);

    const auto & localId = notebook.localId();
    query.bindValue(
        QStringLiteral(":localUid"),
        (localId.isEmpty() ? *gNullValue : localId));

    query.bindValue(
        QStringLiteral(":guid"),
        (notebook.guid() ? *notebook.guid() : *gNullValue));

    const QString linkedNotebookGuid =
        notebook.linkedNotebookGuid().value_or(QString{});

    query.bindValue(
        QStringLiteral(":linkedNotebookGuid"),
        (!linkedNotebookGuid.isEmpty() ? linkedNotebookGuid : *gNullValue));

    query.bindValue(
        QStringLiteral(":updateSequenceNumber"),
        (notebook.updateSequenceNum() ? *notebook.updateSequenceNum()
         : *gNullValue));

    query.bindValue(
        QStringLiteral(":notebookName"),
        (notebook.name() ? *notebook.name() : *gNullValue));

    query.bindValue(
        QStringLiteral(":notebookNameUpper"),
        (notebook.name() ? notebook.name()->toUpper() : *gNullValue));

    query.bindValue(
        QStringLiteral(":creationTimestamp"),
        (notebook.serviceCreated() ? *notebook.serviceCreated()
         : *gNullValue));

    query.bindValue(
        QStringLiteral(":modificationTimestamp"),
        (notebook.serviceUpdated() ? *notebook.serviceUpdated()
         : *gNullValue));

    query.bindValue(
        QStringLiteral(":isDirty"), (notebook.isLocallyModified() ? 1 : 0));

    query.bindValue(
        QStringLiteral(":isLocal"), (notebook.isLocalOnly() ? 1 : 0));

    query.bindValue(
        QStringLiteral(":isDefault"),
        (notebook.defaultNotebook() && *notebook.defaultNotebook()
         ? 1
         : *gNullValue));

    bool isLastUsed = false;
    if (const auto it =
        notebook.localData().constFind(QStringLiteral("lastUsed"));
        it != notebook.localData().constEnd())
    {
        isLastUsed = it.value().toBool();
    }

    query.bindValue(
        QStringLiteral(":isLastUsed"), (isLastUsed ? 1 : *gNullValue));

    query.bindValue(
        QStringLiteral(":isFavorited"),
        (notebook.isLocallyFavorited() ? 1 : 0));

    query.bindValue(
        QStringLiteral(":publishingUri"),
        (notebook.publishing()
         ? (notebook.publishing()->uri() ? *notebook.publishing()->uri()
            : *gNullValue)
         : *gNullValue));

    query.bindValue(
        QStringLiteral(":publishingNoteSortOrder"),
        (notebook.publishing()
         ? (notebook.publishing()->order()
            ? static_cast<int>(*notebook.publishing()->order())
            : *gNullValue)
         : *gNullValue));

    query.bindValue(
        QStringLiteral(":publishingAscendingSort"),
        (notebook.publishing()
         ? (notebook.publishing()->ascending()
            ? static_cast<int>(*notebook.publishing()->ascending())
            : *gNullValue)
         : *gNullValue));

    query.bindValue(
        QStringLiteral(":publicDescription"),
        (notebook.publishing()
         ? (notebook.publishing()->publicDescription()
            ? *notebook.publishing()->publicDescription()
            : *gNullValue)
         : *gNullValue));

    query.bindValue(
        QStringLiteral(":isPublished"),
        (notebook.published() ? static_cast<int>(*notebook.published())
         : *gNullValue));

    query.bindValue(
        QStringLiteral(":stack"),
        (notebook.stack() ? *notebook.stack() : *gNullValue));

    query.bindValue(
        QStringLiteral(":businessNotebookDescription"),
        (notebook.businessNotebook()
         ? (notebook.businessNotebook()->notebookDescription()
            ? *notebook.businessNotebook()->notebookDescription()
            : *gNullValue)
         : *gNullValue));

    query.bindValue(
        QStringLiteral(":businessNotebookPrivilegeLevel"),
        (notebook.businessNotebook()
         ? (notebook.businessNotebook()->privilege()
            ? static_cast<int>(
                *notebook.businessNotebook()->privilege())
            : *gNullValue)
         : *gNullValue));

    query.bindValue(
        QStringLiteral(":businessNotebookIsRecommended"),
        (notebook.businessNotebook()
         ? (notebook.businessNotebook()->recommended()
            ? static_cast<int>(
                *notebook.businessNotebook()->recommended())
            : *gNullValue)
         : *gNullValue));

    query.bindValue(
        QStringLiteral(":contactId"),
        ((notebook.contact() && notebook.contact()->id())
         ? *notebook.contact()->id()
         : *gNullValue));

    query.bindValue(
        QStringLiteral(":recipientReminderNotifyEmail"),
        (notebook.recipientSettings()
         ? (notebook.recipientSettings()->reminderNotifyEmail()
            ? static_cast<int>(*notebook.recipientSettings()
                               ->reminderNotifyEmail())
            : *gNullValue)
         : *gNullValue));

    query.bindValue(
        QStringLiteral(":recipientReminderNotifyInApp"),
        (notebook.recipientSettings()
         ? (notebook.recipientSettings()->reminderNotifyInApp()
            ? static_cast<int>(*notebook.recipientSettings()
                               ->reminderNotifyInApp())
            : *gNullValue)
         : *gNullValue));

    query.bindValue(
        QStringLiteral(":recipientInMyList"),
        (notebook.recipientSettings()
         ? (notebook.recipientSettings()->inMyList()
            ? static_cast<int>(
                *notebook.recipientSettings()->inMyList())
            : *gNullValue)
         : *gNullValue));

    query.bindValue(
        QStringLiteral(":recipientStack"),
        (notebook.recipientSettings()
         ? (notebook.recipientSettings()->stack()
            ? *notebook.recipientSettings()->stack()
            : *gNullValue)
         : *gNullValue));

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::NotebooksHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::NotebooksHandler",
            "Cannot put common notebook data into the local storage database"),
        false);

    return true;
}

bool NotebooksHandler::putNotebookRestrictions(
    const QString & localId,
    const qevercloud::NotebookRestrictions & notebookRestrictions,
    QSqlDatabase & database, ErrorString & errorDescription)
{
    static const QString queryString = QStringLiteral(
        "INSERT OR REPLACE INTO NotebookRestrictions"
        "(localUid, noReadNotes, noCreateNotes, noUpdateNotes, "
        "noExpungeNotes, noShareNotes, noEmailNotes, "
        "noSendMessageToRecipients, noUpdateNotebook, "
        "noExpungeNotebook, noSetDefaultNotebook, "
        "noSetNotebookStack, noPublishToPublic, "
        "noPublishToBusinessLibrary, noCreateTags, noUpdateTags, "
        "noExpungeTags, noSetParentTag, noCreateSharedNotebooks, "
        "updateWhichSharedNotebookRestrictions, "
        "expungeWhichSharedNotebookRestrictions) "
        "VALUES(:localUid, :noReadNotes, :noCreateNotes, "
        ":noUpdateNotes, :noExpungeNotes, :noShareNotes, "
        ":noEmailNotes, :noSendMessageToRecipients, "
        ":noUpdateNotebook, :noExpungeNotebook, "
        ":noSetDefaultNotebook, :noSetNotebookStack, "
        ":noPublishToPublic, :noPublishToBusinessLibrary, "
        ":noCreateTags, :noUpdateTags, :noExpungeTags, "
        ":noSetParentTag, :noCreateSharedNotebooks, "
        ":updateWhichSharedNotebookRestrictions, "
        ":expungeWhichSharedNotebookRestrictions)");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::NotebooksHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::NotebooksHandler",
            "Cannot put notebook restrictions into the local storage database: "
            "failed to prepare query"),
        false);

    query.bindValue(
        QStringLiteral(":localUid"),
        (localId.isEmpty() ? *gNullValue : localId));

    query.bindValue(
        QStringLiteral(":noReadNotes"),
        (notebookRestrictions.noReadNotes()
         ? (*notebookRestrictions.noReadNotes() ? 1 : 0)
         : *gNullValue));

    query.bindValue(
        QStringLiteral(":noCreateNotes"),
        (notebookRestrictions.noCreateNotes()
         ? (*notebookRestrictions.noCreateNotes() ? 1 : 0)
         : *gNullValue));

    query.bindValue(
        QStringLiteral(":noUpdateNotes"),
        (notebookRestrictions.noUpdateNotes()
         ? (*notebookRestrictions.noUpdateNotes() ? 1 : 0)
         : *gNullValue));

    query.bindValue(
        QStringLiteral(":noExpungeNotes"),
        (notebookRestrictions.noExpungeNotes()
         ? (*notebookRestrictions.noExpungeNotes() ? 1 : 0)
         : *gNullValue));

    query.bindValue(
        QStringLiteral(":noShareNotes"),
        (notebookRestrictions.noShareNotes()
         ? (*notebookRestrictions.noShareNotes() ? 1 : 0)
         : *gNullValue));

    query.bindValue(
        QStringLiteral(":noEmailNotes"),
        (notebookRestrictions.noEmailNotes()
         ? (*notebookRestrictions.noEmailNotes() ? 1 : 0)
         : *gNullValue));

    query.bindValue(
        QStringLiteral(":noSendMessageToRecipients"),
        (notebookRestrictions.noSendMessageToRecipients()
         ? (*notebookRestrictions.noSendMessageToRecipients() ? 1 : 0)
         : *gNullValue));

    query.bindValue(
        QStringLiteral(":noUpdateNotebook"),
        (notebookRestrictions.noUpdateNotebook()
         ? (*notebookRestrictions.noUpdateNotebook() ? 1 : 0)
         : *gNullValue));

    query.bindValue(
        QStringLiteral(":noExpungeNotebook"),
        (notebookRestrictions.noExpungeNotebook()
         ? (*notebookRestrictions.noExpungeNotebook() ? 1 : 0)
         : *gNullValue));

    query.bindValue(
        QStringLiteral(":noSetDefaultNotebook"),
        (notebookRestrictions.noSetDefaultNotebook()
         ? (*notebookRestrictions.noSetDefaultNotebook() ? 1 : 0)
         : *gNullValue));

    query.bindValue(
        QStringLiteral(":noSetNotebookStack"),
        (notebookRestrictions.noSetNotebookStack()
         ? (*notebookRestrictions.noSetNotebookStack() ? 1 : 0)
         : *gNullValue));

    query.bindValue(
        QStringLiteral(":noPublishToPublic"),
        (notebookRestrictions.noPublishToPublic()
         ? (*notebookRestrictions.noPublishToPublic() ? 1 : 0)
         : *gNullValue));

    query.bindValue(
        QStringLiteral(":noPublishToBusinessLibrary"),
        (notebookRestrictions.noPublishToBusinessLibrary()
         ? (*notebookRestrictions.noPublishToBusinessLibrary() ? 1 : 0)
         : *gNullValue));

    query.bindValue(
        QStringLiteral(":noCreateTags"),
        (notebookRestrictions.noCreateTags()
         ? (*notebookRestrictions.noCreateTags() ? 1 : 0)
         : *gNullValue));

    query.bindValue(
        QStringLiteral(":noUpdateTags"),
        (notebookRestrictions.noUpdateTags()
         ? (*notebookRestrictions.noUpdateTags() ? 1 : 0)
         : *gNullValue));

    query.bindValue(
        QStringLiteral(":noExpungeTags"),
        (notebookRestrictions.noExpungeTags()
         ? (*notebookRestrictions.noExpungeTags() ? 1 : 0)
         : *gNullValue));

    query.bindValue(
        QStringLiteral(":noSetParentTag"),
        (notebookRestrictions.noSetParentTag()
         ? (*notebookRestrictions.noSetParentTag() ? 1 : 0)
         : *gNullValue));

    query.bindValue(
        QStringLiteral(":noCreateSharedNotebooks"),
        (notebookRestrictions.noCreateSharedNotebooks()
         ? (*notebookRestrictions.noCreateSharedNotebooks() ? 1 : 0)
         : *gNullValue));

    query.bindValue(
        QStringLiteral(":noShareNotesWithBusiness"),
        (notebookRestrictions.noShareNotesWithBusiness()
         ? (*notebookRestrictions.noShareNotesWithBusiness() ? 1 : 0)
         : *gNullValue));

    query.bindValue(
        QStringLiteral(":noRenameNotebook"),
        (notebookRestrictions.noRenameNotebook()
         ? (*notebookRestrictions.noRenameNotebook() ? 1 : 0)
         : *gNullValue));

    query.bindValue(
        QStringLiteral(":updateWhichSharedNotebookRestrictions"),
        notebookRestrictions.updateWhichSharedNotebookRestrictions()
            ? static_cast<int>(
                  *notebookRestrictions.updateWhichSharedNotebookRestrictions())
            : *gNullValue);

    query.bindValue(
        QStringLiteral(":expungeWhichSharedNotebookRestrictions"),
        notebookRestrictions.expungeWhichSharedNotebookRestrictions()
            ? static_cast<int>(*notebookRestrictions
                                    .expungeWhichSharedNotebookRestrictions())
            : *gNullValue);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::NotebooksHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::NotebooksHandler",
            "Cannot put notebook restrictions into the local storage database"),
        false);

    return true;
}

bool NotebooksHandler::putSharedNotebook(
    const qevercloud::SharedNotebook & sharedNotebook,
    const int indexInNotebook, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    static const QString queryString = QStringLiteral(
        "INSERT OR REPLACE INTO SharedNotebooks"
        "(sharedNotebookShareId, sharedNotebookUserId, "
        "sharedNotebookNotebookGuid, sharedNotebookEmail, "
        "sharedNotebookCreationTimestamp, "
        "sharedNotebookModificationTimestamp, "
        "sharedNotebookGlobalId, sharedNotebookUsername, "
        "sharedNotebookPrivilegeLevel, "
        "sharedNotebookRecipientReminderNotifyEmail, "
        "sharedNotebookRecipientReminderNotifyInApp, "
        "sharedNotebookSharerUserId, "
        "sharedNotebookRecipientUsername, "
        "sharedNotebookRecipientUserId, "
        "sharedNotebookRecipientIdentityId, "
        "sharedNotebookAssignmentTimestamp, indexInNotebook) "
        "VALUES(:sharedNotebookShareId, :sharedNotebookUserId, "
        ":sharedNotebookNotebookGuid, :sharedNotebookEmail, "
        ":sharedNotebookCreationTimestamp, "
        ":sharedNotebookModificationTimestamp, "
        ":sharedNotebookGlobalId, :sharedNotebookUsername, "
        ":sharedNotebookPrivilegeLevel, "
        ":sharedNotebookRecipientReminderNotifyEmail, "
        ":sharedNotebookRecipientReminderNotifyInApp, "
        ":sharedNotebookSharerUserId, "
        ":sharedNotebookRecipientUsername, "
        ":sharedNotebookRecipientUserId, "
        ":sharedNotebookRecipientIdentityId, "
        ":sharedNotebookAssignmentTimestamp, :indexInNotebook) ");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::NotebooksHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::NotebooksHandler",
            "Cannot put shared notebook into the local storage database: "
            "failed to prepare query"),
        false);

    query.bindValue(
        QStringLiteral(":sharedNotebookShareId"), sharedNotebook.id().value());

    query.bindValue(
        QStringLiteral(":sharedNotebookUserId"),
        (sharedNotebook.userId() ? *sharedNotebook.userId() : *gNullValue));

    query.bindValue(
        QStringLiteral(":sharedNotebookNotebookGuid"),
        (sharedNotebook.notebookGuid() ? *sharedNotebook.notebookGuid()
                                       : *gNullValue));

    query.bindValue(
        QStringLiteral(":sharedNotebookEmail"),
        (sharedNotebook.email() ? *sharedNotebook.email() : *gNullValue));

    query.bindValue(
        QStringLiteral(":sharedNotebookCreationTimestamp"),
        (sharedNotebook.serviceCreated() ? *sharedNotebook.serviceCreated()
                                         : *gNullValue));

    query.bindValue(
        QStringLiteral(":sharedNotebookModificationTimestamp"),
        (sharedNotebook.serviceUpdated() ? *sharedNotebook.serviceUpdated()
                                         : *gNullValue));

    query.bindValue(
        QStringLiteral(":sharedNotebookGlobalId"),
        (sharedNotebook.globalId() ? *sharedNotebook.globalId() : *gNullValue));

    query.bindValue(
        QStringLiteral(":sharedNotebookUsername"),
        (sharedNotebook.username() ? *sharedNotebook.username() : *gNullValue));

    query.bindValue(
        QStringLiteral(":sharedNotebookPrivilegeLevel"),
        (sharedNotebook.privilege()
             ? static_cast<int>(*sharedNotebook.privilege())
             : *gNullValue));

    query.bindValue(
        QStringLiteral(":sharedNotebookRecipientReminderNotifyEmail"),
        (sharedNotebook.recipientSettings()
             ? (sharedNotebook.recipientSettings()->reminderNotifyEmail()
                    ? (*sharedNotebook.recipientSettings()
                               ->reminderNotifyEmail()
                           ? 1
                           : 0)
                    : *gNullValue)
             : *gNullValue));

    query.bindValue(
        QStringLiteral(":sharedNotebookRecipientReminderNotifyInApp"),
        (sharedNotebook.recipientSettings()
             ? (sharedNotebook.recipientSettings()->reminderNotifyInApp()
                    ? (*sharedNotebook.recipientSettings()
                               ->reminderNotifyInApp()
                           ? 1
                           : 0)
                    : *gNullValue)
             : *gNullValue));

    query.bindValue(
        QStringLiteral(":sharedNotebookSharerUserId"),
        (sharedNotebook.sharerUserId() ? *sharedNotebook.sharerUserId()
                                       : *gNullValue));

    query.bindValue(
        QStringLiteral(":sharedNotebookRecipientUsername"),
        (sharedNotebook.recipientUsername()
             ? *sharedNotebook.recipientUsername()
             : *gNullValue));

    query.bindValue(
        QStringLiteral(":sharedNotebookRecipientUserId"),
        (sharedNotebook.recipientUserId() ? *sharedNotebook.recipientUserId()
                                          : *gNullValue));

    query.bindValue(
        QStringLiteral(":sharedNotebookRecipientIdentityId"),
        (sharedNotebook.recipientIdentityId()
             ? *sharedNotebook.recipientIdentityId()
             : *gNullValue));

    query.bindValue(
        QStringLiteral(":sharedNotebookAssignmentTimestamp"),
        (sharedNotebook.serviceAssigned() ? *sharedNotebook.serviceAssigned()
                                          : *gNullValue));

    query.bindValue(QStringLiteral(":indexInNotebook"), indexInNotebook);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::NotebooksHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::NotebooksHandler",
            "Cannot put shared notebook into the local storage database"),
        false);

    return true;
}

bool NotebooksHandler::removeNotebookRestrictions(
    const QString & localId, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    // TODO: implement
    Q_UNUSED(localId)
    Q_UNUSED(database)
    Q_UNUSED(errorDescription)
    return true;
}

bool NotebooksHandler::removeSharedNotebooks(
    const QString & notebookGuid, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    // TODO: implement
    Q_UNUSED(notebookGuid)
    Q_UNUSED(database)
    Q_UNUSED(errorDescription)
    return true;
}

QString NotebooksHandler::notebookLocalId(
    const qevercloud::Notebook & notebook, QSqlDatabase & database,
    ErrorString & errorDescription) const
{
    auto localId = notebook.localId();
    if (!localId.isEmpty())
    {
        return localId;
    }

    // Notebook's local id is empty. Will try to find local id of this notebook
    // by its guid in the local storage database.
    if (!notebook.guid())
    {
        errorDescription.setBase(
            QT_TRANSLATE_NOOP(
                "local_storage::sql::NotebooksHandler",
                "Cannot find notebook's local id: notebook has no guid"));
        QNWARNING(
            "local_storage::sql::NotebooksHandler",
            errorDescription << ": " << notebook);
        return {};
    }

    QSqlQuery query{database};
    bool res = query.prepare(
        QStringLiteral("SELECT localUid FROM Notebooks WHERE guid = :guid"));

    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::NotebooksHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::NotebooksHandler",
            "Cannot find notebook's local id in the local storage database: "
            "failed to prepare query"),
        {});

    query.bindValue(QStringLiteral(":guid"), *notebook.guid());
    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::NotebooksHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::NotebooksHandler",
            "Cannot find notebook's local id in the local storage database"),
        {});

    if (!query.next()) {
        return {};
    }

    return query.value(0).toString();
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
