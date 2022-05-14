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

#include <quentier/local_storage/Fwd.h>
#include <quentier/synchronization/Fwd.h>
#include <quentier/synchronization/ISyncChunksDataCounters.h>
#include <quentier/utility/Fwd.h>
#include <quentier/utility/Linkage.h>

#include <qevercloud/types/Note.h>
#include <qevercloud/types/TypeAliases.h>

#include <QDir>
#include <QException>
#include <QFuture>
#include <QHash>
#include <QList>
#include <QNetworkCookie>

#include <memory>
#include <optional>

namespace quentier {

class Account;
class ErrorString;

} // namespace quentier

namespace quentier::synchronization {

class QUENTIER_EXPORT ISynchronizer
{
public:
    struct QUENTIER_EXPORT Options
    {
        bool downloadNoteThumbnails = false;
        std::optional<QDir> inkNoteImagesStorageDir;
    };

    struct QUENTIER_EXPORT AuthResult
    {
        qevercloud::UserID userId = 0;
        QString authToken;
        qevercloud::Timestamp authTokenExpirationTime = 0;
        QString shardId;
        QString noteStoreUrl;
        QString webApiUrlPrefix;
        QList<QNetworkCookie> userStoreCookies;
    };

    struct QUENTIER_EXPORT SyncStats
    {
        quint64 syncChunksDownloaded = 0;

        quint64 linkedNotebooksDownloaded = 0;
        quint64 notebooksDownloaded = 0;
        quint64 savedSearchesDownloaded = 0;
        quint64 tagsDownloaded = 0;
        quint64 notesDownloaded = 0;
        quint64 resourcesDownloaded = 0;

        quint64 linkedNotebooksExpunged = 0;
        quint64 notebooksExpunged = 0;
        quint64 savedSearchesExpunged = 0;
        quint64 tagsExpunged = 0;
        quint64 notesExpunged = 0;
        quint64 resourcesExpunged = 0;

        quint64 notebooksSent = 0;
        quint64 savedSearchesSent = 0;
        quint64 tagsSent = 0;
        quint64 notesSent = 0;
    };

    struct QUENTIER_EXPORT SyncState
    {
        qint32 updateCount = 0;
        qevercloud::Timestamp lastSyncTime = 0;
    };

    struct QUENTIER_EXPORT DownloadNotesStatus
    {
        quint64 totalNewNotes = 0UL;
        quint64 totalUpdatedNotes = 0UL;
        quint64 totalExpungedNotes = 0UL;

        struct QUENTIER_EXPORT NoteWithException
        {
            qevercloud::Note note;
            std::shared_ptr<QException> exception;
        };

        struct QUENTIER_EXPORT GuidWithException
        {
            qevercloud::Guid guid;
            std::shared_ptr<QException> exception;
        };

        using UpdateSequenceNumbersByGuid = QHash<qevercloud::Guid, qint32>;

        struct QUENTIER_EXPORT GuidWithUsn
        {
            qevercloud::Guid guid;
            qint32 updateSequenceNumber = 0;
        };

        QList<NoteWithException> notesWhichFailedToDownload;
        QList<NoteWithException> notesWhichFailedToProcess;
        QList<GuidWithException> noteGuidsWhichFailedToExpunge;

        UpdateSequenceNumbersByGuid processedNoteGuidsAndUsns;
        UpdateSequenceNumbersByGuid cancelledNoteGuidsAndUsns;
        QList<qevercloud::Guid> expungedNoteGuids;
    };

    struct QUENTIER_EXPORT DownloadResourcesStatus
    {
        quint64 totalNewResources = 0UL;
        quint64 totalUpdatedResources = 0UL;

        struct QUENTIER_EXPORT ResourceWithException
        {
            qevercloud::Resource resource;
            std::shared_ptr<QException> exception;
        };

        using UpdateSequenceNumbersByGuid = QHash<qevercloud::Guid, qint32>;

        QList<ResourceWithException> resourcesWhichFailedToDownload;
        QList<ResourceWithException> resourcesWhichFailedToProcess;

        UpdateSequenceNumbersByGuid processedResourceGuidsAndUsns;
        UpdateSequenceNumbersByGuid cancelledResourceGuidsAndUsns;
    };

    struct QUENTIER_EXPORT SyncResult
    {
        SyncState userAccountSyncState;
        QHash<qevercloud::Guid, SyncState> linkedNotebookSyncStates;

        DownloadNotesStatus userAccountDownloadNotesStatus;
        QHash<qevercloud::Guid, DownloadNotesStatus>
            linkedNotebookDownloadNotesStatuses;

        DownloadResourcesStatus userAccountDownloadResourcesStatus;
        QHash<qevercloud::Guid, DownloadResourcesStatus>
            linkedNotebookDownloadResourcesStatuses;

        SyncStats syncStats;
    };

public:
    virtual ~ISynchronizer() = default;

    /**
     * @return true if synchronization is being performed at the moment,
     *         false otherwise
     */
    [[nodiscard]] virtual bool isSyncRunning() const = 0;

    /**
     * @return options passed to ISynchronizer on the last sync
     */
    [[nodiscard]] virtual Options options() const = 0;

    [[nodiscard]] virtual QFuture<AuthResult> authenticateNewAccount() = 0;

    [[nodiscard]] virtual QFuture<AuthResult> authenticateAccount(
        Account account) = 0;

    [[nodiscard]] virtual QFuture<SyncResult> synchronizeAccount(
        Account account,
        ISyncConflictResolverPtr syncConflictResolver,
        local_storage::ILocalStoragePtr localStorage,
        Options options) = 0;

    [[nodiscard]] virtual QFuture<void> revokeAuthentication(
        qevercloud::UserID userId) = 0;

    [[nodiscard]] virtual ISyncEventsNotifier * notifier() const = 0;
};

} // namespace quentier::synchronization
