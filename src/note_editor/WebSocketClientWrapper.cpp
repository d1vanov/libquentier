/*
 * Copyright 2016-2020 Dmitry Ivanov
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

#include "WebSocketClientWrapper.h"
#include "WebSocketTransport.h"

#include <QtWebSockets/QWebSocketServer>

WebSocketClientWrapper::WebSocketClientWrapper(
    QWebSocketServer * server, QObject * parent) :
    QObject(parent),
    m_server(server)
{
    QObject::connect(
        server, &QWebSocketServer::newConnection, this,
        &WebSocketClientWrapper::handleNewConnection);
}

void WebSocketClientWrapper::handleNewConnection()
{
    Q_EMIT clientConnected(
        new WebSocketTransport(m_server->nextPendingConnection()));
}
