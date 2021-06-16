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
#include "Tasks.h"
#include "Transaction.h"
#include "TypeChecks.h"

#include "utils/FillFromSqlRecordUtils.h"
#include "utils/NotebookUtils.h"
#include "utils/PutToDatabaseUtils.h"
#include "utils/ResourceDataFilesUtils.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/RuntimeError.h>
#include <quentier/logging/QuentierLogger.h>

#include <utility/Threading.h>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include <utility/Qt5Promise.h>
#endif

#include <qevercloud/utility/ToRange.h>

#include <QGlobalStatic>
#include <QMap>
#include <QSqlRecord>
#include <QSqlQuery>
#include <QThreadPool>

#include <algorithm>

namespace quentier::local_storage::sql {

NotebooksHandler::NotebooksHandler(
    ConnectionPoolPtr connectionPool, QThreadPool * threadPool,
    QThreadPtr writerThread, const QString & localStorageDirPath) :
    m_connectionPool{std::move(connectionPool)},
    m_threadPool{threadPool},
    m_writerThread{std::move(writerThread)},
    m_localStorageDir{localStorageDirPath}
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

    if (Q_UNLIKELY(!m_localStorageDir.isReadable())) {
        throw InvalidArgument{ErrorString{
            QT_TRANSLATE_NOOP(
                "local_storage::sql::NotebooksHandler",
                "NotebooksHandler ctor: local storage dir is not readable")}};
    }

    if (Q_UNLIKELY(
            !m_localStorageDir.exists() &&
            !m_localStorageDir.mkpath(m_localStorageDir.absolutePath())))
    {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "local_storage::sql::NotebooksHandler",
            "NotebooksHandler ctor: local storage dir does not exist and "
            "cannot be created")}};
    }
}

QFuture<quint32> NotebooksHandler::notebookCount() const
{
    return makeReadTask<quint32>(
        makeTaskContext(),
        weak_from_this(),
        [](const NotebooksHandler & handler, QSqlDatabase & database,
           ErrorString & errorDescription)
        {
            return handler.notebookCountImpl(database, errorDescription);
        });
}

QFuture<void> NotebooksHandler::putNotebook(qevercloud::Notebook notebook)
{
    return makeWriteTask<void>(
        makeTaskContext(),
        weak_from_this(),
        [notebook = std::move(notebook)]
        (NotebooksHandler & /* handler */, QSqlDatabase & database,
         ErrorString & errorDescription) mutable
        {
            return utils::putNotebook(
                std::move(notebook), database, errorDescription);
        });
}

QFuture<qevercloud::Notebook> NotebooksHandler::findNotebookByLocalId(
    QString localId) const
{
    return makeReadTask<qevercloud::Notebook>(
        makeTaskContext(),
        weak_from_this(),
        [localId = std::move(localId)]
        (const NotebooksHandler & handler, QSqlDatabase & database,
         ErrorString & errorDescription)
        {
            return handler.findNotebookByLocalIdImpl(
                localId, database, errorDescription);
        });
}

QFuture<qevercloud::Notebook> NotebooksHandler::findNotebookByGuid(
    qevercloud::Guid guid) const
{
    return makeReadTask<qevercloud::Notebook>(
        makeTaskContext(),
        weak_from_this(),
        [guid = std::move(guid)]
        (const NotebooksHandler & handler, QSqlDatabase & database,
         ErrorString & errorDescription) mutable
        {
            return handler.findNotebookByGuidImpl(
                guid, database, errorDescription);
        });
}

QFuture<qevercloud::Notebook> NotebooksHandler::findNotebookByName(
    QString name, std::optional<QString> linkedNotebookGuid) const
{
    return makeReadTask<qevercloud::Notebook>(
        makeTaskContext(),
        weak_from_this(),
        [name = std::move(name),
         linkedNotebookGuid = std::move(linkedNotebookGuid)]
        (const NotebooksHandler & handler, QSqlDatabase & database,
         ErrorString & errorDescription) mutable
        {
            return handler.findNotebookByNameImpl(
                name, linkedNotebookGuid, database, errorDescription);
        });
}

