/*
 * Copyright 2024 Dmitry Ivanov
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

#include <qevercloud/services/IUserStore.h>

namespace quentier::synchronization::tests {

class FakeUserStore : public qevercloud::IUserStore
{
public:
    FakeUserStore(
        FakeUserStoreBackend * backend, QString userStoreUrl,
        qevercloud::IRequestContextPtr ctx,
        qevercloud::IRetryPolicyPtr retryPolicy);

public: // qevercloud::IUserStore
    [[nodiscard]] qevercloud::IRequestContextPtr defaultRequestContext()
        const noexcept override;

    void setDefaultRequestContext(
        qevercloud::IRequestContextPtr ctx) noexcept override;

    [[nodiscard]] QString userStoreUrl() const override;
    void setUserStoreUrl(QString url) override;

    [[nodiscard]] bool checkVersion(
        QString clientName,
        qint16 edamVersionMajor = qevercloud::EDAM_VERSION_MAJOR,
        qint16 edamVersionMinor = qevercloud::EDAM_VERSION_MINOR,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<bool> checkVersionAsync(
        QString clientName,
        qint16 edamVersionMajor = qevercloud::EDAM_VERSION_MAJOR,
        qint16 edamVersionMinor = qevercloud::EDAM_VERSION_MINOR,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] qevercloud::BootstrapInfo getBootstrapInfo(
        QString locale, qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<qevercloud::BootstrapInfo> getBootstrapInfoAsync(
        QString locale, qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] qevercloud::AuthenticationResult authenticateLongSession(
        QString username, QString password, QString consumerKey,
        QString consumerSecret, QString deviceIdentifier,
        QString deviceDescription, bool supportsTwoFactor,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<qevercloud::AuthenticationResult>
        authenticateLongSessionAsync(
            QString username, QString password, QString consumerKey,
            QString consumerSecret, QString deviceIdentifier,
            QString deviceDescription, bool supportsTwoFactor,
            qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] qevercloud::AuthenticationResult
        completeTwoFactorAuthentication(
            QString oneTimeCode, QString deviceIdentifier,
            QString deviceDescription,
            qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<qevercloud::AuthenticationResult>
        completeTwoFactorAuthenticationAsync(
            QString oneTimeCode, QString deviceIdentifier,
            QString deviceDescription,
            qevercloud::IRequestContextPtr ctx = {}) override;

    void revokeLongSession(qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<void> revokeLongSessionAsync(
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] qevercloud::AuthenticationResult authenticateToBusiness(
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<qevercloud::AuthenticationResult>
        authenticateToBusinessAsync(
            qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] qevercloud::User getUser(
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<qevercloud::User> getUserAsync(
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] qevercloud::PublicUserInfo getPublicUserInfo(
        QString username, qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<qevercloud::PublicUserInfo> getPublicUserInfoAsync(
        QString username, qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] qevercloud::UserUrls getUserUrls(
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<qevercloud::UserUrls> getUserUrlsAsync(
        qevercloud::IRequestContextPtr ctx = {}) override;

    void inviteToBusiness(
        QString emailAddress, qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<void> inviteToBusinessAsync(
        QString emailAddress, qevercloud::IRequestContextPtr ctx = {}) override;

    void removeFromBusiness(
        QString emailAddress, qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<void> removeFromBusinessAsync(
        QString emailAddress, qevercloud::IRequestContextPtr ctx = {}) override;

    void updateBusinessUserIdentifier(
        QString oldEmailAddress, QString newEmailAddress,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<void> updateBusinessUserIdentifierAsync(
        QString oldEmailAddress, QString newEmailAddress,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QList<qevercloud::UserProfile> listBusinessUsers(
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<QList<qevercloud::UserProfile>>
        listBusinessUsersAsync(
            qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QList<qevercloud::BusinessInvitation> listBusinessInvitations(
        bool includeRequestedInvitations,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<QList<qevercloud::BusinessInvitation>>
        listBusinessInvitationsAsync(
            bool includeRequestedInvitations,
            qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] qevercloud::AccountLimits getAccountLimits(
        qevercloud::ServiceLevel serviceLevel,
        qevercloud::IRequestContextPtr ctx = {}) override;

    [[nodiscard]] QFuture<qevercloud::AccountLimits> getAccountLimitsAsync(
        qevercloud::ServiceLevel serviceLevel,
        qevercloud::IRequestContextPtr ctx = {}) override;

private:
    void ensureRequestContext(qevercloud::IRequestContextPtr & ctx) const;

private:
    FakeNoteStoreBackend * m_backend;
    QString m_noteStoreUrl;
    qevercloud::IRequestContextPtr m_ctx;
    qevercloud::IRetryPolicyPtr m_retryPolicy;
};

} // namespace quentier::synchronization::tests
