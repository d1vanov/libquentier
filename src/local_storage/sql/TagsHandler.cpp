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

#include "utils/FillFromSqlRecordUtils.h"
#include "utils/ListFromDatabaseUtils.h"
#include "utils/PutToDatabaseUtils.h"
#include "utils/SqlUtils.h"
#include "utils/TagUtils.h"

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

#include <algorithm>

namespace quentier::local_storage::sql {

TagsHandler::TagsHandler(
    ConnectionPoolPtr connectionPool, QThreadPool * threadPool,
    Notifier * notifier, QThreadPtr writerThread) :
    m_connectionPool{std::move(connectionPool)},
    m_threadPool{threadPool}, m_notifier{notifier}, m_writerThread{
                                                        std::move(writerThread)}
{
    if (Q_UNLIKELY(!m_connectionPool)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "local_storage::sql::TagsHandler",
            "TagsHandler ctor: connection pool is null")}};
    }

    if (Q_UNLIKELY(!m_threadPool)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "local_storage::sql::TagsHandler",
            "TagsHandler ctor: thread pool is null")}};
    }

    if (Q_UNLIKELY(!m_notifier)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "local_storage::sql::TagsHandler",
            "TagsHandler ctor: notifier is null")}};
    }

    if (Q_UNLIKELY(!m_writerThread)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "local_storage::sql::TagsHandler",
            "TagsHandler ctor: writer thread is null")}};
    }
}

QFuture<quint32> TagsHandler::tagCount() const
{
    return makeReadTask<quint32>(
        makeTaskContext(), weak_from_this(),
        [](const TagsHandler & handler, QSqlDatabase & database,
           ErrorString & errorDescription) {
            return handler.tagCountImpl(database, errorDescription);
        });
}

QFuture<void> TagsHandler::putTag(qevercloud::Tag tag)
{
    return makeWriteTask<void>(
        makeTaskContext(), weak_from_this(),
        [tag = std::move(tag)](
            TagsHandler & handler, QSqlDatabase & database,
            ErrorString & errorDescription) mutable {
            const bool res = utils::putTag(tag, database, errorDescription);

            if (res) {
                handler.m_notifier->notifyTagPut(tag);
            }

            return res;
        });
}

QFuture<qevercloud::Tag> TagsHandler::findTagByLocalId(QString localId) const
{
    return makeReadTask<qevercloud::Tag>(
        makeTaskContext(), weak_from_this(),
        [localId = std::move(localId)](
            const TagsHandler & handler, QSqlDatabase & database,
            ErrorString & errorDescription) {
            return handler.findTagByLocalIdImpl(
                localId, database, errorDescription);
        });
}

QFuture<qevercloud::Tag> TagsHandler::findTagByGuid(qevercloud::Guid guid) const
{
    return makeReadTask<qevercloud::Tag>(
        makeTaskContext(), weak_from_this(),
        [guid = std::move(guid)](
            const TagsHandler & handler, QSqlDatabase & database,
            ErrorString & errorDescription) mutable {
            return handler.findTagByGuidImpl(guid, database, errorDescription);
        });
}

QFuture<qevercloud::Tag> TagsHandler::findTagByName(
    QString name, std::optional<qevercloud::Guid> linkedNotebookGuid) const
{
    return makeReadTask<qevercloud::Tag>(
        makeTaskContext(), weak_from_this(),
        [name = std::move(name),
         linkedNotebookGuid = std::move(linkedNotebookGuid)](
            const TagsHandler & handler, QSqlDatabase & database,
            ErrorString & errorDescription) mutable {
            return handler.findTagByNameImpl(
                name, linkedNotebookGuid, database, errorDescription);
        });
}

