/*
 * Copyright 2022-2025 Dmitry Ivanov
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

#include "ProtocolVersionChecker.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/RuntimeError.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/threading/Future.h>
#include <quentier/utility/SysInfo.h>

#include <qevercloud/Constants.h>
#include <qevercloud/IRequestContext.h>
#include <qevercloud/services/IUserStore.h>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include <quentier/threading/Qt5Promise.h>
#endif

#include <QCoreApplication>
#include <QMutexLocker>
#include <QThread>

#include <memory>

namespace quentier::synchronization {

namespace {

[[nodiscard]] QString clientNameForProtocolVersionCheck()
{
    QString clientName = QCoreApplication::applicationName();
    clientName += QStringLiteral("/");
    clientName += QCoreApplication::applicationVersion();
    clientName += QStringLiteral("; ");

    utility::SysInfo sysInfo;
    QString platformName = sysInfo.platformName();
    clientName += platformName;

    return clientName;
}

} // namespace

ProtocolVersionChecker::ProtocolVersionChecker(
    qevercloud::IUserStorePtr userStore) : m_userStore{std::move(userStore)}
{
    if (Q_UNLIKELY(!m_userStore)) {
        throw InvalidArgument{ErrorString{
            QStringLiteral("ProtocolVersionChecker ctor: user store is null")}};
    }
}

QFuture<void> ProtocolVersionChecker::checkProtocolVersion(
    const IAuthenticationInfo & authenticationInfo)
{
    QNDEBUG(
        "synchronization::ProtocolVersionChecker",
        "ProtocolVersionChecker::checkProtocolVersion");

    const QMutexLocker locker{&m_mutex};
    if (m_future) {
        QNDEBUG(
            "synchronization::ProtocolVersionChecker",
            "Already tried to check once, returning existing future");
        return *m_future;
    }

    constexpr qint64 userStoreRequestTimeoutMsec = 5000;

    auto ctx = qevercloud::newRequestContext(
        authenticationInfo.authToken(), userStoreRequestTimeoutMsec,
        qevercloud::gDefaultConnectionTimeoutExponentialIncrease,
        qevercloud::gDefaultMaxConnectionTimeoutMsec,
        qevercloud::gDefaultMaxRequestRetryCount,
        authenticationInfo.userStoreCookies());

    auto clientName = clientNameForProtocolVersionCheck();

    auto promise = std::make_shared<QPromise<void>>();
    m_future = promise->future();

    promise->start();

    auto protocolVersionFuture = m_userStore->checkVersionAsync(
        std::move(clientName), qevercloud::EDAM_VERSION_MAJOR,
        qevercloud::EDAM_VERSION_MINOR, std::move(ctx));

    auto * currentThread = QThread::currentThread();

    threading::thenOrFailed(
        std::move(protocolVersionFuture), currentThread, promise,
        [promise](bool res) {
            QNDEBUG(
                "synchronization::ProtocolVersionChecker",
                "Protocol version check completed: "
                    << (res ? "success" : "failure"));

            if (!res) {
                promise->setException(RuntimeError{ErrorString{QStringLiteral(
                    "Protocol version check failed: protocol used by the "
                    "app is too old to communicate with Evernote")}});
            }

            promise->finish();
        });

    return *m_future;
}

} // namespace quentier::synchronization
