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

#include "NoteStoreServer.h"

#include "FakeNoteStoreBackend.h"
#include "utils/HttpUtils.h"

#include <quentier/exception/RuntimeError.h>
#include <quentier/logging/QuentierLogger.h>

#include <qevercloud/services/NoteStoreServer.h>

#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

namespace quentier::synchronization::tests {

NoteStoreServer::NoteStoreServer(
    FakeNoteStoreBackend * backend, QObject * parent) :
    QObject(parent), m_backend{backend}, m_tcpServer{new QTcpServer(this)},
    m_server{new qevercloud::NoteStoreServer(this)}
{
    bool res = m_tcpServer->listen(QHostAddress::LocalHost);
    if (Q_UNLIKELY(!res)) {
        throw RuntimeError{
            ErrorString{QString::fromUtf8("Failed to set up a TCP server for "
                                          "NoteStore on localhost: (%1) "
                                          "%2")
                            .arg(m_tcpServer->serverError())
                            .arg(m_tcpServer->errorString())}};
    }

    QNDEBUG(
        "tests::synchronization::NoteStoreServer",
        "NoteStoreServer: listening on port " << m_tcpServer->serverPort());

    QObject::connect(m_tcpServer, &QTcpServer::newConnection, this, [this] {
        auto * socket = m_tcpServer->nextPendingConnection();
        Q_ASSERT(socket);

        QUuid requestId = QUuid::createUuid();
        m_sockets[requestId] = socket;

        QNDEBUG(
            "tests::synchronization::NoteStoreServer",
            "New connection: socket " << static_cast<const void *>(socket)
                                      << ", request id " << requestId);

        socket->setSocketOption(QAbstractSocket::KeepAliveOption, 1);
        socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);

        QObject::connect(
            socket, &QAbstractSocket::disconnected, this,
            [this, socket, requestId] {
                QNDEBUG(
                    "tests::synchronization::NoteStoreServer",
                    "Socket " << static_cast<const void *>(socket)
                              << " corresponding to request id " << requestId
                              << " disconnected");

                if (const auto it = m_sockets.find(requestId);
                    it != m_sockets.end())
                {
                    m_sockets.erase(it);
                }

                if (!m_backend.isNull()) {
                    m_backend->removeUriForRequestId(requestId);
                }

                socket->disconnect();
                socket->deleteLater();
            });

        if (!socket->waitForConnected()) {
            QNWARNING(
                "tests::synchronization::NoteStoreServer",
                "Failed to establish connection for socket "
                    << static_cast<const void *>(socket)
                    << ", request id = " << requestId);
            QFAIL("Failed to establish connection");
            return;
        }

        utils::HttpRequestData requestData =
            utils::readRequestDataFromSocket(*socket);
        if (requestData.body.isEmpty()) {
            QNWARNING(
                "tests::synchronization::NoteStoreServer",
                "Failed to read request body for socket "
                    << static_cast<const void *>(socket));
            QFAIL("Failed to read request body");
            return;
        }

        if (requestData.uri.size() > 1 && !m_backend.isNull()) {
            m_backend->setUriForRequestId(requestId, requestData.uri.mid(1));
        }

        m_server->onRequest(std::move(requestData.body), requestId);
    });

    QObject::connect(
        m_tcpServer, &QTcpServer::acceptError, this,
        [](const QAbstractSocket::SocketError error) {
            QNWARNING(
                "tests::synchronization::NoteStoreServer",
                "Error accepting connection: " << error);
        });

    connectToQEverCloudServer();
}

NoteStoreServer::~NoteStoreServer()
{
    QNDEBUG("tests::synchronization::NoteStoreServer", "NoteStoreServer: dtor");
}

quint16 NoteStoreServer::port() const noexcept
{
    return m_tcpServer->serverPort();
}

void NoteStoreServer::onRequestReady(
    const QByteArray & responseData, QUuid requestId)
{
    QNDEBUG(
        "tests::synchronization::NoteStoreServer",
        "NoteStoreServer::onRequestReady: request id = " << requestId);

    const auto it = m_sockets.find(requestId);
    if (Q_UNLIKELY(it == m_sockets.end())) {
        QNWARNING(
            "tests::synchronization::NoteStoreServer",
            "Cannot find socket for request id " << requestId);
        QFAIL("NoteStoreServer: no socket on ready request");
        return;
    }

    auto * socket = it.value();
    if (!socket->isOpen()) {
        QNWARNING(
            "tests::synchronization::NoteStoreServer",
            "Cannot respond to request with id " << requestId
                                                 << ": socket is closed");
        QFAIL("NoteStoreServer: socket is closed on ready request");
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
            "tests::synchronization::NoteStoreServer",
            "Cannot respond to request with id "
                << requestId
                << ": cannot write response data to socket; last socket error "
                << "= (" << socket->error() << ") " << socket->errorString());
        QFAIL("Failed to write response to socket");
    }
}

