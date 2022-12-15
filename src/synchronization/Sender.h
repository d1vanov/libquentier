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
#include <qevercloud/types/TypeAliases.h>

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
        Account account, local_storage::ILocalStoragePtr localStorage,
        ISyncStateStoragePtr syncStateStorage,
        INoteStoreProviderPtr noteStoreProvider,
        qevercloud::IRequestContextPtr ctx = {},
        qevercloud::IRetryPolicyPtr retryPolicy = {});

    [[nodiscard]] QFuture<Result> send(
        utility::cancelers::ICancelerPtr canceler,
        ICallbackWeakPtr callbackWeak) override;

private:
    struct SendContext
    {
        SyncStatePtr lastSyncState;
        std::shared_ptr<QPromise<Result>> promise;
        utility::cancelers::ICancelerPtr canceler;
        ICallbackWeakPtr callbackWeak;

        // Linked notebook to which this DownloadContext belongs
        std::optional<qevercloud::LinkedNotebook> linkedNotebook;

        bool shouldRepeatIncrementalSync = false;

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

        // Local cache mapping local ids of newly created tags to their guids
        // so that this guid can be set as parent guid onto the child tags
        QHash<QString, qevercloud::Guid> newTagLocalIdsToGuids;

        threading::QMutexPtr sendStatusMutex;
    };

    using SendContextPtr = std::shared_ptr<SendContext>;

    [[nodiscard]] QFuture<void> processNotes(SendContextPtr sendContext) const;

    void sendNotes(
        SendContextPtr sendContext, QList<qevercloud::Note> notes,
        std::shared_ptr<QPromise<void>> promise) const;

    void sendNote(
        SendContextPtr sendContext, qevercloud::Note note,
        bool containsFailedToSendTags, QFuture<void> previousNoteFuture,
        const std::shared_ptr<QPromise<qevercloud::Note>> & notePromise) const;

    void sendNoteImpl(
        qevercloud::Note note, bool containsFailedToSendTags,
        const qevercloud::INoteStorePtr & noteStore,
        const std::shared_ptr<QPromise<qevercloud::Note>> & notePromise) const;

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

    void sendTag(
        SendContextPtr sendContext, qevercloud::Tag tag,
        QFuture<void> previousTagFuture,
        const std::shared_ptr<QPromise<qevercloud::Tag>> & tagPromise) const;

    void sendTagImpl(
        SendContextPtr sendContext, qevercloud::Tag tag,
        const qevercloud::INoteStorePtr & noteStore,
        const std::shared_ptr<QPromise<qevercloud::Tag>> & tagPromise) const;

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

    void sendNotebook(
        SendContextPtr sendContext, qevercloud::Notebook notebook,
        QFuture<void> previousNotebookFuture,
        const std::shared_ptr<QPromise<qevercloud::Notebook>> & notebookPromise)
        const;

    void sendNotebookImpl(
        qevercloud::Notebook notebook,
        const qevercloud::INoteStorePtr & noteStore,
        const std::shared_ptr<QPromise<qevercloud::Notebook>> & notebookPromise)
        const;

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

    void sendSavedSearch(
        SendContextPtr sendContext, qevercloud::SavedSearch savedSearch,
        QFuture<void> previousSavedSearchFuture,
        const std::shared_ptr<QPromise<qevercloud::SavedSearch>> &
            savedSearchPromise) const;

    void sendSavedSearchImpl(
        qevercloud::SavedSearch savedSearch,
        const qevercloud::INoteStorePtr & noteStore,
        const std::shared_ptr<QPromise<qevercloud::SavedSearch>> &
            savedSearchPromise) const;

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

    [[nodiscard]] std::optional<qint32> lastUpdateCount(
        const SendContext & sendContext,
        const std::optional<qevercloud::Guid> & linkedNotebookGuid = {}) const;

    void updateLastUpdateCount(
        qint32 updateCount, SendContext & sendContext,
        const std::optional<qevercloud::Guid> & linkedNotebookGuid = {}) const;

    void checkUpdateSequenceNumber(
        qint32 updateSequenceNumber, SendContext & sendContext,
        const std::optional<qevercloud::Guid> & linkedNotebookGuid = {}) const;

private:
    const Account m_account;
    const local_storage::ILocalStoragePtr m_localStorage;
    const ISyncStateStoragePtr m_syncStateStorage;
    const INoteStoreProviderPtr m_noteStoreProvider;
    const qevercloud::IRequestContextPtr m_ctx;
    const qevercloud::IRetryPolicyPtr m_retryPolicy;
};

} // namespace quentier::synchronization