QFuture<qevercloud::Notebook> NotebooksHandler::findDefaultNotebook() const
{
    return makeReadTask<qevercloud::Notebook>(
        makeTaskContext(),
        weak_from_this(),
        [](const NotebooksHandler & handler, QSqlDatabase & database,
           ErrorString & errorDescription)
        {
            return handler.findDefaultNotebookImpl(database, errorDescription);
        });
}

QFuture<void> NotebooksHandler::expungeNotebookByLocalId(QString localId)
{
    return makeWriteTask<void>(
        makeTaskContext(),
        weak_from_this(),
        [localId = std::move(localId)]
        (NotebooksHandler & handler, QSqlDatabase & database,
         ErrorString & errorDescription)
        {
            return handler.expungeNotebookByLocalIdImpl(
                localId, database, errorDescription);
        });
}

QFuture<void> NotebooksHandler::expungeNotebookByGuid(qevercloud::Guid guid)
{
    return makeWriteTask<void>(
        makeTaskContext(),
        weak_from_this(),
        [guid = std::move(guid)]
        (NotebooksHandler & handler, QSqlDatabase & database,
         ErrorString & errorDescription)
        {
            return handler.expungeNotebookByGuidImpl(
                guid, database, errorDescription);
        });
}

QFuture<void> NotebooksHandler::expungeNotebookByName(
    QString name, std::optional<QString> linkedNotebookGuid)
{
    return makeWriteTask<void>(
        makeTaskContext(),
        weak_from_this(),
        [name = std::move(name),
         linkedNotebookGuid = std::move(linkedNotebookGuid)]
        (NotebooksHandler & handler, QSqlDatabase & database,
         ErrorString & errorDescription)
        {
            return handler.expungeNotebookByNameImpl(
                name, linkedNotebookGuid, database, errorDescription);
        });
}

QFuture<QList<qevercloud::Notebook>> NotebooksHandler::listNotebooks(
    ListOptions<ListNotebooksOrder> options,
    std::optional<QString> linkedNotebookGuid) const
{
    return makeReadTask<QList<qevercloud::Notebook>>(
        makeTaskContext(),
        weak_from_this(),
        [options = std::move(options),
         linkedNotebookGuid = std::move(linkedNotebookGuid)]
        (const NotebooksHandler & handler, QSqlDatabase & database,
         ErrorString & errorDescription)
        {
            return handler.listNotebooksImpl(
                options, linkedNotebookGuid, database,
                errorDescription);
        });
}

