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

#pragma once

#include <qevercloud/Fwd.h>
#include <qevercloud/types/User.h>

#include <QHash>
#include <QList>
#include <QNetworkCookie>
#include <QObject>
#include <QUuid>

#include <exception>
#include <optional>
#include <variant>

class QTcpServer;
class QTcpSocket;

namespace quentier::synchronization::tests {

class FakeUserStoreBackend;

class UserStoreServer : public QObject
{
    Q_OBJECT
public:
    UserStoreServer(FakeUserStoreBackend * backend, QObject * parent = nullptr);

    ~UserStoreServer() override;

    [[nodiscard]] quint16 port() const noexcept;

    // private signals
Q_SIGNALS:
    void checkVersionRequestReady(
        bool value, std::exception_ptr e, QUuid requestId);

    void getUserRequestReady(
        qevercloud::User value, std::exception_ptr e, QUuid requestId);

private Q_SLOTS:
    void onRequestReady(const QByteArray & responseData, QUuid requestId);

private:
    void connectToQEverCloudServer();

private:
    FakeUserStoreBackend * m_backend;

    QTcpServer * m_tcpServer = nullptr;
    qevercloud::UserStoreServer * m_server = nullptr;
    QHash<QUuid, QTcpSocket *> m_sockets;
};

} // namespace quentier::synchronization::tests
