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
#include "NotesHandler.h"
#include "Notifier.h"
#include "Tasks.h"
#include "Transaction.h"
#include "TypeChecks.h"

#include "utils/Common.h"
#include "utils/FillFromSqlRecordUtils.h"
#include "utils/ListFromDatabaseUtils.h"
#include "utils/NoteUtils.h"
#include "utils/PutToDatabaseUtils.h"
#include "utils/ResourceDataFilesUtils.h"
#include "utils/SqlUtils.h"

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

#include <QReadLocker>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QTextStream>
#include <QThreadPool>
#include <QWriteLocker>

#include <algorithm>

namespace quentier::local_storage::sql {

namespace {

[[nodiscard]] QString noteCountOptionsToSqlQueryPart(
    const ILocalStorage::NoteCountOptions options)
{
    using NoteCountOption = ILocalStorage::NoteCountOption;
    using NoteCountOptions = ILocalStorage::NoteCountOptions;

    QString queryPart;
    if (!(options & NoteCountOption::IncludeNonDeletedNotes) ||
        !(options & NoteCountOption::IncludeDeletedNotes))
    {
        queryPart = QStringLiteral("deletionTimestamp IS ");
        if (options & NoteCountOption::IncludeNonDeletedNotes) {
            queryPart += QStringLiteral("NULL");
        }
        else {
            queryPart += QStringLiteral("NOT NULL");
        }
    }
    return queryPart;
}

} // namespace

NotesHandler::NotesHandler(
    ConnectionPoolPtr connectionPool, QThreadPool * threadPool,
    Notifier * notifier, QThreadPtr writerThread,
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
            "local_storage::sql::NotesHandler",
            "NotesHandler ctor: connection pool is null")}};
    }

    if (Q_UNLIKELY(!m_threadPool)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "local_storage::sql::NotesHandler",
            "NotesHandler ctor: thread pool is null")}};
    }

    if (Q_UNLIKELY(!m_notifier)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "local_storage::sql::NotesHandler",
            "NotesHandler ctor: notifier is null")}};
    }

    if (Q_UNLIKELY(!m_writerThread)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "local_storage::sql::NotesHandler",
            "NotesHandler ctor: writer thread is null")}};
    }

    if (Q_UNLIKELY(!m_localStorageDir.isReadable())) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "local_storage::sql::NotesHandler",
            "NotesHandler ctor: local storage dir is not readable")}};
    }

    if (Q_UNLIKELY(
            !m_localStorageDir.exists() &&
            !m_localStorageDir.mkpath(m_localStorageDir.absolutePath())))
    {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "local_storage::sql::NotesHandler",
            "NotesHandler ctor: local storage dir does not exist and "
            "cannot be created")}};
    }

    if (Q_UNLIKELY(!m_resourceDataFilesLock)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "local_storage::sql::NotesHandler",
            "NotesHandler ctor: resource data files lock is null")}};
    }
}

QFuture<quint32> NotesHandler::noteCount(NoteCountOptions options) const
{
    return makeReadTask<quint32>(
        makeTaskContext(), weak_from_this(),
        [options](
            const NotesHandler & handler, QSqlDatabase & database,
            ErrorString & errorDescription) {
            return handler.noteCountImpl(options, database, errorDescription);
        });
}

QFuture<quint32> NotesHandler::noteCountPerNotebookLocalId(
    QString notebookLocalId, NoteCountOptions options) const
{
    return makeReadTask<quint32>(
        makeTaskContext(), weak_from_this(),
        [notebookLocalId = std::move(notebookLocalId), options](
            const NotesHandler & handler, QSqlDatabase & database,
            ErrorString & errorDescription) {
            return handler.noteCountPerNotebookLocalIdImpl(
                notebookLocalId, options, database, errorDescription);
        });
}