QFuture<QList<qevercloud::SharedNotebook>> NotebooksHandler::listSharedNotebooks(
    qevercloud::Guid notebookGuid) const
{
    return makeReadTask<QList<qevercloud::SharedNotebook>>(
        makeTaskContext(),
        weak_from_this(),
        [notebookGuid = std::move(notebookGuid)]
        (const NotebooksHandler & handler, QSqlDatabase & database,
         ErrorString & errorDescription)
        {
            return handler.listSharedNotebooksImpl(
                notebookGuid, database, errorDescription);
        });
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

std::optional<qevercloud::Notebook> NotebooksHandler::findNotebookByLocalIdImpl(
    const QString & localId, QSqlDatabase & database,
    ErrorString & errorDescription) const
{
    static const QString queryString = QStringLiteral(
        "SELECT * FROM Notebooks "
        "LEFT OUTER JOIN NotebookRestrictions ON "
        "Notebooks.localUid = NotebookRestrictions.localUid "
        "LEFT OUTER JOIN Users ON "
        "Notebooks.contactId = Users.id "
        "LEFT OUTER JOIN UserAttributes ON "
        "Notebooks.contactId = UserAttributes.id "
        "LEFT OUTER JOIN UserAttributesViewedPromotions ON "
        "Notebooks.contactId = UserAttributesViewedPromotions.id "
        "LEFT OUTER JOIN UserAttributesRecentMailedAddresses ON "
        "Notebooks.contactId = UserAttributesRecentMailedAddresses.id "
        "LEFT OUTER JOIN Accounting ON "
        "Notebooks.contactId = Accounting.id "
        "LEFT OUTER JOIN AccountLimits ON "
        "Notebooks.contactId = AccountLimits.id "
        "LEFT OUTER JOIN BusinessUserInfo ON "
        "Notebooks.contactId = BusinessUserInfo.id "
        "WHERE (Notebooks.localUid = :localUid");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::NotebooksHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::NotebooksHandler",
            "Cannot find notebook in the local storage database by local id: "
            "failed to prepare query"),
        std::nullopt);

    query.bindValue(QStringLiteral(":localUid"), localId);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::NotebooksHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::NotebooksHandler",
            "Cannot find notebook in the local storage database by local id"),
        std::nullopt);

    if (!query.next()) {
        return std::nullopt;
    }

    const auto record = query.record();
    qevercloud::Notebook notebook;
    ErrorString error;
    if (!utils::fillNotebookFromSqlRecord(record, notebook, error)) {
        errorDescription.setBase(
            QT_TRANSLATE_NOOP(
                "local_storage::sql::NotebooksHandler",
                "Failed to find notebook by local id in the local storage "
                "database"));
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING("local_storage::sql::NotebooksHandler", errorDescription);
        return std::nullopt;
    }

    return fillSharedNotebooks(notebook, database, errorDescription);
}

std::optional<qevercloud::Notebook> NotebooksHandler::findNotebookByGuidImpl(
    const qevercloud::Guid & guid, QSqlDatabase & database,
    ErrorString & errorDescription) const
{
    static const QString queryString = QStringLiteral(
        "SELECT * FROM Notebooks "
        "LEFT OUTER JOIN NotebookRestrictions ON "
        "Notebooks.localUid = NotebookRestrictions.localUid "
        "LEFT OUTER JOIN Users ON "
        "Notebooks.contactId = Users.id "
        "LEFT OUTER JOIN UserAttributes ON "
        "Notebooks.contactId = UserAttributes.id "
        "LEFT OUTER JOIN UserAttributesViewedPromotions ON "
        "Notebooks.contactId = UserAttributesViewedPromotions.id "
        "LEFT OUTER JOIN UserAttributesRecentMailedAddresses ON "
        "Notebooks.contactId = UserAttributesRecentMailedAddresses.id "
        "LEFT OUTER JOIN Accounting ON "
        "Notebooks.contactId = Accounting.id "
        "LEFT OUTER JOIN AccountLimits ON "
        "Notebooks.contactId = AccountLimits.id "
        "LEFT OUTER JOIN BusinessUserInfo ON "
        "Notebooks.contactId = BusinessUserInfo.id "
        "WHERE (Notebooks.guid = :guid");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::NotebooksHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::NotebooksHandler",
            "Cannot find notebook in the local storage database by guid: "
            "failed to prepare query"),
        std::nullopt);

    query.bindValue(QStringLiteral(":guid"), guid);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::NotebooksHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::NotebooksHandler",
            "Cannot find notebook in the local storage database by guid"),
        std::nullopt);

    if (!query.next()) {
        return std::nullopt;
    }

    const auto record = query.record();
    qevercloud::Notebook notebook;
    ErrorString error;
    if (!utils::fillNotebookFromSqlRecord(record, notebook, error)) {
        errorDescription.setBase(
            QT_TRANSLATE_NOOP(
                "local_storage::sql::NotebooksHandler",
                "Failed to find notebook by guid in the local storage "
                "database"));
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING("local_storage::sql::NotebooksHandler", errorDescription);
        return std::nullopt;
    }

    return fillSharedNotebooks(notebook, database, errorDescription);
}

