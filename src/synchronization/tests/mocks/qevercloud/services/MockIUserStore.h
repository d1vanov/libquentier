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

#include <qevercloud/services/IUserStore.h>

#include <gmock/gmock.h>

namespace quentier::synchronization::tests::mocks::qevercloud {

class MockIUserStore : public ::qevercloud::IUserStore
{
public:
    MOCK_METHOD(QString, userStoreUrl, (), (const, override));
    MOCK_METHOD(void, setUserStoreUrl, (QString url), (override));

    MOCK_METHOD(
        bool, checkVersion,
        (QString clientName, qint16 edamVersionMajor, qint16 edamVersionMinor,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<bool>, checkVersionAsync,
        (QString clientName, qint16 edamVersionMajor, qint16 edamVersionMinor,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        ::qevercloud::BootstrapInfo, getBootstrapInfo,
        (QString locale, ::qevercloud::IRequestContextPtr ctx), (override));

    MOCK_METHOD(
        QFuture<::qevercloud::BootstrapInfo>, getBootstrapInfoAsync,
        (QString locale, ::qevercloud::IRequestContextPtr ctx), (override));

    MOCK_METHOD(
        ::qevercloud::AuthenticationResult, authenticateLongSession,
        (QString username, QString password, QString consumerKey,
         QString consumerSecret, QString deviceIdentifier,
         QString deviceDescription, bool supportsTwoFactor,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<::qevercloud::AuthenticationResult>,
        authenticateLongSessionAsync,
        (QString username, QString password, QString consumerKey,
         QString consumerSecret, QString deviceIdentifier,
         QString deviceDescription, bool supportsTwoFactor,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        ::qevercloud::AuthenticationResult, completeTwoFactorAuthentication,
        (QString oneTimeCode, QString deviceIdentifier,
         QString deviceDescription, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<::qevercloud::AuthenticationResult>,
        completeTwoFactorAuthenticationAsync,
        (QString oneTimeCode, QString deviceIdentifier,
         QString deviceDescription, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        void, revokeLongSession, (::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<void>, revokeLongSessionAsync,
        (::qevercloud::IRequestContextPtr ctx), (override));

    MOCK_METHOD(
        ::qevercloud::AuthenticationResult, authenticateToBusiness,
        (::qevercloud::IRequestContextPtr ctx), (override));

    MOCK_METHOD(
        QFuture<::qevercloud::AuthenticationResult>,
        authenticateToBusinessAsync, (::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        ::qevercloud::User, getUser, (::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<::qevercloud::User>, getUserAsync,
        (::qevercloud::IRequestContextPtr ctx), (override));

    MOCK_METHOD(
        ::qevercloud::PublicUserInfo, getPublicUserInfo,
        (QString username, ::qevercloud::IRequestContextPtr ctx), (override));

    MOCK_METHOD(
        QFuture<::qevercloud::PublicUserInfo>, getPublicUserInfoAsync,
        (QString username, ::qevercloud::IRequestContextPtr ctx), (override));

    MOCK_METHOD(
        ::qevercloud::UserUrls, getUserUrls,
        (::qevercloud::IRequestContextPtr ctx), (override));

    MOCK_METHOD(
        QFuture<::qevercloud::UserUrls>, getUserUrlsAsync,
        (::qevercloud::IRequestContextPtr ctx), (override));

    MOCK_METHOD(
        void, inviteToBusiness,
        (QString emailAddress, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<void>, inviteToBusinessAsync,
        (QString emailAddress, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        void, removeFromBusiness,
        (QString emailAddress, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<void>, removeFromBusinessAsync,
        (QString emailAddress, ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        void, updateBusinessUserIdentifier,
        (QString oldEmailAddress, QString newEmailAddress,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<void>, updateBusinessUserIdentifierAsync,
        (QString oldEmailAddress, QString newEmailAddress,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QList<::qevercloud::UserProfile>, listBusinessUsers,
        (::qevercloud::IRequestContextPtr ctx), (override));

    MOCK_METHOD(
        QFuture<QList<::qevercloud::UserProfile>>, listBusinessUsersAsync,
        (::qevercloud::IRequestContextPtr ctx), (override));

    MOCK_METHOD(
        QList<::qevercloud::BusinessInvitation>, listBusinessInvitations,
        (bool includeRequestedInvitations,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<QList<::qevercloud::BusinessInvitation>>,
        listBusinessInvitationsAsync,
        (bool includeRequestedInvitations,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        ::qevercloud::AccountLimits, getAccountLimits,
        (::qevercloud::ServiceLevel serviceLevel,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<::qevercloud::AccountLimits>, getAccountLimitsAsync,
        (::qevercloud::ServiceLevel serviceLevel,
         ::qevercloud::IRequestContextPtr ctx),
        (override));
};

} // namespace quentier::synchronization::tests::mocks::qevercloud