QFuture<quint32> NotesHandler::noteCountPerTagLocalId(
    QString tagLocalId, NoteCountOptions options) const
{
    return makeReadTask<quint32>(
        makeTaskContext(), weak_from_this(),
        [tagLocalId = std::move(tagLocalId), options](
            const NotesHandler & handler, QSqlDatabase & database,
            ErrorString & errorDescription) {
            return handler.noteCountPerTagLocalIdImpl(
                tagLocalId, options, database, errorDescription);
        });
}

QFuture<QHash<QString, quint32>> NotesHandler::noteCountsPerTags(
    ListOptions<ListTagsOrder> listTagsOptions, NoteCountOptions options) const
{
    return makeReadTask<QHash<QString, quint32>>(
        makeTaskContext(), weak_from_this(),
        [listTagsOptions = std::move(listTagsOptions), options](
            const NotesHandler & handler, QSqlDatabase & database,
            ErrorString & errorDescription) {
            return handler.noteCountsPerTagsImpl(
                listTagsOptions, options, database, errorDescription);
        });
}

QFuture<quint32> NotesHandler::noteCountPerNotebookAndTagLocalIds(
    QStringList notebookLocalIds, QStringList tagLocalIds,
    NoteCountOptions options) const
{
    return makeReadTask<quint32>(
        makeTaskContext(), weak_from_this(),
        [notebookLocalIds = std::move(notebookLocalIds),
         tagLocalIds = std::move(tagLocalIds), options](
            const NotesHandler & handler, QSqlDatabase & database,
            ErrorString & errorDescription) {
            return handler.noteCountPerNotebookAndTagLocalIdsImpl(
                notebookLocalIds, tagLocalIds, options, database,
                errorDescription);
        });
}

QFuture<void> NotesHandler::putNote(qevercloud::Note note)
{
    return makeWriteTask<void>(
        makeTaskContext(), weak_from_this(),
        [note = std::move(note)](
            NotesHandler & handler, QSqlDatabase & database,
            ErrorString & errorDescription) mutable {
            QWriteLocker locker{handler.m_resourceDataFilesLock.get()};
            bool res = utils::putNote(
                handler.m_localStorageDir, note, database, errorDescription);
            if (res) {
                handler.m_notifier->notifyNotePut(note);
            }
            return res;
        });
}

QFuture<void> NotesHandler::updateNote(
    qevercloud::Note note, UpdateNoteOptions options)
{
    return makeWriteTask<void>(
        makeTaskContext(), weak_from_this(),
        [note = std::move(note), options](
            NotesHandler & handler, QSqlDatabase & database,
            ErrorString & errorDescription) mutable {
            std::optional<QWriteLocker> locker;
            if (options.testFlag(UpdateNoteOption::UpdateResourceBinaryData)) {
                locker.emplace(handler.m_resourceDataFilesLock.get());
            }
            bool res = handler.updateNoteImpl(
                note, options, database, errorDescription);
            if (res) {
                handler.m_notifier->notifyNoteUpdated(note, options);
            }
            return res;
        });
}

QFuture<qevercloud::Note> NotesHandler::findNoteByLocalId(
    QString localId, FetchNoteOptions options) const
{
    return makeReadTask<qevercloud::Note>(
        makeTaskContext(), weak_from_this(),
        [localId = std::move(localId), options](
            NotesHandler & handler, QSqlDatabase & database,
            ErrorString & errorDescription) {
            std::optional<QReadLocker> locker;
            if (options.testFlag(FetchNoteOption::WithResourceBinaryData)) {
                locker.emplace(handler.m_resourceDataFilesLock.get());
            }
            return handler.findNoteByLocalIdImpl(
                localId, options, database, errorDescription);
        });
}

QFuture<qevercloud::Note> NotesHandler::findNoteByGuid(
    qevercloud::Guid guid, FetchNoteOptions options) const
{
    return makeReadTask<qevercloud::Note>(
        makeTaskContext(), weak_from_this(),
        [guid = std::move(guid), options](
            NotesHandler & handler, QSqlDatabase & database,
            ErrorString & errorDescription) {
            std::optional<QReadLocker> locker;
            if (options.testFlag(FetchNoteOption::WithResourceBinaryData)) {
                locker.emplace(handler.m_resourceDataFilesLock.get());
            }
            return handler.findNoteByGuidImpl(
                guid, options, database, errorDescription);
        });
}