std::optional<qevercloud::Notebook> NotebooksHandler::findNotebookByNameImpl(
    const QString & name, const std::optional<QString> & linkedNotebookGuid,
    QSqlDatabase & database, ErrorString & errorDescription) const
{
    QString queryString = QStringLiteral(
        "SELECT * FROM Notebooks "
        "LEFT OUTER JOIN NotebookRestrictions ON "
        "Notebooks.localUid = NotebookRestrictions.localUid "
        "LEFT OUTER JOIN Users ON "
        "Notebooks.contactId = Users.id "
        "LEFT OUTER JOIN UserAttributes ON "
        "Notebooks.contactId = UserAttributes.id "
        "LEFT OUTER JOIN UserAttributesViewedPromotions ON "
        "Notebooks.contactId = UserAttributesViewedPromotions.id "
        "LEFT OUTER JOIN UserAttributesRecentMailedAddresses ON "
        "Notebooks.contactId = UserAttributesRecentMailedAddresses.id "
        "LEFT OUTER JOIN Accounting ON "
        "Notebooks.contactId = Accounting.id "
        "LEFT OUTER JOIN AccountLimits ON "
        "Notebooks.contactId = AccountLimits.id "
        "LEFT OUTER JOIN BusinessUserInfo ON "
        "Notebooks.contactId = BusinessUserInfo.id "
        "WHERE (Notebooks.notebookNameUpper = :notebookNameUpper)");

    if (linkedNotebookGuid) {
        queryString.chop(1);
        queryString += QStringLiteral(" AND Notebooks.linkedNotebookGuid ");

        if (linkedNotebookGuid->isEmpty()) {
            queryString += QStringLiteral("IS NULL)");
        }
        else {
            queryString += QStringLiteral(" = :linkedNotebookGuid)");
        }
    }

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::NotebooksHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::NotebooksHandler",
            "Cannot find notebook in the local storage database by guid: "
            "failed to prepare query"),
        std::nullopt);

    query.bindValue(QStringLiteral(":notebookNameUpper"), name.toUpper());

    if (linkedNotebookGuid && !linkedNotebookGuid->isEmpty()) {
        query.bindValue(
            QStringLiteral(":linkedNotebookGuid"), *linkedNotebookGuid);
    }

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::NotebooksHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::NotebooksHandler",
            "Cannot find notebook in the local storage database by name"),
        std::nullopt);

    if (!query.next()) {
        return std::nullopt;
    }

    const auto record = query.record();
    qevercloud::Notebook notebook;
    ErrorString error;
    if (!utils::fillNotebookFromSqlRecord(record, notebook, error)) {
        errorDescription.setBase(
            QT_TRANSLATE_NOOP(
                "local_storage::sql::NotebooksHandler",
                "Failed to find notebook by name in the local storage "
                "database"));
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING("local_storage::sql::NotebooksHandler", errorDescription);
        return std::nullopt;
    }

    return fillSharedNotebooks(notebook, database, errorDescription);
}

std::optional<qevercloud::Notebook> NotebooksHandler::findDefaultNotebookImpl(
    QSqlDatabase & database, ErrorString & errorDescription) const
{
    static const QString queryString = QStringLiteral(
        "SELECT * FROM Notebooks "
        "LEFT OUTER JOIN NotebookRestrictions ON "
        "Notebooks.localUid = NotebookRestrictions.localUid "
        "LEFT OUTER JOIN Users ON "
        "Notebooks.contactId = Users.id "
        "LEFT OUTER JOIN UserAttributes ON "
        "Notebooks.contactId = UserAttributes.id "
        "LEFT OUTER JOIN UserAttributesViewedPromotions ON "
        "Notebooks.contactId = UserAttributesViewedPromotions.id "
        "LEFT OUTER JOIN UserAttributesRecentMailedAddresses ON "
        "Notebooks.contactId = UserAttributesRecentMailedAddresses.id "
        "LEFT OUTER JOIN Accounting ON "
        "Notebooks.contactId = Accounting.id "
        "LEFT OUTER JOIN AccountLimits ON "
        "Notebooks.contactId = AccountLimits.id "
        "LEFT OUTER JOIN BusinessUserInfo ON "
        "Notebooks.contactId = BusinessUserInfo.id "
        "WHERE isDefault = 1 LIMIT 1");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::NotebooksHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::NotebooksHandler",
            "Cannot find default notebook in the local storage database: "
            "failed to prepare query"),
        std::nullopt);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::NotebooksHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::NotebooksHandler",
            "Cannot find default notebook in the local storage database"),
        std::nullopt);

    if (!query.next()) {
        return std::nullopt;
    }

    const auto record = query.record();
    qevercloud::Notebook notebook;
    ErrorString error;
    if (!utils::fillNotebookFromSqlRecord(record, notebook, error)) {
        errorDescription.setBase(
            QT_TRANSLATE_NOOP(
                "local_storage::sql::NotebooksHandler",
                "Failed to find default notebook in the local storage "
                "database"));
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING("local_storage::sql::NotebooksHandler", errorDescription);
        return std::nullopt;
    }

    return fillSharedNotebooks(notebook, database, errorDescription);
}

