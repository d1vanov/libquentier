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

#include "NoteStoreServer.h"

#include "utils/HttpUtils.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/RuntimeError.h>

#include <qevercloud/services/NoteStoreServer.h>

#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest/QtTest>

namespace quentier::synchronization::tests {

NoteStoreServer::NoteStoreServer(
    QString authenticationToken, QList<QNetworkCookie> cookies,
    QHash<qevercloud::Guid, QString> linkedNotebookAuthTokensByGuid,
    QObject * parent) :
    QObject(parent),
    m_authenticationToken{std::move(authenticationToken)},
    m_cookies{std::move(cookies)},
    m_linkedNotebookAuthTokensByGuid{std::move(linkedNotebookAuthTokensByGuid)}
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

    connectToQEverCloudServer();
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
        m_server, &qevercloud::NoteStoreServer::createNoteRequestReady,
        this, &NoteStoreServer::onRequestReady);

    QObject::connect(
        m_server, &qevercloud::NoteStoreServer::updateNoteRequestReady,
        this, &NoteStoreServer::onRequestReady);

    QObject::connect(
        m_server, &qevercloud::NoteStoreServer::createTagRequestReady,
        this, &NoteStoreServer::onRequestReady);

    QObject::connect(
        m_server, &qevercloud::NoteStoreServer::updateTagRequestReady,
        this, &NoteStoreServer::onRequestReady);

    QObject::connect(
        m_server, &qevercloud::NoteStoreServer::createSearchRequestReady,
        this, &NoteStoreServer::onRequestReady);

    QObject::connect(
        m_server, &qevercloud::NoteStoreServer::updateSearchRequestReady,
        this, &NoteStoreServer::onRequestReady);

    QObject::connect(
        m_server, &qevercloud::NoteStoreServer::getSyncStateRequestReady,
        this, &NoteStoreServer::onRequestReady);

    QObject::connect(
        m_server,
        &qevercloud::NoteStoreServer::getLinkedNotebookSyncStateRequestReady,
        this, &NoteStoreServer::onRequestReady);

    QObject::connect(
        m_server,
        &qevercloud::NoteStoreServer::getFilteredSyncChunkRequestReady,
        this, &NoteStoreServer::onRequestReady);

    QObject::connect(
        m_server,
        &qevercloud::NoteStoreServer::getLinkedNotebookSyncChunkRequestReady,
        this, &NoteStoreServer::onRequestReady);

    QObject::connect(
        m_server,
        &qevercloud::NoteStoreServer::getNoteWithResultSpecRequestReady,
        this, &NoteStoreServer::onRequestReady);

    QObject::connect(
        m_server, &qevercloud::NoteStoreServer::getResourceRequestReady,
        this, &NoteStoreServer::onRequestReady);

    QObject::connect(
        m_server,
        &qevercloud::NoteStoreServer::authenticateToSharedNotebookRequestReady,
        this, &NoteStoreServer::onRequestReady);

    // 2. Connect incoming request signals to local slots
    QObject::connect(
        m_server, &qevercloud::NoteStoreServer::createNotebookRequest,
        this, &NoteStoreServer::onCreateNotebookRequest);

    QObject::connect(
        m_server, &qevercloud::NoteStoreServer::updateNotebookRequest,
        this, &NoteStoreServer::onUpdateNotebookRequest);

    QObject::connect(
        m_server, &qevercloud::NoteStoreServer::createNoteRequest,
        this, &NoteStoreServer::onCreateNoteRequest);

    QObject::connect(
        m_server, &qevercloud::NoteStoreServer::updateNoteRequest,
        this, &NoteStoreServer::onUpdateNoteRequest);

    QObject::connect(
        m_server, &qevercloud::NoteStoreServer::createTagRequest,
        this, &NoteStoreServer::onCreateTagRequest);

    QObject::connect(
        m_server, &qevercloud::NoteStoreServer::updateTagRequest,
        this, &NoteStoreServer::onUpdateTagRequest);

    QObject::connect(
        m_server, &qevercloud::NoteStoreServer::createSearchRequest,
        this, &NoteStoreServer::onCreateSavedSearchRequest);

    QObject::connect(
        m_server, &qevercloud::NoteStoreServer::updateSearchRequest,
        this, &NoteStoreServer::onUpdateSavedSearchRequest);

    QObject::connect(
        m_server, &qevercloud::NoteStoreServer::getSyncStateRequest,
        this, &NoteStoreServer::onGetSyncStateRequest);

    QObject::connect(
        m_server,
        &qevercloud::NoteStoreServer::getLinkedNotebookSyncStateRequest,
        this, &NoteStoreServer::onGetLinkedNotebookSyncStateRequest);

    QObject::connect(
        m_server, &qevercloud::NoteStoreServer::getFilteredSyncChunkRequest,
        this, &NoteStoreServer::onGetFilteredSyncChunkRequest);

    QObject::connect(
        m_server,
        &qevercloud::NoteStoreServer::getLinkedNotebookSyncChunkRequest,
        this, &NoteStoreServer::onGetLinkedNotebookSyncChunkRequest);

    QObject::connect(
        m_server, &qevercloud::NoteStoreServer::getNoteWithResultSpecRequest,
        this, &NoteStoreServer::onGetNoteWithResultSpecRequest);

    QObject::connect(
        m_server, &qevercloud::NoteStoreServer::getResourceRequest,
        this, &NoteStoreServer::onGetResourceRequest);

    QObject::connect(
        m_server,
        &qevercloud::NoteStoreServer::authenticateToSharedNotebookRequest,
        this, &NoteStoreServer::onAuthenticateToSharedNotebookRequest);

    // 3. Connect local ready signals to QEverCloud server's slots
    // TODO: implement
}

} // namespace quentier::synchronization::tests
