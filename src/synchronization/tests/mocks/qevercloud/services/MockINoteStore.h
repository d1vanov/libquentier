/*
 * Copyright 2022 Dmitry Ivanov
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

#include <qevercloud/services/INoteStore.h>

#include <gmock/gmock.h>

namespace quentier::synchronization::tests::mocks::qevercloud {

class MockINoteStore : public ::qevercloud::INoteStore
{
    Q_OBJECT
public:
    MOCK_METHOD(QString, noteStoreUrl, (), (const, override));
    MOCK_METHOD(void, setNoteStoreUrl, (QString url), (override));

    MOCK_METHOD(
        const std::optional<::qevercloud::Guid> &, linkedNotebookGuid, (),
        (const, override));

    MOCK_METHOD(
        void, setLinkedNotebookGuid,
        (std::optional<::qevercloud::Guid> linkedNotebookGuid), (override));

    MOCK_METHOD(
        ::qevercloud::SyncState, getSyncState,
        (::qevercloud::IRequestContextPtr ctx), (override));

    MOCK_METHOD(
        QFuture<::qevercloud::SyncState>, getSyncStateAsync,
        (::qevercloud::IRequestContextPtr ctx), (override));

    MOCK_METHOD(
        ::qevercloud::SyncChunk, getFilteredSyncChunk,
        (qint32 afterUSN, qint32 maxEntries,
         const ::qevercloud::SyncChunkFilter & filter,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<::qevercloud::SyncChunk>, getFilteredSyncChunkAsync,
        (qint32 afterUSN, qint32 maxEntries,
         const ::qevercloud::SyncChunkFilter & filter,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        ::qevercloud::SyncState, getLinkedNotebookSyncState,
        (const ::qevercloud::LinkedNotebook & linkedNotebook,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<::qevercloud::SyncState>, getLinkedNotebookSyncStateAsync,
        (const ::qevercloud::LinkedNotebook & linkedNotebook,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        ::qevercloud::SyncChunk, getLinkedNotebookSyncChunk,
        (const ::qevercloud::LinkedNotebook & linkedNotebook, qint32 afterUSN,
         qint32 maxEntries, bool fullSyncOnly,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<::qevercloud::SyncChunk>, getLinkedNotebookSyncChunkAsync,
        (const ::qevercloud::LinkedNotebook & linkedNotebook, qint32 afterUSN,
         qint32 maxEntries, bool fullSyncOnly,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QList<::qevercloud::Notebook>, listNotebooks,
        (::qevercloud::IRequestContextPtr ctx), (override));

    MOCK_METHOD(
        QFuture<QList<::qevercloud::Notebook>>, listNotebooksAsync,
        (::qevercloud::IRequestContextPtr ctx), (override));

    MOCK_METHOD(
        QList<::qevercloud::Notebook>, listAccessibleBusinessNotebooks,
        (::qevercloud::IRequestContextPtr ctx), (override));

    MOCK_METHOD(
        QFuture<QList<::qevercloud::Notebook>>,
        listAccessibleBusinessNotebooksAsync,
        (::qevercloud::IRequestContextPtr ctx), (override));

    MOCK_METHOD(
        ::qevercloud::Notebook, getNotebook,
        (::qevercloud::Guid guid, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<::qevercloud::Notebook>, getNotebookAsync,
        (::qevercloud::Guid guid, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        ::qevercloud::Notebook, getDefaultNotebook,
        (::qevercloud::IRequestContextPtr ctx), (override));

    MOCK_METHOD(
        QFuture<::qevercloud::Notebook>, getDefaultNotebookAsync,
        (::qevercloud::IRequestContextPtr ctx), (override));

    MOCK_METHOD(
        ::qevercloud::Notebook, createNotebook,
        (const ::qevercloud::Notebook & notebook,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<::qevercloud::Notebook>, createNotebookAsync,
        (const ::qevercloud::Notebook & notebook,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        qint32, updateNotebook,
        (const ::qevercloud::Notebook & notebook,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<qint32>, updateNotebookAsync,
        (const ::qevercloud::Notebook & notebook,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        qint32, expungeNotebook,
        (::qevercloud::Guid guid, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<qint32>, expungeNotebookAsync,
        (::qevercloud::Guid guid, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QList<::qevercloud::Tag>, listTags,
        (::qevercloud::IRequestContextPtr ctx), (override));

    MOCK_METHOD(
        QFuture<QList<::qevercloud::Tag>>, listTagsAsync,
        (::qevercloud::IRequestContextPtr ctx), (override));

    MOCK_METHOD(
        QList<::qevercloud::Tag>, listTagsByNotebook,
        (::qevercloud::Guid notebookGuid, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<QList<::qevercloud::Tag>>, listTagsByNotebookAsync,
        (::qevercloud::Guid notebookGuid, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        ::qevercloud::Tag, getTag,
        (::qevercloud::Guid guid, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<::qevercloud::Tag>, getTagAsync,
        (::qevercloud::Guid guid, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        ::qevercloud::Tag, createTag,
        (const ::qevercloud::Tag & tag, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<::qevercloud::Tag>, createTagAsync,
        (const ::qevercloud::Tag & tag, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        qint32, updateTag,
        (const ::qevercloud::Tag & tag, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<qint32>, updateTagAsync,
        (const ::qevercloud::Tag & tag, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        void, untagAll,
        (::qevercloud::Guid guid, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<void>, untagAllAsync,
        (::qevercloud::Guid guid, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        qint32, expungeTag,
        (::qevercloud::Guid guid, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<qint32>, expungeTagAsync,
        (::qevercloud::Guid guid, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QList<::qevercloud::SavedSearch>, listSearches,
        (::qevercloud::IRequestContextPtr ctx), (override));

    MOCK_METHOD(
        QFuture<QList<::qevercloud::SavedSearch>>, listSearchesAsync,
        (::qevercloud::IRequestContextPtr ctx), (override));

    MOCK_METHOD(
        ::qevercloud::SavedSearch, getSearch,
        (::qevercloud::Guid guid, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<::qevercloud::SavedSearch>, getSearchAsync,
        (::qevercloud::Guid guid, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        ::qevercloud::SavedSearch, createSearch,
        (const ::qevercloud::SavedSearch & search,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<::qevercloud::SavedSearch>, createSearchAsync,
        (const ::qevercloud::SavedSearch & search,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        qint32, updateSearch,
        (const ::qevercloud::SavedSearch & search,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<qint32>, updateSearchAsync,
        (const ::qevercloud::SavedSearch & search,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        qint32, expungeSearch,
        (::qevercloud::Guid guid, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<qint32>, expungeSearchAsync,
        (::qevercloud::Guid guid, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        qint32, findNoteOffset,
        (const ::qevercloud::NoteFilter & filter, ::qevercloud::Guid guid,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<qint32>, findNoteOffsetAsync,
        (const ::qevercloud::NoteFilter & filter, ::qevercloud::Guid guid,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        ::qevercloud::NotesMetadataList, findNotesMetadata,
        (const ::qevercloud::NoteFilter & filter, qint32 offset,
         qint32 maxNotes,
         const ::qevercloud::NotesMetadataResultSpec & resultSpec,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<::qevercloud::NotesMetadataList>, findNotesMetadataAsync,
        (const ::qevercloud::NoteFilter & filter, qint32 offset,
         qint32 maxNotes,
         const ::qevercloud::NotesMetadataResultSpec & resultSpec,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        ::qevercloud::NoteCollectionCounts, findNoteCounts,
        (const ::qevercloud::NoteFilter & filter, bool withTrash,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<::qevercloud::NoteCollectionCounts>, findNoteCountsAsync,
        (const ::qevercloud::NoteFilter & filter, bool withTrash,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        ::qevercloud::Note, getNoteWithResultSpec,
        (::qevercloud::Guid guid,
         const ::qevercloud::NoteResultSpec & resultSpec,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<::qevercloud::Note>, getNoteWithResultSpecAsync,
        (::qevercloud::Guid guid,
         const ::qevercloud::NoteResultSpec & resultSpec,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        ::qevercloud::Note, getNote,
        (::qevercloud::Guid guid, bool withContent, bool withResourcesData,
         bool withResourcesRecognition, bool withResourcesAlternateData,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<::qevercloud::Note>, getNoteAsync,
        (::qevercloud::Guid guid, bool withContent, bool withResourcesData,
         bool withResourcesRecognition, bool withResourcesAlternateData,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        ::qevercloud::LazyMap, getNoteApplicationData,
        (::qevercloud::Guid guid, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<::qevercloud::LazyMap>, getNoteApplicationDataAsync,
        (::qevercloud::Guid guid, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QString, getNoteApplicationDataEntry,
        (::qevercloud::Guid guid, QString key,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<QString>, getNoteApplicationDataEntryAsync,
        (::qevercloud::Guid guid, QString key,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        qint32, setNoteApplicationDataEntry,
        (::qevercloud::Guid guid, QString key, QString value,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<qint32>, setNoteApplicationDataEntryAsync,
        (::qevercloud::Guid guid, QString key, QString value,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        qint32, unsetNoteApplicationDataEntry,
        (::qevercloud::Guid guid, QString key,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<qint32>, unsetNoteApplicationDataEntryAsync,
        (::qevercloud::Guid guid, QString key,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QString, getNoteContent,
        (::qevercloud::Guid guid, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<QString>, getNoteContentAsync,
        (::qevercloud::Guid guid, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QString, getNoteSearchText,
        (::qevercloud::Guid guid, bool noteOnly, bool tokenizeForIndexing,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<QString>, getNoteSearchTextAsync,
        (::qevercloud::Guid guid, bool noteOnly, bool tokenizeForIndexing,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QString, getResourceSearchText,
        (::qevercloud::Guid guid, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<QString>, getResourceSearchTextAsync,
        (::qevercloud::Guid guid, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QStringList, getNoteTagNames,
        (::qevercloud::Guid guid, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<QStringList>, getNoteTagNamesAsync,
        (::qevercloud::Guid guid, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        ::qevercloud::Note, createNote,
        (const ::qevercloud::Note & note, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<::qevercloud::Note>, createNoteAsync,
        (const ::qevercloud::Note & note, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        ::qevercloud::Note, updateNote,
        (const ::qevercloud::Note & note, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<::qevercloud::Note>, updateNoteAsync,
        (const ::qevercloud::Note & note, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        qint32, deleteNote,
        (::qevercloud::Guid guid, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<qint32>, deleteNoteAsync,
        (::qevercloud::Guid guid, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        qint32, expungeNote,
        (::qevercloud::Guid guid, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<qint32>, expungeNoteAsync,
        (::qevercloud::Guid guid, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        ::qevercloud::Note, copyNote,
        (::qevercloud::Guid noteGuid, ::qevercloud::Guid toNotebookGuid,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<::qevercloud::Note>, copyNoteAsync,
        (::qevercloud::Guid noteGuid, ::qevercloud::Guid toNotebookGuid,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QList<::qevercloud::NoteVersionId>, listNoteVersions,
        (::qevercloud::Guid noteGuid, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<QList<::qevercloud::NoteVersionId>>, listNoteVersionsAsync,
        (::qevercloud::Guid noteGuid, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        ::qevercloud::Note, getNoteVersion,
        (::qevercloud::Guid noteGuid, qint32 updateSequenceNum,
         bool withResourcesData, bool withResourcesRecognition,
         bool withResourcesAlternateData, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<::qevercloud::Note>, getNoteVersionAsync,
        (::qevercloud::Guid noteGuid, qint32 updateSequenceNum,
         bool withResourcesData, bool withResourcesRecognition,
         bool withResourcesAlternateData, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        ::qevercloud::Resource, getResource,
        (::qevercloud::Guid guid, bool withData, bool withRecognition,
         bool withAttributes, bool withAlternateData,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<::qevercloud::Resource>, getResourceAsync,
        (::qevercloud::Guid guid, bool withData, bool withRecognition,
         bool withAttributes, bool withAlternateData,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        ::qevercloud::LazyMap, getResourceApplicationData,
        (::qevercloud::Guid guid, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<::qevercloud::LazyMap>, getResourceApplicationDataAsync,
        (::qevercloud::Guid guid, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QString, getResourceApplicationDataEntry,
        (::qevercloud::Guid guid, QString key,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<QString>, getResourceApplicationDataEntryAsync,
        (::qevercloud::Guid guid, QString key,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        qint32, setResourceApplicationDataEntry,
        (::qevercloud::Guid guid, QString key, QString value,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<qint32>, setResourceApplicationDataEntryAsync,
        (::qevercloud::Guid guid, QString key, QString value,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        qint32, unsetResourceApplicationDataEntry,
        (::qevercloud::Guid guid, QString key,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<qint32>, unsetResourceApplicationDataEntryAsync,
        (::qevercloud::Guid guid, QString key,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        qint32, updateResource,
        (const ::qevercloud::Resource & resource,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<qint32>, updateResourceAsync,
        (const ::qevercloud::Resource & resource,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QByteArray, getResourceData,
        (::qevercloud::Guid guid, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<QByteArray>, getResourceDataAsync,
        (::qevercloud::Guid guid, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        ::qevercloud::Resource, getResourceByHash,
        (::qevercloud::Guid noteGuid, QByteArray contentHash, bool withData,
         bool withRecognition, bool withAlternateData,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<::qevercloud::Resource>, getResourceByHashAsync,
        (::qevercloud::Guid noteGuid, QByteArray contentHash, bool withData,
         bool withRecognition, bool withAlternateData,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QByteArray, getResourceRecognition,
        (::qevercloud::Guid guid, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<QByteArray>, getResourceRecognitionAsync,
        (::qevercloud::Guid guid, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QByteArray, getResourceAlternateData,
        (::qevercloud::Guid guid, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<QByteArray>, getResourceAlternateDataAsync,
        (::qevercloud::Guid guid, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        ::qevercloud::ResourceAttributes, getResourceAttributes,
        (::qevercloud::Guid guid, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<::qevercloud::ResourceAttributes>, getResourceAttributesAsync,
        (::qevercloud::Guid guid, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        ::qevercloud::Notebook, getPublicNotebook,
        (::qevercloud::UserID userId, QString publicUri,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<::qevercloud::Notebook>, getPublicNotebookAsync,
        (::qevercloud::UserID userId, QString publicUri,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        ::qevercloud::SharedNotebook, shareNotebook,
        (const ::qevercloud::SharedNotebook & sharedNotebook, QString message,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<::qevercloud::SharedNotebook>, shareNotebookAsync,
        (const ::qevercloud::SharedNotebook & sharedNotebook, QString message,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        ::qevercloud::CreateOrUpdateNotebookSharesResult,
        createOrUpdateNotebookShares,
        (const ::qevercloud::NotebookShareTemplate & shareTemplate,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<::qevercloud::CreateOrUpdateNotebookSharesResult>,
        createOrUpdateNotebookSharesAsync,
        (const ::qevercloud::NotebookShareTemplate & shareTemplate,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        qint32, updateSharedNotebook,
        (const ::qevercloud::SharedNotebook & sharedNotebook,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<qint32>, updateSharedNotebookAsync,
        (const ::qevercloud::SharedNotebook & sharedNotebook,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        ::qevercloud::Notebook, setNotebookRecipientSettings,
        (QString notebookGuid,
         const ::qevercloud::NotebookRecipientSettings & recipientSettings,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<::qevercloud::Notebook>, setNotebookRecipientSettingsAsync,
        (QString notebookGuid,
         const ::qevercloud::NotebookRecipientSettings & recipientSettings,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QList<::qevercloud::SharedNotebook>, listSharedNotebooks,
        (::qevercloud::IRequestContextPtr ctx), (override));

    MOCK_METHOD(
        QFuture<QList<::qevercloud::SharedNotebook>>, listSharedNotebooksAsync,
        (::qevercloud::IRequestContextPtr ctx), (override));

    MOCK_METHOD(
        ::qevercloud::LinkedNotebook, createLinkedNotebook,
        (const ::qevercloud::LinkedNotebook & linkedNotebook,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<::qevercloud::LinkedNotebook>, createLinkedNotebookAsync,
        (const ::qevercloud::LinkedNotebook & linkedNotebook,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        qint32, updateLinkedNotebook,
        (const ::qevercloud::LinkedNotebook & linkedNotebook,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<qint32>, updateLinkedNotebookAsync,
        (const ::qevercloud::LinkedNotebook & linkedNotebook,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QList<::qevercloud::LinkedNotebook>, listLinkedNotebooks,
        (::qevercloud::IRequestContextPtr ctx), (override));

    MOCK_METHOD(
        QFuture<QList<::qevercloud::LinkedNotebook>>, listLinkedNotebooksAsync,
        (::qevercloud::IRequestContextPtr ctx), (override));

    MOCK_METHOD(
        qint32, expungeLinkedNotebook,
        (::qevercloud::Guid guid, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<qint32>, expungeLinkedNotebookAsync,
        (::qevercloud::Guid guid, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        ::qevercloud::AuthenticationResult, authenticateToSharedNotebook,
        (QString shareKeyOrGlobalId, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<::qevercloud::AuthenticationResult>,
        authenticateToSharedNotebookAsync,
        (QString shareKeyOrGlobalId, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        ::qevercloud::SharedNotebook, getSharedNotebookByAuth,
        (::qevercloud::IRequestContextPtr ctx), (override));

    MOCK_METHOD(
        QFuture<::qevercloud::SharedNotebook>, getSharedNotebookByAuthAsync,
        (::qevercloud::IRequestContextPtr ctx), (override));

    MOCK_METHOD(
        void, emailNote,
        (const ::qevercloud::NoteEmailParameters & parameters,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<void>, emailNoteAsync,
        (const ::qevercloud::NoteEmailParameters & parameters,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QString, shareNote,
        (::qevercloud::Guid guid, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<QString>, shareNoteAsync,
        (::qevercloud::Guid guid, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        void, stopSharingNote,
        (::qevercloud::Guid guid, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<void>, stopSharingNoteAsync,
        (::qevercloud::Guid guid, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        ::qevercloud::AuthenticationResult, authenticateToSharedNote,
        (QString guid, QString noteKey, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<::qevercloud::AuthenticationResult>,
        authenticateToSharedNoteAsync,
        (QString guid, QString noteKey, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        ::qevercloud::RelatedResult, findRelated,
        (const ::qevercloud::RelatedQuery & query,
         const ::qevercloud::RelatedResultSpec & resultSpec,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<::qevercloud::RelatedResult>, findRelatedAsync,
        (const ::qevercloud::RelatedQuery & query,
         const ::qevercloud::RelatedResultSpec & resultSpec,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        ::qevercloud::UpdateNoteIfUsnMatchesResult, updateNoteIfUsnMatches,
        (const ::qevercloud::Note & note, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<::qevercloud::UpdateNoteIfUsnMatchesResult>,
        updateNoteIfUsnMatchesAsync,
        (const ::qevercloud::Note & note, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        ::qevercloud::ManageNotebookSharesResult, manageNotebookShares,
        (const ::qevercloud::ManageNotebookSharesParameters & parameters,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<::qevercloud::ManageNotebookSharesResult>,
        manageNotebookSharesAsync,
        (const ::qevercloud::ManageNotebookSharesParameters & parameters,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        ::qevercloud::ShareRelationships, getNotebookShares,
        (QString notebookGuid, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<::qevercloud::ShareRelationships>, getNotebookSharesAsync,
        (QString notebookGuid, ::qevercloud::IRequestContextPtr ctx),
        (override));
};

} // namespace quentier::synchronization::tests::mocks::qevercloud