QFuture<void> NotesHandler::expungeNoteByLocalId(QString localId)
{
    return makeWriteTask<void>(
        makeTaskContext(), weak_from_this(),
        [localId = std::move(localId)](
            NotesHandler & handler, QSqlDatabase & database,
            ErrorString & errorDescription) {
            QWriteLocker locker{handler.m_resourceDataFilesLock.get()};
            bool res = handler.expungeNoteByLocalIdImpl(
                localId, database, errorDescription);
            if (res) {
                handler.m_notifier->notifyNoteExpunged(localId);
            }
            return res;
        });
}

QFuture<void> NotesHandler::expungeNoteByGuid(qevercloud::Guid guid)
{
    return makeWriteTask<void>(
        makeTaskContext(), weak_from_this(),
        [guid = std::move(guid)](
            NotesHandler & handler, QSqlDatabase & database,
            ErrorString & errorDescription) {
            QWriteLocker locker{handler.m_resourceDataFilesLock.get()};
            return handler.expungeNoteByGuidImpl(
                guid, database, errorDescription);
        });
}

QFuture<QList<qevercloud::Note>> NotesHandler::listNotes(
    FetchNoteOptions fetchOptions, ListOptions<ListNotesOrder> options) const
{
    return makeReadTask<QList<qevercloud::Note>>(
        makeTaskContext(), weak_from_this(),
        [fetchOptions, options = std::move(options)](
            NotesHandler & handler, QSqlDatabase & database,
            ErrorString & errorDescription) {
            std::optional<QReadLocker> locker;
            if (fetchOptions.testFlag(FetchNoteOption::WithResourceBinaryData))
            {
                locker.emplace(handler.m_resourceDataFilesLock.get());
            }
            return handler.listNotesImpl(
                fetchOptions, options, database, errorDescription);
        });
}

QFuture<QList<qevercloud::SharedNote>> NotesHandler::listSharedNotes(
    qevercloud::Guid guid) const
{
    return makeReadTask<QList<qevercloud::SharedNote>>(
        makeTaskContext(), weak_from_this(),
        [guid = std::move(guid)](
            NotesHandler & handler, QSqlDatabase & database,
            ErrorString & errorDescription) {
            return handler.listSharedNotesImpl(
                guid, database, errorDescription);
        });
}

QFuture<QList<qevercloud::Note>> NotesHandler::listNotesPerNotebookLocalId(
    QString notebookLocalId, FetchNoteOptions fetchOptions,
    ListOptions<ListNotesOrder> options) const
{
    return makeReadTask<QList<qevercloud::Note>>(
        makeTaskContext(), weak_from_this(),
        [notebookLocalId = std::move(notebookLocalId),
         options = std::move(options), fetchOptions](
            NotesHandler & handler, QSqlDatabase & database,
            ErrorString & errorDescription) {
            std::optional<QReadLocker> locker;
            if (fetchOptions.testFlag(FetchNoteOption::WithResourceBinaryData))
            {
                locker.emplace(handler.m_resourceDataFilesLock.get());
            }
            return handler.listNotesPerNotebookLocalIdImpl(
                notebookLocalId, fetchOptions, options, database,
                errorDescription);
        });
}

