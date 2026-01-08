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

#include "StopSynchronizationErrorTrigger.h"
#include "note_store/LinkedNotebooks.h"
#include "note_store/Notebooks.h"
#include "note_store/Notes.h"
#include "note_store/Resources.h"
#include "note_store/SavedSearches.h"
#include "note_store/Tags.h"

#include <quentier/synchronization/types/Errors.h>

#include <qevercloud/Constants.h>
#include <qevercloud/Fwd.h>
#include <qevercloud/types/AuthenticationResult.h>
#include <qevercloud/types/LinkedNotebook.h>
#include <qevercloud/types/Note.h>
#include <qevercloud/types/Notebook.h>
#include <qevercloud/types/Resource.h>
#include <qevercloud/types/SavedSearch.h>
#include <qevercloud/types/SyncChunk.h>
#include <qevercloud/types/SyncState.h>
#include <qevercloud/types/Tag.h>
#include <qevercloud/types/TypeAliases.h>

#include <QHash>
#include <QNetworkCookie>
#include <QObject>
#include <QSet>
#include <QUuid>

#include <boost/bimap.hpp>

#include <exception>
#include <optional>
#include <utility>
#include <variant>

namespace quentier::synchronization::tests {

class FakeNoteStoreBackend : public QObject
{
    Q_OBJECT
public:
    struct ItemData
    {
        // Contains automatically generated or adjusted name of the item (to
        // ensure their uniqueness within the account for the items of the
        // corresponding type) if generation and/or adjustment was necessary.
        std::optional<QString> name;

        // Contains automatically generated guid of the item if it didn't have
        // guid when it was put to the server.
        std::optional<qevercloud::Guid> guid;

        // Update sequence number assigned to the item
        qint32 usn = 0;

        // For notes only: update sequence numbers assigned to note's resources
        QHash<qevercloud::Guid, qint32> resourceUsns;
    };

public:
    FakeNoteStoreBackend(
        QString authenticationToken, QList<QNetworkCookie> cookies,
        QObject * parent = nullptr);

    ~FakeNoteStoreBackend() override;

    // Saved searches
    [[nodiscard]] QHash<qevercloud::Guid, qevercloud::SavedSearch>
        savedSearches() const;

    ItemData putSavedSearch(qevercloud::SavedSearch search);
    [[nodiscard]] std::optional<qevercloud::SavedSearch> findSavedSearch(
        const qevercloud::Guid & guid) const;

    void removeSavedSearch(const qevercloud::Guid & guid);

    // Expunged saved searches
    void putExpungedSavedSearchGuid(const qevercloud::Guid & guid);
    [[nodiscard]] bool containsExpungedSavedSearchGuid(
        const qevercloud::Guid & guid) const;

    void removeExpungedSavedSearchGuid(const qevercloud::Guid & guid);

    // Tags
    [[nodiscard]] QHash<qevercloud::Guid, qevercloud::Tag> tags() const;
    ItemData putTag(qevercloud::Tag tag);

    [[nodiscard]] std::optional<qevercloud::Tag> findTag(
        const qevercloud::Guid & guid) const;

    void removeTag(const qevercloud::Guid & guid);

    // Expunged tags
    void putExpungedUserOwnTagGuid(const qevercloud::Guid & guid);
    [[nodiscard]] bool containsExpungedUserOwnTagGuid(
        const qevercloud::Guid & guid) const;

    void removeExpungedUserOwnTagGuid(const qevercloud::Guid & guid);

    void putExpungedLinkedNotebookTagGuid(
        const qevercloud::Guid & linkedNotebookGuid,
        const qevercloud::Guid & tagGuid);

    [[nodiscard]] bool containsExpungedLinkedNotebookTagGuid(
        const qevercloud::Guid & linkedNotebookGuid,
        const qevercloud::Guid & tagGuid) const;

    void removeExpungedLinkedNotebookTagGuid(
        const qevercloud::Guid & linkedNotebookGuid,
        const qevercloud::Guid & tagGuid);

