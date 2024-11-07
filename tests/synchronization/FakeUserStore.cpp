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

#include "FakeUserStore.h"
#include "FakeUserStoreBackend.h"

#include <quentier/exception/RuntimeError.h>
#include <quentier/utility/EventLoopWithExitStatus.h>
#include <quentier/utility/Unreachable.h>

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <quentier/threading/Qt5Promise.h>
#endif

#include <qevercloud/RequestContextBuilder.h>

#include <QTimer>

#include <memory>

// clazy:excludeall=connect-3arg-lambda
// clazy:excludeall=lambda-in-connect

namespace quentier::synchronization::tests {

namespace {

// 10 minutes in milliseconds
constexpr int gSyncMethodCallTimeout = 600000;

[[nodiscard]] ErrorString exceptionMessage(const std::exception_ptr & e)
{
    try {
        std::rethrow_exception(e);
    }
    catch (const IQuentierException & exc) {
        return exc.errorMessage();
    }
    catch (const std::exception & exc) {
        return ErrorString{QString::fromUtf8(exc.what())};
    }
    catch (...) {
        return ErrorString{
            QT_TRANSLATE_NOOP("exception::Utils", "Unknown exception")};
    }

    UNREACHABLE;
}

} // namespace

FakeUserStore::FakeUserStore(
    FakeUserStoreBackend * backend, QString userStoreUrl,
    qevercloud::IRequestContextPtr ctx,
    qevercloud::IRetryPolicyPtr retryPolicy) :
    m_backend{backend}, m_userStoreUrl{std::move(userStoreUrl)},
    m_ctx{std::move(ctx)}, m_retryPolicy{std::move(retryPolicy)}
{
    Q_ASSERT(m_backend);
}

qevercloud::IRequestContextPtr FakeUserStore::defaultRequestContext()
    const noexcept
{
    return m_ctx;
}

void FakeUserStore::setDefaultRequestContext(
    qevercloud::IRequestContextPtr ctx) noexcept
{
    m_ctx = std::move(ctx);
}

QString FakeUserStore::userStoreUrl() const
{
    return m_userStoreUrl;
}

void FakeUserStore::setUserStoreUrl(QString url)
{
    m_userStoreUrl = std::move(url);
}

bool FakeUserStore::checkVersion(
    QString clientName, const qint16 edamVersionMajor,
    const qint16 edamVersionMinor, qevercloud::IRequestContextPtr ctx)
{
    ensureRequestContext(ctx);

    bool result = false;
    EventLoopWithExitStatus::ExitStatus status =
        EventLoopWithExitStatus::ExitStatus::Failure;
    ErrorString error;
    {
        QTimer timer;
        timer.setInterval(gSyncMethodCallTimeout);
        timer.setSingleShot(true);

        EventLoopWithExitStatus loop;

        QObject::connect(
            &timer, &QTimer::timeout, &loop,
            &EventLoopWithExitStatus::exitAsTimeout);

        auto connection = QObject::connect(
            m_backend, &FakeUserStoreBackend::checkVersionRequestReady,
            [&](const bool value, const std::exception_ptr & e,
                const QUuid requestId) {
                if (requestId != ctx->requestId()) {
                    return;
                }

                if (!e) {
                    result = value;
                    QTimer::singleShot(0, [&loop] { loop.exitAsSuccess(); });
                    return;
                }

                ErrorString errorMessage = exceptionMessage(e);
                QTimer::singleShot(
                    0,
                    [&loop, errorMessage = std::move(errorMessage)]() mutable {
                        loop.exitAsFailureWithErrorString(
                            std::move(errorMessage));
                    });
            });

        QTimer::singleShot(
            0,
            [ctx = std::move(ctx), clientName = std::move(clientName),
             edamVersionMajor, edamVersionMinor, backend = m_backend] {
                backend->onCheckVersionRequest(
                    clientName, edamVersionMajor, edamVersionMinor, ctx);
            });

        timer.start();
        loop.exec();

        QObject::disconnect(connection);
        status = loop.exitStatus();
        error = loop.errorDescription();
    }

    switch (status) {
    case EventLoopWithExitStatus::ExitStatus::Success:
        return result;
    case EventLoopWithExitStatus::ExitStatus::Failure:
        throw RuntimeError{std::move(error)};
    case EventLoopWithExitStatus::ExitStatus::Timeout:
        throw RuntimeError{ErrorString{
            QStringLiteral("Failed to check protocol version in due time")}};
    }

    UNREACHABLE;
}

QFuture<bool> FakeUserStore::checkVersionAsync(
    QString clientName, const qint16 edamVersionMajor,
    const qint16 edamVersionMinor, qevercloud::IRequestContextPtr ctx)
{
    ensureRequestContext(ctx);

    auto promise = std::make_shared<QPromise<bool>>();
    auto future = promise->future();
    promise->start();

    auto dummyObject = std::make_unique<QObject>();
    auto * dummyObjectRaw = dummyObject.get();
    QObject::connect(
        m_backend, &FakeUserStoreBackend::checkVersionRequestReady,
        dummyObjectRaw,
        [ctx, promise = std::move(promise), dummyObjectRaw](
            const bool value, const std::exception_ptr & e,
            const QUuid requestId) {
            if (requestId != ctx->requestId()) {
                return;
            }

            dummyObjectRaw->deleteLater();

            if (!e) {
                promise->addResult(value);
                promise->finish();
                return;
            }

            ErrorString errorMessage = exceptionMessage(e);
            promise->setException(RuntimeError{std::move(errorMessage)});
            promise->finish();
        });
    Q_UNUSED(dummyObject.release()); // NOLINT

    QTimer::singleShot(
        0,
        [ctx = std::move(ctx), clientName = std::move(clientName),
         edamVersionMajor, edamVersionMinor, backend = m_backend] {
            backend->onCheckVersionRequest(
                clientName, edamVersionMajor, edamVersionMinor, ctx);
        });

    return future;
}

qevercloud::BootstrapInfo FakeUserStore::getBootstrapInfo(
    [[maybe_unused]] QString locale,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("getBootstrapInfo not implemented")}};
}

