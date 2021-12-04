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

#include <QFuture>
#include <QList>

namespace quentier::local_storage::sql {

class INotesHandler
{
public:
    virtual ~INotesHandler() = default;

    using NoteCountOption = ILocalStorage::NoteCountOption;
    using NoteCountOptions = ILocalStorage::NoteCountOptions;

    [[nodiscard]] virtual QFuture<quint32> noteCount(
        NoteCountOptions options = NoteCountOptions(
            NoteCountOption::IncludeNonDeletedNotes)) const = 0;

    [[nodiscard]] virtual QFuture<quint32> noteCountPerNotebookLocalId(
        QString notebookLocalId,
        NoteCountOptions options = NoteCountOptions(
            NoteCountOption::IncludeNonDeletedNotes)) const = 0;

    [[nodiscard]] virtual QFuture<quint32> noteCountPerTagLocalId(
        QString tagLocalId,
        NoteCountOptions options = NoteCountOptions(
            NoteCountOption::IncludeNonDeletedNotes)) const = 0;

    template <class T>
    using ListOptions = ILocalStorage::ListOptions<T>;

    using ListTagsOrder = ILocalStorage::ListTagsOrder;

    [[nodiscard]] virtual QFuture<QHash<QString, quint32>> noteCountsPerTags(
        ListOptions<ListTagsOrder> listTagsOptions = {},
        NoteCountOptions options = NoteCountOptions(
            NoteCountOption::IncludeNonDeletedNotes)) const = 0;

    [[nodiscard]] virtual QFuture<quint32> noteCountPerNotebookAndTagLocalIds(
        QStringList notebookLocalIds, QStringList tagLocalIds,
        NoteCountOptions options = NoteCountOptions(
            NoteCountOption::IncludeNonDeletedNotes)) const = 0;

    [[nodiscard]] virtual QFuture<void> putNote(qevercloud::Note note) = 0;

    using UpdateNoteOption = ILocalStorage::UpdateNoteOption;
    using UpdateNoteOptions = ILocalStorage::UpdateNoteOptions;

    [[nodiscard]] virtual QFuture<void> updateNote(
        qevercloud::Note note, UpdateNoteOptions options) = 0;

    using FetchNoteOption = ILocalStorage::FetchNoteOption;
    using FetchNoteOptions = ILocalStorage::FetchNoteOptions;

    [[nodiscard]] virtual QFuture<qevercloud::Note> findNoteByLocalId(
        QString localId, FetchNoteOptions options) const = 0;

    [[nodiscard]] virtual QFuture<qevercloud::Note> findNoteByGuid(
        qevercloud::Guid guid, FetchNoteOptions options) const = 0;

    [[nodiscard]] virtual QFuture<void> expungeNoteByLocalId(
        QString localId) = 0;

    [[nodiscard]] virtual QFuture<void> expungeNoteByGuid(
        qevercloud::Guid guid) = 0;

    using ListNotesOrder = ILocalStorage::ListNotesOrder;

    [[nodiscard]] virtual QFuture<QList<qevercloud::Note>> listNotes(
        FetchNoteOptions fetchOptions,
        ListOptions<ListNotesOrder> options) const = 0;

    [[nodiscard]] virtual QFuture<QList<qevercloud::SharedNote>>
        listSharedNotes(qevercloud::Guid noteGuid = {}) const = 0;

    [[nodiscard]] virtual QFuture<QList<qevercloud::Note>>
        listNotesPerNotebookLocalId(
            QString notebookLocalId, FetchNoteOptions fetchOptions,
            ListOptions<ListNotesOrder> options = {}) const = 0;

    [[nodiscard]] virtual QFuture<QList<qevercloud::Note>>
        listNotesPerTagLocalId(
            QString tagLocalId, FetchNoteOptions fetchOptions,
            ListOptions<ListNotesOrder> options = {}) const = 0;

    [[nodiscard]] virtual QFuture<QList<qevercloud::Note>>
        listNotesPerNotebookAndTagLocalIds(
            QStringList notebookLocalIds, QStringList tagLocalIds,
            FetchNoteOptions fetchOptions,
            ListOptions<ListNotesOrder> options = {}) const = 0;

    [[nodiscard]] virtual QFuture<QList<qevercloud::Note>> listNotesByLocalIds(
        QStringList noteLocalIds, FetchNoteOptions fetchOptions,
        ListOptions<ListNotesOrder> options = {}) const = 0;

    [[nodiscard]] virtual QFuture<QList<qevercloud::Note>> queryNotes(
        NoteSearchQuery query, FetchNoteOptions fetchOptions) const = 0;

    [[nodiscard]] virtual QFuture<QStringList> queryNoteLocalIds(
        NoteSearchQuery query) const = 0;
};

} // namespace quentier::local_storage::sql
