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
#include <quentier/logging/QuentierLogger.h>
#include <quentier/synchronization/IAuthenticator.h>
#include <quentier/synchronization/types/IAuthenticationInfo.h>
#include <quentier/threading/Future.h>
#include <quentier/threading/TrackedTask.h>
#include <quentier/utility/ApplicationSettings.h>
#include <quentier/utility/DateTime.h>
#include <quentier/utility/IKeychainService.h>

#include <synchronization/types/AuthenticationInfo.h>
#include <synchronization/IUserInfoProvider.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QTimeZone>

namespace quentier::synchronization {

namespace {

const QString gAuthTokenKeychainKeyPart = QStringLiteral("auth_token");
const QString gShardIdKeychainKeyPart = QStringLiteral("shard_id");

const QString gSynchronizationPersistence =
    QStringLiteral("SynchronizationPersistence");

const QString gNoteStoreUrlKey = QStringLiteral("NoteStoreUrl");
const QString gWebApiUrlPrefixKey = QStringLiteral("WebApiUrlPrefix");
const QString gUserStoreCookieKey = QStringLiteral("UserStoreCookie");
const QString gExpirationTimestampKey = QStringLiteral("ExpirationTimestamp");

const QString gAuthenticationTimestampKey =
    QStringLiteral("AuthenticationTimestamp");

[[nodiscard]] Account::EvernoteAccountType toEvernoteAccountType(
    const qevercloud::ServiceLevel serviceLevel) noexcept
{
    switch (serviceLevel) {
    case qevercloud::ServiceLevel::BASIC:
        return Account::EvernoteAccountType::Free;
    case qevercloud::ServiceLevel::PLUS:
        return Account::EvernoteAccountType::Plus;
    case qevercloud::ServiceLevel::PREMIUM:
        return Account::EvernoteAccountType::Premium;
    case qevercloud::ServiceLevel::BUSINESS:
        return Account::EvernoteAccountType::Business;
    }

    Q_ASSERT(false); // Unreachable code
    return Account::EvernoteAccountType::Free;
}

} // namespace

AuthenticationInfoProvider::AuthenticationInfoProvider(
    IAuthenticatorPtr authenticator, IKeychainServicePtr keychainService,
    IUserInfoProviderPtr userInfoProvider, QString host) :
    m_authenticator{std::move(authenticator)},
    m_keychainService{std::move(keychainService)},
    m_userInfoProvider{std::move(userInfoProvider)}, m_host{std::move(host)}
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

    if (Q_UNLIKELY(!m_userInfoProvider)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::AuthenticationInfoProvider",
            "AuthenticationInfoProvider ctor: user info provider is null")}};
    }

    if (Q_UNLIKELY(m_host.isEmpty())) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::AuthenticationInfoProvider",
            "AuthenticationInfoProvider ctor: host is empty")}};
    }
}

QFuture<IAuthenticationInfoPtr>
    AuthenticationInfoProvider::authenticateNewAccount()
{
    auto promise = std::make_shared<QPromise<IAuthenticationInfoPtr>>();
    auto future = promise->future();

    promise->start();

    auto authResultFuture = m_authenticator->authenticateNewAccount();

    const auto selfWeak = weak_from_this();

    threading::thenOrFailed(
        std::move(authResultFuture), promise,
        threading::TrackedTask{
            selfWeak,
            [this, promise,
             selfWeak](IAuthenticationInfoPtr authenticationInfo) mutable {
                Q_ASSERT(authenticationInfo);

                auto accountFuture = findAccountForUserId(
                    authenticationInfo->userId(),
                    authenticationInfo->shardId());

                auto accountThenFuture = threading::then(
                    std::move(accountFuture),
                    threading::TrackedTask{
                        selfWeak,
                        [this, promise,
                         authenticationInfo](Account account) mutable {
                            auto storeAuthInfoFuture = storeAuthenticationInfo(
                                authenticationInfo, std::move(account));

                            auto storeAuthInfoThenFuture = threading::then(
                                std::move(storeAuthInfoFuture),
                                [promise, authenticationInfo]() mutable {
                                    promise->addResult(
                                        std::move(authenticationInfo));
                                    promise->finish();
                                });

                            threading::onFailed(
                                std::move(storeAuthInfoThenFuture),
                                [promise,
                                 authenticationInfo =
                                     std::move(authenticationInfo)](
                                    const QException & e) mutable {
                                    QNWARNING(
                                        "synchronization::"
                                        "AuthenticationInfoProvider",
                                        "Failed to store authentication info: "
                                            << e.what());

                                    // Even though we failed to save the
                                    // authentication info locally, we still got
                                    // it from Evernote so it should be returned
                                    // to the original caller.
                                    promise->addResult(
                                        std::move(authenticationInfo));
                                    promise->finish();
                                });
                        }});

                threading::onFailed(
                    std::move(accountThenFuture),
                    [promise = std::move(promise),
                     authenticationInfo = std::move(authenticationInfo)](
                        const QException & e) mutable {
                        QNWARNING(
                            "synchronization::AuthenticationInfoProvider",
                            "Failed to find account for user id: "
                                << e.what() << ", user id = "
                                << authenticationInfo->userId());

                        // Even though we failed to find account info for
                        // the authenticated user and thus failed to save
                        // the authentication info locally, we still got the
                        // info from Evernote and should return it to the
                        // original caller.
                        promise->addResult(std::move(authenticationInfo));
                        promise->finish();
                    });
            }});

    return future;
}

