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

#include "AuthenticationManager_p.h"
#include <quentier/logging/QuentierLogger.h>

namespace quentier {

AuthenticationManagerPrivate::AuthenticationManagerPrivate(const QString & consumerKey, const QString & consumerSecret,
                                                           const QString & host, QObject * parent) :
    QObject(parent),
    m_consumerKey(consumerKey),
    m_consumerSecret(consumerSecret),
    m_host(host),
    m_OAuthWebView()
{
    // Connections with OAuth handler
    QObject::connect(&m_OAuthWebView, QNSIGNAL(qevercloud::EvernoteOAuthWebView,authenticationFinished,bool),
                     this, QNSLOT(AuthenticationManagerPrivate,onOAuthResult,bool));
    QObject::connect(&m_OAuthWebView, QNSIGNAL(qevercloud::EvernoteOAuthWebView,authenticationSuceeded),
                     this, QNSLOT(AuthenticationManagerPrivate,onOAuthSuccess));
    QObject::connect(&m_OAuthWebView, QNSIGNAL(qevercloud::EvernoteOAuthWebView,authenticationFailed),
                     this, QNSLOT(AuthenticationManagerPrivate,onOAuthFailure));
}

void AuthenticationManagerPrivate::onOAuthResult(bool result)
{
    QNDEBUG(QStringLiteral("AuthenticationManagerPrivate::onOAuthResult: result = ")
            << (result ? QStringLiteral("true") : QStringLiteral("false")));

    if (result) {
        onOAuthSuccess();
    }
    else {
        onOAuthFailure();
    }
}

void AuthenticationManagerPrivate::onOAuthSuccess()
{
    QNDEBUG(QStringLiteral("AuthenticationManagerPrivate::onOAuthSuccess"));

    qevercloud::EvernoteOAuthDialog::OAuthResult result = m_OAuthWebView.oauthResult();
    emit sendAuthenticationResult(/* success = */ true, result.userId, result.authenticationToken,
                                  result.expires, result.shardId, result.noteStoreUrl,
                                  result.webApiUrlPrefix, ErrorString());
}

void AuthenticationManagerPrivate::onOAuthFailure()
{
    QNDEBUG(QStringLiteral("AuthenticationManagerPrivate::onOAuthFailure: ") << m_OAuthWebView.oauthError());

    ErrorString errorDescription(QT_TRANSLATE_NOOP("", "Authentication failed"));
    errorDescription.details() = m_OAuthWebView.oauthError();
    emit sendAuthenticationResult(/* success = */ false, qevercloud::UserID(-1), QString(),
                                  qevercloud::Timestamp(0), QString(), QString(), QString(),
                                  errorDescription);
}

void AuthenticationManagerPrivate::onAuthenticationRequest()
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::onAuthenticationRequest"));
    m_OAuthWebView.authenticate(m_host, m_consumerKey, m_consumerSecret);
}

} // namespace quentier