QFuture<QList<qevercloud::Note>> NotesHandler::listNotesPerTagLocalId(
    QString tagLocalId, FetchNoteOptions fetchOptions,
    ListOptions<ListNotesOrder> options) const
{
    return makeReadTask<QList<qevercloud::Note>>(
        makeTaskContext(), weak_from_this(),
        [tagLocalId = std::move(tagLocalId), options = std::move(options),
         fetchOptions](
            NotesHandler & handler, QSqlDatabase & database,
            ErrorString & errorDescription) {
            std::optional<QReadLocker> locker;
            if (fetchOptions.testFlag(FetchNoteOption::WithResourceBinaryData))
            {
                locker.emplace(handler.m_resourceDataFilesLock.get());
            }
            return handler.listNotesPerTagLocalIdImpl(
                tagLocalId, fetchOptions, options, database, errorDescription);
        });
}

QFuture<QList<qevercloud::Note>> NotesHandler::listNotesPerNotebookAndTagLocalIds(
    QStringList notebookLocalIds, QStringList tagLocalIds,
    FetchNoteOptions fetchOptions, ListOptions<ListNotesOrder> options) const
{
    return makeReadTask<QList<qevercloud::Note>>(
        makeTaskContext(), weak_from_this(),
        [notebookLocalIds = std::move(notebookLocalIds),
         tagLocalIds = std::move(tagLocalIds),
         options = std::move(options), fetchOptions](
            NotesHandler & handler, QSqlDatabase & database,
            ErrorString & errorDescription) {
            std::optional<QReadLocker> locker;
            if (fetchOptions.testFlag(FetchNoteOption::WithResourceBinaryData))
            {
                locker.emplace(handler.m_resourceDataFilesLock.get());
            }
            return handler.listNotesPerNotebookAndTagLocalIdsImpl(
                notebookLocalIds, tagLocalIds, fetchOptions, options,
                database, errorDescription);
         });
}

QFuture<QList<qevercloud::Note>> NotesHandler::listNotesByLocalIds(
    QStringList noteLocalIds, FetchNoteOptions fetchOptions,
    ListOptions<ListNotesOrder> options) const
{
    return makeReadTask<QList<qevercloud::Note>>(
        makeTaskContext(), weak_from_this(),
        [noteLocalIds = std::move(noteLocalIds), options = std::move(options),
         fetchOptions](
            NotesHandler & handler, QSqlDatabase & database,
            ErrorString & errorDescription) {
            std::optional<QReadLocker> locker;
            if (fetchOptions.testFlag(FetchNoteOption::WithResourceBinaryData))
            {
                locker.emplace(handler.m_resourceDataFilesLock.get());
            }
            return handler.listNotesByLocalIdsImpl(
                noteLocalIds, fetchOptions, options, database,
                errorDescription);
         });
}

QFuture<QList<qevercloud::Note>> NotesHandler::queryNotes(
    NoteSearchQuery query, FetchNoteOptions fetchOptions) const
{
    return makeReadTask<QList<qevercloud::Note>>(
        makeTaskContext(), weak_from_this(),
        [query = std::move(query), fetchOptions](
            NotesHandler & handler, QSqlDatabase & database,
            ErrorString & errorDescription) {
            std::optional<QReadLocker> locker;
            if (fetchOptions.testFlag(FetchNoteOption::WithResourceBinaryData))
            {
                locker.emplace(handler.m_resourceDataFilesLock.get());
            }
            return handler.queryNotesImpl(
                query, fetchOptions, database, errorDescription);
        });
}

std::optional<quint32> NotesHandler::noteCountImpl(
    NoteCountOptions options, QSqlDatabase & database,
    ErrorString & errorDescription) const
{
    const QString queryString = [&]
    {
        QString queryString = QStringLiteral("SELECT COUNT(localUid) FROM Notes");
        const QString condition = noteCountOptionsToSqlQueryPart(options);
        if (!condition.isEmpty()) {
            queryString += QStringLiteral(" WHERE ");
            queryString += condition;
        }
        return queryString;
    }();

    QSqlQuery query{database};
    const bool res = query.exec(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::NotesHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::NotesHandler",
            "Cannot count notes in the local storage database"),
        std::nullopt);

    if (!query.next()) {
        QNDEBUG(
            "local_storage::sql::NotesHandler",
            "Found no notes in the local storage database");
        return 0;
    }

    bool conversionResult = false;
    const int count = query.value(0).toInt(&conversionResult);
    if (Q_UNLIKELY(!conversionResult)) {
        errorDescription.setBase(
            QT_TRANSLATE_NOOP(
                "local_storage::sql::NotesHandler",
                "Cannot count notes in the local storage database: failed "
                "to convert note count to int"));
        QNWARNING("local_storage::sql::NotesHandler", errorDescription);
        return std::nullopt;
    }

    return count;
}