QFuture<IAuthenticationInfoPtr> AuthenticationInfoProvider::authenticateAccount(
    Account account, Mode mode)
{
    if (Q_UNLIKELY(account.type() != Account::Type::Evernote)) {
        return threading::makeExceptionalFuture<IAuthenticationInfoPtr>(
            RuntimeError{ErrorString{QT_TRANSLATE_NOOP(
                "synchronization::AuthenticationInfoProvider",
                "Detected attempt to authenticate non-Evernote account")}});
    }

    auto promise = std::make_shared<QPromise<IAuthenticationInfoPtr>>();
    auto future = promise->future();

    promise->start();

    if (mode == Mode::NoCache) {
        authenticateAccountWithoutCache(std::move(account), promise);
        return future;
    }

    auto authenticationInfo = readAuthenticationInfoPart(account);
    if (!authenticationInfo) {
        authenticateAccountWithoutCache(std::move(account), promise);
        return future;
    }

    // TODO: implement further

    return future;
}

QFuture<IAuthenticationInfoPtr>
    AuthenticationInfoProvider::authenticateToLinkedNotebook(
        Account account, qevercloud::Guid linkedNotebookGuid, // NOLINT
        QString sharedNotebookGlobalId, QString noteStoreUrl,
        Mode mode) // NOLINT
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

void AuthenticationInfoProvider::authenticateAccountWithoutCache(
    Account account,
    const std::shared_ptr<QPromise<IAuthenticationInfoPtr>> & promise)
{
    const auto selfWeak = weak_from_this();

    auto authResultFuture = m_authenticator->authenticateAccount(account);

    threading::thenOrFailed(
        std::move(authResultFuture), promise,
        threading::TrackedTask{
            selfWeak,
            [this, promise, account = std::move(account),
             selfWeak](IAuthenticationInfoPtr authenticationInfo) mutable {
                Q_ASSERT(authenticationInfo);
                Q_ASSERT(account.id() == authenticationInfo->userId());

                auto storeAuthInfoFuture = storeAuthenticationInfo(
                    authenticationInfo, std::move(account));

                auto storeAuthInfoThenFuture = threading::then(
                    std::move(storeAuthInfoFuture),
                    [promise, authenticationInfo]() mutable {
                        promise->addResult(std::move(authenticationInfo));
                        promise->finish();
                    });

                threading::onFailed(
                    std::move(storeAuthInfoThenFuture),
                    [promise,
                     authenticationInfo = std::move(authenticationInfo)](
                        const QException & e) mutable {
                        QNWARNING(
                            "synchronization::"
                            "AuthenticationInfoProvider",
                            "Failed to store authentication info: "
                                << e.what());

                        // Even though we failed to save the
                        // authentication info locally, we still got
                        // it from Evernote so it should be returned
                        // to the original caller.
                        promise->addResult(std::move(authenticationInfo));
                        promise->finish();
                    });
            }});
}

