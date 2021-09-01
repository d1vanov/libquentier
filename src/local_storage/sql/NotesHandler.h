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

#include "Fwd.h"

#include <quentier/local_storage/Fwd.h>
#include <quentier/local_storage/ILocalStorage.h>

#include <qevercloud/types/Note.h>

#include <QDir>
#include <QFuture>
#include <QtGlobal>

#include <memory>
#include <optional>

namespace quentier::local_storage::sql {

class NotesHandler final :
    public std::enable_shared_from_this<NotesHandler>
{
public:
    explicit NotesHandler(
        ConnectionPoolPtr connectionPool, QThreadPool * threadPool,
        Notifier * notifier, QThreadPtr writerThread,
        const QString & localStorageDirPath);

    using NoteCountOption = ILocalStorage::NoteCountOption;
    using NoteCountOptions = ILocalStorage::NoteCountOptions;

    [[nodiscard]] QFuture<quint32> noteCount(
        NoteCountOptions options =
            NoteCountOptions(NoteCountOption::IncludeNonDeletedNotes)) const;

    [[nodiscard]] QFuture<quint32> noteCountPerNotebookLocalId(
        QString notebookLocalId,
        NoteCountOptions options =
            NoteCountOptions(NoteCountOption::IncludeNonDeletedNotes)) const;

    [[nodiscard]] QFuture<quint32> noteCountPerTagLocalId(
        QString tagLocalId,
        NoteCountOptions options =
            NoteCountOptions(NoteCountOption::IncludeNonDeletedNotes)) const;

    template <class T>
    using ListOptions = ILocalStorage::ListOptions<T>;

    using ListTagsOrder = ILocalStorage::ListTagsOrder;

    [[nodiscard]] QFuture<QHash<QString, quint32>>
        noteCountsPerTags(
            ListOptions<ListTagsOrder> listTagsOptions = {},
            NoteCountOptions options = NoteCountOptions(
                NoteCountOption::IncludeNonDeletedNotes)) const;

    [[nodiscard]] QFuture<quint32> noteCountPerNotebookAndTagLocalIds(
        QStringList notebookLocalIds, QStringList tagLocalIds,
        NoteCountOptions options = NoteCountOptions(
            NoteCountOption::IncludeNonDeletedNotes)) const;

    [[nodiscard]] QFuture<void> putNote(qevercloud::Note note);

    using UpdateNoteOption = ILocalStorage::UpdateNoteOption;
    using UpdateNoteOptions = ILocalStorage::UpdateNoteOptions;

    [[nodiscard]] QFuture<void> updateNote(
        qevercloud::Note note, UpdateNoteOptions options);

    [[nodiscard]] QFuture<qevercloud::Note> findNoteByLocalId(
        QString localId) const;

    [[nodiscard]] QFuture<qevercloud::Note> findNoteByGuid(
        qevercloud::Guid guid) const;

    [[nodiscard]] QFuture<void> expungeNoteByLocalId(QString localId);
    [[nodiscard]] QFuture<void> expungeNoteByGuid(qevercloud::Guid guid);

    using ListNotesOrder = ILocalStorage::ListNotesOrder;

    using FetchNoteOption = ILocalStorage::FetchNoteOption;
    using FetchNoteOptions = ILocalStorage::FetchNoteOptions;

    [[nodiscard]] QFuture<QList<qevercloud::Note>> listNotes(
        FetchNoteOptions fetchOptions,
        ListOptions<ListNotesOrder> options) const;

    [[nodiscard]] QFuture<QList<qevercloud::SharedNote>>
        listSharedNotes(qevercloud::Guid noteGuid = {}) const;

    [[nodiscard]] QFuture<QList<qevercloud::Note>>
        listNotesPerNotebookLocalId(
            QString notebookLocalId, FetchNoteOptions fetchOptions,
            ListOptions<ListNotesOrder> listOptions = {}) const;

    [[nodiscard]] QFuture<QList<qevercloud::Note>>
        listNotesPerTagLocalId(
            QString tagLocalId, FetchNoteOptions fetchOptions,
            ListOptions<ListNotesOrder> listOptions = {}) const;

    [[nodiscard]] QFuture<QList<qevercloud::Note>>
        listNotesPerNotebookAndTagLocalIds(
            QStringList notebookLocalIds, QStringList tagLocalIds,
            FetchNoteOptions fetchOptions,
            ListOptions<ListNotesOrder> listOptions = {}) const;

    [[nodiscard]] QFuture<QList<qevercloud::Note>>
        listNotesByLocalIds(
            QStringList noteLocalIds, FetchNoteOptions fetchOptions,
            ListOptions<ListNotesOrder> listOptions = {}) const;

    [[nodiscard]] QFuture<QList<qevercloud::Note>> queryNotes(
        NoteSearchQuery query, FetchNoteOptions fetchOptions) const;

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
        const ListOptions<ListTagsOrder> & listTagsOptions,
        NoteCountOptions options, QSqlDatabase & database,
        ErrorString & errorDescription) const;