    // Notebooks
    [[nodiscard]] QHash<qevercloud::Guid, qevercloud::Notebook> notebooks()
        const;

    ItemData putNotebook(qevercloud::Notebook notebook);
    [[nodiscard]] std::optional<qevercloud::Notebook> findNotebook(
        const qevercloud::Guid & guid) const;

    void removeNotebook(const qevercloud::Guid & guid);

    [[nodiscard]] QList<qevercloud::Notebook>
        findNotebooksForLinkedNotebookGuid(
            const qevercloud::Guid & linkedNotebookGuid) const;

    // Expunged notebooks
    void putExpungedUserOwnNotebookGuid(const qevercloud::Guid & guid);
    [[nodiscard]] bool containsExpungedUserOwnNotebookGuid(
        const qevercloud::Guid & guid) const;

    void removeExpungedUserOwnNotebookGuid(const qevercloud::Guid & guid);

    void putExpungedLinkedNotebookNotebookGuid(
        const qevercloud::Guid & linkedNotebookGuid,
        const qevercloud::Guid & notebookGuid);

    [[nodiscard]] bool containsExpungedLinkedNotebookNotebookGuid(
        const qevercloud::Guid & linkedNotebookGuid,
        const qevercloud::Guid & notebookGuid) const;

    void removeExpungedLinkedNotebookNotebookGuid(
        const qevercloud::Guid & linkedNotebookGuid,
        const qevercloud::Guid & notebookGuid);

    // Notes
    [[nodiscard]] QHash<qevercloud::Guid, qevercloud::Note> notes() const;
    ItemData putNote(qevercloud::Note note);
    [[nodiscard]] std::optional<qevercloud::Note> findNote(
        const qevercloud::Guid & guid) const;

    void removeNote(const qevercloud::Guid & guid);

    [[nodiscard]] QList<qevercloud::Note> getNotesByConflictSourceNoteGuid(
        const qevercloud::Guid & conflictSourceNoteGuid) const;

    // Expunged notes
    void putExpungedUserOwnNoteGuid(const qevercloud::Guid & guid);
    [[nodiscard]] bool containsExpungedUserOwnNoteGuid(
        const qevercloud::Guid & guid) const;

    void removeExpungedUserOwnNoteGuid(const qevercloud::Guid & guid);

    void putExpungedLinkedNotebookNoteGuid(
        const qevercloud::Guid & linkedNotebookGuid,
        const qevercloud::Guid & noteGuid);

    [[nodiscard]] bool containsExpungedLinkedNotebookNoteGuid(
        const qevercloud::Guid & linkedNotebookGuid,
        const qevercloud::Guid & noteGuid) const;

    void removeExpungedLinkedNotebookNoteGuid(
        const qevercloud::Guid & linkedNotebookGuid,
        const qevercloud::Guid & noteGuid);

    // Resources
    [[nodiscard]] QHash<qevercloud::Guid, qevercloud::Resource> resources()
        const;

    ItemData putResource(qevercloud::Resource resource);
    [[nodiscard]] std::optional<qevercloud::Resource> findResource(
        const qevercloud::Guid & guid) const;

    void removeResource(const qevercloud::Guid & guid);

    // Linked notebooks
    [[nodiscard]] QHash<qevercloud::Guid, qevercloud::LinkedNotebook>
        linkedNotebooks() const;

    ItemData putLinkedNotebook(qevercloud::LinkedNotebook linkedNotebook);

    [[nodiscard]] std::optional<qevercloud::LinkedNotebook> findLinkedNotebook(
        const qevercloud::Guid & guid) const;

    void removeLinkedNotebook(const qevercloud::Guid & guid);

    // Expunged linked notebooks
    void putExpungedLinkedNotebookGuid(const qevercloud::Guid & guid);
    [[nodiscard]] bool containsExpungedLinkedNotebookGuid(
        const qevercloud::Guid & guid) const;