QFuture<void> TagsHandler::expungeTagByLocalId(QString localId)
{
    return makeWriteTask<void>(
        makeTaskContext(), weak_from_this(),
        [localId = std::move(localId)](
            TagsHandler & handler, QSqlDatabase & database,
            ErrorString & errorDescription) {
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
        makeTaskContext(), weak_from_this(),
        [guid = std::move(guid)](
            TagsHandler & handler, QSqlDatabase & database,
            ErrorString & errorDescription) {
            const auto res =
                handler.expungeTagByGuidImpl(guid, database, errorDescription);

            if (res.status) {
                handler.m_notifier->notifyTagExpunged(
                    res.expungedTagLocalId, res.expungedChildTagLocalIds);
            }

            return res.status;
        });
}

QFuture<void> TagsHandler::expungeTagByName(
    QString name, std::optional<qevercloud::Guid> linkedNotebookGuid)
{
    return makeWriteTask<void>(
        makeTaskContext(), weak_from_this(),
        [name = std::move(name),
         linkedNotebookGuid = std::move(linkedNotebookGuid)](
            TagsHandler & handler, QSqlDatabase & database,
            ErrorString & errorDescription) {
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
        makeTaskContext(), weak_from_this(),
        [options = std::move(options)](
            const TagsHandler & handler, QSqlDatabase & database,
            ErrorString & errorDescription) {
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
    QSqlQuery query{database};
    const bool res =
        query.exec(QStringLiteral("SELECT COUNT(localUid) FROM Tags"));

    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::TagsHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::TagsHandler",
            "Cannot count tags in the local storage database"),
        std::nullopt);

    if (!query.next()) {
        QNDEBUG(
            "local_storage::sql::TagsHandler",
            "Found no tags in the local storage database");
        return 0;
    }

    bool conversionResult = false;
    const int count = query.value(0).toInt(&conversionResult);
    if (Q_UNLIKELY(!conversionResult)) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "local_storage::sql::TagsHandler",
            "Cannot count tags in the local storage database: failed "
            "to convert tag count to int"));
        QNWARNING("local_storage:sql", errorDescription);
        return std::nullopt;
    }

    return count;
}

std::optional<qevercloud::Tag> TagsHandler::findTagByLocalIdImpl(
    const QString & localId, QSqlDatabase & database,
    ErrorString & errorDescription) const
{
    static const QString queryString = QStringLiteral(
        "SELECT localUid, guid, linkedNotebookGuid, "
        "updateSequenceNumber, name, parentGuid, "
        "parentLocalUid, isDirty, isLocal, isLocal, isFavorited "
        "FROM Tags WHERE (localUid = :localUid)");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::TagsHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::TagsHandler",
            "Cannot find tag in the local storage database by local id: "
            "failed to prepare query"),
        std::nullopt);

    query.bindValue(QStringLiteral(":localUid"), localId);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::TagsHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::TagsHandler",
            "Cannot find tag in the local storage database by local id"),
        std::nullopt);

    if (!query.next()) {
        return std::nullopt;
    }

    const auto record = query.record();
    qevercloud::Tag tag;
    ErrorString error;
    if (!utils::fillTagFromSqlRecord(record, tag, error)) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "local_storage::sql::TagsHandler",
            "Failed to find tag by local id in the local storage "
            "database"));
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING("local_storage::sql::TagsHandler", errorDescription);
        return std::nullopt;
    }

    return tag;
}

std::optional<qevercloud::Tag> TagsHandler::findTagByGuidImpl(
    const qevercloud::Guid & guid, QSqlDatabase & database,
    ErrorString & errorDescription) const
{
    static const QString queryString = QStringLiteral(
        "SELECT localUid, guid, linkedNotebookGuid, "
        "updateSequenceNumber, name, parentGuid, "
        "parentLocalUid, isDirty, isLocal, isLocal, isFavorited "
        "FROM Tags WHERE (guid = :guid)");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::TagsHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::TagsHandler",
            "Cannot find tag in the local storage database by guid: "
            "failed to prepare query"),
        std::nullopt);

    query.bindValue(QStringLiteral(":guid"), guid);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::TagsHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::TagsHandler",
            "Cannot find tag in the local storage database by guid"),
        std::nullopt);

    if (!query.next()) {
        return std::nullopt;
    }

    const auto record = query.record();
    qevercloud::Tag tag;
    ErrorString error;
    if (!utils::fillTagFromSqlRecord(record, tag, error)) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "local_storage::sql::TagsHandler",
            "Failed to find tag by guid in the local storage database"));
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING("local_storage::sql::TagsHandler", errorDescription);
        return std::nullopt;
    }

    return tag;
}

