/*
 * Copyright 2023 Dmitry Ivanov
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

#include "note_store/LinkedNotebooks.h"
#include "note_store/Notebooks.h"
#include "note_store/Notes.h"
#include "note_store/Resources.h"
#include "note_store/SavedSearches.h"
#include "note_store/Tags.h"

#include <quentier/synchronization/types/Errors.h>

#include <qevercloud/Constants.h>
#include <qevercloud/Fwd.h>
#include <qevercloud/types/LinkedNotebook.h>
#include <qevercloud/types/Note.h>
#include <qevercloud/types/Notebook.h>
#include <qevercloud/types/Resource.h>
#include <qevercloud/types/SavedSearch.h>
#include <qevercloud/types/SyncState.h>
#include <qevercloud/types/Tag.h>
#include <qevercloud/types/TypeAliases.h>

#include <QHash>
#include <QNetworkCookie>
#include <QObject>
#include <QSet>

#include <boost/bimap.hpp>

#include <exception>
#include <optional>
#include <utility>
#include <variant>

QT_BEGIN_NAMESPACE

class QTcpServer;
class QTcpSocket;

QT_END_NAMESPACE

namespace quentier::synchronization::tests {

class NoteStoreServer : public QObject
{
    Q_OBJECT
public:
    struct ItemData
    {
        // Contains automatically generated or adjusted name of the item (to
        // ensure their uniqueness within the account for the items of the
        // corresponding type) if generation and/or adjustment was necessary.
        std::optional<QString> name;

        // Update sequence number assigned to the item
        qint32 usn = 0;
    };

    enum class StopSynchronizationErrorTrigger
    {
        OnGetUserOwnSyncState,
        OnGetLinkedNotebookSyncState,
        OnGetUserOwnSyncChunk,
        OnGetNoteAfterDownloadingUserOwnSyncChunks,
        OnGetResourceAfterDownloadingUserOwnSyncChunks,
        OnGetLinkedNotebookSyncChunk,
        OnGetNoteAfterDownloadingLinkedNotebookSyncChunks,
        OnGetResourceAfterDownloadingLinkedNotebookSyncChunks,
        OnCreateSavedSearch,
        OnUpdateSavedSearch,
        OnCreateTag,
        OnUpdateTag,
        OnCreateNotebook,
        OnUpdateNotebook,
        OnCreateNote,
        OnUpdateNote,
        OnAuthenticateToSharedNotebook
    };

public:
    NoteStoreServer(
        QString authenticationToken, QList<QNetworkCookie> cookies,
        QHash<qevercloud::Guid, QString> linkedNotebookAuthTokensByGuid,
        QObject * parent = nullptr);

    ~NoteStoreServer() override;

    // Saved searches
    [[nodiscard]] QHash<qevercloud::Guid, qevercloud::SavedSearch>
        savedSearches() const;

    [[nodiscard]] ItemData putSavedSearch(qevercloud::SavedSearch search);
    [[nodiscard]] std::optional<qevercloud::SavedSearch> findSavedSearch(
        const QString & guid) const;

    void removeSavedSearch(const QString & guid);

    // Expunged saved searches
    void putExpungedSavedSearchGuid(const QString & guid);
    [[nodiscard]] bool containsExpungedSavedSearchGuid(
        const QString & guid) const;

    void removeExpungedSavedSearchGuid(const QString & guid);

    // Tags
    [[nodiscard]] QHash<qevercloud::Guid, qevercloud::Tag> tags() const;
    [[nodiscard]] ItemData putTag(qevercloud::Tag & tag);

    [[nodiscard]] std::optional<qevercloud::Tag> findTag(
        const QString & guid) const;

    void removeTag(const QString & guid);

    // Expunged tags
    void putExpungedTagGuid(const QString & guid);
    [[nodiscard]] bool containsExpungedTagGuid(const QString & guid) const;
    void removeExpungedTagGuid(const QString & guid);

    // Notebooks
    [[nodiscard]] QHash<qevercloud::Guid, qevercloud::Notebook> notebooks()
        const;

    [[nodiscard]] ItemData putNotebook(qevercloud::Notebook notebook);
    [[nodiscard]] std::optional<qevercloud::Notebook> findNotebook(
        const QString & guid) const;

    void removeNotebook(const QString & guid);

    [[nodiscard]] QList<qevercloud::Notebook>
        findNotebooksForLinkedNotebookGuid(
            const QString & linkedNotebookGuid) const;

    // Expunged notebooks
    void putExpungedNotebookGuid(const QString & guid);
    [[nodiscard]] bool containsExpungedNotebookGuid(const QString & guid) const;
    void removeExpungedNotebookGuid(const QString & guid);

    // Notes
    [[nodiscard]] QHash<qevercloud::Guid, qevercloud::Note> notes() const;
    [[nodiscard]] ItemData putNote(qevercloud::Note note);
    [[nodiscard]] std::optional<qevercloud::Note> findNote(
        const QString & guid) const;

    void removeNote(const QString & guid);

    // Expunged notes
    void putExpungedNoteGuid(const QString & guid);
    [[nodiscard]] bool containsExpungedNoteGuid(const QString & guid) const;
    void removeExpungedNoteGuid(const QString & guid);

    [[nodiscard]] QList<qevercloud::Note> getNotesByConflictSourceNoteGuid(
        const QString & conflictSourceNoteGuid) const;

    // Resources
    [[nodiscard]] QHash<qevercloud::Guid, qevercloud::Resource> resources()
        const;
    [[nodiscard]] bool putResource(qevercloud::Resource resource);
    [[nodiscard]] std::optional<qevercloud::Resource> findResource(
        const QString & guid) const;

    void removeResource(const QString & guid);

    // Linked notebooks
    [[nodiscard]] QHash<qevercloud::Guid, qevercloud::LinkedNotebook>
        linkedNotebooks() const;

    [[nodiscard]] ItemData putLinkedNotebook(
        qevercloud::LinkedNotebook & linkedNotebook);

    [[nodiscard]] std::optional<qevercloud::LinkedNotebook> findLinkedNotebook(
        const QString & guid) const;

    void removeLinkedNotebook(const QString & guid);

    // Expunged linked notebooks
    void putExpungedLinkedNotebookGuid(const QString & guid);
    [[nodiscard]] bool containsExpungedLinkedNotebookGuid(
        const QString & guid) const;

    void removeExpungedLinkedNotebookGuid(const QString & guid);

    // User own sync state
    [[nodiscard]] std::optional<qevercloud::SyncState> userOwnSyncState() const;
    void putUserOwnSyncState(qevercloud::SyncState syncState);
    void removeUserOwnSyncState();

    // Linked notebook sync states
    [[nodiscard]] QHash<qevercloud::Guid, qevercloud::SyncState>
        linkedNotebookSyncStates() const;

    void putLinkedNotebookSyncState(
        const qevercloud::Guid & linkedNotebookGuid,
        qevercloud::SyncState syncState);

    [[nodiscard]] std::optional<qevercloud::SyncState>
        findLinkedNotebookSyncState(
            const qevercloud::Guid & linkedNotebookGuid);

    void removeLinkedNotebookSyncState(
        const qevercloud::Guid & linkedNotebookGuid);

    void clearLinkedNotebookSyncStates();

    // Update sequence numbers
    [[nodiscard]] qint32 currentUserOwnMaxUsn() const;

    [[nodiscard]] std::optional<qint32> currentLinkedNotebookMaxUsn(
        const qevercloud::Guid & linkedNotebookGuid) const;

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

    // private signals
Q_SIGNALS:
    void createNotebookRequestReady(
        qevercloud::Notebook notebook, std::exception_ptr e);

    void updateNotebookRequestReady(
        qevercloud::Notebook notebook, std::exception_ptr e);

    void createNoteRequestReady(qevercloud::Note note, std::exception_ptr e);
    void updateNoteRequestReady(qevercloud::Note note, std::exception_ptr e);
    void createTagRequestReady(qevercloud::Tag tag, std::exception_ptr e);
    void updateTagRequestReady(qevercloud::Tag, std::exception_ptr e);

    void createSavedSearchRequestReady(
        qevercloud::SavedSearch search, std::exception_ptr e);

    void updateSavedSearchRequestReady(
        qevercloud::SavedSearch search, std::exception_ptr e);

    void getSyncStateRequestReady(
        qevercloud::SyncState syncState, std::exception_ptr e);

    void getLinkedNotebookSyncStateReady(
        qevercloud::SyncState syncState, std::exception_ptr e);

    void getFilteredSyncChunkRequestReady(
        qevercloud::SyncChunk syncChunk, std::exception_ptr e);

    void getLinkedNotebookSyncChunkRequestReady(
        qevercloud::SyncChunk syncChunk, std::exception_ptr e);

    void getNoteWithResultSpecRequestReady(
        qevercloud::Note note, std::exception_ptr e);

    void getResourceRequestReady(
        qevercloud::Resource resource, std::exception_ptr e);

    void authenticateToSharedNotebookRequestReady(
        qevercloud::AuthenticationResult result, std::exception_ptr e);

private Q_SLOTS:
    void onRequestReady(const QByteArray & responseData);

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
        const qevercloud::LinkedNotebook & linkedNotebook,
        qint32 afterUSN, qint32 maxEntries, bool fullSyncOnly,
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
    void connectToQEverCloudServer();

    [[nodiscard]] std::exception_ptr checkAuthentication(
        const qevercloud::IRequestContextPtr & ctx) const;

    [[nodiscard]] std::exception_ptr checkLinkedNotetookAuthentication(
        const qevercloud::IRequestContextPtr & ctx) const;

private:
    struct StopSynchronizationErrorData
    {
        StopSynchronizationErrorTrigger trigger;
        StopSynchronizationError error;
    };

private:
    const QString m_authenticationToken;
    const QList<QNetworkCookie> m_cookies;
    const QHash<qevercloud::Guid, QString> m_linkedNotebookAuthTokensByGuid;

    QTcpServer * m_tcpServer = nullptr;
    QTcpSocket * m_tcpSocket = nullptr;
    qevercloud::NoteStoreServer * m_server = nullptr;

    note_store::SavedSearches m_savedSearches;
    QSet<qevercloud::Guid> m_expungedSavedSearchGuids;

    note_store::Tags m_tags;
    QSet<qevercloud::Guid> m_expungedTagGuids;

    note_store::Notebooks m_notebooks;
    QSet<qevercloud::Guid> m_expungedNotebookGuids;

    note_store::Notes m_notes;
    QSet<qevercloud::Guid> m_expungedNoteGuids;

    note_store::Resources m_resources;

    note_store::LinkedNotebooks m_linkedNotebooks;
    QSet<QString> m_expungedLinkedNotebookGuids;

    std::optional<StopSynchronizationErrorData> m_stopSynchronizationErrorData;

    quint32 m_maxNumSavedSearches;
    quint32 m_maxNumTags;
    quint32 m_maxNumNotebooks;
    quint32 m_maxNumNotes;
    quint64 m_maxNoteSize;
    quint32 m_maxNumResourcesPerNote;
    quint32 m_maxNumTagsPerNote;
    quint64 m_maxResourceSize;

    qevercloud::SyncState m_userOwnSyncState;
    QHash<QString, qevercloud::SyncState> m_linkedNotebookSyncStates;
};

} // namespace quentier::synchronization::tests
