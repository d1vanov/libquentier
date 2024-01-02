/*
 * Copyright 2016-2024 Dmitry Ivanov
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

#pragma once

#include <QtWebChannel/QWebChannelAbstractTransport>

class QWebSocket;

class WebSocketTransport final : public QWebChannelAbstractTransport
{
    Q_OBJECT
public:
    explicit WebSocketTransport(QWebSocket * socket);
    ~WebSocketTransport() override;

    void sendMessage(const QJsonObject & message) override;

private Q_SLOTS:
    void textMessageReceived(const QString & message);

private:
    [[nodiscard]] bool parseMessage(
        QByteArray messageData, QJsonObject & object);

private:
    QWebSocket * m_socket;
};