std::optional<qevercloud::Notebook> NotebooksHandler::fillSharedNotebooks(
    qevercloud::Notebook & notebook, QSqlDatabase & database,
    ErrorString & errorDescription) const
{
    if (!notebook.guid()) {
        return notebook;
    }

    auto sharedNotebooks = listSharedNotebooksImpl(
        *notebook.guid(), database, errorDescription);

    if (!errorDescription.isEmpty()) {
        QNWARNING("local_storage::sql::NotebooksHandler", errorDescription);
        return std::nullopt;
    }

    if (!sharedNotebooks.isEmpty()) {
        notebook.setSharedNotebooks(std::move(sharedNotebooks));
    }

    return notebook;
}


bool NotebooksHandler::expungeNotebookByLocalIdImpl(
    const QString & localId, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    QNDEBUG(
        "local_storage::sql::NotebooksHandler",
        "NotebooksHandler::expungeNotebookByLocalIdImpl: local id = "
            << localId);

    const auto noteLocalIds = listNoteLocalIdsByNotebookLocalId(
        localId, database, errorDescription);
    if (!errorDescription.isEmpty()) {
        return false;
    }

    static const QString queryString = QStringLiteral(
        "DELETE FROM Notebooks WHERE localUid = :localUid");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::NotebooksHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::NotebooksHandler",
            "Cannot expunge notebook by local id from the local storage "
            "database: failed to prepare query"),
        false);

    query.bindValue(QStringLiteral(":localUid"), localId);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::NotebooksHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::NotebooksHandler",
            "Cannot expunge notebook by local id from the local storage "
            "database"),
        false);

    for (const auto & noteLocalId: qAsConst(noteLocalIds)) {
        if (!utils::removeResourceDataFilesForNote(
                noteLocalId, m_localStorageDir, errorDescription)) {
            return false;
        }
    }

    return true;
}

bool NotebooksHandler::expungeNotebookByGuidImpl(
    const qevercloud::Guid & guid, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    QNDEBUG(
        "local_storage::sql::NotebooksHandler",
        "NotebooksHandler::expungeNotebookByGuidImpl: guid = " << guid);

    const auto localId = utils::notebookLocalIdByGuid(guid, database, errorDescription);
    if (!errorDescription.isEmpty()) {
        return false;
    }

    if (localId.isEmpty()) {
        // No such notebook exists in the local storage
        return true;
    }

    return expungeNotebookByLocalIdImpl(localId, database, errorDescription);
}

bool NotebooksHandler::expungeNotebookByNameImpl(
    const QString & name, const std::optional<QString> & linkedNotebookGuid,
    QSqlDatabase & database, ErrorString & errorDescription)
{
    QNDEBUG(
        "local_storage::sql::NotebooksHandler",
        "NotebooksHandler::expungeNotebookByNameImpl: name = "
            << name << ", linked notebook guid = "
            << linkedNotebookGuid.value_or(QStringLiteral("<not set>")));

    const auto localId = utils::notebookLocalIdByName(
        name, linkedNotebookGuid, database, errorDescription);

    if (!errorDescription.isEmpty()) {
        return false;
    }

    if (localId.isEmpty()) {
        // No such notebook exists in the local storage
        return true;
    }

    return expungeNotebookByLocalIdImpl(localId, database, errorDescription);
}