    void removeExpungedLinkedNotebookGuid(const qevercloud::Guid & guid);

    // User own sync state
    [[nodiscard]] qevercloud::SyncState userOwnSyncState() const;
    void putUserOwnSyncState(qevercloud::SyncState syncState);

    // Linked notebook sync states
    [[nodiscard]] QHash<qevercloud::Guid, qevercloud::SyncState>
        linkedNotebookSyncStates() const;

    void putLinkedNotebookSyncState(
        const qevercloud::Guid & linkedNotebookGuid,
        qevercloud::SyncState syncState);

    [[nodiscard]] std::optional<qevercloud::SyncState>
        findLinkedNotebookSyncState(
            const qevercloud::Guid & linkedNotebookGuid) const;

    void removeLinkedNotebookSyncState(
        const qevercloud::Guid & linkedNotebookGuid);

    void clearLinkedNotebookSyncStates();

    // Update sequence numbers
    [[nodiscard]] qint32 currentUserOwnMaxUsn() const noexcept;

    [[nodiscard]] std::optional<qint32> currentLinkedNotebookMaxUsn(
        const qevercloud::Guid & linkedNotebookGuid) const noexcept;

    // Stop synchronization error
    [[nodiscard]] std::optional<
        std::pair<StopSynchronizationErrorTrigger, StopSynchronizationError>>
        stopSynchronizationError() const;

    void setStopSynchronizationError(
        StopSynchronizationErrorTrigger trigger,
        StopSynchronizationError error);

    void clearStopSynchronizationError();

    // Other
    [[nodiscard]] quint32 maxNumSavedSearches() const noexcept;
    void setMaxNumSavedSearches(quint32 maxNumSavedSearches) noexcept;

    [[nodiscard]] quint32 maxNumTags() const noexcept;
    void setMaxNumTags(quint32 maxNumTags) noexcept;

    [[nodiscard]] quint32 maxNumNotebooks() const noexcept;
    void setMaxNumNotebooks(quint32 maxNumNotebooks) noexcept;

    [[nodiscard]] quint32 maxNumNotes() const noexcept;
    void setMaxNumNotes(quint32 maxNumNotes) noexcept;

    [[nodiscard]] quint64 maxNoteSize() const noexcept;
    void setMaxNoteSize(quint64 maxNoteSize) noexcept;

    [[nodiscard]] quint32 maxNumResourcesPerNote() const noexcept;
    void setMaxNumResourcesPerNote(quint32 maxNumResourcesPerNote) noexcept;

    [[nodiscard]] quint32 maxNumTagsPerNote() const noexcept;
    void setMaxNumTagsPerNote(quint32 maxNumTagsPerNote) noexcept;

    [[nodiscard]] quint64 maxResourceSize() const noexcept;
    void setMaxResourceSize(quint64 maxResourceSize) noexcept;

    [[nodiscard]] QHash<qevercloud::Guid, QString>
        linkedNotebookAuthTokensByGuid() const;

    void setLinkedNotebookAuthTokensByGuid(
        QHash<qevercloud::Guid, QString> tokens);

    void setUriForRequestId(QUuid requestId, QByteArray uri);
    void removeUriForRequestId(QUuid requestId);

Q_SIGNALS:
    void createNotebookRequestReady(
        qevercloud::Notebook notebook, std::exception_ptr e, QUuid requestId);

    void updateNotebookRequestReady(
        qint32 updateSequenceNum, std::exception_ptr e, QUuid requestId);

    void createNoteRequestReady(
        qevercloud::Note note, std::exception_ptr e, QUuid requestId);

    void updateNoteRequestReady(
        qevercloud::Note note, std::exception_ptr e, QUuid requestId);

    void createTagRequestReady(
        qevercloud::Tag, std::exception_ptr e, QUuid requestId);

    void updateTagRequestReady(
        qint32 updateSequenceNum, std::exception_ptr e, QUuid requestId);

