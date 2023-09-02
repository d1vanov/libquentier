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

#pragma once

#include <qevercloud/Fwd.h>
#include <qevercloud/types/User.h>

#include <QHash>
#include <QList>
#include <QNetworkCookie>
#include <QObject>

#include <exception>
#include <optional>
#include <variant>

QT_BEGIN_NAMESPACE

class QTcpServer;
class QTcpSocket;

QT_END_NAMESPACE

namespace quentier::synchronization::tests {

class UserStoreServer : public QObject
{
    Q_OBJECT
public:
    UserStoreServer(
        QString authenticationToken, QList<QNetworkCookie> cookies,
        QObject * parent = nullptr);

    ~UserStoreServer() override;

    [[nodiscard]] quint16 port() const noexcept;

    [[nodiscard]] qint16 edamVersionMajor() const noexcept;
    void setEdamVersionMajor(qint16 edamVersionMajor) noexcept;

    [[nodiscard]] qint16 edamVersionMinor() const noexcept;
    void setEdamVersionMinor(qint16 edamVersionMinor) noexcept;

    using UserOrException = std::variant<qevercloud::User, std::exception_ptr>;

    [[nodiscard]] std::optional<UserOrException> findUser(
        const QString & authenticationToken) const;

    void putUser(const QString & authenticationToken, qevercloud::User user);
    void putUserException(
        const QString & authenticationToken, std::exception_ptr e);

    void removeUser(const QString & authenticationToken);

    // private signals
Q_SIGNALS:
    void checkVersionRequestReady(bool value, std::exception_ptr e);
    void getUserRequestReady(qevercloud::User value, std::exception_ptr e);

private Q_SLOTS:
    void onCheckVersionRequest(
        const QString & clientName, qint16 edamVersionMajor,
        qint16 edamVersionMinor, const qevercloud::IRequestContextPtr & ctx);

    void onGetUserRequest(const qevercloud::IRequestContextPtr & ctx);
    void onRequestReady(const QByteArray & responseData);

private:
    void connectToQEverCloudServer();

    [[nodiscard]] std::exception_ptr checkAuthentication(
        const qevercloud::IRequestContextPtr & ctx) const;

private:
    const QString m_authenticationToken;
    const QList<QNetworkCookie> m_cookies;

    QTcpServer * m_tcpServer = nullptr;
    QTcpSocket * m_tcpSocket = nullptr;
    qevercloud::UserStoreServer * m_server = nullptr;

    qint16 m_edamVersionMajor = 0;
    qint16 m_edamVersionMinor = 0;

    QHash<QString, UserOrException> m_users;
};

} // namespace quentier::synchronization::tests
