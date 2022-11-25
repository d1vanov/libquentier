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

#include "ISender.h"

#include <quentier/local_storage/Fwd.h>
#include <quentier/synchronization/Fwd.h>
#include <quentier/threading/Fwd.h>
#include <quentier/types/Account.h>

#include <synchronization/Fwd.h>
#include <synchronization/types/Fwd.h>
#include <synchronization/types/SendStatus.h>

#include <qevercloud/Fwd.h>
#include <qevercloud/services/Fwd.h>
#include <qevercloud/types/LinkedNotebook.h>

#include <QHash>
#include <QSet>
#include <QtGlobal>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include <quentier/threading/Qt5Promise.h>
#endif

#include <memory>
#include <optional>

namespace quentier::synchronization {

class Sender final : public ISender, public std::enable_shared_from_this<Sender>
{
public:
    Sender(
        Account account,
        IAuthenticationInfoProviderPtr authenticationInfoProvider,
        ISyncStateStoragePtr syncStateStorage,
        qevercloud::IRequestContextPtr ctx, qevercloud::INoteStorePtr noteStore,
        local_storage::ILocalStoragePtr localStorage);

    [[nodiscard]] QFuture<Result> send(
        utility::cancelers::ICancelerPtr canceler,
        ICallbackWeakPtr callbackWeak) override;

private:
    [[nodiscard]] QFuture<Result> launchSend(
        const IAuthenticationInfo & authenticationInfo,
        SyncStateConstPtr lastSyncState,
        utility::cancelers::ICancelerPtr canceler,
        ICallbackWeakPtr callbackWeak);

    void cancel(QPromise<Result> & promise);

    struct SendContext
    {
        SyncStateConstPtr lastSyncState;
        std::shared_ptr<QPromise<Result>> promise;
        qevercloud::IRequestContextPtr ctx;
        utility::cancelers::ICancelerPtr canceler;
        ICallbackWeakPtr callbackWeak;

        // Linked notebook to which this DownloadContext belongs
        std::optional<qevercloud::LinkedNotebook> linkedNotebook;

        // Running result
        SendStatusPtr userOwnSendStatus;
        QHash<qevercloud::Guid, SendStatusPtr> linkedNotebookSendStatuses;

        // Local ids of new notebooks which could not be sent to Evernote
        // due to some error
        QSet<QString> failedToSendNewNotebookLocalIds;

        // Local ids of new tags which could not be sent to Evernote due to
        // some error
        QSet<QString> failedToSendNewTagLocalIds;

        // Local cache mapping notebook local ids to linked notebook guid
        // or lack thereof
        QHash<QString, std::optional<qevercloud::Guid>>
            notebookLocalIdsToLinkedNotebookGuids;

        threading::QMutexPtr sendStatusMutex;
    };

    using SendContextPtr = std::shared_ptr<SendContext>;

    [[nodiscard]] QFuture<void> processNotes(SendContextPtr sendContext) const;

    void sendNotes(
        SendContextPtr sendContext, QList<qevercloud::Note> notes,
        std::shared_ptr<QPromise<void>> promise) const;

    void processNote(
        const SendContextPtr & sendContext, qevercloud::Note note,
        const std::shared_ptr<QPromise<void>> & promise) const;

    void processNoteFailure(
        const SendContextPtr & sendContext, qevercloud::Note note,
        const QException & e,
        const std::shared_ptr<QPromise<void>> & promise) const;

    [[nodiscard]] QFuture<void> processTags(SendContextPtr sendContext) const;

    void sendTags(
        SendContextPtr sendContext, QList<qevercloud::Tag> tags,
        std::shared_ptr<QPromise<void>> promise) const;

    void processTag(
        const SendContextPtr & sendContext, qevercloud::Tag tag,
        const std::shared_ptr<QPromise<void>> & promise) const;

    static void processTagFailure(
        const SendContextPtr & sendContext, qevercloud::Tag tag,
        const QException & e, const std::shared_ptr<QPromise<void>> & promise);

    [[nodiscard]] QFuture<void> processNotebooks(
        SendContextPtr sendContext) const;

    void sendNotebooks(
        SendContextPtr sendContext,
        const QList<qevercloud::Notebook> & notebooks,
        std::shared_ptr<QPromise<void>> promise) const;

    void processNotebook(
        const SendContextPtr & sendContext, qevercloud::Notebook notebook,
        const std::shared_ptr<QPromise<void>> & promise) const;

    static void processNotebookFailure(
        const SendContextPtr & sendContext, qevercloud::Notebook notebook,
        const QException & e, const std::shared_ptr<QPromise<void>> & promise);

    [[nodiscard]] QFuture<void> processSavedSearches(
        SendContextPtr sendContext) const;

    void sendSavedSearches(
        SendContextPtr sendContext,
        const QList<qevercloud::SavedSearch> & savedSearches,
        std::shared_ptr<QPromise<void>> promise) const;

    void processSavedSearch(
        const SendContextPtr & sendContext, qevercloud::SavedSearch savedSearch,
        const std::shared_ptr<QPromise<void>> & promise) const;

    static void processSavedSearchFailure(
        const SendContextPtr & sendContext, qevercloud::SavedSearch savedSearch,
        const QException & e, const std::shared_ptr<QPromise<void>> & promise);

    [[nodiscard]] static SendStatusPtr sendStatus(
        const SendContextPtr & sendContext,
        const std::optional<qevercloud::Guid> & linkedNotebooKGuid);

    static void sendUpdate(
        const SendContextPtr & sendContext, const SendStatusPtr & sendStatus,
        const std::optional<qevercloud::Guid> & linkedNotebookGuid);

private:
    const Account m_account;
    const IAuthenticationInfoProviderPtr m_authenticationInfoProvider;
    const ISyncStateStoragePtr m_syncStateStorage;
    const qevercloud::IRequestContextPtr m_ctx;
    const qevercloud::INoteStorePtr m_noteStore;
    const local_storage::ILocalStoragePtr m_localStorage;
};

} // namespace quentier::synchronization