QFuture<qevercloud::BootstrapInfo> FakeUserStore::getBootstrapInfoAsync(
    [[maybe_unused]] QString locale,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("getBootstrapInfoAsync not implemented")}};
}

qevercloud::AuthenticationResult FakeUserStore::authenticateLongSession(
    [[maybe_unused]] QString username, [[maybe_unused]] QString password,
    [[maybe_unused]] QString consumerKey,
    [[maybe_unused]] QString consumerSecret,
    [[maybe_unused]] QString deviceIdentifier,
    [[maybe_unused]] QString deviceDescription,
    [[maybe_unused]] bool supportsTwoFactor,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("authenticateLongSession not implemented")}};
}

QFuture<qevercloud::AuthenticationResult>
    FakeUserStore::authenticateLongSessionAsync(
        [[maybe_unused]] QString username, [[maybe_unused]] QString password,
        [[maybe_unused]] QString consumerKey,
        [[maybe_unused]] QString consumerSecret,
        [[maybe_unused]] QString deviceIdentifier,
        [[maybe_unused]] QString deviceDescription,
        [[maybe_unused]] bool supportsTwoFactor,
        [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{ErrorString{
        QStringLiteral("authenticateLongSessionAsync not implemented")}};
}

qevercloud::AuthenticationResult FakeUserStore::completeTwoFactorAuthentication(
    [[maybe_unused]] QString oneTimeCode,
    [[maybe_unused]] QString deviceIdentifier,
    [[maybe_unused]] QString deviceDescription,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{ErrorString{
        QStringLiteral("completeTwoFactorAuthentication not implemented")}};
}

QFuture<qevercloud::AuthenticationResult>
    FakeUserStore::completeTwoFactorAuthenticationAsync(
        [[maybe_unused]] QString oneTimeCode,
        [[maybe_unused]] QString deviceIdentifier,
        [[maybe_unused]] QString deviceDescription,
        [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{ErrorString{QStringLiteral(
        "completeTwoFactorAuthenticationAsync not implemented")}};
}

void FakeUserStore::revokeLongSession(
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("revokeLongSession not implemented")}};
}