std::optional<quint32> NotesHandler::noteCountPerNotebookLocalIdImpl(
    const QString & notebookLocalId, NoteCountOptions options,
    QSqlDatabase & database, ErrorString & errorDescription) const
{
    const QString queryString = [&]
    {
        QString queryString = QStringLiteral(
            "SELECT COUNT(localUid) FROM Notes WHERE "
            "notebookLocalUid = :notebookLocalUid");
        const QString condition = noteCountOptionsToSqlQueryPart(options);
        if (!condition.isEmpty()) {
            queryString += QStringLiteral(" AND ");
            queryString += condition;
        }
        return queryString;
    }();

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::NotesHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::NotesHandler",
            "Cannot count notes per notebook local id in the local storage "
            "database: failed to prepare query"),
        std::nullopt);

    query.bindValue(QStringLiteral(":notebookLocalUid"), notebookLocalId);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::NotesHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::NotesHandler",
            "Cannot count notes per notebook local id in the local storage "
            "database"),
        std::nullopt);

    if (!query.next()) {
        QNDEBUG(
            "local_storage::sql::NotesHandler",
            "Found no notes per notebook local id in the local storage "
                << "database, notebook local id = " << notebookLocalId);
        return 0;
    }

    bool conversionResult = false;
    const int count = query.value(0).toInt(&conversionResult);
    if (Q_UNLIKELY(!conversionResult)) {
        errorDescription.setBase(
            QT_TRANSLATE_NOOP(
                "local_storage::sql::NotesHandler",
                "Cannot count notes per notebook local id in the local storage "
                "database: failed to convert note count to int"));
        QNWARNING("local_storage::sql::NotesHandler", errorDescription);
        return std::nullopt;
    }

    return count;
}

std::optional<quint32> NotesHandler::noteCountPerTagLocalIdImpl(
    const QString & tagLocalId, NoteCountOptions options,
    QSqlDatabase & database, ErrorString & errorDescription) const
{
    const QString queryString = [&]
    {
        QString queryString = QStringLiteral(
            "SELECT COUNT(localUid) FROM Notes WHERE "
            "(localUid IN (SELECT DISTINCT "
            "localNote FROM NoteTags WHERE localTag = :localTag))");
        const QString condition = noteCountOptionsToSqlQueryPart(options);
        if (!condition.isEmpty()) {
            queryString += QStringLiteral(" AND ");
            queryString += condition;
        }
        return queryString;
    }();

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::NotesHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::NotesHandler",
            "Cannot count notes per tag local id in the local storage "
            "database: failed to prepare query"),
        std::nullopt);

    query.bindValue(QStringLiteral(":localTag"), tagLocalId);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::NotesHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::NotesHandler",
            "Cannot count notes per tag local id in the local storage "
            "database"),
        std::nullopt);

    if (!query.next()) {
        QNDEBUG(
            "local_storage::sql::NotesHandler",
            "Found no notes per tag local id in the local storage "
                << "database, tag local id = " << tagLocalId);
        return 0;
    }

    bool conversionResult = false;
    const int count = query.value(0).toInt(&conversionResult);
    if (Q_UNLIKELY(!conversionResult)) {
        errorDescription.setBase(
            QT_TRANSLATE_NOOP(
                "local_storage::sql::NotesHandler",
                "Cannot count notes per tag local id in the local storage "
                "database: failed to convert note count to int"));
        QNWARNING("local_storage::sql::NotesHandler", errorDescription);
        return std::nullopt;
    }

    return count;
}

