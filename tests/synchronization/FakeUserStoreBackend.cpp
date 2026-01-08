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

#include "FakeUserStoreBackend.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/RuntimeError.h>
#include <quentier/logging/QuentierLogger.h>

#include <qevercloud/IRequestContext.h>

namespace quentier::synchronization::tests {

FakeUserStoreBackend::FakeUserStoreBackend(
    QString authenticationToken, QList<QNetworkCookie> cookies,
    QObject * parent) :
    QObject(parent), m_authenticationToken{std::move(authenticationToken)},
    m_cookies{std::move(cookies)}
{}

FakeUserStoreBackend::~FakeUserStoreBackend() = default;

qint16 FakeUserStoreBackend::edamVersionMajor() const noexcept
{
    return m_edamVersionMajor;
}

void FakeUserStoreBackend::setEdamVersionMajor(
    const qint16 edamVersionMajor) noexcept
{
    m_edamVersionMajor = edamVersionMajor;
}

qint16 FakeUserStoreBackend::edamVersionMinor() const noexcept
{
    return m_edamVersionMinor;
}

void FakeUserStoreBackend::setEdamVersionMinor(
    const qint16 edamVersionMinor) noexcept
{
    m_edamVersionMinor = edamVersionMinor;
}

std::optional<FakeUserStoreBackend::UserOrException>
    FakeUserStoreBackend::findUser(const QString & authenticationToken) const
{
    const auto it = m_users.constFind(authenticationToken);
    if (it != m_users.constEnd()) {
        return it.value();
    }

    return std::nullopt;
}

void FakeUserStoreBackend::putUser(
    const QString & authenticationToken, qevercloud::User user)
{
    m_users[authenticationToken] = std::move(user);
}

void FakeUserStoreBackend::putUserException(
    const QString & authenticationToken, std::exception_ptr e)
{
    m_users[authenticationToken] = std::move(e);
}

void FakeUserStoreBackend::removeUser(const QString & authenticationToken)
{
    m_users.remove(authenticationToken);
}

void FakeUserStoreBackend::onCheckVersionRequest(
    const QString & clientName, qint16 edamVersionMajor,
    qint16 edamVersionMinor, const qevercloud::IRequestContextPtr & ctx)
{
    QNDEBUG(
        "synchronization::tests::FakeUserStoreBackend",
        "FakeUserStoreBackend::onCheckVersionRequest: client name = "
            << clientName << ", edam version major = " << edamVersionMajor
            << ", edam version minor = " << edamVersionMinor);

    Q_ASSERT(ctx);

    if (edamVersionMajor != m_edamVersionMajor) {
        QNWARNING(
            "synchronization::tests::FakeUserStoreBackend",
            "FakeUserStoreBackend::onCheckVersionRequest: expected EDAM major "
            "version "
                << m_edamVersionMajor);

        Q_EMIT checkVersionRequestReady(
            false,
            std::make_exception_ptr(RuntimeError{
                ErrorString{QString::fromUtf8(
                                "Wrong EDAM version major, expected %1, got %2")
                                .arg(m_edamVersionMajor, edamVersionMajor)}}),
            ctx->requestId());
        return;
    }

    if (edamVersionMinor != m_edamVersionMinor) {
        QNWARNING(
            "synchronization::tests::FakeUserStoreBackend",
            "FakeUserStoreBackend::onCheckVersionRequest: expected EDAM minor "
            "version "
                << m_edamVersionMinor);

        Q_EMIT checkVersionRequestReady(
            false,
            std::make_exception_ptr(RuntimeError{
                ErrorString{QString::fromUtf8(
                                "Wrong EDAM version minor, expected %1, got %2")
                                .arg(m_edamVersionMinor, edamVersionMinor)}}),
            ctx->requestId());
        return;
    }

    Q_EMIT checkVersionRequestReady(true, nullptr, ctx->requestId());
}

void FakeUserStoreBackend::onGetUserRequest(
    const qevercloud::IRequestContextPtr & ctx)
{
    Q_ASSERT(ctx);

    if (auto e = checkAuthentication(ctx)) {
        Q_EMIT getUserRequestReady(
            qevercloud::User{}, std::move(e), ctx->requestId());
        return;
    }

    const auto it = m_users.constFind(ctx->authenticationToken());
    if (it != m_users.constEnd()) {
        if (std::holds_alternative<qevercloud::User>(it.value())) {
            Q_EMIT getUserRequestReady(
                std::get<qevercloud::User>(it.value()), nullptr,
                ctx->requestId());
        }
        else {
            Q_EMIT getUserRequestReady(
                qevercloud::User{}, std::get<std::exception_ptr>(it.value()),
                ctx->requestId());
        }
        return;
    }

    Q_EMIT getUserRequestReady(
        qevercloud::User{},
        std::make_exception_ptr(RuntimeError{ErrorString{
            QString::fromUtf8(
                "Could not find user corresponding to authentication token %1")
                .arg(ctx->authenticationToken())}}),
        ctx->requestId());
}

std::exception_ptr FakeUserStoreBackend::checkAuthentication(
    const qevercloud::IRequestContextPtr & ctx) const
{
    if (!ctx) {
        return std::make_exception_ptr(InvalidArgument{
            ErrorString{QStringLiteral("Request context is null")}});
    }

    if (ctx->authenticationToken() != m_authenticationToken) {
        return std::make_exception_ptr(InvalidArgument{ErrorString{
            QString::fromUtf8(
                "Invalid authentication token, expected %1, got %2")
                .arg(m_authenticationToken, ctx->authenticationToken())}});
    }

    const auto cookies = ctx->cookies();
    for (const auto & cookie: m_cookies) {
        const auto it = std::find_if(
            cookies.constBegin(), cookies.constEnd(),
            [&cookie](const QNetworkCookie & c) {
                return c.name() == cookie.name();
            });
        if (it == cookies.constEnd()) {
            return std::make_exception_ptr(InvalidArgument{ErrorString{
                QString::fromUtf8(
                    "Missing network cookie in request: expected to find "
                    "cookie with name %1 but haven't found it")
                    .arg(QString::fromUtf8(cookie.name()))}});
        }

        if (it->value() != cookie.value()) {
            return std::make_exception_ptr(InvalidArgument{ErrorString{
                QString::fromUtf8(
                    "Network cookie contains unexpected value: expected for "
                    "cookie with name %1 to have value %2 but got %3")
                    .arg(
                        QString::fromUtf8(cookie.name()),
                        QString::fromUtf8(cookie.value()),
                        QString::fromUtf8(it->value()))}});
        }
    }

    return nullptr;
}

} // namespace quentier::synchronization::tests
