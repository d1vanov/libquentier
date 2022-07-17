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

#include <quentier/synchronization/Fwd.h>
#include <quentier/utility/Fwd.h>

#include <synchronization/IAuthenticationInfoProvider.h>

namespace quentier::synchronization {

class AuthenticationInfoProvider final : public IAuthenticationInfoProvider
{
public:
    AuthenticationInfoProvider(
        IAuthenticatorPtr authenticator,
        IKeychainServicePtr keychainService);

public:
    // IAuthenticationInfoProvider
    [[nodiscard]] QFuture<IAuthenticationInfoPtr>
        authenticateNewAccount() override;

    [[nodiscard]] QFuture<IAuthenticationInfoPtr> authenticateAccount(
        Account account, Mode mode = Mode::Cache) override;

    [[nodiscard]] QFuture<IAuthenticationInfoPtr>
        authenticateToLinkedNotebook(
            Account account, qevercloud::Guid linkedNotebookGuid,
            QString sharedNotebookGlobalId, QString noteStoreUrl,
            Mode mode = Mode::Cache) override;

private:
    const IAuthenticatorPtr m_authenticator;
    const IKeychainServicePtr m_keychainService;
};

} // namespace quentier::synchronization
