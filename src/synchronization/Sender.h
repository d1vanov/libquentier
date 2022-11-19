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
#include <quentier/types/Account.h>

#include <synchronization/Fwd.h>
#include <synchronization/types/Fwd.h>
#include <synchronization/types/SendStatus.h>

#include <qevercloud/Fwd.h>
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
        qevercloud::IRequestContextPtr ctx,
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
        SendStatusPtr sendStatus;
    };

    using SendContextPtr = std::shared_ptr<SendContext>;

    struct SendTagsResult
    {
        struct LocalResult
        {
            QSet<QString> successfullySentTagLocalIds;
            QHash<QString, SendStatus::TagWithException>
                failedToSendTagsByLocalId;
        };

        LocalResult userOwnResult;
        QHash<qevercloud::Guid, LocalResult> linkedNotebookResults;
    };

    [[nodiscard]] QFuture<SendTagsResult> processTags(
        SendContextPtr sendContext) const;

    void sendTags(
        SendContextPtr sendContext, QList<qevercloud::Tag> tags,
        std::shared_ptr<QPromise<SendTagsResult>> promise) const;

    struct SendNotebooksResult
    {
        struct LocalResult
        {
            QSet<QString> successfullySentNotebookLocalIds;
            QHash<QString, SendStatus::NotebookWithException>
                failedToSendNotebooksByLocalId;
        };

        LocalResult userOwnResult;
        QHash<qevercloud::Guid, LocalResult> linkedNotebookResults;
    };

    [[nodiscard]] QFuture<SendNotebooksResult> processNotebooks(
        SendContextPtr sendContext) const;

    void sendNotebooks(
        SendContextPtr sendContext, QList<qevercloud::Notebook> notebooks,
        std::shared_ptr<QPromise<SendNotebooksResult>> promise) const;

    struct SendSavedSearchesResult
    {
        QSet<QString> successfullySentSavedSearchLocalIds;
        QList<SendStatus::SavedSearchWithException> failedToSendSavedSearches;
    };

    [[nodiscard]] QFuture<SendSavedSearchesResult> processSavedSearches(
        SendContextPtr sendContext) const;

    void sendSavedSearches(
        SendContextPtr sendContext,
        QList<qevercloud::SavedSearch> savedSearches,
        std::shared_ptr<QPromise<SendSavedSearchesResult>> promise) const;

private:
    const Account m_account;
    const IAuthenticationInfoProviderPtr m_authenticationInfoProvider;
    const ISyncStateStoragePtr m_syncStateStorage;
    const qevercloud::IRequestContextPtr m_ctx;
    const local_storage::ILocalStoragePtr m_localStorage;
};

} // namespace quentier::synchronization
