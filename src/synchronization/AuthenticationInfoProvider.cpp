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

#include <synchronization/INoteStoreFactory.h>
#include <synchronization/IUserInfoProvider.h>
#include <synchronization/types/AuthenticationInfo.h>

#include <qevercloud/RequestContext.h>
#include <qevercloud/RequestContextBuilder.h>
#include <qevercloud/services/INoteStore.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QReadLocker>
#include <QTimeZone>
#include <QWriteLocker>

#include <limits>

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

const QString gLinkedNotebookExpirationTimestampKey =
    QStringLiteral("LinkedNotebookExpirationTimestamp");

const QString gAuthenticationTimestampKey =
    QStringLiteral("AuthenticationTimestamp");

const QString gLinkedNotebookAuthenticationTimestamp =
    QStringLiteral("LinkedNotebookAuthenticationTimestamp");

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

[[nodiscard]] QString authTokenKeychainServiceName()
{
    static const QString appName = QCoreApplication::applicationName();
    return QString::fromUtf8("%1_%2").arg(appName, gAuthTokenKeychainKeyPart);
}

[[nodiscard]] QString authTokenKeychainKeyName(
    const QString & host, const QString & userId)
{
    static const QString appName = QCoreApplication::applicationName();
    return QString::fromUtf8("%1_%2_%3_%4")
        .arg(appName, gAuthTokenKeychainKeyPart, host, userId);
}

[[nodiscard]] QString shardIdKeychainServiceName()
{
    static const QString appName = QCoreApplication::applicationName();
    return QString::fromUtf8("%1_%2").arg(appName, gShardIdKeychainKeyPart);
}

[[nodiscard]] QString shardIdKeychainKeyName(
    const QString & host, const QString & userId)
{
    static const QString appName = QCoreApplication::applicationName();
    return QString::fromUtf8("%1_%2_%3_%4")
        .arg(appName, gShardIdKeychainKeyPart, host, userId);
}

[[nodiscard]] QString linkedNotebookAuthTokenKeychainServiceName()
{
    static const QString appName = QCoreApplication::applicationName();
    return QString::fromUtf8("%1_linked_notebook_%2")
        .arg(appName, gAuthTokenKeychainKeyPart);
}

[[nodiscard]] QString linkedNotebookAuthTokenKeychainKeyName(
    const QString & host, const QString & userId,
    const qevercloud::Guid & linkedNotebookGuid)
{
    static const QString appName = QCoreApplication::applicationName();
    return QString::fromUtf8("%1_linked_notebook_%2_%3_%4_%5")
        .arg(
            appName, gAuthTokenKeychainKeyPart, host, userId,
            linkedNotebookGuid);
}

[[nodiscard]] bool isTimestampAboutToExpire(
    const qevercloud::Timestamp timestamp)
{
    const qevercloud::Timestamp currentTimestamp =
        QDateTime::currentMSecsSinceEpoch();

    constexpr qint64 halfAnHourMsec = 1800000;

    return (timestamp - currentTimestamp) < halfAnHourMsec;
}

} // namespace

