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

#include "Fwd.h"
#include "INoteStoreProvider.h"

#include <quentier/local_storage/Fwd.h>
#include <quentier/types/Account.h>

#include <qevercloud/Fwd.h>
#include <qevercloud/types/LinkedNotebook.h>

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
        local_storage::ILocalStoragePtr localStorage,
        IAuthenticationInfoProviderPtr authenticationInfoProvider,
        INoteStoreFactoryPtr noteStoreFactory,
        Account account);

    [[nodiscard]] QFuture<qevercloud::INoteStorePtr> noteStore(
        QString notebookLocalId,
        qevercloud::IRequestContextPtr ctx = {},
        qevercloud::IRetryPolicyPtr retryPolicy = {}) override;

private:
    [[nodiscard]] QFuture<std::optional<qevercloud::LinkedNotebook>>
        findLinkedNotebookByNotebookLocalId(
            const QString & notebookLocalId) const;

private:
    const local_storage::ILocalStoragePtr m_localStorage;
    const IAuthenticationInfoProviderPtr m_authenticationInfoProvider;
    const INoteStoreFactoryPtr m_noteStoreFactory;
    const Account m_account;

    QHash<QString, QFuture<std::optional<qevercloud::LinkedNotebook>>>
        m_linkedNotebooksByNotebookLocalId;

    QMutex m_mutex;
};

} // namespace quentier::synchronization
