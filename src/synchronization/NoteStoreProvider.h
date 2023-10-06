/*
 * Copyright 2022-2023 Dmitry Ivanov
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
#include "INoteStoreProvider.h"

#include <quentier/types/Account.h>

#include <qevercloud/Fwd.h>
#include <qevercloud/types/LinkedNotebook.h>
#include <qevercloud/types/TypeAliases.h>

#include <QHash>
#include <QMutex>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include <quentier/threading/Qt5Promise.h>
#endif

#include <memory>

namespace quentier::synchronization {

class NoteStoreProvider final :
    public INoteStoreProvider,
    public std::enable_shared_from_this<NoteStoreProvider>
{
public:
    NoteStoreProvider(
        ILinkedNotebookFinderPtr linkedNotebookFinder,
        INotebookFinderPtr notebookFinder,
        IAuthenticationInfoProviderPtr authenticationInfoProvider,
        INoteStoreFactoryPtr noteStoreFactory, Account account);

    [[nodiscard]] QFuture<qevercloud::INoteStorePtr>
        noteStoreForNotebookLocalId(
            QString notebookLocalId, qevercloud::IRequestContextPtr ctx = {},
            qevercloud::IRetryPolicyPtr retryPolicy = {}) override;

    [[nodiscard]] QFuture<qevercloud::INoteStorePtr>
        noteStoreForNotebookGuid(
            qevercloud::Guid notebookGuid,
            qevercloud::IRequestContextPtr ctx = {},
            qevercloud::IRetryPolicyPtr retryPolicy = {}) override;

    [[nodiscard]] QFuture<qevercloud::INoteStorePtr> noteStoreForNoteLocalId(
        QString noteLocalId, qevercloud::IRequestContextPtr ctx = {},
        qevercloud::IRetryPolicyPtr retryPolicy = {}) override;

    [[nodiscard]] QFuture<qevercloud::INoteStorePtr> userOwnNoteStore(
        qevercloud::IRequestContextPtr ctx = {},
        qevercloud::IRetryPolicyPtr retryPolicy = {}) override;

    [[nodiscard]] QFuture<qevercloud::INoteStorePtr> linkedNotebookNoteStore(
        qevercloud::Guid linkedNotebookGuid,
        qevercloud::IRequestContextPtr ctx = {},
        qevercloud::IRetryPolicyPtr retryPolicy = {}) override;

    void clearCaches() override;

private:
    [[nodiscard]] qevercloud::INoteStorePtr cachedUserOwnNoteStore(
        const qevercloud::IRequestContextPtr & ctx);

    [[nodiscard]] qevercloud::INoteStorePtr cachedLinkedNotebookNoteStore(
        const qevercloud::LinkedNotebook & linkedNotebook,
        const qevercloud::IRequestContextPtr & ctx);

    void createNoteStore(
        const std::optional<qevercloud::LinkedNotebook> & linkedNotebook,
        qevercloud::IRequestContextPtr ctx,
        qevercloud::IRetryPolicyPtr retryPolicy,
        const std::shared_ptr<QPromise<qevercloud::INoteStorePtr>> & promise);

private:
    const ILinkedNotebookFinderPtr m_linkedNotebookFinder;
    const INotebookFinderPtr m_notebookFinder;
    const IAuthenticationInfoProviderPtr m_authenticationInfoProvider;
    const INoteStoreFactoryPtr m_noteStoreFactory;
    const Account m_account;

    struct NoteStoreData
    {
        qevercloud::INoteStorePtr m_noteStore;
        qevercloud::Timestamp m_authTokenExpirationTime = 0;
    };

    NoteStoreData m_userOwnNoteStoreData;
    QMutex m_userOwnNoteStoreDataMutex;

    QHash<qevercloud::Guid, NoteStoreData> m_linkedNotebooksNoteStoreData;
    QMutex m_linkedNotebooksNoteStoreDataMutex;
};

} // namespace quentier::synchronization