// clang-format off
AuthenticationInfoProvider::AuthenticationInfoProvider(
    IAuthenticatorPtr authenticator, IKeychainServicePtr keychainService,
    IUserInfoProviderPtr userInfoProvider,
    INoteStoreFactoryPtr noteStoreFactory, qevercloud::IRequestContextPtr ctx,
    qevercloud::IRetryPolicyPtr retryPolicy, QString host) :
    m_authenticator{std::move(authenticator)},
    m_keychainService{std::move(keychainService)},
    m_userInfoProvider{std::move( userInfoProvider)},
    m_noteStoreFactory{std::move(noteStoreFactory)}, m_ctx{std::move(ctx)},
    m_retryPolicy{std::move(retryPolicy)}, m_host{std::move(host)}
{
    // clang-format on
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

    if (Q_UNLIKELY(!m_noteStoreFactory)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::AuthenticationInfoProvider",
            "AuthenticationInfoProvider ctor: note store factory is null")}};
    }

    if (Q_UNLIKELY(!m_ctx)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::AuthenticationInfoProvider",
            "AuthenticationInfoProvider ctor: request context is null")}};
    }

    if (Q_UNLIKELY(!m_retryPolicy)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::AuthenticationInfoProvider",
            "AuthenticationInfoProvider ctor: retry policy is null")}};
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

                {
                    const QWriteLocker locker{&m_authenticationInfosRWLock};
                    m_authenticationInfos[authenticationInfo->userId()] =
                        authenticationInfo;
                }

                auto accountFuture = findAccountForUserId(
                    authenticationInfo->userId(),
                    authenticationInfo->authToken(),
                    authenticationInfo->shardId(),
                    authenticationInfo->userStoreCookies());

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
            InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
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

    {
        const QReadLocker locker{&m_authenticationInfosRWLock};
        if (const auto it = m_authenticationInfos.find(account.id());
            it != m_authenticationInfos.end() &&
            !isTimestampAboutToExpire(it.value()->authTokenExpirationTime()))
        {
            return threading::makeReadyFuture<IAuthenticationInfoPtr>(
                it.value());
        }
    }

    auto authenticationInfo = readAuthenticationInfoPart(account);
    if (!authenticationInfo) {
        authenticateAccountWithoutCache(std::move(account), promise);
        return future;
    }

    if (isTimestampAboutToExpire(authenticationInfo->m_authTokenExpirationTime))
    {
        QNDEBUG(
            "synchronization::AuthenticationInfoProvider",
            "Authentication token is about to expire: expiration timestamp = "
                << printableDateTimeFromTimestamp(
                       authenticationInfo->m_authTokenExpirationTime));

        authenticateAccountWithoutCache(std::move(account), promise);
        return future;
    }

    Q_ASSERT(authenticationInfo->userId() == account.id());
    const auto userIdStr = QString::number(authenticationInfo->userId());

    auto readAuthTokenFuture = m_keychainService->readPassword(
        authTokenKeychainServiceName(),
        authTokenKeychainKeyName(m_host, userIdStr));

    auto readShardIdFuture = m_keychainService->readPassword(
        shardIdKeychainServiceName(),
        shardIdKeychainKeyName(m_host, userIdStr));

    auto readAllFuture = threading::whenAll<QString>(
        QList<QFuture<QString>>{} << readAuthTokenFuture << readShardIdFuture);

    const auto selfWeak = weak_from_this();

    auto readAllThenFuture = threading::then(
        std::move(readAllFuture),
        [promise, selfWeak, authenticationInfo = std::move(authenticationInfo)](
            QList<QString> tokenAndShardId) mutable {
            Q_ASSERT(tokenAndShardId.size() == 2);
            authenticationInfo->m_authToken = std::move(tokenAndShardId[0]);
            authenticationInfo->m_shardId = std::move(tokenAndShardId[1]);

            if (const auto self = selfWeak.lock()) {
                const QWriteLocker locker{&self->m_authenticationInfosRWLock};
                self->m_authenticationInfos[authenticationInfo->userId()] =
                    authenticationInfo;
            }

            promise->addResult(std::move(authenticationInfo));
            promise->finish();
        });

    threading::onFailed(
        std::move(readAllThenFuture),
        [promise, selfWeak,
         account = std::move(account)](const QException & e) mutable {
            QNINFO(
                "synchronization::AuthenticationInfoProvider",
                "Could not read auth token or shard id from the keychain "
                    << "for user with id " << account.id() << ": " << e.what());

            if (const auto self = selfWeak.lock()) {
                self->authenticateAccountWithoutCache(
                    std::move(account), promise);
            }
        });

    return future;
}

