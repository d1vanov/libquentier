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
#include "TypeChecks.h"

#include "utils/Common.h"
#include "utils/FillFromSqlRecordUtils.h"
#include "utils/ListFromDatabaseUtils.h"
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

#include <QSqlRecord>
#include <QSqlQuery>
#include <QThreadPool>
#include <QWriteLocker>

#include <algorithm>

namespace quentier::local_storage::sql {

NotesHandler::NotesHandler(
    ConnectionPoolPtr connectionPool, QThreadPool * threadPool,
    Notifier * notifier, QThreadPtr writerThread,
    const QString & localStorageDirPath) :
    m_connectionPool{std::move(connectionPool)},
    m_threadPool{threadPool},
    m_notifier{notifier},
    m_writerThread{std::move(writerThread)},
    m_localStorageDir{localStorageDirPath}
{
    if (Q_UNLIKELY(!m_connectionPool)) {
        throw InvalidArgument{ErrorString{
            QT_TRANSLATE_NOOP(
                "local_storage::sql::NotesHandler",
                "NotesHandler ctor: connection pool is null")}};
    }

    if (Q_UNLIKELY(!m_threadPool)) {
        throw InvalidArgument{ErrorString{
            QT_TRANSLATE_NOOP(
                "local_storage::sql::NotesHandler",
                "NotesHandler ctor: thread pool is null")}};
    }

    if (Q_UNLIKELY(!m_notifier)) {
        throw InvalidArgument{ErrorString{
            QT_TRANSLATE_NOOP(
                "local_storage::sql::NotesHandler",
                "NotesHandler ctor: notifier is null")}};
    }

    if (Q_UNLIKELY(!m_writerThread)) {
        throw InvalidArgument{ErrorString{
            QT_TRANSLATE_NOOP(
                "local_storage::sql::NotesHandler",
                "NotesHandler ctor: writer thread is null")}};
    }

    if (Q_UNLIKELY(!m_localStorageDir.isReadable())) {
        throw InvalidArgument{ErrorString{
            QT_TRANSLATE_NOOP(
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
        [notebookLocalIds = std::move(notebookLocalIds), tagLocalIds = std::move(tagLocalIds), options](
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
            QWriteLocker locker{&handler.m_resourceDataFilesLock};
            bool res = utils::putNote(
                handler.m_localStorageDir, note, database, errorDescription);
            if (res) {
                handler.m_notifier->notifyNotePut(note);
            }
            return res;
        });
}

} // namespace quentier::local_storage::sql