    [[nodiscard]] std::optional<quint32> noteCountPerNotebookAndTagLocalIdsImpl(
        const QStringList & notebookLocalIds, const QStringList & tagLocalIds,
        NoteCountOptions options, QSqlDatabase & database,
        ErrorString & errorDescription) const;

    [[nodiscard]] std::optional<qevercloud::Note> findNoteByLocalIdImpl(
        const QString & localId, QSqlDatabase & database,
        ErrorString & errorDescription) const;

    [[nodiscard]] std::optional<qevercloud::Note> findNoteByGuidImpl(
        const qevercloud::Guid & guid, QSqlDatabase & database,
        ErrorString & errorDescription) const;

    [[nodiscard]] std::optional<qevercloud::Note> fillSharedNotes(
        qevercloud::Note & note, QSqlDatabase & database,
        ErrorString & errorDescription) const;

    [[nodiscard]] bool expungeNoteByLocalIdImpl(
        const QString & localId, QSqlDatabase & database,
        ErrorString & errorDescription);

    [[nodiscard]] bool expungeNoteByGuidImpl(
        const qevercloud::Guid & guid, QSqlDatabase & database,
        ErrorString & errorDescription);

    [[nodiscard]] QStringList listNoteLocalIdsByNoteLocalId(
        const QString & noteLocalId, QSqlDatabase & database,
        ErrorString & errorDescription) const;

    [[nodiscard]] QList<qevercloud::Note> listNotesImpl(
        const ListOptions<ListNotesOrder> & options,
        QSqlDatabase & database, ErrorString & errorDescription) const;

    [[nodiscard]] QList<qevercloud::SharedNote> listSharedNotesImpl(
        const qevercloud::Guid & noteGuid, QSqlDatabase & database,
        ErrorString & errorDescription) const;

    [[nodiscard]] QList<qevercloud::Note> listNotesPerNotebookLocalIdImpl(
        const QString & notebookLocalId, FetchNoteOptions fetchOptions,
        ListOptions<ListNotesOrder> listOptions, QSqlDatabase & database,
        ErrorString & errorDescription) const;

    [[nodiscard]] QList<qevercloud::Note> listNotesPerTagLocalIdImpl(
        const QString & tagLocalId, FetchNoteOptions fetchOptions,
        ListOptions<ListNotesOrder> listOptions, QSqlDatabase & database,
        ErrorString & errorDescription) const;

    [[nodiscard]] QList<qevercloud::Note>
        listNotesPerNotebookAndTagLocalIdsImpl(
            const QStringList & notebookLocalIds,
            const QStringList & tagLocalIds, FetchNoteOptions fetchOptions,
            ListOptions<ListNotesOrder> listOptions, QSqlDatabase & database,
            ErrorString & errorDescription) const;

    [[nodiscard]] QList<qevercloud::Note> listNotesByLocalIdsImpl(
        const QStringList & noteLocalIds, FetchNoteOptions fetchOptions,
        ListOptions<ListNotesOrder> listOptions, QSqlDatabase & database,
        ErrorString & errorDescription) const;

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
};

} // namespace quentier::local_storage::sql
