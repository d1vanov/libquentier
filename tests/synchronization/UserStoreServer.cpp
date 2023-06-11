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

#include "UserStoreServer.h"

#include "utils/HttpUtils.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/RuntimeError.h>
#include <quentier/utility/Unreachable.h>

#include <qevercloud/services/UserStoreServer.h>

#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest/QtTest>

#include <algorithm>

namespace quentier::synchronization::tests {

namespace {

[[nodiscard]] QString serviceLevelToString(
    const qevercloud::ServiceLevel serviceLevel)
{
    switch (serviceLevel) {
    case qevercloud::ServiceLevel::BASIC:
        return QStringLiteral("BASIC");
    case qevercloud::ServiceLevel::PLUS:
        return QStringLiteral("PLUS");
    case qevercloud::ServiceLevel::PREMIUM:
        return QStringLiteral("PREMIUM");
    case qevercloud::ServiceLevel::BUSINESS:
        return QStringLiteral("BUSINESS");
    }

    UNREACHABLE;
}

} // namespace

UserStoreServer::UserStoreServer(
    QString authenticationToken, QList<QNetworkCookie> cookies,
    QObject * parent) :
    QObject(parent),
    // clang-format off
    m_authenticationToken{std::move(authenticationToken)},
    m_cookies{std::move(cookies)},
    m_tcpServer{new QTcpServer(this)},
    m_server{new qevercloud::UserStoreServer(this)}
// clang-format on
{
    bool res = m_tcpServer->listen(QHostAddress::LocalHost);
    if (Q_UNLIKELY(!res)) {
        throw RuntimeError{
            ErrorString{QString::fromUtf8("Failed to set up a TCP server for "
                                          "UserStore on localhost: (%1) "
                                          "%2")
                            .arg(m_tcpServer->serverError())
                            .arg(m_tcpServer->errorString())}};
    }

    QObject::connect(m_tcpServer, &QTcpServer::newConnection, this, [this] {
        m_tcpSocket = m_tcpServer->nextPendingConnection();
        Q_ASSERT(m_tcpSocket);

        QObject::connect(
            m_tcpSocket, &QAbstractSocket::disconnected, m_tcpSocket,
            &QAbstractSocket::deleteLater);
        if (!m_tcpSocket->waitForConnected()) {
            QFAIL("Failed to establish connection");
        }

        QByteArray requestData = utils::readRequestBodyFromSocket(*m_tcpSocket);

        m_server->onRequest(std::move(requestData));
    });

    QObject::connect(
        m_server, &qevercloud::UserStoreServer::checkVersionRequestReady, this,
        &UserStoreServer::onRequestReady);

    QObject::connect(
        m_server, &qevercloud::UserStoreServer::getUserRequestReady, this,
        &UserStoreServer::onRequestReady);

    QObject::connect(
        m_server, &qevercloud::UserStoreServer::getAccountLimitsRequestReady,
        this, &UserStoreServer::onRequestReady);

    QObject::connect(
        m_server, &qevercloud::UserStoreServer::checkVersionRequest, this,
        &UserStoreServer::onCheckVersionRequest);

    QObject::connect(
        m_server, &qevercloud::UserStoreServer::getUserRequest, this,
        &UserStoreServer::onGetUserRequest);

    QObject::connect(
        m_server, &qevercloud::UserStoreServer::getAccountLimitsRequest, this,
        &UserStoreServer::onGetAccountLimitsRequest);

    QObject::connect(
        this, &UserStoreServer::checkVersionRequestReady, m_server,
        &qevercloud::UserStoreServer::onCheckVersionRequestReady);

    QObject::connect(
        this, &UserStoreServer::getUserRequestReady, m_server,
        &qevercloud::UserStoreServer::onGetUserRequestReady);

    QObject::connect(
        this, &UserStoreServer::getAccountLimitsRequestReady, m_server,
        &qevercloud::UserStoreServer::onGetAccountLimitsRequestReady);
}

UserStoreServer::~UserStoreServer() = default;

quint16 UserStoreServer::port() const noexcept
{
    return m_tcpServer->serverPort();
}

qint16 UserStoreServer::edamVersionMajor() const noexcept
{
    return m_edamVersionMajor;
}

void UserStoreServer::setEdamVersionMajor(
    const qint16 edamVersionMajor) noexcept
{
    m_edamVersionMajor = edamVersionMajor;
}

qint16 UserStoreServer::edamVersionMinor() const noexcept
{
    return m_edamVersionMinor;
}

void UserStoreServer::setEdamVersionMinor(
    const qint16 edamVersionMinor) noexcept
{
    m_edamVersionMinor = edamVersionMinor;
}

std::optional<qevercloud::AccountLimits> UserStoreServer::findAccountLimits(
    const qevercloud::ServiceLevel serviceLevel) const
{
    const auto it = m_accountLimits.constFind(serviceLevel);
    if (it != m_accountLimits.constEnd()) {
        return it.value();
    }

    return std::nullopt;
}

void UserStoreServer::setAccountLimits(
    const qevercloud::ServiceLevel serviceLevel,
    qevercloud::AccountLimits limits)
{
    m_accountLimits[serviceLevel] = std::move(limits);
}

void UserStoreServer::removeAccountLimits(
    const qevercloud::ServiceLevel serviceLevel)
{
    m_accountLimits.remove(serviceLevel);
}

std::optional<qevercloud::User> UserStoreServer::findUser(
    const QString & authenticationToken) const
{
    const auto it = m_users.constFind(authenticationToken);
    if (it != m_users.constEnd()) {
        return it.value();
    }

    return std::nullopt;
}

void UserStoreServer::putUser(
    const QString & authenticationToken, qevercloud::User user)
{
    m_users[authenticationToken] = std::move(user);
}

void UserStoreServer::removeUser(const QString & authenticationToken)
{
    m_users.remove(authenticationToken);
}

void UserStoreServer::onCheckVersionRequest(
    [[maybe_unused]] const QString & clientName, qint16 edamVersionMajor,
    qint16 edamVersionMinor, const qevercloud::IRequestContextPtr & ctx)
{
    if (auto e = checkAuthentication(ctx)) {
        Q_EMIT checkVersionRequestReady(false, std::move(e));
        return;
    }

    if (edamVersionMajor != m_edamVersionMajor) {
        Q_EMIT checkVersionRequestReady(
            false,
            std::make_exception_ptr(RuntimeError{
                ErrorString{QString::fromUtf8(
                                "Wrong EDAM version major, expected %1, got %2")
                                .arg(m_edamVersionMajor, edamVersionMajor)}}));
        return;
    }

    if (edamVersionMinor != m_edamVersionMinor) {
        Q_EMIT checkVersionRequestReady(
            false,
            std::make_exception_ptr(RuntimeError{
                ErrorString{QString::fromUtf8(
                                "Wrong EDAM version minor, expected %1, got %2")
                                .arg(m_edamVersionMinor, edamVersionMinor)}}));
        return;
    }

    Q_EMIT checkVersionRequestReady(true, nullptr);
}

void UserStoreServer::onGetUserRequest(
    const qevercloud::IRequestContextPtr & ctx)
{
    if (auto e = checkAuthentication(ctx)) {
        Q_EMIT getUserRequestReady(qevercloud::User{}, std::move(e));
        return;
    }

    Q_ASSERT(ctx);
    const auto it = m_users.constFind(ctx->authenticationToken());
    if (it != m_users.constEnd()) {
        Q_EMIT getUserRequestReady(it.value(), nullptr);
        return;
    }

    Q_EMIT getUserRequestReady(
        qevercloud::User{},
        std::make_exception_ptr(RuntimeError{ErrorString{
            QString::fromUtf8(
                "Could not find user corresponding to authentication token %1")
                .arg(ctx->authenticationToken())}}));
}

void UserStoreServer::onGetAccountLimitsRequest(
    const qevercloud::ServiceLevel serviceLevel,
    const qevercloud::IRequestContextPtr & ctx)
{
    if (auto e = checkAuthentication(ctx)) {
        Q_EMIT getAccountLimitsRequestReady(
            qevercloud::AccountLimits{}, std::move(e));
        return;
    }

    const auto it = m_accountLimits.constFind(serviceLevel);
    if (it != m_accountLimits.constEnd()) {
        Q_EMIT getAccountLimitsRequestReady(it.value(), nullptr);
        return;
    }

    Q_EMIT getAccountLimitsRequestReady(
        qevercloud::AccountLimits{},
        std::make_exception_ptr(RuntimeError{ErrorString{
            QString::fromUtf8(
                "Could not find account limits corresponding to service level "
                "%1")
                .arg(serviceLevelToString(serviceLevel))}}));
}

void UserStoreServer::onRequestReady(const QByteArray & responseData)
{
    if (Q_UNLIKELY(!m_tcpSocket)) {
        QFAIL("UserStoreServer: no socket on ready request");
        return;
    }

    QByteArray buffer;
    buffer.append("HTTP/1.1 200 OK\r\n");
    buffer.append("Content-Length: ");
    buffer.append(QString::number(responseData.size()).toUtf8());
    buffer.append("\r\n");
    buffer.append("Content-Type: application/x-thrift\r\n\r\n");
    buffer.append(responseData);

    if (!utils::writeBufferToSocket(buffer, *m_tcpSocket)) {
        QFAIL("Failed to write response to socket");
    }
}

std::exception_ptr UserStoreServer::checkAuthentication(
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