QStringList NotebooksHandler::listNoteLocalIdsByNotebookLocalId(
    const QString & notebookLocalId, QSqlDatabase & database,
    ErrorString & errorDescription) const
{
    static const QString queryString = QStringLiteral(
        "SELECT localUid FROM Notes "
        "WHERE notebookLocalUid = :notebookLocalUid");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::NotebooksHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::NotebooksHandler",
            "Cannot list note local ids by notebook local id from the local "
            "storage database: failed to prepare query"),
        {});

    query.bindValue(QStringLiteral(":notebookLocalUid"), notebookLocalId);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::NotebooksHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::NotebooksHandler",
            "Cannot list note local ids by notebook local id from the local "
            "storage database"),
        {});

    QStringList noteLocalIds;
    noteLocalIds.reserve(std::max(query.size(), 0));

    while (query.next()) {
        noteLocalIds << query.value(0).toString();
    }

    return noteLocalIds;
}

QList<qevercloud::Notebook> NotebooksHandler::listNotebooksImpl(
    const ListOptions<ListNotebooksOrder> & options,
    const std::optional<QString> & linkedNotebookGuid,
    QSqlDatabase & database, ErrorString & errorDescription) const
{
    // TODO: implement
    Q_UNUSED(options)
    Q_UNUSED(linkedNotebookGuid)
    Q_UNUSED(database)
    Q_UNUSED(errorDescription)
    return {};
}

QList<qevercloud::SharedNotebook> NotebooksHandler::listSharedNotebooksImpl(
    const qevercloud::Guid & notebookGuid, QSqlDatabase & database,
    ErrorString & errorDescription) const
{
    QSqlQuery query{database};
    bool res = query.prepare(QStringLiteral(
        "SELECT * FROM SharedNotebooks "
        "WHERE sharedNotebookNotebookGuid = :sharedNotebookNotebookGuid"));

    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::NotebooksHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::NotebooksHandler",
            "Cannot list shared notebooks by notebook guid from the local "
            "storage database: failed to prepare query"),
        {});

    query.bindValue(
        QStringLiteral(":sharedNotebookNotebookGuid"), notebookGuid);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::NotebooksHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::NotebooksHandler",
            "Cannot list shared notebooks by notebook guid from the local "
            "storage database"),
        {});

    QMap<int, qevercloud::SharedNotebook> sharedNotebooksByIndex;
    while (query.next()) {
        qevercloud::SharedNotebook sharedNotebook;
        int indexInNotebook = -1;
        ErrorString error;
        if (!utils::fillSharedNotebookFromSqlRecord(
                query.record(), sharedNotebook, indexInNotebook,
                errorDescription)) {
            return {};
        }

        sharedNotebooksByIndex[indexInNotebook] = sharedNotebook;
    }

    QList<qevercloud::SharedNotebook> sharedNotebooks;
    sharedNotebooks.reserve(qMax(sharedNotebooksByIndex.size(), 0));
    for (const auto it: qevercloud::toRange(sharedNotebooksByIndex)) {
        sharedNotebooks << it.value();
    }

    return sharedNotebooks;
}

TaskContext NotebooksHandler::makeTaskContext() const
{
    return TaskContext{
        m_threadPool,
        m_writerThread,
        m_connectionPool,
        ErrorString{QT_TRANSLATE_NOOP(
                "local_storage::sql::NotebooksHandler",
                "NotebooksHandler is already destroyed")},
        ErrorString{QT_TRANSLATE_NOOP(
                "local_storage::sql::NotebooksHandler",
                "Request has been canceled")}};
}

} // namespace quentier::local_storage::sql
