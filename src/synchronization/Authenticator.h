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

#include <quentier/synchronization/IAuthenticator.h>
#include <quentier/threading/Fwd.h>

#include <QString>
#include <QUrl>

#include <memory>

QT_BEGIN_NAMESPACE;

class QWidget;

QT_END_NAMESPACE;

namespace quentier::synchronization {

class Authenticator final :
    public IAuthenticator,
    public std::enable_shared_from_this<Authenticator>
{
public:
    Authenticator(
        QString consumerKey, QString consumerSecret, QUrl serverUrl,
        threading::QThreadPtr uiThread, QWidget * parentWidget = nullptr);

public: // IAuthenticator
    [[nodiscard]] QFuture<IAuthenticationInfoPtr>
        authenticateNewAccount() override;

    [[nodiscard]] QFuture<IAuthenticationInfoPtr> authenticateAccount(
        Account account) override;

private:
    [[nodiscard]] IAuthenticationInfoPtr authenticateNewAccountImpl();

private:
    const QString m_consumerKey;
    const QString m_consumerSecret;
    const QUrl m_serverUrl;
    const threading::QThreadPtr m_uiThread;
    QWidget * m_parentWidget;
};

} // namespace quentier::synchronization
