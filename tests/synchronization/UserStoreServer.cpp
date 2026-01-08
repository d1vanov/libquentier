/*
 * Copyright 2023-2024 Dmitry Ivanov
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

#include "FakeUserStoreBackend.h"
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
    FakeUserStoreBackend * backend, QObject * parent) :
    QObject(parent), m_backend{backend}, m_tcpServer{new QTcpServer(this)},
    m_server{new qevercloud::UserStoreServer(this)}
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
        m_server, &qevercloud::UserStoreServer::checkVersionRequest, m_backend,
        &FakeUserStoreBackend::onCheckVersionRequest);

    QObject::connect(
        m_server, &qevercloud::UserStoreServer::getUserRequest, m_backend,
        &FakeUserStoreBackend::onGetUserRequest);

    QObject::connect(
        m_backend, &FakeUserStoreBackend::checkVersionRequestReady, m_server,
        &qevercloud::UserStoreServer::onCheckVersionRequestReady);

    QObject::connect(
        m_backend, &FakeUserStoreBackend::getUserRequestReady, m_server,
        &qevercloud::UserStoreServer::onGetUserRequestReady);
}

} // namespace quentier::synchronization::tests