std::optional<QHash<QString, quint32>> NotesHandler::noteCountsPerTagsImpl(
    const ListOptions<ListTagsOrder> & listTagsOptions,
    NoteCountOptions options, QSqlDatabase & database,
    ErrorString & errorDescription) const
{
    Transaction transaction{database, Transaction::Type::Selection};

    const QList<qevercloud::Tag> tags = utils::listObjects<
        qevercloud::Tag, ILocalStorage::ListTagsOrder>(
        listTagsOptions.m_flags, listTagsOptions.m_limit,
        listTagsOptions.m_offset, listTagsOptions.m_order,
        listTagsOptions.m_direction, QString{}, database,
        errorDescription);

    if (tags.isEmpty()) {
        QNDEBUG(
            "local_storage::sql::NotesHandler",
            "NotesHandler::noteCountsPerTagsImpl: the list of tags is empty");
        return QHash<QString, quint32>{};
    }

    const QString queryString = [&]
    {
        QString queryString;
        QTextStream strm{&queryString};

        strm << "SELECT localTag, COUNT(localTag) AS noteCount FROM "
            << "NoteTags LEFT OUTER JOIN Notes "
            << "ON NoteTags.localNote = Notes.localUid WHERE (localTag IN ";

        int counter = 0;
        for (const qevercloud::Tag & tag: qAsConst(tags)) {
            strm << ":localTag" << counter;
            ++counter;

            if (&tag != &tags.constLast()) {
                strm << ", ";
            }
        }

        strm << ") ";

        const QString condition = noteCountOptionsToSqlQueryPart(options);
        if (!condition.isEmpty()) {
            strm << "AND ";
            strm << condition;
            strm << " ";
        }
        strm << QStringLiteral("GROUP BY localTag");
        return queryString;
    }();

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::NotesHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::NotesHandler",
            "Cannot count notes per tags in the local storage "
            "database: failed to prepare query"),
        std::nullopt);

    int counter = 0;
    for (const qevercloud::Tag & tag: qAsConst(tags)) {
        query.bindValue(
            QStringLiteral(":localTag") + QString::number(counter),
            tag.localId());
        ++counter;
    }

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::NotesHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::NotesHandler",
            "Cannot count notes per tags in the local storage database"),
        std::nullopt);

    QHash<QString, quint32> noteCountsPerTagLocalId;
    noteCountsPerTagLocalId.reserve(std::max(query.size(), 0));

    const ErrorString errorPrefix(QT_TRANSLATE_NOOP(
        "local_storage::sql::NotesHandler",
        "Can't get note counts per tags from the local storage database"));

    while (query.next()) {
        const QSqlRecord rec = query.record();

        const int tagLocalIdIndex = rec.indexOf(QStringLiteral("localTag"));
        if (Q_UNLIKELY(tagLocalIdIndex < 0)) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(
                QT_TR_NOOP("can't find local id of tag in the result of "
                           "SQL query"));
            QNWARNING("local_storage::sql::NotesHandler", errorDescription);
            return std::nullopt;
        }

        const QString tagLocalId = rec.value(tagLocalIdIndex).toString();
        if (Q_UNLIKELY(tagLocalId.isEmpty())) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(
                QT_TR_NOOP("local id of a tag from the result of SQL query "
                           "is empty"));
            QNWARNING("local_storage::sql::NotesHandler", errorDescription);
            return std::nullopt;
        }

        const int noteCountIndex = rec.indexOf(QStringLiteral("noteCount"));
        if (Q_UNLIKELY(noteCountIndex < 0)) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(
                QT_TR_NOOP("can't find note count for tag in the result of "
                           "SQL query"));
            QNWARNING("local_storage::sql::NotesHandler", errorDescription);
            return std::nullopt;
        }

        bool conversionResult = false;

        const quint32 noteCount =
            rec.value(noteCountIndex).toUInt(&conversionResult);

        if (Q_UNLIKELY(!conversionResult)) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(
                QT_TR_NOOP("failed to convert note count for tag from "
                           "the result of SQL query to unsigned int"));
            QNWARNING("local_storage::sql::NotesHandler", errorDescription);
            return std::nullopt;
        }

        noteCountsPerTagLocalId[tagLocalId] = noteCount;
    }

    return noteCountsPerTagLocalId;
}

