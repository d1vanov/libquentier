/*
 * Copyright 2024 Dmitry Ivanov
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

#include <qevercloud/services/INoteStore.h>

namespace quentier::synchronization::tests {

class FakeNoteStore : public qevercloud::INoteStore
{
public:
    FakeNoteStore(
        FakeNoteStoreBackend * backend, QString noteStoreUrl,
        std::optional<qevercloud::Guid> linkedNotebookGuid,
        qevercloud::IRequestContextPtr ctx,
        qevercloud::IRetryPolicyPtr retryPolicy);

public: // qevercloud::INoteStore
    [[nodiscard]] qevercloud::IRequestContextPtr defaultRequestContext()
        const noexcept override;

    void setDefaultRequestContext(
        qevercloud::IRequestContextPtr ctx) noexcept override;

    [[nodiscard]] QString noteStoreUrl() const override;
    void setNoteStoreUrl(QString url) override;

    [[nodiscard]] const std::optional<qevercloud::Guid> & linkedNotebookGuid()
        const noexcept override;

    void setLinkedNotebookGuid(
        std::optional<qevercloud::Guid> linkedNotebookGuid) override;

    [[nodiscard]] qevercloud::SyncState getSyncState(
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<qevercloud::SyncState> getSyncStateAsync(
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] qevercloud::SyncChunk getFilteredSyncChunk(
        qint32 afterUSN, qint32 maxEntries,
        const qevercloud::SyncChunkFilter & filter,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<qevercloud::SyncChunk> getFilteredSyncChunkAsync(
        qint32 afterUSN, qint32 maxEntries,
        const qevercloud::SyncChunkFilter & filter,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] qevercloud::SyncState getLinkedNotebookSyncState(
        const qevercloud::LinkedNotebook & linkedNotebook,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<qevercloud::SyncState>
        getLinkedNotebookSyncStateAsync(
            const qevercloud::LinkedNotebook & linkedNotebook,
            qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] qevercloud::SyncChunk getLinkedNotebookSyncChunk(
        const qevercloud::LinkedNotebook & linkedNotebook, qint32 afterUSN,
        qint32 maxEntries, bool fullSyncOnly,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<qevercloud::SyncChunk>
        getLinkedNotebookSyncChunkAsync(
            const qevercloud::LinkedNotebook & linkedNotebook, qint32 afterUSN,
            qint32 maxEntries, bool fullSyncOnly,
            qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QList<qevercloud::Notebook> listNotebooks(
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<QList<qevercloud::Notebook>> listNotebooksAsync(
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QList<qevercloud::Notebook> listAccessibleBusinessNotebooks(
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<QList<qevercloud::Notebook>>
        listAccessibleBusinessNotebooksAsync(
            qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] qevercloud::Notebook getNotebook(
        qevercloud::Guid guid,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<qevercloud::Notebook> getNotebookAsync(
        qevercloud::Guid guid,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] qevercloud::Notebook getDefaultNotebook(
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<qevercloud::Notebook> getDefaultNotebookAsync(
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] qevercloud::Notebook createNotebook(
        const qevercloud::Notebook & notebook,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<qevercloud::Notebook> createNotebookAsync(
        const qevercloud::Notebook & notebook,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] qint32 updateNotebook(
        const qevercloud::Notebook & notebook,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<qint32> updateNotebookAsync(
        const qevercloud::Notebook & notebook,
        qevercloud::IRequestContextPtr ctx) override;

    [[nodiscard]] qint32 expungeNotebook(
        qevercloud::Guid guid,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<qint32> expungeNotebookAsync(
        qevercloud::Guid guid,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QList<qevercloud::Tag> listTags(
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<QList<qevercloud::Tag>> listTagsAsync(
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QList<qevercloud::Tag> listTagsByNotebook(
        qevercloud::Guid notebookGuid,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<QList<qevercloud::Tag>> listTagsByNotebookAsync(
        qevercloud::Guid notebookGuid,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] qevercloud::Tag getTag(
        qevercloud::Guid guid,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<qevercloud::Tag> getTagAsync(
        qevercloud::Guid guid,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] qevercloud::Tag createTag(
        const qevercloud::Tag & tag,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<qevercloud::Tag> createTagAsync(
        const qevercloud::Tag & tag,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] qint32 updateTag(
        const qevercloud::Tag & tag,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<qint32> updateTagAsync(
        const qevercloud::Tag & tag,
        qevercloud::IRequestContextPtr ctx = {}) override;

    void untagAll(
        qevercloud::Guid guid,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<void> untagAllAsync(
        qevercloud::Guid guid,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] qint32 expungeTag(
        qevercloud::Guid guid,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<qint32> expungeTagAsync(
        qevercloud::Guid guid,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QList<qevercloud::SavedSearch> listSearches(
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<QList<qevercloud::SavedSearch>> listSearchesAsync(
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] qevercloud::SavedSearch getSearch(
        qevercloud::Guid guid,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<qevercloud::SavedSearch> getSearchAsync(
        qevercloud::Guid guid,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] qevercloud::SavedSearch createSearch(
        const qevercloud::SavedSearch & search,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<qevercloud::SavedSearch> createSearchAsync(
        const qevercloud::SavedSearch & search,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] qint32 updateSearch(
        const qevercloud::SavedSearch & search,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<qint32> updateSearchAsync(
        const qevercloud::SavedSearch & search,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] qint32 expungeSearch(
        qevercloud::Guid guid,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<qint32> expungeSearchAsync(
        qevercloud::Guid guid,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] qint32 findNoteOffset(
        const qevercloud::NoteFilter & filter, qevercloud::Guid guid,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<qint32> findNoteOffsetAsync(
        const qevercloud::NoteFilter & filter, qevercloud::Guid guid,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] qevercloud::NotesMetadataList findNotesMetadata(
        const qevercloud::NoteFilter & filter, qint32 offset, qint32 maxNotes,
        const qevercloud::NotesMetadataResultSpec & resultSpec,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<qevercloud::NotesMetadataList> findNotesMetadataAsync(
        const qevercloud::NoteFilter & filter, qint32 offset, qint32 maxNotes,
        const qevercloud::NotesMetadataResultSpec & resultSpec,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] qevercloud::NoteCollectionCounts findNoteCounts(
        const qevercloud::NoteFilter & filter, bool withTrash,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<qevercloud::NoteCollectionCounts> findNoteCountsAsync(
        const qevercloud::NoteFilter & filter, bool withTrash,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] qevercloud::Note getNoteWithResultSpec(
        qevercloud::Guid guid, const qevercloud::NoteResultSpec & resultSpec,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<qevercloud::Note> getNoteWithResultSpecAsync(
        qevercloud::Guid guid, const qevercloud::NoteResultSpec & resultSpec,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] qevercloud::Note getNote(
        qevercloud::Guid guid, bool withContent, bool withResourcesData,
        bool withResourcesRecognition, bool withResourcesAlternateData,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<qevercloud::Note> getNoteAsync(
        qevercloud::Guid guid, bool withContent, bool withResourcesData,
        bool withResourcesRecognition, bool withResourcesAlternateData,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] qevercloud::LazyMap getNoteApplicationData(
        qevercloud::Guid guid,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<qevercloud::LazyMap> getNoteApplicationDataAsync(
        qevercloud::Guid guid,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QString getNoteApplicationDataEntry(
        qevercloud::Guid guid, QString key,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<QString> getNoteApplicationDataEntryAsync(
        qevercloud::Guid guid, QString key,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] qint32 setNoteApplicationDataEntry(
        qevercloud::Guid guid, QString key, QString value,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<qint32> setNoteApplicationDataEntryAsync(
        qevercloud::Guid guid, QString key, QString value,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] qint32 unsetNoteApplicationDataEntry(
        qevercloud::Guid guid, QString key,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<qint32> unsetNoteApplicationDataEntryAsync(
        qevercloud::Guid guid, QString key,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QString getNoteContent(
        qevercloud::Guid guid,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<QString> getNoteContentAsync(
        qevercloud::Guid guid,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QString getNoteSearchText(
        qevercloud::Guid guid, bool noteOnly, bool tokenizeForIndexing,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<QString> getNoteSearchTextAsync(
        qevercloud::Guid guid, bool noteOnly, bool tokenizeForIndexing,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QString getResourceSearchText(
        qevercloud::Guid guid,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<QString> getResourceSearchTextAsync(
        qevercloud::Guid guid,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QStringList getNoteTagNames(
        qevercloud::Guid guid,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<QStringList> getNoteTagNamesAsync(
        qevercloud::Guid guid,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] qevercloud::Note createNote(
        const qevercloud::Note & note,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<qevercloud::Note> createNoteAsync(
        const qevercloud::Note & note,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] qevercloud::Note updateNote(
        const qevercloud::Note & note,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<qevercloud::Note> updateNoteAsync(
        const qevercloud::Note & note,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] qint32 deleteNote(
        qevercloud::Guid guid,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<qint32> deleteNoteAsync(
        qevercloud::Guid guid,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] qint32 expungeNote(
        qevercloud::Guid guid,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<qint32> expungeNoteAsync(
        qevercloud::Guid guid,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] qevercloud::Note copyNote(
        qevercloud::Guid noteGuid, qevercloud::Guid toNotebookGuid,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<qevercloud::Note> copyNoteAsync(
        qevercloud::Guid noteGuid, qevercloud::Guid toNotebookGuid,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QList<qevercloud::NoteVersionId> listNoteVersions(
        qevercloud::Guid noteGuid,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<QList<qevercloud::NoteVersionId>>
        listNoteVersionsAsync(
            qevercloud::Guid noteGuid,
            qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] qevercloud::Note getNoteVersion(
        qevercloud::Guid noteGuid, qint32 updateSequenceNum,
        bool withResourcesData, bool withResourcesRecognition,
        bool withResourcesAlternateData,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<qevercloud::Note> getNoteVersionAsync(
        qevercloud::Guid noteGuid, qint32 updateSequenceNum,
        bool withResourcesData, bool withResourcesRecognition,
        bool withResourcesAlternateData,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] qevercloud::Resource getResource(
        qevercloud::Guid guid, bool withData, bool withRecognition,
        bool withAttributes, bool withAlternateData,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<qevercloud::Resource> getResourceAsync(
        qevercloud::Guid guid, bool withData, bool withRecognition,
        bool withAttributes, bool withAlternateData,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] qevercloud::LazyMap getResourceApplicationData(
        qevercloud::Guid guid,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<qevercloud::LazyMap> getResourceApplicationDataAsync(
        qevercloud::Guid guid,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QString getResourceApplicationDataEntry(
        qevercloud::Guid guid, QString key,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<QString> getResourceApplicationDataEntryAsync(
        qevercloud::Guid guid, QString key,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] qint32 setResourceApplicationDataEntry(
        qevercloud::Guid guid, QString key, QString value,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<qint32> setResourceApplicationDataEntryAsync(
        qevercloud::Guid guid, QString key, QString value,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] qint32 unsetResourceApplicationDataEntry(
        qevercloud::Guid guid, QString key,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<qint32> unsetResourceApplicationDataEntryAsync(
        qevercloud::Guid guid, QString key,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] qint32 updateResource(
        const qevercloud::Resource & resource,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<qint32> updateResourceAsync(
        const qevercloud::Resource & resource,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QByteArray getResourceData(
        qevercloud::Guid guid,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<QByteArray> getResourceDataAsync(
        qevercloud::Guid guid,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] qevercloud::Resource getResourceByHash(
        qevercloud::Guid noteGuid, QByteArray contentHash, bool withData,
        bool withRecognition, bool withAlternateData,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<qevercloud::Resource> getResourceByHashAsync(
        qevercloud::Guid noteGuid, QByteArray contentHash, bool withData,
        bool withRecognition, bool withAlternateData,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QByteArray getResourceRecognition(
        qevercloud::Guid guid,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<QByteArray> getResourceRecognitionAsync(
        qevercloud::Guid guid,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QByteArray getResourceAlternateData(
        qevercloud::Guid guid,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<QByteArray> getResourceAlternateDataAsync(
        qevercloud::Guid guid,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] qevercloud::ResourceAttributes getResourceAttributes(
        qevercloud::Guid guid,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<qevercloud::ResourceAttributes>
        getResourceAttributesAsync(
            qevercloud::Guid guid,
            qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] qevercloud::Notebook getPublicNotebook(
        qevercloud::UserID userId, QString publicUri,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<qevercloud::Notebook> getPublicNotebookAsync(
        qevercloud::UserID userId, QString publicUri,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] qevercloud::SharedNotebook shareNotebook(
        const qevercloud::SharedNotebook & sharedNotebook, QString message,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<qevercloud::SharedNotebook> shareNotebookAsync(
        const qevercloud::SharedNotebook & sharedNotebook, QString message,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] qevercloud::CreateOrUpdateNotebookSharesResult
        createOrUpdateNotebookShares(
            const qevercloud::NotebookShareTemplate & shareTemplate,
            qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<qevercloud::CreateOrUpdateNotebookSharesResult>
        createOrUpdateNotebookSharesAsync(
            const qevercloud::NotebookShareTemplate & shareTemplate,
            qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] qint32 updateSharedNotebook(
        const qevercloud::SharedNotebook & sharedNotebook,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<qint32> updateSharedNotebookAsync(
        const qevercloud::SharedNotebook & sharedNotebook,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] qevercloud::Notebook setNotebookRecipientSettings(
        QString notebookGuid,
        const qevercloud::NotebookRecipientSettings & recipientSettings,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<qevercloud::Notebook>
        setNotebookRecipientSettingsAsync(
            QString notebookGuid,
            const qevercloud::NotebookRecipientSettings & recipientSettings,
            qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QList<qevercloud::SharedNotebook> listSharedNotebooks(
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<QList<qevercloud::SharedNotebook>>
        listSharedNotebooksAsync(
            qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] qevercloud::LinkedNotebook createLinkedNotebook(
        const qevercloud::LinkedNotebook & linkedNotebook,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<qevercloud::LinkedNotebook> createLinkedNotebookAsync(
        const qevercloud::LinkedNotebook & linkedNotebook,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] qint32 updateLinkedNotebook(
        const qevercloud::LinkedNotebook & linkedNotebook,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<qint32> updateLinkedNotebookAsync(
        const qevercloud::LinkedNotebook & linkedNotebook,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QList<qevercloud::LinkedNotebook> listLinkedNotebooks(
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<QList<qevercloud::LinkedNotebook>>
        listLinkedNotebooksAsync(
            qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] qint32 expungeLinkedNotebook(
        qevercloud::Guid guid,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<qint32> expungeLinkedNotebookAsync(
        qevercloud::Guid guid,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] qevercloud::AuthenticationResult authenticateToSharedNotebook(
        QString shareKeyOrGlobalId,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<qevercloud::AuthenticationResult>
        authenticateToSharedNotebookAsync(
            QString shareKeyOrGlobalId,
            qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] qevercloud::SharedNotebook getSharedNotebookByAuth(
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<qevercloud::SharedNotebook>
        getSharedNotebookByAuthAsync(
            qevercloud::IRequestContextPtr ctx = {}) override;

    void emailNote(
        const qevercloud::NoteEmailParameters & parameters,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<void> emailNoteAsync(
        const qevercloud::NoteEmailParameters & parameters,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QString shareNote(
        qevercloud::Guid guid,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<QString> shareNoteAsync(
        qevercloud::Guid guid,
        qevercloud::IRequestContextPtr ctx = {}) override;

    void stopSharingNote(
        qevercloud::Guid guid,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<void> stopSharingNoteAsync(
        qevercloud::Guid guid,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] qevercloud::AuthenticationResult authenticateToSharedNote(
        QString guid, QString noteKey,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<qevercloud::AuthenticationResult>
        authenticateToSharedNoteAsync(
            QString guid, QString noteKey,
            qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] qevercloud::RelatedResult findRelated(
        const qevercloud::RelatedQuery & query,
        const qevercloud::RelatedResultSpec & resultSpec,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<qevercloud::RelatedResult> findRelatedAsync(
        const qevercloud::RelatedQuery & query,
        const qevercloud::RelatedResultSpec & resultSpec,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] qevercloud::UpdateNoteIfUsnMatchesResult
        updateNoteIfUsnMatches(
            const qevercloud::Note & note,
            qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<qevercloud::UpdateNoteIfUsnMatchesResult>
        updateNoteIfUsnMatchesAsync(
            const qevercloud::Note & note,
            qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] qevercloud::ManageNotebookSharesResult manageNotebookShares(
        const qevercloud::ManageNotebookSharesParameters & parameters,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<qevercloud::ManageNotebookSharesResult>
        manageNotebookSharesAsync(
            const qevercloud::ManageNotebookSharesParameters & parameters,
            qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] qevercloud::ShareRelationships getNotebookShares(
        QString notebookGuid, qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<qevercloud::ShareRelationships>
        getNotebookSharesAsync(
            QString notebookGuid,
            qevercloud::IRequestContextPtr ctx = {}) override;

private:
    void ensureRequestContext(qevercloud::IRequestContextPtr & ctx) const;

private:
    FakeNoteStoreBackend * m_backend;
    QString m_noteStoreUrl;
    std::optional<qevercloud::Guid> m_linkedNotebookGuid;
    qevercloud::IRequestContextPtr m_ctx;
    qevercloud::IRetryPolicyPtr m_retryPolicy;
};

} // namespace quentier::synchronization::tests