QFuture<IAuthenticationInfoPtr>
    AuthenticationInfoProvider::authenticateToLinkedNotebook(
        Account account, qevercloud::LinkedNotebook linkedNotebook, Mode mode)
{
    if (Q_UNLIKELY(account.type() != Account::Type::Evernote)) {
        return threading::makeExceptionalFuture<IAuthenticationInfoPtr>(
            InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
                "synchronization::AuthenticationInfoProvider",
                "Detected attempt to authenticate to linked notebook for "
                "non-Evernote account")}});
    }

    if (Q_UNLIKELY(!linkedNotebook.guid())) {
        return threading::makeExceptionalFuture<IAuthenticationInfoPtr>(
            InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
                "synchronization::AuthenticationInfoProvider",
                "Detected attempt to authenticate to linked notebook wihout "
                "guid")}});
    }

    auto promise = std::make_shared<QPromise<IAuthenticationInfoPtr>>();
    auto future = promise->future();

    promise->start();

    const auto & sharedNotebookGlobalId =
        linkedNotebook.sharedNotebookGlobalId();

    const auto & uri = linkedNotebook.uri();

    if ((!sharedNotebookGlobalId || sharedNotebookGlobalId->isEmpty()) && uri &&
        !uri->isEmpty())
    {
        // This appears to be a public notebook and per the official
        // documentation from Evernote
        // (dev.evernote.com/media/pdf/edam-sync.pdf) it doesn't need the
        // authentication token at all so will use empty string for its
        // authentication token
        auto authenticationInfo = std::make_shared<AuthenticationInfo>();
        authenticationInfo->m_userId = account.id();
        authenticationInfo->m_authTokenExpirationTime =
            std::numeric_limits<qint64>::max();

        authenticationInfo->m_authenticationTime =
            QDateTime::currentMSecsSinceEpoch();

        authenticationInfo->m_shardId =
            linkedNotebook.shardId().value_or(QString{});

        authenticationInfo->m_noteStoreUrl =
            linkedNotebook.noteStoreUrl().value_or(QString{});

        authenticationInfo->m_webApiUrlPrefix =
            linkedNotebook.webApiUrlPrefix().value_or(QString{});

        promise->addResult(std::move(authenticationInfo));
        promise->finish();
        return future;
    }

    if (mode == Mode::NoCache) {
        authenticateToLinkedNotebookWithoutCache(
            std::move(account), std::move(linkedNotebook), promise);
        return future;
    }

    {
        QReadLocker locker{&m_linkedNotebookAuthenticationInfosRWLock};
        if (const auto it = m_linkedNotebookAuthenticationInfos.find(
                *linkedNotebook.guid());
            it != m_linkedNotebookAuthenticationInfos.end() &&
            !isTimestampAboutToExpire(it.value()->authTokenExpirationTime()))
        {
            const auto authenticationInfo = it.value();

            locker.unlock();
            Q_ASSERT(authenticationInfo);

            if (linkedNotebook.noteStoreUrl() ==
                    authenticationInfo->noteStoreUrl() &&
                authenticationInfo->userId() == account.id())
            {
                promise->addResult(authenticationInfo);
                promise->finish();
                return future;
            }
        }
    }

    // Checking whether there is a stored expiration timestamp for this
    // linked notebook's authentication info and if yes, whether the timestamp
    // is too close to expiration.

    ApplicationSettings settings{account, gSynchronizationPersistence};

    // NOTE: having account id as a part of the group seems redundant
    // as the settings are already being persisted for the given account
    // but that's the legacy layout which is maintained for compatibility.
    const QString keyGroup = QString::fromUtf8("Authentication/%1/%2")
                                 .arg(m_host, QString::number(account.id()));

    settings.beginGroup(keyGroup);
    ApplicationSettings::GroupCloser groupCloser{settings};

    const QString authenticationTimestampKey = QString::fromUtf8("%1_%2").arg(
        gLinkedNotebookAuthenticationTimestamp, *linkedNotebook.guid());

    const QString expirationTimestampKey = QString::fromUtf8("%1_%2").arg(
        gLinkedNotebookExpirationTimestampKey, *linkedNotebook.guid());

    if (!settings.contains(expirationTimestampKey) ||
        !settings.contains(authenticationTimestampKey))
    {
        authenticateToLinkedNotebookWithoutCache(
            std::move(account), std::move(linkedNotebook), promise);
        return future;
    }

    const std::optional<qevercloud::Timestamp> expirationTimestamp =
        [&]() -> std::optional<qevercloud::Timestamp> {
        const QVariant expirationTimestampValue =
            settings.value(expirationTimestampKey);

        bool conversionResult = false;
        const qevercloud::Timestamp expirationTimestamp =
            expirationTimestampValue.toLongLong(&conversionResult);

        if (Q_UNLIKELY(!conversionResult)) {
            QNWARNING(
                "synchronization::AuthenticationInfoProvider",
                "Stored authentication expiration timestamp for a linked "
                    << "notebook is not a valid integer: "
                    << expirationTimestampValue);

            return std::nullopt;
        }

        if (isTimestampAboutToExpire(expirationTimestamp)) {
            QNDEBUG(
                "synchronization::AuthenticationInfoProvider",
                "Authentication token for linked notebook with guid "
                    << *linkedNotebook.guid() << " is about to expire: "
                    << "expiration timestamp = "
                    << printableDateTimeFromTimestamp(expirationTimestamp));

            return std::nullopt;
        }

        return expirationTimestamp;
    }();

    if (!expirationTimestamp) {
        authenticateToLinkedNotebookWithoutCache(
            std::move(account), std::move(linkedNotebook), promise);
        return future;
    }

    const std::optional<qevercloud::Timestamp> authenticationTimestamp =
        [&]() -> std::optional<qevercloud::Timestamp> {
        const QVariant authenticationTimestampValue =
            settings.value(authenticationTimestampKey);

        bool conversionResult = false;
        const qevercloud::Timestamp authenticationTimestamp =
            authenticationTimestampValue.toLongLong(&conversionResult);

        if (Q_UNLIKELY(!conversionResult)) {
            QNWARNING(
                "synchronization::AuthenticationInfoProvider",
                "Stored authentication timestamp for a linked notebook is "
                    << "not a valid integer: " << authenticationTimestampValue);

            return std::nullopt;
        }

        return authenticationTimestamp;
    }();

    if (!authenticationTimestamp) {
        authenticateToLinkedNotebookWithoutCache(
            std::move(account), std::move(linkedNotebook), promise);
        return future;
    }

    const auto userIdStr = QString::number(account.id());

    auto readAuthTokenFuture = m_keychainService->readPassword(
        linkedNotebookAuthTokenKeychainServiceName(),
        linkedNotebookAuthTokenKeychainKeyName(
            m_host, userIdStr, *linkedNotebook.guid()));

    const auto selfWeak = weak_from_this();

    auto readAuthTokenThenFuture = threading::then(
        std::move(readAuthTokenFuture),
        [promise, selfWeak, userId = account.id(),
         expirationTimestamp = *expirationTimestamp,
         authenticationTimestamp = *authenticationTimestamp,
         linkedNotebookGuid = *linkedNotebook.guid(),
         noteStoreUrl = linkedNotebook.noteStoreUrl(),
         webApiUrlPrefix = linkedNotebook.webApiUrlPrefix(),
         shardId = linkedNotebook.shardId().value_or(QString{})](
            QString authToken) mutable {
            auto authenticationInfo = std::make_shared<AuthenticationInfo>();
            authenticationInfo->m_authToken = std::move(authToken);
            authenticationInfo->m_shardId = std::move(shardId);
            authenticationInfo->m_userId = userId;

            authenticationInfo->m_authenticationTime = authenticationTimestamp;
            authenticationInfo->m_authTokenExpirationTime = expirationTimestamp;

            authenticationInfo->m_noteStoreUrl =
                noteStoreUrl.value_or(QString{});

            authenticationInfo->m_webApiUrlPrefix =
                webApiUrlPrefix.value_or(QString{});

            if (const auto self = selfWeak.lock()) {
                const QWriteLocker locker{&self->m_authenticationInfosRWLock};
                self->m_linkedNotebookAuthenticationInfos[linkedNotebookGuid] =
                    authenticationInfo;
            }

            promise->addResult(std::move(authenticationInfo));
            promise->finish();
        });

    threading::onFailed(
        std::move(readAuthTokenThenFuture),
        [promise, selfWeak, linkedNotebook = std::move(linkedNotebook),
         account = std::move(account)](const QException & e) mutable {
            QNINFO(
                "synchronization::AuthenticationInfoProvider",
                "Could not read auth token for linked notebook with guid "
                    << *linkedNotebook.guid()
                    << " from the keychain: " << e.what());

            if (const auto self = selfWeak.lock()) {
                self->authenticateToLinkedNotebookWithoutCache(
                    std::move(account), std::move(linkedNotebook), promise);
            }
        });

    return future;
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

