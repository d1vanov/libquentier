/*
 * Copyright 2021-2022 Dmitry Ivanov
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

#include <quentier/local_storage/ILocalStorage.h>

#include <gmock/gmock.h>

namespace quentier::local_storage::tests::mocks {

class MockILocalStorage : public ILocalStorage
{
public:
    MOCK_METHOD(QFuture<bool>, isVersionTooHigh, (), (const, override));
    MOCK_METHOD(QFuture<bool>, requiresUpgrade, (), (const, override));

    MOCK_METHOD(
        QFuture<QList<IPatchPtr>>, requiredPatches, (), (const, override));

    MOCK_METHOD(QFuture<qint32>, version, (), (const, override));

    MOCK_METHOD(
        QFuture<qint32>, highestSupportedVersion, (), (const, override));

    MOCK_METHOD(QFuture<quint32>, userCount, (), (const, override));
    MOCK_METHOD(QFuture<void>, putUser, (qevercloud::User user), (override));

    MOCK_METHOD(
        QFuture<std::optional<qevercloud::User>>, findUserById,
        (qevercloud::UserID userId), (const, override));

    MOCK_METHOD(
        QFuture<void>, expungeUserById, (qevercloud::UserID userId),
        (override));

    MOCK_METHOD(QFuture<quint32>, notebookCount, (), (const, override));

    MOCK_METHOD(
        QFuture<void>, putNotebook, (qevercloud::Notebook notebook),
        (override));

    MOCK_METHOD(
        QFuture<std::optional<qevercloud::Notebook>>, findNotebookByLocalId,
        (QString localId), (const, override));

    MOCK_METHOD(
        QFuture<std::optional<qevercloud::Notebook>>, findNotebookByGuid,
        (qevercloud::Guid guid), (const, override));

    MOCK_METHOD(
        QFuture<std::optional<qevercloud::Notebook>>, findNotebookByName,
        (QString name, std::optional<qevercloud::Guid> linkedNotebookGuid),
        (const, override));

    MOCK_METHOD(
        QFuture<std::optional<qevercloud::Notebook>>, findDefaultNotebook, (),
        (const, override));

    MOCK_METHOD(
        QFuture<void>, expungeNotebookByLocalId, (QString localId), (override));

    MOCK_METHOD(
        QFuture<void>, expungeNotebookByGuid, (qevercloud::Guid guid),
        (override));

    MOCK_METHOD(
        QFuture<void>, expungeNotebookByName,
        (QString name, std::optional<qevercloud::Guid> linkedNotebookGuid),
        (override));

    MOCK_METHOD(
        QFuture<QList<qevercloud::Notebook>>, listNotebooks,
        (ListNotebooksOptions options), (const, override));

    MOCK_METHOD(
        QFuture<QList<qevercloud::SharedNotebook>>, listSharedNotebooks,
        (qevercloud::Guid notebookGuid), (const, override));

    MOCK_METHOD(QFuture<quint32>, linkedNotebookCount, (), (const, override));

    MOCK_METHOD(
        QFuture<void>, putLinkedNotebook,
        (qevercloud::LinkedNotebook linkedNotebook), (override));

    MOCK_METHOD(
        QFuture<std::optional<qevercloud::LinkedNotebook>>,
        findLinkedNotebookByGuid, (qevercloud::Guid guid), (const, override));

    MOCK_METHOD(
        QFuture<void>, expungeLinkedNotebookByGuid, (qevercloud::Guid guid),
        (override));

    MOCK_METHOD(
        QFuture<QList<qevercloud::LinkedNotebook>>, listLinkedNotebooks,
        (ListLinkedNotebooksOptions options), (const, override));

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
        QFuture<void>, expungeNoteByLocalId, (QString localId), (override));

    MOCK_METHOD(
        QFuture<void>, expungeNoteByGuid, (qevercloud::Guid guid), (override));

    MOCK_METHOD(
        QFuture<QList<qevercloud::Note>>, listNotes,
        (FetchNoteOptions fetchOptions, ListNotesOptions options),
        (const, override));

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

    MOCK_METHOD(QFuture<quint32>, tagCount, (), (const, override));
    MOCK_METHOD(QFuture<void>, putTag, (qevercloud::Tag tag), (override));

    MOCK_METHOD(
        QFuture<std::optional<qevercloud::Tag>>, findTagByLocalId,
        (QString tagLocalId), (const, override));

    MOCK_METHOD(
        QFuture<std::optional<qevercloud::Tag>>, findTagByGuid,
        (qevercloud::Guid tagGuid), (const, override));

    MOCK_METHOD(
        QFuture<std::optional<qevercloud::Tag>>, findTagByName,
        (QString tagName, std::optional<QString> linkedNotebookGuid),
        (const, override));

    MOCK_METHOD(
        QFuture<QList<qevercloud::Tag>>, listTags, (ListTagsOptions options),
        (const, override));

    MOCK_METHOD(
        QFuture<QList<qevercloud::Tag>>, listTagsPerNoteLocalId,
        (QString noteLocalId, ListTagsOptions options), (const, override));

    MOCK_METHOD(
        QFuture<void>, expungeTagByLocalId, (QString tagLocalId), (override));

    MOCK_METHOD(
        QFuture<void>, expungeTagByGuid, (qevercloud::Guid tagGuid),
        (override));

    MOCK_METHOD(
        QFuture<void>, expungeTagByName,
        (QString name, std::optional<qevercloud::Guid> linkedNotebookGuid),
        (override));

    MOCK_METHOD(
        QFuture<quint32>, resourceCount, (NoteCountOptions options),
        (const, override));

    MOCK_METHOD(
        QFuture<quint32>, resourceCountPerNoteLocalId, (QString noteLocalId),
        (const, override));

    MOCK_METHOD(
        QFuture<void>, putResource,
        (qevercloud::Resource resource), (override));

    MOCK_METHOD(
        QFuture<std::optional<qevercloud::Resource>>, findResourceByLocalId,
        (QString resourceLocalId, FetchResourceOptions options),
        (const, override));

    MOCK_METHOD(
        QFuture<std::optional<qevercloud::Resource>>, findResourceByGuid,
        (qevercloud::Guid resourceGuid, FetchResourceOptions options),
        (const, override));

    MOCK_METHOD(
        QFuture<void>, expungeResourceByLocalId, (QString resourceLocalId),
        (override));

    MOCK_METHOD(
        QFuture<void>, expungeResourceByGuid, (qevercloud::Guid resourceGuid),
        (override));

    MOCK_METHOD(QFuture<quint32>, savedSearchCount, (), (const, override));

    MOCK_METHOD(
        QFuture<void>, putSavedSearch, (qevercloud::SavedSearch search),
        (override));

    MOCK_METHOD(
        QFuture<std::optional<qevercloud::SavedSearch>>,
        findSavedSearchByLocalId, (QString localId), (const, override));

    MOCK_METHOD(
        QFuture<std::optional<qevercloud::SavedSearch>>, findSavedSearchByGuid,
        (qevercloud::Guid guid), (const, override));

    MOCK_METHOD(
        QFuture<std::optional<qevercloud::SavedSearch>>, findSavedSearchByName,
        (QString name), (const, override));

    MOCK_METHOD(
        QFuture<QList<qevercloud::SavedSearch>>, listSavedSearches,
        (ListSavedSearchesOptions options), (const, override));

    MOCK_METHOD(
        QFuture<void>, expungeSavedSearchByLocalId, (QString localId),
        (override));

    MOCK_METHOD(
        QFuture<void>, expungeSavedSearchByGuid, (qevercloud::Guid guid),
        (override));

    MOCK_METHOD(
        QFuture<qint32>, highestUpdateSequenceNumber, (HighestUsnOption option),
        (const, override));

    MOCK_METHOD(
        QFuture<qint32>, highestUpdateSequenceNumber,
        (qevercloud::Guid linkedNotebookGuid), (const, override));

    MOCK_METHOD(ILocalStorageNotifier *, notifier, (), (const, override));
};

} // namespace quentier::local_storage::tests::mocks