std::shared_ptr<AuthenticationInfo>
    AuthenticationInfoProvider::readAuthenticationInfoPart(
        const Account & account) const
{
    ApplicationSettings settings{
        account, gSynchronizationPersistence};

    const QString keyGroup = QString::fromUtf8("Authentication/%1/%2/")
                                 .arg(m_host, QString::number(account.id()));

    settings.beginGroup(keyGroup);
    ApplicationSettings::GroupCloser groupCloser{settings};

    if (!settings.contains(gAuthenticationTimestampKey) ||
        !settings.contains(gExpirationTimestampKey) ||
        !settings.contains(gNoteStoreUrlKey) ||
        !settings.contains(gWebApiUrlPrefixKey))
    {
        return nullptr;
    }

    auto authenticationInfo = std::make_shared<AuthenticationInfo>();

    const QVariant authenticationTimestamp =
        settings.value(gAuthenticationTimestampKey);

    QDateTime authenticationDateTime;
    if (!authenticationTimestamp.isNull()) {
        bool conversionResult = false;

        const qint64 authenticationTimestampInt =
            authenticationTimestamp.toLongLong(&conversionResult);

        if (!conversionResult) {
            QNWARNING(
                "synchronization::AuthenticationInfoProvider",
                "Stored authentication timestamp is not a valid integer: "
                    << authenticationTimestamp);
            return nullptr;
        }

        authenticationDateTime =
            QDateTime::fromMSecsSinceEpoch(authenticationTimestampInt);
    }

    if (!authenticationDateTime.isValid()) {
        QNDEBUG(
            "synchronization::AuthenticationInfoProvider",
            "Authentication timestamp is not valid: "
                << authenticationTimestamp);
        return nullptr;
    }

    if (authenticationDateTime <
        QDateTime(QDate(2020, 4, 22), QTime(0, 0), QTimeZone::utc()))
    {
        QNINFO(
            "synchronization::AuthenticationInfoProvider",
            "Last authentication was performed before Evernote introduced a "
                << "bug which requires to set a particular cookie into API "
                << "calls which was received during OAuth. Forcing new OAuth");
        return nullptr;
    }

    authenticationInfo->m_authenticationTime =
        authenticationDateTime.toMSecsSinceEpoch();

    const QVariant tokenExpirationValue =
        settings.value(gExpirationTimestampKey);

    if (!tokenExpirationValue.isNull()) {
        bool conversionResult = false;

        const qevercloud::Timestamp tokenExpirationTimestamp =
            tokenExpirationValue.toLongLong(&conversionResult);

        if (!conversionResult) {
            QNWARNING(
                "synchronization::AuthenticationInfoProvider",
                "Stored authentication token expiration timestamp is not a "
                    << "valid integer: " << tokenExpirationValue);
            return nullptr;
        }

        authenticationInfo->m_authTokenExpirationTime =
            tokenExpirationTimestamp;
    }

    // TODO: continue from here
    return authenticationInfo;
}

QFuture<Account> AuthenticationInfoProvider::findAccountForUserId(
    const qevercloud::UserID userId, QString shardId)
{
    auto promise = std::make_shared<QPromise<Account>>();
    auto future = promise->future();

    promise->start();

    auto userFuture = m_userInfoProvider->userInfo(userId);

    threading::thenOrFailed(
        std::move(userFuture), promise,
        [promise, userId, host = m_host,
         shardId = std::move(shardId)](const qevercloud::User & user) mutable {
            QNDEBUG(
                "synchronization::AuthenticationInfoProvider",
                "Received user for id " << userId << ": " << user);

            Q_ASSERT(user.id() && *user.id() == userId);

            if (Q_UNLIKELY(!user.username())) {
                QNWARNING(
                    "synchronization::AuthenticationInfoProvider",
                    "User for id " << userId << " has no username: " << user);
                promise->setException(
                    RuntimeError{ErrorString{QT_TRANSLATE_NOOP(
                        "synchronization::AuthenticationInfoProvider",
                        "Authenticated user has no username")}});
                promise->finish();
                return;
            }

            Account account{
                *user.username(),
                Account::Type::Evernote,
                userId,
                user.serviceLevel()
                    ? toEvernoteAccountType(*user.serviceLevel())
                    : Account::EvernoteAccountType::Free,
                std::move(host),
                std::move(shardId)};

            if (user.name()) {
                account.setDisplayName(*user.name());
            }

            promise->addResult(std::move(account));
            promise->finish();
        });

    return future;
}