QFuture<void> FakeUserStore::revokeLongSessionAsync(
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("revokeLongSessionAsync not implemented")}};
}

qevercloud::AuthenticationResult FakeUserStore::authenticateToBusiness(
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("authenticateToBusiness not implemented")}};
}

QFuture<qevercloud::AuthenticationResult>
    FakeUserStore::authenticateToBusinessAsync(
        [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{ErrorString{
        QStringLiteral("authenticateToBusinessAsync not implemented")}};
}

qevercloud::User FakeUserStore::getUser(qevercloud::IRequestContextPtr ctx)
{
    ensureRequestContext(ctx);

    qevercloud::User result;
    EventLoopWithExitStatus::ExitStatus status =
        EventLoopWithExitStatus::ExitStatus::Failure;
    ErrorString error;
    {
        QTimer timer;
        timer.setInterval(gSyncMethodCallTimeout);
        timer.setSingleShot(true);

        EventLoopWithExitStatus loop;

        QObject::connect(
            &timer, &QTimer::timeout, &loop,
            &EventLoopWithExitStatus::exitAsTimeout);

        auto connection = QObject::connect(
            m_backend, &FakeUserStoreBackend::getUserRequestReady,
            [&](qevercloud::User user, const std::exception_ptr & e,
                const QUuid requestId) {
                if (requestId != ctx->requestId()) {
                    return;
                }

                if (!e) {
                    result = std::move(user);
                    QTimer::singleShot(0, [&loop] { loop.exitAsSuccess(); });
                    return;
                }

                ErrorString errorMessage = exceptionMessage(e);
                QTimer::singleShot(
                    0,
                    [&loop, errorMessage = std::move(errorMessage)]() mutable {
                        loop.exitAsFailureWithErrorString(
                            std::move(errorMessage));
                    });
            });

        QTimer::singleShot(0, [ctx = std::move(ctx), backend = m_backend] {
            backend->onGetUserRequest(ctx);
        });

        timer.start();
        loop.exec();

        QObject::disconnect(connection);
        status = loop.exitStatus();
        error = loop.errorDescription();
    }

    switch (status) {
    case EventLoopWithExitStatus::ExitStatus::Success:
        return result;
    case EventLoopWithExitStatus::ExitStatus::Failure:
        throw RuntimeError{std::move(error)};
    case EventLoopWithExitStatus::ExitStatus::Timeout:
        throw RuntimeError{
            ErrorString{QStringLiteral("Failed to get user in due time")}};
    }

    UNREACHABLE;
}

QFuture<qevercloud::User> FakeUserStore::getUserAsync(
    qevercloud::IRequestContextPtr ctx)
{
    ensureRequestContext(ctx);

    auto promise = std::make_shared<QPromise<qevercloud::User>>();
    auto future = promise->future();
    promise->start();

    auto dummyObject = std::make_unique<QObject>();
    auto * dummyObjectRaw = dummyObject.get();
    QObject::connect(
        m_backend, &FakeUserStoreBackend::getUserRequestReady, dummyObjectRaw,
        [ctx, promise = std::move(promise), dummyObjectRaw](
            qevercloud::User user, const std::exception_ptr & e,
            const QUuid requestId) {
            if (requestId != ctx->requestId()) {
                return;
            }

            dummyObjectRaw->deleteLater();

            if (!e) {
                promise->addResult(std::move(user));
                promise->finish();
                return;
            }

            ErrorString errorMessage = exceptionMessage(e);
            promise->setException(RuntimeError{std::move(errorMessage)});
            promise->finish();
        });
    Q_UNUSED(dummyObject.release()); // NOLINT

    QTimer::singleShot(0, [ctx = std::move(ctx), backend = m_backend] {
        backend->onGetUserRequest(ctx);
    });

    return future;
}

qevercloud::PublicUserInfo FakeUserStore::getPublicUserInfo(
    [[maybe_unused]] QString username,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("getPublicUserInfo not implemented")}};
}

