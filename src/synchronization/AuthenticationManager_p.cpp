/*
 * Copyright 2017-2020 Dmitry Ivanov
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

#if !QEVERCLOUD_HAS_OAUTH
#error "The used QEverCloud library has no OAuth support"
#endif

#include <qt5qevercloud/QEverCloudOAuth.h>

#include <memory>

namespace quentier {

AuthenticationManagerPrivate::AuthenticationManagerPrivate(
    const QString & consumerKey, const QString & consumerSecret,
    const QString & host, QObject * parent) :
    QObject(parent),
    m_consumerKey(consumerKey), m_consumerSecret(consumerSecret), m_host(host)
{}

void AuthenticationManagerPrivate::onAuthenticationRequest()
{
    QNDEBUG(
        "synchronization:authentication",
        "AuthenticationManagerPrivate::onAuthenticationRequest");

    QWidget * pParentWidget = qobject_cast<QWidget *>(parent());

    auto pDialog = std::make_unique<qevercloud::EvernoteOAuthDialog>(
        m_consumerKey, m_consumerSecret, m_host, pParentWidget);
    pDialog->setWindowModality(Qt::WindowModal);

    auto res = pDialog->exec();
    if (res == QDialog::Accepted) {
        auto result = pDialog->oauthResult();
        Q_EMIT sendAuthenticationResult(
            /* success = */ true, result.userId, result.authenticationToken,
            result.expires, result.shardId, result.noteStoreUrl,
            result.webApiUrlPrefix, result.cookies, ErrorString());
    }
    else {
        ErrorString errorDescription(
            QT_TR_NOOP("Can't authenticate to Evernote"));
        errorDescription.details() = pDialog->oauthError();

        Q_EMIT sendAuthenticationResult(
            /* success = */ false, qevercloud::UserID(-1), {},
            qevercloud::Timestamp(0), {}, {}, {}, {}, errorDescription);
    }
}

} // namespace quentier