void NoteStoreServer::connectToQEverCloudServer()
{
    // 1. Connect QEverCloud server's request ready signals to local slot
    QObject::connect(
        m_server, &qevercloud::NoteStoreServer::createNotebookRequestReady,
        this, &NoteStoreServer::onRequestReady);

    QObject::connect(
        m_server, &qevercloud::NoteStoreServer::updateNotebookRequestReady,
        this, &NoteStoreServer::onRequestReady);

    QObject::connect(
        m_server, &qevercloud::NoteStoreServer::createNoteRequestReady, this,
        &NoteStoreServer::onRequestReady);

    QObject::connect(
        m_server, &qevercloud::NoteStoreServer::updateNoteRequestReady, this,
        &NoteStoreServer::onRequestReady);

    QObject::connect(
        m_server, &qevercloud::NoteStoreServer::createTagRequestReady, this,
        &NoteStoreServer::onRequestReady);

    QObject::connect(
        m_server, &qevercloud::NoteStoreServer::updateTagRequestReady, this,
        &NoteStoreServer::onRequestReady);

    QObject::connect(
        m_server, &qevercloud::NoteStoreServer::createSearchRequestReady, this,
        &NoteStoreServer::onRequestReady);

    QObject::connect(
        m_server, &qevercloud::NoteStoreServer::updateSearchRequestReady, this,
        &NoteStoreServer::onRequestReady);

    QObject::connect(
        m_server, &qevercloud::NoteStoreServer::getSyncStateRequestReady, this,
        &NoteStoreServer::onRequestReady);

    QObject::connect(
        m_server,
        &qevercloud::NoteStoreServer::getLinkedNotebookSyncStateRequestReady,
        this, &NoteStoreServer::onRequestReady);

    QObject::connect(
        m_server,
        &qevercloud::NoteStoreServer::getFilteredSyncChunkRequestReady, this,
        &NoteStoreServer::onRequestReady);

    QObject::connect(
        m_server,
        &qevercloud::NoteStoreServer::getLinkedNotebookSyncChunkRequestReady,
        this, &NoteStoreServer::onRequestReady);

    QObject::connect(
        m_server,
        &qevercloud::NoteStoreServer::getNoteWithResultSpecRequestReady, this,
        &NoteStoreServer::onRequestReady);

    QObject::connect(
        m_server, &qevercloud::NoteStoreServer::getResourceRequestReady, this,
        &NoteStoreServer::onRequestReady);

    QObject::connect(
        m_server,
        &qevercloud::NoteStoreServer::authenticateToSharedNotebookRequestReady,
        this, &NoteStoreServer::onRequestReady);

    // 2. Connect incoming request signals to backend slots
    QObject::connect(
        m_server, &qevercloud::NoteStoreServer::createNotebookRequest,
        m_backend.data(), &FakeNoteStoreBackend::onCreateNotebookRequest);

    QObject::connect(
        m_server, &qevercloud::NoteStoreServer::updateNotebookRequest,
        m_backend.data(), &FakeNoteStoreBackend::onUpdateNotebookRequest);

    QObject::connect(
        m_server, &qevercloud::NoteStoreServer::createNoteRequest,
        m_backend.data(), &FakeNoteStoreBackend::onCreateNoteRequest);

    QObject::connect(
        m_server, &qevercloud::NoteStoreServer::updateNoteRequest,
        m_backend.data(), &FakeNoteStoreBackend::onUpdateNoteRequest);

    QObject::connect(
        m_server, &qevercloud::NoteStoreServer::createTagRequest,
        m_backend.data(), &FakeNoteStoreBackend::onCreateTagRequest);

    QObject::connect(
        m_server, &qevercloud::NoteStoreServer::updateTagRequest,
        m_backend.data(), &FakeNoteStoreBackend::onUpdateTagRequest);

    QObject::connect(
        m_server, &qevercloud::NoteStoreServer::createSearchRequest,
        m_backend.data(), &FakeNoteStoreBackend::onCreateSavedSearchRequest);

    QObject::connect(
        m_server, &qevercloud::NoteStoreServer::updateSearchRequest,
        m_backend.data(), &FakeNoteStoreBackend::onUpdateSavedSearchRequest);

    QObject::connect(
        m_server, &qevercloud::NoteStoreServer::getSyncStateRequest,
        m_backend.data(), &FakeNoteStoreBackend::onGetSyncStateRequest);

    QObject::connect(
        m_server,
        &qevercloud::NoteStoreServer::getLinkedNotebookSyncStateRequest,
        m_backend.data(),
        &FakeNoteStoreBackend::onGetLinkedNotebookSyncStateRequest);

    QObject::connect(
        m_server, &qevercloud::NoteStoreServer::getFilteredSyncChunkRequest,
        m_backend.data(), &FakeNoteStoreBackend::onGetFilteredSyncChunkRequest);

    QObject::connect(
        m_server,
        &qevercloud::NoteStoreServer::getLinkedNotebookSyncChunkRequest,
        m_backend.data(),
        &FakeNoteStoreBackend::onGetLinkedNotebookSyncChunkRequest);

    QObject::connect(
        m_server, &qevercloud::NoteStoreServer::getNoteWithResultSpecRequest,
        m_backend.data(),
        &FakeNoteStoreBackend::onGetNoteWithResultSpecRequest);

    QObject::connect(
        m_server, &qevercloud::NoteStoreServer::getResourceRequest,
        m_backend.data(), &FakeNoteStoreBackend::onGetResourceRequest);

    QObject::connect(
        m_server,
        &qevercloud::NoteStoreServer::authenticateToSharedNotebookRequest,
        m_backend.data(),
        &FakeNoteStoreBackend::onAuthenticateToSharedNotebookRequest);

    // 3. Connect local ready signals to QEverCloud server's slots
    QObject::connect(
        m_backend.data(), &FakeNoteStoreBackend::createNotebookRequestReady,
        m_server, &qevercloud::NoteStoreServer::onCreateNotebookRequestReady);

    QObject::connect(
        m_backend.data(), &FakeNoteStoreBackend::updateNotebookRequestReady,
        m_server, &qevercloud::NoteStoreServer::onUpdateNotebookRequestReady);

    QObject::connect(
        m_backend.data(), &FakeNoteStoreBackend::createNoteRequestReady,
        m_server, &qevercloud::NoteStoreServer::onCreateNoteRequestReady);

    QObject::connect(
        m_backend.data(), &FakeNoteStoreBackend::updateNoteRequestReady,
        m_server, &qevercloud::NoteStoreServer::onUpdateNoteRequestReady);

    QObject::connect(
        m_backend.data(), &FakeNoteStoreBackend::createTagRequestReady,
        m_server, &qevercloud::NoteStoreServer::onCreateTagRequestReady);

    QObject::connect(
        m_backend.data(), &FakeNoteStoreBackend::updateTagRequestReady,
        m_server, &qevercloud::NoteStoreServer::onUpdateTagRequestReady);

    QObject::connect(
        m_backend.data(), &FakeNoteStoreBackend::createSavedSearchRequestReady,
        m_server, &qevercloud::NoteStoreServer::onCreateSearchRequestReady);

    QObject::connect(
        m_backend.data(), &FakeNoteStoreBackend::updateSavedSearchRequestReady,
        m_server, &qevercloud::NoteStoreServer::onUpdateSearchRequestReady);

    QObject::connect(
        m_backend.data(), &FakeNoteStoreBackend::getSyncStateRequestReady,
        m_server, &qevercloud::NoteStoreServer::onGetSyncStateRequestReady);

    QObject::connect(
        m_backend.data(),
        &FakeNoteStoreBackend::getLinkedNotebookSyncStateRequestReady, m_server,
        &qevercloud::NoteStoreServer::onGetLinkedNotebookSyncStateRequestReady);

    QObject::connect(
        m_backend.data(),
        &FakeNoteStoreBackend::getFilteredSyncChunkRequestReady, m_server,
        &qevercloud::NoteStoreServer::onGetFilteredSyncChunkRequestReady);

    QObject::connect(
        m_backend.data(),
        &FakeNoteStoreBackend::getLinkedNotebookSyncChunkRequestReady, m_server,
        &qevercloud::NoteStoreServer::onGetLinkedNotebookSyncChunkRequestReady);

    QObject::connect(
        m_backend.data(),
        &FakeNoteStoreBackend::getNoteWithResultSpecRequestReady, m_server,
        &qevercloud::NoteStoreServer::onGetNoteWithResultSpecRequestReady);

    QObject::connect(
        m_backend.data(), &FakeNoteStoreBackend::getResourceRequestReady,
        m_server, &qevercloud::NoteStoreServer::onGetResourceRequestReady);

    QObject::connect(
        m_backend.data(),
        &FakeNoteStoreBackend::authenticateToSharedNotebookRequestReady,
        m_server,
        &qevercloud::NoteStoreServer::
            onAuthenticateToSharedNotebookRequestReady);
}

} // namespace quentier::synchronization::tests