std::optional<qevercloud::Tag> TagsHandler::findTagByNameImpl(
    const QString & name,
    const std::optional<qevercloud::Guid> & linkedNotebookGuid,
    QSqlDatabase & database, ErrorString & errorDescription) const
{
    QString queryString = QStringLiteral(
        "SELECT localUid, guid, linkedNotebookGuid, "
        "updateSequenceNumber, name, parentGuid, "
        "parentLocalUid, isDirty, isLocal, isLocal, isFavorited "
        "FROM Tags WHERE (nameLower = :nameLower)");

    if (linkedNotebookGuid) {
        queryString.chop(1);
        queryString += QStringLiteral(" AND linkedNotebookGuid ");

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
        res, query, "local_storage::sql::TagsHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::TagsHandler",
            "Cannot find tag in the local storage database by name: "
            "failed to prepare query"),
        std::nullopt);

    // Legacy behaviour affecting only tags: due to a mistake tags nameLower
    // field contains lowercase names of tags which were also cleared from
    // diacritics. So need to search by lowercase name with removed diacritics
    // as well + need to check that actual name of the tag matches and only
    // in that case return non-nullopt result.
    auto nameLower = name.toLower();
    m_stringUtils.removeDiacritics(nameLower);

    query.bindValue(QStringLiteral(":nameLower"), nameLower);

    if (linkedNotebookGuid && !linkedNotebookGuid->isEmpty()) {
        query.bindValue(
            QStringLiteral(":linkedNotebookGuid"), *linkedNotebookGuid);
    }

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::TagsHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::TagsHandler",
            "Cannot find tag in the local storage database by name"),
        std::nullopt);

    while (query.next()) {
        const auto record = query.record();
        qevercloud::Tag tag;
        ErrorString error;
        if (!utils::fillTagFromSqlRecord(record, tag, error)) {
            errorDescription.setBase(QT_TRANSLATE_NOOP(
                "local_storage::sql::TagsHandler",
                "Failed to find tag by name in the local storage database"));
            errorDescription.appendBase(error.base());
            errorDescription.appendBase(error.additionalBases());
            errorDescription.details() = error.details();
            QNWARNING("local_storage::sql::TagsHandler", errorDescription);
            return std::nullopt;
        }

        if (tag.name() && *tag.name() != name) {
            continue;
        }

        return tag;
    }

    return std::nullopt;
}

QStringList TagsHandler::listChildTagLocalIds(
    const QString & tagLocalId, QSqlDatabase & database,
    ErrorString & errorDescription) const
{
    static const QString queryString = QStringLiteral(
        "SELECT localUid FROM Tags WHERE parentLocalUid = :localUid");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::TagsHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::TagsHandler",
            "Cannot list child tag local ids from the local storage database: "
            "failed to prepare query"),
        {});

    query.bindValue(QStringLiteral(":localUid"), tagLocalId);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::TagsHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::TagsHandler",
            "Cannot list child tag local ids from the local storage database"),
        {});

    QStringList result;
    result.reserve(std::max(query.size(), 0));
    while (query.next()) {
        const QSqlRecord record = query.record();

        const int index = record.indexOf(QStringLiteral("localUid"));
        if (Q_UNLIKELY(index < 0)) {
            continue;
        }

        const QVariant value = record.value(index);
        if (Q_UNLIKELY(value.isNull())) {
            continue;
        }

        const QString childTagLocalId = value.toString();
        if (Q_UNLIKELY(childTagLocalId.isEmpty())) {
            continue;
        }

        result << childTagLocalId;
    }

    return result;
}