void AuthenticationInfoProvider::authenticateToLinkedNotebookWithoutCache(
    Account account,
    qevercloud::LinkedNotebook linkedNotebook, // NOLINT
    const std::shared_ptr<QPromise<IAuthenticationInfoPtr>> & promise)
{
    const auto & noteStoreUrl = linkedNotebook.noteStoreUrl();
    if (Q_UNLIKELY(!noteStoreUrl)) {
        promise->setException(RuntimeError{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::AuthenticationInfoProvider",
            "Cannot authenticate to linked notebook: no note store url")}});
        promise->finish();
        return;
    }

    const auto & linkedNotebookGuid = linkedNotebook.guid();
    if (Q_UNLIKELY(!linkedNotebookGuid)) {
        promise->setException(RuntimeError{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::AuthenticationInfoProvider",
            "Cannot authenticate to linked notebook: no guid")}});
        promise->finish();
        return;
    }

    const auto & sharedNotebookGlobalId =
        linkedNotebook.sharedNotebookGlobalId();

    if (Q_UNLIKELY(!sharedNotebookGlobalId)) {
        promise->setException(RuntimeError{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::AuthenticationInfoProvider",
            "Cannot authenticate to linked notebook: no shared notebook global "
            "id")}});
        promise->finish();
        return;
    }

    const auto noteStore = m_noteStoreFactory->noteStore(
        *noteStoreUrl, linkedNotebookGuid, m_ctx, m_retryPolicy);

    if (Q_UNLIKELY(!noteStore)) {
        promise->setException(RuntimeError{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::AuthenticationInfoProvider",
            "Cannot authenticate to linked notebook: cannot create note "
            "store")}});
        promise->finish();
        return;
    }

    auto authFuture = noteStore->authenticateToSharedNotebookAsync(
        *sharedNotebookGlobalId, m_ctx);

    const auto selfWeak = weak_from_this();

    threading::thenOrFailed(
        std::move(authFuture), promise,
        threading::TrackedTask{
            selfWeak,
            [this, promise, account = std::move(account),
             linkedNotebook = std::move(linkedNotebook)](
                const qevercloud::AuthenticationResult & result) mutable {
                auto authenticationInfo =
                    std::make_shared<AuthenticationInfo>();
                authenticationInfo->m_userId = account.id();
                authenticationInfo->m_authToken = result.authenticationToken();
                authenticationInfo->m_authTokenExpirationTime =
                    result.expiration();

                authenticationInfo->m_authenticationTime = result.currentTime();

                authenticationInfo->m_shardId =
                    linkedNotebook.shardId().value_or(QString{});

                const auto & urls = result.urls();
                const auto & publicUserInfo = result.publicUserInfo();

                authenticationInfo->m_noteStoreUrl = [&] {
                    if (urls) {
                        const auto & noteStoreUrl = urls->noteStoreUrl();
                        if (noteStoreUrl) {
                            return *noteStoreUrl;
                        }

                        QNWARNING(
                            "synchronization::AuthenticationInfoProvider",
                            "No noteStoreUrl in "
                            "qevercloud::AuthenticationResult::urls");
                    }

                    if (publicUserInfo) {
                        const auto & noteStoreUrl =
                            publicUserInfo->noteStoreUrl();

                        if (noteStoreUrl) {
                            return *noteStoreUrl;
                        }

                        QNWARNING(
                            "synchronization::AuthenticationInfoProvider",
                            "No noteStoreUrl in "
                            "qevercloud::AuthenticationResult::publicUserInfo");
                    }

                    return linkedNotebook.noteStoreUrl().value_or(QString{});
                }();

                authenticationInfo->m_webApiUrlPrefix = [&] {
                    if (urls) {
                        const auto & webApiUrlPrefix = urls->webApiUrlPrefix();
                        if (webApiUrlPrefix) {
                            return *webApiUrlPrefix;
                        }

                        QNWARNING(
                            "synchronization::AuthenticationInfoProvider",
                            "No webApiUrlPrefix in "
                            "qevercloud::AuthenticationResult::urls");
                    }

                    if (publicUserInfo) {
                        const auto & webApiUrlPrefix =
                            publicUserInfo->webApiUrlPrefix();
                        if (webApiUrlPrefix) {
                            return *webApiUrlPrefix;
                        }

                        QNWARNING(
                            "synchronization::AuthenticationInfoProvider",
                            "No webApiUrlPrefix in "
                            "qevercloud::AuthenticationResult::publicUserInfo");
                    }

                    return linkedNotebook.webApiUrlPrefix().value_or(QString{});
                }();

                {
                    const QWriteLocker locker{
                        &m_linkedNotebookAuthenticationInfosRWLock};

                    m_linkedNotebookAuthenticationInfos[*linkedNotebook
                                                             .guid()] =
                        authenticationInfo;
                }

                qevercloud::Guid linkedNotebookGuid = *linkedNotebook.guid();

                auto storeAuthInfoFuture =
                    storeLinkedNotebookAuthenticationInfo(
                        authenticationInfo, std::move(linkedNotebook),
                        std::move(account));

                auto storeAuthInfoThenFuture = threading::then(
                    std::move(storeAuthInfoFuture),
                    [promise, authenticationInfo]() mutable {
                        promise->addResult(std::move(authenticationInfo));
                        promise->finish();
                    });

                threading::onFailed(
                    std::move(storeAuthInfoThenFuture),
                    [promise,
                     authenticationInfo = std::move(authenticationInfo),
                     linkedNotebookGuid = std::move(linkedNotebookGuid)](
                        const QException & e) mutable {
                        QNWARNING(
                            "synchronization::AuthenticationInfoProvider",
                            "Failed to store authentication info for "
                                << "linked notebook with guid "
                                << linkedNotebookGuid << ": " << e.what());

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
    ApplicationSettings settings{account, gSynchronizationPersistence};

    // NOTE: having account id as a part of the group seems redundant
    // as the settings are already being persisted for the given account
    // but that's the legacy layout which is maintained for compatibility.
    const QString keyGroup = QString::fromUtf8("Authentication/%1/%2/")
                                 .arg(m_host, QString::number(account.id()));

    settings.beginGroup(keyGroup);
    ApplicationSettings::GroupCloser groupCloser{settings};

    // NOTE: user store cookies are optional, so not considering them a hard
    // requirement
    if (!settings.contains(gAuthenticationTimestampKey) ||
        !settings.contains(gExpirationTimestampKey) ||
        !settings.contains(gNoteStoreUrlKey) ||
        !settings.contains(gWebApiUrlPrefixKey))
    {
        return nullptr;
    }

    auto authenticationInfo = std::make_shared<AuthenticationInfo>();

    authenticationInfo->m_userId = account.id();

    const QVariant authenticationTimestamp =
        settings.value(gAuthenticationTimestampKey);
    {
        bool conversionResult = false;

        authenticationInfo->m_authenticationTime =
            authenticationTimestamp.toLongLong(&conversionResult);

        if (Q_UNLIKELY(!conversionResult)) {
            QNWARNING(
                "synchronization::AuthenticationInfoProvider",
                "Stored authentication timestamp is not a valid integer: "
                    << authenticationTimestamp);
            return nullptr;
        }
    }

    const QVariant tokenExpirationValue =
        settings.value(gExpirationTimestampKey);
    {
        bool conversionResult = false;

        authenticationInfo->m_authTokenExpirationTime =
            tokenExpirationValue.toLongLong(&conversionResult);

        if (Q_UNLIKELY(!conversionResult)) {
            QNWARNING(
                "synchronization::AuthenticationInfoProvider",
                "Stored authentication token expiration timestamp is not a "
                    << "valid integer: " << tokenExpirationValue);
            return nullptr;
        }
    }

    const QVariant noteStoreUrlValue = settings.value(gNoteStoreUrlKey);
    authenticationInfo->m_noteStoreUrl = noteStoreUrlValue.toString();
    if (authenticationInfo->m_noteStoreUrl.isEmpty()) {
        QNWARNING(
            "synchronization::AuthenticationInfoProvider",
            "Stored note store url is not a string or empty string: "
                << noteStoreUrlValue);
        return nullptr;
    }

    const QVariant webApiUrlPrefixValue = settings.value(gWebApiUrlPrefixKey);
    authenticationInfo->m_webApiUrlPrefix = webApiUrlPrefixValue.toString();
    if (authenticationInfo->m_webApiUrlPrefix.isEmpty()) {
        QNWARNING(
            "synchronization::AuthenticationInfoProvider",
            "Stored web api url prefix is not a string or empty string: "
                << webApiUrlPrefixValue);
        return nullptr;
    }

    if (settings.contains(gUserStoreCookieKey)) {
        const QByteArray userStoreCookies =
            settings.value(gUserStoreCookieKey).toByteArray();

        authenticationInfo->m_userStoreCookies =
            QNetworkCookie::parseCookies(userStoreCookies);
    }

    return authenticationInfo;
}

QFuture<Account> AuthenticationInfoProvider::findAccountForUserId(
    const qevercloud::UserID userId, QString authToken, QString shardId, // NOLINT
    QList<QNetworkCookie> cookies) // NOLINT
{
    auto promise = std::make_shared<QPromise<Account>>();
    auto future = promise->future();

    promise->start();

    auto ctx = qevercloud::RequestContextBuilder{}
                   .setAuthenticationToken(std::move(authToken))
                   .setCookies(std::move(cookies))
                   .build();

    auto userFuture = m_userInfoProvider->userInfo(std::move(ctx));

    threading::thenOrFailed(
        std::move(userFuture), promise,
        [promise, userId, host = m_host,
         shardId = std::move(shardId)](const qevercloud::User & user) mutable {
            QNDEBUG(
                "synchronization::AuthenticationInfoProvider",
                "Received user for id " << userId << ": " << user);

            Q_ASSERT(user.id() && *user.id() == userId);

            QString name = user.name() ? *user.name()
                                       : user.username().value_or(QString{});

            if (Q_UNLIKELY(name.isEmpty())) {
                QNWARNING(
                    "synchronization::AuthenticationInfoProvider",
                    "User for id " << userId
                                   << " has no name or username: " << user);
                promise->setException(
                    RuntimeError{ErrorString{QT_TRANSLATE_NOOP(
                        "synchronization::AuthenticationInfoProvider",
                        "Authenticated user has no name or username")}});
                promise->finish();
                return;
            }

            Account account{
                std::move(name),
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

    Q_ASSERT(authenticationInfo->userId() == account.id());
    const auto userIdStr = QString::number(authenticationInfo->userId());

    auto writeAuthTokenFuture = m_keychainService->writePassword(
        authTokenKeychainServiceName(),
        authTokenKeychainKeyName(m_host, userIdStr),
        authenticationInfo->authToken());

    auto writeShardIdFuture = m_keychainService->writePassword(
        shardIdKeychainServiceName(), shardIdKeychainKeyName(m_host, userIdStr),
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
                    gNoteStoreUrlKey, authenticationInfo->noteStoreUrl());

                settings.setValue(
                    gExpirationTimestampKey,
                    authenticationInfo->authTokenExpirationTime());

                settings.setValue(
                    gAuthenticationTimestampKey,
                    authenticationInfo->authenticationTime());

                settings.setValue(
                    gWebApiUrlPrefixKey, authenticationInfo->webApiUrlPrefix());

                const auto userStoreCookies =
                    authenticationInfo->userStoreCookies();

                bool persistentCookieFound = false;
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

                    persistentCookieFound = true;
                    settings.setValue(gUserStoreCookieKey, cookie.toRawForm());
                    QNDEBUG(
                        "synchronization::AuthenticationInfoProvider",
                        "Persisted cookie " << cookie.name());

                    break;
                }

                if (!persistentCookieFound) {
                    settings.remove(gUserStoreCookieKey);
                }

                QNDEBUG(
                    "synchronization::AuthenticationInfoProvider",
                    "Successfully wrote the authentication info to the "
                        << "application settings for host " << m_host
                        << ", user id " << authenticationInfo->userId()
                        << ": auth token expiration timestamp = "
                        << printableDateTimeFromTimestamp(
                               authenticationInfo->authTokenExpirationTime())
                        << ", authentication time = "
                        << printableDateTimeFromTimestamp(
                               authenticationInfo->authenticationTime())
                        << ", web API url prefix = "
                        << authenticationInfo->webApiUrlPrefix());

                promise->finish();
            }});

    return future;
}

QFuture<void> AuthenticationInfoProvider::storeLinkedNotebookAuthenticationInfo(
    IAuthenticationInfoPtr authenticationInfo,
    qevercloud::LinkedNotebook linkedNotebook, Account account)
{
    Q_ASSERT(authenticationInfo);

    auto promise = std::make_shared<QPromise<void>>();
    auto future = promise->future();

    promise->start();

    Q_ASSERT(authenticationInfo->userId() == account.id());
    const auto userIdStr = QString::number(authenticationInfo->userId());

    if (Q_UNLIKELY(!linkedNotebook.guid())) {
        promise->setException(RuntimeError{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::AuthenticationInfoProvider",
            "Cannot store authentication info for linked notebook: "
            "no guid")}});
        promise->finish();
        return future;
    }

    auto writeAuthTokenFuture = m_keychainService->writePassword(
        linkedNotebookAuthTokenKeychainServiceName(),
        linkedNotebookAuthTokenKeychainKeyName(
            m_host, userIdStr, *linkedNotebook.guid()),
        authenticationInfo->authToken());

    threading::thenOrFailed(
        std::move(writeAuthTokenFuture), promise,
        threading::TrackedTask{
            weak_from_this(),
            [this, promise, authenticationInfo = std::move(authenticationInfo),
             account = std::move(account),
             linkedNotebook = std::move(linkedNotebook)] {
                ApplicationSettings settings{
                    account, gSynchronizationPersistence};

                settings.beginGroup(
                    QString::fromUtf8("Authentication/%1/%2/")
                        .arg(
                            m_host,
                            QString::number(authenticationInfo->userId())));

                ApplicationSettings::GroupCloser groupCloser{settings};

                const QString authenticationTimestampKey =
                    QString::fromUtf8("%1_%2").arg(
                        gLinkedNotebookAuthenticationTimestamp,
                        *linkedNotebook.guid());

                settings.setValue(
                    authenticationTimestampKey,
                    authenticationInfo->authenticationTime());

                const QString expirationTimestampKey =
                    QString::fromUtf8("%1_%2").arg(
                        gLinkedNotebookExpirationTimestampKey,
                        *linkedNotebook.guid());

                settings.setValue(
                    expirationTimestampKey,
                    authenticationInfo->authTokenExpirationTime());

                QNDEBUG(
                    "synchronization::AuthenticationInfoProvider",
                    "Successfully wrote the linked notebook authentication "
                        << "info to the application settings for host "
                        << m_host << ", user id "
                        << authenticationInfo->userId()
                        << ": auth token expiration timestamp = "
                        << printableDateTimeFromTimestamp(
                               authenticationInfo->authTokenExpirationTime())
                        << ", authentication time = "
                        << printableDateTimeFromTimestamp(
                               authenticationInfo->authenticationTime()));
            }});

    return future;
}

} // namespace quentier::synchronization
