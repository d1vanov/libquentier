/*
 * Copyright 2017 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_SYNCHRONIZATION_AUTHENTICATION_MANAGER_PRIVATE_H
#define LIB_QUENTIER_SYNCHRONIZATION_AUTHENTICATION_MANAGER_PRIVATE_H

#include <quentier/synchronization/AuthenticationManager.h>

#ifdef _MSC_VER
// God, MSVC is such a piece of CRAP! See https://msdn.microsoft.com/en-us/library/h5sh3k99.aspx
#define true 1
#define false 0
#endif

#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)

#include <qt5qevercloud/VersionInfo.h>

#if !QEVERCLOUD_HAS_OAUTH
#error "The used QEverCloud library has no OAuth support"
#endif

#ifdef _MSC_VER
#undef true
#undef false
#endif

#include <qt5qevercloud/QEverCloudOAuth.h>

#else // QT_VERSION < QT_VERSION_CHECK(5, 0, 0)

#include <qt4qevercloud/VersionInfo.h>
#if !QEVERCLOUD_HAS_OAUTH
#error "The used QEverCloud library has no OAuth support"
#endif

#ifdef _MSC_VER
#undef true
#undef false
#endif

#include <qt4qevercloud/QEverCloudOAuth.h>

#endif // QT_VERSION

namespace quentier {

class AuthenticationManagerPrivate: public QObject
{
    Q_OBJECT
public:
    explicit AuthenticationManagerPrivate(const QString & consumerKey, const QString & consumerSecret,
                                          const QString & host, QObject * parent = Q_NULLPTR);

Q_SIGNALS:
    void sendAuthenticationResult(bool success, qevercloud::UserID userId, QString authToken,
                                  qevercloud::Timestamp authTokenExpirationTime, QString shardId,
                                  QString noteStoreUrl, QString webApiUrlPrefix, ErrorString errorDescription);

public Q_SLOTS:
    void onAuthenticationRequest();

private Q_SLOTS:
    void onOAuthResult(bool result);
    void onOAuthSuccess();
    void onOAuthFailure();

private:
    Q_DISABLE_COPY(AuthenticationManagerPrivate)

private:
    QString         m_consumerKey;
    QString         m_consumerSecret;
    QString         m_host;

    qevercloud::EvernoteOAuthWebView        m_OAuthWebView;
};

} // namespace quentier

#endif // LIB_QUENTIER_SYNCHRONIZATION_AUTHENTICATION_MANAGER_PRIVATE_H