TagsHandler::ExpungeTagResult TagsHandler::expungeTagByLocalIdImpl(
    const QString & localId, QSqlDatabase & database,
    ErrorString & errorDescription, std::optional<Transaction> transaction,
    const utils::TransactionOption transactionOption)
{
    QNDEBUG(
        "local_storage::sql::TagsHandler",
        "TagsHandler::expungeTagByLocalIdImpl: local id = " << localId);

    if (transactionOption == utils::TransactionOption::UseSeparateTransaction &&
        !transaction)
    {
        transaction.emplace(database, Transaction::Type::Exclusive);
    }

    ErrorString error;
    const auto childTagLocalIds =
        listChildTagLocalIds(localId, database, error);

    if (childTagLocalIds.isEmpty() && !error.isEmpty()) {
        errorDescription = error;
        return ExpungeTagResult{false, {}, {}};
    }

    ExpungeTagResult result;

    for (const auto & childTagLocalId: qAsConst(childTagLocalIds)) {
        ErrorString error;
        const auto res = expungeTagByLocalIdImpl(
            childTagLocalId, database, error, std::nullopt,
            utils::TransactionOption::DontUseSeparateTransaction);

        if (!res.status) {
            errorDescription.setBase(QT_TRANSLATE_NOOP(
                "local_storage::sql::TagsHandler",
                "Cannot expunge tag from the local storage database: "
                "failed to expunge one of child tags"));
            errorDescription.appendBase(error.base());
            errorDescription.appendBase(error.additionalBases());
            errorDescription.details() = error.details();
            QNWARNING("local_storage::sql::TagsHandler", errorDescription);
            return ExpungeTagResult{false, {}, {}};
        }

        result.expungedChildTagLocalIds << childTagLocalId;
        result.expungedChildTagLocalIds << res.expungedChildTagLocalIds;
    }

    static const QString queryString =
        QStringLiteral("DELETE FROM Tags WHERE localUid = :localUid");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::TagsHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::TagsHandler",
            "Cannot expunge tag from the local storage database by local id: "
            "failed to prepare query"),
        (ExpungeTagResult{false, {}, {}}));

    query.bindValue(QStringLiteral(":localUid"), localId);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::TagsHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::TagsHandler",
            "Cannot expunge tag from the local storage database by local id"),
        (ExpungeTagResult{false, {}, {}}));

    result.status = true;
    result.expungedTagLocalId = localId;

    if (transaction) {
        res = transaction->commit();
        ENSURE_DB_REQUEST_RETURN(
            res, database, "local_storage::sql::TagsHandler",
            QT_TRANSLATE_NOOP(
                "local_storage::sql::TagsHandler",
                "Cannot expunge tag from the local storage database, failed to "
                "commit transaction"),
            (ExpungeTagResult{false, {}, {}}));
    }

    return result;
}

TagsHandler::ExpungeTagResult TagsHandler::expungeTagByGuidImpl(
    const qevercloud::Guid & guid, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    QNDEBUG(
        "local_storage::sql::TagsHandler",
        "TagsHandler::expungeTagByGuidImpl: guid = " << guid);

    Transaction transaction{database, Transaction::Type::Exclusive};

    const auto localId =
        utils::tagLocalIdByGuid(guid, database, errorDescription);

    if (!errorDescription.isEmpty()) {
        return ExpungeTagResult{false, {}, {}};
    }

    if (localId.isEmpty()) {
        QNDEBUG(
            "local_storage::sql::TagsHandler",
            "Found no tag local id for guid " << guid);
        return ExpungeTagResult{true, {}, {}};
    }

    QNDEBUG(
        "local_storage::sql::TagsHandler",
        "Found tag local id for guid " << guid << ": " << localId);

    return expungeTagByLocalIdImpl(
        localId, database, errorDescription, std::move(transaction),
        utils::TransactionOption::UseSeparateTransaction);
}