    void createSavedSearchRequestReady(
        qevercloud::SavedSearch search, std::exception_ptr e, QUuid requestId);

    void updateSavedSearchRequestReady(
        qint32 updateSequenceNum, std::exception_ptr e, QUuid requestId);

    void getSyncStateRequestReady(
        qevercloud::SyncState syncState, std::exception_ptr e, QUuid requestId);

    void getLinkedNotebookSyncStateRequestReady(
        qevercloud::SyncState syncState, std::exception_ptr e, QUuid requestId);

    void getFilteredSyncChunkRequestReady(
        qevercloud::SyncChunk syncChunk, std::exception_ptr e, QUuid requestId);

    void getLinkedNotebookSyncChunkRequestReady(
        qevercloud::SyncChunk syncChunk, std::exception_ptr e, QUuid requestId);

    void getNoteWithResultSpecRequestReady(
        qevercloud::Note note, std::exception_ptr e, QUuid requestId);

    void getResourceRequestReady(
        qevercloud::Resource resource, std::exception_ptr e, QUuid requestId);

    void authenticateToSharedNotebookRequestReady(
        qevercloud::AuthenticationResult result, std::exception_ptr e,
        QUuid requestId);

public Q_SLOTS:
    void onCreateNotebookRequest(
        qevercloud::Notebook notebook,
        const qevercloud::IRequestContextPtr & ctx);

    void onUpdateNotebookRequest(
        qevercloud::Notebook notebook,
        const qevercloud::IRequestContextPtr & ctx);

    void onCreateNoteRequest(
        qevercloud::Note note, const qevercloud::IRequestContextPtr & ctx);

    void onUpdateNoteRequest(
        qevercloud::Note note, const qevercloud::IRequestContextPtr & ctx);

    void onCreateTagRequest(
        qevercloud::Tag tag, const qevercloud::IRequestContextPtr & ctx);

    void onUpdateTagRequest(
        qevercloud::Tag tag, const qevercloud::IRequestContextPtr & ctx);

    void onCreateSavedSearchRequest(
        qevercloud::SavedSearch search,
        const qevercloud::IRequestContextPtr & ctx);

    void onUpdateSavedSearchRequest(
        qevercloud::SavedSearch search,
        const qevercloud::IRequestContextPtr & ctx);

    void onGetSyncStateRequest(const qevercloud::IRequestContextPtr & ctx);

    void onGetLinkedNotebookSyncStateRequest(
        const qevercloud::LinkedNotebook & linkedNotebook,
        const qevercloud::IRequestContextPtr & ctx);

    void onGetFilteredSyncChunkRequest(
        qint32 afterUSN, qint32 maxEntries,
        const qevercloud::SyncChunkFilter & filter,
        const qevercloud::IRequestContextPtr & ctx);

    void onGetLinkedNotebookSyncChunkRequest(
        const qevercloud::LinkedNotebook & linkedNotebook, qint32 afterUSN,
        qint32 maxEntries, bool fullSyncOnly,
        const qevercloud::IRequestContextPtr & ctx);

    void onGetNoteWithResultSpecRequest(
        const qevercloud::Guid & guid,
        const qevercloud::NoteResultSpec & resultSpec,
        const qevercloud::IRequestContextPtr & ctx);

    void onGetResourceRequest(
        const qevercloud::Guid & guid, bool withData, bool withRecognition,
        bool withAttributes, bool withAlternateData,
        const qevercloud::IRequestContextPtr & ctx);

    void onAuthenticateToSharedNotebookRequest(
        const QString & shareKeyOrGlobalId,
        const qevercloud::IRequestContextPtr & ctx);

private:
    [[nodiscard]] std::exception_ptr checkAuthentication(
        const qevercloud::IRequestContextPtr & ctx) const;

    [[nodiscard]] std::exception_ptr checkLinkedNotebookAuthentication(
        const qevercloud::Guid & linkedNotebookGuid,
        const qevercloud::IRequestContextPtr & ctx) const;