std::optional<quint32> NotesHandler::noteCountPerNotebookAndTagLocalIdsImpl(
    const QStringList & notebookLocalIds, const QStringList & tagLocalIds,
    NoteCountOptions options, QSqlDatabase & database,
    ErrorString & errorDescription) const
{
    const QString queryString = [&]
    {
        QString queryString;
        QTextStream strm{&queryString};

        strm << "SELECT COUNT(localUid) FROM Notes";
        if (notebookLocalIds.isEmpty() && tagLocalIds.isEmpty()) {
            return queryString;
        }

        strm << " WHERE";

        if (!notebookLocalIds.isEmpty()) {
            strm << " (notebookLocalUid IN (";

            int notebookCounter = 0;
            for (const auto & notebookLocalId: qAsConst(notebookLocalIds)) {
                strm << ":notebookLocalUid" << notebookCounter;
                ++notebookCounter;
                if (&notebookLocalId != &notebookLocalIds.constLast()) {
                    strm << ", ";
                }
            }

            strm << "))";
        }

        if (!tagLocalIds.isEmpty()) {
            if (!notebookLocalIds.isEmpty()) {
                strm << " AND ";
            }

            strm << "(localUid IN (SELECT DISTINCT localNote "
                "FROM NoteTags WHERE localTag IN (";

            int tagCounter = 0;
            for (const auto & tagLocalId: tagLocalIds) {
                strm << ":tagLocalUid" << tagCounter;
                ++tagCounter;
                if (&tagLocalId != &tagLocalIds.constLast()) {
                    strm << ", ";
                }
            }

            strm << ")))";
        }

        const QString condition = noteCountOptionsToSqlQueryPart(options);
        if (!condition.isEmpty()) {
            if (!notebookLocalIds.isEmpty() || !tagLocalIds.isEmpty()) {
                strm << " AND ";
            }
            else {
                strm << " WHERE ";
            }

            strm << condition;
        }

        return queryString;
    }();

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::NotesHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::NotesHandler",
            "Cannot count notes per notebooks and tags in the local storage "
            "database: failed to prepare query"),
        std::nullopt);

    int notebookCounter = 0;
    for (const QString & notebookLocalId: qAsConst(notebookLocalIds)) {
        query.bindValue(
            QStringLiteral(":notebookLocalUid") + QString::number(notebookCounter),
            notebookLocalId);
        ++notebookCounter;
    }

    int tagCounter = 0;
    for (const QString & tagLocalId: qAsConst(tagLocalIds)) {
        query.bindValue(
            QStringLiteral(":tagLocalUid") + QString::number(tagCounter),
            tagLocalId);
        ++tagCounter;
    }

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::NotesHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::NotesHandler",
            "Cannot count notes per notebooks and tags in the local storage "
            "database"),
        std::nullopt);

    if (!query.next()) {
        QNDEBUG(
            "local_storage::sql::NotesHandler",
            "Found no notes per notebook and tag local ids in the local "
                << "storage database");
        return 0;
    }

    bool conversionResult = false;
    const int count = query.value(0).toInt(&conversionResult);
    if (Q_UNLIKELY(!conversionResult)) {
        errorDescription.setBase(
            QT_TRANSLATE_NOOP(
                "local_storage::sql::NotesHandler",
                "Cannot count notes per notebook and tag local ids in "
                "the local storage database: failed to convert note count to "
                "int"));
        QNWARNING("local_storage::sql::NotesHandler", errorDescription);
        return std::nullopt;
    }

    return count;
}