QFuture<qevercloud::PublicUserInfo> FakeUserStore::getPublicUserInfoAsync(
    [[maybe_unused]] QString username,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("getPublicUserInfoAsync not implemented")}};
}

qevercloud::UserUrls FakeUserStore::getUserUrls(
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("getUserUrls not implemented")}};
}

QFuture<qevercloud::UserUrls> FakeUserStore::getUserUrlsAsync(
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("getUserUrlsAsync not implemented")}};
}

void FakeUserStore::inviteToBusiness(
    [[maybe_unused]] QString emailAddress,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("inviteToBusiness not implemented")}};
}

QFuture<void> FakeUserStore::inviteToBusinessAsync(
    [[maybe_unused]] QString emailAddress,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("inviteToBusinessAsync not implemented")}};
}

void FakeUserStore::removeFromBusiness(
    [[maybe_unused]] QString emailAddress,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("removeFromBusiness not implemented")}};
}

QFuture<void> FakeUserStore::removeFromBusinessAsync(
    [[maybe_unused]] QString emailAddress,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("removeFromBusinessAsync not implemented")}};
}

void FakeUserStore::updateBusinessUserIdentifier(
    [[maybe_unused]] QString oldEmailAddress,
    [[maybe_unused]] QString newEmailAddress,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{ErrorString{
        QStringLiteral("updateBusinessUserIdentifier not implemented")}};
}

QFuture<void> FakeUserStore::updateBusinessUserIdentifierAsync(
    [[maybe_unused]] QString oldEmailAddress,
    [[maybe_unused]] QString newEmailAddress,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{ErrorString{
        QStringLiteral("updateBusinessUserIdentifierAsync not implemented")}};
}

QList<qevercloud::UserProfile> FakeUserStore::listBusinessUsers(
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("listBusinessUsers not implemented")}};
}

QFuture<QList<qevercloud::UserProfile>> FakeUserStore::listBusinessUsersAsync(
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("listBusinessUsersAsync not implemented")}};
}

QList<qevercloud::BusinessInvitation> FakeUserStore::listBusinessInvitations(
    [[maybe_unused]] bool includeRequestedInvitations,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("listBusinessInvitations not implemented")}};
}

QFuture<QList<qevercloud::BusinessInvitation>>
    FakeUserStore::listBusinessInvitationsAsync(
        [[maybe_unused]] bool includeRequestedInvitations,
        [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{ErrorString{
        QStringLiteral("listBusinessInvitationsAsync not implemented")}};
}

qevercloud::AccountLimits FakeUserStore::getAccountLimits(
    [[maybe_unused]] qevercloud::ServiceLevel serviceLevel,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("getAccountLimits not implemented")}};
}

QFuture<qevercloud::AccountLimits> FakeUserStore::getAccountLimitsAsync(
    [[maybe_unused]] qevercloud::ServiceLevel serviceLevel,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("getAccountLimitsAsync not implemented")}};
}

void FakeUserStore::ensureRequestContext(
    qevercloud::IRequestContextPtr & ctx) const
{
    if (ctx) {
        return;
    }

    qevercloud::RequestContextBuilder builder;
    builder.setRequestId(QUuid::createUuid());

    if (m_ctx) {
        builder.setAuthenticationToken(m_ctx->authenticationToken());
        builder.setConnectionTimeout(m_ctx->connectionTimeout());
        builder.setIncreaseConnectionTimeoutExponentially(
            m_ctx->increaseConnectionTimeoutExponentially());
        builder.setMaxRetryCount(m_ctx->maxRequestRetryCount());
        builder.setCookies(m_ctx->cookies());
    }

    ctx = builder.build();
}

} // namespace quentier::synchronization::tests