    void setMaxUsn(
        qint32 maxUsn,
        const std::optional<qevercloud::Guid> & linkedNotebookGuid =
            std::nullopt);

    [[nodiscard]] std::pair<qevercloud::SyncChunk, std::exception_ptr>
        getSyncChunkImpl(
            qint32 afterUsn, qint32 maxEntries, bool fullSyncOnly,
            const std::optional<qevercloud::Guid> & linkedNotebookGuid,
            const qevercloud::SyncChunkFilter & filter,
            const qevercloud::IRequestContextPtr & ctx) const;

    [[nodiscard]] note_store::NotesByUSN::const_iterator nextNoteByUsnIterator(
        note_store::NotesByUSN::const_iterator it,
        const std::optional<qevercloud::Guid> & targetLinkedNotebookGuid = {})
        const;

    [[nodiscard]] note_store::ResourcesByUSN::const_iterator
        nextResourceByUsnIterator(
            note_store::ResourcesByUSN::const_iterator it,
            const std::optional<qevercloud::Guid> & targetLinkedNotebookGuid =
                {}) const;

private:
    struct StopSynchronizationErrorData
    {
        StopSynchronizationErrorTrigger trigger;
        StopSynchronizationError error;
    };

private:
    const QString m_authenticationToken;
    const QList<QNetworkCookie> m_cookies;

    QHash<qevercloud::Guid, QString> m_linkedNotebookAuthTokensByGuid;

    qint32 m_lastServedUserOwnSyncChunkHighUsn = -1;
    QHash<qevercloud::Guid, qint32> m_lastServedLinkedNotebookSyncChunkHighUsns;

    QHash<QUuid, QByteArray> m_uriByRequestId;

    note_store::SavedSearches m_savedSearches;
    QHash<qevercloud::Guid, qint32> m_expungedSavedSearchGuidsAndUsns;

    note_store::Tags m_tags;
    QHash<qevercloud::Guid, qint32> m_expungedUserOwnTagGuidsAndUsns;
    QHash<qevercloud::Guid, QHash<qevercloud::Guid, qint32>>
        m_expungedLinkedNotebookTagGuidsAndUsns;

    note_store::Notebooks m_notebooks;
    QHash<qevercloud::Guid, qint32> m_expungedUserOwnNotebookGuidsAndUsns;
    QHash<qevercloud::Guid, QHash<qevercloud::Guid, qint32>>
        m_expungedLinkedNotebookNotebookGuidsAndUsns;

    note_store::Notes m_notes;
    QHash<qevercloud::Guid, qint32> m_expungedUserOwnNoteGuidsAndUsns;
    QHash<qevercloud::Guid, QHash<qevercloud::Guid, qint32>>
        m_expungedLinkedNotebookNoteGuidsAndUsns;

    note_store::Resources m_resources;

    QSet<qevercloud::Guid> m_onceServedNoteGuids;

    note_store::LinkedNotebooks m_linkedNotebooks;
    QHash<QString, qint32> m_expungedLinkedNotebookGuidsAndUsns;

    std::optional<StopSynchronizationErrorData> m_stopSynchronizationErrorData;

    bool m_onceGetLinkedNotebookSyncChunkCalled = false;

    quint32 m_maxNumSavedSearches = 0;
    quint32 m_maxNumTags = 0;
    quint32 m_maxNumNotebooks = 0;
    quint32 m_maxNumNotes = 0;
    quint64 m_maxNoteSize = 0;
    quint32 m_maxNumResourcesPerNote = 0;
    quint32 m_maxNumTagsPerNote = 0;
    quint64 m_maxResourceSize = 0;

    qevercloud::SyncState m_userOwnSyncState;
    QHash<qevercloud::Guid, qevercloud::SyncState> m_linkedNotebookSyncStates;

    qint32 m_userOwnMaxUsn = 0;
    QHash<qevercloud::Guid, qint32> m_linkedNotebookMaxUsns;
};

} // namespace quentier::synchronization::tests
