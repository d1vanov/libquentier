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

#include <local_storage/sql/INotesHandler.h>

#include <gmock/gmock.h>

namespace quentier::local_storage::sql::tests::mocks {

class MockINotesHandler : public INotesHandler
{
public:
    MOCK_METHOD(
        QFuture<quint32>, noteCount, (NoteCountOptions options),
        (const, override));

    MOCK_METHOD(
        QFuture<quint32>, noteCountPerNotebookLocalId,
        (QString notebookLocalId, NoteCountOptions options), (const, override));

    MOCK_METHOD(
        QFuture<quint32>, noteCountPerTagLocalId,
        (QString tagLocalId, NoteCountOptions options), (const, override));

    MOCK_METHOD(
        (QFuture<QHash<QString, quint32>>), noteCountsPerTags,
        (ListTagsOptions listTagsOptions, NoteCountOptions options),
        (const, override));

    MOCK_METHOD(
        QFuture<quint32>, noteCountPerNotebookAndTagLocalIds,
        (QStringList notebookLocalIds, QStringList tagLocalIds,
         NoteCountOptions options),
        (const, override));

    MOCK_METHOD(QFuture<void>, putNote, (qevercloud::Note note), (override));

    MOCK_METHOD(
        QFuture<void>, updateNote,
        (qevercloud::Note note, UpdateNoteOptions options), (override));

    MOCK_METHOD(
        QFuture<std::optional<qevercloud::Note>>, findNoteByLocalId,
        (QString localId, FetchNoteOptions options), (const, override));

    MOCK_METHOD(
        QFuture<std::optional<qevercloud::Note>>, findNoteByGuid,
        (qevercloud::Guid guid, FetchNoteOptions options), (const, override));

    MOCK_METHOD(
        QFuture<void>, expungeNoteByLocalId,
        (QString localId), (override));

    MOCK_METHOD(
        QFuture<void>, expungeNoteByGuid, (qevercloud::Guid guid), (override));

    MOCK_METHOD(
        QFuture<QList<qevercloud::Note>>, listNotes,
        (FetchNoteOptions fetchOptions, ListNotesOptions options),
        (const, override));

    MOCK_METHOD(
        QFuture<QList<qevercloud::SharedNote>>, listSharedNotes,
        (qevercloud::Guid noteGuid), (const, override));

    MOCK_METHOD(
        QFuture<QList<qevercloud::Note>>, listNotesPerNotebookLocalId,
        (QString notebookLocalId, FetchNoteOptions fetchOptions,
         ListNotesOptions options),
        (const, override));

    MOCK_METHOD(
        QFuture<QList<qevercloud::Note>>, listNotesPerTagLocalId,
        (QString tagLocalId, FetchNoteOptions fetchOptions,
         ListNotesOptions options),
        (const, override));

    MOCK_METHOD(
        QFuture<QList<qevercloud::Note>>, listNotesPerNotebookAndTagLocalIds,
        (QStringList notebookLocalIds, QStringList tagLocalIds,
         FetchNoteOptions fetchOptions, ListNotesOptions options),
        (const, override));

    MOCK_METHOD(
        QFuture<QList<qevercloud::Note>>, listNotesByLocalIds,
        (QStringList noteLocalIds, FetchNoteOptions fetchOptions,
         ListNotesOptions options),
        (const, override));

    MOCK_METHOD(
        QFuture<QList<qevercloud::Note>>, queryNotes,
        (NoteSearchQuery query, FetchNoteOptions fetchOptions),
        (const, override));

    MOCK_METHOD(
        QFuture<QStringList>, queryNoteLocalIds, (NoteSearchQuery query),
        (const, override));
};

} // namespace quentier::local_storage::sql::tests::mocks