QFuture<void> AuthenticationInfoProvider::storeAuthenticationInfo(
    IAuthenticationInfoPtr authenticationInfo, Account account)
{
    Q_ASSERT(authenticationInfo);

    auto promise = std::make_shared<QPromise<void>>();
    auto future = promise->future();

    promise->start();

    const auto appName = QCoreApplication::applicationName();
    const auto userIdStr = QString::number(authenticationInfo->userId());

    QString writeAuthTokenService =
        QString::fromUtf8("%1_%2").arg(appName, gAuthTokenKeychainKeyPart);

    QString writeAuthTokenKey =
        QString::fromUtf8("%1_%2_%3_%4")
            .arg(appName, gAuthTokenKeychainKeyPart, m_host, userIdStr);

    auto writeAuthTokenFuture = m_keychainService->writePassword(
        std::move(writeAuthTokenService), std::move(writeAuthTokenKey),
        authenticationInfo->authToken());

    QString writeShardIdService =
        QString::fromUtf8("%1_%2").arg(appName, gShardIdKeychainKeyPart);

    QString writeShardIdKey =
        QString::fromUtf8("%1_%2_%3_%4")
            .arg(appName, gShardIdKeychainKeyPart, m_host, userIdStr);

    auto writeShardIdFuture = m_keychainService->writePassword(
        std::move(writeShardIdService), std::move(writeShardIdKey),
        authenticationInfo->shardId());

    auto writeAuthTokenAndShardIdFuture = threading::whenAll(
        QList<QFuture<void>>{} << writeAuthTokenFuture << writeShardIdFuture);

    threading::thenOrFailed(
        std::move(writeAuthTokenAndShardIdFuture), promise,
        threading::TrackedTask{
            weak_from_this(),
            [this, promise, authenticationInfo = std::move(authenticationInfo),
             account = std::move(account)] {
                ApplicationSettings settings{
                    account, gSynchronizationPersistence};

                settings.beginGroup(
                    QString::fromUtf8("Authentication/%1/%2/")
                        .arg(
                            m_host,
                            QString::number(authenticationInfo->userId())));

                ApplicationSettings::GroupCloser groupCloser{settings};

                settings.setValue(
                    gNoteStoreUrlKey,
                    authenticationInfo->noteStoreUrl());

                settings.setValue(
                    gExpirationTimestampKey,
                    authenticationInfo->authTokenExpirationTime());

                settings.setValue(
                    gAuthenticationTimestampKey,
                    authenticationInfo->authenticationTime());

                settings.setValue(
                    gWebApiUrlPrefixKey,
                    authenticationInfo->webApiUrlPrefix());

                const auto userStoreCookies =
                    authenticationInfo->userStoreCookies();

                for (const auto & cookie: qAsConst(userStoreCookies)) {
                    const QString cookieName = QString::fromUtf8(cookie.name());
                    if (!cookieName.startsWith(QStringLiteral("web")) ||
                        !cookieName.endsWith(QStringLiteral("PreUserGuid")))
                    {
                        QNDEBUG(
                            "synchronization::AuthenticationInfoProvider",
                            "Skipping cookie " << cookie.name()
                                               << " from persistence");
                        continue;
                    }

                    settings.setValue(
                        gUserStoreCookieKey, cookie.toRawForm());
                    QNDEBUG(
                        "synchronization::AuthenticationInfoProvider",
                        "Persisted cookie " << cookie.name());
                }

                QNDEBUG(
                    "synchronization::AuthenticationInfoProvider",
                    "Successfully wrote the authentication info to the "
                        << "application settings for host " << m_host
                        << ", user id " << authenticationInfo->userId()
                        << ": auth token expiration timestamp = "
                        << printableDateTimeFromTimestamp(
                               authenticationInfo->authTokenExpirationTime())
                        << ", web API url prefix = "
                        << authenticationInfo->webApiUrlPrefix());

                promise->finish();
            }});

    return future;
}

} // namespace quentier::synchronization
