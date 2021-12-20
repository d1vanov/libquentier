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

#include <qevercloud/types/TypeAliases.h>

#include <QDir>
#include <QFuture>
#include <QList>
#include <QNetworkCookie>

#include <optional>

namespace quentier {

class Account;
class ErrorString;

} // namespace quentier

namespace quentier::synchronization {

class QUENTIER_EXPORT ISynchronizer
{
public:
    struct Options
    {
        bool downloadNoteThumbnails = false;
        std::optional<QDir> inkNoteImagesStorageDir;
    };

    struct AuthResult
    {
        qevercloud::UserID userId = 0;
        QString authToken;
        qevercloud::Timestamp authTokenExpirationTime = 0;
        QString shardId;
        QString noteStoreUrl;
        QString webApiUrlPrefix;
        QList<QNetworkCookie> userStoreCookies;
    };

    struct SyncStats
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

    [[nodiscard]] virtual QFuture<SyncStats> synchronizeAccount(
        Account account,
        ISyncConflictResolverPtr syncConflictResolver,
        local_storage::ILocalStoragePtr localStorage,
        Options options) = 0;

    [[nodiscard]] virtual QFuture<void> revokeAuthentication(
        qevercloud::UserID userId) = 0;

    [[nodiscard]] virtual ISyncEventsNotifier * notifier() const = 0;
};

} // namespace quentier::synchronization
