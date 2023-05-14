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

#include "AccountSynchronizerFactory.h"

#include <quentier/exception/InvalidArgument.h>

#include <synchronization/LinkedNotebookFinder.h>
#include <synchronization/NoteStoreFactory.h>
#include <synchronization/NoteStoreProvider.h>
#include <synchronization/sync_chunks/SyncChunksDownloader.h>

#include <qevercloud/DurableService.h>

namespace quentier::synchronization {

AccountSynchronizerFactory::AccountSynchronizerFactory(
    ISyncStateStoragePtr syncStateStorage,
    IAuthenticationInfoProviderPtr authenticationInfoProvider) :
    m_syncStateStorage{std::move(syncStateStorage)},
    m_authenticationInfoProvider{std::move(authenticationInfoProvider)}
{
    if (Q_UNLIKELY(!m_syncStateStorage)) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "AccountSynchronizerFactory ctor: sync state storage is null")}};
    }

    if (Q_UNLIKELY(!m_authenticationInfoProvider)) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "AccountSynchronizerFactory ctor: authentication info provider "
            "is null")}};
    }
}

IAccountSynchronizerPtr AccountSynchronizerFactory::createAccountSynchronizer(
    Account account,
    ISyncConflictResolverPtr syncConflictResolver,
    local_storage::ILocalStoragePtr localStorage,
    ISyncOptionsPtr options)
{
    if (Q_UNLIKELY(account.isEmpty())) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "AccountSynchronizerFactory: account is empty")}};
    }

    if (Q_UNLIKELY(!syncConflictResolver)) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "AccountSynchronizerFactory: sync conflict resolver is null")}};
    }

    if (Q_UNLIKELY(!localStorage)) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "AccountSynchronizerFactory: local storage is null")}};
    }

    if (Q_UNLIKELY(!options)) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "AccountSynchronizerFactory: sync options are null")}};
    }

    auto noteStoreFactory = std::make_shared<NoteStoreFactory>();
    auto linkedNotebookFinder =
        std::make_shared<LinkedNotebookFinder>(localStorage);

    auto noteStoreProvider = std::make_shared<NoteStoreProvider>(
        std::move(linkedNotebookFinder), m_authenticationInfoProvider,
        std::move(noteStoreFactory), account);

    auto syncChunksDownloader = std::make_shared<SyncChunksDownloader>(
        noteStoreProvider, qevercloud::newRetryPolicy());

    // TODO: implement further
    return nullptr;
}

} // namespace quentier::synchronization
