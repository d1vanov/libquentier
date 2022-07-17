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

#include "AuthenticationInfoProvider.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/RuntimeError.h>
#include <quentier/synchronization/IAuthenticator.h>
#include <quentier/threading/Future.h>

namespace quentier::synchronization {

AuthenticationInfoProvider::AuthenticationInfoProvider(
    IAuthenticatorPtr authenticator,
    IKeychainServicePtr keychainService) :
    m_authenticator{std::move(authenticator)},
    m_keychainService{std::move(keychainService)}
{
    if (Q_UNLIKELY(!m_authenticator)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::AuthenticationInfoProvider",
            "AuthenticationInfoProvider ctor: authenticator is null")}};
    }

    if (Q_UNLIKELY(!m_keychainService)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::AuthenticationInfoProvider",
            "AuthenticationInfoProvider ctor: keychain service is null")}};
    }
}

QFuture<IAuthenticationInfoPtr>
    AuthenticationInfoProvider::authenticateNewAccount()
{
    return m_authenticator->authenticateNewAccount();
}

QFuture<IAuthenticationInfoPtr> AuthenticationInfoProvider::authenticateAccount(
    Account account, Mode mode)
{
    // TODO: implement
    Q_UNUSED(account)
    Q_UNUSED(mode)
    return threading::makeExceptionalFuture<
        IAuthenticationInfoPtr>(RuntimeError{ErrorString{QT_TRANSLATE_NOOP(
        "synchronization::AuthenticationInfoProvider",
        "AuthenticationInfoProvider::authenticateAccount: not implemented")}});
}

QFuture<IAuthenticationInfoPtr>
    AuthenticationInfoProvider::authenticateToLinkedNotebook(
        Account account, qevercloud::Guid linkedNotebookGuid,
        QString sharedNotebookGlobalId, QString noteStoreUrl, Mode mode)
{
    // TODO: implement
    Q_UNUSED(account)
    Q_UNUSED(linkedNotebookGuid)
    Q_UNUSED(sharedNotebookGlobalId)
    Q_UNUSED(noteStoreUrl)
    Q_UNUSED(mode)
    return threading::makeExceptionalFuture<IAuthenticationInfoPtr>(
        RuntimeError{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::AuthenticationInfoProvider",
            "AuthenticationInfoProvider::authenticateToLinkedNotebook: "
            "not implemented")}});
}

} // namespace quentier::synchronization