TagsHandler::ExpungeTagResult TagsHandler::expungeTagByNameImpl(
    const QString & name,
    const std::optional<qevercloud::Guid> & linkedNotebookGuid,
    QSqlDatabase & database, ErrorString & errorDescription)
{
    QNDEBUG(
        "local_storage::sql::TagHandler",
        "TagsHandler::expungeTagByNameImpl: name = "
            << name << ", linked notebook guid = "
            << linkedNotebookGuid.value_or(QStringLiteral("<not set>")));

    Transaction transaction{database, Transaction::Type::Exclusive};

    const auto localId = utils::tagLocalIdByName(
        name, linkedNotebookGuid, database, errorDescription);

    if (!errorDescription.isEmpty()) {
        return ExpungeTagResult{false, {}, {}};
    }

    if (localId.isEmpty()) {
        QNDEBUG(
            "local_storage::sql::TagsHandler",
            "Found no tag local id for name " << name);
        return ExpungeTagResult{true, {}, {}};
    }

    QNDEBUG(
        "local_storage::sql::TagsHandler",
        "Found tag local id for name " << name << ": " << localId);

    return expungeTagByLocalIdImpl(
        localId, database, errorDescription, std::move(transaction),
        utils::TransactionOption::UseSeparateTransaction);
}

QList<qevercloud::Tag> TagsHandler::listTagsImpl(
    const ListOptions<ListTagsOrder> & options, QSqlDatabase & database,
    ErrorString & errorDescription) const
{
    ErrorString error;
    QString linkedNotebookGuidSqlQueryCondition =
        utils::linkedNotebookGuidSqlQueryCondition(options, error);
    if (linkedNotebookGuidSqlQueryCondition.isEmpty() && !error.isEmpty()) {
        errorDescription = error;
        return {};
    }

    QString tagNotesRelationSqlQueryCondition;
    if (options.m_tagNotesRelation == TagNotesRelation::WithoutNotes) {
        tagNotesRelationSqlQueryCondition =
            QStringLiteral("localUid NOT IN (SELECT localTag FROM NoteTags)");
    }
    else if (options.m_tagNotesRelation == TagNotesRelation::WithNotes) {
        tagNotesRelationSqlQueryCondition =
            QStringLiteral("localUid IN (SELECT localTag FROM NoteTags)");
    }

    QString sqlQueryCondition;
    if (tagNotesRelationSqlQueryCondition.isEmpty()) {
        sqlQueryCondition = linkedNotebookGuidSqlQueryCondition;
    }
    else if (linkedNotebookGuidSqlQueryCondition.isEmpty()) {
        sqlQueryCondition = tagNotesRelationSqlQueryCondition;
    }
    else {
        QTextStream strm{&sqlQueryCondition};
        strm << "(" << linkedNotebookGuidSqlQueryCondition << ") AND ("
             << tagNotesRelationSqlQueryCondition << ")";
    }

    return utils::listObjects<qevercloud::Tag, ILocalStorage::ListTagsOrder>(
        options.m_flags, options.m_limit, options.m_offset, options.m_order,
        options.m_direction, sqlQueryCondition, database, errorDescription);
}

QList<qevercloud::Tag> TagsHandler::listTagsPerNoteLocalIdImpl(
    const QString & noteLocalId, const ListOptions<ListTagsOrder> & options,
    QSqlDatabase & database, ErrorString & errorDescription) const
{
    if (options.m_tagNotesRelation == TagNotesRelation::WithoutNotes) {
        QNWARNING(
            "local_storage::sql::TagsHandler",
            "Detected strange use of TagNotesRelation::WithoutNotes when "
            "listing tags per note local id");
        return {};
    }

    const auto noteLocalIdSqlQueryCondition =
        QString::fromUtf8("localNote = '%1'")
            .arg(utils::sqlEscape(noteLocalId));

    return utils::listObjects<qevercloud::Tag, ILocalStorage::ListTagsOrder>(
        options.m_flags, options.m_limit, options.m_offset, options.m_order,
        options.m_direction, noteLocalIdSqlQueryCondition, database,
        errorDescription);
}

TaskContext TagsHandler::makeTaskContext() const
{
    return TaskContext{
        m_threadPool, m_writerThread, m_connectionPool,
        ErrorString{QT_TRANSLATE_NOOP(
            "local_storage::sql::TagsHandler",
            "TagsHandler is already destroyed")},
        ErrorString{QT_TRANSLATE_NOOP(
            "local_storage::sql::TagsHandler", "Request has been canceled")}};
}

} // namespace quentier::local_storage::sql
