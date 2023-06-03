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

#include "Authenticator.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/RuntimeError.h>
#include <quentier/threading/Future.h>
#include <quentier/threading/Post.h>
#include <quentier/threading/TrackedTask.h>

#include <synchronization/types/AuthenticationInfo.h>

#if !QEVERCLOUD_HAS_OAUTH
#error "The used QEverCloud library has no OAuth support"
#endif

#include <qevercloud/QEverCloudOAuth.h>

#include <QThread>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include <quentier/threading/Qt5Promise.h>
#endif

#include <stdexcept>

namespace quentier::synchronization {

Authenticator::Authenticator(
    QString consumerKey, QString consumerSecret, QString host,
    threading::QThreadPtr uiThread, QWidget * parentWidget) :
    m_consumerKey{std::move(consumerKey)},
    m_consumerSecret{std::move(consumerSecret)}, m_host{std::move(host)},
    m_uiThread{std::move(uiThread)}, m_parentWidget{parentWidget}
{
    if (Q_UNLIKELY(m_host.isEmpty())) {
        throw InvalidArgument{
            ErrorString{QStringLiteral("Authenticator ctor: host is empty")}};
    }

    if (Q_UNLIKELY(!m_uiThread)) {
        throw InvalidArgument{ErrorString{
            QStringLiteral("Authenticator ctor: UI thread is empty")}};
    }
}

QFuture<IAuthenticationInfoPtr> Authenticator::authenticateNewAccount()
{
    if (QThread::currentThread() == m_uiThread.get()) {
        return threading::makeReadyFuture<IAuthenticationInfoPtr>(
            authenticateNewAccountImpl());
    }

    auto promise = std::make_shared<QPromise<IAuthenticationInfoPtr>>();
    auto future = promise->future();
    promise->start();

    auto selfWeak = weak_from_this();

    threading::postToThread(
        m_uiThread.get(),
        threading::TrackedTask{
            selfWeak, [this, promise = std::move(promise)] {
                try {
                    auto result = authenticateNewAccountImpl();
                    promise->addResult(std::move(result));
                }
                catch (const QException & e) {
                    promise->setException(e);
                }
                catch (const std::exception & e) {
                    promise->setException(RuntimeError{ErrorString{
                        QString::fromUtf8(
                            "Error authenticating new account: %1")
                            .arg(QString::fromStdString(e.what()))}});
                }
                catch (...) {
                    promise->setException(RuntimeError{ErrorString{
                        QStringLiteral("Unknown error while authenticating new "
                                       "account")}});
                }

                promise->finish();
            }});

    return future;
}

QFuture<IAuthenticationInfoPtr> Authenticator::authenticateAccount(
    [[maybe_unused]] Account account) // NOLINT
{
    // Currently QEverCloud doesn't have the ability to authenticate any
    // particular account so just authenticating the account as a new one
    return authenticateNewAccount();
}

IAuthenticationInfoPtr Authenticator::authenticateNewAccountImpl()
{
    auto pDialog = std::make_unique<qevercloud::EvernoteOAuthDialog>(
        m_consumerKey, m_consumerSecret, m_host, m_parentWidget);

    pDialog->setWindowModality(Qt::WindowModal);

    if (pDialog->exec() == QDialog::Accepted) {
        auto result = pDialog->oauthResult();

        auto authenticationInfo = std::make_shared<AuthenticationInfo>();
        authenticationInfo->m_userId = result.userId;
        authenticationInfo->m_authToken = std::move(result.authenticationToken);
        authenticationInfo->m_authTokenExpirationTime = result.expires;
        authenticationInfo->m_shardId = std::move(result.shardId);
        authenticationInfo->m_noteStoreUrl = std::move(result.noteStoreUrl);
        authenticationInfo->m_webApiUrlPrefix =
            std::move(result.webApiUrlPrefix);

        authenticationInfo->m_userStoreCookies = std::move(result.cookies);
        return authenticationInfo;
    }

    throw RuntimeError{ErrorString{
        QStringLiteral("Cannot authenticate to Evernote")}};
}

} // namespace quentier::synchronization
