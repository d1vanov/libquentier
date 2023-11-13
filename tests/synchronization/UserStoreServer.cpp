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
#include <quentier/logging/QuentierLogger.h>

#include <qevercloud/services/UserStoreServer.h>

#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

#include <algorithm>

namespace quentier::synchronization::tests {

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
        auto * socket = m_tcpServer->nextPendingConnection();
        Q_ASSERT(socket);

        QUuid requestId = QUuid::createUuid();
        m_sockets[requestId] = socket;

        QNDEBUG(
            "synchronization::tests::UserStoreServer",
            "New connection: socket " << static_cast<const void *>(socket)
                                      << ", request id " << requestId);

        socket->setSocketOption(QAbstractSocket::KeepAliveOption, 1);
        socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);

        QObject::connect(
            socket, &QAbstractSocket::disconnected, this,
            [this, socket, requestId] {
                QNDEBUG(
                    "synchronization::tests::UserStoreServer",
                    "Socket " << static_cast<const void *>(socket)
                              << " corresponding to request id " << requestId
                              << " disconnected");

                const auto it = m_sockets.find(requestId);
                if (it != m_sockets.end()) {
                    m_sockets.erase(it);
                }

                socket->disconnect();
                socket->deleteLater();
            });

        if (!socket->waitForConnected()) {
            QNWARNING(
                "synchronization::tests::UserStoreServer",
                "Failed to establish connection for socket "
                    << static_cast<const void *>(socket)
                    << ", request id = " << requestId);
            QFAIL("Failed to establish connection");
            return;
        }

        QByteArray requestData = utils::readRequestBodyFromSocket(*socket);
        m_server->onRequest(std::move(requestData), requestId);
    });

    connectToQEverCloudServer();
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

std::optional<UserStoreServer::UserOrException> UserStoreServer::findUser(
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

void UserStoreServer::putUserException(
    const QString & authenticationToken, std::exception_ptr e)
{
    m_users[authenticationToken] = std::move(e);
}

void UserStoreServer::removeUser(const QString & authenticationToken)
{
    m_users.remove(authenticationToken);
}

void UserStoreServer::onCheckVersionRequest(
    const QString & clientName, qint16 edamVersionMajor,
    qint16 edamVersionMinor, const qevercloud::IRequestContextPtr & ctx)
{
    QNDEBUG(
        "synchronization::tests::UserStoreServer",
        "UserStoreServer::onCheckVersionRequest: client name = "
            << clientName << ", edam version major = " << edamVersionMajor
            << ", edam version minor = " << edamVersionMinor);

    Q_ASSERT(ctx);

    if (edamVersionMajor != m_edamVersionMajor) {
        QNWARNING(
            "synchronization::tests::UserStoreServer",
            "UserStoreServer::onCheckVersionRequest: expected EDAM major "
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
            "synchronization::tests::UserStoreServer",
            "UserStoreServer::onCheckVersionRequest: expected EDAM minor "
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

void UserStoreServer::onGetUserRequest(
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

void UserStoreServer::onRequestReady(
    const QByteArray & responseData, const QUuid requestId)
{
    QNDEBUG(
        "synchronization::tests::UserStoreServer",
        "UserStoreServer::onRequestReady: request id = " << requestId);

    const auto it = m_sockets.find(requestId);
    if (Q_UNLIKELY(it == m_sockets.end())) {
        QNWARNING(
            "synchronization::tests::UserStoreServer",
            "Cannot find socket for request id " << requestId);
        QFAIL("UserStoreServer: no socket on ready request");
        return;
    }

    auto * socket = it.value();
    if (!socket->isOpen()) {
        QNWARNING(
            "synchronization::tests::UserStoreServer",
            "Cannot respond to request with id " << requestId
                                                 << ": socket is closed");
        QFAIL("UserStoreServer: socket is closed on ready request");
        return;
    }

    QByteArray buffer;
    buffer.append("HTTP/1.1 200 OK\r\n");
    buffer.append("Content-Length: ");
    buffer.append(QString::number(responseData.size()).toUtf8());
    buffer.append("\r\n");
    buffer.append("Content-Type: application/x-thrift\r\n\r\n");
    buffer.append(responseData);

    if (!utils::writeBufferToSocket(buffer, *socket)) {
        QNWARNING(
            "synchronization::tests::UserStoreServer",
            "Cannot respond to request with id "
                << requestId
                << ": cannot write response data to socket; last socket error "
                << "= (" << socket->error() << ") " << socket->errorString());
        QFAIL("Failed to write response to socket");
    }
}

void UserStoreServer::connectToQEverCloudServer()
{
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
        this, &UserStoreServer::checkVersionRequestReady, m_server,
        &qevercloud::UserStoreServer::onCheckVersionRequestReady);

    QObject::connect(
        this, &UserStoreServer::getUserRequestReady, m_server,
        &qevercloud::UserStoreServer::onGetUserRequestReady);
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
