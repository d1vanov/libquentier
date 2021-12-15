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

#pragma once

#include "INotesHandler.h"
#include "Transaction.h"

#include <QDir>
#include <QReadWriteLock>
#include <QtGlobal>

#include <memory>
#include <optional>

class QSqlQuery;

namespace quentier::local_storage::sql {

class NotesHandler final :
    public INotesHandler,
    public std::enable_shared_from_this<NotesHandler>
{
public:
    explicit NotesHandler(
        ConnectionPoolPtr connectionPool, QThreadPool * threadPool,
        Notifier * notifier, QThreadPtr writerThread,
        const QString & localStorageDirPath,
        QReadWriteLockPtr resourceDataFilesLock);

    [[nodiscard]] QFuture<quint32> noteCount(
        NoteCountOptions options = NoteCountOptions(
            NoteCountOption::IncludeNonDeletedNotes)) const override;

    [[nodiscard]] QFuture<quint32> noteCountPerNotebookLocalId(
        QString notebookLocalId,
        NoteCountOptions options = NoteCountOptions(
            NoteCountOption::IncludeNonDeletedNotes)) const override;

    [[nodiscard]] QFuture<quint32> noteCountPerTagLocalId(
        QString tagLocalId,
        NoteCountOptions options = NoteCountOptions(
            NoteCountOption::IncludeNonDeletedNotes)) const override;

    [[nodiscard]] QFuture<QHash<QString, quint32>> noteCountsPerTags(
        ListTagsOptions listTagsOptions = {},
        NoteCountOptions options = NoteCountOptions(
            NoteCountOption::IncludeNonDeletedNotes)) const override;

    [[nodiscard]] QFuture<quint32> noteCountPerNotebookAndTagLocalIds(
        QStringList notebookLocalIds, QStringList tagLocalIds,
        NoteCountOptions options = NoteCountOptions(
            NoteCountOption::IncludeNonDeletedNotes)) const override;

    [[nodiscard]] QFuture<void> putNote(qevercloud::Note note) override;

    [[nodiscard]] QFuture<void> updateNote(
        qevercloud::Note note, UpdateNoteOptions options) override;

    [[nodiscard]] QFuture<qevercloud::Note> findNoteByLocalId(
        QString localId, FetchNoteOptions options) const override;

    [[nodiscard]] QFuture<qevercloud::Note> findNoteByGuid(
        qevercloud::Guid guid, FetchNoteOptions options) const override;

    [[nodiscard]] QFuture<void> expungeNoteByLocalId(QString localId) override;
    [[nodiscard]] QFuture<void> expungeNoteByGuid(
        qevercloud::Guid guid) override;

    [[nodiscard]] QFuture<QList<qevercloud::Note>> listNotes(
        FetchNoteOptions fetchOptions,
        ListNotesOptions options) const override;

    [[nodiscard]] QFuture<QList<qevercloud::SharedNote>> listSharedNotes(
        qevercloud::Guid noteGuid = {}) const override;

    [[nodiscard]] QFuture<QList<qevercloud::Note>> listNotesPerNotebookLocalId(
        QString notebookLocalId, FetchNoteOptions fetchOptions,
        ListNotesOptions options = {}) const override;

    [[nodiscard]] QFuture<QList<qevercloud::Note>> listNotesPerTagLocalId(
        QString tagLocalId, FetchNoteOptions fetchOptions,
        ListNotesOptions options = {}) const override;

    [[nodiscard]] QFuture<QList<qevercloud::Note>>
        listNotesPerNotebookAndTagLocalIds(
            QStringList notebookLocalIds, QStringList tagLocalIds,
            FetchNoteOptions fetchOptions,
            ListNotesOptions options = {}) const override;

    [[nodiscard]] QFuture<QList<qevercloud::Note>> listNotesByLocalIds(
        QStringList noteLocalIds, FetchNoteOptions fetchOptions,
        ListNotesOptions options = {}) const override;

    [[nodiscard]] QFuture<QList<qevercloud::Note>> queryNotes(
        NoteSearchQuery query, FetchNoteOptions fetchOptions) const override;

    [[nodiscard]] QFuture<QStringList> queryNoteLocalIds(
        NoteSearchQuery query) const override;

private:
    [[nodiscard]] std::optional<quint32> noteCountImpl(
        NoteCountOptions options, QSqlDatabase & database,
        ErrorString & errorDescription) const;

    [[nodiscard]] std::optional<quint32> noteCountPerNotebookLocalIdImpl(
        const QString & notebookLocalId, NoteCountOptions options,
        QSqlDatabase & database, ErrorString & errorDescription) const;

    [[nodiscard]] std::optional<quint32> noteCountPerTagLocalIdImpl(
        const QString & tagLocalId, NoteCountOptions options,
        QSqlDatabase & database, ErrorString & errorDescription) const;

    [[nodiscard]] std::optional<QHash<QString, quint32>> noteCountsPerTagsImpl(
        const ListTagsOptions & listTagsOptions,
        NoteCountOptions options, QSqlDatabase & database,
        ErrorString & errorDescription) const;

    [[nodiscard]] std::optional<quint32> noteCountPerNotebookAndTagLocalIdsImpl(
        const QStringList & notebookLocalIds, const QStringList & tagLocalIds,
        NoteCountOptions options, QSqlDatabase & database,
        ErrorString & errorDescription) const;

    [[nodiscard]] bool updateNoteImpl(
        qevercloud::Note & note, UpdateNoteOptions options,
        QSqlDatabase & database, ErrorString & errorDescription);

    [[nodiscard]] std::optional<qevercloud::Note> findNoteByLocalIdImpl(
        const QString & localId, FetchNoteOptions options,
        QSqlDatabase & database, ErrorString & errorDescription) const;

    [[nodiscard]] std::optional<qevercloud::Note> findNoteByGuidImpl(
        const qevercloud::Guid & guid, FetchNoteOptions options,
        QSqlDatabase & database, ErrorString & errorDescription) const;

    [[nodiscard]] std::optional<qevercloud::Note> fillNoteData(
        FetchNoteOptions options, QSqlQuery & query, QSqlDatabase & database,
        ErrorString & errorDescription) const;

    [[nodiscard]] bool fillSharedNotes(
        qevercloud::Note & note, QSqlDatabase & database,
        ErrorString & errorDescription) const;

    [[nodiscard]] bool fillTagIds(
        qevercloud::Note & note, QSqlDatabase & database,
        ErrorString & errorDescription) const;

    [[nodiscard]] bool fillResources(
        FetchNoteOptions fetchOptions, const ErrorString & errorPrefix,
        qevercloud::Note & note, QSqlDatabase & database,
        ErrorString & errorDescription) const;

    [[nodiscard]] QStringList listResourceLocalIdsPerNoteLocalId(
        const QString & noteLocalId, QSqlDatabase & database,
        ErrorString & errorDescription) const;

    [[nodiscard]] bool expungeNoteByLocalIdImpl(
        const QString & localId, QSqlDatabase & database,
        ErrorString & errorDescription,
        std::optional<Transaction> transaction = std::nullopt);

    [[nodiscard]] bool expungeNoteByGuidImpl(
        const qevercloud::Guid & guid, QSqlDatabase & database,
        ErrorString & errorDescription);

    [[nodiscard]] QList<qevercloud::Note> listNotesImpl(
        FetchNoteOptions fetchOptions,
        const ListNotesOptions & options, QSqlDatabase & database,
        ErrorString & errorDescription, const QString & sqlQueryCondition = {},
        std::optional<Transaction> transaction = std::nullopt) const;

    [[nodiscard]] QList<qevercloud::SharedNote> listSharedNotesImpl(
        const qevercloud::Guid & noteGuid, QSqlDatabase & database,
        ErrorString & errorDescription) const;

    [[nodiscard]] QList<qevercloud::Note> listNotesPerNotebookLocalIdImpl(
        const QString & notebookLocalId, FetchNoteOptions fetchOptions,
        const ListNotesOptions & options, QSqlDatabase & database,
        ErrorString & errorDescription) const;

    [[nodiscard]] QList<qevercloud::Note> listNotesPerTagLocalIdImpl(
        const QString & tagLocalId, FetchNoteOptions fetchOptions,
        const ListNotesOptions & options, QSqlDatabase & database,
        ErrorString & errorDescription) const;

    [[nodiscard]] QList<qevercloud::Note>
        listNotesPerNotebookAndTagLocalIdsImpl(
            const QStringList & notebookLocalIds,
            const QStringList & tagLocalIds, FetchNoteOptions fetchOptions,
            const ListNotesOptions & options,
            QSqlDatabase & database, ErrorString & errorDescription) const;

    [[nodiscard]] QList<qevercloud::Note> listNotesByLocalIdsImpl(
        const QStringList & noteLocalIds, FetchNoteOptions fetchOptions,
        const ListNotesOptions & options, QSqlDatabase & database,
        ErrorString & errorDescription,
        std::optional<Transaction> transaction = std::nullopt) const;

    [[nodiscard]] QList<qevercloud::Note> queryNotesImpl(
        const NoteSearchQuery & query, FetchNoteOptions fetchOptions,
        QSqlDatabase & database, ErrorString & errorDescription) const;

    [[nodiscard]] TaskContext makeTaskContext() const;

private:
    ConnectionPoolPtr m_connectionPool;
    QThreadPool * m_threadPool;
    Notifier * m_notifier;
    QThreadPtr m_writerThread;
    QDir m_localStorageDir;
    QReadWriteLockPtr m_resourceDataFilesLock;
};

} // namespace quentier::local_storage::sql