bool NotesHandler::updateNoteImpl(
    qevercloud::Note & note, UpdateNoteOptions options, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    const ErrorString errorPrefix(QT_TRANSLATE_NOOP(
        "local_storage::sql::NotesHandler", "Cannot update note"));

    Transaction transaction{database, Transaction::Type::Exclusive};

    ErrorString error;
    const QString notebookLocalId =
        utils::notebookLocalId(note, database, error);
    if (notebookLocalId.isEmpty()) {
        errorDescription.setBase(errorPrefix.base());
        if (error.isEmpty()) {
            errorDescription.appendBase(QT_TRANSLATE_NOOP(
                "local_storage::sql::NotesHandler",
                "notebook local id is empty for note"));
            errorDescription.details() = note.localId();
        }
        else {
            errorDescription.appendBase(error.base());
            errorDescription.appendBase(error.additionalBases());
            errorDescription.details() = error.details();
        }
        QNWARNING("local_storage::sql::NotesHandler", errorDescription);
        return false;
    }

    note.setNotebookLocalId(notebookLocalId);

    error.clear();
    const QString notebookGuid = utils::notebookGuid(note, database, error);
    if (notebookGuid.isEmpty() && !error.isEmpty()) {
        errorDescription.setBase(errorPrefix.base());
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING("local_storage::sql::NotesHandler", errorDescription);
        return false;
    }

    note.setNotebookGuid(
        notebookGuid.isEmpty() ? std::nullopt
                               : std::make_optional(notebookGuid));

    error.clear();
    if (!checkNote(note, error)) {
        errorDescription.setBase(errorPrefix.base());
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING("local_storage::sql::NotesHandler", errorDescription);
        return false;
    }

    const QString noteLocalId = note.localId();
    const auto & noteGuid = note.guid();

    if (note.resources()) {
        auto & resources = *note.mutableResources();
        for (auto & resource: resources) {
            resource.setNoteLocalId(noteLocalId);
            resource.setNoteGuid(noteGuid);
        }
    }

    error.clear();
    if (!utils::rowExists(
            QStringLiteral("Notes"), QStringLiteral("localUid"), noteLocalId,
            database, error))
    {
        if (!error.isEmpty()) {
            errorDescription.setBase(errorPrefix.base());
            errorDescription.appendBase(error.base());
            errorDescription.appendBase(error.additionalBases());
            errorDescription.details() = error.details();
            QNWARNING("local_storage::sql::NotesHandler", errorDescription);
            return false;
        }

        errorDescription.setBase(errorPrefix.base());
        errorDescription.appendBase(QT_TRANSLATE_NOOP(
            "local_storage::sql::NotesHandler",
            "cannot update note which doesn't exist in the local storage "
            "database"));
        QNWARNING("local_storage::sql::NotesHandler", errorDescription);
        return false;
    }

    utils::PutNoteOptions putNoteOptions;
    if (options.testFlag(UpdateNoteOption::UpdateResourceMetadata)) {
        putNoteOptions.setFlag(utils::PutNoteOption::PutResourceMetadata);
    }

    if (options.testFlag(UpdateNoteOption::UpdateResourceBinaryData)) {
        putNoteOptions.setFlag(utils::PutNoteOption::PutResourceBinaryData);
    }

    if (options.testFlag(UpdateNoteOption::UpdateTags)) {
        putNoteOptions.setFlag(utils::PutNoteOption::PutTagIds);
    }

    bool res = utils::putNote(
        m_localStorageDir, note, database, errorDescription, putNoteOptions,
        utils::TransactionOption::DontUseSeparateTransaction);
    if (!res) {
        return false;
    }

    res = transaction.commit();
    ENSURE_DB_REQUEST_RETURN(
        res, database, "local_storage::sql::NotesHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::NotesHandler",
            "Cannot update note in the local storage database: failed to "
            "commit transaction"),
        false);

    return true;
}

} // namespace quentier::local_storage::sql
